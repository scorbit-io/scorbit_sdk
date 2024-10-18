import asyncio
import sys
import os
from dotenv import load_dotenv

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# Load environment variables
load_dotenv()

from src.scorbit_sdk import ScorbitSDK, initialize, start, create_game_state

async def authenticate_example():
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

    # Start the SDK (this includes authentication)
    await start()

    # Check if authentication was successful
    if ScorbitSDK._net_instance.session_token:
        print("Authentication successful!")
        print("Stoken:", ScorbitSDK._net_instance.session_token)

        # Create a game state (optional, depending on your use case)
        game_state = create_game_state()
        print("Game state created")
    else:
        print("Authentication failed")

if __name__ == "__main__":
    asyncio.run(authenticate_example())