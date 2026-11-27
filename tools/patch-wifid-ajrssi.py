#!/usr/bin/env python3
"""wifid: let auto-join take a known network whatever WiFiPolicy's per-device RSSI threshold says.

WiFiPolicy (built into iOS 27.2's /usr/sbin/wifid) answers CoreWiFi's join-candidate callout
(-[CWFAutoJoinManager __calloutToAllowJoinCandidate:...]) with
    threshold = <device manager's auto-join RSSI threshold for this network>   (wifid ~0x1001ac268)
    if (candidate.RSSI < threshold) refuse: "Below minimum RSSI threshold", reason LowRSSI
On this phone the threshold comes out above any real signal: the home network at -47 dBm on 5 GHz was refused
18 times in a row (2026-11-27, outputs/aj-info4.txt), so Wi-Fi never joined by itself after the power-budget
(ApplePPM) and scan-timestamp fixes. This patch makes that one comparison never refuse (b.lt -> nop). The scan's own
minRSSI (-80 dBm, -90 for its low-RSSI pass) still keeps weak networks out before this point. 2026-11-27.

Usage: tools/patch-wifid-ajrssi.py INPUT OUTPUT   (CodeDirectory page hashes refreshed as patch-bluetool27-profile.py)
"""
import hashlib, importlib.util, os, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("bt", HERE / "patch-bluetool27-profile.py")
bt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bt)

CMP = 0xEB38C01F            # cmp x0, w24, sxtw   (candidate RSSI vs threshold)
NOP = 0xD503201F


def find(b: bytes) -> list:
    hits = []
    for off in range(0, len(b) - 8, 4):
        w, w2 = struct.unpack_from("<II", b, off)
        if w == CMP and (w2 & 0xFF00001F) == 0x5400000B:      # b.lt <refuse>
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
    if b"Below minimum RSSI threshold" not in data:
        print("not the expected wifid (no 'Below minimum RSSI threshold'); NOT writing", file=sys.stderr)
        return 1
    hits = find(data)
    if len(hits) != 1:
        print(f"expected exactly one RSSI threshold test, found {len(hits)}; NOT writing", file=sys.stderr)
        return 1
    off = hits[0]
    patched = bytearray(data)
    struct.pack_into("<I", patched, off, NOP)
    pages = bt.refresh_page_hashes(patched)
    tmp = dst.with_name(dst.name + ".new")
    tmp.write_bytes(patched)
    os.chmod(tmp, (src.stat().st_mode & 0o777) | 0o111)
    tmp.replace(dst)
    print(f"patched: {src} -> {dst} (file offset {off:#x}: b.lt -> nop)")
    print(f"input SHA-256:  {hashlib.sha256(data).hexdigest()}")
    print(f"output SHA-256: {hashlib.sha256(patched).hexdigest()}")
    print(f"CodeDirectory page hashes refreshed: {pages}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
