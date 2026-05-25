"""Helpers for turning debug symbols into stable C identifiers."""

from __future__ import annotations

import re


_IDENT_RE = re.compile(r"[^0-9A-Za-z_]+")


def readable_symbol_name(name: str) -> str:
    """Best-effort cleanup for MSVC MAP/PDB symbol names."""
    name = str(name).strip()
    if not name:
        return "symbol"

    # MSVC C++ decorated names often look like ?Method@Class@Namespace@@...
    # Keep the scope/name prefix; the full decorated form remains in metadata.
    if name.startswith("?") and not name.startswith("??"):
        prefix = name[1:].split("@@", 1)[0]
        parts = [part for part in prefix.split("@") if part]
        if parts:
            name = "_".join(list(reversed(parts[1:])) + parts[:1])

    # Common C/stdcall decoration. This intentionally stays conservative.
    name = name.lstrip("_")
    name = name.replace("@", "_")
    name = _IDENT_RE.sub("_", name)
    name = re.sub(r"_+", "_", name).strip("_")
    return name or "symbol"


def make_c_identifier(name: str, addr: int | None = None, prefix: str = "fn") -> str:
    """Return a valid, address-stable C function identifier."""
    readable = readable_symbol_name(name)
    if addr is not None:
        ident = f"{prefix}_{addr:08X}_{readable}"
    else:
        ident = readable

    if ident[0].isdigit():
        ident = f"{prefix}_{ident}"

    # Keep generated declarations readable and friendly to compilers/debuggers.
    if len(ident) > 180:
        ident = ident[:180].rstrip("_")
    return ident
