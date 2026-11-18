"""Exercise firmware upload orchestration without contacting a board."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parents[1]


class UploadTests(unittest.TestCase):
    def setUp(self):
        """Copy the script into an isolated project and replace network commands."""
        if not shutil.which("jq"):
            self.skipTest("jq is required by the upload tool")
        self.temporary = tempfile.TemporaryDirectory(prefix="upload test ")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        tools = self.work / "tools"
        tools.mkdir()
        self.script = tools / "upload"
        shutil.copyfile(TOOLS / "upload", self.script)
        self.firmware = self.work / "Firmware/bin/TimeMachine.ino.bin"
        self.firmware.parent.mkdir(parents=True)
        self.firmware.write_bytes(b"simulated firmware")
        self.log = self.work / "calls.json"
        commands = self.work / "commands"
        commands.mkdir()
        curl = commands / "curl"
        curl.write_text('''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys
from urllib.parse import urlsplit

args = sys.argv[1:]
url = urlsplit(args[-1])
host = url.hostname
mode = json.loads(os.environ["UPLOAD_TEST_MODES"]).get(host, "ok")
log = Path(os.environ["UPLOAD_TEST_LOG"])
events = json.loads(log.read_text()) if log.exists() else []
previous = [event for event in events if event["host"] == host]
events.append({"host": host, "path": url.path, "args": args})
log.write_text(json.dumps(events))

if url.path == "/upload":
    if mode == "upload-failure":
        print("simulated upload failure", file=sys.stderr)
        raise SystemExit(22)
    print("Update OK. Rebooting...")
else:
    uploaded = any(event["path"] == "/upload" for event in previous)
    checks = sum(event["path"] == "/api/config" for event in previous) - 1
    if mode == "invalid-version":
        print('{"version":null}')
    elif mode == "http-failure":
        print('{"version":"0.6.0"}')
        raise SystemExit(22)
    elif uploaded and (mode == "never-recovers" or (mode == "slow-recovery" and checks < 2)):
        raise SystemExit(7)
    else:
        print(json.dumps({"version": "0.6.1" if uploaded else "0.6.0"}))
''')
        curl.chmod(0o700)
        sleep = commands / "sleep"
        sleep.write_text("#!/bin/sh\nexit 0\n")
        sleep.chmod(0o700)
        self.environment = dict(
            os.environ,
            PATH=str(commands) + os.pathsep + os.environ["PATH"],
            UPLOAD_TEST_LOG=str(self.log),
        )

    def run_upload(self, *hosts, modes=None):
        """Run from an unrelated working directory with deterministic board responses."""
        environment = dict(self.environment, UPLOAD_TEST_MODES=json.dumps(modes or {}))
        return subprocess.run(
            ["bash", str(self.script), *hosts], cwd=self.work,
            env=environment, capture_output=True, text=True,
        )

    def calls(self):
        """Return recorded network attempts, including HTTP options and destinations."""
        return json.loads(self.log.read_text()) if self.log.exists() else []

    def test_upload_failure_is_nonzero_and_other_boards_continue(self):
        """An error must survive a later board's successful upload."""
        result = self.run_upload("bad", "good", modes={"bad": "upload-failure"})
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("bad: firmware upload failed", result.stderr)
        self.assertIn("Board: good", result.stdout)
        self.assertEqual(result.stdout.count("New version:"), 1)
        bad_calls = [event for event in self.calls() if event["host"] == "bad"]
        self.assertEqual([event["path"] for event in bad_calls], ["/api/config", "/upload"])

    def test_recovery_is_polled_and_requests_have_timeouts(self):
        """Wait through temporary connection failures and bound every request."""
        result = self.run_upload("board", modes={"board": "slow-recovery"})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("New version: 0.6.1", result.stdout)
        calls = self.calls()
        self.assertEqual(sum(event["path"] == "/api/config" for event in calls), 4)
        for event in calls:
            self.assertIn("--connect-timeout", event["args"])
            self.assertIn("--max-time", event["args"])
            self.assertIn("--fail-with-body", event["args"])
        upload = next(event for event in calls if event["path"] == "/upload")
        firmware_argument = next(arg for arg in upload["args"] if arg.startswith("firmware=@"))
        self.assertEqual(Path(firmware_argument[len("firmware=@"):]).resolve(), self.firmware.resolve())

    def test_recovery_failure_is_bounded_and_nonzero(self):
        """Stop polling after the documented number of unsuccessful checks."""
        result = self.run_upload("board", modes={"board": "never-recovers"})
        self.assertEqual(result.returncode, 1)
        self.assertIn("did not recover after 30 checks", result.stderr)
        self.assertNotIn("New version:", result.stdout)
        self.assertEqual(sum(event["path"] == "/api/config" for event in self.calls()), 31)

    def test_invalid_or_failed_preflight_prevents_upload(self):
        """Reject missing versions and HTTP errors even when their JSON looks valid."""
        for mode in ("invalid-version", "http-failure"):
            with self.subTest(mode=mode):
                result = self.run_upload(mode, modes={mode: mode})
                self.assertEqual(result.returncode, 1)
                self.assertIn("skipping upload", result.stderr)
        self.assertFalse(any(event["path"] == "/upload" for event in self.calls()))

    def test_missing_arguments_or_binary_prevents_network_access(self):
        """Validate local prerequisites before attempting a board connection."""
        self.assertEqual(self.run_upload().returncode, 2)
        self.firmware.unlink()
        result = self.run_upload("board")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Run tools/build first", result.stderr)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
