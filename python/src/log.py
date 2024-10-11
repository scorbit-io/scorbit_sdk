# python/src/log.py

from enum import Enum
from typing import Callable, Any

class LogLevel(Enum):
    DEBUG = 0
    INFO = 1
    WARN = 2
    ERROR = 3

LoggerCallback = Callable[[str, LogLevel, str, int, Any], None]

class Logger:
    def __init__(self):
        self._logger_function = None
        self._user_data = None

    def register_logger(self, logger_function: LoggerCallback, user_data: Any = None):
        self._logger_function = logger_function
        self._user_data = user_data

    def unregister_logger(self):
        self._logger_function = None
        self._user_data = None

    def log(self, message: str, level: LogLevel, file: str, line: int):
        if self._logger_function:
            self._logger_function(message, level, file, line, self._user_data)

_logger_instance = None

def logger() -> Logger:
    global _logger_instance
    if _logger_instance is None:
        _logger_instance = Logger()
    return _logger_instance

def register_logger(logger_function: LoggerCallback, user_data: Any = None):
    logger().register_logger(logger_function, user_data)

def unregister_logger():
    logger().unregister_logger()

# Convenience functions for logging
def log_debug(message: str, file: str, line: int):
    logger().log(message, LogLevel.DEBUG, file, line)

def log_info(message: str, file: str, line: int):
    logger().log(message, LogLevel.INFO, file, line)

def log_warn(message: str, file: str, line: int):
    logger().log(message, LogLevel.WARN, file, line)

def log_error(message: str, file: str, line: int):
    logger().log(message, LogLevel.ERROR, file, line)