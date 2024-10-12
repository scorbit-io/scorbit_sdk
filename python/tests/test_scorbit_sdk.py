import pytest
import re
import aiohttp
import asyncio
from unittest.mock import AsyncMock, patch, MagicMock
from src.scorbit_sdk import ScorbitSDK, GameState, Net

@pytest.fixture(scope="module")
def event_loop():
    policy = asyncio.get_event_loop_policy()
    loop = policy.new_event_loop()
    yield loop
    loop.close()

@pytest.fixture(scope="module")
async def aiohttp_session():
    session = aiohttp.ClientSession()
    yield session
    await session.close()

@pytest.fixture
def mock_scorbit_sdk():
    net_mock = AsyncMock(spec=Net)
    net_mock.initialize = AsyncMock()
    net_mock.start = AsyncMock()
    net_mock.process_messages = AsyncMock()
    net_mock.api_call = AsyncMock(return_value={"stoken": "mocked_session_token"})
    net_mock._get_session_token = AsyncMock(return_value="mocked_session_token")
    net_mock._send_installed_data = AsyncMock()
    ScorbitSDK._net_instance = net_mock

    game_state_mock = AsyncMock(spec=GameState)
    game_state_mock.set_game_started = AsyncMock()
    game_state_mock.set_game_finished = AsyncMock()
    game_state_mock.send_session_log = AsyncMock()
    game_state_mock.set_current_ball = AsyncMock()
    game_state_mock.set_active_player = AsyncMock()
    game_state_mock.set_score = AsyncMock()
    game_state_mock.add_mode = AsyncMock()
    game_state_mock.remove_mode = AsyncMock()
    game_state_mock.clear_modes = AsyncMock()
    game_state_mock.commit = AsyncMock()
    ScorbitSDK._current_game = game_state_mock

    return net_mock, game_state_mock

@pytest.mark.asyncio
async def test_initialize(mock_scorbit_sdk):
    mock_net, _ = mock_scorbit_sdk
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
        "1.0",
        ""
    )

def test_create_game_state(mock_scorbit_sdk):
    mock_net, _ = mock_scorbit_sdk
    ScorbitSDK._net_instance = mock_net
    result = ScorbitSDK.create_game_state()
    assert isinstance(result, GameState)

def test_destroy_game_state():
    ScorbitSDK._current_game = AsyncMock()
    ScorbitSDK.destroy_game_state()
    assert ScorbitSDK._current_game is None

@pytest.mark.asyncio
async def test_set_game_started(mock_scorbit_sdk):
    _, mock_game_state = mock_scorbit_sdk
    await ScorbitSDK.set_game_started()
    mock_game_state.set_game_started.assert_awaited_once()

@pytest.mark.asyncio
async def test_set_game_finished(mock_scorbit_sdk):
    _, mock_game_state = mock_scorbit_sdk
    await ScorbitSDK.set_game_finished()
    mock_game_state.set_game_finished.assert_awaited_once()
    mock_game_state.send_session_log.assert_awaited_once()

@pytest.mark.asyncio
async def test_set_current_ball(mock_scorbit_sdk):
    _, mock_game_state = mock_scorbit_sdk
    await ScorbitSDK.set_current_ball(3)
    mock_game_state.set_current_ball.assert_awaited_once_with(3)

@pytest.mark.asyncio
async def test_set_active_player(mock_scorbit_sdk):
    _, mock_game_state = mock_scorbit_sdk
    await ScorbitSDK.set_active_player(2)
    mock_game_state.set_active_player.assert_awaited_once_with(2)

@pytest.mark.asyncio
async def test_set_score(mock_scorbit_sdk):
    _, mock_game_state = mock_scorbit_sdk
    await ScorbitSDK.set_score(1, 1000)
    mock_game_state.set_score.assert_awaited_once_with(1, 1000)

@pytest.mark.asyncio
async def test_add_mode(mock_scorbit_sdk):
    _, mock_game_state = mock_scorbit_sdk
    await ScorbitSDK.add_mode("multiball")
    mock_game_state.add_mode.assert_awaited_once_with("multiball")

@pytest.mark.asyncio
async def test_remove_mode(mock_scorbit_sdk):
    _, mock_game_state = mock_scorbit_sdk
    await ScorbitSDK.remove_mode("multiball")
    mock_game_state.remove_mode.assert_awaited_once_with("multiball")

@pytest.mark.asyncio
async def test_clear_modes(mock_scorbit_sdk):
    _, mock_game_state = mock_scorbit_sdk
    await ScorbitSDK.clear_modes()
    mock_game_state.clear_modes.assert_awaited_once()

@pytest.mark.asyncio
async def test_commit(mock_scorbit_sdk):
    _, mock_game_state = mock_scorbit_sdk
    await ScorbitSDK.commit()
    mock_game_state.commit.assert_called_once()

@pytest.mark.asyncio
async def test_update_game(mock_scorbit_sdk):
    _, mock_game_state = mock_scorbit_sdk
    await ScorbitSDK.update_game(True, current_player=1, current_ball=2, player_scores={1: 1000, 2: 2000}, game_modes=["multiball"])
    mock_game_state.set_active_player.assert_awaited_once_with(1)
    mock_game_state.set_current_ball.assert_awaited_once_with(2)
    mock_game_state.set_score.assert_any_call(1, 1000)
    mock_game_state.set_score.assert_any_call(2, 2000)
    mock_game_state.clear_modes.assert_awaited_once()
    mock_game_state.add_mode.assert_awaited_once_with("multiball")
    mock_game_state.commit.assert_awaited_once()

@pytest.mark.asyncio
async def test_tick(mock_scorbit_sdk):
    mock_net, _ = mock_scorbit_sdk
    await ScorbitSDK.tick()
    mock_net.process_messages.assert_called_once()

@pytest.mark.asyncio
async def test_api_call(mock_scorbit_sdk):
    mock_net, _ = mock_scorbit_sdk
    callback = AsyncMock()
    await ScorbitSDK.api_call("GET", "/endpoint", data={"key": "value"}, authorization=True, callback=callback)
    mock_net.api_call.assert_called_once_with("GET", "/endpoint", {"key": "value"}, None, True)
    callback.assert_called_once()

def test_get_pairing_qr_url():
    url = ScorbitSDK.get_pairing_qr_url("prefix", "machine_id", "uuid")
    assert url == "https://scorbit.link/qrcode?$deeplink_path=prefix&machineid=machine_id&uuid=uuid"

def test_get_claiming_qr_url():
    url = ScorbitSDK.get_claiming_qr_url("venuemachine_id", "opdb_id")
    assert url == "https://scorbit.link/qrcode?$deeplink_path=venuemachine_id&opdb=opdb_id"

@pytest.mark.asyncio
async def test_start(mock_scorbit_sdk):
    mock_net, _ = mock_scorbit_sdk
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
    ScorbitSDK._net_instance = None
    with pytest.raises(Exception, match=re.escape("Net instance not initialized. Call ScorbitSDK.initialize() first.")):
        ScorbitSDK.create_game_state()

@pytest.mark.asyncio
async def test_set_game_started_error():
    ScorbitSDK._current_game = None
    with pytest.raises(Exception, match=re.escape("Game state not created. Call create_game_state() first.")):
        await ScorbitSDK.set_game_started()

@pytest.fixture(autouse=True)
async def reset_scorbit_sdk():
    ScorbitSDK._net_instance = None
    ScorbitSDK._current_game = None
    ScorbitSDK._api_call_callbacks = {}
    ScorbitSDK._api_call_number = 0
    yield
    if ScorbitSDK._net_instance:
        await ScorbitSDK.close()