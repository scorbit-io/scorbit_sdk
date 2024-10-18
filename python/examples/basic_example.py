import asyncio
import sys
import os
from dotenv import load_dotenv

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# Load environment variables
load_dotenv()

from src.scorbit_sdk import ScorbitSDK, initialize, start, tick, create_game_state, set_score, add_mode, remove_mode, commit

async def main():
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

    # Start the SDK
    await start()

    # Create a game state
    game_state = create_game_state()
    print("Game state created.")

    # Start the game
    await ScorbitSDK.set_game_started()
    print("Game session started.")

    # Simulate a game with a tick loop
    for i in range(1, 4):  # Simulate 3 balls
        await set_score(1, i * 1000)  # Update score for player 1
        print(f"Updated score for player 1: {i * 1000}")

        # Add a mode after the first ball
        if i == 1:
            await add_mode("Multiball")
            print("Added mode: Multiball")

        await tick()  # Simulate a tick
        await asyncio.sleep(2)  # Simulate time between balls

    # Remove the mode after the second ball
    await remove_mode("Multiball")
    print("Removed mode: Multiball")

    # End the game
    await ScorbitSDK.set_game_finished()
    print("Game session ended.")

if __name__ == "__main__":
    asyncio.run(main())