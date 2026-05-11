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
from torchmetrics import Accuracy, MeanMetric
import torch.nn.functional as F
from lightning.pytorch.loggers import WandbLogger

import MoveDataset
import tokenizer
import pickle
import math


class WindowedTrainMetrics(Callback):
    """Accumulates train metrics over a fixed number of steps (window) and logs once.

    Handles device placement automatically: the internal TorchMetrics objects are
    moved to the device of the incoming tensors on the first update (and whenever
    the trainer migrates to a different device such as during DDP spawn).
    """

    def __init__(self, window_size: int):
        super().__init__()
        self.window_size = window_size
        self.loss_metric = MeanMetric()

    # ---------------------------------------------------------------------
    # helpers
    # ---------------------------------------------------------------------
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

    # ---------------------------------------------------------------------
    # hooks
    # ---------------------------------------------------------------------
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


class MaskedBrierLoss(nn.Module):
    def __init__(self, reduction='mean'):
        super(MaskedBrierLoss, self).__init__()
        self.reduction = reduction

    def forward(self, pred, target, mask):
        """
        Args:
            pred (torch.Tensor): Predicted logits of shape (batch_size, num_classes)
            target (torch.Tensor): True class indices of shape (batch_size,)
            mask (torch.Tensor): Boolean mask of shape (batch_size, num_classes)
        """
        # Apply mask to input logits
        mask = mask.bool()
        masked_input = pred.masked_fill(~mask, float('-inf'))

        # Compute softmax probabilities over valid classes
        probs = F.softmax(masked_input, dim=1)

        # Create one-hot encoded targets
        one_hot = torch.zeros_like(probs)
        one_hot.scatter_(1, target.unsqueeze(1), 1)

        # Compute squared differences only for valid moves
        # Mask out invalid moves from the squared differences
        squared_diff = (probs - one_hot) ** 2
        squared_diff = squared_diff * mask.float()

        # Compute Brier score
        # Sum over classes (only valid ones contribute due to masking)
        brier_scores = squared_diff.sum(dim=1)

        if self.reduction == 'none':
            return brier_scores
        elif self.reduction == 'mean':
            return brier_scores.mean()
        elif self.reduction == 'sum':
            return brier_scores.sum()
        else:
            raise ValueError(f"Invalid reduction mode: {self.reduction}")


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


class PatzerModel(pl.LightningModule):
    def __init__(
            self,
            recent_moves_sequence_length,
            recent_moves_vocab_size,
            board_sequence_length,
            board_input_vocab_size,
            embedding_dim,
            widening_factor,
            num_layers,
            num_heads,
            output_vocab_size):
        super().__init__()
        self.save_hyperparameters()

        self.recent_moves_sequence_length = recent_moves_sequence_length
        self.recent_moves_vocab_size = recent_moves_vocab_size
        self.board_sequence_length = board_sequence_length
        self.input_vocab_size = board_input_vocab_size
        self.output_vocab_size = output_vocab_size
        self.embedding_dim = embedding_dim
        self.widening_factor = widening_factor
        self.num_layers = num_layers
        self.num_heads = num_heads
        self.output_vocab_size = output_vocab_size

        self.board_embedding = nn.Embedding(board_input_vocab_size, embedding_dim)
        self.rating_embedding = nn.Linear(1, embedding_dim)
        self.clock_time_embedding = nn.Linear(1, embedding_dim)

        self.recent_moves_embedding = nn.Embedding(recent_moves_vocab_size, embedding_dim)
        self.learned_positional_encoding = nn.Parameter(
            torch.randn(recent_moves_sequence_length + board_sequence_length + 2, embedding_dim))  # +2 for rating and clock time

        hidden_size = embedding_dim * widening_factor
        self.mlp_blocks = nn.ModuleList([MlpBlock(embedding_dim, hidden_size, embedding_dim) for _ in range(num_layers)])
        self._attention_blocks = nn.ModuleList([AttentionBlock(embedding_dim, num_heads) for _ in range(num_layers)])
        self.layer_norm = nn.LayerNorm(embedding_dim)
        self.output_layer = nn.Linear(embedding_dim, output_vocab_size)

    def forward(self, recent_moves, board, scaled_rating, scaled_log_time):
        recent_moves = self.recent_moves_embedding(recent_moves)
        board = self.board_embedding(board)
        
        # Process rating input
        scaled_rating = scaled_rating.to(dtype=self.dtype)
        embedded_rating = self.rating_embedding(scaled_rating.unsqueeze(1))
        embedded_rating = embedded_rating.unsqueeze(1)
        
        # Process clock time input
        scaled_log_time = scaled_log_time.to(dtype=self.dtype)
        embedded_clock_time = self.clock_time_embedding(scaled_log_time.unsqueeze(1))
        embedded_clock_time = embedded_clock_time.unsqueeze(1)
        
        # Concatenate all inputs: recent moves, rating, clock time, and board
        # Important that board comes last because it has the CLS token at the end
        x = torch.cat([recent_moves, embedded_rating, embedded_clock_time, board], dim=1)
        
        x = x + self.learned_positional_encoding
        for mlp_block, attention_block in zip(self.mlp_blocks, self._attention_blocks):
            x = x + attention_block(x)
            x = x + mlp_block(x)

        x = self.layer_norm(x)  # Other layernorms are in the blocks
        x = self.output_layer(x)
        x = x[:, -1, :]  # Only take the last token

        return x


