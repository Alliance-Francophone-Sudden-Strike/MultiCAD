#!/usr/bin/env python3
"""Check DllVersionDetector::AnalyzeDll against an independent PE reader.

AnalyzeDll hashes the first .text/.petite/"." section of a PE to fingerprint
game DLL versions, so a silent change there silently breaks version detection.
This parses the same section with struct/hashlib and compares.

Usage: analyze_dll_test.py <wine-exe> <pe-file>...
"""
import hashlib
import os
import struct
import subprocess
import sys


def expected_hash(path):
    """SHA-256 of the first .text/.petite/"." section's raw bytes, or None."""
    d = open(path, "rb").read()
    if len(d) < 0x40 or d[:2] != b"MZ":
        return None
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if len(d) < pe + 24 or d[pe:pe + 4] != b"PE\0\0":
        return None
    nsec, opt_size = struct.unpack_from("<H", d, pe + 6)[0], struct.unpack_from("<H", d, pe + 20)[0]
    table = pe + 24 + opt_size
    if table + nsec * 40 > len(d):
        return None
    for i in range(nsec):
        name, vsize, vaddr, rsize, roff = struct.unpack_from("<8sIIII", d, table + i * 40)
        if not (name.startswith(b".text") or name.startswith(b".petite") or name == b".\0" * 4):
            continue
        if rsize == 0 or roff + rsize > len(d):
            return None
        return hashlib.sha256(d[roff:roff + rsize]).hexdigest()
    return None


def main(argv):
    exe, files = argv[1], argv[2:]
    # No check=True: a broken AnalyzeDll exits non-zero, and the per-file
    # comparison below is a far more useful report than a traceback.
    out = subprocess.run(["wine", exe, *files], capture_output=True, text=True,
                         env=os.environ | {"WINEDEBUG": "-all"}).stdout
    got = {}
    for line in out.splitlines():
        if " hash=" not in line:
            continue
        got[line.split()[0]] = line.split("hash=")[1].strip()

    failures = 0
    for f in files:
        want = expected_hash(f)
        have = got.get(f)
        zero = "0" * 64
        ok = (have == want) if want else (have == zero)
        print(f"  {'ok  ' if ok else 'FAIL'} {f.split('/')[-1]:<24} {have}")
        failures += not ok
    print("analyze_dll_test:", "PASS" if not failures else f"{failures} FAILED")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
