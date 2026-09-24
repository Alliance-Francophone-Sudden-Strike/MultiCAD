#!/usr/bin/env python3
"""
Stage a patched copy of ../src into obj/gen for the MinGW-w64 build.

The Visual Studio sources are MSVC-only in a handful of spots that GCC cannot
parse or does not support. Rather than fork the tree, we copy it and apply the
adjustments here. ../src is never modified. Run from the Makefile (target
`prep`) or directly.
"""
import hashlib
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "src"))
GEN = os.path.join(HERE, "obj", "gen")
STAMP = os.path.join(HERE, "obj", ".prep-stamp")


# --------------------------------------------------------------------------- #
# individual transforms
# --------------------------------------------------------------------------- #
def drop_seh(src: str) -> str:
    """GCC has no __try/__except. Turn the guard blocks into plain scopes;
    the handler bodies (empty, or a MessageBoxA) become dead code."""
    src = re.sub(
        r'(?m)^([ \t]*)__try[ \t]*$',
        r'\1/* __try -> plain block (MinGW build: SEH guard dropped) */',
        src)
    src = re.sub(
        r'(?m)^([ \t]*)__except[ \t]*\(\s*EXCEPTION_EXECUTE_HANDLER\s*\)[ \t]*$',
        r'\1if (false) /* __except (EXCEPTION_EXECUTE_HANDLER) */',
        src)
    return src


def fix_uifilter_case(src: str) -> str:
    return src.replace('#include "UiFilter.h"', '#include "UIFilter.h"')


_CONV_RE = re.compile(
    r'\bget(Fn|Ptr)<\s*'
    r'([A-Za-z_][\w:\s]*(?:\s*\*)*)\s*'                 # return type
    r'\(\s*__(cdecl|stdcall|thiscall|fastcall)\s*\)\s*' # calling convention
    r'\(([^()]*)\)\s*>'                                 # parameter list
)


def fix_calling_conv(src: str) -> str:
    """getFn<RET(__thiscall)(ARGS)> -> getFn<mscc::thiscall_<RET(ARGS)>::type>"""
    def repl(m):
        which, ret, conv, args = (m.group(1), m.group(2).strip(),
                                  m.group(3), m.group(4).strip())
        return f'get{which}<mscc::{conv}_<{ret}({args})>::type>'
    return _CONV_RE.sub(repl, src)


_USING_CONV_RE = re.compile(
    r'\busing(\s+\w+\s*=\s*)'                            # alias name
    r'([A-Za-z_][\w:\s]*(?:\s*\*)*)\s*'                  # return type
    r'\(\s*__(cdecl|stdcall|thiscall|fastcall)\s*\)\s*'    # calling convention
    r'\(([^()]*)\)\s*;'                                    # parameter list
)


def fix_using_conv(src: str) -> str:
    """using F = RET(__fastcall)(ARGS); -> using F = mscc::fastcall_<RET(ARGS)>::type;"""
    def repl(m):
        alias, ret, conv, args = (m.group(1), m.group(2).strip(),
                                  m.group(3), m.group(4).strip())
        return f'using{alias}mscc::{conv}_<{ret}({args})>::type;'
    return _USING_CONV_RE.sub(repl, src)


PATCHES = {
    "ui/SplashTextRenderer.h":        [drop_seh],
    "patcher/GameDllHooks.cpp":       [drop_seh, fix_uifilter_case],
    "ui/UIFilter.cpp":               [fix_uifilter_case],
}

# Run over every staged .cpp/.h rather than a per-file list. GCC parses a bare
# convention in an abstract declarator - `RET(__thiscall)(ARGS)` - but silently
# drops it, so a site nobody listed is a wrong-ABI call at runtime, not a build
# error. The guard in main() fails the build on any form these don't cover.
CONV_FIXES = [fix_calling_conv, fix_using_conv]

_BARE_CONV_RE = re.compile(r'\(\s*__(cdecl|stdcall|thiscall|fastcall)\s*\)')

EXPORT_WRAPPER = os.path.join(HERE, "mingw_export.cpp")


# --------------------------------------------------------------------------- #
def tree_signature(root: str) -> str:
    h = hashlib.sha256()
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            p = os.path.join(dirpath, name)
            h.update(os.path.relpath(p, root).encode())
            h.update(str(os.path.getmtime(p)).encode())
            h.update(str(os.path.getsize(p)).encode())
    for name in sorted(PATCHES):
        h.update(name.encode())
    h.update(open(__file__, "rb").read())
    return h.hexdigest()


def main() -> int:
    if not os.path.isdir(SRC):
        print(f"prep: source tree not found: {SRC}", file=sys.stderr)
        return 1

    sig = tree_signature(SRC)
    if os.path.isfile(STAMP) and open(STAMP).read().strip() == sig and os.path.isdir(GEN):
        return 0  # up to date

    if os.path.exists(GEN):
        shutil.rmtree(GEN)
    shutil.copytree(SRC, GEN)

    for rel, fns in PATCHES.items():
        p = os.path.join(GEN, rel)
        with open(p, "r", encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        original = text
        for fn in fns:
            text = fn(text)
        if text != original:
            with open(p, "w", encoding="utf-8", errors="surrogateescape") as f:
                f.write(text)
            print(f"prep: patched {rel}")
        else:
            print(f"prep: WARNING no change in {rel}", file=sys.stderr)

    shutil.copy2(EXPORT_WRAPPER, os.path.join(GEN, "mingw_export.cpp"))

    sources = []
    for dp, _, fns in os.walk(GEN):
        sources += [os.path.join(dp, n) for n in sorted(fns) if n.endswith((".cpp", ".h"))]

    for fp in sources:
        with open(fp, "r", encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        original = text
        for fn in CONV_FIXES:
            text = fn(text)
        if text != original:
            with open(fp, "w", encoding="utf-8", errors="surrogateescape") as f:
                f.write(text)
            print(f"prep: calling conventions in {os.path.relpath(fp, GEN)}")

    # guards: no SEH keywords, and no bare calling convention GCC would drop
    bad_seh, bad_conv = [], []
    for fp in sources:
        with open(fp, "r", encoding="utf-8", errors="surrogateescape") as f:
            for i, line in enumerate(f, 1):
                code = re.sub(r'/\*.*?\*/', '', line.split("//", 1)[0])
                where = f"{os.path.relpath(fp, GEN)}:{i}"
                if re.search(r'(?<![\w"])__(try|except|finally|leave)\b', code):
                    bad_seh.append(where)
                if _BARE_CONV_RE.search(code):
                    bad_conv.append(where)
    if bad_seh:
        print("prep: SEH keywords still present:\n  " + "\n  ".join(bad_seh), file=sys.stderr)
        return 1
    if bad_conv:
        print("prep: bare calling convention GCC would silently drop:\n  "
              + "\n  ".join(bad_conv), file=sys.stderr)
        return 1

    os.makedirs(os.path.dirname(STAMP), exist_ok=True)
    with open(STAMP, "w") as f:
        f.write(sig)
    print("prep: gen tree ready")
    return 0


if __name__ == "__main__":
    sys.exit(main())
