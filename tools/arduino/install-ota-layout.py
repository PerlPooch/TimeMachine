#!/usr/bin/env python3
"""Add an optional XIAO ESP32-C6 OTA partition menu; never changes existing choices."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("platform", type=Path, help="Installed ESP32 Arduino platform directory")
parser.add_argument("--partition-file", type=Path, default=Path(__file__).resolve().parents[2] / "Firmware/TimeMachine/partitions.csv")
args = parser.parse_args()
assert (args.platform / "boards.txt").is_file(), "Not an Arduino platform directory"
source = args.partition_file
identifier = "XIAO_ESP32C6.menu.PartitionScheme.tm_ota"
entry = "\n".join([
    identifier + "=Time Machine 4MB OTA (1.75MB APP/384KB FS)",
    identifier + ".build.partitions=timemachine_ota",
    identifier + ".upload.maximum_size=1835008",
]) + "\n"
destination = args.platform / "tools/partitions/timemachine_ota.csv"
assert destination.parent.is_dir(), "Partition table directory not found"
source_bytes = source.read_bytes()
if destination.exists():
    assert destination.read_bytes() == source_bytes, "Conflicting partition CSV exists; review before replacing"
target = args.platform / "boards.local.txt"
old = target.read_text() if target.exists() else ""
if identifier in old:
    assert entry.strip() in old, "Conflicting custom menu already exists; review it first"
else:
    target.write_text(old + "\n" + entry)
if not destination.exists():
    destination.write_bytes(source_bytes)
print("Added optional Time Machine OTA partition menu. Restart Arduino IDE to select it.")
