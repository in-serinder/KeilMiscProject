#!/usr/bin/env python3
"""Fail the build when an artefact exceeds the size cap.

    python tools/checksize.py build/capsaenc.exe build/stub.exe --limit 120K
"""

from __future__ import annotations

import argparse
from pathlib import Path


def parse_size(text: str) -> int:
    text = text.strip().upper()
    scale = 1
    if text.endswith("K"):
        scale, text = 1024, text[:-1]
    elif text.endswith("M"):
        scale, text = 1024 * 1024, text[:-1]
    return int(text) * scale


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+")
    parser.add_argument("--limit", default="120K")
    args = parser.parse_args()

    limit = parse_size(args.limit)
    failed = False

    for name in args.files:
        path = Path(name)
        if not path.exists():
            print(f"[FAIL] missing artefact: {name}")
            failed = True
            continue

        size = path.stat().st_size
        status = "OK  " if size <= limit else "OVER"
        print(f"[{status}] {name:<28} {size:>7} B  ({size/1024:.1f} KB)  limit {limit/1024:.0f} KB")
        if size > limit:
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
