import asyncio
import sys
import os
from dotenv import load_dotenv

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# Load environment variables
load_dotenv()

from src.scorbit_sdk import ScorbitSDK, initialize, start

async def heartbeat_example():
    # Initialize the SDK
    await initialize(
        domain=os.getenv("SCORBIT_DOMAIN", "api.scorbit.io"),
        provider=os.getenv("SCORBIT_PROVIDER"),
        private_key=os.getenv("SCORBIT_PRIVATE_KEY"),
        uuid=os.getenv("SCORBIT_UUID"),
        machine_serial=123456,
        machine_id=789,
        software_version="1.0.0"
    )

    # Start the SDK (this will also start the heartbeat)
    await start()

    # Start the heartbeat with a specified interval (e.g., 10 seconds)
    await ScorbitSDK.start_heartbeat(interval=10)

    # The heartbeat is now running in the background
    print("Heartbeat started. Running for 2 minutes...")
    await asyncio.sleep(120)

    # Optionally, you can manually control the heartbeat
    # await ScorbitSDK.stop_heartbeat()
    # print("Heartbeat stopped.")
    # await asyncio.sleep(30)
    # await ScorbitSDK.start_heartbeat(interval=5)
    # print("Heartbeat restarted with 5-second interval.")
    # await asyncio.sleep(30)

    print("Example finished.")

if __name__ == "__main__":
    asyncio.run(heartbeat_example())