class LightningModel(pl.LightningModule):
    def __init__(self, patzer_model, val_check_interval: int, 
                 learning_rate=4e-4, dataset_size=None, batch_size=None, use_one_cycle=False):
        super().__init__()
        self.model = patzer_model
        
        self.loss = MaskedBrierLoss()

        self.val_check_interval = val_check_interval
        self.learning_rate = learning_rate
        self.dataset_size = dataset_size
        self.batch_size = batch_size
        self.use_one_cycle = use_one_cycle

        self.train_accuracy = Accuracy(num_classes=patzer_model.output_vocab_size, task="multiclass")
        self.train_vc_accuracy = Accuracy(num_classes=patzer_model.output_vocab_size, task="multiclass")
        self.val_accuracy = Accuracy(num_classes=patzer_model.output_vocab_size, task="multiclass")

        self.lr_scheduler = None
        self.last_lr = None

    def forward(self, recent_moves, board, scaled_rating, scaled_log_time):
        return self.model(recent_moves, board, scaled_rating, scaled_log_time)

    @staticmethod
    def logits_to_probabilities(logits, action_mask):
        probabilities = F.softmax(logits, dim=1)
        probabilities = probabilities * action_mask
        probabilities = probabilities / probabilities.sum(dim=1, keepdim=True)
        return probabilities

    def training_step(self, batch, batch_idx):
        recent_moves, board, scaled_rating, scaled_log_time, action_mask, y = batch
        y_hat = self(recent_moves, board, scaled_rating, scaled_log_time)
        loss = self.loss(y_hat, y, action_mask)

        self.log("train/loss_step", loss, on_step=True, on_epoch=False, sync_dist=False, prog_bar=True, logger=True)
        self.log("train/loss_epoch", loss, on_step=False, on_epoch=True, sync_dist=False, prog_bar=False, logger=True)

        # Convert to probabilities and apply mask
        probabilities = self.logits_to_probabilities(y_hat, action_mask)

        self.train_accuracy(probabilities, y)
        self.train_vc_accuracy(probabilities, y)
        self.log('train/acc_step', self.train_accuracy)

        if self.val_check_interval > 0 and self.global_step % self.val_check_interval == 0:
            vc_accuracy = self.train_vc_accuracy.compute()
            self.log('train/acc_vc', vc_accuracy, sync_dist=False)
            self.train_vc_accuracy.reset()

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
        recent_moves, board, scaled_rating, scaled_log_time, action_mask, y = batch
        y_hat = self(recent_moves, board, scaled_rating, scaled_log_time)
        loss = self.loss(y_hat, y, action_mask)
        # Ensure val/loss is always logged properly both at step and epoch level
        self.log("val/loss", loss, on_step=True, on_epoch=True, sync_dist=False, prog_bar=True)

        # Convert to probabilities and apply mask
        probabilities = self.logits_to_probabilities(y_hat, action_mask)
        self.val_accuracy(probabilities, y)

        return loss

    def on_train_epoch_end(self):
        # log epoch metric
        accuracy = self.train_accuracy.compute()
        self.log('train/acc_epoch', accuracy, sync_dist=False)
        self.train_accuracy.reset()

    def on_validation_epoch_end(self):
        # log epoch metric
        accuracy = self.val_accuracy.compute()
        self.log('val/acc_epoch', accuracy, sync_dist=False)
        self.val_accuracy.reset()

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


