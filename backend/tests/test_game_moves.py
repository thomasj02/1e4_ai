"""Tests for game_moves feature - storing full game data including moves, clocks, and evaluations."""

import pytest
from unittest.mock import patch, MagicMock
from fastapi.testclient import TestClient
from main import app, MoveEval, MoveRecord, GameResultRequest, GameHistoryItem
from auth import get_current_user

# Test client - created fresh for each test class
client = TestClient(app)


# Test user data for dependency override
TEST_USER = {
    'user_id': 'test-user-123',
    'email': 'test@example.com',
}


def override_get_current_user():
    """Override dependency for testing."""
    return TEST_USER


# ============================================================================
# Test Data Fixtures
# ============================================================================

SAMPLE_MOVE_RECORDS = [
    {"move": "e4", "clock": 300000, "eval": {"white": 0.52, "draw": 0.30, "black": 0.18}},
    {"move": "e5", "clock": 298500, "eval": {"white": 0.50, "draw": 0.32, "black": 0.18}},
    {"move": "Nf3", "clock": 295000, "eval": {"white": 0.53, "draw": 0.30, "black": 0.17}},
]


# ============================================================================
# Pydantic Model Tests
# ============================================================================

class TestMoveEvalModel:
    """Tests for the MoveEval Pydantic model."""

    def test_move_eval_valid_data(self):
        """Test creating MoveEval with valid white/draw/black probabilities."""
        eval_data = MoveEval(white=0.52, draw=0.30, black=0.18)
        assert eval_data.white == 0.52
        assert eval_data.draw == 0.30
        assert eval_data.black == 0.18

    def test_move_eval_boundary_values(self):
        """Test MoveEval with boundary values (0 and 1)."""
        # All white
        eval_all_white = MoveEval(white=1.0, draw=0.0, black=0.0)
        assert eval_all_white.white == 1.0

        # All draw
        eval_all_draw = MoveEval(white=0.0, draw=1.0, black=0.0)
        assert eval_all_draw.draw == 1.0

        # All black
        eval_all_black = MoveEval(white=0.0, draw=0.0, black=1.0)
        assert eval_all_black.black == 1.0

    def test_move_eval_equal_probabilities(self):
        """Test MoveEval with roughly equal probabilities."""
        eval_equal = MoveEval(white=0.33, draw=0.34, black=0.33)
        assert eval_equal.white == 0.33
        assert eval_equal.draw == 0.34
        assert eval_equal.black == 0.33


class TestMoveRecordModel:
    """Tests for the MoveRecord Pydantic model."""

    def test_move_record_valid_data(self):
        """Test creating MoveRecord with valid move, clock, and eval."""
        record = MoveRecord(
            move="e4",
            clock=300000,
            eval=MoveEval(white=0.52, draw=0.30, black=0.18)
        )
        assert record.move == "e4"
        assert record.clock == 300000
        assert record.eval.white == 0.52

    def test_move_record_san_notation_pawn(self):
        """Test MoveRecord accepts pawn moves in SAN notation."""
        pawn_moves = ["e4", "d4", "c3", "f6", "a6", "h4"]
        for move in pawn_moves:
            record = MoveRecord(
                move=move,
                clock=290000,
                eval=MoveEval(white=0.50, draw=0.30, black=0.20)
            )
            assert record.move == move

    def test_move_record_san_notation_pieces(self):
        """Test MoveRecord accepts piece moves in SAN notation."""
        piece_moves = ["Nf3", "Bc4", "Qd2", "Rad1", "Re1", "Kd2"]
        for move in piece_moves:
            record = MoveRecord(
                move=move,
                clock=285000,
                eval=MoveEval(white=0.50, draw=0.30, black=0.20)
            )
            assert record.move == move

    def test_move_record_san_notation_castling(self):
        """Test MoveRecord accepts castling moves in SAN notation."""
        castling_moves = ["O-O", "O-O-O"]
        for move in castling_moves:
            record = MoveRecord(
                move=move,
                clock=280000,
                eval=MoveEval(white=0.50, draw=0.30, black=0.20)
            )
            assert record.move == move

    def test_move_record_san_notation_promotion(self):
        """Test MoveRecord accepts promotion moves in SAN notation."""
        promotion_moves = ["e8=Q", "a1=R", "g8=N", "c1=B"]
        for move in promotion_moves:
            record = MoveRecord(
                move=move,
                clock=275000,
                eval=MoveEval(white=0.50, draw=0.30, black=0.20)
            )
            assert record.move == move

    def test_move_record_san_notation_captures(self):
        """Test MoveRecord accepts capture moves in SAN notation."""
        capture_moves = ["exd5", "Nxe5", "Bxf7+", "Qxh7#"]
        for move in capture_moves:
            record = MoveRecord(
                move=move,
                clock=270000,
                eval=MoveEval(white=0.50, draw=0.30, black=0.20)
            )
            assert record.move == move

    def test_move_record_with_check_checkmate(self):
        """Test MoveRecord accepts moves with check/checkmate notation."""
        check_moves = ["Bb5+", "Qh7#", "Nf7+", "Re8#"]
        for move in check_moves:
            record = MoveRecord(
                move=move,
                clock=265000,
                eval=MoveEval(white=0.50, draw=0.30, black=0.20)
            )
            assert record.move == move

    def test_move_record_clock_zero(self):
        """Test MoveRecord with clock at zero (flagging)."""
        record = MoveRecord(
            move="Qh7+",
            clock=0,
            eval=MoveEval(white=0.50, draw=0.30, black=0.20)
        )
        assert record.clock == 0

    def test_move_record_high_clock_value(self):
        """Test MoveRecord with high clock value (long time control)."""
        record = MoveRecord(
            move="d4",
            clock=5400000,  # 90 minutes in ms
            eval=MoveEval(white=0.50, draw=0.30, black=0.20)
        )
        assert record.clock == 5400000


