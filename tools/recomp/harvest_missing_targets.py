#!/usr/bin/env python3
"""Harvest generated function references that are not declared yet."""

from __future__ import annotations

import argparse
import glob
import json
import re
from pathlib import Path


FUNC_NAME_RE = r"(?:sub|fn)_([0-9A-Fa-f]{8})[A-Za-z0-9_]*"
CALL_RE = re.compile(rf"\b({FUNC_NAME_RE})\s*\(")
DECL_RE = re.compile(rf"void ({FUNC_NAME_RE})\(void\);")
FALLBACK_RE = re.compile(r"recomp_missing_target\(0x([0-9A-Fa-f]{8})u\)")


def addr_from_name(name: str) -> int:
    match = re.match(r"^(?:sub|fn)_([0-9A-Fa-f]{8})", name)
    if not match:
        raise ValueError(f"not a generated function name: {name}")
    return int(match.group(1), 16)


def fallback_seed_name(name: str) -> str:
    addr = addr_from_name(name)
    if name == f"fn_{addr:08X}_sub_{addr:08X}":
        return f"sub_{addr:08X}"
    return name


def load_seed_file(path: Path) -> dict[int, dict]:
    if not path or not path.exists():
        return {}
    result = {}
    for entry in json.loads(path.read_text()):
        addr = int(entry["start"], 16)
        result[addr] = dict(entry)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gen-dir", type=Path, default=Path("src/game/recomp/gen"))
    parser.add_argument("--base-seeds", type=Path,
                        help="Optional existing seed JSON to merge")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    header = args.gen_dir / "recomp_funcs.h"
    declared = {match.group(1) for match in DECL_RE.finditer(header.read_text(errors="replace"))}

    refs = set()
    for path in glob.glob(str(args.gen_dir / "recomp_*.c")):
        refs.update(match.group(1) for match in CALL_RE.finditer(Path(path).read_text(errors="replace")))

    fallback_addrs: set[int] = set()
    for path in glob.glob(str(args.gen_dir / "recomp_*.c")):
        text = Path(path).read_text(errors="replace")
        fallback_addrs.update(int(m.group(1), 16) for m in FALLBACK_RE.finditer(text))

    missing = sorted(refs - declared, key=addr_from_name)
    seeds = load_seed_file(args.base_seeds) if args.base_seeds else {}
    for name in declared:
        addr = addr_from_name(name)
        seeds.setdefault(addr, {
            "start": f"0x{addr:08X}",
            "name": fallback_seed_name(name),
        })
    for name in missing:
        addr = addr_from_name(name)
        seeds.setdefault(addr, {
            "start": f"0x{addr:08X}",
            "name": fallback_seed_name(name),
        })
    for addr in fallback_addrs:
        seeds.setdefault(addr, {"start": f"0x{addr:08X}", "name": f"sub_{addr:08X}"})

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps([seeds[k] for k in sorted(seeds)], indent=2) + "\n")

    print(f"declared: {len(declared)}")
    print(f"referenced: {len(refs)}")
    print(f"missing: {len(missing)}")
    print(f"fallback stubs: {len(fallback_addrs)}")
    print(f"merged seeds: {len(seeds)}")
    print(f"wrote: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
