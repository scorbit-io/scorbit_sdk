import asyncio
import sys
import os
import qrcode

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, get_pairing_qr_url, get_claiming_qr_url

async def qr_code_example():
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

    # Generate pairing QR code
    pairing_url = await get_pairing_qr_url()
    pairing_qr = qrcode.make(pairing_url)
    pairing_qr.save("pairing_qr.png")
    print(f"Pairing QR code saved as 'pairing_qr.png'")
    print(f"Pairing URL: {pairing_url}")

    # Generate claiming QR code
    claiming_url = await get_claiming_qr_url()
    claiming_qr = qrcode.make(claiming_url)
    claiming_qr.save("claiming_qr.png")
    print(f"Claiming QR code saved as 'claiming_qr.png'")
    print(f"Claiming URL: {claiming_url}")

if __name__ == "__main__":
    asyncio.run(qr_code_example())