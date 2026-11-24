#!/usr/bin/env python3
"""Read bindings and replay only into temporary copies. No build or device."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    m = json.loads((HERE/'manifest.json').read_text())
    for name, expected in m['owned_files_sha256'].items():
        assert sha(HERE/name) == expected, name
    for patches in [('candidate.patch',), ('iohid-publication.patch', 'iohid-attachment.patch')]:
        with tempfile.TemporaryDirectory(prefix='native-input-replay-') as directory:
            tree = Path(directory)/'tree'
            shutil.copytree(HERE/'before', tree)
            for patch in patches:
                subprocess.run(['git', 'apply', '--check', str(HERE/patch)], cwd=tree, check=True)
                subprocess.run(['git', 'apply', str(HERE/patch)], cwd=tree, check=True)
            for name, binding in m['files'].items():
                assert sha(tree/name) == binding['after_sha256'], name
    checkout = LAB/'sources/puredarwin-m1-desktop'
    if checkout.is_dir():
        for name, binding in m['files'].items():
            if name.startswith('src/Kernel/Extensions/IOHIDFamily/'):
                assert sha(checkout/name) == binding['before_sha256'], name
                raw = subprocess.check_output(['git', 'show', m['puredarwin_pin']+':'+name], cwd=checkout)
                assert hashlib.sha256(raw).hexdigest() == binding['before_sha256'], name
    receipt = LAB/m['native005_receipt']
    if receipt.exists():
        assert sha(receipt) == m['native005_receipt_sha256']
        sdk = json.loads((LAB/'reports/sprint-010/keyboard-kext-build-005.json').read_text())
        for name, item in sdk['source_fidelity'].items():
            assert sha(LAB/'integrations/puredarwin/m1-keyboard'/name) == item['expected_sha256'], name
    print('PASS: hashes, split/combined replay and available frozen sources; no SDK or hardware')


if __name__ == '__main__':
    main()
