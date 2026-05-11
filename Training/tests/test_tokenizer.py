import unittest
import numpy as np
from tokenizer import (
    tokenize, _CHARACTERS, _CHARACTERS_INDEX, _SPACES_CHARACTERS,
    SEQUENCE_LENGTH, INPUT_VOCAB_SIZE, CLASS_TOKEN, PAD_TOKEN,
    MOVE_TO_ACTION, ACTION_TO_MOVE, NUM_ACTIONS, _compute_all_possible_actions
)


class TestTokenizer(unittest.TestCase):
    """Test suite for the chess tokenizer module."""

    def setUp(self):
        """Set up test fixtures."""
        # Standard starting position FEN
        self.starting_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        
        # A mid-game position with various features
        self.mid_game_fen = "r1bqk2r/ppp2ppp/2np1n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 7"
        
        # A position with en passant
        self.en_passant_fen = "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2"
        
        # A position with only some castling rights
        self.partial_castling_fen = "rnbqk2r/pppp1ppp/5n2/4p3/1b2P3/3P1N2/PPP2PPP/RNBQKB1R w KQk - 2 5"
        
        # A late game position with high move numbers
        self.late_game_fen = "8/8/8/2k5/8/8/4K3/8 w - - 42 120"

    def test_constants(self):
        """Test if constants are correctly defined."""
        self.assertEqual(len(_CHARACTERS), 31, "Should have exactly 31 characters in vocabulary")
        self.assertEqual(INPUT_VOCAB_SIZE, len(_CHARACTERS) + 2, "INPUT_VOCAB_SIZE should be len(_CHARACTERS) + 2")
        self.assertEqual(CLASS_TOKEN, INPUT_VOCAB_SIZE - 2, "CLASS_TOKEN should be INPUT_VOCAB_SIZE - 2")
        self.assertEqual(PAD_TOKEN, INPUT_VOCAB_SIZE - 1, "PAD_TOKEN should be INPUT_VOCAB_SIZE - 1")
        self.assertEqual(SEQUENCE_LENGTH, 77 + 1, "SEQUENCE_LENGTH should be 77 + 1")
        
        # Test _CHARACTERS_INDEX mapping
        for idx, char in enumerate(_CHARACTERS):
            self.assertEqual(_CHARACTERS_INDEX[char], idx, f"Character {char} should map to index {idx}")
        
        # Test _SPACES_CHARACTERS set
        self.assertEqual(_SPACES_CHARACTERS, frozenset({'1', '2', '3', '4', '5', '6', '7', '8'}),
                        "SPACES_CHARACTERS should contain digits 1-8")

    def test_tokenize_starting_position(self):
        """Test tokenization of the standard starting position."""
        tokens = tokenize(self.starting_fen)
        
        # Check output properties
        self.assertIsInstance(tokens, np.ndarray, "Output should be a numpy array")
        self.assertEqual(tokens.dtype, np.uint8, "Output should have dtype np.uint8")
        self.assertEqual(len(tokens), SEQUENCE_LENGTH, f"Output should have length {SEQUENCE_LENGTH}")
        
        # Check that the last token is the class token
        self.assertEqual(tokens[-1], CLASS_TOKEN, "Last token should be the CLASS_TOKEN")
        
        # Check that the first token corresponds to side 'w'
        self.assertEqual(tokens[0], _CHARACTERS_INDEX['w'], "First token should be 'w'")

    def test_tokenize_en_passant(self):
        """Test tokenization of en passant information."""
        tokens = tokenize(self.en_passant_fen)
        
        # Find the position of en passant tokens (after board and castling)
        # We have 64 squares (starting with the side token) + 4 castling tokens
        en_passant_pos = 65 + 4
        
        # Check that the en passant square 'd6' is correctly tokenized
        self.assertEqual(tokens[en_passant_pos], _CHARACTERS_INDEX['d'], "First en passant character should be 'd'")
        self.assertEqual(tokens[en_passant_pos+1], _CHARACTERS_INDEX['6'], "Second en passant character should be '6'")

    def test_tokenize_no_en_passant(self):
        """Test tokenization when there's no en passant square."""
        tokens = tokenize(self.starting_fen)  # Starting position has no en passant
        
        # Find the position of en passant tokens
        en_passant_pos = 65 + 4
        
        # Check that both en passant tokens are '.'
        self.assertEqual(tokens[en_passant_pos], _CHARACTERS_INDEX['.'], "First en passant character should be '.'")
        self.assertEqual(tokens[en_passant_pos+1], _CHARACTERS_INDEX['.'], "Second en passant character should be '.'")

    def test_tokenize_all_castling(self):
        """Test tokenization of full castling rights."""
        tokens = tokenize(self.starting_fen)  # Starting position has all castling rights "KQkq"
        
        # Find the position of castling tokens (after the 64 squares, starting with side)
        castling_pos = 65
        
        # Check that the castling rights are correctly tokenized
        self.assertEqual(tokens[castling_pos], _CHARACTERS_INDEX['K'], "First castling token should be 'K'")
        self.assertEqual(tokens[castling_pos+1], _CHARACTERS_INDEX['Q'], "Second castling token should be 'Q'")
        self.assertEqual(tokens[castling_pos+2], _CHARACTERS_INDEX['k'], "Third castling token should be 'k'")
        self.assertEqual(tokens[castling_pos+3], _CHARACTERS_INDEX['q'], "Fourth castling token should be 'q'")

    def test_tokenize_partial_castling(self):
        """Test tokenization of partial castling rights."""
        tokens = tokenize(self.partial_castling_fen)  # Position has "KQk" castling rights
        
        # Find the position of castling tokens
        castling_pos = 65
        
        # Check that the castling rights are correctly tokenized
        self.assertEqual(tokens[castling_pos], _CHARACTERS_INDEX['K'], "First castling token should be 'K'")
        self.assertEqual(tokens[castling_pos+1], _CHARACTERS_INDEX['Q'], "Second castling token should be 'Q'")
        self.assertEqual(tokens[castling_pos+2], _CHARACTERS_INDEX['k'], "Third castling token should be 'k'")
        self.assertEqual(tokens[castling_pos+3], _CHARACTERS_INDEX['.'], "Fourth castling token should be '.'")

    def test_tokenize_no_castling(self):
        """Test tokenization of no castling rights."""
        no_castling_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1"
        tokens = tokenize(no_castling_fen)
        
        # Find the position of castling tokens
        castling_pos = 65
        
        # Check that all castling tokens are '.'
        for i in range(4):
            self.assertEqual(tokens[castling_pos+i], _CHARACTERS_INDEX['.'], 
                            f"Castling token at position {i} should be '.'")

    def test_tokenize_halfmoves(self):
        """Test tokenization of halfmove clock."""
        tokens = tokenize(self.late_game_fen)  # Position has halfmove clock of 42
        
        # Find the position of halfmove tokens (after board, castling, and en passant)
        halfmove_pos = 65 + 4 + 2
        
        # Check that the halfmove clock is correctly tokenized (padding with '.' to 3 digits)
        self.assertEqual(tokens[halfmove_pos], _CHARACTERS_INDEX['4'], "First halfmove digit should be '4'")
        self.assertEqual(tokens[halfmove_pos+1], _CHARACTERS_INDEX['2'], "Second halfmove digit should be '2'")
        self.assertEqual(tokens[halfmove_pos+2], _CHARACTERS_INDEX['.'], "Third halfmove digit should be '.'")

    def test_tokenize_fullmoves(self):
        """Test tokenization of fullmove number."""
        tokens = tokenize(self.late_game_fen)  # Position has fullmove number of 120
        
        # Find the position of fullmove tokens (after board, castling, en passant, and halfmoves)
        fullmove_pos = 65 + 4 + 2 + 3
        
        # Check that the fullmove number is correctly tokenized
        self.assertEqual(tokens[fullmove_pos], _CHARACTERS_INDEX['1'], "First fullmove digit should be '1'")
        self.assertEqual(tokens[fullmove_pos+1], _CHARACTERS_INDEX['2'], "Second fullmove digit should be '2'")
        self.assertEqual(tokens[fullmove_pos+2], _CHARACTERS_INDEX['0'], "Third fullmove digit should be '0'")

    def test_tokenize_board(self):
        """Test detailed tokenization of the board representation."""
        tokens = tokenize(self.starting_fen)
        
        # The first position is the side token
        self.assertEqual(tokens[0], _CHARACTERS_INDEX['w'], "First token should be 'w'")
        
        # Check pieces from the starting position
        # First rank should be RNBQKBNR at positions 57-64
        first_rank_pieces = ['R', 'N', 'B', 'Q', 'K', 'B', 'N', 'R']
        for i, piece in enumerate(first_rank_pieces):
            self.assertEqual(tokens[i+57], _CHARACTERS_INDEX[piece], 
                            f"Token at position {i+57} should be '{piece}'")
        
        # The third rank is all empty (8 empty squares)
        empty_rank_start = 17  # Position where the 3rd rank starts
        for i in range(8):
            self.assertEqual(tokens[empty_rank_start+i], _CHARACTERS_INDEX['.'], 
                            f"Token at position {empty_rank_start+i} should be '.'")

    def test_tokenize_empty_spaces_expansion(self):
        """Test that digits representing empty spaces are correctly expanded."""
        # Create a test FEN with different digits for empty squares
        test_fen = "8/3p4/2n5/1b6/7P/6N1/5B2/4K3 w - - 0 1"
        tokens = tokenize(test_fen)
        
        # Verify specific pieces are correctly positioned
        # First rank has 4K3 which is King at position 61 with dots around it
        self.assertEqual(tokens[61], _CHARACTERS_INDEX['K'], "Token at position 61 should be 'K'")
        self.assertEqual(tokens[60], _CHARACTERS_INDEX['.'], "Token before K should be '.'")
        self.assertEqual(tokens[62], _CHARACTERS_INDEX['.'], "Token after K should be '.'")
        
        # 7th rank has 3p4 which means pawn at position 12 with dots around it
        self.assertEqual(tokens[12], _CHARACTERS_INDEX['p'], "Token at position 12 should be 'p'")
        self.assertEqual(tokens[11], _CHARACTERS_INDEX['.'], "Token before p should be '.'")
        self.assertEqual(tokens[13], _CHARACTERS_INDEX['.'], "Token after p should be '.'")
        
        # 6th rank has 2n5 which means knight at position 19 with dots around it
        self.assertEqual(tokens[19], _CHARACTERS_INDEX['n'], "Token at position 19 should be 'n'")
        self.assertEqual(tokens[18], _CHARACTERS_INDEX['.'], "Token before n should be '.'")
        self.assertEqual(tokens[20], _CHARACTERS_INDEX['.'], "Token after n should be '.'")
        
        # 5th rank has 1b6 which means bishop at position 26 with dots around it
        self.assertEqual(tokens[26], _CHARACTERS_INDEX['b'], "Token at position 26 should be 'b'")
        self.assertEqual(tokens[25], _CHARACTERS_INDEX['.'], "Token before b should be '.'")
        self.assertEqual(tokens[27], _CHARACTERS_INDEX['.'], "Token after b should be '.'")
        
        # 4th rank has 7P which means pawn at position 40 with dots to the left
        self.assertEqual(tokens[40], _CHARACTERS_INDEX['P'], "Token at position 40 should be 'P'")
        self.assertEqual(tokens[39], _CHARACTERS_INDEX['.'], "Token before P should be '.'")
        
        # 3rd rank has 6N1 which means knight at position 47 with dots around it
        self.assertEqual(tokens[47], _CHARACTERS_INDEX['N'], "Token at position 47 should be 'N'")
        self.assertEqual(tokens[46], _CHARACTERS_INDEX['.'], "Token before N should be '.'")
        self.assertEqual(tokens[48], _CHARACTERS_INDEX['.'], "Token after N should be '.'")
        
        # 2nd rank has 5B2 which means bishop at position 54 with dots around it
        self.assertEqual(tokens[54], _CHARACTERS_INDEX['B'], "Token at position 54 should be 'B'")
        self.assertEqual(tokens[53], _CHARACTERS_INDEX['.'], "Token before B should be '.'")
        self.assertEqual(tokens[55], _CHARACTERS_INDEX['.'], "Token after B should be '.'")
        
        # Count the total number of '.' tokens in the board representation (should be 57)
        dot_tokens = sum(1 for i in range(1, 65) if tokens[i] == _CHARACTERS_INDEX['.'])
        self.assertEqual(dot_tokens, 57, "Should have 57 dot tokens representing empty squares")

    def test_compute_all_possible_actions(self):
        """Test the computation of all possible chess moves."""
        move_to_action, action_to_move = _compute_all_possible_actions()
        
        # Check that we have the correct number of actions
        self.assertEqual(len(move_to_action), len(action_to_move), 
                        "move_to_action and action_to_move should have the same length")
        self.assertEqual(len(move_to_action), NUM_ACTIONS, 
                        f"Number of actions should be {NUM_ACTIONS}")
        
        # Test some specific moves
        # Test regular moves
        self.assertIn("e2e4", move_to_action, "Common move e2e4 should be in move_to_action")
        self.assertIn("a1h8", move_to_action, "Diagonal move a1h8 should be in move_to_action")
        self.assertIn("g1f3", move_to_action, "Knight move g1f3 should be in move_to_action")
        
        # Test promotion moves
        self.assertIn("e7e8q", move_to_action, "Promotion move e7e8q should be in move_to_action")
        self.assertIn("e7e8r", move_to_action, "Promotion move e7e8r should be in move_to_action")
        self.assertIn("e7e8b", move_to_action, "Promotion move e7e8b should be in move_to_action")
        self.assertIn("e7e8n", move_to_action, "Promotion move e7e8n should be in move_to_action")
        
        # Test capture promotions
        self.assertIn("e7d8q", move_to_action, "Capture promotion e7d8q should be in move_to_action")
        self.assertIn("e7f8n", move_to_action, "Capture promotion e7f8n should be in move_to_action")
        
        # Test bidirectional mapping
        for move, action in move_to_action.items():
            self.assertEqual(action_to_move[action], move, 
                            f"Bidirectional mapping failed for move {move} and action {action}")

    def test_action_mapping_consistency(self):
        """Test that the action mapping is consistent across module loads."""
        # Generate a new mapping
        new_move_to_action, new_action_to_move = _compute_all_possible_actions()
        
        # Compare with the module constants
        self.assertEqual(len(new_move_to_action), len(MOVE_TO_ACTION), 
                        "Length of newly computed move_to_action should match the module constant")
        
        for move, action in MOVE_TO_ACTION.items():
            self.assertEqual(new_move_to_action[move], action, 
                            f"Action for move {move} should be consistent across computations")
            
        for action, move in ACTION_TO_MOVE.items():
            self.assertEqual(new_action_to_move[action], move, 
                            f"Move for action {action} should be consistent across computations")

    def test_edge_cases(self):
        """Test tokenization of various edge cases."""
        # Test a position with a very high halfmove clock (close to 50)
        high_halfmove_fen = "8/8/8/2k5/8/8/4K3/8 w - - 49 120"
        tokens = tokenize(high_halfmove_fen)
        halfmove_pos = 65 + 4 + 2
        self.assertEqual(tokens[halfmove_pos], _CHARACTERS_INDEX['4'], "First halfmove digit should be '4'")
        self.assertEqual(tokens[halfmove_pos+1], _CHARACTERS_INDEX['9'], "Second halfmove digit should be '9'")
        self.assertEqual(tokens[halfmove_pos+2], _CHARACTERS_INDEX['.'], "Third halfmove digit should be '.'")
        
        # Test a position with a very high fullmove number (999)
        high_fullmove_fen = "8/8/8/2k5/8/8/4K3/8 w - - 0 999"
        tokens = tokenize(high_fullmove_fen)
        fullmove_pos = 65 + 4 + 2 + 3
        self.assertEqual(tokens[fullmove_pos], _CHARACTERS_INDEX['9'], "First fullmove digit should be '9'")
        self.assertEqual(tokens[fullmove_pos+1], _CHARACTERS_INDEX['9'], "Second fullmove digit should be '9'")
        self.assertEqual(tokens[fullmove_pos+2], _CHARACTERS_INDEX['9'], "Third fullmove digit should be '9'")
        
        # Test a position with a single-digit halfmove and fullmove numbers
        single_digit_fen = "8/8/8/2k5/8/8/4K3/8 w - - 5 9"
        tokens = tokenize(single_digit_fen)
        halfmove_pos = 65 + 4 + 2
        fullmove_pos = halfmove_pos + 3
        self.assertEqual(tokens[halfmove_pos], _CHARACTERS_INDEX['5'], "First halfmove digit should be '5'")
        self.assertEqual(tokens[halfmove_pos+1], _CHARACTERS_INDEX['.'], "Second halfmove digit should be '.'")
        self.assertEqual(tokens[halfmove_pos+2], _CHARACTERS_INDEX['.'], "Third halfmove digit should be '.'")
        self.assertEqual(tokens[fullmove_pos], _CHARACTERS_INDEX['9'], "First fullmove digit should be '9'")
        self.assertEqual(tokens[fullmove_pos+1], _CHARACTERS_INDEX['.'], "Second fullmove digit should be '.'")
        self.assertEqual(tokens[fullmove_pos+2], _CHARACTERS_INDEX['.'], "Third fullmove digit should be '.'")
        
        # Test a position with black to move
        black_to_move_fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
        tokens = tokenize(black_to_move_fen)
        self.assertEqual(tokens[0], _CHARACTERS_INDEX['b'], "First token should be 'b'")


if __name__ == '__main__':
    unittest.main()