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

    # Create a game state
    game_state = create_game_state()

    # Create Modes instance
    modes = Modes()

    # Simulate a game with different modes
    async def simulate_game_with_modes():
        # Start the game
        game_state.start_game()
        await ScorbitSDK.commit()
        print("Game started")

        # Normal play
        modes.add(GameMode("NA", "Normal Play"))  # Using the NA category
        game_state.set_modes(modes)
        await ScorbitSDK.log_event(game_state.scores, game_state.current_player, game_state.current_ball, str(modes))
        await ScorbitSDK.commit()
        print("Normal Play mode active")
        await asyncio.sleep(2)

        # Multiball
        modes.add(GameMode("MB", "Multiball"))  # Using the MB category
        game_state.set_modes(modes)
        await ScorbitSDK.log_event(game_state.scores, game_state.current_player, game_state.current_ball, str(modes))
        await ScorbitSDK.commit()
        print("Multiball mode active")
        await asyncio.sleep(2)

        # Wizard mode
        modes.clear()  # Clear previous modes
        modes.add(GameMode("WM", "Wizard Mode"))  # Using the WM category
        game_state.set_modes(modes)
        await ScorbitSDK.log_event(game_state.scores, game_state.current_player, game_state.current_ball, str(modes))
        await ScorbitSDK.commit()
        print("Wizard Mode active")
        await asyncio.sleep(2)

        # End the game
        modes.clear()
        game_state.end_game()
        await ScorbitSDK.commit()
        print("Game ended")

    await simulate_game_with_modes()

if __name__ == "__main__":
    asyncio.run(game_modes_example())