def load_pretrained_weights(patzer_model, checkpoint_path):
    """Load weights from a pre-trained PatzerModel checkpoint.
    
    Since we're loading from another PatzerModel, architectures must match exactly.
    """
    print(f"Loading pre-trained weights from {checkpoint_path}")
    
    # Load the checkpoint
    checkpoint = torch.load(checkpoint_path, map_location='cpu')
    if 'state_dict' in checkpoint:
        state_dict = checkpoint['state_dict']
    else:
        state_dict = checkpoint
    
    # Handle different prefixes that might be in the checkpoint
    cleaned_state_dict = {}
    for k, v in state_dict.items():
        # Remove common prefixes
        cleaned_key = k
        if cleaned_key.startswith('model.model.'):
            cleaned_key = cleaned_key.replace('model.model.', '')
        elif cleaned_key.startswith('model.'):
            cleaned_key = cleaned_key.replace('model.', '')
        
        # Also remove _orig_mod. prefix if present (from torch.compile)
        if cleaned_key.startswith('_orig_mod.'):
            cleaned_key = cleaned_key.replace('_orig_mod.', '')
            
        cleaned_state_dict[cleaned_key] = v
    
    # Get current model state dict
    current_state = patzer_model.state_dict()
    
    # Verify architecture compatibility
    missing_keys = set(current_state.keys()) - set(cleaned_state_dict.keys())
    unexpected_keys = set(cleaned_state_dict.keys()) - set(current_state.keys())
    shape_mismatches = []
    
    for key in current_state.keys():
        if key in cleaned_state_dict:
            if current_state[key].shape != cleaned_state_dict[key].shape:
                shape_mismatches.append(f"{key}: checkpoint {cleaned_state_dict[key].shape} vs current {current_state[key].shape}")
    
    # Report any issues
    if missing_keys or unexpected_keys or shape_mismatches:
        print(f"\nArchitecture mismatch detected!")
        if missing_keys:
            print(f"  Missing keys in checkpoint: {len(missing_keys)}")
            for k in list(missing_keys)[:5]:
                print(f"    - {k}")
            if len(missing_keys) > 5:
                print(f"    ... and {len(missing_keys) - 5} more")
        
        if unexpected_keys:
            print(f"  Unexpected keys in checkpoint: {len(unexpected_keys)}")
            for k in list(unexpected_keys)[:5]:
                print(f"    - {k}")
            if len(unexpected_keys) > 5:
                print(f"    ... and {len(unexpected_keys) - 5} more")
        
        if shape_mismatches:
            print(f"  Shape mismatches: {len(shape_mismatches)}")
            for m in shape_mismatches[:5]:
                print(f"    - {m}")
            if len(shape_mismatches) > 5:
                print(f"    ... and {len(shape_mismatches) - 5} more")
        
        raise ValueError("Cannot load checkpoint: architecture mismatch. "
                       "Ensure the checkpoint was trained with the same model configuration.")
    
    # Load the state dict
    patzer_model.load_state_dict(cleaned_state_dict)
    
    print(f"\nSuccessfully loaded pre-trained weights:")
    print(f"  Total parameters loaded: {len(cleaned_state_dict)}")
    print(f"  Model architecture verified: exact match")
    
    return patzer_model


