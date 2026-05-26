# xboxrecomp

```
 #   #  ####    ###   #   #         #####   ###    ###   #       ###
 #   #  #   #  #   #  #   #           #    #   #  #   #  #      #
  # #   ####   #   #   # #            #    #   #  #   #  #       ##
  # #   #   #  #   #   # #            #    #   #  #   #  #         #
 #   #  #   #  #   #  #   #           #    #   #  #   #  #         #
 #   #  ####    ###   #   #           #     ###    ###   #####   ###

 Static Recompilation Toolkit for Original Xbox Games
```

> Turn an Xbox game binary into a native executable. Windows has the current D3D11 rendering backend; Linux builds natively with SDL2 input/audio and a Vulkan backend.

### Recent Changes

- **Native Linux Runtime Build** — The toolkit now configures and builds on Linux. Linux uses POSIX-backed replacements for the Win32 subset used by the runtime, SDL2 for controller/audio output, and a Vulkan backend.
- **Linux Vulkan RHW Path** — The Vulkan backend now has a native fixed-function `XYZRHW` draw path with shader compilation, texture upload, depth clear/test/write support, and whole-call triangle batching. The software/null path remains available for headless bring-up and pixel dumps.
- **Full Multi-Texture Fixed-Function Pipeline** — 4-stage texture blending with all D3D8 operations (MODULATE, ADD, SUBTRACT, BLEND*, DOTPRODUCT3, etc.), full D3DTA argument resolution (DIFFUSE, CURRENT, TEXTURE, TFACTOR, SPECULAR + COMPLEMENT/ALPHAREPLICATE), and 4 samplers bound per draw.
- **Hardware T&L Lighting** — Up to 8 lights (directional, point, spot) with material properties, global ambient, specular highlights, and world-space normal transform. Full Blinn-Phong with attenuation and spotlight cones.
- **Vertex Fog** — Linear/exp/exp2 fog computed in vertex shader, blended with fog color in pixel shader. Fog parameters sourced from D3D8 render states.
- **DrawPrimitiveUP Ring Buffer** — 4MB persistent ring buffer eliminates per-call D3D11 buffer create/destroy. Triangle fan and quad list to triangle list conversion.
- **NV2A Register Combiner Pixel Shaders** — Full 8-stage combiner + final combiner translated to HLSL at runtime with 128-entry shader cache.
- **NV2A Programmable Vertex Shaders** — 128-bit microcode parser and HLSL generator covering all 14 MAC + 8 ILU operations, 192 constant registers, and relative addressing.
- **Texture Unswizzling** — Xbox Z-order (Morton code) swizzled textures converted to linear D3D11 layout. Optimized masked-increment algorithm from xemu.
- **NV2A PGRAPH→D3D11 Translator** — Push buffer method interception and D3D11 rendering.
- **EEPROM / AV Pack / SMBus** — Games can query region, language, video standard, AV pack type, and hardware info.
- **Game bring-up workflow** — XBE parsing, symbol ingestion, disassembly, function identification, C lifting, and runtime integration.

---

## What Is This?

This is a toolkit for **statically recompiling original Xbox (2001-2005) games** from their retail XBE executables into native programs.

Static recompilation takes the raw x86 machine code from an Xbox binary and translates every function — every `mov`, every `jmp`, every `call` — into equivalent C source code. That C code compiles into a native host binary. The game's original logic executes directly on your CPU, not through an interpreter or JIT compiler.

**This is the first known static recompilation project targeting the original Xbox.**

