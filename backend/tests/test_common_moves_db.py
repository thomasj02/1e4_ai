"""Unit tests for CommonMovesDB."""

import json
import gzip
import tempfile
import pytest
from pathlib import Path
from common_moves_db import CommonMovesDB, find_common_moves_file, load_common_moves_db
import chess


class TestCommonMovesDB:
    """Test suite for CommonMovesDB class."""
    
    def test_init_empty(self):
        """Test initialization with no file."""
        db = CommonMovesDB()
        assert len(db.common_moves) == 0
        assert len(db.fen_only_moves) == 0
    
    def test_init_nonexistent_file(self):
        """Test initialization with non-existent file."""
        db = CommonMovesDB(Path("nonexistent.jsonl"))
        assert len(db.common_moves) == 0
        assert len(db.fen_only_moves) == 0
    
    def test_load_from_jsonl(self):
        """Test loading from regular JSONL file."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.jsonl', delete=False) as f:
            # Write test data
            f.write('{"fen": "[[\\"e2e4\\"],\\"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1\\"]", "moves": {"e7e5": 10, "c7c5": 5}}\n')
            f.write('{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "moves": {"e2e4": 20, "d2d4": 15}}\n')
            temp_path = Path(f.name)
        
        try:
            db = CommonMovesDB(temp_path)
            
            # Check data was loaded
            assert len(db.common_moves) == 2
            assert len(db.fen_only_moves) == 2
            
            # Check FEN with move history
            key = '[["e2e4"],"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"]'
            assert key in db.common_moves
            assert db.common_moves[key]["e7e5"] == 10
            assert db.common_moves[key]["c7c5"] == 5
            
            # Check FEN only index
            fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
            assert fen in db.fen_only_moves
            assert db.fen_only_moves[fen]["e7e5"] == 10
            
        finally:
            temp_path.unlink()
    
    def test_load_from_gzipped_jsonl(self):
        """Test loading from gzipped JSONL file."""
        with tempfile.NamedTemporaryFile(mode='wb', suffix='.jsonl.gz', delete=False) as f:
            # Write gzipped test data
            with gzip.open(f, 'wt') as gz:
                gz.write('{"fen": "[[\\"e2e4\\"],\\"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1\\"]", "moves": {"e7e5": 10}}\n')
                gz.write('{"fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "moves": {"e2e4": 20}}\n')
            temp_path = Path(f.name)
        
        try:
            db = CommonMovesDB(temp_path)
            assert len(db.common_moves) == 2
            assert len(db.fen_only_moves) == 2
        finally:
            temp_path.unlink()
    
    def test_get_moves_with_history(self):
        """Test getting moves with move history."""
        db = CommonMovesDB()
        # Manually add test data
        key = '[["e2e4"],"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"]'
        db.common_moves[key] = {"e7e5": 10, "c7c5": 5}
        
        fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
        moves, strategy = db.get_moves(fen, ["e2e4"])
        
        assert strategy == "FEN+moves"
        assert moves["e7e5"] == 10
        assert moves["c7c5"] == 5
    
    def test_get_moves_fen_only_fallback(self):
        """Test fallback to FEN only when move history not found."""
        db = CommonMovesDB()
        # Add FEN only data
        fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
        db.fen_only_moves[fen] = {"e7e5": 15, "d7d5": 8}
        
        moves, strategy = db.get_moves(fen, ["d2d4"])  # Different move history
        
        assert strategy == "FEN only"
        assert moves["e7e5"] == 15
        assert moves["d7d5"] == 8
    
    def test_get_moves_direct_fen_fallback(self):
        """Test fallback to direct FEN lookup."""
        db = CommonMovesDB()
        # Add direct FEN data (without move history)
        fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
        db.common_moves[fen] = {"e2e4": 20, "d2d4": 15}
        
        moves, strategy = db.get_moves(fen)
        
        assert strategy == "direct FEN"
        assert moves["e2e4"] == 20
        assert moves["d2d4"] == 15
    
    def test_get_moves_not_found(self):
        """Test when position is not found."""
        db = CommonMovesDB()
        moves, strategy = db.get_moves("unknown/position")
        
        assert moves is None
        assert strategy == "not found"
    
    def test_sample_move_basic(self):
        """Test basic move sampling."""
        db = CommonMovesDB()
        # Add test data with deterministic distribution
        fen = "test/position"
        db.fen_only_moves[fen] = {"e2e4": 100}  # Only one move with 100% probability
        
        move, strategy, info = db.sample_move(fen)
        
        assert move == "e2e4"
        assert strategy == "FEN only"
        assert info["probability"] == 1.0
        assert info["move_count"] == 100
        assert info["total_count"] == 100
        assert info["num_moves"] == 1
    
    def test_sample_move_not_found(self):
        """Test sampling when position not found."""
        db = CommonMovesDB()
        move, strategy, info = db.sample_move("unknown/position")
        
        assert move is None
        assert strategy == "not found"
        assert info is None
    
    def test_sample_move_distribution(self):
        """Test that sampling respects probability distribution."""
        db = CommonMovesDB()
        fen = "test/position"
        db.fen_only_moves[fen] = {"e2e4": 900, "d2d4": 100}
        
        # Sample many times and check distribution
        samples = {"e2e4": 0, "d2d4": 0}
        for _ in range(1000):
            move, _, _ = db.sample_move(fen)
            samples[move] += 1
        
        # e2e4 should be sampled approximately 90% of the time
        assert 850 < samples["e2e4"] < 950  # Allow some variance
        assert 50 < samples["d2d4"] < 150
    
    def test_move_merging(self):
        """Test that moves are merged when multiple entries have same FEN."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.jsonl', delete=False) as f:
            # Same FEN with different move histories
            f.write('{"fen": "[[\\"e2e4\\"],\\"test/fen\\"]", "moves": {"a7a6": 5}}\n')
            f.write('{"fen": "[[\\"d2d4\\"],\\"test/fen\\"]", "moves": {"a7a6": 3, "b7b6": 2}}\n')
            temp_path = Path(f.name)
        
        try:
            db = CommonMovesDB(temp_path)
            
            # Check that moves were merged for FEN only
            assert db.fen_only_moves["test/fen"]["a7a6"] == 8  # 5 + 3
            assert db.fen_only_moves["test/fen"]["b7b6"] == 2
            
        finally:
            temp_path.unlink()
    
    def test_malformed_json_handling(self):
        """Test handling of malformed JSON lines."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.jsonl', delete=False) as f:
            f.write('{"fen": "valid/fen", "moves": {"e2e4": 10}}\n')
            f.write('invalid json line\n')  # Malformed
            f.write('{"fen": "another/fen", "moves": {"d2d4": 5}}\n')
            temp_path = Path(f.name)
        
        try:
            # Should not crash, just skip bad lines
            db = CommonMovesDB(temp_path)
            assert len(db.common_moves) == 2  # Only valid lines
            
        finally:
            temp_path.unlink()
    
    def test_empty_file_handling(self):
        """Test handling of empty file."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.jsonl', delete=False) as f:
            # Empty file
            temp_path = Path(f.name)
        
        try:
            db = CommonMovesDB(temp_path)
            assert len(db.common_moves) == 0
            assert len(db.fen_only_moves) == 0
            
        finally:
            temp_path.unlink()
    
    def test_convert_san_to_uci(self):
        """Test SAN to UCI conversion."""
        db = CommonMovesDB()
        
        # Test basic conversion
        san_moves = ["e4", "e5", "Nf3", "Nc6"]
        uci_moves = db.convert_san_to_uci(san_moves)
        assert uci_moves == ["e2e4", "e7e5", "g1f3", "b8c6"]
        
        # Test with captures and checks
        san_moves = ["e4", "e5", "Nf3", "Nc6", "Bb5", "a6", "Bxc6", "dxc6"]
        uci_moves = db.convert_san_to_uci(san_moves)
        assert uci_moves == ["e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5c6", "d7c6"]
        
        # Test invalid moves
        san_moves = ["e4", "invalid_move"]
        uci_moves = db.convert_san_to_uci(san_moves)
        assert uci_moves is None
        
        # Test empty list
        assert db.convert_san_to_uci([]) == []
    
    def test_get_moves_with_san_history(self):
        """Test getting moves with SAN move history."""
        db = CommonMovesDB()
        # Add test data with UCI move history
        key = '[["e2e4"],"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"]'
        db.common_moves[key] = {"e7e5": 10, "c7c5": 5}
        
        fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
        
        # Test with SAN move history
        moves, strategy = db.get_moves(fen, ["e4"], move_format="san")
        assert strategy == "FEN+moves"
        assert moves["e7e5"] == 10
        assert moves["c7c5"] == 5
        
        # Test with UCI move history (default)
        moves, strategy = db.get_moves(fen, ["e2e4"])
        assert strategy == "FEN+moves"
        assert moves["e7e5"] == 10
    
    def test_sample_move_with_san_history(self):
        """Test sampling moves with SAN move history."""
        db = CommonMovesDB()
        # Add test data
        key = '[["e2e4"],"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"]'
        db.common_moves[key] = {"e7e5": 100}  # Only one move for deterministic test
        
        fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
        
        # Test with SAN moves
        move, strategy, info = db.sample_move(fen, ["e4"], move_format="san")
        assert move == "e7e5"
        assert strategy == "FEN+moves"
        assert info["probability"] == 1.0
    
    def test_get_moves_invalid_san_history(self):
        """Test handling of invalid SAN move history."""
        db = CommonMovesDB()
        # Add FEN-only fallback data
        fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
        db.fen_only_moves[fen] = {"e7e5": 20}
        
        # Test with invalid SAN moves - should fall back to FEN only
        moves, strategy = db.get_moves(fen, ["invalid_move"], move_format="san")
        assert strategy == "FEN only"
        assert moves["e7e5"] == 20
    
    def test_strip_fen_clocks(self):
        """Test FEN clock stripping functionality."""
        db = CommonMovesDB()
        
        # Test normal FEN
        assert db.strip_fen_clocks("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") == \
               "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"
        
        # Test with en passant
        assert db.strip_fen_clocks("8/8/8/8/8/8/8/8 b - e3 0 1") == "8/8/8/8/8/8/8/8 b - e3"
        
        # Test already stripped
        assert db.strip_fen_clocks("8/8/8/8/8/8/8/8 w - -") == "8/8/8/8/8/8/8/8 w - -"
        
        # Test too few parts
        assert db.strip_fen_clocks("8/8/8/8/8/8/8/8 w") == "8/8/8/8/8/8/8/8 w"
    
    def test_load_positions_full(self):
        """Test loading FEN-only positions from separate file."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.jsonl.gz', delete=False) as f_gz:
            with gzip.open(f_gz.name, 'wt') as f:
                # Write test data without move clocks
                f.write('{"fen": "1K6/1P1k4/2q5/8/8/8/8/8 w - -", "moves": {"b8a7": 24, "b8a8": 23}, "total": 47}\n')
                f.write('{"fen": "8/8/8/8/8/8/8/8 b - e3", "moves": {"a1a2": 10}, "total": 10}\n')
            temp_path = Path(f_gz.name)
        
        try:
            db = CommonMovesDB()
            db.load_positions_full(temp_path)
            
            # Check data was loaded
            assert len(db.fen_positions_full) == 2
            assert "1K6/1P1k4/2q5/8/8/8/8/8 w - -" in db.fen_positions_full
            assert db.fen_positions_full["1K6/1P1k4/2q5/8/8/8/8/8 w - -"]["b8a7"] == 24
            
        finally:
            temp_path.unlink()
    
    def test_get_moves_with_positions_full_fallback(self):
        """Test FEN-only fallback using positions_full database."""
        db = CommonMovesDB()
        
        # Manually add position to positions_full
        db.fen_positions_full["1K6/1P1k4/2q5/8/8/8/8/8 w - -"] = {"b8a7": 24, "b8a8": 23}
        
        # Query with move clocks (should strip and find)
        moves, strategy = db.get_moves("1K6/1P1k4/2q5/8/8/8/8/8 w - - 0 42")
        assert moves is not None
        assert strategy == "FEN only (positions_full)"
        assert moves["b8a7"] == 24
        assert moves["b8a8"] == 23
    
    def test_sample_move_with_positions_full_fallback(self):
        """Test sampling move from positions_full database."""
        db = CommonMovesDB()
        
        # Manually add position to positions_full
        db.fen_positions_full["8/8/8/8/8/8/8/8 b - e3"] = {"a1a2": 100}  # Single move
        
        # Sample move (should always return the only available move)
        move, strategy, info = db.sample_move("8/8/8/8/8/8/8/8 b - e3 0 1")
        assert move == "a1a2"
        assert strategy == "FEN only (positions_full)"
        assert info['probability'] == 1.0
        assert info['move_count'] == 100
        assert info['total_count'] == 100


class TestHelperFunctions:
    """Test suite for helper functions."""
    
    def test_find_common_moves_file_default(self):
        """Test finding common moves file with default paths.

        The public repository does not ship generated common-move data, so the
        default lookup may legitimately return None in a clean checkout.
        """
        found = find_common_moves_file()
        if found is not None:
            assert found.name in ["common_moves.jsonl.gz", "common_moves.jsonl"]
    
    def test_find_common_moves_file_custom_paths(self):
        """Test finding common moves file with custom paths."""
        with tempfile.TemporaryDirectory() as tmpdir:
            test_file = Path(tmpdir) / "custom.jsonl"
            test_file.touch()
            
            found = find_common_moves_file([test_file, Path("nonexistent.jsonl")])
            assert found == test_file
    
    def test_find_common_moves_file_not_found(self):
        """Test when no file is found."""
        found = find_common_moves_file([Path("nonexistent1.jsonl"), Path("nonexistent2.jsonl")])
        assert found is None
    
    def test_load_common_moves_db_with_file(self):
        """Test loading database when file exists."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.jsonl', delete=False) as f:
            f.write('{"fen": "test/fen", "moves": {"e2e4": 10}}\n')
            temp_path = Path(f.name)
        
        try:
            db = load_common_moves_db([temp_path])
            assert len(db.common_moves) == 1
            
        finally:
            temp_path.unlink()
    
    def test_load_common_moves_db_no_file(self):
        """Test loading database when no file exists."""
        db = load_common_moves_db([Path("nonexistent.jsonl")])
        assert len(db.common_moves) == 0
        assert len(db.fen_only_moves) == 0


if __name__ == "__main__":
    pytest.main([__file__])
