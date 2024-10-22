import asyncio
import sys
import os
from dotenv import load_dotenv

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start

async def achievements_example():
    # Load environment variables
    load_dotenv()

    # Step 1: Authenticate and initialize the SDK
    await initialize(
        domain=os.getenv("SCORBIT_DOMAIN", "api.scorbit.io"),
        provider=os.getenv("SCORBIT_PROVIDER"),
        private_key=os.getenv("SCORBIT_PRIVATE_KEY"),
        uuid=os.getenv("SCORBIT_UUID"),
        machine_serial=123456,
        machine_id=789,
        software_version="1.0.0"
    )

    # Step 2: Start the SDK
    await start()

    # Step 3: Send installed data
    installed_data = {
        "type": "score_detector",
        "version": "1.0.0",
        "installed": True
    }
    await ScorbitSDK.send_installed_data(installed_data)  # Abstracted method

    # Step 4: Get current configuration
    config = await ScorbitSDK.get_config()  # Abstracted method
    print("Current configuration:", config)

    # Step 5: Pull the master list of achievements
    achievements = await ScorbitSDK.get_achievements()  # Abstracted method
    print("Available achievements:", achievements)

    # Step 6: Start game session
    await ScorbitSDK.create_game_state()  # Create a new game state
    await ScorbitSDK.set_game_started()  # Start the game session
    print("Game session started.")

    # Step 7: Post a score
    score_data = {
        "player_id": 1,
        "score": 1000  # Example score
    }
    await ScorbitSDK.post_score(score_data)  # Abstracted method
    print("Score posted successfully!")

    # Step 8: Unlock one of the achievements
    achievement_id = achievements[0]['id']  # Example: unlock the first achievement
    user_id = os.getenv("USER_ID")  # Assuming user ID is stored in environment variables

    await ScorbitSDK.unlock_achievement(user_id, achievement_id)  # Abstracted method with user ID
    print(f"Achievement {achievement_id} unlocked successfully for user {user_id}!")

    # Step 9: Increment the achievement count by 100
    await ScorbitSDK.increment_achievement(achievement_id, 100)  # Abstracted method
    print(f"Achievement count for {achievement_id} incremented successfully!")

    # Step 10: Set the achievement as achieved
    await ScorbitSDK.set_achievement_achieved(achievement_id)  # Abstracted method
    print(f"Achievement {achievement_id} set as achieved successfully!")

    # End the game session
    await ScorbitSDK.set_game_finished()  # Abstracted method
    print("Game session ended.")

if __name__ == "__main__":
    asyncio.run(achievements_example())