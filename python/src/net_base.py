from abc import ABC, abstractmethod
from typing import Dict, Any, Callable

class NetBase(ABC):
    @abstractmethod
    async def initialize(self, domain: str, provider: str, private_key: str, uuid: str, machine_serial: int, machine_id: int, software_version: str):
        pass

    @abstractmethod
    async def api_call(self, method: str, endpoint: str, data: Dict[str, Any] = None, files: Dict[str, Any] = None, authorization: bool = False) -> Dict[str, Any]:
        pass

    @abstractmethod
    async def ws_send(self, command: str, data: Dict[str, Any]):
        pass

    @abstractmethod
    async def send_game_data(self, data: Dict[str, Any]):
        pass

    @abstractmethod
    def set_ws_callback(self, callback: Callable[[Dict[str, Any]], None]):
        pass

    @abstractmethod
    async def start(self):
        pass

    @abstractmethod
    async def stop(self):
        pass