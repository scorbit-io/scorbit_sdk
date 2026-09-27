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
        self.assertEqual(scorbit.EventType.AchievementUpdated, 10)
        self.assertEqual(scorbit.AchievementStatus.UnlockedLocally, 1)
        self.assertEqual(scorbit.AchievementStatus.Retracted, 4)
        self.assertEqual(scorbit.AchievementRuleType.Achievement, 11)
        self.assertEqual(scorbit.AchievementComparison.Ne, 3)
        self.assertEqual(scorbit.AchievementEvaluation.Unlimited, 1)
        self.assertEqual(scorbit.AchievementScope.Global, 3)

    def test_types_exported(self):
        for name in (
            "Achievement",
            "AchievementRule",
            "AchievementProgress",
            "AchievementRuleProgress",
            "AchievementUpdate",
        ):
            self.assertTrue(hasattr(scorbit, name), name)


class TestAchievementsGameState(unittest.TestCase):
    def setUp(self):
        self.data_dir = tempfile.mkdtemp(prefix="sb-ach-py-")
        config = scorbit.Config()
        config.set_provider("vscorbitron")
        config.set_machine_id(4419)
        config.set_game_code_version("0.1.0")
        config.set_signer(_signer)
        config.set_data_dir(self.data_dir)
        self.gs = scorbit.create_game_state(config)

    def tearDown(self):
        self.gs.destroy()

    def test_empty_cache(self):
        self.assertEqual(self.gs.get_achievements(), [])
        self.assertIsNone(self.gs.find_achievement("game-cv-boom"))
        self.assertIsNone(self.gs.get_achievement_progress(1, "game-cv-boom"))
        self.assertIsNone(self.gs.get_achievement_frame("game-cv-boom"))

    def test_events_and_network_calls(self):
        self.gs.set_game_started(scorbit.GameStartOrigin.StartButton)
        self.gs.add_event("spins", 3)
        self.gs.add_event("spins", -1)
        self.gs.commit()
        self.gs.flush_achievement_reports()
        self.gs.refresh_achievements()
        self.gs.download_achievement_frames()
        self.gs.set_game_finished()


if __name__ == "__main__":
    unittest.main()
