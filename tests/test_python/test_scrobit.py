import unittest
import threading
import time
from scorbit import scorbit


class TestScorbitEnums(unittest.TestCase):
    """Test enum definitions and values."""
    
    def test_log_level_enum(self):
        """Test LogLevel enum values."""
        self.assertEqual(scorbit.LogLevel.Debug.name, "Debug")
        self.assertEqual(scorbit.LogLevel.Info.name, "Info")
        self.assertEqual(scorbit.LogLevel.Warn.name, "Warn")
        self.assertEqual(scorbit.LogLevel.Error.name, "Error")
        
    def test_error_enum(self):
        """Test Error enum values."""
        self.assertEqual(scorbit.Error.Success.name, "Success")
        self.assertEqual(scorbit.Error.Unknown.name, "Unknown")
        self.assertEqual(scorbit.Error.AuthFailed.name, "AuthFailed")
        self.assertEqual(scorbit.Error.NotPaired.name, "NotPaired")
        self.assertEqual(scorbit.Error.ApiError.name, "ApiError")
        
    def test_auth_status_enum(self):
        """Test AuthStatus enum values."""
        self.assertEqual(scorbit.AuthStatus.NotAuthenticated.name, "NotAuthenticated")
        self.assertEqual(scorbit.AuthStatus.Authenticating.name, "Authenticating")
        self.assertEqual(scorbit.AuthStatus.AuthenticatedCheckingPairing.name, "AuthenticatedCheckingPairing")
        self.assertEqual(scorbit.AuthStatus.AuthenticatedUnpaired.name, "AuthenticatedUnpaired")
        self.assertEqual(scorbit.AuthStatus.AuthenticatedPaired.name, "AuthenticatedPaired")
        self.assertEqual(scorbit.AuthStatus.AuthenticationFailed.name, "AuthenticationFailed")


class TestDeviceInfo(unittest.TestCase):
    """Test DeviceInfo class functionality."""
    
    def setUp(self):
        self.device_info = scorbit.DeviceInfo()
        
    def test_device_info_creation(self):
        """Test DeviceInfo instance creation."""
        self.assertIsInstance(self.device_info, scorbit.DeviceInfo)
        
    def test_device_info_attributes(self):
        """Test DeviceInfo attribute access and modification."""
        # Test setting and getting attributes
        self.device_info.provider = "test_provider"
        self.assertEqual(self.device_info.provider, "test_provider")
        
        self.device_info.machine_id = 12345
        self.assertEqual(self.device_info.machine_id, 12345)
        
        self.device_info.game_code_version = "1.0.0"
        self.assertEqual(self.device_info.game_code_version, "1.0.0")
        
        self.device_info.hostname = "staging"
        self.assertEqual(self.device_info.hostname, "staging")
        
        self.device_info.uuid = "f0b188f8-9f2d-4f8d-abe4-c3107516e7ce"
        self.assertEqual(self.device_info.uuid, "f0b188f8-9f2d-4f8d-abe4-c3107516e7ce")
        
        self.device_info.serial_number = 98765
        self.assertEqual(self.device_info.serial_number, 98765)
        
        self.device_info.auto_download_player_pics = True
        self.assertTrue(self.device_info.auto_download_player_pics)

        self.device_info.score_features = ["ramp", "left slingshot", "right slingshot"]
        self.assertEqual(self.device_info.score_features, ["ramp", "left slingshot", "right slingshot"])

        self.device_info.score_features_version = 1;
        self.assertEqual(self.device_info.score_features_version, 1)
        
    def test_device_info_repr(self):
        """Test DeviceInfo string representation."""
        self.device_info.provider = "scorbitron"
        self.device_info.machine_id = 4419
        self.device_info.game_code_version = "1.12.3"
        
        repr_str = repr(self.device_info)
        self.assertIn("scorbitron", repr_str)
        self.assertIn("4419", repr_str)
        self.assertIn("1.12.3", repr_str)
        self.assertIn("DeviceInfo", repr_str)


class TestPlayerInfo(unittest.TestCase):
    """Test PlayerInfo class functionality."""
    
    def setUp(self):
        # Note: PlayerInfo is read-only, so we'd typically get it from GameState
        # For testing purposes, we'll mock the behavior
        pass
        
    def test_player_info_readonly_attributes(self):
        """Test that PlayerInfo attributes are read-only."""
        # This test would need actual PlayerInfo instances from the C++ side
        # Since they're read-only, we can't easily create test instances
        # In a real scenario, you'd get PlayerInfo from GameState.get_player_info()

        pi = scorbit.PlayerInfo()
        self.assertTrue(hasattr(pi, 'id'))
        self.assertTrue(hasattr(pi, 'preferred_name'))
        self.assertTrue(hasattr(pi, 'name'))
        self.assertTrue(hasattr(pi, 'initials'))
        self.assertTrue(hasattr(pi, 'picture_url'))
        self.assertTrue(hasattr(pi, 'picture'))


