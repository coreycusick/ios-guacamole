#!/usr/bin/env python3
"""BatteryUsageUI (Settings' Battery pane): this phone has no Apple battery authentication.

Settings' Battery Health waits on a battery-authentication result ("Adding auth spinner" / "Auth spinner tick")
whenever MobileGestalt's battery-auth question (obfuscated key D6/BMDrlb8V3WSiqL8gL+w, the same one powerd asks,
see patch-powerd-battauth.py) answers true; MobileGestalt answers it from the identity we run as, not from its cache.
Here every `adrp x0 / add x0 (that key) / bl _MGGetBoolAnswer` in the bundle has its call replaced by
`mov w0, #0`: the pane takes its path for devices without battery authentication. Nothing claims the battery is
genuine. 2026-11-27.

Usage: tools/patch-bui-battauth.py INPUT OUTPUT
  INPUT = /System/Library/PreferenceBundles/BatteryUsageUI.bundle/BatteryUsageUI (CodeDirectory page hashes refreshed)
"""
import hashlib, importlib.util, os, re, struct, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("bt", HERE / "patch-bluetool27-profile.py")
bt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bt)
KEY = b"D6/BMDrlb8V3WSiqL8gL+w"
MOV_W0_0 = 0x52800000


def sections(b):
    """(segname, sectname, addr, size, offset) from the Mach-O load commands."""
    out = []
    ncmds = struct.unpack_from("<I", b, 16)[0]
    p = 32
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from("<II", b, p)
        if cmd == 0x19:  # LC_SEGMENT_64
            nsects = struct.unpack_from("<I", b, p + 64)[0]
            q = p + 72
            for _ in range(nsects):
                sect = b[q:q + 16].rstrip(b"\0").decode()
                seg = b[q + 16:q + 32].rstrip(b"\0").decode()
                addr, size, off = struct.unpack_from("<QQI", b, q + 32)
                out.append((seg, sect, addr, size, off))
                q += 80
        p += cs
    return out


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {Path(sys.argv[0]).name} INPUT OUTPUT", file=sys.stderr)
        return 2
    src, dst = map(Path, sys.argv[1:])
    if src.resolve() == dst.resolve():
        print("refusing to patch in place; keep the original for rollback", file=sys.stderr)
        return 1
    b = bytearray(src.read_bytes())
    secs = {s[1]: s for s in sections(b)}
    if "__cfstring" not in secs or "__text" not in secs:
        print("no __cfstring/__text; NOT writing", file=sys.stderr)
        return 1
    cstr = bytes(b).find(KEY + b"\0")
    _, _, cfa, cfs, cfo = secs["__cfstring"]
    key_va = None
    for e in range(cfo, cfo + cfs, 32):
        if struct.unpack_from("<Q", b, e + 16)[0] & 0xFFFFFFFFFF == cstr:
            key_va = cfa + (e - cfo)
    if cstr < 0 or key_va is None:
        print("battery-auth key not found; NOT writing", file=sys.stderr)
        return 1
    _, _, ta, ts, to = secs["__text"]
    hits = []
    for off in range(to, to + ts - 8, 4):
        w0, w1, w2 = struct.unpack_from("<III", b, off)
        if (w0 & 0x9F00001F) != 0x90000000 or (w1 & 0xFFC003FF) != 0x91000000 or (w2 >> 26) != 0x25:
            continue  # adrp x0 ; add x0, x0, #imm ; bl
        pc = ta + (off - to)
        immlo, immhi = (w0 >> 29) & 3, (w0 >> 5) & 0x7FFFF
        page = (pc & ~0xFFF) + (((immhi << 2 | immlo) ^ 0x100000) - 0x100000 << 12)
        if page + ((w1 >> 10) & 0xFFF) == key_va:
            hits.append(off + 8)
    if not hits:
        print("no MGGetBoolAnswer(battery-auth) call found; NOT writing", file=sys.stderr)
        return 1
    for h in hits:
        struct.pack_into("<I", b, h, MOV_W0_0)
    pages = bt.refresh_page_hashes(b)
    tmp = dst.with_name(dst.name + ".new")
    tmp.write_bytes(b)
    os.chmod(tmp, (src.stat().st_mode & 0o777) | 0o111)
    tmp.replace(dst)
    print(f"patched {len(hits)} call(s) at file offsets {[hex(h) for h in hits]}: {src} -> {dst}")
    print(f"output SHA-256: {hashlib.sha256(b).hexdigest()}; CodeDirectory page hashes refreshed: {pages}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
