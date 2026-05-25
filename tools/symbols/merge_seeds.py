#!/usr/bin/env python3
"""Merge disassembler seed JSON files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from .names import make_c_identifier


def load_seed_file(path: Path) -> list[dict]:
    return json.loads(path.read_text())


def parse_addr(entry: dict) -> int:
    return int(entry["start"], 16)


def normalize_entry(entry: dict) -> dict:
    addr = parse_addr(entry)
    result = dict(entry)
    has_real_symbol = bool(result.get("symbol_name") or result.get("raw_name"))
    raw_name = result.get("symbol_name") or result.get("raw_name") or result.get("name")
    if raw_name and (has_real_symbol or not str(raw_name).startswith("sub_")):
        result.setdefault("symbol_name", raw_name)
        result["name"] = make_c_identifier(raw_name, addr)
    else:
        result["name"] = f"sub_{addr:08X}"
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("seed_files", type=Path, nargs="+",
                        help="Input seed JSON files. Later files win by default.")
    parser.add_argument("--out", type=Path, required=True,
                        help="Merged output seed JSON")
    parser.add_argument("--prefer", choices=("first", "last"), default="last",
                        help="Conflict policy for duplicate addresses")
    args = parser.parse_args()

    merged: dict[int, dict] = {}
    inputs = args.seed_files if args.prefer == "last" else list(reversed(args.seed_files))
    for path in inputs:
        for entry in load_seed_file(path):
            merged[parse_addr(entry)] = normalize_entry(entry)

    seeds = [merged[addr] for addr in sorted(merged)]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(seeds, indent=2) + "\n")

    print(f"inputs: {len(args.seed_files)}")
    print(f"merged seeds: {len(seeds)}")
    print(f"wrote: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
