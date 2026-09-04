"""Platform routing contracts; no CEF, network, input, or subprocess launch."""

import unittest

import run_page_snapshot_fixture as fixture


class ScenarioSelectionTest(unittest.TestCase):
    def test_macos_keeps_its_ui_and_all_shared_scenarios(self):
        expected = tuple(
            name for name in fixture.AUTOMATED_SCENARIOS
            if name != "media-cast-ui-win"
        )
        self.assertEqual(fixture.select_scenarios("darwin"), expected)
        self.assertIn("media-cast-ui", expected)

    def test_windows_keeps_its_physical_input_scenario(self):
        expected = tuple(
            name for name in fixture.AUTOMATED_SCENARIOS
            if name not in ("media-cast-ui", "media-navigation",
                            "media-source-reload", "media-player-replace")
        )
        self.assertEqual(fixture.select_scenarios("win32"), expected)
        self.assertIn("media-cast-ui-win", expected)

    def test_wrong_platform_and_unknown_requests_are_rejected(self):
        self.assertIsNone(fixture.select_scenarios("darwin", "media-cast-ui-win"))
        self.assertIsNone(fixture.select_scenarios("win32", "media-cast-ui"))
        self.assertIsNone(fixture.select_scenarios("win32", "media-navigation"))
        self.assertIsNone(fixture.select_scenarios("win32", "media-source-reload"))
        self.assertIsNone(fixture.select_scenarios("win32", "media-player-replace"))
        self.assertIsNone(fixture.select_scenarios("darwin", "unknown"))

    def test_content_manual_and_shared_requests_are_unchanged(self):
        for platform in ("darwin", "win32"):
            self.assertEqual(
                fixture.select_scenarios(platform, "content"),
                fixture.CONTENT_SCENARIOS,
            )
            for name in ("normal", "media-manual", "media-forged"):
                self.assertEqual(fixture.select_scenarios(platform, name), (name,))


if __name__ == "__main__":
    unittest.main()
