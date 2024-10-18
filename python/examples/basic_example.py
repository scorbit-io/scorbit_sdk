import asyncio
import sys
import os
from dotenv import load_dotenv

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# Load environment variables
load_dotenv()

from src.scorbit_sdk import ScorbitSDK, initialize, start, create_game_state, set_score, commit
from src.modes import Modes, GameMode

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

    # Create Modes instance
    modes = Modes()

    # Simulate a game with a tick loop
    for i in range(1, 4):  # Simulate 3 balls
        current_scores = {1: i * 1000}  # Example score for player 1
        await set_score(1, current_scores[1])

        # Update the game state
        game_state.current_player = 1
        game_state.current_ball = i
        game_state.scores[0] = current_scores[1]  # Update score for player 1

        # Log the event after updating the score
        await ScorbitSDK.log_event(current_scores, game_state.current_player, game_state.current_ball, str(modes))

        # Add a mode after the first ball
        if i == 1:
            modes.add(GameMode("MB", "Multiball"))  # Using the MB category
            game_state.set_modes(modes)
            print("Added mode: Multiball")
            await ScorbitSDK.log_event(current_scores, game_state.current_player, game_state.current_ball, str(modes))  # Log after adding mode

        await asyncio.sleep(2)  # Simulate time between balls

    # Remove the mode after the second ball
    modes.clear()  # Clear previous modes
    game_state.set_modes(modes)
    print("Removed mode: Multiball")
    await ScorbitSDK.log_event(current_scores, game_state.current_player, game_state.current_ball, str(modes))  # Log after removing mode

    # End the game
    await ScorbitSDK.set_game_finished()
    print("Game session ended.")

if __name__ == "__main__":
    asyncio.run(main())