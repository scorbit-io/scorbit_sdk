import asyncio
import sys
import os
from dotenv import load_dotenv
from datetime import datetime

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
from src.scorbit_sdk import ScorbitSDK, initialize, start, add_logger_callback, reset_logger

def logger_callback(message, user_data):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{timestamp}] Logger ({user_data}): {message}")

async def logger_example():
    add_logger_callback(logger_callback, user_data="Example")

    await initialize(
        domain=os.getenv("SCORBIT_DOMAIN", "api.scorbit.io"),
        provider=os.getenv("SCORBIT_PROVIDER"),
        private_key=os.getenv("SCORBIT_PRIVATE_KEY"),
        uuid=os.getenv("SCORBIT_UUID"),
        machine_serial=123456,
        machine_id=789,
        software_version="1.0.0"
    )

    await start()

    # Perform some operations that might generate log messages
    # ...

    reset_logger()
    print("Logger has been reset")

if __name__ == "__main__":
    asyncio.run(logger_example())