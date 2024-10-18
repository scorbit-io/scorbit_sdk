import asyncio
import sys
import os
from dotenv import load_dotenv

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, update_game, create_game_state
from src.modes import Modes, GameMode

async def game_modes_example():
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

    # Check if a game state already exists
    if ScorbitSDK._current_game is None:
        # Create a game state
        game_state = create_game_state()
    else:
        game_state = ScorbitSDK._current_game  # Use the existing game state
        print("Using existing game state.")

    # Create Modes instance
    modes = Modes()

    # Simulate a game with different modes
    async def simulate_game_with_modes():
        # Start the game
        if not game_state.is_game_active():  # Assuming is_game_active() is a method in GameState
            game_state.start_game()
            await ScorbitSDK.commit()  # Abstracted network call
            print("Game started")
        else:
            print("Game is already active.")

        # Stacking multiple modes
        modes.add_mode(GameMode("Normal Play"))
        modes.add_mode(GameMode("Multiball"))
        modes.add_mode(GameMode("Wizard Mode"))
        game_state.set_modes(modes)
        await ScorbitSDK.commit()  # Abstracted network call
        print("Stacked modes active: Normal Play, Multiball, Wizard Mode")
        await asyncio.sleep(2)

        # Remove one of the first modes (e.g., "Normal Play")
        modes.remove_mode("Normal Play")
        game_state.set_modes(modes)
        await ScorbitSDK.commit()  # Abstracted network call
        print("Removed 'Normal Play' mode. Remaining modes: Multiball, Wizard Mode")
        await asyncio.sleep(2)

        # End the game
        modes.clear_modes()
        game_state.end_game()
        await ScorbitSDK.commit()  # Abstracted network call
        print("Game ended")

    await simulate_game_with_modes()

if __name__ == "__main__":
    asyncio.run(game_modes_example())