# python/src/log.py

from enum import Enum
from typing import Callable, Any
import threading

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
        self._logger_mutex = threading.Lock()

    def add_logger_callback(self, logger_function: LoggerCallback, user_data: Any = None):
        with self._logger_mutex:
            self._logger_function = logger_function
            self._user_data = user_data

    def reset_logger(self):
        with self._logger_mutex:
            self._logger_function = None
            self._user_data = None

    def log(self, message: str, level: LogLevel, file: str, line: int):
        if self._logger_function:
            with self._logger_mutex:
                self._logger_function(message, level, file, line, self._user_data)

_logger_instance = None
_logger_instance_lock = threading.Lock()

def logger() -> Logger:
    global _logger_instance
    if _logger_instance is None:
        with _logger_instance_lock:
            if _logger_instance is None:
                _logger_instance = Logger()
    return _logger_instance

def add_logger_callback(logger_function: LoggerCallback, user_data: Any = None):
    logger().add_logger_callback(logger_function, user_data)

def reset_logger():
    logger().reset_logger()

def log_message(level: LogLevel, file: str, line: int, message: str):
    logger().log(message, level, file, line)

# Convenience functions for logging
def log_debug(message: str, file: str, line: int):
    log_message(LogLevel.DEBUG, file, line, message)

def log_info(message: str, file: str, line: int):
    log_message(LogLevel.INFO, file, line, message)

def log_warn(message: str, file: str, line: int):
    log_message(LogLevel.WARN, file, line, message)

def log_error(message: str, file: str, line: int):
    log_message(LogLevel.ERROR, file, line, message)

# Macros equivalent
DEBUG = lambda message, file=__file__, line=__import__('sys')._getframe().f_lineno: log_debug(message, file, line)
INFO = lambda message, file=__file__, line=__import__('sys')._getframe().f_lineno: log_info(message, file, line)
WARN = lambda message, file=__file__, line=__import__('sys')._getframe().f_lineno: log_warn(message, file, line)
ERROR = lambda message, file=__file__, line=__import__('sys')._getframe().f_lineno: log_error(message, file, line)