class TestGameResultRequestModel:
    """Tests for the GameResultRequest Pydantic model."""

    def test_game_result_request_with_moves(self):
        """Test GameResultRequest with game_moves array."""
        move_records = [
            MoveRecord(
                move="e4", clock=300000,
                eval=MoveEval(white=0.52, draw=0.30, black=0.18)
            ),
            MoveRecord(
                move="e5", clock=298500,
                eval=MoveEval(white=0.50, draw=0.32, black=0.18)
            ),
        ]

        request = GameResultRequest(
            bot_rating=1800,
            player_color="white",
            result="win",
            result_reason="Checkmate",
            move_count=40,
            time_control="5+3",
            game_moves=move_records
        )

        assert request.bot_rating == 1800
        assert request.player_color == "white"
        assert request.result == "win"
        assert request.game_moves is not None
        assert len(request.game_moves) == 2
        assert request.game_moves[0].move == "e4"
        assert request.game_moves[1].move == "e5"

    def test_game_result_request_without_moves(self):
        """Test GameResultRequest with game_moves as None (optional field)."""
        request = GameResultRequest(
            bot_rating=1500,
            player_color="black",
            result="loss",
            result_reason="Time forfeit"
        )

        assert request.bot_rating == 1500
        assert request.player_color == "black"
        assert request.result == "loss"
        assert request.game_moves is None

    def test_game_result_request_empty_moves(self):
        """Test GameResultRequest with empty game_moves array."""
        request = GameResultRequest(
            bot_rating=2000,
            player_color="white",
            result="draw",
            result_reason="Stalemate",
            game_moves=[]
        )

        assert request.game_moves is not None
        assert len(request.game_moves) == 0

    def test_game_result_request_all_optional_fields(self):
        """Test GameResultRequest with all optional fields populated."""
        move_records = [
            MoveRecord(
                move="d4", clock=600000,
                eval=MoveEval(white=0.51, draw=0.32, black=0.17)
            ),
        ]

        request = GameResultRequest(
            bot_rating=2200,
            player_color="white",
            result="win",
            result_reason="Resignation",
            move_count=35,
            time_control="10+0",
            game_moves=move_records
        )

        assert request.result_reason == "Resignation"
        assert request.move_count == 35
        assert request.time_control == "10+0"
        assert len(request.game_moves) == 1


# ============================================================================
# Mock Supabase Fixture
# ============================================================================

