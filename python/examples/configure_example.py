import asyncio
import sys
import os
from dotenv import load_dotenv

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# Load environment variables
load_dotenv()

from src.scorbit_sdk import ScorbitSDK, initialize, start, api_call, METHOD_POST, METHOD_GET

async def configure_example():
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

    # Send installed data
    async def installed_callback(response):
        if response.success:
            print("Installed data sent successfully!")
            print("Custom data from server:", response.result)
        else:
            print("Failed to send installed data:", response.message)

    installed_data = {
        "type": "score_detector", # Tells us the name of your client and version number
        "version": "1.0.0", # We recommend versioning client changes for Scorbit support
        "installed": True # Set to true if the game is installed, false if it is not
    }
    await api_call(METHOD_POST, "/api/installed/", data=installed_data, authorization=True, callback=installed_callback)

# Get current configuration
    async def get_config_callback(response):
        if response.success:
            print("Current configuration:", response.result)
        else:
            print("Failed to get configuration:", response.message)

    await api_call(METHOD_GET, "/api/config/", authorization=True, callback=get_config_callback)

# Remove the SET configuration part as it's not supported
if __name__ == "__main__":
    asyncio.run(configure_example())