#!/usr/bin/env python3
"""
Parse an MSVC linker .map file into xboxrecomp-friendly symbol JSON.

Some Xbox debug builds ship with linker MAP files. Those addresses may use
the original PE preferred load address while the XBE uses a different base.
Pass --bias so xbe_va = map_va - bias.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from .names import make_c_identifier


SYMBOL_RE = re.compile(
    r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s+(\S+)\s+([0-9A-Fa-f]{8})\s*(.*)$"
)


def parse_int(text: str) -> int:
    text = text.strip()
    return int(text, 16 if text.lower().startswith("0x") else 10)


def parse_flags_and_object(rest: str) -> tuple[list[str], str]:
    parts = rest.split()
    flags: list[str] = []
    while parts and len(parts[0]) <= 2 and all(c.isalpha() for c in parts[0]):
        flags.append(parts.pop(0))
    return flags, " ".join(parts)


def parse_map(path: Path, bias: int) -> list[dict]:
    symbols: list[dict] = []
    current_section = None

    for line in path.read_text(errors="replace").splitlines():
        if "Publics by Value" in line:
            current_section = "public"
            continue
        if "Static symbols" in line:
            current_section = "static"
            continue

        match = SYMBOL_RE.match(line)
        if not match:
            continue

        seg, offset, name, map_va_text, rest = match.groups()
        map_va = int(map_va_text, 16)
        flags, obj = parse_flags_and_object(rest)
        symbols.append(
            {
                "segment": int(seg, 16),
                "offset": int(offset, 16),
                "map_va": map_va,
                "xbe_va": map_va - bias,
                "name": name,
                "object": obj,
                "flags": flags,
                "scope": current_section or "unknown",
            }
        )

    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map_file", type=Path)
    parser.add_argument("--bias", default="0",
                        help="Address bias: xbe_va = map_va - bias")
    parser.add_argument("--json", type=Path, required=True,
                        help="Output symbol JSON")
    parser.add_argument("--seeds", type=Path,
                        help="Optional disassembler seed JSON")
    args = parser.parse_args()

    bias = parse_int(args.bias)
    symbols = parse_map(args.map_file, bias)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(symbols, indent=2) + "\n")

    if args.seeds:
        seen = set()
        seeds = []
        for sym in symbols:
            if sym["segment"] != 1 or sym["xbe_va"] <= 0 or sym["xbe_va"] in seen:
                continue
            seen.add(sym["xbe_va"])
            seeds.append({
                "start": f"0x{sym['xbe_va']:08X}",
                "name": make_c_identifier(sym["name"], sym["xbe_va"]),
                "symbol_name": sym["name"],
            })
        args.seeds.parent.mkdir(parents=True, exist_ok=True)
        args.seeds.write_text(json.dumps(seeds, indent=2) + "\n")
    else:
        seeds = []

    public_count = sum(1 for sym in symbols if sym["scope"] == "public")
    static_count = sum(1 for sym in symbols if sym["scope"] == "static")
    code_seed_count = len(seeds)

    print(f"Parsed {len(symbols)} symbols")
    print(f"  publics: {public_count}")
    print(f"  statics: {static_count}")
    if args.seeds:
        print(f"  code seeds: {code_seed_count}")
    print(f"Wrote {args.json}")
    if args.seeds:
        print(f"Wrote {args.seeds}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
