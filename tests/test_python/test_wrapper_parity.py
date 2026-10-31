# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License

"""Python wrappers for C entry points the C++ API already had (SB-6151)."""

import importlib
import json
import unittest
from unittest import mock

import scorbit


def _game_state():
    game_state_mod = importlib.import_module("scorbit.game_state")
    gs = object.__new__(game_state_mod.GameState)
    gs._handle = None
    gs._async_callbacks = []
    return game_state_mod, gs


class TestDownloadWithOptions(unittest.TestCase):
    def _options_sent(self, options):
        game_state_mod, gs = _game_state()
        fake_lib = mock.Mock()
        with mock.patch.object(game_state_mod, "_lib", fake_lib):
            gs.download_with_options(
                "https://example.invalid/f.bin", "/tmp/f.bin", options, callback=lambda e, r: None
            )
        args = fake_lib.sb_download_with_options.call_args[0]
        self.assertEqual(args[1], b"https://example.invalid/f.bin")
        self.assertEqual(args[2], b"/tmp/f.bin")
        return args[5]

    def test_a_dict_is_sent_as_json(self):
        sent = self._options_sent({"max_bytes": 1024, "redirect": "none"})
        self.assertEqual(json.loads(sent.decode("utf-8")), {"max_bytes": 1024, "redirect": "none"})

    def test_a_json_string_is_sent_as_is(self):
        self.assertEqual(self._options_sent('{"max_bytes": 5}'), b'{"max_bytes": 5}')

    def test_no_options_send_null(self):
        self.assertIsNone(self._options_sent(None))
        self.assertIsNone(self._options_sent({}))
        self.assertIsNone(self._options_sent(""))

    def test_a_callback_is_required(self):
        _, gs = _game_state()
        with self.assertRaises(TypeError):
            gs.download_with_options("https://example.invalid/f.bin", "/tmp/f.bin")


class TestLoggerCallbacksSupported(unittest.TestCase):
    def test_exported_whatever_the_backend(self):
        self.assertIn("logger_callbacks_supported", scorbit.__all__)
        self.assertIsInstance(scorbit.logger_callbacks_supported(), bool)

    def test_matches_the_logger_api_being_defined(self):
        self.assertEqual(
            scorbit.logger_callbacks_supported(), hasattr(scorbit, "add_logger_callback")
        )


if __name__ == "__main__":
    unittest.main()
