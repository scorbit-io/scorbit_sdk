import asyncio
import sys
import os

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, api_call, METHOD_GET, METHOD_POST

async def challenges_example():
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

    # Get active challenges
    async def get_active_challenges():
        async def challenges_callback(response):
            if response.success:
                print("Active challenges:")
                for challenge in response.result:
                    print(f"- {challenge['name']}: {challenge['description']}")
            else:
                print("Failed to get challenges:", response.message)

        await api_call(METHOD_GET, "/api/challenges/active/", authorization=True, callback=challenges_callback)

    # Submit a challenge result
    async def submit_challenge_result(challenge_id, score):
        data = {
            "challenge_id": challenge_id,
            "score": score
        }

        async def submit_callback(response):
            if response.success:
                print(f"Challenge result submitted successfully: {response.result}")
            else:
                print("Failed to submit challenge result:", response.message)

        await api_call(METHOD_POST, "/api/challenges/submit/", data=data, authorization=True, callback=submit_callback)

    # Example usage
    await get_active_challenges()
    await submit_challenge_result("example_challenge_id", 1000000)

if __name__ == "__main__":
    asyncio.run(challenges_example())