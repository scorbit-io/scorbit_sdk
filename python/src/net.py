import asyncio
import multiprocessing
import time
import hashlib
import json
from typing import Dict, Any, Callable
import websockets
import aiohttp

from ecdsa import SigningKey, NIST256p

from .net_base import NetBase
class Net(NetBase):
    def __init__(self):
        self._session = None
        self.domain = ""
        self.provider = ""
        self.private_key = b""
        self.uuid = ""
        self.machine_serial = 0
        self.machine_id = 0
        self.software_version = ""
        self.session_token = None
        self.api_url = ""
        self.websocket_url = ""
        self.websocket = None
        self.ws_callback = None
        self.api_call_number = 0
        self.websocket_queue = []
        self.websocket_task = None
        self.process = None
        self.pipe = None
        self.ws_message_callback = None
        self.ws_callbacks = {}
        self.developer_token = ""
    
    async def initialize(self, domain: str, provider: str, private_key: str, uuid: str,
                     machine_serial: int, machine_id: int, software_version: str,
                     developer_token: str = ""):
        self.domain = domain
        self.provider = provider
        self.private_key = bytes.fromhex(private_key.replace(":", ""))
        self.uuid = uuid
        self.machine_serial = machine_serial
        self.machine_id = machine_id
        self.software_version = software_version
        self.api_url = f'https://{self.domain}/'
        self.websocket_url = f'wss://{self.domain}/ws/'
        self.developer_token = developer_token
        
        self._session = aiohttp.ClientSession()
        
        await self._get_session_token()
        await self._send_installed_data()
    
    async def authenticate(self):
        timestamp = str(int(time.time()))
        nonce = bytes.fromhex(self.uuid) + timestamp.encode('utf-8')

        sk = SigningKey.from_string(self.private_key, curve=NIST256p)
        signature = sk.sign(nonce)

        payload = {
            'provider': self.provider,
            'uuid': self.uuid,
            'serial_number': self.machine_serial,
            'machine_id': self.machine_id,
            'timestamp': timestamp,
            'sign': signature.hex()
        }

        result = await self.api_call("POST", "/api/stoken/", data=payload, authorization=False)
        if 'stoken' in result:
            self.session_token = result['stoken']
            return True
        return False
    
    async def start_heartbeat(self, interval=10):
        self.heartbeat_interval = interval
        self.heartbeat_task = asyncio.create_task(self._heartbeat_loop())

    async def stop_heartbeat(self):
        if self.heartbeat_task:
            self.heartbeat_task.cancel()
            try:
                await self.heartbeat_task
            except asyncio.CancelledError:
                pass
            self.heartbeat_task = None

    async def _heartbeat_loop(self):
        while True:
            await self._send_heartbeat()
            await asyncio.sleep(self.heartbeat_interval)

    async def _send_heartbeat(self):
        try:
            result = await self.api_call("POST", "/api/heartbeat/", authorization=True)
            print("Heartbeat sent successfully")
        except Exception as e:
            print(f"Failed to send heartbeat: {e}")
            
    async def process_messages(self):
        # Process API call responses and WebSocket messages
        # This method should be called regularly (e.g., in a game loop)
        pass

    def set_ws_callback(self, callback: Callable):
        self.ws_message_callback = callback
    def start_process(self):
        self.pipe, child_pipe = multiprocessing.Pipe()
        self.process = multiprocessing.Process(target=self._run_in_process, args=(child_pipe,))
        self.process.start()

    def _run_in_process(self, pipe):
        asyncio.run(self._process_main(pipe))
    
    async def _process_main(self, pipe):
        while True:
            try:
                if pipe.poll():
                    message = pipe.recv()
                    if isinstance(message, ScorbitRESTMessage):
                        result = await self.api_call(message.method, message.endpoint, message.data, message.files, message.authorization)
                        response = ScorbitRESTResponse(message, result)
                        pipe.send(response)
                    elif isinstance(message, ScorbitWSMessage):
                        await self.ws_send(message.command, message.data)
                await asyncio.sleep(0.1)
            except Exception as e:
                print(f"Process error: {e}")
                await asyncio.sleep(1)

    async def api_call(self, method: str, endpoint: str, data: Dict[str, Any] = None, 
                   files: Dict[str, Any] = None, authorization: bool = False, 
                   callback: Callable[[Dict[str, Any]], None] = None) -> Dict[str, Any]:
        self.api_call_number += 1
        headers = {}
        if authorization:
            if self.developer_token:
                headers['Authorization'] = f'Token {self.developer_token}'
            else:
                headers['Authorization'] = f'Token {self.session_token}'
        url = self._get_endpoint_url(endpoint)
        try:
            async with asyncio.timeout(8):  # 8-second timeout
                if files:
                    form_data = aiohttp.FormData()
                    for key, value in files.items():
                        form_data.add_field(key, value)
                    async with self._session.request(method, url, data=form_data, headers=headers) as response:
                        response.raise_for_status()
                        result = await response.json()
                else:
                    async with self._session.request(method, url, json=data, headers=headers) as response:
                        response.raise_for_status()
                        result = await response.json()

                # Invoke the callback if provided
                if callback:
                    callback(result)

                return result
        except asyncio.TimeoutError:
            print(f"API call to {endpoint} timed out")
            raise

    async def ws_send(self, command: str, data: Dict[str, Any]):
        message = ScorbitWSMessage(command, data)
        self.websocket_queue.append(message)
        return await self._wait_for_response(command)
    
    async def _wait_for_response(self, command: str):
        # Implement waiting for the specific command response
        # This is a placeholder implementation and should be adjusted based on your specific needs
        for _ in range(100):  # Wait for up to 10 seconds
            await asyncio.sleep(0.1)
            # Check if the response for this command has been received
            # You might need to implement a mechanism to track sent commands and their responses
        raise TimeoutError(f"No response received for command: {command}")

    async def send_game_data(self, data: Dict[str, Any]):
        await self.ws_send("ENTRY", data)

    def set_ws_callback(self, callback: Callable[[Dict[str, Any]], None]):
        self.ws_callback = callback

    async def start(self):
        # Initialize the session if it hasn't been done already
        if not self._session:
            self._session = aiohttp.ClientSession()
        
        # Authenticate and get the session token
        await self._get_session_token()
        
        # Send installed data
        await self._send_installed_data()
        
        # Start the WebSocket handler
        self.websocket_task = asyncio.create_task(self._websocket_handler())

        print("Network connection started and authenticated")

    async def stop(self):
        if self.websocket_task:
            self.websocket_task.cancel()
            try:
                await self.websocket_task
            except asyncio.CancelledError:
                pass

    async def _websocket_handler(self):
        while True:
            try:
                ws_url = f"{self.websocket_url}?token={await self._get_websocket_token()}"
                async with websockets.connect(ws_url) as websocket:
                    self.websocket = websocket
                    await asyncio.gather(
                        self._ws_sender(),
                        self._ws_receiver()
                    )
            except Exception as e:
                print(f"WebSocket error: {e}")
                await asyncio.sleep(5)
            print("Reconnecting WebSocket...")

    async def _ws_sender(self):
        while True:
            if self.websocket_queue:
                message = self.websocket_queue.pop(0)
                await self.websocket.send(message.get_json())
            await asyncio.sleep(0.1)

    async def _ws_receiver(self):
        while True:
            try:
                message = await self.websocket.recv()
                response = ScorbitWebSocketResponse(json.loads(message))
                if response.is_background_message():
                    self._handle_background_message(response)
                elif response.command.upper() in self.ws_callbacks:
                    self.ws_callbacks[response.command.upper()](response)
                elif self.ws_message_callback:
                    self.ws_message_callback(response)
            except websockets.ConnectionClosed:
                break

    def _handle_background_message(self, message):
        # Handle background messages (e.g., COIN_DROP, balance updates)
        print(f"Received background message: {message}")
        # Implement specific handling based on message type

    async def _get_session_token(self) -> str:
        if not self.session_token:
            data = {
                'provider': self.provider,
                'uuid': self.uuid,
                'serial_number': self.machine_serial,
                'timestamp': self._get_timestamp_str(),
                'sign': self._get_signature()
            }
            result = await self.api_call("POST", "/api/stoken/", data)
            self.session_token = result['stoken']
        return self.session_token

    async def _get_websocket_token(self) -> str:
        result = await self.api_call("GET", "/api/auth/ws/", authorization=True)
        return result['token']

    async def _send_installed_data(self):
        await self.api_call("POST", "/api/installed/", {
            "type": "score_detector",
            "version": self.software_version,
            "installed": True
        }, authorization=True)

    def _get_signature(self) -> str:
        nonce = bytes.fromhex(self.uuid) + self._get_timestamp_str().encode('utf-8')
        sk = SigningKey.from_string(self.private_key, curve=NIST256p, hashfunc=hashlib.sha256)
        signature_bytes = sk.sign(nonce)
        return signature_bytes.hex()

    def _get_endpoint_url(self, endpoint: str) -> str:
        return self.api_url + endpoint.lstrip("/")

    def _get_timestamp_str(self) -> str:
        return str(int(time.time()))

    async def close(self):
        if self._session:
            await self._session.close()
        if self.websocket:
            await self.websocket.close()
        if self.websocket_task:
            self.websocket_task.cancel()
            try:
                await self.websocket_task
            except asyncio.CancelledError:
                pass
    async def send_installed_data(self, data: dict):
        return await self.api_call("POST", "/api/installed/", data=data, authorization=True)

    async def get_config(self):
        return await self.api_call("GET", "/api/config/", authorization=True)

    async def get_achievements(self):
        return await self.api_call("GET", "/api/achievements/", authorization=True)

    async def post_score(self, score_data: dict):
        return await self.api_call("POST", "/api/scores/", data=score_data, authorization=True)

    async def unlock_achievement(self, user_id: str, achievement_id: str):
        data = {
            "user_id": user_id,
            "achievement_id": achievement_id
        }
        return await self.api_call("POST", "/api/achievements/unlock/", data=data, authorization=True)

    async def increment_achievement(self, achievement_id: str, increment: int):
        data = {
            "achievement_id": achievement_id,
            "increment": increment
        }
        return await self.api_call("POST", "/api/achievements/increment/", data=data, authorization=True)

    async def set_achievement_achieved(self, achievement_id: str):
        return await self.api_call("POST", f"/api/achievements/achieve/{achievement_id}/", authorization=True)
    
    async def upload_session_log(self, uuid: str, filename: str, content: str):
        data = {"uuid": uuid}
        files = {"log_file": (filename, content)}
        return await self.api_call("POST", "/api/session_log/", data=data, files=files, authorization=True)
    
    async def send_installed_data(self, installed_data):
        return await self.api_call("POST", "/api/installed/", data=installed_data, authorization=True)

    async def get_config(self):
        return await self.api_call("GET", "/api/config/", authorization=True)
class ScorbitRESTMessage:
    def __init__(self, api_call_number, method, endpoint, data, files, authorization):
        self.api_call_number = api_call_number
        self.method = method
        self.endpoint = endpoint
        self.data = data
        self.files = files
        self.authorization = authorization
        self.result = None

class ScorbitRESTResponse:
    def __init__(self, message, result):
        self.message: ScorbitRESTMessage = message
        self.result: dict = result

class ScorbitWSMessage:
    def __init__(self, command, data):
        self.command = command
        self.data = data

    def get_json(self):
        return json.JSONEncoder().encode({
            "message": {
                "cmd": self.command,
                "data": self.data,
            }
        })
class ScorbitWebSocketResponse:
    def __init__(self, result):
        self.result = result