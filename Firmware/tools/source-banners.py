#!/usr/bin/env python3
# ===========================================================================
# File:      tools/source-banners.py
# Firmware:  0.5.1
# Git base:  1e4e91f4dbce
#
# Copyright (c) 2026 Marc D. Spencer.
# All Rights Reserved under the applicable license.
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# ===========================================================================

"""Refresh source banners, or verify them with --check. Requires Python 3."""

import argparse
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
RULE = "=" * 75
SUFFIXES = {".ino", ".h", ".hpp", ".c", ".cpp", ".py", ".sh", ".csv"}
NOTICE = (
    "This Source Code Form is subject to the terms of the Mozilla Public",
    "License, v. 2.0. If a copy of the MPL was not distributed with this",
    "file, You can obtain one at https://mozilla.org/MPL/2.0/.",
)


def banner(path, version, revision):
    lines = [
        RULE,
        "File:      " + path.relative_to(ROOT).as_posix(),
        "Firmware:  " + version,
        "Git base:  " + revision,
        "",
        "Copyright (c) 2026 Marc D. Spencer.",
        "All Rights Reserved under the applicable license.",
        "SPDX-License-Identifier: MPL-2.0",
        "",
        *NOTICE,
        RULE,
    ]
    if path.suffix in {".py", ".sh", ".csv"}:
        return "\n".join("# " + line if line else "#" for line in lines) + "\n\n"
    return "/*\n" + "\n".join(" * " + line if line else " *" for line in lines) + "\n */\n\n"


def split_source(source):
    shebang = ""
    if source.startswith("#!"):
        shebang, source = source.split("\n", 1)
        shebang += "\n"
    # Replace only our own banner; preserve existing comments and notices.
    if source.startswith("/*\n * " + RULE + "\n * File:      "):
        source = source.split(" */\n\n", 1)[1]
    elif source.startswith("# " + RULE + "\n# File:      "):
        source = source.split("# " + RULE + "\n\n", 1)[1]
    return shebang, source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Report stale/missing banners without writing")
    args = parser.parse_args()
    sketch = (ROOT / "TimeMachine/TimeMachine.ino").read_text()
    version = re.search(r'^#define\s+VERSION\s+"([^"]+)"', sketch, re.MULTILINE).group(1)
    revision = subprocess.check_output(
        ["git", "rev-parse", "--short=12", "HEAD"], cwd=ROOT, text=True
    ).strip()
    changed = 0
    for directory in ("TimeMachine", "tests", "tools"):
        for path in sorted((ROOT / directory).rglob("*")):
            if not path.is_file() or path.suffix not in SUFFIXES:
                continue
            source = path.read_text()
            shebang, body = split_source(source)
            updated = shebang + banner(path, version, revision) + body
            if source != updated:
                changed += 1
                if args.check:
                    print(path.relative_to(ROOT))
                else:
                    path.write_text(updated)
    print(f"{changed} banners {'need updating' if args.check else 'updated'}.")
    return int(args.check and changed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
