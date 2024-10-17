import asyncio
import sys
import os

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, add_logger_callback, reset_logger

def logger_callback(message, user_data):
    print(f"Logger ({user_data}): {message}")

async def logger_example():
    # Add a logger callback
    add_logger_callback(logger_callback, user_data="Example")

    await initialize(
        domain="staging.scorbit.io",
        provider="your_provider",
        private_key="your_private_key",
        uuid="your_uuid",
        machine_serial=123456,
        machine_id=789,
        software_version="1.0.0"
    )

    await start()

    # Perform some operations that might generate log messages
    # ...

    # Reset the logger
    reset_logger()

    print("Logger has been reset")

if __name__ == "__main__":
    asyncio.run(logger_example())