The technique has been proven on other platforms — [N64Recomp](https://github.com/N64Recomp/N64Recomp) showed MIPS-to-C was viable, [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) brought it to Xbox 360's PowerPC — but nobody had tackled the OG Xbox until now. Its x86 architecture makes it both easier (same instruction set family as the host) and harder (variable-length instructions, complex addressing modes, x87 FPU stack) than MIPS or PPC targets.

### Why Not Just Use an Emulator?

Emulators are great. Cxbx-Reloaded and xemu do incredible work. But static recomp offers some unique advantages:

- **Native performance** — recompiled code runs at full speed, no interpretation overhead
- **Moddability** — the output is human-readable C code; you can patch, extend, and improve the game
- **Portability** — the C output can target any platform with a C compiler (ARM, RISC-V, WebAssembly...)
- **Preservation** — a self-contained native binary is the ultimate form of game preservation
- **Understanding** — the process forces you to deeply understand the game at the machine code level

## The Pipeline

```
         YOUR XBOX DISC
              |
              v
    +-------------------+
    |  1. Extract XBE   |     Extract default.xbe from the disc image
    +-------------------+
              |
              v
    +-------------------+
    |  2. Parse XBE     |     Read headers, sections, kernel imports
    +-------------------+     tools/xbe_parser/
              |
              v
    +-------------------+
    |  3. Disassemble   |     Find functions, build control flow graphs
    +-------------------+     tools/disasm/
              |
              v
    +-------------------+
    |  4. Identify      |     Classify: CRT, RenderWare, D3D, game code
    +-------------------+     tools/func_id/
              |
              v
    +-------------------+
    |  5. Lift to C     |     Translate x86 instructions to C statements
    +-------------------+     tools/recomp/
              |
              v
    +-------------------+
    |  6. Build Runtime  |    Kernel shim, D3D translation, memory layout
    +-------------------+     templates/runtime/
              |
              v
    +-------------------+
    |  7. Compile & Run  |    Host compiler builds a native binary
    +-------------------+
```

## Runtime Libraries

Following the [RexGlueSDK](https://github.com/rexglue/rexglue-sdk) pattern (which does the same for Xbox 360 via Xenia), xboxrecomp provides link-time libraries extracted from [xemu](https://github.com/xemu-project/xemu) and purpose-built compatibility layers. Your recompiled game links against these — no emulator needed at runtime.

| Library | Source | What It Does |
|---------|--------|-------------|
| **xbox_kernel** | Custom | Xbox kernel → host OS (Win32 on Windows, POSIX-backed compatibility shim on Linux) |
| **xbox_d3d8** | Custom | Windows: D3D8 → D3D11 graphics. Linux: Xbox D3D8 ABI backend with SDL/Vulkan windowing, software fallback, and a native RHW Vulkan draw path; `null` remains available for headless bring-up |
| **xbox_dsound** | Custom | DirectSound → software mixer (IDirectSound8/IDirectSoundBuffer8) |
| **xbox_apu** | xemu | MCPX APU audio (256-voice processor, ADPCM/PCM, envelopes, HRTF; waveOut on Windows, SDL2 queued audio on Linux) |
| **xbox_nv2a** | xemu+Custom | NV2A GPU register handlers/MMIO/push buffer parsing; PGRAPH routes to D3D11 on Windows or the Linux backend boundary |
| **xbox_input** | Custom | Xbox gamepad → XInput on Windows; SDL2 GameController on Linux, including DualSense through SDL's controller database |

### Building the Libraries

```bash
cd xboxrecomp
cmake -S . -B build
cmake --build build --config Release
```

On Linux, use the same commands. `XBOXRECOMP_GRAPHICS_BACKEND` defaults to `vulkan`; Windows defaults to `d3d11`.

```bash
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/linux -j$(nproc)
```

If you create a local game target under `src/game/`, enable it explicitly:

```bash
cmake -S . -B build/linux-game -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/linux-game -j$(nproc)
./build/linux-game/src/game/<your_game_target>
```

Headless/non-rendering Linux fallback:

```bash
cmake -S . -B build/linux-null -DCMAKE_BUILD_TYPE=RelWithDebInfo -DXBOXRECOMP_GRAPHICS_BACKEND=null
cmake --build build/linux-null -j$(nproc)
```

This produces 6 static libraries in `build/src/*/`. Link your game project against `xboxrecomp` (umbrella target) or individual libraries.

`d8vk` is not vendored as a submodule at this stage. It is a D3D8-to-Vulkan DLL for Wine/Windows-style D3D8 callers, while this runtime implements the Xbox D3D8 ABI directly in-process. The Linux renderer should sit behind `xbox_d3d8`/`xbox_nv2a` as a native backend, not as a dropped-in `d3d8.dll`.

### Linux Runtime Debugging

Useful runtime switches:

```bash
XBOXRECOMP_INPUT_DEBUG=1 ./your_game        # Log SDL/Xbox controller transitions
XBOXRECOMP_DUMP_FRAME=120 ./your_game       # Dump software framebuffer frame 120 to /tmp/xboxrecomp_frame.ppm
XBOXRECOMP_DUMP_VULKAN_FRAME=120 ./your_game # Dump Vulkan render target frame 120 to /tmp/xboxrecomp_vulkan_frame.ppm
XBOXRECOMP_VULKAN_NATIVE=0 ./your_game      # Disable the native Vulkan RHW path and use the software fallback
```

### Integration Pattern

Your recompiled game provides two callback functions that the kernel bridge calls to resolve function addresses:

```c
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t xbox_va);        // Auto-generated dispatch table
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);  // Hand-written overrides
```

The recompiler output (`tools/recomp`) generates these automatically. The xboxrecomp libraries handle everything else — memory layout, kernel calls, graphics, audio, and input.

### Architecture

```
┌──────────────────────────────────────────────────┐
│              Your Game (.exe)                    │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐  │
│  │ recomp/  │ │ manual   │ │ game-specific    │  │
│  │ gen/*.c  │ │ overrides│ │ loaders/formats  │  │
│  └────┬─────┘ └────┬─────┘ └────────┬─────────┘  │
│       │            │                │            │
│       └──────┬─────┘────────────────┘            │
│              │ recomp_lookup() / ICALL dispatch  │
├──────────────┼───────────────────────────────────┤
│              │   xboxrecomp libraries            │
│  ┌───────────┴──────────┐                        │
│  │    xbox_kernel       │  Memory layout, file   │
│  │    (kernel_bridge.c) │  I/O, threading, sync  │
│  └───────────┬──────────┘                        │
│              │                                   │
│  ┌───────┐ ┌─┴─────┐ ┌────────┐ ┌──────┐ ┌──────┐│
│  │xbox_  │ │xbox_  │ │xbox_   │ │xbox_ │ │xbox_ ││
│  │d3d8   │ │dsound │ │apu     │ │nv2a  │ │input ││
│  │D3D8→  │ │DSound→│ │MCPX APU│ │NV2A  │ │XPP→  ││
│  │D3D11/ │ │mixer  │ │(xemu)  │ │(xemu)│ │XInput││
│  │Vulkan │ │       │ │        │ │      │ │/SDL2 ││
│  └───────┘ └───────┘ └────────┘ └──────┘ └──────┘│
├──────────────────────────────────────────────────┤
│  Windows: D3D11, XInput, waveOut, Win32 API      │
│  Linux: POSIX shims, SDL2, Vulkan backend        │
└──────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

- **Windows 11/10** for the D3D11 backend, or **Linux** with SDL2 and Vulkan development packages
- **Python 3.10+** with `capstone` (`pip install capstone`)
- **Visual Studio 2022** (MSVC compiler) on Windows, or GCC/Clang on Linux
- **CMake 3.20+**
- An original Xbox game disc image (you must own the game)

### Step-by-Step

```bash
# 1. Clone this repo
git clone https://github.com/sp00nznet/xboxrecomp.git
cd xboxrecomp

# 2. Extract default.xbe from your Xbox disc image
#    (Use xdvdfs, extract-xiso, or similar tool)
mkdir game_files
# copy default.xbe and game data into game_files/

# 3. Parse the XBE — learn what you're working with
python3 tools/xbe_parser/xbe_parser.py game_files/default.xbe --json game_files/default_analysis.json
#    Output: section map, kernel imports, entry point, XDK version

# 4. Disassemble — find all functions
py -3 -m tools.disasm game_files/default.xbe --text-only
#    Output: tools/disasm/output/ (functions.json, xrefs.json, strings.json)

# 5. Identify library functions
py -3 -m tools.func_id game_files/default.xbe -v
#    Output: tools/func_id/output/ (CRT, RenderWare, vtables classified)

# 6. Lift to C — the big one
py -3 -m tools.recomp game_files/default.xbe --all --split 1000
#    Output: src/game/recomp/gen/ (millions of lines of C)

# 7. Set up runtime shims (see docs/runtime/ for templates)
#    - Xbox kernel replacement (147 imports)
#    - D3D8 -> D3D11 translation layer
#    - Memory layout reproduction
#    - Input system

# 8. Build and run
cmake -S . -B build
cmake --build build --config Release
bin/your_game.exe
```

On Linux the runtime creates a Vulkan window, accepts SDL2 controllers, outputs mixed audio through SDL2, and can draw pre-transformed D3D8 `XYZRHW` geometry through the native Vulkan path. Full NV2A push-buffer and programmable shader lowering into Vulkan command buffers is still an active renderer milestone.

Game-specific bring-up notes can live under `docs/game-bringup/`; generated game code and proprietary assets should stay out of the neutral runtime fork.

See [docs/technical/candidate-games.md](docs/technical/candidate-games.md) for a detailed list of promising targets.

## Projects Using This Toolkit

- **[Burnout 3: Takedown](https://github.com/sp00nznet/burnout3)** — The reference implementation. 22,097 functions lifted, full main menu rendering at 60fps, 37 playable tracks with textures, 67 vehicle models, AWD audio playback.
- **[Wreckless: The Yakuza Missions](https://github.com/sp00nznet/wreckless)** — Xbox launch title (2002). Custom engine, 3,407 functions, boots through CRT init into game main. Debugging early gameplay crash.
- **[Blood Wake](https://github.com/sp00nznet/bloodwake)** — First-party Microsoft naval combat (2001). Stormfront Studios custom engine. 4,608 functions, 367K lines of C generated (99.1% success). Project scaffolded, working toward first build.

### What To Expect

The first time you run a recompiled game, **it will crash**. That's normal. The process is iterative:

1. **Boot** — get past the entry point (usually straightforward)
2. **Stub** — identify and stub out functions that touch hardware you haven't implemented yet
3. **Fix ICALLs** — indirect calls (vtable dispatches, function pointers) are the hardest 10%
4. **Add runtime** — implement kernel functions, D3D calls, and input as the game needs them
5. **Debug** — use the ICALL trace ring buffer, memory access logging, and your debugger
6. **Iterate** — each crash teaches you something about the game. Fix it and move on.

Bring-up time varies widely by game, engine, symbols, and how much of the runtime surface it exercises.

## Repository Structure

```
xboxrecomp/
├── README.md                    # You are here
├── CMakeLists.txt               # Top-level build (builds all runtime libs)
├── tools/                       # The recompilation toolchain (Python)
│   ├── xbe_parser/              # XBE file format parser
│   ├── disasm/                  # x86 disassembler + function detector
│   ├── func_id/                 # Library function identifier
│   └── recomp/                  # x86 -> C static recompiler
├── src/                         # Runtime libraries (C, link-time)
│   ├── kernel/                  # xbox_kernel - Xbox kernel → host OS
│   ├── d3d/                     # xbox_d3d8   - D3D8 → D3D11/Vulkan/null graphics
│   ├── audio/                   # xbox_dsound - DirectSound compat
│   ├── apu/                     # xbox_apu    - MCPX APU emulation (xemu)
│   ├── nv2a/                    # xbox_nv2a   - NV2A GPU emulation (xemu)
│   └── input/                   # xbox_input  - Gamepad → XInput
├── src/platform/                # Linux compatibility shims
├── include/                     # Public headers
├── templates/                   # Starter templates for new projects
│   └── runtime/                 # Runtime shim templates
│       ├── recomp_types.h       # Register model + ICALL macros
│       ├── xbox_memory.h        # Memory layout helpers
│       └── kernel_stubs.h       # Kernel function stub templates
├── docs/                        # Documentation
│   ├── pipeline/                # Step-by-step pipeline guides
│   ├── technical/               # Deep technical documentation
│   ├── formats/                 # Xbox file format references
│   └── runtime/                 # Runtime implementation guides
└── examples/                    # Example configurations
```

## Documentation

### Start Here
- **[Getting Started Guide](docs/GETTING_STARTED.md)** — End-to-end walkthrough from XBE to running game
- **[Tools Reference](tools/README.md)** — Detailed usage for all 5 pipeline tools
- **[Runtime Libraries](src/README.md)** — Architecture, build instructions, integration guide

### Per-Module API Reference
- [xbox_kernel](src/kernel/README.md) — Memory layout, file I/O, threading, sync, crypto, EEPROM, SMBus (8,298 LOC)
- [xbox_d3d8](src/d3d/README.md) — D3D8 interface, register combiners, vertex shaders, texture unswizzle (7,018 LOC)
- [xbox_dsound](src/audio/README.md) — DirectSound buffers, 3D audio, mixbins (573 LOC)
- [xbox_apu](src/apu/README.md) — MCPX APU voice processor, mixer, MMIO (3,918 LOC)
- [xbox_nv2a](src/nv2a/README.md) — NV2A GPU registers, push buffer, PGRAPH→D3D11 (4,778 LOC)
- [xbox_input](src/input/README.md) — Gamepad state, vibration, button mapping (212 LOC)

### Pipeline Guides
- [Extracting and Parsing XBE Files](docs/pipeline/01-xbe-parsing.md)
- [Disassembly and Function Detection](docs/pipeline/02-disassembly.md)
- [Function Identification](docs/pipeline/03-function-id.md)
- [x86 to C Lifting](docs/pipeline/04-lifting.md)
- [Building the Runtime](docs/pipeline/05-runtime.md)
- [Iterative Debugging](docs/pipeline/06-debugging.md)

### Technical Deep Dives
- [The Register Model](docs/technical/register-model.md) — Why global registers work and how the stack is simulated
- [Memory Layout Reproduction](docs/technical/memory-layout.md) — CreateFileMapping, mirror views, and address space tricks
- [Indirect Call Dispatch](docs/technical/indirect-calls.md) — The RECOMP_ICALL problem and how to solve it
- [D3D8 to D3D11 Translation](docs/technical/d3d-translation.md) — Bridging Xbox's graphics API to modern DirectX
- [D3D8LTCG Device Context](docs/technical/d3d8ltcg-device-context.md) — Device field map, PB ring management, stub calling conventions **(NEW)**
- [Xbox Kernel Replacement](docs/technical/kernel-replacement.md) — Mapping 147 kernel imports to Win32
- [SEH and Exception Handling](docs/technical/seh-handling.md) — Structured exception handling in recompiled code
- [Lessons Learned](docs/technical/lessons-learned.md) — What worked, what didn't, mistakes to avoid
- [Gap Analysis vs xemu](docs/technical/gap-analysis.md) — What's implemented, what's missing, prioritized roadmap

### Xbox Formats
- [XBE File Format](docs/formats/xbe.md) — Xbox executable format reference
- [Xbox Kernel Exports](docs/formats/kernel-exports.md) — All 366 kernel functions documented

## How The Key Pieces Work

### The Register Model

Xbox uses 32-bit x86 with 8 general-purpose registers. In recompiled code, these become C globals:

```c
// Volatile (caller-saved) — shared across all functions
uint32_t g_eax, g_ecx, g_edx, g_esp;

// Callee-saved — also global (implicit parameter passing via esi/edi/ebx)
uint32_t g_ebx, g_esi, g_edi;

// Stack lives in Xbox memory space
#define PUSH32(val)  do { g_esp -= 4; MEM32(g_esp) = (val); } while(0)
#define POP32(dst)   do { (dst) = MEM32(g_esp); g_esp += 4; } while(0)
```

Every recompiled function is `void func(void)` — arguments pass through the simulated stack and registers, just like real x86.

### Memory Layout

Xbox has 64 MB of unified RAM. We reproduce the exact address layout:

```c
// Create shared memory object
HANDLE h = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 64*1024*1024, NULL);

// Map at Xbox virtual addresses
MapViewOfFileEx(h, FILE_MAP_ALL_ACCESS, 0, offset, size, (LPVOID)0x00010000);  // XBE
MapViewOfFileEx(h, FILE_MAP_ALL_ACCESS, 0, offset, size, (LPVOID)0x80000000);  // mirror
// ... 28 views total covering the Xbox address space
```

Why `CreateFileMapping` instead of `VirtualAlloc`? The Xbox has **mirror regions** — the same physical memory at multiple virtual addresses. File mapping gives us true aliases; VirtualAlloc would give separate copies where writes in one aren't visible in the other.

### The ICALL Problem

The single hardest challenge. When the game does `call [eax+0x10]` (a virtual method call), we don't know the target at compile time. Our dispatch macro handles it:

```c
#define RECOMP_ICALL(va) do {                          \
    recomp_func_t fn = recomp_lookup_manual(va);       \  // 1. hand-written overrides
    if (!fn) fn = recomp_lookup(va);                   \  // 2. auto dispatch table
    if (!fn) fn = recomp_lookup_kernel(va);            \  // 3. kernel bridge
    if (fn) fn();                                       \
    else { g_esp += 4; /* pop dummy ret addr */ }      \
} while(0)
```

Most ICALLs resolve through the auto-generated dispatch table (binary search over all function addresses). The rest are either kernel calls (0xFE000000+ range) or garbage pointers from corrupted vtables that need per-function guards.

### NV2A Register Combiners → HLSL

Xbox games don't use traditional pixel shaders — they configure the NV2A's 8-stage register combiner pipeline. Each stage performs independent RGB and alpha math (multiply, dot product, MUX) on a register file of textures, vertex colors, and constants. The final combiner blends everything together.

We translate this to HLSL at runtime:

```c
// Game configures 3 combiner stages...
SetPixelShader(0x00000103);  // 3 stages, tex0=2D, tex1=2D

