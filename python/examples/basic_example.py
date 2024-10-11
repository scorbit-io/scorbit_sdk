import asyncio

from scorbit_sdk import (
    ScorbitSDK, initialize, start, tick, api_call, set_ws_message_callback,
    ScorbitRESTResponse, ScorbitWSMessage, DOMAIN_STAGING, METHOD_GET
)

async def _scorbitron_paired_callback(message: ScorbitRESTResponse):
    print("_scorbitron_paired_callback", message)

async def _machinegroups_callback(response: ScorbitRESTResponse):
    print("_machinegroups_callback", response.message.endpoint, response.result)

async def _ws_message_callback(message: ScorbitWSMessage):
    print("_ws_message_callback", message)

async def main():
    # See Scorbit Manufacturer API Documentation to read about the parameters and how to populate / generate them.
    await initialize(
        domain=DOMAIN_STAGING,
        provider="",
        private_key="",
        uuid="",  # You don't have to remove the dashes, the SDK will handle this
        machine_serial=0,
        machine_id=0,
        software_version="1.337"
    )
    
    set_ws_message_callback(_ws_message_callback)
    
    await start()

    await api_call(METHOD_GET, f"/api/scorbitron_paired/{ScorbitSDK._net_instance.uuid}/", authorization=True, callback=_scorbitron_paired_callback)

    # Main loop
    while True:
        await tick()
        
        # Here you would typically add your game logic
        # For this example, we'll just wait for user input
        user_input = input("Press 'g' to get machine groups, or 'q' to quit: ")
        if user_input.lower() == 'g':
            await api_call(METHOD_GET, "/api/machinegroups/", authorization=True, callback=_machinegroups_callback)
        elif user_input.lower() == 'q':
            break

        await asyncio.sleep(0.1)  # Small delay to prevent busy-waiting

if __name__ == '__main__':
    asyncio.run(main())