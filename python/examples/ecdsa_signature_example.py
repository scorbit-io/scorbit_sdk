import asyncio
import sys
import os

# Add the parent directory to the Python path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from src.scorbit_sdk import ScorbitSDK, initialize, start, api_call, METHOD_POST

async def ecdsa_signature_example():
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

    message = "Hello, Scorbit!"

    async def sign_callback(response):
        if response.success:
            print(f"Message: {message}")
            print(f"Signature: {response.result.get('signature', 'N/A')}")
        else:
            print("Signing failed:", response.message)

    await api_call(METHOD_POST, "/api/sign/", data={"message": message}, authorization=True, callback=sign_callback)

    async def verify_callback(response):
        if response.success:
            print(f"Verification result: {response.result.get('verified', False)}")
        else:
            print("Verification failed:", response.message)

    await api_call(METHOD_POST, "/api/verify/", data={"message": message, "signature": sign_callback.result['signature']}, authorization=True, callback=verify_callback)

if __name__ == "__main__":
    asyncio.run(ecdsa_signature_example())