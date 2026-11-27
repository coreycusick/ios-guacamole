#!/usr/bin/env python3
"""lower-macho.py FILE... : lower arm64e Mach-O files in place for a CPU without FEAT_PAuth (as lower-tar.py does for a
root tar): every non-hint PAC word in the code sections becomes its plain equivalent (zeta_userland_pauth.lower), and the
CodeDirectory page hashes are refreshed. For userland we build ourselves (iOS 26 Metal plugin, Turnip). Words without an
exact lowering are printed and kept."""
import sys, os, struct, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zeta_userland_pauth import lower
PRE = ((0xffff0000, 0xdac10000), (0xffe0fc00, 0x9ac03000), (0xfe9f0000, 0xd61f0000), (0xff200400, 0xf8200400))
ALGO = {1: 'sha1', 2: 'sha256', 3: 'sha256', 4: 'sha384'}

def slices(b):
    magic = struct.unpack_from('>I', b)[0]
    if magic in (0xcafebabe, 0xcafebabf):
        n, big = struct.unpack_from('>I', b, 4)[0], magic == 0xcafebabf
        for i in range(n):
            if big:
                cpu, sub, off, size = struct.unpack_from('>iiQQ', b, 8 + 32 * i)
            else:
                cpu, sub, off, size = struct.unpack_from('>iiII', b, 8 + 20 * i)
            yield off, size
    elif struct.unpack_from('<I', b)[0] == 0xfeedfacf:
        yield 0, len(b)

def lower_slice(b, base, refused, path):
    magic, cpu, sub, ft, ncmds, sz = struct.unpack_from('<IiiIII', b, base)
    if magic != 0xfeedfacf or cpu != 0x0100000c or sub & 0xff != 2:
        return 0, None
    words, sig, p = 0, None, base + 32
    for _ in range(ncmds):
        cmd, cs = struct.unpack_from('<II', b, p)
        if cmd == 0x19:
            for k in range(struct.unpack_from('<I', b, p + 64)[0]):
                so = p + 72 + 80 * k
                size, off = struct.unpack_from('<QI', b, so + 40)
                flags = struct.unpack_from('<I', b, so + 64)[0]
                if not flags & 0x80000400 or not off:
                    continue
                o0 = base + off
                for j, (w,) in enumerate(struct.iter_unpack('<I', bytes(b[o0:o0 + (size // 4) * 4]))):
                    for mask, val in PRE:
                        if w & mask == val:
                            break
                    else:
                        continue
                    try:
                        r = lower(w)
                    except ValueError as e:
                        refused.append((path, hex(off + 4 * j), str(e)))
                        continue
                    if r:
                        struct.pack_into('<I', b, o0 + 4 * j, r[1])
                        words += 1
        elif cmd == 0x1d:
            sig = struct.unpack_from('<II', b, p + 8)
        p += cs
    sigres = 'unsigned'
    if sig and words:
        so = base + sig[0]
        if b[so:so + 4] == b'\xfa\xde\x0c\xc0':
            sigres = []
            for i in range(struct.unpack_from('>I', b, so + 8)[0]):
                btype, boff = struct.unpack_from('>II', b, so + 12 + 8 * i)
                cd = so + boff
                if b[cd:cd + 4] != b'\xfa\xde\x0c\x02':
                    continue
                hash_off, _, _, ncode, limit = struct.unpack_from('>IIIII', b, cd + 16)
                hsize, htype, page = b[cd + 36], b[cd + 37], 1 << b[cd + 39]
                if b[cd + 39] == 0:
                    page = limit or 1
                n = 0
                for pg in range(ncode):
                    s = base + pg * page
                    e = base + min(pg * page + page, limit)
                    h = hashlib.new(ALGO[htype], bytes(b[s:e])).digest()[:hsize]
                    slot = cd + hash_off + pg * hsize
                    if b[slot:slot + hsize] != h:
                        b[slot:slot + hsize] = h
                        n += 1
                sigres.append(f'{ALGO[htype]}/{page}:{n}')
            sigres = ','.join(sigres) or 'no CD'
    elif sig:
        sigres = 'signed, unchanged'
    return words, sigres


rc = 0
for path in sys.argv[1:]:
    b = bytearray(open(path, 'rb').read())
    refused, total, sigs = [], 0, []
    for off, size in slices(b):
        w, s = lower_slice(b, off, refused, path)
        total += w
        if s:
            sigs.append(s)
    if total:
        open(path, 'wb').write(b)
    print(f'{path}: {total} words lowered, signature {";".join(sigs) or "-"}, {len(refused)} refused')
    for r in refused[:20]:
        print('  refused', *r)
    rc |= bool(refused)
sys.exit(rc)
