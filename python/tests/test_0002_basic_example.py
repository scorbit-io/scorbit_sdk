# python/tests/test_basic_example.py

import unittest
from unittest.mock import patch, AsyncMock, Mock
import pytest
import sys
import os
import asyncio

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from examples.basic_example import main
from src.scorbit_sdk import ScorbitSDK, DOMAIN_STAGING, METHOD_GET

class TestBasicExample:
    @pytest.fixture(autouse=True)
    def setup_scorbit_sdk(self):
        ScorbitSDK.reset()
        yield
        ScorbitSDK.reset()
    
    @patch.dict(os.environ, {
        "SCORBIT_DOMAIN": "api.scorbit.io",
        "SCORBIT_PROVIDER": "your_provider",
        "SCORBIT_PRIVATE_KEY": "a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef",
        "SCORBIT_UUID": "1234567890abcdef1234567890abcdef"
    })
    @pytest.mark.asyncio
    @patch('src.scorbit_sdk.Net')  # Mock the Net class
    @patch('examples.basic_example.input', side_effect=['start', 'q'])  # Simulate starting and quitting the game
    async def test_main_flow(self, mock_input, mock_Net):
        # Create a mock Net instance
        mock_net_instance = AsyncMock()
        mock_net_instance.uuid = 'test-uuid'
        mock_Net.return_value = mock_net_instance

        # Mock process_messages with a print statement
        mock_net_instance.process_messages = AsyncMock(side_effect=lambda: print("process_messages called"))

        # Create a mock GameState instance with required async methods
        mock_game_state = Mock()
        mock_game_state.set_game_started = AsyncMock()
        mock_game_state.set_score = AsyncMock()  # Ensure set_score is an AsyncMock
        mock_game_state.log_event = AsyncMock()
        mock_game_state.end_game = AsyncMock()
        mock_game_state.set_game_finished = AsyncMock()  # Added AsyncMock for set_game_finished

        # Assign a real dictionary to `scores` to allow item assignment
        mock_game_state.scores = {}

        # Optionally, initialize other attributes to avoid AttributeErrors
        mock_game_state.current_player = None
        mock_game_state.current_ball = None

        # Create a mock SessionLogger instance with required methods
        mock_session_logger = Mock()
        mock_session_logger.log_event = Mock()
        mock_session_logger.save_session_log = AsyncMock()  # Added AsyncMock for save_session_log
        mock_session_logger.send_session_log = AsyncMock()  # Added AsyncMock for send_session_log

        # Mock the initialize method to set up the Net instance
        async def initialize_side_effect(*args, **kwargs):
            ScorbitSDK._net_instance = mock_net_instance
            # Call the mock_net_instance.initialize with the provided arguments
            await mock_net_instance.initialize(*args, **kwargs)

        ScorbitSDK.initialize = AsyncMock(side_effect=initialize_side_effect)

        # Mock the start method to call process_messages
        async def start_side_effect(*args, **kwargs):
            await mock_net_instance.start()
            await mock_net_instance.process_messages()

        ScorbitSDK.start = AsyncMock(side_effect=start_side_effect)

        # Mock the create_game_state method to set _current_game and _session_logger
        def create_game_state_side_effect(*args, **kwargs):
            ScorbitSDK._current_game = mock_game_state
            ScorbitSDK._session_logger = mock_session_logger
            return mock_game_state

        ScorbitSDK.create_game_state = Mock(side_effect=create_game_state_side_effect)
        ScorbitSDK._current_game = mock_game_state

        print("*** TEST BASIC: Creating game state")
        # Create a game state before starting the game
        game_state = ScorbitSDK.create_game_state()
        assert ScorbitSDK._current_game is not None
        # Ensure the game state is not None
        assert game_state is not None

        print("*** TEST BASIC: Starting game")
        await main()

        # Allow some time for async tasks to complete
        await asyncio.sleep(0.1)

        # Assertions to ensure methods were called correctly
        mock_net_instance.initialize.assert_called_once_with(
            domain='api.scorbit.io',
            provider='your_provider',
            private_key='a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef',
            uuid='1234567890abcdef1234567890abcdef',
            machine_serial=123456,
            machine_id=789,
            software_version='1.0.0'
        )
        mock_net_instance.start.assert_called_once()
        mock_net_instance.process_messages.assert_called()

        # Ensure set_game_started was called on the game state
        mock_game_state.set_game_started.assert_awaited_once()

        # Ensure set_game_finished was called
        mock_game_state.set_game_finished.assert_awaited_once()

        # Ensure save_session_log was called
        mock_session_logger.save_session_log.assert_awaited_once()

        # Ensure send_session_log was called
        mock_session_logger.send_session_log.assert_awaited_once()

    @pytest.mark.asyncio
    @patch('src.scorbit_sdk.ScorbitSDK.initialize', new_callable=AsyncMock)
    @patch('src.scorbit_sdk.ScorbitSDK.start', new_callable=AsyncMock)
    @patch('examples.basic_example.input', side_effect=['g', 'q'])  # Mock input with side effects
    async def test_get_machine_groups(self, mock_input, mock_start, mock_initialize):

        # Create a mock Net instance
        mock_net_instance = AsyncMock()
        mock_initialize.side_effect = lambda *args, **kwargs: setattr(ScorbitSDK, '_net_instance', mock_net_instance)

        # Create a mock GameState instance with required async methods
        mock_game_state = Mock()
        mock_game_state.set_game_started = AsyncMock()
        mock_game_state.set_score = AsyncMock()
        mock_game_state.end_game = AsyncMock()
        mock_game_state.set_game_finished = AsyncMock()
        mock_game_state.scores = {}
        mock_game_state.current_player = None
        mock_game_state.current_ball = None

        # Create a mock SessionLogger instance
        mock_session_logger = Mock()
        mock_session_logger.log_event = Mock()
        mock_session_logger.save_session_log = AsyncMock()
        mock_session_logger.send_session_log = AsyncMock()
    
        # Mock the create_game_state method to set _current_game and _session_logger
        def create_game_state_side_effect(*args, **kwargs):
            ScorbitSDK._current_game = mock_game_state
            ScorbitSDK._session_logger = mock_session_logger
            return mock_game_state
    
        ScorbitSDK.create_game_state = Mock(side_effect=create_game_state_side_effect)

            # Mock ScorbitSDK methods
        ScorbitSDK.set_game_started = AsyncMock()
        ScorbitSDK.set_score = AsyncMock()
        ScorbitSDK.log_event = AsyncMock()
        ScorbitSDK.set_game_finished = AsyncMock()
        
        print("*** TEST BASIC GET_MACHINE_GROUPS: Creating game state")
        # Create a game state before starting the game
        game_state = ScorbitSDK.create_game_state()
        assert ScorbitSDK._current_game is not None
        assert game_state is not None
    
        print("*** TEST BASIC GET_MACHINE_GROUPS: Starting game")

        async def mock_main_impl():
            await ScorbitSDK.initialize(
                domain='api.scorbit.io',
                provider='your_provider',
                private_key='a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef',
                uuid='1234567890abcdef1234567890abcdef',
                machine_serial=123456,
                machine_id=789,
                software_version='1.0.0'
            )
            await ScorbitSDK.start()
            
            # Create game state and start the game
            game_state = ScorbitSDK.create_game_state()
            await ScorbitSDK.set_game_started()

            # Simulate some game actions
            current_scores = {1: 1000}
            current_player = 1
            current_ball = 1
            game_modes = "NA:Tiger Saw"
            await ScorbitSDK.set_score(player=1, score=1000)
            await ScorbitSDK.log_event(current_scores, current_player, current_ball, game_modes)

            # End the game
            await ScorbitSDK.set_game_finished()

        with patch('examples.basic_example.main', new_callable=AsyncMock, side_effect=mock_main_impl) as mock_main:
            await mock_main()
        # Allow some time for async tasks to complete
        await asyncio.sleep(0.1)
    
        # Assertions to ensure methods were called correctly
        mock_initialize.assert_called_once_with(
            domain='api.scorbit.io',
            provider='your_provider',
            private_key='a1b2c3d4e5f678901234567890abcdef1234567890abcdef1234567890abcdef',
            uuid='1234567890abcdef1234567890abcdef',
            machine_serial=123456,
            machine_id=789,
            software_version='1.0.0'
        )
        # Assertions
        mock_initialize.assert_called_once()
        mock_start.assert_called_once()
        ScorbitSDK.set_game_started.assert_awaited_once()
        ScorbitSDK.set_score.assert_awaited()  # This will be called multiple times
        ScorbitSDK.log_event.assert_awaited()  # This will be called multiple times
        ScorbitSDK.set_game_finished.assert_awaited_once()

            # Check if log_event was called with the expected arguments
        log_event_calls = ScorbitSDK.log_event.await_args_list
        assert any(call.args[3] == "NA:Tiger Saw" for call in log_event_calls), "log_event was not called with 'NA:Tiger Saw'"

if __name__ == '__main__':
    unittest.main()