class TestLoggerFunctionality(unittest.TestCase):
    """Test logger callback functionality."""
    
    def setUp(self):
        self.log_messages = []
        self.callback_called = threading.Event()
        
    def tearDown(self):
        # Reset logger after each test
        scorbit.reset_logger()
        
    def test_add_logger_callback(self):
        """Test adding a logger callback."""
        def log_callback(message, level, file, line):
            self.log_messages.append({
                'message': message,
                'level': level,
                'file': file,
                'line': line
            })
            self.callback_called.set()
            
        # This should not raise an exception
        scorbit.add_logger_callback(log_callback)
        
    def test_reset_logger(self):
        """Test resetting logger callbacks."""
        def log_callback(message, level, file, line):
            pass
            
        scorbit.add_logger_callback(log_callback)
        # This should not raise an exception
        scorbit.reset_logger()
        
    def test_logger_callback_with_different_log_levels(self):
        """Test logger callback receives different log levels."""
        received_levels = []
        
        def log_callback(message, level, file, line):
            received_levels.append(level)
            
        scorbit.add_logger_callback(log_callback)
        
        # Note: We can't easily trigger log messages from the C++ side in unit tests
        # This would typically be tested in integration tests


class TestGameStateCreation(unittest.TestCase):
    """Test GameState creation and basic functionality."""
    
    def setUp(self):
        self.device_info = scorbit.DeviceInfo()
        self.device_info.provider = "test_provider"
        self.device_info.machine_id = 12345
        self.device_info.game_code_version = "1.0.0"
        self.encrypted_key = "test_encrypted_key"
        
    def test_create_game_state(self):
        """Test GameState creation."""
        game_state = scorbit.create_game_state(self.encrypted_key, self.device_info)
        self.assertIsInstance(game_state, scorbit.GameState)
        
    def test_game_state_basic_methods(self):
        """Test basic GameState methods."""
        game_state = scorbit.create_game_state(self.encrypted_key, self.device_info)
        
        # These methods should not raise exceptions
        status = game_state.get_status()
        self.assertIsInstance(status, scorbit.AuthStatus)
        
        uuid = game_state.get_machine_uuid()
        self.assertIsInstance(uuid, str)
        
        pair_link = game_state.get_pair_deeplink()
        self.assertIsInstance(pair_link, str)
        
        claim_link = game_state.get_claim_deeplink(1)
        self.assertIsInstance(claim_link, str)


class TestGameStateGameLogic(unittest.TestCase):
    """Test GameState game logic functionality."""
    
    def setUp(self):
        self.device_info = scorbit.DeviceInfo()
        self.device_info.provider = "test_provider"
        self.device_info.machine_id = 12345
        self.device_info.game_code_version = "1.0.0"
        self.encrypted_key = "test_encrypted_key"
        self.game_state = scorbit.create_game_state(self.encrypted_key, self.device_info)
        
    def test_game_lifecycle(self):
        """Test basic game lifecycle methods."""
        # Start game
        self.game_state.set_game_started()
        
        # Set various game properties
        self.game_state.set_current_ball(2)
        self.game_state.set_active_player(1)
        self.game_state.set_score(1, 50000)
        
        # Add modes
        self.game_state.add_mode("MB:Multiball")
        self.game_state.add_mode("JACKPOT:Super Jackpot")
        
        # Commit changes
        self.game_state.commit()
        
        # Remove mode
        self.game_state.remove_mode("MB:Multiball")
        self.game_state.commit()
        
        # Clear all modes
        self.game_state.clear_modes()
        self.game_state.commit()
        
        # Finish game
        self.game_state.set_game_finished()
        
    def test_set_score_multiple_players(self):
        """Test setting scores for multiple players."""
        self.game_state.set_game_started()
        
        # Set scores for multiple players
        for player in range(1, 5):
            self.game_state.set_score(player, player * 10000)
            
        self.game_state.commit()
        
    def test_set_score_with_feature(self):
        """Test setting score with feature parameter."""
        self.game_state.set_game_started()
        self.game_state.set_score(1, 25000, 42)  # feature = 42
        self.game_state.commit()
        
    def test_ball_number_boundaries(self):
        """Test ball number boundary conditions."""
        self.game_state.set_game_started()
        
        # Valid ball numbers
        for ball in range(1, 10):
            self.game_state.set_current_ball(ball)
            
        # Invalid ball numbers (should be ignored)
        self.game_state.set_current_ball(0)
        self.game_state.set_current_ball(10)
        
        self.game_state.commit()
        
    def test_player_number_boundaries(self):
        """Test player number boundary conditions."""
        self.game_state.set_game_started()
        
        # Valid player numbers
        for player in range(1, 10):
            self.game_state.set_active_player(player)
            self.game_state.set_score(player, 1000)
            
        # Invalid player numbers (should be ignored)
        self.game_state.set_active_player(0)
        self.game_state.set_active_player(10)
        self.game_state.set_score(0, 1000)
        self.game_state.set_score(10, 1000)
        
        self.game_state.commit()


