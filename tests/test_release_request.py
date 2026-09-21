"""Fast host-side regression tests for GameBoy release request selection."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.resolve_release_request import resolve


class ReleaseRequestTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "platformio.ini").write_text("[version]\nVersion = 1.2.1\n", encoding="utf-8")
        (self.root / ".github").mkdir()
        (self.root / ".github" / "release-request.json").write_text(
            '{"enabled": false, "tag": "v1.2.1"}\n', encoding="utf-8"
        )

    @patch("scripts.resolve_release_request.subprocess.run")
    def test_tag_publishes_independently_of_disabled_request(self, run):
        run.return_value = subprocess.CompletedProcess([], 0)
        with patch.dict(os.environ, {"GITHUB_REF": "refs/tags/v1.2.1"}):
            self.assertEqual(resolve("push", {}, self.root), {
                "publish": "true", "tag": "v1.2.1", "version": "1.2.1"
            })
        run.assert_called_once_with(
            ["git", "merge-base", "--is-ancestor", "HEAD", "origin/master"],
            cwd=self.root, check=False,
        )

    @patch("scripts.resolve_release_request.subprocess.run")
    def test_tag_must_point_to_master(self, run):
        run.return_value = subprocess.CompletedProcess([], 1)
        with patch.dict(os.environ, {"GITHUB_REF": "refs/tags/v1.2.1"}):
            with self.assertRaisesRegex(ValueError, "does not point to a commit on master"):
                resolve("push", {}, self.root)

    def test_tag_version_must_match_platformio(self):
        with patch.dict(os.environ, {"GITHUB_REF": "refs/tags/v1.2.2"}):
            with self.assertRaisesRegex(ValueError, "does not match platformio.ini"):
                resolve("push", {}, self.root)

    def test_master_request_disabled(self):
        with patch.dict(os.environ, {"GITHUB_REF": "refs/heads/master"}):
            self.assertEqual(resolve("push", {}, self.root)["publish"], "false")

    def test_manual_release_on_master(self):
        with patch.dict(os.environ, {"GITHUB_REF": "refs/heads/master"}):
            self.assertEqual(resolve("workflow_dispatch", {"inputs": {"tag": "v1.2.1"}}, self.root)["publish"], "true")

    def test_manual_release_on_other_branch_rejected(self):
        with patch.dict(os.environ, {"GITHUB_REF": "refs/heads/feature"}):
            with self.assertRaisesRegex(ValueError, "master"):
                resolve("workflow_dispatch", {"inputs": {"tag": "v1.2.1"}}, self.root)


if __name__ == "__main__":
    unittest.main()
