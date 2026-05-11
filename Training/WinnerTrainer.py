import os
import shutil
from glob import glob
import lightning.pytorch as pl
import torch
from lightning.pytorch.loggers import TensorBoardLogger
from torch import nn
from torch.utils.data import DataLoader
from lightning.pytorch.callbacks import ModelCheckpoint, EarlyStopping, Callback
from lightning.fabric.utilities.rank_zero import rank_zero_only
from torchmetrics import Accuracy, MeanMetric, ConfusionMatrix
import torch.nn.functional as F
from lightning.pytorch.loggers import WandbLogger
import WinnerDataset
from chessmimic_core import (
    MOVE_TO_ACTION,
    INPUT_VOCAB_SIZE
)
import pickle
import math


class WindowedTrainMetrics(Callback):
    """Accumulates train metrics over a fixed number of steps (window) and logs once."""

    def __init__(self, window_size: int):
        super().__init__()
        self.window_size = window_size
        self.loss_metric = MeanMetric()

    @staticmethod
    def _extract_loss(outputs):
        if outputs is None:
            return None
        if torch.is_tensor(outputs):
            return outputs
        if isinstance(outputs, dict) and "loss" in outputs and torch.is_tensor(outputs["loss"]):
            return outputs["loss"]
        return None

    @staticmethod
    def _move_metric_to_device(metric: torch.nn.Module, device: torch.device):
        # TorchMetrics returns CPU by default (no params). Only move when necessary.
        if next(metric.parameters(), None) is None:  # Metric has no parameters
            # create a dummy parameter so .to(device) works consistently
            metric.add_state("_dummy_param", default=torch.tensor(0.0), persistent=False)
        metric.to(device)

    def on_train_batch_end(self, trainer, pl_module, outputs, batch, batch_idx):
        loss_val = self._extract_loss(outputs)
        if loss_val is None:
            return
        device = loss_val.device
        # ensure metrics live on the correct device
        if self.loss_metric.device != device:
            self._move_metric_to_device(self.loss_metric, device)
        self.loss_metric.update(loss_val.detach())

        if self.window_size > 0 and (trainer.global_step + 1) % self.window_size == 0:
            mean_loss = self.loss_metric.compute()
            pl_module.log("train/loss_window", mean_loss, on_step=True, prog_bar=True, logger=True)
            self.loss_metric.reset()


class BrierLoss(nn.Module):
    """Brier loss for multi-class classification.
    
    Computes the mean squared difference between predicted probabilities 
    and one-hot encoded targets.
    """
    def __init__(self):
        super().__init__()
        
    @staticmethod
    def forward(logits, targets):
        """
        Args:
            logits (torch.Tensor): Predicted logits of shape (batch_size, num_classes)
            targets (torch.Tensor): True class indices of shape (batch_size,)
        
        Returns:
            torch.Tensor: Scalar Brier loss
        """
        # Convert logits to probabilities
        probs = F.softmax(logits, dim=-1)
        
        # Convert targets to one-hot encoding
        num_classes = logits.shape[1]
        one_hot = F.one_hot(targets, num_classes=num_classes).float()
        
        # Compute squared differences
        squared_diff = (probs - one_hot) ** 2
        
        # Sum over classes and average over batch
        brier_score = torch.mean(torch.sum(squared_diff, dim=-1))
        
        return brier_score


class MlpBlock(pl.LightningModule):
    def __init__(self, input_size, hidden_size, output_size):
        super(MlpBlock, self).__init__()
        self.layer_norm = nn.LayerNorm(input_size)
        self.split1_linear = nn.Linear(input_size, hidden_size, bias=False)
        self.split2_linear = nn.Linear(input_size, hidden_size, bias=False)
        self.activation = nn.SiLU()
        self.join_linear = nn.Linear(hidden_size, output_size, bias=False)

    def forward(self, x):
        x = self.layer_norm(x)
        split_1 = self.split1_linear(x)
        split_2 = self.split2_linear(x)
        x = self.activation(split_1) * split_2
        x = self.join_linear(x)

        return x


class AttentionBlock(pl.LightningModule):
    def __init__(self, input_size, num_heads):
        super(AttentionBlock, self).__init__()
        self.layer_norm = nn.LayerNorm(input_size)
        self.self_attention = nn.MultiheadAttention(embed_dim=input_size, num_heads=num_heads, batch_first=True, )

    def forward(self, x):
        x = self.layer_norm(x)
        x = self.self_attention(query=x, key=x, value=x, need_weights=False)[0]
        return x


