#!/usr/bin/env python3
"""Host-only structural checks for experimental guacamole Wi-Fi bring-up."""
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import adt


def child_named(node, name):
    for child in node.children:
        if any(k == 'name' and v == name.encode() + b'\0'
               for k, _, v in child.props):
            return child
    return None


with tempfile.TemporaryDirectory(prefix='guac-wifi-test-') as temp:
    for enabled in (False, True):
        out = Path(temp) / ('with-wifi.adt' if enabled else 'without-wifi.adt')
        args = [sys.executable, str(ROOT / 'tools/mkadt.py'), str(out),
                '--highmem', '--guacamole']
        if enabled:
            args.append('--guacamole-wifi')
        subprocess.run(args, cwd=ROOT, check=True, capture_output=True)
        device_tree, consumed = adt.parse(out.read_bytes())
        assert consumed == len(out.read_bytes())
        arm_io = child_named(device_tree, 'arm-io')
        assert arm_io is not None
        assert (child_named(arm_io, 'wlan') is not None) == enabled
        print(f'ADT wlan node: {"on" if enabled else "off"} -- OK')

    board = (ROOT / 'userland/wifi/guac_board.h').read_text()
    assert '#define GUAC_WIFI_HARDWARE_ENABLED 1' in board
    assert '#define GUAC_WIFI_DMA_POOL_PA   0xe0000000ull' in board
    assert '#define GUAC_WIFI_MODEM_PA   0x8dc00000ull' in board
    assert '#define GUAC_WIFI_MODEM_SIZE 0x09600000ull' in board
    assert '#define GUAC_WIFI_MSA_SIZE          0x00100000u' in board
    assert '#define GUAC_WIFI_SMMU_PA   0x15000000ull' in board
    assert '#define GUAC_WIFI_SID       0x640u' in board
    boot = (ROOT / 'tools/guac-mkboot.sh').read_text()
    assert 'WIFI:-1' in boot and 'sweetpmem=1 sweetwifid=1 sweetwifi=1' in boot
    assert 'cpus=${CPUS:-8}' in boot
    assert 'sweetgpu=$GPU_STAGE sweetgl=$GL_UI' in boot
    assert 'GPU_STAGE=${GPU_STAGE:-0}' in boot and 'GL_UI=${GL_UI:-0}' in boot

    workflow = (ROOT / '.github/workflows/xnu.yml').read_text()
    assert 'branches: [main, wifi-pixel4-stage, wifi-pixel4-latest]' in workflow
    assert 'python3 tools/test-guac-wifi-stage.py' in workflow
    assert 'sh userland/wifi/build.sh' in workflow
    assert 'name: guacamole-wland' in workflow

    candidates = ROOT / 'userland/wifi/kernel-patch-candidates'
    pmem = (candidates / '0125-guacamole-pmem-candidate.patch').read_text()
    bridge = (candidates / '0127-guacamole-wifi-apple80211-candidate.patch').read_text()
    assert '+\tuint32_t on = 0;' in pmem
    assert '+\tuint32_t on = 0;' in bridge
    assert 'iokit/Kernel/arm/SweetSurface.cpp' not in bridge
    assert 'iokit/Kernel/arm/SweetKeyStore.cpp' not in bridge
    for patch in sorted(candidates.glob('*.patch')):
        subprocess.run(['git', 'apply', '--stat', str(patch)],
                       cwd=ROOT, check=True, capture_output=True)
        print(f'Kernel candidate parse: {patch.name} -- OK')

    for donor_num, active_name in [
        ('0125', '0130-guacamole-wifi-pmem.patch'),
        ('0126', '0131-guacamole-wifi-rpmh.patch'),
        ('0127', '0132-guacamole-wifi-apple80211.patch'),
        ('0128', '0133-guacamole-wifi-bss.patch'),
    ]:
        candidate = next(candidates.glob(donor_num + '-*.patch'))
        active = ROOT / 'xnu/patches' / active_name
        assert active.read_bytes() == candidate.read_bytes()
        print(f'Active kernel patch: {active_name} -- OK')

    plist_in = Path(temp) / 'launchd-in.plist'
    plist_out = Path(temp) / 'launchd-out.plist'
    with plist_in.open('wb') as f:
        plistlib.dump({'LaunchDaemons': {}}, f)
    subprocess.run([sys.executable, str(ROOT / 'tools/sweet-root-tweaks.py'),
                    '--launchd-plist', str(plist_in), str(plist_out)],
                   cwd=ROOT, check=True, capture_output=True)
    with plist_out.open('rb') as f:
        jobs = plistlib.load(f)['LaunchDaemons']
    wland = jobs['/System/Library/LaunchDaemons/org.guacamole.wland.plist']
    assert wland['RunAtLoad']
    assert wland['ProgramArguments'] == ['/usr/libexec/wland', 'auto']
    print('Launchd Wi-Fi initialization job -- OK')

print('Host-only structural tests PASSED. XNU and radio firmware are NOT built or device-tested.')
