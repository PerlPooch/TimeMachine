# ===========================================================================
# File:      tools/lfs_common.py
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

"""LittleFS image transfer helpers for the Time Machine ESP32-C6."""

import argparse
import csv
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time


PROJECT = Path(__file__).resolve().parents[1]
TABLE_OFFSET = 0x8000
TABLE_SIZE = 0x1000
BLOCK_SIZE = 4096
PAGE_SIZE = 256
RESET_DELAY_SECONDS = 1.0


@dataclass(frozen=True)
class Partition:
    name: str
    offset: int
    size: int
    subtype: int


def number(text):
    """Accept decimal, hexadecimal, and the partition CSV's K/M suffixes."""
    text = text.strip().upper()
    multiplier = {"K": 1024, "M": 1024 * 1024}.get(text[-1:], 1)
    return int(text[:-1] if multiplier != 1 else text, 0) * multiplier


def read_partition(path, label):
    with path.open(newline="") as source:
        rows = csv.reader(line for line in source if line.strip() and not line.lstrip().startswith("#"))
        matches = [list(map(str.strip, row)) for row in rows if row and row[0].strip() == label]

    if len(matches) != 1 or len(matches[0]) < 5:
        raise ValueError(f"Expected one partition named {label!r} in {path}")

    name, kind, subtype, offset, size, *flags = matches[0]
    if kind not in ("data", "1", "0x01"):
        raise ValueError("The selected partition must be a data partition")
    if any(flag for flag in flags):
        raise ValueError("Partition flags are not supported by these tools")

    subtype = {"spiffs": 0x82, "littlefs": 0x83}.get(subtype, subtype)
    if isinstance(subtype, str):
        subtype = number(subtype)
    if subtype not in (0x82, 0x83):
        raise ValueError("Select a SPIFFS/LittleFS data partition, not NVS or another data type")

    partition = Partition(name, number(offset), number(size), subtype)
    if (partition.offset < TABLE_OFFSET + TABLE_SIZE or partition.offset % BLOCK_SIZE
            or partition.size <= 0 or partition.size % BLOCK_SIZE
            or partition.offset + partition.size > 0x400000):
        raise ValueError("Partition must be aligned and fit the project's 4 MiB flash")

    return partition


def find_tool(name, override):
    if override:
        candidate = Path(override).expanduser()
        candidate = candidate if candidate.is_file() else Path(shutil.which(override) or override)
    else:
        # Prefer the newest installed ESP32 tool; keep PATH as a fallback.
        if sys.platform == "darwin":
            data = Path.home() / "Library/Arduino15"
        elif sys.platform == "win32":
            data = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "Arduino15"
        else:
            data = Path.home() / ".arduino15"

        package = "esptool_py" if name == "esptool" else name
        candidates = list((data / "packages/esp32/tools" / package).glob(f"*/{name}"))
        candidates += list((data / "packages/esp32/tools" / package).glob(f"*/{name}.exe"))
        candidates.sort(key=lambda item: tuple(map(int, re.findall(r"\d+", item.parent.name))), reverse=True)
        candidate = candidates[0] if candidates else Path(shutil.which(name) or name)

    if not candidate.is_file() or not os.access(candidate, os.X_OK):
        raise ValueError(f"Cannot find executable {name}; specify --{name} PATH")
    return candidate.resolve()


def run(command):
    subprocess.run([str(item) for item in command], check=True)


def run_mkfs(command):
    result = subprocess.run([str(item) for item in command], capture_output=True, text=True)
    print(result.stdout, end="", flush=True)
    print(result.stderr, end="", file=sys.stderr, flush=True)
    if result.returncode:
        raise subprocess.CalledProcessError(result.returncode, command)

    # Some mklittlefs builds return success even when LittleFS could not mount.
    if re.search(r"(?m)^.*\.(?:c|cpp):\d+:error:", result.stdout + result.stderr):
        raise ValueError("mklittlefs reported a filesystem error")


def flash_command(tool, args, *operation):
    return [tool, "--chip", "esp32c6", "--port", args.port, "--baud", str(args.baud), *operation]