@pytest.fixture
def mock_supabase():
    """Fixture to mock Supabase client for endpoint tests."""
    # Override authentication dependency
    app.dependency_overrides[get_current_user] = override_get_current_user

    with patch('main.get_supabase') as mock_get:
        mock_client = MagicMock()
        mock_table = MagicMock()

        # Setup table method chaining
        mock_client.table.return_value = mock_table
        mock_table.select.return_value = mock_table
        mock_table.insert.return_value = mock_table
        mock_table.update.return_value = mock_table
        mock_table.eq.return_value = mock_table
        mock_table.order.return_value = mock_table
        mock_table.range.return_value = mock_table

        # Default execute response
        mock_execute_result = MagicMock()
        mock_execute_result.data = []
        mock_execute_result.count = 0
        mock_table.execute.return_value = mock_execute_result

        mock_get.return_value = mock_client

        yield {
            'client': mock_client,
            'table': mock_table,
            'execute_result': mock_execute_result
        }

    # Clean up dependency override
    app.dependency_overrides.pop(get_current_user, None)


@pytest.fixture
def mock_auth():
    """Fixture to mock authentication - now redundant but kept for compatibility."""
    # The mock_supabase fixture now handles auth override
    yield


# ============================================================================
# Endpoint Tests
# ============================================================================

class TestRecordGameEndpoint:
    """Tests for POST /rating/game endpoint with game_moves."""

    def test_record_game_with_moves(self, mock_supabase):
        """Test recording a game result with game_moves data."""
        # Setup mock to return existing user rating
        mock_supabase['execute_result'].data = [{
            'clerk_user_id': 'test-user-123',
            'rating': 1500,
            'rating_deviation': 350,
            'games_played': 5,
            'wins': 2,
            'losses': 2,
            'draws': 1,
            'last_game_at': None
        }]

        # Make request with game_moves
        response = client.post('/rating/game', json={
            'bot_rating': 1800,
            'player_color': 'white',
            'result': 'win',
            'result_reason': 'Checkmate',
            'move_count': 40,
            'time_control': '5+3',
            'game_moves': SAMPLE_MOVE_RECORDS
        })

        assert response.status_code == 200
        data = response.json()
        assert 'new_rating' in data
        assert 'rating_change' in data
        assert 'rating_tier' in data

    def test_record_game_without_moves(self, mock_supabase):
        """Test recording a game result without game_moves data."""
        mock_supabase['execute_result'].data = [{
            'clerk_user_id': 'test-user-123',
            'rating': 1500,
            'rating_deviation': 350,
            'games_played': 5,
            'wins': 2,
            'losses': 2,
            'draws': 1,
            'last_game_at': None
        }]

        response = client.post('/rating/game', json={
            'bot_rating': 1600,
            'player_color': 'black',
            'result': 'loss',
            'result_reason': 'Time forfeit'
        })

        assert response.status_code == 200
        data = response.json()
        assert 'new_rating' in data
        assert 'rating_change' in data

    def test_record_game_moves_stored_in_history(self, mock_supabase):
        """Test that game_moves are passed to the insert call."""
        mock_supabase['execute_result'].data = [{
            'clerk_user_id': 'test-user-123',
            'rating': 1500,
            'rating_deviation': 350,
            'games_played': 0,
            'wins': 0,
            'losses': 0,
            'draws': 0,
            'last_game_at': None
        }]

        response = client.post('/rating/game', json={
            'bot_rating': 1500,
            'player_color': 'white',
            'result': 'draw',
            'game_moves': SAMPLE_MOVE_RECORDS
        })

        assert response.status_code == 200

        # Verify insert was called
        assert mock_supabase['client'].table.called

        # Check that the insert included game_moves
        insert_calls = mock_supabase['table'].insert.call_args_list
        assert len(insert_calls) > 0

        # Find the game_history insert call
        for call in insert_calls:
            insert_data = call[0][0] if call[0] else call[1].get('data', {})
            if 'game_moves' in insert_data:
                # Verify game_moves structure
                assert insert_data['game_moves'] is not None
                assert len(insert_data['game_moves']) == 3
                assert insert_data['game_moves'][0]['move'] == 'e4'
                break


