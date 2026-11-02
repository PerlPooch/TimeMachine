#!/usr/bin/env python3
"""Explicit settings backup/restore through the existing local config API."""
import argparse
import json
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import urlopen

parser = argparse.ArgumentParser()
parser.add_argument("operation", choices=["backup", "restore"])
parser.add_argument("base_url", help="For example http://192.168.0.120")
parser.add_argument("file", type=Path)
args = parser.parse_args()
endpoint = args.base_url.rstrip("/") + "/api/config"
if args.operation == "backup":
    with urlopen(endpoint, timeout=10) as response:
        values = json.load(response)
    assert isinstance(values, dict), "Expected a config object"
    args.file.write_text(json.dumps(values, indent=2) + "\n")
    print("Backed up settings; sensitive fields are omitted by the API and must be restored separately.")
else:
    values = json.loads(args.file.read_text())
    # Restore only persistent writable values; status and the volatile offset are excluded.
    edits = {key: value for key, value in values.items() if key not in {"schema", "version", "ipA", "ipB", "timeOffset"}}
    with urlopen(endpoint + "?" + urlencode(edits), timeout=10) as response:
        result = json.load(response)
    assert "error" not in result, "Restore failed"
    print("Restored writable application settings.")
