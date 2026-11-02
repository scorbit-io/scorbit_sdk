# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License

import tempfile
import unittest

import scorbit


def _signer(digest):
    # A signature the backend would refuse; enough to create a game state offline
    return b"\x00" * 64


class TestAchievementsExports(unittest.TestCase):
    def test_enums_match_c_values(self):
        self.assertEqual(scorbit.EventType.AchievementUnlocked, 10)
        self.assertEqual(scorbit.AchievementArtwork.None_, 0)
        self.assertEqual(scorbit.AchievementArtwork.Frame, 1)
        self.assertEqual(scorbit.AchievementArtwork.Image, 2)

    def test_unlock_fields(self):
        unlock = scorbit.AchievementUnlock()
        self.assertEqual(unlock.key, "")
        self.assertEqual(unlock.player, 0)
        self.assertEqual(unlock.artwork, b"")


class TestAchievementsGameState(unittest.TestCase):
    def setUp(self):
        self.data_dir = tempfile.mkdtemp(prefix="sb-ach-py-")
        config = scorbit.Config()
        config.set_provider("vscorbitron")
        config.set_machine_id(4419)
        config.set_game_code_version("0.1.0")
        config.set_signer(_signer)
        config.set_data_dir(self.data_dir)
        config.set_achievement_artwork(scorbit.AchievementArtwork.Frame)
        self.gs = scorbit.create_game_state(config)

    def tearDown(self):
        self.gs.destroy()

    def test_game_events(self):
        self.gs.set_game_started(scorbit.GameStartOrigin.StartButton)
        self.gs.add_game_event("spins", 3)
        self.gs.add_game_event("spins", -1)
        self.gs.commit()
        self.gs.set_game_finished()


if __name__ == "__main__":
    unittest.main()
