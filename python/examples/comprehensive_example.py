import asyncio
import sys
import os

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, api_call, METHOD_GET, METHOD_POST, create_game_state, update_game

async def comprehensive_example():
    await initialize(
        domain="staging.scorbit.io",
        provider="your_provider",
        private_key="your_private_key",
        uuid="your_uuid",
        machine_serial=123456,
        machine_id=789,
        software_version="1.0.0"
    )

    await start()

    game_state = create_game_state()

    # Get active challenges
    active_challenges = []
    async def get_challenges_callback(response):
        nonlocal active_challenges
        if response.success:
            active_challenges = response.result
            print("Active challenges:", active_challenges)

    await api_call(METHOD_GET, "/api/challenges/active/", authorization=True, callback=get_challenges_callback)

    # Get achievements
    achievements = []
    async def get_achievements_callback(response):
        nonlocal achievements
        if response.success:
            achievements = response.result
            print("Available achievements:", achievements)

    await api_call(METHOD_GET, "/api/achievements/", authorization=True, callback=get_achievements_callback)

    # Simulate a game
    async def simulate_game():
        game_state.start_game()
        await update_game(game_state)
        print("Game started")

        for i in range(1, 4):  # 3 balls
            game_state.current_player = 1
            game_state.current_ball = i
            game_state.scores[0] = i * 1000000

            await update_game(game_state)
            print(f"Updated ball {i} for player 1, score: {game_state.scores[0]}")

            # Check for achievements
            for achievement in achievements:
                if game_state.scores[0] >= achievement['score_threshold']:
                    await api_call(METHOD_POST, "/api/achievements/unlock/", data={"achievement_id": achievement['id']}, authorization=True)
                    print(f"Achievement unlocked: {achievement['name']}")

            # Check for challenges
            for challenge in active_challenges:
                if game_state.scores[0] >= challenge['target_score']:
                    await api_call(METHOD_POST, "/api/challenges/submit/", data={"challenge_id": challenge['id'], "score": game_state.scores[0]}, authorization=True)
                    print(f"Challenge completed: {challenge['name']}")

            await asyncio.sleep(2)  # Simulate game duration

        game_state.end_game()
        await update_game(game_state)
        print("Game ended")

    await simulate_game()

if __name__ == "__main__":
    asyncio.run(comprehensive_example())