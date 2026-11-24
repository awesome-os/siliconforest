#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the real staging transform on a combined, unmodified USB tree.

Argument: native build-host source tree. This checks bundle composition,
including refusal before source mutation; it does not establish USB hardware.
"""
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]) / 'src/Kernel/Extensions/RavynXHCIPort'
relative = Path('src/Kernel/Extensions/RavynXHCIPort')
transform = here / 'tools/prepare_dock_usb.py'
original = plistlib.loads((source / 'Info.plist').read_bytes())
personalities = original['IOKitPersonalities']
preserved = {k: v for k, v in personalities.items()
             if v.get('IOClass') == 'SFAppleUSB' or
             v.get('IONameMatch') == 'pd,apple-dwc3-host'}

with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    for case in ('complete', 'missing-personality', 'missing-code', 'missing-direct'):
        tree = directory / case
        dest = tree / relative
        shutil.copytree(source, dest)
        dest.chmod(0o755)
        for file in dest.rglob('*'):
            if file.is_file():
                file.chmod(0o644)
            elif file.is_dir():
                file.chmod(0o755)
        info = plistlib.loads((dest / 'Info.plist').read_bytes())
        if case in ('missing-personality', 'missing-direct'):
            info['IOKitPersonalities'] = {
                k: v for k, v in info['IOKitPersonalities'].items()
                if not (v.get('IOClass') == 'SFAppleUSB' if case == 'missing-personality'
                        else v.get('IONameMatch') == 'pd,apple-dwc3-host')}
            (dest / 'Info.plist').write_bytes(plistlib.dumps(info))
        elif case == 'missing-code':
            (dest / 'SFAppleUSB.cpp').unlink()
        before = {p.relative_to(dest): p.read_bytes() for p in dest.rglob('*') if p.is_file()}
        result = subprocess.run([sys.executable, str(transform), str(tree)], capture_output=True)
        if case != 'complete':
            assert result.returncode, case
            assert before == {p.relative_to(dest): p.read_bytes()
                              for p in dest.rglob('*') if p.is_file()}, case
            continue
        assert result.returncode == 0, result.stderr.decode()
        after = plistlib.loads((dest / 'Info.plist').read_bytes())['IOKitPersonalities']
        assert len(after) == len(preserved) + 1
        assert all(after[k] == v for k, v in preserved.items())
        pci = after['T8103 dock FL1100']
        assert pci['IOProviderClass'] == 'IOPCIDevice'
        assert pci['IOPCIPrimaryMatch'] == '0x11001b73' and pci['IOPCITunnelCompatible'] is True
        assert all(p.read_bytes() == before[p.relative_to(dest)]
                   for p in dest.rglob('*') if p.is_file() and
                   p.name not in ('Info.plist', 'RavynXHCIPort.cpp', 'RavynXHCIPort.h',
                                  'RavynXHCIUSBBus.cpp'))
        cpp = (dest / 'RavynXHCIPort.cpp').read_text()
        assert 'if (!fPCI || fPCI->configRead32(0)' not in cpp
        assert 'if (!fPCI) return true;' in cpp
        assert 'startApple(provider)' in cpp
        print('actual USB composition:', case, 'PASS (host source fixture only)')
print('missing broker code/personality and prepared direct-host personality refused before mutation PASS')
