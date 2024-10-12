import unittest
from unittest.mock import patch, AsyncMock
import asyncio
import pytest
import sys
import os
# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from examples.basic_example import main, DOMAIN_STAGING, METHOD_GET, _machinegroups_callback
from src.scorbit_sdk import ScorbitSDK

class TestBasicExample:
    @pytest.mark.asyncio
    @patch('src.scorbit_sdk.Net')
    @patch('examples.basic_example.input', return_value='q')
    async def test_main_flow(self, mock_input, mock_Net):
        mock_net_instance = AsyncMock()
        mock_net_instance.uuid = 'test-uuid'
        mock_Net.return_value = mock_net_instance

        await main()

        mock_net_instance.initialize.assert_called_once_with(
            DOMAIN_STAGING, '', '', '', 0, 0, '1.337', ''
        )
        mock_net_instance.start.assert_called_once()
        mock_net_instance.process_messages.assert_called()

    @pytest.mark.asyncio
    @patch('examples.basic_example.initialize')
    @patch('examples.basic_example.start')
    @patch('examples.basic_example.api_call')
    @patch('examples.basic_example.input', side_effect=['g', 'q'])
    async def test_get_machine_groups(self, mock_input, mock_api_call, mock_start, mock_initialize):
        await main()

        mock_initialize.assert_called_once_with(
            domain=DOMAIN_STAGING,
            provider="",
            private_key="",
            uuid="",
            machine_serial=0,
            machine_id=0,
            software_version="1.337"
        )
        mock_start.assert_called_once()
        assert mock_api_call.call_count == 2
        mock_api_call.assert_any_call(
            METHOD_GET,
            "/api/machinegroups/",
            authorization=True,
            callback=mock_api_call.call_args_list[1][1]['callback']
        )

if __name__ == '__main__':
    unittest.main()