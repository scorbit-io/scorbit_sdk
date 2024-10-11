# python/src/messages.py

import json

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