import asyncio
import sys
import os

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, api_call, METHOD_POST

async def configure_example():
    # Initialize the SDK
    await initialize(
        domain="staging.scorbit.io",
        provider="your_provider",
        private_key="your_private_key",
        uuid="your_uuid",
        machine_serial=123456,
        machine_id=789,
        software_version="1.0.0"
    )

    # Start the SDK
    await start()

    # Example configuration data
    config_data = {
        "machine_name": "My Pinball Machine",
        "location": "Game Room",
        "custom_field": "Custom Value"
    }

    # Configure the machine
    async def configure_callback(response):
        if response.success:
            print("Configuration successful!")
            print("Updated config:", response.result)
        else:
            print("Configuration failed:", response.message)

    await api_call(METHOD_POST, "/api/configure/", data=config_data, authorization=True, callback=configure_callback)

if __name__ == "__main__":
    asyncio.run(configure_example())