class WinnerPatzerModel(pl.LightningModule):
    """Winner prediction model based on PatzerModel architecture with 3-class output."""
    
    def __init__(
            self,
            recent_moves_sequence_length,
            recent_moves_vocab_size,
            board_sequence_length,
            board_input_vocab_size,
            embedding_dim,
            widening_factor,
            num_layers,
            num_heads):
        super().__init__()
        self.save_hyperparameters()

        self.recent_moves_sequence_length = recent_moves_sequence_length
        self.recent_moves_vocab_size = recent_moves_vocab_size
        self.board_sequence_length = board_sequence_length
        self.input_vocab_size = board_input_vocab_size
        self.embedding_dim = embedding_dim
        self.widening_factor = widening_factor
        self.num_layers = num_layers
        self.num_heads = num_heads

        # Embeddings
        self.board_embedding = nn.Embedding(board_input_vocab_size, embedding_dim)
        self.recent_moves_embedding = nn.Embedding(recent_moves_vocab_size, embedding_dim)
        
        # Winner features embedding (5D: white_rating, black_rating, white_clock, black_clock, increment)
        self.winner_features_embedding = nn.Linear(5, embedding_dim)
        
        self.learned_positional_encoding = nn.Parameter(
            torch.randn(recent_moves_sequence_length + board_sequence_length + 1, embedding_dim))  # +1 for features

        # Transformer blocks
        hidden_size = embedding_dim * widening_factor
        self.mlp_blocks = nn.ModuleList([MlpBlock(embedding_dim, hidden_size, embedding_dim) for _ in range(num_layers)])
        self._attention_blocks = nn.ModuleList([AttentionBlock(embedding_dim, num_heads) for _ in range(num_layers)])
        self.layer_norm = nn.LayerNorm(embedding_dim)
        
        # 3-class output head (black win, draw, white win)
        self.winner_head = nn.Linear(embedding_dim, 3)

    def forward(self, input_ids, attention_mask, features):
        # Split input_ids into recent moves and board
        recent_moves = input_ids[:, :self.recent_moves_sequence_length]
        board = input_ids[:, self.recent_moves_sequence_length:]
        
        recent_moves = self.recent_moves_embedding(recent_moves)
        board = self.board_embedding(board)
        
        # Process winner features (5D vector)
        features = features.to(dtype=self.dtype)
        embedded_features = self.winner_features_embedding(features)
        embedded_features = embedded_features.unsqueeze(1)
        
        # Concatenate all inputs: recent moves, features, and board
        x = torch.cat([recent_moves, embedded_features, board], dim=1)
        
        x = x + self.learned_positional_encoding
        for mlp_block, attention_block in zip(self.mlp_blocks, self._attention_blocks):
            x = x + attention_block(x)
            x = x + mlp_block(x)

        x = self.layer_norm(x)
        x = x[:, -1, :]  # Only take the last token (CLS token)
        
        # Generate 3-class logits (no activation here, applied in loss)
        output = self.winner_head(x)

        return output


