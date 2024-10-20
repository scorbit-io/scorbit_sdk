import pytest
from src.scorbit_sdk import ScorbitSDK

@pytest.fixture(autouse=True)
def reset_scorbit_sdk():
    ScorbitSDK._net_instance = None
    ScorbitSDK._current_game = None
    ScorbitSDK._session_logger = None
    # Reset any other static variables in ScorbitSDK
    yield
