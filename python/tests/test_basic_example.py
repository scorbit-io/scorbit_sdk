import unittest
from unittest.mock import patch, AsyncMock
import asyncio
import sys
import os

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from examples.basic_example import main
from src.scorbit_sdk import DOMAIN_STAGING, METHOD_GET

class TestBasicExample(unittest.TestCase):
    @patch('src.scorbit_sdk.Net')
    @patch('examples.basic_example.input', return_value='q')
    def test_main_flow(self, mock_input, mock_Net):
        # Mock the Net instance
        mock_net_instance = AsyncMock()
        mock_net_instance.uuid = 'test-uuid'
        mock_Net.return_value = mock_net_instance

        # Run the main function
        asyncio.run(main())

        # Assert that the methods were called as expected
        mock_net_instance.initialize.assert_called_once_with(
            DOMAIN_STAGING, '', '', '', 0, 0, '1.337', ''
        )
        mock_net_instance.start.assert_called_once()
        mock_net_instance.api_call.assert_called_with(
            METHOD_GET,
            f"/api/scorbitron_paired/test-uuid/",
            None,
            None,
            True
        )

    @patch('src.scorbit_sdk.Net')
    @patch('examples.basic_example.input', side_effect=['g', 'q'])
    def test_get_machine_groups(self, mock_input, mock_Net):
        # Mock the Net instance
        mock_net_instance = AsyncMock()
        mock_net_instance.uuid = 'test-uuid'
        mock_Net.return_value = mock_net_instance

        # Run the main function
        asyncio.run(main())

        # Assert that the methods were called as expected
        mock_net_instance.initialize.assert_called_once_with(
            DOMAIN_STAGING, '', '', '', 0, 0, '1.337', ''
        )
        mock_net_instance.start.assert_called_once()
        
        # Check that api_call was called twice (once for scorbitron_paired and once for machinegroups)
        self.assertEqual(mock_net_instance.api_call.call_count, 2)
        
        # Check the second api_call for machinegroups
        mock_net_instance.api_call.assert_any_call(
            METHOD_GET,
            "/api/machinegroups/",
            None,
            None,
            True
        )

if __name__ == '__main__':
    unittest.main()