#!/usr/bin/env python3
"""powerd: this phone has no Apple battery authentication, so take powerd's own "not supported" path.

powerd's PackAuthManager support check (iOS 27.2 powerd, ~0x10000c71c) is
    if (!MGGetBoolAnswer(@"D6/BMDrlb8V3WSiqL8gL+w") || <device class check> || MGIsDeviceOfType(<list>)) unsupported
MobileGestalt answers that question from the identity we run as (iPhone 14 Pro: true) and does not take it from
the cache, so powerd waits forever for an auth service (AppleBatteryAuth) this phone cannot have: Settings'
Battery Health spins ("Not initializing due to auth not passing ... failedAuthServiceFlags=800000").
This patch makes the first test always say "unsupported" (cbz w0 -> b, same target). powerd's open source
(BatteryTimeRemaining.m) treats "not supported" as legacy devices without auth; it does NOT claim the battery is
genuine (no trusted state is produced). 2026-11-26.

Usage: tools/patch-powerd-battauth.py INPUT OUTPUT   (CodeDirectory page hashes refreshed as patch-bluetool27-profile.py)
"""
import hashlib, importlib.util, os, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("bt", HERE / "patch-bluetool27-profile.py")
bt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bt)

# bl _MGGetBoolAnswer ; cbz w0, +0xc ; bl <device class check> ; tbz w0, #0, +0xc  (the cbz is patched)
CBZ, B = 0x34000060, 0x14000003


def find(b: bytes) -> int:
    hits = []
    for off in range(0, len(b) - 16, 4):
        w = struct.unpack_from("<IIII", b, off)
        if w[0] >> 26 == 0x25 and w[1] == CBZ and w[2] >> 26 == 0x25 and w[3] == 0x36000060:
            hits.append(off + 4)
    return hits


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {Path(sys.argv[0]).name} INPUT OUTPUT", file=sys.stderr)
        return 2
    src, dst = map(Path, sys.argv[1:])
    if src.resolve() == dst.resolve():
        print("refusing to patch in place; keep the original for rollback", file=sys.stderr)
        return 1
    data = src.read_bytes()
    if not data.startswith(bt.MACHO64_MAGIC):
        print("input is not a 64-bit little-endian Mach-O", file=sys.stderr)
        return 1
    if b"Pack auth supported status checked" not in data:
        print("not the expected powerd (no PackAuthManager); NOT writing", file=sys.stderr)
        return 1
    hits = find(data)
    if len(hits) != 1:
        print(f"expected exactly one auth-support test, found {len(hits)}; NOT writing", file=sys.stderr)
        return 1
    off = hits[0]
    patched = bytearray(data)
    struct.pack_into("<I", patched, off, B)
    pages = bt.refresh_page_hashes(patched)
    tmp = dst.with_name(dst.name + ".new")
    tmp.write_bytes(patched)
    os.chmod(tmp, (src.stat().st_mode & 0o777) | 0o111)
    tmp.replace(dst)
    print(f"patched: {src} -> {dst} (file offset {off:#x}: cbz -> b)")
    print(f"input SHA-256:  {hashlib.sha256(data).hexdigest()}")
    print(f"output SHA-256: {hashlib.sha256(patched).hexdigest()}")
    print(f"CodeDirectory page hashes refreshed: {pages}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