class LightningModel(pl.LightningModule):
    def __init__(self, winner_model, 
                 val_check_interval: int = 25000, learning_rate=4e-4,
                 dataset_size=None, batch_size=None, use_one_cycle=False):
        super().__init__()
        self.model = winner_model
        self.val_check_interval = val_check_interval
        self.learning_rate = learning_rate
        self.dataset_size = dataset_size
        self.batch_size = batch_size
        self.use_one_cycle = use_one_cycle
        
        # Use Brier loss for 3-class classification
        self.loss = BrierLoss()

        # Overall 3-class accuracy
        self.train_accuracy = Accuracy(task="multiclass", num_classes=3)
        self.val_accuracy = Accuracy(task="multiclass", num_classes=3)
        
        # Per-class binary accuracy metrics
        self.train_black_accuracy = Accuracy(task="binary")
        self.train_draw_accuracy = Accuracy(task="binary")
        self.train_white_accuracy = Accuracy(task="binary")
        
        self.val_black_accuracy = Accuracy(task="binary")
        self.val_draw_accuracy = Accuracy(task="binary")
        self.val_white_accuracy = Accuracy(task="binary")
        
        # Confusion matrix for detailed analysis
        self.train_confusion = ConfusionMatrix(task="multiclass", num_classes=3)
        self.val_confusion = ConfusionMatrix(task="multiclass", num_classes=3)

        self.lr_scheduler = None
        self.last_lr = None
        
        # Initialize metrics with dummy data to avoid warnings during sanity check
        self._initialize_metrics()

    def _initialize_metrics(self):
        """Initialize metrics with dummy data to avoid warnings during sanity check."""
        # Create minimal dummy data
        dummy_preds_multiclass = torch.tensor([0, 1, 2])
        dummy_targets_multiclass = torch.tensor([0, 1, 2])
        dummy_preds_binary = torch.tensor([0, 1])
        dummy_targets_binary = torch.tensor([0, 1])

        # Initialize train metrics
        self.train_accuracy.update(dummy_preds_multiclass, dummy_targets_multiclass)
        self.train_black_accuracy.update(dummy_preds_binary, dummy_targets_binary)
        self.train_draw_accuracy.update(dummy_preds_binary, dummy_targets_binary)
        self.train_white_accuracy.update(dummy_preds_binary, dummy_targets_binary)
        self.train_confusion.update(dummy_preds_multiclass, dummy_targets_multiclass)
        
        # Initialize val metrics
        self.val_accuracy.update(dummy_preds_multiclass, dummy_targets_multiclass)
        self.val_black_accuracy.update(dummy_preds_binary, dummy_targets_binary)
        self.val_draw_accuracy.update(dummy_preds_binary, dummy_targets_binary)
        self.val_white_accuracy.update(dummy_preds_binary, dummy_targets_binary)
        self.val_confusion.update(dummy_preds_multiclass, dummy_targets_multiclass)
        
        # Reset all metrics to clear the dummy data
        self.train_accuracy.reset()
        self.train_black_accuracy.reset()
        self.train_draw_accuracy.reset()
        self.train_white_accuracy.reset()
        self.train_confusion.reset()
        
        self.val_accuracy.reset()
        self.val_black_accuracy.reset()
        self.val_draw_accuracy.reset()
        self.val_white_accuracy.reset()
        self.val_confusion.reset()

    def forward(self, input_ids, attention_mask, features):
        return self.model(input_ids, attention_mask, features)

    def _compute_metrics(self, logits, targets, prefix):
        """Compute and log metrics for a batch.
        
        Args:
            logits: Model output logits (batch_size, 3)
            targets: Class indices (batch_size,)
            prefix: 'train' or 'val'
        """
        # Get predicted classes from logits
        pred_classes = logits.argmax(dim=-1)
        
        # Get metrics based on prefix
        if prefix == "train":
            accuracy = self.train_accuracy
            confusion = self.train_confusion
            black_accuracy = self.train_black_accuracy
            draw_accuracy = self.train_draw_accuracy
            white_accuracy = self.train_white_accuracy
        else:
            accuracy = self.val_accuracy
            confusion = self.val_confusion
            black_accuracy = self.val_black_accuracy
            draw_accuracy = self.val_draw_accuracy
            white_accuracy = self.val_white_accuracy
        
        # Overall 3-class accuracy
        accuracy(pred_classes, targets)
        confusion(pred_classes, targets)
        
        # Per-class binary accuracy (was this outcome predicted correctly?)
        # Black wins (class 0)
        black_pred_binary = (pred_classes == 0).long()
        black_target_binary = (targets == 0).long()
        black_accuracy(black_pred_binary, black_target_binary)
        
        # Draws (class 1)
        draw_pred_binary = (pred_classes == 1).long()
        draw_target_binary = (targets == 1).long()
        draw_accuracy(draw_pred_binary, draw_target_binary)
        
        # White wins (class 2)
        white_pred_binary = (pred_classes == 2).long()
        white_target_binary = (targets == 2).long()
        white_accuracy(white_pred_binary, white_target_binary)

    def training_step(self, batch, batch_idx):
        logits = self(
            batch['input_ids'], 
            batch['attention_mask'], 
            batch['features']
        )
        
        # Brier loss expects integer class indices
        targets = batch['winner']
        loss = self.loss(logits, targets)
        
        # Store loss history for spike detection
        if not hasattr(self, 'loss_history'):
            self.loss_history = []
        
        current_loss = loss.item()
        self.loss_history.append(current_loss)
        
        # Keep only recent history (last 100 steps)
        if len(self.loss_history) > 100:
            self.loss_history.pop(0)
        
        # Log losses
        self.log("train/loss_step", loss, on_step=True, on_epoch=False, sync_dist=False, prog_bar=True, logger=True)
        self.log("train/loss_epoch", loss, on_step=False, on_epoch=True, sync_dist=False, prog_bar=False, logger=True)
        
        # Compute metrics using logits directly
        self._compute_metrics(logits, targets, "train")
        
        # Log metrics
        self.log('train/accuracy_step', self.train_accuracy, prog_bar=True)
        self.log('train/black_accuracy_step', self.train_black_accuracy)
        self.log('train/draw_accuracy_step', self.train_draw_accuracy)
        self.log('train/white_accuracy_step', self.train_white_accuracy)

        if self.lr_scheduler is not None:
            current_lr = self.lr_scheduler.get_last_lr()[0]
            self.log('lr', current_lr)
            # Only print LR changes for ReduceLROnPlateau to avoid spam with OneCycleLR
            if isinstance(self.lr_scheduler, torch.optim.lr_scheduler.ReduceLROnPlateau):
                if current_lr != self.last_lr:
                    print(f"Learning rate changed from {self.last_lr} to {current_lr}")
            self.last_lr = current_lr

        return loss

    def validation_step(self, batch, batch_idx):
        logits = self(
            batch['input_ids'], 
            batch['attention_mask'], 
            batch['features']
        )
        
        # Brier loss expects integer class indices
        targets = batch['winner']
        loss = self.loss(logits, targets)
        
        # Log losses
        self.log("val/loss", loss, on_step=True, on_epoch=True, sync_dist=False, prog_bar=True)
        
        # Compute metrics using logits directly
        self._compute_metrics(logits, targets, "val")
        
        # Log metrics for validation step
        self.log('val/accuracy_step', self.val_accuracy)
        self.log('val/black_accuracy_step', self.val_black_accuracy)
        self.log('val/draw_accuracy_step', self.val_draw_accuracy)
        self.log('val/white_accuracy_step', self.val_white_accuracy)

        return loss

    def on_train_epoch_end(self):
        # Log epoch metrics
        self.log('train/accuracy_epoch', self.train_accuracy.compute(), sync_dist=False)
        self.log('train/black_accuracy_epoch', self.train_black_accuracy.compute(), sync_dist=False)
        self.log('train/draw_accuracy_epoch', self.train_draw_accuracy.compute(), sync_dist=False)
        self.log('train/white_accuracy_epoch', self.train_white_accuracy.compute(), sync_dist=False)
        
        # Log confusion matrix (handle multiple loggers)
        cm = self.train_confusion.compute()
        if hasattr(self, 'logger') and self.logger is not None:
            if hasattr(self.logger, 'experiment'):
                # Single logger
                if hasattr(self.logger.experiment, 'add_text'):
                    self.logger.experiment.add_text('train/confusion_matrix', str(cm), self.current_epoch)
            elif hasattr(self.logger, '__iter__'):
                # Multiple loggers
                for logger in self.logger:
                    if hasattr(logger, 'experiment') and hasattr(logger.experiment, 'add_text'):
                        logger.experiment.add_text('train/confusion_matrix', str(cm), self.current_epoch)
        
        # Reset metrics
        self.train_accuracy.reset()
        self.train_black_accuracy.reset()
        self.train_draw_accuracy.reset()
        self.train_white_accuracy.reset()
        self.train_confusion.reset()

    def on_validation_epoch_end(self):
        # Log epoch metrics
        self.log('val/accuracy_epoch', self.val_accuracy.compute(), sync_dist=False)
        self.log('val/black_accuracy_epoch', self.val_black_accuracy.compute(), sync_dist=False)
        self.log('val/draw_accuracy_epoch', self.val_draw_accuracy.compute(), sync_dist=False)
        self.log('val/white_accuracy_epoch', self.val_white_accuracy.compute(), sync_dist=False)
        
        # Log confusion matrix (handle multiple loggers)
        cm = self.val_confusion.compute()
        if hasattr(self, 'logger') and self.logger is not None:
            if hasattr(self.logger, 'experiment'):
                # Single logger
                if hasattr(self.logger.experiment, 'add_text'):
                    self.logger.experiment.add_text('val/confusion_matrix', str(cm), self.current_epoch)
            elif hasattr(self.logger, '__iter__'):
                # Multiple loggers
                for logger in self.logger:
                    if hasattr(logger, 'experiment') and hasattr(logger.experiment, 'add_text'):
                        logger.experiment.add_text('val/confusion_matrix', str(cm), self.current_epoch)
        
        # Reset metrics
        self.val_accuracy.reset()
        self.val_black_accuracy.reset()
        self.val_draw_accuracy.reset()
        self.val_white_accuracy.reset()
        self.val_confusion.reset()

    def configure_optimizers(self):
        optimizer = torch.optim.Adam(self.parameters(), lr=self.learning_rate, fused=True)
        
        # Use OneCycleLR only when fine-tuning
        if self.use_one_cycle and self.dataset_size is not None and self.batch_size is not None:
            # Use OneCycleLR for fine-tuning (typically single epoch)
            self.lr_scheduler = torch.optim.lr_scheduler.OneCycleLR(
                optimizer,
                max_lr=self.learning_rate * 10,  # Peak LR is 10x base
                total_steps=self.trainer.estimated_stepping_batches,
                pct_start=0.3,  # 30% warmup
                anneal_strategy='cos',
                div_factor=25,  # Initial LR = max_lr/25
                final_div_factor=10000  # Final LR = max_lr/10000
            )
            
            return {
                "optimizer": optimizer,
                "lr_scheduler": {
                    "scheduler": self.lr_scheduler,
                    "interval": "step",  # Update every step, not epoch
                    "frequency": 1
                }
            }
        else:
            # Use ReduceLROnPlateau for training from scratch
            self.lr_scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
                optimizer, mode="min", factor=0.1, patience=0, threshold=0.001, min_lr=self.learning_rate/100)
            return {
                "optimizer": optimizer,
                "lr_scheduler": {
                    "scheduler": self.lr_scheduler,
                    "monitor": "train/loss_epoch"
                }
            }


