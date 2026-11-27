#!/usr/bin/env python3
"""Turn a kern.sweet_profile report (bsd/dev/sweet_sample.c) into function names, summed per function.

  ssh PHONE 'sysctl kern.sweet_profile="backboardd 10"; sleep 12; sysctl -n kern.sweet_profile' > outputs/prof.txt
  python3 tools/sweet-profile.py outputs/prof.txt [--cache CACHE] [--bin NAME=PATH ...] [--top N] [--chains N]

CACHE: the iOS 27.2 dyld_shared_cache_arm64e (default research/ios27.2/dsc-system/dyld_shared_cache_arm64e);
addresses in it are unslid and resolved with `ipsw dyld a2s` (cached in outputs/.a2s-cache.json between runs).
Images outside the cache (the report's "img LOAD PATH" lines: the main binary, plugins such as AdrenoMetal) are
resolved with macOS `atos` when --bin gives a local copy for the image's file name (e.g.
--bin NativeAdrenoMetal=work/UniXNU/.../NativeAdrenoMetal); otherwise shown as image + offset.
Leaf = where the CPU time is (self time); inclusive = time spent in a function or anything it called. Return
addresses (every inclusive frame but a leaf pc) are looked up at address - 1, inside the calling instruction.
Only reads the report; runs nothing on the phone.
"""
import argparse, collections, json, os, re, subprocess, sys

CACHE_LO, CACHE_HI = 0x180000000, 0x300000000
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def parse(path):
    rep = {'slide': 0, 'mh': 0, 'imgs': [], 'leaf': [], 'incl': [], 'call': [], 'head': ''}
    for line in open(path, errors='replace'):
        line = line.rstrip('\n')
        if line.startswith('profile '):
            rep['head'] = line
        m = re.match(r'slide (0x[0-9a-f]+) mh (0x[0-9a-f]+)', line)
        if m:
            rep['slide'], rep['mh'] = int(m.group(1), 16), int(m.group(2), 16)
        m = re.match(r'img (0x[0-9a-f]+) (.*)', line)
        if m:
            rep['imgs'].append((int(m.group(1), 16), m.group(2)))
        m = re.match(r'(leaf|incl) (\d+) (0x[0-9a-f]+)', line)
        if m:
            rep[m.group(1)].append((int(m.group(2)), int(m.group(3), 16)))
        m = re.match(r'call (\d+)((?: 0x[0-9a-f]+)+)', line)
        if m:
            rep['call'].append((int(m.group(1)), [int(x, 16) for x in m.group(2).split()]))
    rep['imgs'].sort()
    return rep


def strip_offset(sym):
    return re.sub(r'\s*\+\s*(0x)?[0-9a-f]+\s*$', '', sym).strip()


class Resolver:
    def __init__(self, rep, cache, bins):
        self.rep, self.cache, self.bins = rep, cache, bins
        self.store = os.path.join(REPO, 'outputs', '.a2s-cache.json')
        try:
            self.a2s = json.load(open(self.store))
        except Exception:
            self.a2s = {}
        self.atos = {}

    def save(self):
        os.makedirs(os.path.dirname(self.store), exist_ok=True)
        json.dump(self.a2s, open(self.store, 'w'))

    def image_of(self, addr):
        best = None
        for load, path in self.rep['imgs']:
            if load <= addr and addr - load < 0x10000000:
                best = (load, path)
        return best

    def cache_sym(self, unslid):
        key = hex(unslid)
        if key not in self.a2s:
            r = subprocess.run(['ipsw', 'dyld', 'a2s', self.cache, key], capture_output=True, text=True)
            lines = [l for l in r.stdout.splitlines() if l.startswith('0x')]
            self.a2s[key] = lines[-1].split(': ', 1)[1] if lines else '?'
        return self.a2s[key]

    def prefetch_atos(self, addrs):
        """One atos call per image for all its addresses."""
        per = collections.defaultdict(list)
        for a in addrs:
            img = self.image_of(a)
            if img and os.path.basename(img[1]) in self.bins:
                per[img].append(a)
        for (load, path), lst in per.items():
            r = subprocess.run(['atos', '-o', self.bins[os.path.basename(path)], '-l', hex(load)] + [hex(a) for a in lst],
                               capture_output=True, text=True)
            for a, out in zip(lst, r.stdout.splitlines()):
                self.atos[a] = out.strip()

    def name(self, addr):
        slide = self.rep['slide']
        if CACHE_LO <= addr - slide < CACHE_HI:
            return self.cache_sym(addr - slide)
        if addr in self.atos:
            return self.atos[addr]
        img = self.image_of(addr)
        if img:
            return f'{os.path.basename(img[1])}@{hex(addr - img[0])}'
        return f'?@{hex(addr)}'


