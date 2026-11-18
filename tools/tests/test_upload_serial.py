"""Check serial firmware upload commands without opening a serial port."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parents[1]
FQBN = "esp32:esp32:XIAO_ESP32C6:CDCOnBoot=cdc,PartitionScheme=tm_ota"


class SerialUploadTests(unittest.TestCase):
    def setUp(self):
        """Prepare a complete fake build and an Arduino CLI command recorder."""
        self.temporary = tempfile.TemporaryDirectory(prefix="serial upload test ")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        tools = self.work / "tools"
        tools.mkdir()
        self.script = tools / "upload-serial"
        shutil.copyfile(TOOLS / "upload-serial", self.script)
        self.build = self.work / "Firmware/bin"
        self.build.mkdir(parents=True)
        self.sketch = self.work / "Firmware/TimeMachine"
        self.sketch.mkdir()
        for name in ("TimeMachine.ino.bin", "TimeMachine.ino.bootloader.bin", "TimeMachine.ino.partitions.bin"):
            (self.build / name).write_bytes(b"build artifact")
        self.config = self.work / "arduino-cli.yaml"
        self.config.write_text("{}\n")
        self.log = self.work / "calls.json"
        self.cli = self.work / "arduino-cli"
        self.cli.write_text('''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

args = sys.argv[1:]
log = Path(os.environ["SERIAL_TEST_LOG"])
calls = json.loads(log.read_text()) if log.exists() else []
calls.append(args)
log.write_text(json.dumps(calls))
port = args[args.index("--port") + 1]
raise SystemExit(1 if port == "BAD_PORT" else 0)
''')
        self.cli.chmod(0o700)
        self.environment = dict(os.environ, ARDUINO_CLI=str(self.cli),
                                ARDUINO_CLI_CONFIG=str(self.config), SERIAL_TEST_LOG=str(self.log))

    def run_upload(self, *ports):
        """Invoke from an unrelated directory to check path independence."""
        return subprocess.run(["bash", str(self.script), *ports], cwd=self.work,
                              env=self.environment, capture_output=True, text=True)

    def calls(self):
        """Read recorded Arduino invocations, if any."""
        return json.loads(self.log.read_text()) if self.log.exists() else []

    def test_upload_uses_serial_recipe_and_existing_artifacts(self):
        """Use the OTA board configuration and binary directory without recompiling."""
        result = self.run_upload("/dev/cu.usbmodem123", "/dev/cu.usbmodem456")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        calls = self.calls()
        self.assertEqual(len(calls), 2)
        for call, port in zip(calls, ("/dev/cu.usbmodem123", "/dev/cu.usbmodem456")):
            self.assertEqual(call[0], "upload")
            self.assertEqual(call[call.index("--port") + 1], port)
            self.assertEqual(call[call.index("--fqbn") + 1], FQBN)
            self.assertEqual(call[call.index("--protocol") + 1], "serial")
            self.assertEqual(Path(call[call.index("--input-dir") + 1]).resolve(), self.build.resolve())
            self.assertEqual(Path(call[-1]).resolve(), self.sketch.resolve())
            self.assertEqual(call[call.index("--config-file") + 1], str(self.config))

    def test_failed_board_does_not_hide_failure_or_skip_later_boards(self):
        """Return nonzero even when a later serial upload succeeds."""
        result = self.run_upload("BAD_PORT", "GOOD_PORT")
        self.assertEqual(result.returncode, 1)
        self.assertIn("BAD_PORT: serial firmware upload failed", result.stderr)
        self.assertEqual(len(self.calls()), 2)

    def test_missing_artifacts_prevent_serial_access(self):
        """Require the application, bootloader, and partition table before upload."""
        for artifact in self.build.iterdir():
            with self.subTest(artifact=artifact.name):
                contents = artifact.read_bytes()
                artifact.write_bytes(b"")
                result = self.run_upload("PORT")
                self.assertEqual(result.returncode, 2)
                self.assertIn("Run tools/build first", result.stderr)
                artifact.write_bytes(contents)
        self.assertEqual(self.calls(), [])

    def test_usage_and_missing_configuration_prevent_serial_access(self):
        """Handle help and prerequisite errors without launching Arduino CLI."""
        self.assertEqual(self.run_upload().returncode, 2)
        self.assertEqual(self.run_upload("--help").returncode, 0)
        self.config.unlink()
        self.assertEqual(self.run_upload("PORT").returncode, 2)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
