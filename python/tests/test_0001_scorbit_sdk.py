import pytest
import re
import aiohttp
import asyncio
from unittest.mock import AsyncMock, patch, MagicMock
from src.scorbit_sdk import ScorbitSDK, GameState, Net, SessionLogger

@pytest.fixture(autouse=True)
def setup_scorbit_sdk():
    # Store original class attributes and methods
    original_attrs = {attr: getattr(ScorbitSDK, attr) for attr in dir(ScorbitSDK) if not attr.startswith('__')}
    
    # Perform a complete reset before each test
    ScorbitSDK.reset()
    ScorbitSDK._net_instance = None
    ScorbitSDK._current_game = None
    ScorbitSDK._session_logger = None
    ScorbitSDK._initialized = False
    ScorbitSDK._started = False

    yield
    
    # Restore original class attributes and methods after each test
    for attr, value in original_attrs.items():
        setattr(ScorbitSDK, attr, value)
    
    # Perform the reset again after each test
    ScorbitSDK.reset()
    ScorbitSDK._net_instance = None
    ScorbitSDK._current_game = None
    ScorbitSDK._session_logger = None
    ScorbitSDK._initialized = False
    ScorbitSDK._started = False

#@pytest.fixture(scope="module")
#def event_loop():
#    policy = asyncio.get_event_loop_policy()
#    loop = policy.new_event_loop()
#    yield loop
#    loop.close()

@pytest.fixture(scope="module")
async def aiohttp_session():
    session = aiohttp.ClientSession()
    yield session
    await session.close()

@pytest.fixture
def mock_scorbit_sdk():
    with patch('src.scorbit_sdk.Net') as net_mock, \
         patch('src.scorbit_sdk.GameState') as game_state_mock, \
         patch('src.scorbit_sdk.SessionLogger') as session_logger_mock:
        
        net_mock.return_value = AsyncMock(spec=Net)
        game_state_mock.return_value = AsyncMock(spec=GameState)
        session_logger_mock.return_value = AsyncMock(spec=SessionLogger)

        # Set up async methods for GameState
        game_state_mock.return_value.set_game_started = AsyncMock()
        game_state_mock.return_value.set_game_finished = AsyncMock()
        game_state_mock.return_value.set_current_ball = AsyncMock()
        game_state_mock.return_value.set_active_player = AsyncMock()
        game_state_mock.return_value.set_score = AsyncMock()
        game_state_mock.return_value.add_mode = AsyncMock()
        game_state_mock.return_value.remove_mode = AsyncMock()
        game_state_mock.return_value.clear_modes = AsyncMock()
        game_state_mock.return_value.commit = AsyncMock()

        ScorbitSDK._net_instance = net_mock.return_value
        ScorbitSDK._current_game = game_state_mock.return_value
        ScorbitSDK._session_logger = session_logger_mock.return_value
        ScorbitSDK._initialized = True
        ScorbitSDK._started = False

        yield net_mock.return_value, game_state_mock.return_value, session_logger_mock.return_value

    # Reset ScorbitSDK state after each test
    ScorbitSDK.reset()
    ScorbitSDK._net_instance = None
    ScorbitSDK._current_game = None
    ScorbitSDK._session_logger = None
    ScorbitSDK._initialized = False
    ScorbitSDK._started = False

