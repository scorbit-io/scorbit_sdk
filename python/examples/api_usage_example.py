import asyncio
import sys
import os

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, api_call, METHOD_POST, create_game_state, update_game

async def api_usage_example():
    # Initialize the SDK
    await initialize(
        domain="staging.scorbit.io",
        provider="your_provider",
        private_key="your_private_key",
        uuid="your_uuid",
        machine_serial=123456,
        machine_id=789,
        software_version="1.0.0"
    )

    # Start the SDK
    await start()

    # Create a game state
    game_state = create_game_state()

    # Simulate a game
    async def simulate_game():
        # Start the game
        game_state.start_game()
        await update_game(game_state)
        print("Game started")

        # Update scores and ball
        for i in range(1, 4):  # 3 balls
            game_state.current_player = 1
            game_state.current_ball = i
            game_state.scores[0] = i * 1000000  # Simulate increasing score
            await update_game(game_state)
            print(f"Updated ball {i} for player 1")
            await asyncio.sleep(2)  # Simulate game duration

        # End the game
        game_state.end_game()
        await update_game(game_state)
        print("Game ended")

    await simulate_game()

if __name__ == "__main__":
    asyncio.run(api_usage_example())