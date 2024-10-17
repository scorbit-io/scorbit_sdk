import asyncio
import sys
import os

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, api_call, METHOD_GET, METHOD_POST

async def achievements_example():
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

    # Get achievements for the current game
    async def get_achievements():
        async def achievements_callback(response):
            if response.success:
                print("Achievements:")
                for achievement in response.result:
                    print(f"- {achievement['name']}: {achievement['description']}")
            else:
                print("Failed to get achievements:", response.message)

        await api_call(METHOD_GET, "/api/achievements/", authorization=True, callback=achievements_callback)

    # Unlock an achievement
    async def unlock_achievement(achievement_id):
        data = {
            "achievement_id": achievement_id
        }

        async def unlock_callback(response):
            if response.success:
                print(f"Achievement unlocked successfully: {response.result}")
            else:
                print("Failed to unlock achievement:", response.message)

        await api_call(METHOD_POST, "/api/achievements/unlock/", data=data, authorization=True, callback=unlock_callback)

    # Example usage
    await get_achievements()
    await unlock_achievement("example_achievement_id")

if __name__ == "__main__":
    asyncio.run(achievements_example())