# Full Spectrum Warrior: Ten Hammers Dec 5, 2005 Bring-Up

Local game files were in `game_files/`.

## Input Files

| File | Notes |
|------|-------|
| `default.xbe`| Debug XBE, 6.6 MiB |
| `fsw.exe`    | Matching debug path executable name from XBE metadata |
| `fsw.map`    | Linker map, 4.9 MiB |
| `fsw.pdb`    | PDB symbols, 21 MiB |

## XBE Metadata

- Title: `Full Spectrum Warrior: Ten Hammers`
- Title ID: `0x545100F8`
- Build type: debug
- Build date: `2005-12-15 03:40:25 UTC`
- Debug path: `D:\FSW2\FSW\Build\XboxRelease\FSW.exe`
- Base address: `0x00010000`
- Image size: `0x00767FE0`
- Entry point: `0x0005EE09`
- Kernel thunk table: `0x0050B060`
- Kernel imports: 151

## Linked XDK Libraries

The XBE reports XDK `1.0.5849` libraries including:

- `XAPILIB`
- `DSOUND`
- `XBOXKRNL`
- `XONLINES`
- `XVOICE`
- `VOICMAIL`
- `LIBC`
- `LIBCP`
- `D3D8LTCG`
- `XGRAPHCL`
- `XACTENLT`

## Initial Static Analysis

Commands used:

```bash
python3 tools/xbe_parser/xbe_parser.py game_files/default.xbe --json game_files/default_analysis.json --quiet
python3 -m tools.disasm game_files/default.xbe --analysis-json game_files/default_analysis.json --text-only --force -v
python3 -m tools.func_id game_files/default.xbe --functions tools/disasm/output/functions.json --strings tools/disasm/output/strings.json --xrefs tools/disasm/output/xrefs.json -v
python3 -m tools.disasm game_files/default.xbe --analysis-json game_files/default_analysis.json --text-only --force -v --seed-functions tools/func_id/output/identified_functions.json
python3 -m tools.symbols.map_parser game_files/fsw.map --bias 0x003EF5C0 --json game_files/fsw_map_symbols.json --seeds game_files/fsw_map_seed_functions.json
python3 -m tools.symbols.pdb_parser game_files/fsw.pdb --map-file game_files/fsw.map --bias 0x003EF5C0 --json game_files/fsw_pdb_symbols.json --seeds game_files/fsw_pdb_seed_functions.json
python3 -m tools.symbols.merge_seeds game_files/fsw_map_seed_functions.json game_files/fsw_pdb_seed_functions.json --out game_files/fsw_symbol_seed_functions.json
python3 -m tools.disasm game_files/default.xbe --analysis-json game_files/default_analysis.json --text-only --force -v --seed-functions game_files/fsw_symbol_seed_functions.json
python3 tools/recomp/harvest_missing_targets.py --gen-dir src/game/recomp/gen --base-seeds game_files/fsw_symbol_seed_functions.json --out game_files/fsw_combined_seed_functions.json
python3 -m tools.disasm game_files/default.xbe --analysis-json game_files/default_analysis.json --text-only --force -v --seed-functions game_files/fsw_combined_seed_functions.json
python3 -m tools.recomp game_files/default.xbe --analysis-json game_files/default_analysis.json --all --split 1000 --gen-dir src/game/recomp/gen
cmake -S . -B build/linux-game -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/linux-game -j$(nproc)
```

First pass results:

- Instructions: 1,498,902
- Functions: 24,575 before vtable seed feedback
- Cross-references: 229,587
- Strings: 5,927
- Function ID discovered 6,706 vtable thunks
- Function ID total after classification: 31,281

Seeded second pass results:

- Functions: 28,942 in disassembly output after vtable seed feedback
- Reachable instructions: 1,247,838 (83.3%)
- Labels: 35,020
- Function ID total: 31,281

Corrected map-seeded pass results:

- Parsed linker map symbols: 51,143
- Unique code seeds from map: 30,544
- Default map bias: `0x003EF5C0`
- Functions: 30,529 in disassembly output
- Labels: 36,607
- Function ID total after vtable additions: 36,512

Fallback-seeded recomp pass results:

- Direct PDB symbols parsed: 38,245
- Unique PDB function seeds: 21,359
- MAP+PDB named symbol seeds: 33,094
- Combined feedback seed functions: 49,214
- Functions: 46,004 in disassembly output
- Named functions in disassembly output: 30,506
- Anonymous fallback functions in disassembly output: 15,497
- Labels: 54,669
- Recompiled functions: 46,004/46,004
- Generated C: 2,356,134 lines across 47 source chunks
- Fallback direct-target stubs remaining: 871

## Current Runtime Notes

- Linux runtime builds with SDL2 input, SDL2 audio, and Vulkan presentation.
- The local ignored game target builds as `local_game_recomp` and loads `game_files/default.xbe`.
- Runtime reaches `_mainCRTStartup`, creates the XAPI startup thread, enters `_mainXapiStartup`, calls `_main`, and returns cleanly.
- Current next blocker: `_main` exits immediately during early initialization; the import bridge still reports 81 stubbed kernel ordinals.
- The Vulkan backend currently presents cleared frames; real NV2A/D3D draw lowering is still required for visible gameplay.
- `fsw.pdb` and `fsw.map` now produce named seed symbols. Generated C uses stable `fn_<VA>_<symbol>` identifiers and keeps the original decorated PDB/MAP symbol in each function comment.