def reset_board(tool, args):
    """Let the serial connection settle, then reset into the application."""
    print("Waiting one second before resetting the ESP32...", flush=True)
    time.sleep(RESET_DELAY_SECONDS)

    # Establish the normal boot-control line states before the exit reset.
    run(flash_command(tool, args, "--before", "default-reset",
                      "--after", "hard-reset", "read-mac"))


def verify_board(tool, args, partition, directory):
    """Read the board's table before accessing the configured filesystem range."""
    table = directory / "partition-table.bin"
    # Keep the stub active for the filesystem transfer and final reset.
    run(flash_command(tool, args, "--before", "default-reset", "--after", "no-reset-stub",
                      "read-flash", hex(TABLE_OFFSET), hex(TABLE_SIZE), table))
    data = table.read_bytes()
    if len(data) != TABLE_SIZE:
        raise ValueError("Downloaded partition table has the wrong size")

    matches = []
    for start in range(0, len(data), 32):
        entry = data[start:start + 32]
        if entry == b"\xff" * 32:
            break
        if entry[:2] == b"\xeb\xeb":
            if hashlib.md5(data[:start]).digest() != entry[16:]:
                raise ValueError("Board partition table checksum is invalid")
            break
        magic, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", entry)
        if magic != 0x50AA:
            raise ValueError("Board partition table is invalid")
        label = label.split(b"\0", 1)[0].decode("utf-8")
        if label == partition.name:
            matches.append((kind, subtype, offset, size, flags))

    expected = (1, partition.subtype, partition.offset, partition.size, 0)
    if matches != [expected]:
        raise ValueError("Board filesystem partition does not match the CSV; no filesystem transfer performed")


def image_size(path, partition):
    if path.stat().st_size != partition.size:
        raise ValueError(f"Image must be exactly {partition.size} bytes")


