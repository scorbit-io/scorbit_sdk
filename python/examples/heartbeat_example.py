import asyncio
import sys
import os

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, api_call, METHOD_POST

async def heartbeat_example():
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

    # Heartbeat function
    async def send_heartbeat():
        async def heartbeat_callback(response):
            if response.success:
                print("Heartbeat sent successfully!")
            else:
                print("Heartbeat failed:", response.message)

        await api_call(METHOD_POST, "/api/heartbeat/", authorization=True, callback=heartbeat_callback)

    # Send heartbeat every 60 seconds
    while True:
        await send_heartbeat()
        await asyncio.sleep(60)

if __name__ == "__main__":
    asyncio.run(heartbeat_example())