def main(args):
    print("Loading scalers")
    scalers = pickle.load(open(args.scalers, 'rb'))
    print("Loading train dataset")
    train_dataset = MoveDataset.BagzDataset(bagz_path=args.train_bagz_path, scalers=scalers)
    print(f"Train dataset length: {len(train_dataset)}")
    print(f"X recent moves shape: {train_dataset[0][0].shape}")
    print(f"X board shape: {train_dataset[0][1].shape}")
    print(f"X scaled rating: {train_dataset[0][2]}")
    print(f"X scaled log time: {train_dataset[0][3]}")
    print(f"Output move: {train_dataset[0][5]}")

    if args.val_bagz_path is None:
        val_dataset = None
        print("No val dataset")
    else:
        print("Loading val dataset")
        val_dataset = MoveDataset.BagzDataset(bagz_path=args.val_bagz_path, scalers=scalers)
        print(f"Val dataset length: {len(val_dataset)}")
        print(f"X recent moves shape: {val_dataset[0][0].shape}")
        print(f"X board shape: {val_dataset[0][1].shape}")
        print(f"X scaled rating: {val_dataset[0][2]}")
        print(f"X scaled log time: {val_dataset[0][3]}")
        print(f"Output move: {val_dataset[0][5]}")

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
        # num_workers=0,
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
            # num_workers=0
        )
    else:
        val_loader = None

    torch.set_float32_matmul_precision("medium")
    torch.backends.cuda.matmul.allow_tf32 = True
    model = PatzerModel(
        recent_moves_sequence_length=train_dataset[0][0].shape[0],
        recent_moves_vocab_size=len(tokenizer.MOVE_TO_ACTION),
        board_sequence_length=tokenizer.SEQUENCE_LENGTH,
        board_input_vocab_size=tokenizer.INPUT_VOCAB_SIZE,
        embedding_dim=args.embedding_dim,
        widening_factor=args.widening_factor,
        num_layers=args.num_layers,
        num_heads=args.num_heads,
        output_vocab_size=tokenizer.NUM_ACTIONS)

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
        use_one_cycle=use_one_cycle,
    )

    # Create loggers based on settings
    tensorboard_logger = TensorBoardLogger("tensorboard", name=args.tensorboard_name)
    
    # Make sure the tensorboard directory exists
    os.makedirs(tensorboard_logger.log_dir, exist_ok=True)
    
    # Configure wandb_logger based on flags
    wandb_logger = None
    if not args.disable_wandb:
        log_model = "all" if args.wandb_checkpoint else False
        wandb_logger = WandbLogger(project="DeepPatzer", name=args.tensorboard_name, log_model=log_model)

    # Copy all python files (in the same directory as this file) to the tensorboard directory
    if rank_zero_only.rank == 0:
        tensorboard_checkpoint_dir = tensorboard_logger.log_dir
        # Create directory if it doesn't exist
        os.makedirs(tensorboard_checkpoint_dir, exist_ok=True)
        print("Missing logging folder warning above is okay if version is 0")
        print("Copying files to", tensorboard_checkpoint_dir)
        python_files = glob(os.path.join(os.path.dirname(__file__), "*.py"))
        for _python_file in python_files:
            shutil.copy(_python_file, tensorboard_checkpoint_dir)

    # Train model
    checkpoint_dir = None
    if rank_zero_only.rank == 0 and wandb_logger is not None:
        checkpoint_dir = os.path.join(wandb_logger.experiment.dir, "checkpoints")
    elif rank_zero_only.rank == 0:
        checkpoint_dir = os.path.join(tensorboard_logger.log_dir, "checkpoints")
    checkpoint_monitor_metric = "val/loss" if val_loader is not None else "train/loss_epoch"
    checkpoint_monitor_metric_for_filename = checkpoint_monitor_metric.replace("/", "_")
    callbacks = [
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

    # Configure logger list based on flags
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

    # Add a special additional callback if resuming from a checkpoint
    if args.resume_from_checkpoint and val_loader is not None:
        # When resuming training, run validation first to ensure val/loss is available
        class ValidateOnResumeCallback(Callback):
            def on_fit_start(self, trainer, pl_module):
                if trainer.ckpt_path:
                    print("Running validation before resuming training to ensure val/loss is available...")
                    trainer.validate(pl_module, val_loader)

        callbacks.append(ValidateOnResumeCallback())

    trainer.fit(
        lightning_model,
        train_loader,
        val_loader,
        ckpt_path=args.resume_from_checkpoint,
    )


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description='Train a model using bagz datasets')
    parser.add_argument('--train_bagz_path', type=str, help='Path to the training bagz file', required=True)
    parser.add_argument('--val_bagz_path', type=str, help='Path to the validation bagz file', required=False)
    parser.add_argument('--scalers', type=str, help='Path to the scalers pickle file', required=True)
    parser.add_argument('--tensorboard_name', type=str, help='Name of the tensorboard run', required=True)
    parser.add_argument('--resume_from_checkpoint', type=str, help='Checkpoint to resume from', default=None)
    parser.add_argument('--batch_size', type=int, help='Batch size', default=256 * 8)
    parser.add_argument('--val_check_interval', help='Validation check interval', default=25000)
    parser.add_argument('--embedding_dim', type=int, help='Embedding dimension', default=256)
    parser.add_argument('--widening_factor', type=int, help='Widening factor', default=4)
    parser.add_argument('--num_layers', type=int, help='Number of layers', default=8)
    parser.add_argument('--num_heads', type=int, help='Number of attention heads', default=8)
    parser.add_argument('--pretrained_checkpoint', type=str, help='Path to pre-trained PatzerModel checkpoint for fine-tuning', default=None)
    parser.add_argument('--fine_tune_lr', type=float, help='Learning rate for fine-tuning', default=1e-4)
    parser.add_argument('--wandb_checkpoint', action='store_true', help='Enable wandb checkpoint uploads')
    parser.add_argument('--disable_wandb', action='store_true', help='Disable wandb logging completely')
    parser.add_argument('--max_epochs', type=int, default=None, help='Maximum number of epochs to train for (default: no limit)')
    _args = parser.parse_args()

    # Cast val_check_interval to int or float as needed
    try:
        _args.val_check_interval = int(_args.val_check_interval)
    except ValueError:
        _args.val_check_interval = float(_args.val_check_interval)

    main(_args)