def load_pretrained_weights(winner_model, checkpoint_path):
    """Load weights from a pre-trained PatzerModel checkpoint.
    
    Handles architectural differences:
    - Rating embedding -> Winner features embedding (5D)
    - Clock embedding: Removed (incorporated into features)
    - Output layer: Action prediction -> 3-class winner prediction (skipped)
    """
    print(f"Loading pre-trained weights from {checkpoint_path}")
    
    # Load the checkpoint
    checkpoint = torch.load(checkpoint_path, map_location='cpu')
    if 'state_dict' in checkpoint:
        state_dict = checkpoint['state_dict']
    else:
        state_dict = checkpoint
    
    # Handle different prefixes (model.model., model., _orig_mod., or no prefix)
    # Process in order of specificity to handle nested prefixes correctly
    cleaned_state_dict = {}
    for k, v in state_dict.items():
        # Remove prefixes in order
        cleaned_key = k
        if cleaned_key.startswith('model.model.'):
            cleaned_key = cleaned_key.replace('model.model.', '')
        elif cleaned_key.startswith('model.'):
            cleaned_key = cleaned_key.replace('model.', '')
        
        # Also remove _orig_mod. prefix if present
        if cleaned_key.startswith('_orig_mod.'):
            cleaned_key = cleaned_key.replace('_orig_mod.', '')
            
        cleaned_state_dict[cleaned_key] = v
    state_dict = cleaned_state_dict
    
    # Get current model state dict
    current_state = winner_model.state_dict()
    
    # Copy matching weights
    loaded_keys = []
    skipped_keys = []
    special_handling = []
    
    for key, value in state_dict.items():
        if key in current_state:
            if key.startswith('output_layer') or key.startswith('discrete_head') or key.startswith('continuous_') or key.startswith('mode_'):
                # Skip output layers as they're completely different
                skipped_keys.append(f"{key} (output layer)")
            elif current_state[key].shape == value.shape:
                current_state[key] = value
                loaded_keys.append(key)
            else:
                skipped_keys.append(f"{key} (shape mismatch: {value.shape} -> {current_state[key].shape})")
        elif key == 'rating_embedding.weight' and 'winner_features_embedding.weight' in current_state:
            # Special handling: Use rating embedding as initialization for first dimension of features
            # Original is (embedding_dim, 1), new is (embedding_dim, 5)
            pretrained_weights = value.squeeze(-1)  # Shape: (embedding_dim,)
            # Initialize all 5 dimensions with scaled versions of rating embedding
            current_state['winner_features_embedding.weight'][:, 0] = pretrained_weights  # White rating
            current_state['winner_features_embedding.weight'][:, 1] = pretrained_weights  # Black rating  
            current_state['winner_features_embedding.weight'][:, 2] = pretrained_weights * 0.5  # White clock (scaled)
            current_state['winner_features_embedding.weight'][:, 3] = pretrained_weights * 0.5  # Black clock (scaled)
            current_state['winner_features_embedding.weight'][:, 4] = pretrained_weights * 0.1  # Increment (scaled down)
            special_handling.append("rating_embedding -> winner_features_embedding (1D->5D)")
        elif key == 'rating_embedding.bias' and 'winner_features_embedding.bias' in current_state:
            # Copy bias if it exists
            current_state['winner_features_embedding.bias'] = value
            special_handling.append("rating_embedding.bias -> winner_features_embedding.bias")
        elif key.startswith('clock_time_embedding'):
            # Skip clock embedding as it's incorporated into features
            skipped_keys.append(f"{key} (clock embedding removed)")
        else:
            skipped_keys.append(f"{key} (not in winner model)")
    
    # Load the modified state dict
    winner_model.load_state_dict(current_state)
    
    # Print detailed loading summary
    print(f"\nPre-trained checkpoint loading summary:")
    print(f"  Total keys in checkpoint: {len(state_dict)}")
    print(f"  Total keys in winner model: {len(current_state)}")
    print(f"  Loaded {len(loaded_keys)} layers unchanged")
    print(f"  Special handling for {len(special_handling)} layers:")
    for sh in special_handling:
        print(f"    - {sh}")
    
    # Group skipped keys by reason
    output_skipped = [sk for sk in skipped_keys if 'output' in sk or 'discrete' in sk or 'continuous' in sk or 'mode' in sk]
    not_in_model = [sk for sk in skipped_keys if 'not in winner model' in sk]
    clock_skipped = [sk for sk in skipped_keys if 'clock embedding' in sk]
    shape_mismatch = [sk for sk in skipped_keys if 'shape mismatch' in sk and sk not in output_skipped]
    
    print(f"  Skipped {len(skipped_keys)} layers:")
    if output_skipped:
        print(f"    - {len(output_skipped)} output layers (expected - different architecture)")
    if clock_skipped:
        print(f"    - {len(clock_skipped)} clock embedding layers (incorporated into features)")
    if not_in_model:
        print(f"    - {len(not_in_model)} layers not in winner model")
    if shape_mismatch:
        print(f"    - {len(shape_mismatch)} layers with shape mismatch")
        for sm in shape_mismatch[:3]:
            print(f"      {sm}")
    
    # List new layers that will use random initialization
    new_layers = ['winner_head (3 classes)']
    print(f"  New layers with random init: {', '.join(new_layers)}")
    
    # Calculate percentage of transformer layers loaded
    transformer_loaded = sum(1 for k in loaded_keys if 'mlp_blocks' in k or '_attention_blocks' in k)
    total_transformer = sum(1 for k in current_state.keys() if 'mlp_blocks' in k or '_attention_blocks' in k)
    if total_transformer > 0:
        print(f"  Transformer layers: {transformer_loaded}/{total_transformer} loaded ({transformer_loaded/total_transformer*100:.1f}%)")
    
    return winner_model