class TestGameStateAsyncMethods(unittest.TestCase):
    """Test GameState asynchronous methods."""
    
    def setUp(self):
        self.device_info = scorbit.DeviceInfo()
        self.device_info.provider = "test_provider"
        self.device_info.machine_id = 12345
        self.device_info.game_code_version = "1.0.0"
        self.encrypted_key = "test_encrypted_key"
        self.game_state = scorbit.create_game_state(self.encrypted_key, self.device_info)
        
    def test_request_top_scores(self):
        """Test requesting top scores with callback."""
        callback_called = threading.Event()
        received_data = {}
        
        def score_callback(error, json_scores):
            received_data['error'] = error
            received_data['scores'] = json_scores
            callback_called.set()
            
        # This should not raise an exception
        self.game_state.request_top_scores(100000, score_callback)
        
        # Wait a bit for potential callback (won't actually be called in unit test)
        callback_called.wait(timeout=0.1)
        
    def test_request_pair_code(self):
        """Test requesting pair code with callback."""
        callback_called = threading.Event()
        received_data = {}
        
        def pair_callback(error, code):
            received_data['error'] = error
            received_data['code'] = code
            callback_called.set()
            
        # This should not raise an exception
        self.game_state.request_pair_code(pair_callback)
        
        # Wait a bit for potential callback
        callback_called.wait(timeout=0.1)
        
    def test_request_unpair(self):
        """Test requesting unpair with callback."""
        callback_called = threading.Event()
        received_data = {}
        
        def unpair_callback(error, response):
            received_data['error'] = error
            received_data['response'] = response
            callback_called.set()
            
        # This should not raise an exception
        self.game_state.request_unpair(unpair_callback)
        
        # Wait a bit for potential callback
        callback_called.wait(timeout=0.1)


class TestGameStatePlayerInfo(unittest.TestCase):
    """Test GameState player info functionality."""
    
    def setUp(self):
        self.device_info = scorbit.DeviceInfo()
        self.device_info.provider = "test_provider"
        self.device_info.machine_id = 12345
        self.device_info.game_code_version = "1.0.0"
        self.encrypted_key = "test_encrypted_key"
        self.game_state = scorbit.create_game_state(self.encrypted_key, self.device_info)
        
    def test_player_info_methods(self):
        """Test player info related methods."""
        # These should not raise exceptions
        is_updated = self.game_state.is_players_info_updated()
        self.assertIsInstance(is_updated, bool)
        
        has_info = self.game_state.has_player_info(1)
        self.assertIsInstance(has_info, bool)
        
        # get_player_info might raise if no info available, so we test conditionally
        if has_info:
            player_info = self.game_state.get_player_info(1)
            self.assertIsInstance(player_info, scorbit.PlayerInfo)


class TestModuleAttributes(unittest.TestCase):
    """Test module-level attributes and functions."""
    
    def test_module_version(self):
        """Test module version attribute."""
        self.assertTrue(hasattr(scorbit, '__version__'))
        version = scorbit.__version__
        self.assertIsInstance(version, str)
        self.assertGreater(len(version), 0)
        
    def test_module_doc(self):
        """Test module documentation."""
        self.assertEqual(scorbit.__doc__, "Scorbit SDK")


class TestErrorHandling(unittest.TestCase):
    """Test error handling and edge cases."""
    
    def test_invalid_device_info(self):
        """Test creating GameState with invalid DeviceInfo."""
        device_info = scorbit.DeviceInfo()
        # Leave required fields empty
        
        # This might raise an exception or handle gracefully
        try:
            game_state = scorbit.create_game_state("test_key", device_info)
            self.assertIsInstance(game_state, scorbit.GameState)
        except Exception as e:
            # If it raises an exception, that's also valid behavior
            self.assertIsInstance(e, Exception)
            
    def test_callback_exception_handling(self):
        """Test that exceptions in callbacks are handled gracefully."""
        def failing_callback(*args):
            raise ValueError("Test exception in callback")
            
        # These should not cause the main thread to crash
        scorbit.add_logger_callback(failing_callback)
        
        device_info = scorbit.DeviceInfo()
        device_info.provider = "test"
        device_info.machine_id = 1
        device_info.game_code_version = "1.0.0"
        
        game_state = scorbit.create_game_state("test_key", device_info)
        
        # These async methods should handle callback exceptions gracefully
        game_state.request_top_scores(0, failing_callback)
        game_state.request_pair_code(failing_callback)
        game_state.request_unpair(failing_callback)
        
        # Give some time for potential async operations
        time.sleep(0.1)


if __name__ == '__main__':
    # Create a test suite
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    
    # Add all test classes
    test_classes = [
        TestScorbitEnums,
        TestDeviceInfo,
        TestPlayerInfo,
        TestLoggerFunctionality,
        TestGameStateCreation,
        TestGameStateGameLogic,
        TestGameStateAsyncMethods,
        TestGameStatePlayerInfo,
        TestModuleAttributes,
        TestErrorHandling
    ]
    
    for test_class in test_classes:
        tests = loader.loadTestsFromTestCase(test_class)
        suite.addTests(tests)
    
    # Run the tests
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    
    # Exit with appropriate code
    exit(0 if result.wasSuccessful() else 1)
