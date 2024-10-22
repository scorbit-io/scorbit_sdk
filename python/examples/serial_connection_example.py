import asyncio
import sys
import os
import serial
from dotenv import load_dotenv

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, create_game_state

async def serial_connection_example():
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

    # Set up serial connection (adjust port and baud rate as needed)
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)

    async def send_game_status():
        while True:
            # Prepare the game status string
            status_string = f"s,{','.join(map(str, game_state.scores))},{game_state.current_player},{game_state.current_ball},NA:Tiger Saw;MB:Multiball,n\n"
            ser.write(status_string.encode('utf-8'))  # Send the status over the serial port
            print(f"Sent game status: {status_string.strip()}")
            await asyncio.sleep(1)  # Send updates every second

    print("Starting serial connection. Press Ctrl+C to exit.")
    try:
        await send_game_status()
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        ser.close()  # Close the serial connection when done

if __name__ == "__main__":
    asyncio.run(serial_connection_example())