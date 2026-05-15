# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License

import importlib
import unittest
from unittest import mock
from ctypes import c_void_p

import scorbit


class TestLeaderboardWrapperConversion(unittest.TestCase):
    def test_exported_leaderboard_types(self):
        self.assertTrue(hasattr(scorbit, "LeaderboardPlayer"))
        self.assertTrue(hasattr(scorbit, "LeaderboardEntry"))
        self.assertTrue(hasattr(scorbit, "LeaderboardResult"))

    def test_game_state_converts_leaderboard_handle(self):
        game_state_mod = importlib.import_module("scorbit.game_state")
        gs = object.__new__(game_state_mod.GameState)
        gs._handle = None
        gs._async_callbacks = []

        fake_lib = mock.Mock()
        fake_lib.sb_leaderboard_entries_count.return_value = 1

        def set_u64(value):
            def _impl(_handle, _index, out):
                out._obj.value = value
                return True

            return _impl

        def set_i64(value):
            def _impl(_handle, _index, out):
                out._obj.value = value
                return True

            return _impl

        def set_i(value):
            def _impl(_handle, _index, out):
                out._obj.value = value
                return True

            return _impl

        def set_b(value):
            def _impl(_handle, _index, out):
                out._obj.value = value
                return True

            return _impl

        def set_s(value):
            encoded = value.encode("utf-8")

            def _impl(_handle, _index, out):
                out._obj.value = encoded
                return True

            return _impl

        def missing(*_args):
            return False

        fake_lib.sb_leaderboard_entry_id.side_effect = set_u64(3828382)
        fake_lib.sb_leaderboard_entry_rank.side_effect = set_i(1)
        fake_lib.sb_leaderboard_entry_high_score.side_effect = set_i64(146250)
        fake_lib.sb_leaderboard_entry_image.side_effect = set_s(
            "https://cdn-staging.scorbit.io/leaderboards/3828382.jpg"
        )
        fake_lib.sb_leaderboard_entry_reaction_count.side_effect = set_i(0)
        fake_lib.sb_leaderboard_entry_score_count.side_effect = set_i(12)
        fake_lib.sb_leaderboard_entry_is_nfc_verified.side_effect = set_b(False)
        fake_lib.sb_leaderboard_entry_is_verified.side_effect = set_b(True)
        fake_lib.sb_leaderboard_entry_is_vpin.side_effect = set_b(False)
        fake_lib.sb_leaderboard_entry_created.side_effect = set_s("2026-03-31T08:14:10.091057Z")
        fake_lib.sb_leaderboard_entry_player_id.side_effect = set_s(
            "016ae0a4-b1f8-7fc5-ba90-bd106d680829"
        )
        fake_lib.sb_leaderboard_entry_player_username.side_effect = set_s("dilshodm")
        fake_lib.sb_leaderboard_entry_player_display_name.side_effect = set_s("Dilshod")
        fake_lib.sb_leaderboard_entry_player_initials.side_effect = set_s("DTM")
        fake_lib.sb_leaderboard_entry_player_avatar.side_effect = set_s(
            "https://cdn-staging.scorbit.io/profile_pictures/dilshodm.jpg"
        )
        fake_lib.sb_leaderboard_entry_player_follower_count.side_effect = set_i(14)
        fake_lib.sb_leaderboard_entry_player_following_count.side_effect = set_i(10)
        fake_lib.sb_leaderboard_entry_player_last_login.side_effect = set_s(
            "2026-05-13T14:57:07.616568Z"
        )

        with mock.patch.object(game_state_mod, "_lib", fake_lib):
            result = game_state_mod.GameState._leaderboard_from_handle(gs, 123)

        self.assertIsInstance(result, scorbit.LeaderboardResult)
        self.assertEqual(len(result.entries), 1)
        self.assertEqual(result.entries[0].rank, 1)
        self.assertEqual(result.entries[0].high_score, 146250)
        self.assertEqual(
            result.entries[0].image,
            "https://cdn-staging.scorbit.io/leaderboards/3828382.jpg",
        )
        self.assertEqual(result.entries[0].player.display_name, "Dilshod")
        self.assertEqual(
            result.entries[0].player.avatar,
            "https://cdn-staging.scorbit.io/profile_pictures/dilshodm.jpg",
        )

    def test_request_top_scores_passes_since_and_vpin_filter(self):
        game_state_mod = importlib.import_module("scorbit.game_state")
        gs = object.__new__(game_state_mod.GameState)
        gs._handle = c_void_p(321)
        gs._async_callbacks = []

        fake_lib = mock.Mock()
        fake_cb = object()

        with mock.patch.object(game_state_mod, "_lib", fake_lib), mock.patch.object(
            game_state_mod.GameState, "_make_leaderboard_cb", return_value=fake_cb
        ):
            game_state_mod.GameState.request_top_scores(
                gs,
                scorbit.LeaderboardScope.Game,
                scorbit.LeaderboardPeriod.Days30,
                "2026-04-22T04:00:00Z",
                scorbit.LeaderboardVpinFilter.RealOnly,
                mock.Mock(),
            )

        fake_lib.sb_request_top_scores.assert_called_once_with(
            gs._handle,
            int(scorbit.LeaderboardScope.Game),
            int(scorbit.LeaderboardPeriod.Days30),
            b"2026-04-22T04:00:00Z",
            int(scorbit.LeaderboardVpinFilter.RealOnly),
            fake_cb,
            None,
        )
        gs._handle = None

    def test_leaderboard_callback_leaves_destroy_to_native_sdk(self):
        game_state_mod = importlib.import_module("scorbit.game_state")
        gs = object.__new__(game_state_mod.GameState)
        gs._handle = None
        gs._async_callbacks = []

        fake_lib = mock.Mock()
        received = {}

        def callback(error, leaderboard):
            received["error"] = error
            received["leaderboard"] = leaderboard

        with mock.patch.object(game_state_mod, "_lib", fake_lib), mock.patch.object(
            game_state_mod.GameState,
            "_leaderboard_from_handle",
            return_value=scorbit.LeaderboardResult(),
        ), mock.patch.object(game_state_mod._config_mod, "_shutting_down", False):
            cb = game_state_mod.GameState._make_leaderboard_cb(gs, callback)
            cb(0, 123, None)

        self.assertEqual(received["error"], scorbit.Error.Success)
        self.assertIsInstance(received["leaderboard"], scorbit.LeaderboardResult)

    def test_leaderboard_callback_shutdown_skips_python_callback_native_still_frees(self):
        game_state_mod = importlib.import_module("scorbit.game_state")
        gs = object.__new__(game_state_mod.GameState)
        gs._handle = None
        gs._async_callbacks = []

        fake_lib = mock.Mock()
        callback = mock.Mock()

        with mock.patch.object(game_state_mod, "_lib", fake_lib), mock.patch.object(
            game_state_mod._config_mod, "_shutting_down", True
        ):
            cb = game_state_mod.GameState._make_leaderboard_cb(gs, callback)
            cb(0, 456, None)

        callback.assert_not_called()


if __name__ == "__main__":
    unittest.main()