def main(args):
    print("Loading scalers")
    scalers = pickle.load(open(args.scalers, 'rb'))
    
    print("Loading train dataset")
    train_dataset = WinnerDataset.WinnerDataset(
        bagz_path=args.train_bagz_path, 
        scalers=scalers
    )
    print(f"Train dataset length: {len(train_dataset)}")
    
    # Get sample to check shapes
    sample = train_dataset[0]
    print(f"Input IDs shape: {sample['input_ids'].shape}")
    print(f"Features shape: {sample['features'].shape}")
    print(f"Winner value: {sample['winner']}")
    
    # Extract sequence lengths from first sample
    total_length = sample['input_ids'].shape[0]
    recent_moves_length = 12  # This is hardcoded based on the known structure
    board_length = total_length - recent_moves_length

    if args.val_bagz_path is None:
        val_dataset = None
        print("No val dataset")
    else:
        print("Loading val dataset")
        val_dataset = WinnerDataset.WinnerDataset(
            bagz_path=args.val_bagz_path, 
            scalers=scalers
        )
        print(f"Val dataset length: {len(val_dataset)}")

    batch_size = args.batch_size
    print(f"Batch size: {batch_size}")

    num_workers = os.cpu_count()

    train_loader = DataLoader(
        train_dataset,
        batch_size=batch_size,
        shuffle=False,
        pin_memory=True,
        persistent_workers=True,
        num_workers=num_workers,
        drop_last=True,
    )

    if val_dataset is not None:
        val_loader = DataLoader(
            val_dataset,
            batch_size=batch_size,
            shuffle=False,
            pin_memory=True,
            persistent_workers=True,
            num_workers=num_workers,
            drop_last=True,
        )
    else:
        val_loader = None

    torch.set_float32_matmul_precision("medium")
    
    # Create winner model
    model = WinnerPatzerModel(
        recent_moves_sequence_length=recent_moves_length,
        recent_moves_vocab_size=len(MOVE_TO_ACTION),
        board_sequence_length=board_length,
        board_input_vocab_size=INPUT_VOCAB_SIZE,
        embedding_dim=args.embedding_dim,
        widening_factor=args.widening_factor,
        num_layers=args.num_layers,
        num_heads=args.num_heads
    )
    
    # Load pre-trained weights if specified
    use_one_cycle = False
    if args.pretrained_checkpoint:
        model = load_pretrained_weights(model, args.pretrained_checkpoint)
        learning_rate = args.fine_tune_lr
        use_one_cycle = True
        print(f"Using fine-tuning learning rate: {learning_rate}")
        
        # Print info about OneCycleLR schedule
        print(f"\nLearning rate schedule: OneCycleLR (fine-tuning mode)")
        print(f"  Dataset size: {len(train_dataset):,} samples")
        print(f"  Batch size: {batch_size}")
        print(f"  Steps per epoch: {math.ceil(len(train_dataset) / batch_size):,}")
        print(f"  Max LR: {learning_rate * 10:.2e}")
        print(f"  Initial LR: {learning_rate * 10 / 25:.2e}")
        print(f"  Final LR: {learning_rate * 10 / 10000:.2e}")
    else:
        learning_rate = 4e-4
        print(f"Training from scratch with learning rate: {learning_rate}")
        print(f"Learning rate schedule: ReduceLROnPlateau")

    model = torch.compile(model, mode="reduce-overhead")
    
    lightning_model = LightningModel(
        model, 
        val_check_interval=args.val_check_interval,
        learning_rate=learning_rate,
        dataset_size=len(train_dataset),
        batch_size=batch_size,
        use_one_cycle=use_one_cycle
    )

    # Create loggers based on settings
    tensorboard_logger = TensorBoardLogger("tensorboard", name=args.tensorboard_name)
    
    # Make sure the tensorboard directory exists
    os.makedirs(tensorboard_logger.log_dir, exist_ok=True)
    
    # Configure wandb_logger based on flags
    wandb_logger = None
    if not args.disable_wandb:
        log_model = "all" if args.wandb_checkpoint else False
        wandb_logger = WandbLogger(project="ChessWinner", name=args.tensorboard_name, log_model=log_model)

    # Copy all python files to the tensorboard directory
    @rank_zero_only
    def copy_files():
        tensorboard_checkpoint_dir = tensorboard_logger.log_dir
        os.makedirs(tensorboard_checkpoint_dir, exist_ok=True)
        print("Copying files to", tensorboard_checkpoint_dir)
        python_files = glob(os.path.join(os.path.dirname(__file__), "*.py"))
        for _python_file in python_files:
            shutil.copy(_python_file, tensorboard_checkpoint_dir)
    
    copy_files()

    # Train model
    if wandb_logger is not None:
        checkpoint_dir = os.path.join(wandb_logger.experiment.dir, "checkpoints")
    else:
        checkpoint_dir = os.path.join(tensorboard_logger.log_dir, "checkpoints")
    
    checkpoint_monitor_metric = "val/loss" if val_loader is not None else "train/loss_epoch"
    checkpoint_monitor_metric_for_filename = checkpoint_monitor_metric.replace("/", "_")
    
    callbacks: list[Callback] = [
        WindowedTrainMetrics(window_size=args.val_check_interval),
        ModelCheckpoint(
            dirpath=checkpoint_dir,
            filename="epoch={epoch}-step={step}-" + checkpoint_monitor_metric_for_filename + "={" + checkpoint_monitor_metric + ":.4f}",
            save_top_k=3,
            monitor=checkpoint_monitor_metric,
            mode="min",
            verbose=True,
            auto_insert_metric_name=False,
            save_on_train_epoch_end=(val_loader is None),
        )
    ]

    # Configure early stopping only if not using OneCycleLR
    if not use_one_cycle:
        strict_setting = not bool(args.resume_from_checkpoint)
        
        if val_loader is not None:
            callbacks.append(EarlyStopping(
                monitor="val/loss",
                mode="min",
                patience=3,
                check_on_train_epoch_end=False,
                log_rank_zero_only = True,
                verbose=True,
                strict=strict_setting,
            ))

        # Add train-based early stopping
        if args.val_check_interval > 0:
            callbacks.append(EarlyStopping(
                monitor="train/loss_window",
                mode="min",
                patience=3,
                strict=strict_setting,
                log_rank_zero_only=True,
                verbose=True,
            ))
        else:
            callbacks.append(EarlyStopping(
                monitor="train/loss_epoch",
                mode="min",
                patience=3,
                strict=strict_setting,
                log_rank_zero_only=True,
                verbose=True,
            ))
    else:
        print("Early stopping disabled for OneCycleLR fine-tuning")

    # Configure logger list
    loggers = []
    if wandb_logger is not None:
        loggers.append(wandb_logger)
    loggers.append(tensorboard_logger)

    trainer = pl.Trainer(
        logger=loggers,
        log_every_n_steps=1,
        max_epochs=args.max_epochs if args.max_epochs is not None else -1,
        val_check_interval=args.val_check_interval if val_loader is not None and args.val_check_interval > 0 else None,
        callbacks=callbacks,
        accelerator="auto",
        devices="auto",
        strategy="auto",
        precision="bf16-mixed",
    )

    # Add validation callback if resuming
    if args.resume_from_checkpoint and val_loader is not None:
        class ValidateOnResumeCallback(Callback):
            def on_fit_start(self, trainer_instance, pl_module):
                if trainer_instance.ckpt_path:
                    print("Running validation before resuming training...")
                    trainer_instance.validate(pl_module, val_loader)

        callbacks.append(ValidateOnResumeCallback())

    trainer.fit(
        lightning_model,
        train_loader,
        val_loader,
        ckpt_path=args.resume_from_checkpoint,
    )


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description='Train a winner prediction model using bagz datasets')
    
    # Dataset arguments
    parser.add_argument('--train_bagz_path', type=str, help='Path to the training winner bagz file', required=True)
    parser.add_argument('--val_bagz_path', type=str, help='Path to the validation winner bagz file', required=False)
    parser.add_argument('--scalers', type=str, help='Path to the winner scalers pickle file', required=True)
    
    # Model arguments
    parser.add_argument('--embedding_dim', type=int, help='Embedding dimension', default=256)
    parser.add_argument('--widening_factor', type=int, help='Widening factor', default=4)
    parser.add_argument('--num_layers', type=int, help='Number of layers', default=8)
    parser.add_argument('--num_heads', type=int, help='Number of attention heads', default=8)
    
    # Training arguments
    parser.add_argument('--pretrained_checkpoint', type=str, help='Path to pre-trained PatzerModel checkpoint', default=None)
    parser.add_argument('--fine_tune_lr', type=float, help='Learning rate for fine-tuning', default=1e-4)
    
    # Logging arguments
    parser.add_argument('--tensorboard_name', type=str, help='Name of the tensorboard run', required=True)
    parser.add_argument('--resume_from_checkpoint', type=str, help='Checkpoint to resume from', default=None)
    parser.add_argument('--batch_size', type=int, help='Batch size', default=256 * 8)
    parser.add_argument('--val_check_interval', type=int, help='Validation check interval', default=25000)
    parser.add_argument('--max_epochs', type=int, default=None, help='Maximum number of epochs to train for')
    
    # Wandb arguments
    parser.add_argument('--wandb_checkpoint', action='store_true', help='Enable wandb checkpoint uploads')
    parser.add_argument('--disable_wandb', action='store_true', help='Disable wandb logging completely')
    
    _args = parser.parse_args()

    main(_args)
