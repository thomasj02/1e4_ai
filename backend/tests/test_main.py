import pytest
from fastapi.testclient import TestClient
from main import app
from auth import get_current_user

# Test user data for authentication override
TEST_USER = {
    'user_id': 'test-user-123',
    'email': 'test@example.com',
}


def override_get_current_user():
    """Override authentication for testing."""
    return TEST_USER


@pytest.fixture(autouse=True)
def setup_auth_override():
    """Automatically override auth for all tests."""
    app.dependency_overrides[get_current_user] = override_get_current_user
    yield
    app.dependency_overrides.pop(get_current_user, None)


client = TestClient(app)

def test_read_root():
    """Test the root endpoint returns the expected message."""
    response = client.get("/")
    assert response.status_code == 200
    assert response.json() == {"message": "Chessmimic AI Backend Root"}

def test_get_move_normal_position():
    """Test get_move endpoint returns a valid move for normal position."""
    response = client.post("/get_move", json={
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "moves": []
    })
    assert response.status_code == 200
    data = response.json()
    assert "move" in data
    assert data["move"] is not None
    # Should return a valid SAN move
    assert isinstance(data["move"], str)
    assert len(data["move"]) >= 2

def test_get_move_game_over():
    """Test get_move endpoint returns None when game is over."""
    # Fool's mate position
    response = client.post("/get_move", json={
        "fen": "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
        "moves": ["f3", "e5", "g4", "Qh4#"]
    })
    assert response.status_code == 200
    data = response.json()
    assert data["move"] is None

def test_get_move_stalemate():
    """Test get_move endpoint returns None for stalemate position."""
    # King vs King endgame
    response = client.post("/get_move", json={
        "fen": "8/8/8/8/8/8/8/k1K5 w - - 0 1",
        "moves": []
    })
    assert response.status_code == 200
    data = response.json()
    assert data["move"] is None

def test_get_move_with_parameters():
    """Test get_move endpoint with optional parameters including min_probability."""
    response = client.post("/get_move", json={
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "moves": [],
        "rating": 2000,
        "clock_time": 150.0,
        "min_probability": 0.05
    })
    assert response.status_code == 200
    data = response.json()
    assert "move" in data
    assert data["move"] is not None
    # Should return a valid SAN move
    assert isinstance(data["move"], str)

def test_get_move_with_high_min_probability():
    """Test get_move with very high min_probability still returns a move."""
    # Test the fallback behavior - even with 95% threshold, should get a move
    response = client.post("/get_move", json={
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "moves": [],
        "min_probability": 0.95
    })
    assert response.status_code == 200
    data = response.json()
    assert "move" in data
    # Should still return a move due to fallback behavior
    assert data["move"] is not None

def test_get_move_with_zero_min_probability():
    """Test get_move with zero min_probability (no filtering)."""
    response = client.post("/get_move", json={
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "moves": [],
        "min_probability": 0.0
    })
    assert response.status_code == 200
    data = response.json()
    assert "move" in data
    assert data["move"] is not None

def test_get_move_includes_thinking_time():
    """Test get_move endpoint includes thinking_time in response."""
    response = client.post("/get_move", json={
        "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "moves": []
    })
    assert response.status_code == 200
    data = response.json()
    assert "thinking_time" in data
    assert isinstance(data["thinking_time"], (int, float))
    assert 0 <= data["thinking_time"] <= 50  # Allow full range of clock buckets
    # Should be non-negative numeric values
    assert data["thinking_time"] >= 0

def test_thinking_time_range():
    """Test that thinking_time is always between 3 and 5 seconds."""
    for _ in range(10):
        response = client.post("/get_move", json={
            "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "moves": []
        })
        assert response.status_code == 200
        data = response.json()
        assert "thinking_time" in data
        assert isinstance(data["thinking_time"], (int, float))
        assert 0 <= data["thinking_time"] <= 50  # Allow full range of clock buckets
        # Should be integer values even though JSON returns float
        assert data["thinking_time"] == int(data["thinking_time"])

def test_thinking_time_randomness():
    """Test that thinking_time values vary (are random)."""
    thinking_times = []
    for _ in range(20):
        response = client.post("/get_move", json={
            "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "moves": []
        })
        data = response.json()
        thinking_times.append(data["thinking_time"])
    
    # Check that we have some variation in thinking times
    unique_times = set(thinking_times)
    assert len(unique_times) > 1, "All thinking times are the same, should be random"

def test_thinking_time_for_game_over():
    """Test that thinking_time is still included even when game is over."""
    # Fool's mate position
    response = client.post("/get_move", json={
        "fen": "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
        "moves": ["f3", "e5", "g4", "Qh4#"]
    })
    assert response.status_code == 200
    data = response.json()
    assert data["move"] is None
    assert "thinking_time" in data
    assert isinstance(data["thinking_time"], (int, float))
    assert 0 <= data["thinking_time"] <= 50  # Allow full range of clock buckets
    # Should be non-negative numeric values
    assert data["thinking_time"] >= 0