def copy_new(source, destination):
    """Keep backups private and refuse to overwrite an existing file."""
    descriptor = os.open(destination, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as output, source.open("rb") as input_file:
            shutil.copyfileobj(input_file, output)
    except BaseException:
        destination.unlink(missing_ok=True)
        raise


def unused(path):
    if path.exists() or path.is_symlink():
        raise ValueError(f"Output already exists: {path}; choose a new name")


def geometry(partition):
    return ["-b", str(BLOCK_SIZE), "-p", str(PAGE_SIZE), "-s", str(partition.size)]


def publish_download(tree, image, destination, backup, work):
    """Replace both outputs, restoring the previous folder if publishing fails."""
    previous = work / "previous"

    # Stage the image beside its destination so its final replacement is atomic.
    with tempfile.TemporaryDirectory(prefix=".lfs-image-", dir=backup.parent) as image_work:
        replacement = Path(image_work) / "littlefs.bin"
        copy_new(image, replacement)

        had_destination = destination.exists() or destination.is_symlink()
        if had_destination:
            destination.rename(previous)

        try:
            tree.rename(destination)
            try:
                os.replace(replacement, backup)
            except OSError:
                destination.rename(tree)
                raise
        except OSError:
            if had_destination:
                previous.rename(destination)
            raise


def download(args, partition, mkfs, esptool):
    destination = args.directory.expanduser().absolute()
    backup = (args.image or destination.with_name(destination.name + ".bin")).expanduser().absolute()

    # Replace a named symlink itself, rather than overwriting its target.
    destination = destination.parent.resolve() / destination.name
    backup = backup.parent.resolve() / backup.name
    if backup == destination or destination in backup.parents:
        raise ValueError("The raw backup must be outside the unpacked filesystem folder")
    if destination in Path(__file__).resolve().parents or destination == Path.home().resolve():
        raise ValueError("Choose a download folder outside the tools' own directory and its parents")
    if backup.is_dir() and not backup.is_symlink():
        raise ValueError(f"Raw backup path is a directory: {backup}")

    destination.parent.mkdir(parents=True, exist_ok=True)
    backup.parent.mkdir(parents=True, exist_ok=True)

    # Unpack off to the side; publish the folder only after a successful transfer.
    with tempfile.TemporaryDirectory(prefix=".lfs-download-", dir=destination.parent) as work:
        work = Path(work)
        image = work / "littlefs.bin"
        tree = work / "files"
        tree.mkdir(mode=0o700)

        verify_board(esptool, args, partition, work)
        run(flash_command(esptool, args, "--before", "no-reset-no-sync",
                          "--after", "no-reset-stub", "read-flash",
                          hex(partition.offset), hex(partition.size), image))
        image_size(image, partition)

        try:
            run_mkfs([mkfs, "-u", tree, *geometry(partition), image])
        except (ValueError, subprocess.CalledProcessError):
            # Preserve an earlier backup; retain the failed read if none exists yet.
            if not backup.exists() and not backup.is_symlink():
                copy_new(image, backup)
            raise

        publish_download(tree, image, destination, backup, work)

    print(f"Filesystem: {destination}")
    print(f"Raw backup: {backup}")
    reset_board(esptool, args)


def upload(args, partition, mkfs, esptool):
    source = args.directory.expanduser().resolve()
    if not source.is_dir():
        raise ValueError(f"Not a filesystem directory: {source}")

    output = args.pack_only.expanduser().absolute() if args.pack_only else None
    if output:
        unused(output)
        output = output.resolve()
        if source in output.parents:
            raise ValueError("The image output must be outside the filesystem source folder")

    with tempfile.TemporaryDirectory(prefix="lfs-upload-") as work:
        work = Path(work)
        if source == work.resolve() or source in work.resolve().parents:
            raise ValueError("The temporary image must be outside the filesystem source folder")
        image = work / "littlefs.bin"

        # Pack before contacting the board, so oversized input cannot reach flash.
        run_mkfs([mkfs, "-c", source, "-a", *geometry(partition), image])
        image_size(image, partition)

        if output:
            output.parent.mkdir(parents=True, exist_ok=True)
            copy_new(image, output)
            print(f"Packed image: {output}")
            return

        verify_board(esptool, args, partition, work)
        print(f"Replacing LittleFS with {source}; only files in that folder will remain.", flush=True)
        run(flash_command(esptool, args, "--before", "no-reset-no-sync",
                          "--after", "no-reset-stub", "write-flash",
                          hex(partition.offset), image))
        print("Filesystem uploaded. Firmware and Wi-Fi NVS were not replaced.")

    reset_board(esptool, args)


def main(operation):
    parser = argparse.ArgumentParser(description=(
        "Download and unpack the board's LittleFS, retaining a raw backup."
        if operation == "download" else
        "Pack a filesystem tree and replace the board's LittleFS with it."
    ))
    directory_help = ("New output folder" if operation == "download" else
                      "Filesystem root containing www/ and any config.json")
    parser.add_argument("directory", type=Path, help=directory_help)
    parser.add_argument("--port", help="Board serial port, e.g. /dev/cu.usbmodem...")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--partition-file", type=Path,
                        default=PROJECT / "Firmware/TimeMachine/partitions.csv")
    parser.add_argument("--partition", default="spiffs", help="Filesystem partition label (default: spiffs)")
    parser.add_argument("--mklittlefs", help="Path to mklittlefs (auto-detected by default)")
    parser.add_argument("--esptool", help="Path to esptool 5 or newer (auto-detected by default)")
    if operation == "download":
        parser.add_argument("--image", type=Path, help="Raw backup path (default: DIRECTORY.bin)")
    else:
        parser.add_argument("--pack-only", type=Path, metavar="IMAGE",
                            help="Build an image without contacting a board")
    args = parser.parse_args()
    if not args.port and not (operation == "upload" and args.pack_only):
        parser.error("--port is required for a board transfer")
    if args.baud <= 0:
        parser.error("--baud must be positive")

    try:
        partition = read_partition(args.partition_file.expanduser(), args.partition)
        mkfs = find_tool("mklittlefs", args.mklittlefs)
        esptool = None
        if operation == "download" or not args.pack_only:
            esptool = find_tool("esptool", args.esptool)
        print(f"LittleFS {partition.name}: offset {partition.offset:#x}, size {partition.size} bytes", flush=True)
        (download if operation == "download" else upload)(args, partition, mkfs, esptool)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"lfs-{operation}: {error}", file=sys.stderr)
        return 1
    return 0