// At draw time, we generate and cache an HLSL pixel shader:
//   Stage 0: r0.rgb = tex0 * diffuse
//   Stage 1: r0.rgb = r0 * tex1 (environment map modulate)
//   Stage 2: r0.a   = tex0.a * diffuse.a
//   Final:   output = r0
```

The 128-entry shader cache means each unique combiner configuration is compiled once and reused. This handles multi-texturing, bump mapping, environment mapping, water effects, and every other Xbox rendering technique.

### NV2A Vertex Shader Microcode → HLSL

When games use programmable vertex shaders (water displacement, skeletal animation, custom lighting), they upload NV2A microcode — 128-bit instructions with paired MAC+ILU operations. We parse and translate to HLSL:

- **14 MAC ops**: MOV, MUL, ADD, MAD, DP3, DP4, DPH, DST, MIN, MAX, SLT, SGE, ARL
- **8 ILU ops**: MOV, RCP, RCC, RSQ, EXP, LOG, LIT
- **192 constant registers**, 12 temporaries, 16 vertex inputs
- Relative addressing via the address register (A0)
- 64-entry compiled shader cache

## Games That Work Well As Targets

The best candidates for Xbox static recomp generally share these traits:

| Factor | Easier | Harder |
|--------|--------|--------|
| **Engine** | RenderWare (shared patterns) | Custom engine (unique quirks) |
| **Threading** | Single-threaded | Multi-threaded with sync |
| **GPU usage** | Standard D3D8 calls | NV2A push buffer microcode |
| **Code size** | Small .text section | Large with LTCG |
| **Online** | Offline only | Xbox Live dependent |
| **PC port** | No PC version (worth the effort!) | Good PC port exists |

Keep game-specific generated code, proprietary assets, and per-title runtime patches in separate projects or private branches.

## How You Can Help

This is an emerging field. Here's how you can contribute:

1. **Try it on a new game** — Pick an Xbox exclusive, follow the pipeline, and see how far you get. Even partial results teach us about the toolchain's gaps.
2. **Improve the lifter** — The x86-to-C translator handles ~95% of instructions. Edge cases (SIMD, obscure FPU ops, segment prefixes) need work.
3. **Document Xbox formats** — Every game has its own asset formats. Document what you discover.
4. **Build runtime components** — Better D3D8 emulation, audio, networking — the runtime layer is where most per-game work happens.
5. **Share your findings** — Write up what you learn. The Xbox modding/preservation community benefits from every discovery.

## Dependencies

The toolchain is intentionally lightweight:

```
Python 3.10+
capstone        # x86 disassembly (pip install capstone)
```

That's it. No IDA, no Ghidra, no proprietary tools. Standard library + Capstone.

The runtime libraries (C) use:
- MSVC (Visual Studio 2022) or MinGW-w64
- Windows SDK (D3D11, DXGI, XInput, waveOut)
- CMake 3.20+
- No external dependencies — all hardware emulation code is self-contained

## FAQ

**Q: Is this legal?**
A: This project provides tools and documentation. You must own a legitimate copy of any game you recompile. No copyrighted game code or assets are included in this repository.

**Q: How is this different from an emulator?**
A: Emulators interpret or JIT-compile code at runtime. Static recompilation translates the entire binary ahead of time into native C code that compiles to a regular `.exe`. There's no CPU emulation at runtime — the recompiled functions execute directly.

**Q: Can I use this on Xbox 360 games?**
A: No. Xbox 360 uses PowerPC (big-endian, different ISA). See [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for Xbox 360 static recompilation. This toolkit is specifically for the original Xbox's x86 code.

**Q: How long does it take to get a game running?**
A: It depends on the game's complexity. Symbol-rich debug builds and conventional engine/runtime usage can move quickly; complex games with custom engines, threading, networking, and direct hardware access take longer. The toolchain handles the mechanical translation — the real work is building the runtime shims and debugging indirect calls.

**Q: Why C output instead of direct x86-64 binary translation?**
A: C is portable, debuggable, and the compiler optimizes it for you. You can read the output, set breakpoints in it, and modify individual functions. Direct binary translation would be faster to run but impossible to debug or modify.

## License

MIT

## Credits

Built with [Claude Code](https://claude.ai) (Anthropic) — proving that AI-assisted systems programming can tackle problems previously considered impractical.

## References

- [XBE File Format](https://xboxdevwiki.net/Xbe) — Xbox Dev Wiki
- [Xbox Kernel Exports](https://xboxdevwiki.net/Kernel) — Xbox Dev Wiki
- [NV2A GPU](https://xboxdevwiki.net/NV2A) — Xbox GPU documentation
- [Xbox Architecture](https://www.copetti.org/writings/consoles/xbox/) — Copetti's deep dive
- [N64Recomp](https://github.com/N64Recomp/N64Recomp) — Static recomp for N64 (MIPS→C)
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) — Static recomp for Xbox 360 (PPC→C)
- [RexGlueSDK](https://github.com/rexglue/rexglue-sdk) — Xbox 360 recomp runtime (Xenia as link-time library)
- [Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded) — Xbox emulator (dynamic recomp)
- [xemu](https://github.com/xemu-project/xemu) — Xbox emulator (LLE)
