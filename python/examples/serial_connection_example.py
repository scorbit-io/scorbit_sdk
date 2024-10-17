import asyncio
import sys
import os
import serial

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, update_game, create_game_state

async def serial_connection_example():
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

    # Set up serial connection (adjust port and baud rate as needed)
    ser = serial.Serial('/dev/ttyUSB0', 9600, timeout=1)

    async def read_serial_data():
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8').strip()
                # Parse the serial data and update game state
                # This is a simplified example; adjust parsing based on your serial data format
                if line.startswith("SCORE:"):
                    score = int(line.split(":")[1])
                    game_state.scores[game_state.current_player - 1] = score
                    await update_game(game_state)
                    print(f"Updated score: {score}")
            await asyncio.sleep(0.1)

    print("Starting serial connection. Press Ctrl+C to exit.")
    try:
        await read_serial_data()
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        ser.close()

if __name__ == "__main__":
    asyncio.run(serial_connection_example())