@pytest.mark.asyncio(loop_scope="module")
async def test_initialize(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.initialize(
        "domain",
        "provider",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "0123456789abcdef0123456789abcdef",
        1,
        2,
        "1.0"
    )
    mock_net.initialize.assert_called_once_with(
        "domain",
        "provider",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "0123456789abcdef0123456789abcdef",
        1,
        2,
        "1.0"
    )

def test_create_game_state(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    result = ScorbitSDK.create_game_state()
    assert isinstance(result, GameState)

def test_destroy_game_state():
    ScorbitSDK._current_game = AsyncMock()
    ScorbitSDK.destroy_game_state()
    assert ScorbitSDK._current_game is None

@pytest.mark.asyncio(loop_scope="module")
async def test_set_game_started(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.set_game_started()
    mock_game_state.set_game_started.assert_awaited_once()

@pytest.mark.asyncio(loop_scope="module")
async def test_set_game_finished(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.set_game_started()
    mock_game_state.set_game_started.assert_awaited_once()
    print("**** Game started.")
    await ScorbitSDK.set_game_finished()
    mock_game_state.set_game_finished.assert_awaited_once()
    print("**** Game finished.")
    assert mock_session_logger is not None
    mock_session_logger.send_session_log.assert_awaited_once()
    print("**** Session log sent.")

@pytest.mark.asyncio(loop_scope="module")
async def test_set_current_ball(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.set_current_ball(3)
    mock_game_state.set_current_ball.assert_awaited_once_with(3)

@pytest.mark.asyncio(loop_scope="module")
async def test_set_active_player(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.set_active_player(2)
    mock_game_state.set_active_player.assert_awaited_once_with(2)

@pytest.mark.asyncio(loop_scope="module")
async def test_set_score(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.set_score(1, 1000)
    mock_game_state.set_score.assert_awaited_once_with(1, 1000)

@pytest.mark.asyncio(loop_scope="module")
async def test_add_mode(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.add_mode("multiball")
    mock_game_state.add_mode.assert_awaited_once_with("multiball")

@pytest.mark.asyncio(loop_scope="module")
async def test_remove_mode(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.remove_mode("multiball")
    mock_game_state.remove_mode.assert_awaited_once_with("multiball")

@pytest.mark.asyncio(loop_scope="module")
async def test_clear_modes(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.clear_modes()
    mock_game_state.clear_modes.assert_awaited_once()

@pytest.mark.asyncio(loop_scope="module")
async def test_commit(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.commit()
    mock_game_state.commit.assert_called_once()

@pytest.mark.asyncio(loop_scope="module")
async def test_update_game(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.update_game(True, current_player=1, current_ball=2, player_scores={1: 1000, 2: 2000}, game_modes=["multiball"])
    mock_game_state.set_active_player.assert_awaited_once_with(1)
    mock_game_state.set_current_ball.assert_awaited_once_with(2)
    mock_game_state.set_score.assert_any_call(1, 1000)
    mock_game_state.set_score.assert_any_call(2, 2000)
    mock_game_state.clear_modes.assert_awaited_once()
    mock_game_state.add_mode.assert_awaited_once_with("multiball")
    mock_game_state.commit.assert_awaited_once()

@pytest.mark.asyncio(loop_scope="module")
async def test_tick(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.tick()
    mock_net.process_messages.assert_called_once()

@pytest.mark.asyncio(loop_scope="module")
async def test_api_call(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    callback = AsyncMock()
    await mock_net.api_call("GET", "/api/venues", {"key": "value"}, None, True)
    mock_net.api_call.assert_called_once_with("GET", "/api/venues", {"key": "value"}, None, True)

def test_get_pairing_qr_url():
    url = ScorbitSDK.get_pairing_qr_url("prefix", "machine_id", "uuid")
    assert url == "https://scorbit.link/qrcode?$deeplink_path=prefix&machineid=machine_id&uuid=uuid"

def test_get_claiming_qr_url():
    url = ScorbitSDK.get_claiming_qr_url("venuemachine_id", "opdb_id")
    assert url == "https://scorbit.link/qrcode?$deeplink_path=venuemachine_id&opdb=opdb_id"

@pytest.mark.asyncio(loop_scope="module")
async def test_start(mock_scorbit_sdk):
    mock_net, mock_game_state, mock_session_logger = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    ScorbitSDK._current_game = mock_game_state
    ScorbitSDK._session_logger = mock_session_logger
    ScorbitSDK._initialized = True
    await ScorbitSDK.start()
    mock_net.start.assert_called_once()

def test_version():
    with patch('src.scorbit_sdk.VERSION', '1.0.0'):
        assert ScorbitSDK.version() == '1.0.0'

def test_add_logger_callback():
    with patch('src.scorbit_sdk.add_logger_callback') as mock_add_logger:
        callback = MagicMock()
        ScorbitSDK.add_logger_callback(callback, user_data="test")
        mock_add_logger.assert_called_once_with(callback, "test")

def test_reset_logger():
    with patch('src.scorbit_sdk.reset_logger') as mock_reset_logger:
        ScorbitSDK.reset_logger()
        mock_reset_logger.assert_called_once()

# Error case tests

def test_create_game_state_error():
    ScorbitSDK.reset()
    with pytest.raises(Exception, match=re.escape("Net instance not initialized. Call ScorbitSDK.initialize() first.")):
        ScorbitSDK.create_game_state()

@pytest.mark.asyncio(loop_scope="module")
async def test_set_game_started_error():
    ScorbitSDK.reset()
    with pytest.raises(Exception, match=re.escape("Game state not created. Call create_game_state() first.")):
        await ScorbitSDK.set_game_started()
