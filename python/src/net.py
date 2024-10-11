# python/src/net.py

import requests
import websockets
import asyncio
from typing import Callable, Any, Dict

class Net:
    def __init__(self):
        self.session = requests.Session()
        self.ws_connection = None
        self.ws_callback = None

    async def connect_websocket(self, url: str, callback: Callable[[Dict[str, Any]], None]):
        self.ws_callback = callback
        self.ws_connection = await websockets.connect(url)
        await self.ws_listener()

    async def ws_listener(self):
        while True:
            try:
                message = await self.ws_connection.recv()
                if self.ws_callback:
                    self.ws_callback(message)
            except websockets.exceptions.ConnectionClosed:
                break

    def api_call(self, method: str, url: str, data: Dict[str, Any] = None, files: Dict[str, Any] = None, headers: Dict[str, str] = None) -> requests.Response:
        return self.session.request(method, url, data=data, files=files, headers=headers)

    async def ws_send(self, message: Dict[str, Any]):
        if self.ws_connection:
            await self.ws_connection.send(json.dumps(message))

net_instance = Net()

def get_net() -> Net:
    return net_instance