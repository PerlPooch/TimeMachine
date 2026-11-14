# ===========================================================================
# File:      tools/tests/test_lfs.py
# Firmware:  0.5.1
# Git base:  dafb18db29e0
#
# Copyright (c) 2026 Marc D. Spencer.
# All Rights Reserved under the applicable license.
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# ===========================================================================

"""Exercise the transfer tools with real LittleFS images and a simulated board."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from lfs_common import find_tool, publish_download


class TransferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mkfs = find_tool("mklittlefs", os.environ.get("MKLITTLEFS"))

    def setUp(self):
        """Prepare visible files and hidden entries in a simulated board filesystem."""
        self.temporary = tempfile.TemporaryDirectory(prefix="lfs-test-")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.csv = self.work / "partitions.csv"
        self.csv.write_text("# Label, type, subtype, offset, size\nspiffs,data,spiffs,0x390000,384K\n")
        self.source = self.work / "source"
        (self.source / "www/assets").mkdir(parents=True)
        self.packed_files = {
            "config.json": b'{"schema":1,"name":"Saved","owAppKey":"keep-private"}',
            "www/index.htm": b'<img src="/assets/logo.png">',
            "www/assets/logo.png": b"\x89PNG\x00\xff\x01",
        }
        self.files = dict(self.packed_files)
        self.files.update({
            ".DS_Store": b"Finder metadata",
            ".hidden": b"root dotfile",
            ".cache/visible.txt": b"file inside hidden directory",
            "www/assets/.hidden": b"nested dotfile",
            "www/assets/.cache/visible.txt": b"nested hidden directory",
        })
        for name, contents in self.files.items():
            (self.source / name).parent.mkdir(parents=True, exist_ok=True)
            (self.source / name).write_bytes(contents)

        self.image = self.work / "board.bin"
        self.native("-c", self.source, "-a", "-s", "393216", self.image)
        self.write_table(0x390000)
        self.log = self.work / "calls.jsonl"
        self.fake = self.work / "esptool"
        self.fake.write_text('''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import shutil
import sys
import time
work = Path(os.environ["LFS_TEST_BOARD"])
args = sys.argv[1:]
with (work / "calls.jsonl").open("a") as log:
    log.write(json.dumps({"args": args, "at": time.time()}) + "\\n")
if "read-flash" in args:
    start = args.index("read-flash")
    offset, size, output = args[start + 1:]
    source = work / ("table.bin" if int(offset, 0) == 0x8000 else "board.bin")
    assert args[args.index("--after") + 1] == "no-reset-stub"
    if int(offset, 0) != 0x8000:
        assert (work / "stub.active").exists()
        assert args[args.index("--before") + 1] == "no-reset-no-sync"
    (work / "stub.active").touch()
    shutil.copyfile(source, output)
elif "write-flash" in args:
    assert (work / "stub.active").exists()
    assert args[args.index("--before") + 1] == "no-reset-no-sync"
    assert args[args.index("--after") + 1] == "no-reset-stub"
    start = args.index("write-flash")
    offset, source = args[start + 1:]
    assert int(offset, 0) == 0x390000
    shutil.copyfile(source, work / "written.bin")
elif "read-mac" in args:
    assert (work / "stub.active").exists()
    assert args[args.index("--after") + 1] == "hard-reset"
    assert args[args.index("--before") + 1] == "default-reset"
    assert "--no-stub" not in args
    if (work / "fail-reset").exists():
        raise SystemExit(7)
    (work / "reset.done").write_text("reset")
    (work / "stub.active").unlink()
else:
    raise SystemExit("Unexpected operation")
''')
        self.fake.chmod(0o700)

    def write_table(self, offset):
        entry = struct.pack("<HBBII16sI", 0x50AA, 1, 0x82, offset, 393216, b"spiffs", 0)
        checksum = b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entry).digest()
        (self.work / "table.bin").write_bytes((entry + checksum).ljust(4096, b"\xff"))

    def native(self, *args):
        return subprocess.run(
            [str(self.mkfs), "-b", "4096", "-p", "256", *map(str, args)],
            check=True, capture_output=True, text=True,
        )

    def command(self, operation, directory, *extra, success=True):
        environment = dict(os.environ, LFS_TEST_BOARD=str(self.work), PYTHONDONTWRITEBYTECODE="1")
        result = subprocess.run([
            sys.executable, str(TOOLS / f"lfs-{operation}"), str(directory),
            "--port", "SIMULATED", "--partition-file", str(self.csv),
            "--esptool", str(self.fake), "--mklittlefs", str(self.mkfs), *map(str, extra),
        ], env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        self.assertNotIn("keep-private", result.stdout + result.stderr)
        return result

    def calls(self):
        return [event["args"] for event in self.events()]

    def events(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def assert_delayed_reset(self):
        transfer, reset = self.events()[-2:]
        self.assertGreaterEqual(reset["at"] - transfer["at"], 0.9)
        self.assertIn("no-reset-stub", transfer["args"])
        self.assertIn("read-mac", reset["args"])
        self.assertIn("hard-reset", reset["args"])
        self.assertNotIn("--no-stub", reset["args"])
        self.assertNotIn("soft-reset", reset["args"])
        self.assertEqual(reset["args"][reset["args"].index("--before") + 1], "default-reset")

    def assert_tree(self, directory, expected):
        actual = {p.relative_to(directory).as_posix(): p.read_bytes() for p in directory.rglob("*") if p.is_file()}
        self.assertEqual(actual, expected)

    def test_download_edit_upload_roundtrip(self):
        """Download all board data, then repack only visible files after editing."""
        destination = self.work / "download"
        self.command("download", destination)
        self.assert_delayed_reset()
        self.assert_tree(destination, self.files)
        backup = self.work / "download.bin"
        self.assertFalse(backup.exists())
        self.assertEqual(destination.stat().st_mode & 0o777, 0o700)

        updated = b"updated homepage"
        (destination / "www/index.htm").write_bytes(updated)
        self.command("upload", destination)
        self.assert_delayed_reset()
        written = self.work / "written.bin"
        self.assertEqual(written.stat().st_size, 393216)
        unpacked = self.work / "verify"
        unpacked.mkdir()
        self.native("-u", unpacked, "-s", "393216", written)
        self.assert_tree(unpacked, dict(self.packed_files, **{"www/index.htm": updated}))
        self.assertEqual((destination / "www/assets/.hidden").read_bytes(),
                         self.files["www/assets/.hidden"])

    def test_existing_download_and_backup_are_replaced(self):
        destination = self.work / "download"
        destination.mkdir()
        marker = destination / "stale-local-file"
        marker.write_bytes(b"remove on refresh")
        backup = self.work / "download.bin"
        backup.write_bytes(b"old backup")

        self.command("download", destination, "--keep-image")
        self.assert_tree(destination, self.files)
        self.assertEqual(backup.read_bytes(), self.image.read_bytes())
        self.assertEqual(backup.stat().st_mode & 0o777, 0o600)
        self.assert_delayed_reset()

    def test_download_cleans_custom_image_after_success(self):
        """An explicit backup location still follows the default cleanup policy."""
        destination = self.work / "download"
        backup = self.work / "custom.bin"
        self.command("download", destination, "--image", backup)
        self.assert_tree(destination, self.files)
        self.assertFalse(backup.exists())

    def test_upload_cleans_sibling_image_after_success(self):
        """Remove an earlier raw image only after upload and reset both succeed."""
        backup = self.work / "source.bin"
        backup.write_bytes(b"previous image")
        self.command("upload", self.source)
        self.assertFalse(backup.exists())
        self.assert_delayed_reset()

    def test_upload_keep_image_retains_current_packed_data(self):
        """Keep the uploaded image, replacing an older backup with private permissions."""
        backup = self.work / "source.bin"
        backup.write_bytes(b"previous image")
        self.command("upload", self.source, "--keep-image")
        self.assertEqual(backup.read_bytes(), (self.work / "written.bin").read_bytes())
        self.assertEqual(backup.stat().st_mode & 0o777, 0o600)

    def test_upload_reset_failure_preserves_sibling_image(self):
        """A failed final reset must not delete the previous recovery image."""
        backup = self.work / "source.bin"
        backup.write_bytes(b"previous image")
        (self.work / "fail-reset").touch()
        for flags in ((), ("--keep-image",)):
            with self.subTest(flags=flags):
                self.command("upload", self.source, *flags, success=False)
                self.assertEqual(backup.read_bytes(), b"previous image")

    def test_failed_refresh_preserves_previous_folder_and_backup(self):
        destination = self.work / "download"
        destination.mkdir()
        marker = destination / "keep"
        marker.write_bytes(b"original")
        backup = self.work / "download.bin"
        backup.write_bytes(b"original backup")

        self.image.write_bytes(b"\xff" * 393216)
        self.command("download", destination, success=False)
        self.assertEqual(marker.read_bytes(), b"original")
        self.assertEqual(backup.read_bytes(), b"original backup")
        self.assertFalse((self.work / "reset.done").exists())

    def test_publish_failure_restores_previous_folder_and_backup(self):
        destination = self.work / "download"
        destination.mkdir()
        (destination / "keep").write_bytes(b"original")
        backup = self.work / "download.bin"
        backup.write_bytes(b"original backup")
        staging = self.work / "staging"
        tree = staging / "files"
        tree.mkdir(parents=True)
        (tree / "new").write_bytes(b"new content")

        with patch("lfs_common.os.replace", side_effect=OSError("simulated publication failure")):
            with self.assertRaises(OSError):
                publish_download(tree, self.image, destination, backup, staging)

        self.assert_tree(destination, {"keep": b"original"})
        self.assertEqual(backup.read_bytes(), b"original backup")

    def test_download_replaces_symlink_without_changing_its_target(self):
        untouched = self.work / "untouched"
        untouched.mkdir()
        (untouched / "keep").write_bytes(b"original")
        destination = self.work / "download"
        destination.symlink_to(untouched, target_is_directory=True)

        self.command("download", destination)
        self.assertFalse(destination.is_symlink())
        self.assert_tree(destination, self.files)
        self.assert_tree(untouched, {"keep": b"original"})

    def test_board_layout_mismatch_prevents_filesystem_access(self):
        self.write_table(0x290000)
        self.command("download", self.work / "download", success=False)
        self.command("upload", self.source, success=False)
        self.assertEqual(len(self.calls()), 2)
        for command in self.calls():
            self.assertIn("read-flash", command)
            self.assertIn("0x8000", command)
            self.assertIn("no-reset-stub", command)
        self.assertFalse((self.work / "written.bin").exists())

    def test_corrupt_partition_table_prevents_upload(self):
        table = bytearray((self.work / "table.bin").read_bytes())
        table[16] ^= 1
        (self.work / "table.bin").write_bytes(table)
        result = self.command("upload", self.source, success=False)
        self.assertIn("checksum", result.stderr)
        self.assertFalse((self.work / "written.bin").exists())

    def test_non_filesystem_partition_is_rejected_without_board_contact(self):
        self.csv.write_text("spiffs,data,0x02,0x9000,24K\n")
        self.command("upload", self.source, success=False)
        self.assertFalse(self.calls())

    def test_oversized_tree_fails_before_board_contact(self):
        (self.source / "too-large.bin").write_bytes(b"x" * 393216)
        self.command("upload", self.source, success=False)
        self.assertFalse(self.calls())

    def test_pack_only_excludes_dotfiles_and_preserves_binary_files(self):
        """Omit hidden entries recursively while leaving the original tree untouched."""
        output = self.work / "packed.bin"
        result = subprocess.run([
            sys.executable, str(TOOLS / "lfs-upload"), str(self.source),
            "--pack-only", str(output), "--partition-file", str(self.csv),
            "--mklittlefs", str(self.mkfs),
        ], env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(self.calls())
        unpacked = self.work / "verify"
        unpacked.mkdir()
        self.native("-u", unpacked, "-s", "393216", output)
        self.assert_tree(unpacked, self.packed_files)
        self.assertFalse((unpacked / ".cache").exists())
        self.assertFalse((unpacked / "www/assets/.cache").exists())
        self.assert_tree(self.source, self.files)
        self.command("upload", self.source, "--pack-only", output, success=False)

    def test_image_cannot_be_packed_into_its_own_source(self):
        self.command("upload", self.source, "--pack-only", self.source / "image.bin", success=False)
        self.assertFalse(self.calls())

    def test_truncated_download_is_not_published(self):
        self.image.write_bytes(b"truncated")
        destination = self.work / "download"
        self.command("download", destination, success=False)
        self.assertFalse(destination.exists())
        self.assertFalse((self.work / "download.bin").exists())
        self.assertFalse((self.work / "reset.done").exists())

    def test_reset_failure_is_reported_without_losing_download(self):
        (self.work / "fail-reset").touch()
        destination = self.work / "download"
        self.command("download", destination, success=False)
        self.assert_tree(destination, self.files)
        self.assertEqual((self.work / "download.bin").read_bytes(), self.image.read_bytes())
        self.assert_delayed_reset()

    def test_damaged_filesystem_retains_raw_backup(self):
        contents = b"\xff" * 393216
        self.image.write_bytes(contents)
        destination = self.work / "download"
        self.command("download", destination, success=False)
        self.assertFalse(destination.exists())
        self.assertEqual((self.work / "download.bin").read_bytes(), contents)


if __name__ == "__main__":
    unittest.main()