class TestGameHistoryEndpoint:
    """Tests for GET /rating/history endpoint with game_moves."""

    def test_history_includes_game_moves(self, mock_supabase):
        """Test that game history response includes game_moves data."""
        # Setup mock to return games with game_moves
        mock_supabase['execute_result'].data = [
            {
                'id': '1',
                'bot_rating': 1800,
                'player_color': 'white',
                'result': 'win',
                'result_reason': 'Checkmate',
                'move_count': 40,
                'time_control': '5+3',
                'rating_before': 1500,
                'rating_after': 1525,
                'rating_change': 25,
                'played_at': '2024-01-15T12:00:00Z',
                'game_moves': SAMPLE_MOVE_RECORDS
            }
        ]
        mock_supabase['execute_result'].count = 1

        response = client.get('/rating/history?limit=10&offset=0')

        assert response.status_code == 200
        data = response.json()
        assert 'games' in data
        assert len(data['games']) == 1

        game = data['games'][0]
        assert 'game_moves' in game
        assert game['game_moves'] is not None
        assert len(game['game_moves']) == 3
        assert game['game_moves'][0]['move'] == 'e4'

    def test_history_handles_null_game_moves(self, mock_supabase):
        """Test that history handles games without game_moves (null/undefined)."""
        mock_supabase['execute_result'].data = [
            {
                'id': '1',
                'bot_rating': 1600,
                'player_color': 'black',
                'result': 'loss',
                'result_reason': None,
                'move_count': None,
                'time_control': None,
                'rating_before': 1500,
                'rating_after': 1475,
                'rating_change': -25,
                'played_at': '2024-01-14T10:00:00Z',
                'game_moves': None  # No game_moves stored
            }
        ]
        mock_supabase['execute_result'].count = 1

        response = client.get('/rating/history?limit=10&offset=0')

        assert response.status_code == 200
        data = response.json()
        assert len(data['games']) == 1

        game = data['games'][0]
        assert 'game_moves' in game
        assert game['game_moves'] is None

    def test_history_mixed_with_and_without_moves(self, mock_supabase):
        """Test history with mix of games with and without game_moves."""
        mock_supabase['execute_result'].data = [
            {
                'id': '1',
                'bot_rating': 1800,
                'player_color': 'white',
                'result': 'win',
                'result_reason': 'Checkmate',
                'move_count': 40,
                'time_control': '5+3',
                'rating_before': 1525,
                'rating_after': 1550,
                'rating_change': 25,
                'played_at': '2024-01-15T14:00:00Z',
                'game_moves': SAMPLE_MOVE_RECORDS
            },
            {
                'id': '2',
                'bot_rating': 1600,
                'player_color': 'black',
                'result': 'draw',
                'result_reason': 'Stalemate',
                'move_count': 60,
                'time_control': '10+0',
                'rating_before': 1500,
                'rating_after': 1525,
                'rating_change': 25,
                'played_at': '2024-01-15T12:00:00Z',
                'game_moves': None  # Older game without moves
            }
        ]
        mock_supabase['execute_result'].count = 2

        response = client.get('/rating/history?limit=10&offset=0')

        assert response.status_code == 200
        data = response.json()
        assert len(data['games']) == 2

        # First game should have moves
        assert data['games'][0]['game_moves'] is not None
        assert len(data['games'][0]['game_moves']) == 3

        # Second game should not have moves
        assert data['games'][1]['game_moves'] is None


class TestGameHistoryItemModel:
    """Tests for the GameHistoryItem Pydantic model."""

    def test_game_history_item_with_moves(self):
        """Test creating GameHistoryItem with game_moves."""
        item = GameHistoryItem(
            id='test-id-123',
            bot_rating=1800,
            player_color='white',
            result='win',
            result_reason='Checkmate',
            move_count=40,
            time_control='5+3',
            rating_before=1500.0,
            rating_after=1525.0,
            rating_change=25.0,
            played_at='2024-01-15T12:00:00Z',
            game_moves=SAMPLE_MOVE_RECORDS
        )

        assert item.id == 'test-id-123'
        assert item.game_moves is not None
        assert len(item.game_moves) == 3
        assert item.game_moves[0]['move'] == 'e4'

    def test_game_history_item_without_moves(self):
        """Test creating GameHistoryItem without game_moves."""
        item = GameHistoryItem(
            id='test-id-456',
            bot_rating=1600,
            player_color='black',
            result='loss',
            result_reason=None,
            move_count=None,
            time_control=None,
            rating_before=1500.0,
            rating_after=1475.0,
            rating_change=-25.0,
            played_at='2024-01-14T10:00:00Z',
            game_moves=None
        )

        assert item.game_moves is None
        assert item.result_reason is None
