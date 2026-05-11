# Testing Guide for Chessmimic

This document provides instructions on how to run tests for both the frontend and backend components of the Chessmimic application.

## Frontend Tests

The frontend tests use Jest and React Testing Library to test React components.

### Setup

1. Navigate to the frontend directory:
   ```
   cd frontend
   ```

2. Install dependencies:
   ```
   npm install
   ```

### Running Tests

To run all frontend tests:
```
npm test
```

To run tests with coverage report:
```
npm test -- --coverage
```

### Test Files

- `src/App.test.jsx`: Tests for the main App component
- `src/MoveList.test.jsx`: Tests for the MoveList component

## Backend Tests

The backend tests use pytest to test the FastAPI endpoints.

### Setup

1. Navigate to the backend directory:
   ```
   cd backend
   ```

2. Create and activate a virtual environment:
   ```
   python -m venv .venv
   source .venv/bin/activate  # On Windows: .venv\Scripts\activate
   ```

3. Install dependencies:
   ```
   pip install -r requirements.txt
   pip install -r requirements-dev.txt
   ```

### Running Tests

To run all backend tests with coverage:
```
pytest
```

The pytest configuration in `pytest.ini` is set to:
- Run tests from the `tests` directory
- Generate a coverage report
- Fail if coverage is below 100%

### Test Files

- `tests/test_main.py`: Tests for the main FastAPI application

## Continuous Integration

For CI/CD pipelines, you can run all tests with:

```bash
# Frontend tests
cd frontend && npm test -- --coverage

# Backend tests
cd backend && python -m pytest
```

Both test suites are configured to fail if coverage is below 100%, ensuring comprehensive test coverage.