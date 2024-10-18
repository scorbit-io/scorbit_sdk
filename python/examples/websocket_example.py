import asyncio
import sys
import os

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, set_ws_message_callback

async def websocket_example():
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

    # Define a callback function for WebSocket messages
    async def ws_message_callback(message):
        print("Received WebSocket message:", message)

    # Set the WebSocket message callback
    set_ws_message_callback(ws_message_callback)

    # Start the SDK (this will establish the WebSocket connection)
    await start()

    # Keep the connection open for a while
    print("WebSocket connection established. Waiting for messages...")
    await asyncio.sleep(60)  # Wait for 60 seconds

    # Example of using ws_command
    async def custom_ws_command():
        command = "CUSTOM_COMMAND"
        data = {"key": "value"}
        
        async def command_callback(response):
            print(f"Received response for {command}:", response)

        await ScorbitSDK.ws_command(command, data, specific_callback=command_callback)

    await custom_ws_command()

    # Keep the connection open for a while longer
    await asyncio.sleep(30)

if __name__ == "__main__":
    asyncio.run(websocket_example())