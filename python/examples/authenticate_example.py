import asyncio
import sys
import os

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, api_call, METHOD_POST

async def authenticate():
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

    # Perform authentication
    async def auth_callback(response):
        if response.success:
            print("Authentication successful!")
            print("Stoken:", response.result.get('stoken'))
        else:
            print("Authentication failed:", response.message)

    await api_call(METHOD_POST, "/api/stoken/", authorization=True, callback=auth_callback)

if __name__ == "__main__":
    asyncio.run(authenticate())