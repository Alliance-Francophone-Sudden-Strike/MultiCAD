# MultiCAD — MinGW-w64 cross build (Linux)

Unofficial Linux build path for MultiCAD. The Visual Studio project in `../`
stays the source of truth; this only exists so the DLLs can be produced on a
Linux box with no MSVC. Nothing here is committed.

## Requirements

- `mingw-w64` + `g++-mingw-w64-i686` (GCC 13, Win32 threads)
- `python3`, `make`
- `wine` (optional, only for `make test`)

```sh
sudo apt install g++-mingw-w64-i686 mingw-w64 make python3
```

## Build

```sh
cd mingw
make            # -> bin/cadMulti_mt.dll  and  bin/cadMulti.dll
make mt         # only the standalone cadMulti_mt.dll
make test       # LoadLibrary smoke test both DLLs under Wine
make clean
```

`bin/` and `obj/` are git-ignored (root `.gitignore` covers `[Bb]in/` `[Oo]bj/`).

## Output

| File | Deps | Notes |
|------|------|-------|
| `bin/cadMulti_mt.dll` | none beyond system DLLs | **use this** — static libstdc++/libgcc/libwinpthread, analogue of the VS `Release_MT` config |
| `bin/cadMulti.dll` | `libstdc++-6.dll`, `libgcc_s_dw2-1.dll`, `libwinpthread-1.dll` (copied into `bin/` by the build) | analogue of the VS `Release` config; drop all four files into the game folder together |

Both export exactly `CADraw_Init` (the symbol the game's CAD loader looks up).

## How it works

`make` runs `prep.py`, which copies `../src` into `obj/gen/` and applies a small
set of MSVC→GCC adjustments (never touching `../src`), then compiles that copy.

`msvc_compat.h` is force-included (`-include`) into every TU.

### Adjustments (`prep.py`)

| What | Why | Impact |
|------|-----|--------|
| `__try` / `__except` → plain scope | GCC has no SEH keywords | 5 sites (`SplashTextRenderer.h`, `GameDllHooks.cpp`). Handlers become dead code — **a fault inside those blocks now crashes instead of being swallowed.** They guard reverse-engineered heap-free reimplementations and splash-text callbacks. |
| `getFn<RET(__thiscall)(ARGS)>` → `getFn<mscc::thiscall_<RET(ARGS)>::type>` | MSVC's bare calling-convention token in an abstract declarator doesn't parse in GCC | 169 call sites; `mscc::` aliases in `msvc_compat.h` carry the convention as a trailing `__attribute__`. Pointer forms `RET(__thiscall* f)(ARGS)` already parse and are left alone. |
| `std::ifstream(std::wstring)` → `.c_str()` | MinGW libstdc++ has the `const wchar_t*` ctor, not the `wstring` one | `DllVersionDetector.cpp` |
| `#include "UiFilter.h"` → `"UIFilter.h"` | case-sensitive FS | `GameDllHooks.cpp`, `UIFilter.cpp` |

### Other pieces

- `shim/Windows.h`, `shim/TlHelp32.h` — case shims for the two capitalised system includes.
- `shim/atlbase.h` — ~40-line `CComPtr<T>` (MinGW ships no ATL; only `AudioHelper.h` needs it).
- `msvc_compat.h` — force-includes `<cstdint>/<cstring>/…` (MSVC leaks these transitively) and `<crtdbg.h>` (no-op `_CrtSetDbgFlag` stubs).
- `mingw_export.cpp` + `cadMulti.def` — replace the `#pragma comment(linker,"/EXPORT")` alias.
- `MultiCAD_mingw.rc` — UTF-8 rewrite of the UTF-16 `src/MultiCAD.rc` (`VS_VERSION_INFO` only).
- `-fpermissive` — 4 implicit function-pointer→`void*` conversions in `GameDllHooks.cpp` (value passed straight through, same as MSVC).

## Caveats

- GCC codegen ≠ MSVC codegen. Calling conventions for the game hooks
  (cdecl/stdcall/thiscall/fastcall) match on 32-bit x86, and both DLLs
  `LoadLibrary` cleanly under Wine, but this has **not** been run inside an
  actual game here. Test before relying on it.
- The SEH change above is a real behavioural difference from the MSVC build.
