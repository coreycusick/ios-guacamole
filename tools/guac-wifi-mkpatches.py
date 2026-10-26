#!/usr/bin/env python3
"""Stage only the Pixel 4 kernel Wi-Fi pieces as guacamole patch candidates.

Usage from ios-sweet guacamole repo root:
    python3 tools/guac-wifi-mkpatches.py ../ios-flame-reference

Do NOT copy candidates into xnu/patches without applying and building against
the OnePlus XNU tree: iOS-flame and guacamole diverged after patch 0081.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'userland/wifi/kernel-patch-candidates'


def sections(source: Path):
    raw = source.read_text()
    result = {}
    for item in raw.split('diff --git ')[1:]:
        head = item.split('\n', 1)[0]
        path = head.split(' b/')[-1]
        if path in result:
            raise ValueError(f'duplicate patch path: {path}')
        result[path] = 'diff --git ' + item
    return result


def select(source: Path, wanted):
    blocks = sections(source)
    missing = set(wanted) - blocks.keys()
    if missing:
        raise ValueError(f'{source}: missing sections: {sorted(missing)}')
    return ''.join(blocks[k] for k in wanted)


def pmem_glue():
    # Minimal anchors remain valid after guacamole adds GPU and SWEETBOX code.
    return '''diff --git a/bsd/conf/files b/bsd/conf/files
--- a/bsd/conf/files
+++ b/bsd/conf/files
@@ -1 +1,2 @@
 bsd/dev/sweet_rpmh.c\t\t\tstandard
+bsd/dev/sweet_pmem.c\t\t\tstandard
diff --git a/bsd/dev/sweet_ufs.c b/bsd/dev/sweet_ufs.c
--- a/bsd/dev/sweet_ufs.c
+++ b/bsd/dev/sweet_ufs.c
@@ -1 +1,2 @@
 extern void sweet_cpufreq_init(void);
+extern void sweet_pmem_init(void);
@@ -1 +1,2 @@
 \tsweet_cpufreq_init();
+\tsweet_pmem_init();
'''


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: guac-wifi-mkpatches.py /path/to/ios-flame')
    donor = Path(sys.argv[1]).resolve() / 'xnu/patches'
    if not donor.is_dir():
        sys.exit(f'not a donor xnu/patches directory: {donor}')
    OUT.mkdir(parents=True, exist_ok=True)

    pmem = select(donor / '0094-flame-pmem-files-read.patch',
                  ['bsd/dev/sweet_pmem.c'])
    # Both physical memory and the synthetic Wi-Fi bridge must be explicit
    # opt-ins on the OnePlus: a normal UI boot must not expose either device.
    assert pmem.count('+\tuint32_t on = 1;') == 1
    pmem = pmem.replace('+\tuint32_t on = 1;', '+\tuint32_t on = 0;')
    pmem += pmem_glue()
    rpmh = (donor / '0102-flame-rpmh-generic.patch').read_text()
    bridge = select(donor / '0123-flame-en0-apple80211-wifid-over-wland.patch', [
        'bsd/conf/files',
        'bsd/dev/flame_sta.c', 'bsd/dev/flame_wlan.c',
        'bsd/dev/redwood_apple80211.c', 'bsd/dev/redwood_net.c',
        'bsd/dev/redwood_sta.c', 'bsd/dev/sweet_ufs.c',
        'bsd/sys/flame_wlan.h', 'bsd/sys/redwood_input.h',
        'bsd/sys/redwood_net.h', 'bsd/sys/redwood_sta.h',
        'iokit/Kernel/IOService.cpp', 'iokit/Kernel/IOStartIOKit.cpp',
        'iokit/Kernel/arm/MiniIONetworkStack.cpp',
        'iokit/Kernel/arm/RedwoodWLAN.cpp',
        'iokit/conf/files.arm64',
    ])
    assert bridge.count('+\tuint32_t on = 1;') == 1
    bridge = bridge.replace('+\tuint32_t on = 1;', '+\tuint32_t on = 0;')
    bss = select(donor / '0124-flame-surf10-webcontent-only-link-bss.patch',
                 ['bsd/dev/flame_wlan.c'])
    for filename, content in [
        ('0125-guacamole-pmem-candidate.patch', pmem),
        ('0126-guacamole-rpmh-candidate.patch', rpmh),
        ('0127-guacamole-wifi-apple80211-candidate.patch', bridge),
        ('0128-guacamole-wifi-linked-bss-candidate.patch', bss),
    ]:
        dest = OUT / filename
        dest.write_text(content)
        print(f'{dest.relative_to(ROOT)} ({len(content)} bytes)')
    print('NOT INSTALLED: validate against OnePlus patched XNU before enabling any kernel patch.')


if __name__ == '__main__':
    main()
