#!/usr/bin/env python3
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Consolidate one rllogger session directory into a single JSON
document. Mirrors the shape produced by cua-bench's exportLog():

    {
      "schemaVersion": 1,
      "sessionId":     <dir name>,
      "exportedAt":    <unix ms>,
      "raw":           [<every raw event>],
      "semantic":      [<every semantic event>],
      "outcome":       <last outcome snapshot, or null>
    }

Tolerates a partial trailing line in raw.jsonl / semantic.jsonl —
the file is append-only and a crash can leave the final line
half-written. JSON-decode failures on the last line are silently
skipped; earlier lines are unaffected.

Usage:
    rllogger-export.py <session-dir> [-o out.json]
    rllogger-export.py ~/.lo-rl-logs/2026-05-18-180510-pid920771

Without -o, JSON is written to stdout.
"""

import argparse
import json
import sys
import time
from pathlib import Path


def read_jsonl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    out: list[dict] = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                # Append-only log; tolerate a torn trailing line.
                continue
    return out


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Consolidate an rllogger session into one JSON file."
    )
    ap.add_argument("session_dir", type=Path, help="Path to the session directory")
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output JSON path (default: stdout)",
    )
    args = ap.parse_args()

    session_dir: Path = args.session_dir
    if not session_dir.is_dir():
        sys.exit(f"Not a directory: {session_dir}")

    raw = read_jsonl(session_dir / "raw.jsonl")
    semantic = read_jsonl(session_dir / "semantic.jsonl")

    outcome = None
    outcome_lines = read_jsonl(session_dir / "outcome.jsonl")
    if outcome_lines:
        # outcome.jsonl is overwritten each tick, so it usually has one
        # line; take the last just in case.
        outcome = outcome_lines[-1]

    payload = {
        "schemaVersion": 1,
        "sessionId": session_dir.name,
        "exportedAt": int(time.time() * 1000),
        "raw": raw,
        "semantic": semantic,
        "outcome": outcome,
    }

    if args.output:
        args.output.write_text(json.dumps(payload, indent=2))
        print(
            f"Wrote {args.output} "
            f"({len(raw)} raw, {len(semantic)} semantic, "
            f"{'1' if outcome else '0'} outcome)",
            file=sys.stderr,
        )
    else:
        json.dump(payload, sys.stdout, indent=2)


if __name__ == "__main__":
    main()
