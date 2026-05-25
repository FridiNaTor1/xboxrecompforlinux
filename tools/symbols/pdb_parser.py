#!/usr/bin/env python3
"""
Extract MSVC PDB symbols into xboxrecomp-friendly JSON.

This wrapper reads PDB globals directly with pdbparse when available, with an
llvm-pdbutil fallback. PDB files store symbols as section:offset pairs, so the
parser also needs segment base addresses. The most reliable source is a
matching linker MAP file; otherwise pass one or more --segment-base SEG=VA
values.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from .names import make_c_identifier


SYMBOL_RE = re.compile(r"\|\s+(S_[A-Z0-9_]+)\b.*`(.+)`")
ADDR_RE = re.compile(r"\baddr\s*=\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})")
MAP_SYMBOL_RE = re.compile(
    r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s+\S+\s+([0-9A-Fa-f]{8})\b"
)

FUNC_RECORDS = {
    "S_GPROC32",
    "S_LPROC32",
    "S_GPROC32_ID",
    "S_LPROC32_ID",
    "S_LPROC32_DPC",
}


class PdbParseUnavailable(RuntimeError):
    pass


def parse_int(text: str) -> int:
    text = text.strip()
    return int(text, 16 if text.lower().startswith("0x") else 10)


def parse_segment_base(text: str) -> tuple[int, int]:
    if "=" not in text:
        raise argparse.ArgumentTypeError("expected SEG=VA")
    seg, va = text.split("=", 1)
    return int(seg, 0), parse_int(va)


def parse_map_segment_bases(path: Path) -> dict[int, int]:
    bases: dict[int, int] = {}
    for line in path.read_text(errors="replace").splitlines():
        match = MAP_SYMBOL_RE.match(line)
        if not match:
            continue
        seg_text, off_text, va_text = match.groups()
        seg = int(seg_text, 16)
        off = int(off_text, 16)
        va = int(va_text, 16)
        bases.setdefault(seg, va - off)
    return bases


def run_llvm_pdbutil(tool: str, pdb_file: Path) -> str:
    cmd = [tool, "dump", "-publics", "-globals", str(pdb_file)]
    try:
        return subprocess.check_output(cmd, text=True, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as exc:
        sys.stderr.write(exc.stderr)
        raise SystemExit(exc.returncode) from exc


def parse_pdbutil_dump(text: str, segment_bases: dict[int, int], bias: int) -> list[dict]:
    symbols: list[dict] = []
    pending: dict | None = None

    for line in text.splitlines():
        match = SYMBOL_RE.search(line)
        if match:
            record, name = match.groups()
            pending = {
                "record": record,
                "name": name,
                "is_function": record in FUNC_RECORDS,
                "flags": [],
            }
            continue

        if pending is None:
            continue

        stripped = line.strip()
        if stripped.startswith("flags ="):
            flags = [part.strip() for part in stripped[7:].split("|")]
            pending["flags"] = [flag for flag in flags if flag]
            if pending["record"] == "S_PUB32" and any(flag == "function" for flag in flags):
                pending["is_function"] = True
            continue

        addr_match = ADDR_RE.search(line)
        if not addr_match:
            continue

        seg = int(addr_match.group(1), 16)
        offset = int(addr_match.group(2), 16)
        base = segment_bases.get(seg)
        if base is None:
            pending = None
            continue

        map_va = base + offset
        xbe_va = map_va - bias
        symbols.append(
            {
                "segment": seg,
                "offset": offset,
                "map_va": map_va,
                "xbe_va": xbe_va,
                "name": pending["name"],
                "record": pending["record"],
                "flags": pending["flags"],
                "is_function": pending["is_function"],
                "scope": "pdb",
            }
        )
        pending = None

    return symbols


def parse_pdbparse_symbols(pdb_file: Path, segment_bases: dict[int, int], bias: int) -> list[dict]:
    try:
        import pdbparse
        from pdbparse import dbi, gdata
    except ImportError as exc:
        raise PdbParseUnavailable("pdbparse is not installed") from exc

    pdb = pdbparse.parse(str(pdb_file), fast_load=True)

    dbi_stream = pdb.STREAM_DBI.stream_file
    dbi_stream.seek(0)
    dbi_header = dbi.DBIHeader.parse_stream(dbi_stream)
    symbol_stream_index = dbi_header.symrecStream
    if symbol_stream_index < 0:
        raise PdbParseUnavailable("PDB does not contain a global symbol stream")

    try:
        symbol_stream = pdb.streams[symbol_stream_index].stream_file
    except IndexError as exc:
        raise PdbParseUnavailable(
            f"PDB global symbol stream {symbol_stream_index} is missing"
        ) from exc

    symbol_stream.seek(0)
    globals_ = gdata.parse_stream(symbol_stream)

    symbols: list[dict] = []
    for sym in globals_:
        if not all(hasattr(sym, attr) for attr in ("segment", "offset", "name")):
            continue

        seg = int(sym.segment)
        offset = int(sym.offset)
        base = segment_bases.get(seg)
        if base is None:
            continue

        symtype = getattr(sym, "symtype", None)
        leaf_type = getattr(sym, "leaf_type", None)
        is_function = symtype == 2
        map_va = base + offset
        xbe_va = map_va - bias
        symbols.append(
            {
                "segment": seg,
                "offset": offset,
                "map_va": map_va,
                "xbe_va": xbe_va,
                "name": str(sym.name),
                "record": f"0x{int(leaf_type):04X}" if leaf_type is not None else "pdbparse",
                "symtype": symtype,
                "flags": ["function"] if is_function else [],
                "is_function": is_function,
                "scope": "pdb",
            }
        )

    return symbols


def write_outputs(symbols: list[dict], json_path: Path, seeds_path: Path | None) -> None:
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(symbols, indent=2) + "\n")

    if not seeds_path:
        return

    seen: set[int] = set()
    seeds: list[dict] = []
    for sym in sorted(symbols, key=lambda item: item["xbe_va"]):
        xbe_va = sym["xbe_va"]
        if not sym.get("is_function") or xbe_va <= 0 or xbe_va in seen:
            continue
        seen.add(xbe_va)
        seeds.append({
            "start": f"0x{xbe_va:08X}",
            "name": make_c_identifier(sym["name"], xbe_va),
            "symbol_name": sym["name"],
        })

    seeds_path.parent.mkdir(parents=True, exist_ok=True)
    seeds_path.write_text(json.dumps(seeds, indent=2) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pdb_file", type=Path, nargs="?",
                        help="Input PDB file. Optional with --dump.")
    parser.add_argument("--dump", type=Path,
                        help="Use an existing llvm-pdbutil dump instead of invoking llvm-pdbutil")
    parser.add_argument("--llvm-pdbutil", default=None,
                        help="Path to llvm-pdbutil")
    parser.add_argument("--backend", choices=("auto", "pdbparse", "llvm"), default="auto",
                        help="PDB reader backend. Default: auto")
    parser.add_argument("--map-file", type=Path,
                        help="Matching linker MAP file used to infer segment bases")
    parser.add_argument("--segment-base", action="append", type=parse_segment_base, default=[],
                        help="Manual segment base as SEG=VA; can be repeated")
    parser.add_argument("--bias", default="0",
                        help="Address bias: xbe_va = map_va - bias")
    parser.add_argument("--json", type=Path, required=True,
                        help="Output symbol JSON")
    parser.add_argument("--seeds", type=Path,
                        help="Optional disassembler seed JSON")
    args = parser.parse_args()

    segment_bases: dict[int, int] = {}
    if args.map_file:
        segment_bases.update(parse_map_segment_bases(args.map_file))
    for seg, base in args.segment_base:
        segment_bases[seg] = base

    if not segment_bases:
        parser.error("PDB addresses require --map-file or at least one --segment-base SEG=VA")

    backend_used = "dump"
    if args.dump:
        dump_text = args.dump.read_text(errors="replace")
        symbols = parse_pdbutil_dump(dump_text, segment_bases, parse_int(args.bias))
    else:
        if not args.pdb_file:
            parser.error("pdb_file is required unless --dump is used")
        bias = parse_int(args.bias)
        errors: list[str] = []
        symbols = []

        if args.backend in ("auto", "pdbparse"):
            try:
                symbols = parse_pdbparse_symbols(args.pdb_file, segment_bases, bias)
                backend_used = "pdbparse"
            except Exception as exc:
                if args.backend == "pdbparse":
                    parser.error(f"pdbparse backend failed: {exc}")
                errors.append(f"pdbparse: {exc}")

        if not symbols and args.backend in ("auto", "llvm"):
            tool = args.llvm_pdbutil or shutil.which("llvm-pdbutil")
            if not tool:
                errors.append("llvm: llvm-pdbutil not found")
            else:
                dump_text = run_llvm_pdbutil(tool, args.pdb_file)
                symbols = parse_pdbutil_dump(dump_text, segment_bases, bias)
                backend_used = "llvm"

        if not symbols and errors:
            parser.error("unable to read PDB symbols (" + "; ".join(errors) + ")")

    write_outputs(symbols, args.json, args.seeds)

    function_count = sum(1 for sym in symbols if sym.get("is_function"))
    print(f"Parsed {len(symbols)} PDB symbols")
    print(f"  backend: {backend_used}")
    print(f"  functions: {function_count}")
    print(f"  segments: {len(segment_bases)}")
    print(f"Wrote {args.json}")
    if args.seeds:
        print(f"Wrote {args.seeds}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