def table(title, rows, res, top, is_leaf, total):
    sums, examples = collections.Counter(), {}
    look = [a if is_leaf else a - 1 for _, a in rows]
    res.prefetch_atos(look)
    for i, ((n, a), la) in enumerate(zip(rows, look)):
        sys.stderr.write(f'\r{title}: {i + 1}/{len(rows)}')
        f = strip_offset(res.name(la))
        sums[f] += n
        examples.setdefault(f, hex(a))
    sys.stderr.write('\n')
    print(f'\n== {title} (share of all thread samples)')
    for f, n in sums.most_common(top):
        print(f'{100.0 * n / total:6.1f}%  {n:7d}  {f}   [{examples[f]}]')


def chains(rows, res, top, total, depth):
    """Call chains (kernel sweet_sample.c "call" lines), summed by function names: leaf <- caller <- ..."""
    if not rows:
        return
    look = sorted({a if i == 0 else a - 1 for _, ch in rows for i, a in enumerate(ch[:depth]) if a})
    res.prefetch_atos(look)
    sums, done = collections.Counter(), 0
    for n, ch in rows:
        names = []
        for i, a in enumerate(ch[:depth]):
            if not a:
                break
            names.append(strip_offset(res.name(a if i == 0 else a - 1)))
        dedup = [x for j, x in enumerate(names) if j == 0 or x != names[j - 1]]
        sums[' <- '.join(dedup)] += n
        done += 1
        sys.stderr.write(f'\rcall chains: {done}/{len(rows)}')
    sys.stderr.write('\n')
    print(f'\n== call chains (leaf <- callers, {depth} frames; share of all thread samples)')
    for c, n in sums.most_common(top):
        print(f'{100.0 * n / total:6.1f}%  {n:7d}  {c}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('report')
    ap.add_argument('--cache', default=os.path.join(REPO, 'research/ios27.2/dsc-system/dyld_shared_cache_arm64e'))
    ap.add_argument('--bin', action='append', default=[], help='NAME=PATH: local copy of an image outside the cache')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--limit', type=int, default=150, help='resolve only the N biggest addresses of each list')
    ap.add_argument('--chains', type=int, default=80, help='resolve the N biggest call chains (0: none)')
    ap.add_argument('--chain-depth', type=int, default=6, help='frames of each chain to name (1-6)')
    a = ap.parse_args()
    rep = parse(a.report)
    if not rep['leaf']:
        sys.exit('no leaf lines: is the profile finished? (sysctl -n kern.sweet_profile says "running" until then)')
    bins = dict(b.split('=', 1) for b in a.bin)
    m = re.search(r'(\d+) thread samples', rep['head'])
    total = int(m.group(1)) if m else sum(n for n, _ in rep['leaf'])
    print(rep['head'])
    for load, path in rep['imgs']:
        print(f'image {hex(load)} {path}')
    res = Resolver(rep, a.cache, bins)
    try:
        table('self time (leaf)', rep['leaf'][:a.limit], res, a.top, True, total)
        table('inclusive (function and its callees)', rep['incl'][:a.limit], res, a.top, False, total)
        if a.chains:
            chains(rep['call'][:a.chains], res, a.top, total, max(1, min(6, a.chain_depth)))
    finally:
        res.save()


if __name__ == '__main__':
    main()
