import asyncio
import sys
import os
from dotenv import load_dotenv

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# Load environment variables
load_dotenv()

from src.scorbit_sdk import ScorbitSDK, initialize, start

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
    installed_data = {
        "type": "score_detector",  # Tells us the name of your client and version number
        "version": "1.0.0",  # We recommend versioning client changes for Scorbit support
        "installed": True  # Set to true if the game is installed, false if it is not
    }
    
    response = await ScorbitSDK._net_instance.send_installed_data(installed_data)
    if response.success:
        print("Installed data sent successfully!")
        print("Custom data from server:", response.result)
    else:
        print("Failed to send installed data:", response.message)

    # Get current configuration
    response = await ScorbitSDK._net_instance.get_config()
    if response.success:
        print("Current configuration:", response.result)
    else:
        print("Failed to get configuration:", response.message)