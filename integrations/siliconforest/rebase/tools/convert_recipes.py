#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Point the lab's recipes at rebase/pd.nix instead of a revision written into each of them.

Usage: convert_recipes.py [--root <integrations/siliconforest>] [--write]

Rewrites, in every live recipe, the expression
    builtins.getFlake "git+file:///home/chrisk/src/darwin-lab/sources/puredarwin-baseline?submodules=1&shallow=1&rev=<pin>"
(the revision written out, or as "${pin}" next to a `pin = "<pin>";` binding) into
    (import <relative path>/rebase/pd.nix).pd
and nothing else. Without --write it only lists what it would change. Not touched: frozen
history (revisions/, attempts/, implementation-revisions/, host-revision*/, source/), the
directories of other streams (cocoa, qemu-dev, homebrew, libsystem-default, m1-native-boot)
and the three recipes of SELF_INPUT below; all of these keep naming the pin. Running it
again changes nothing.
"""
import os
import re
import sys
from pathlib import Path

PIN = 'e7aa00a60f0b56ef35aa24252e81467787bf71ea'
URL = 'git+file:///home/chrisk/src/darwin-lab/sources/puredarwin-baseline?submodules=1&shallow=1&rev='
CALL = re.compile(r'builtins\.getFlake "' + re.escape(URL) + r'(?:' + PIN + r'|\$\{pin\})"')
FROZEN = ('revisions', 'attempts', 'implementation-revisions', 'source', 'build-recipes')
OTHERS = ('cocoa', 'qemu-dev', 'homebrew', 'libsystem-default', 'm1-native-boot', 'rebase')
# Recipes whose own text is an input of a build: converting them would change what the pin
# builds, so they keep naming the pin. None is on the path of a current collection or RAM root.
#   m1-keyboard/build-kext.nix          its directory, recipe included, is the kext's source
#   m1-network-default-route/build.nix  the network guests take that whole directory as an
#                                       include path (-I${../m1-network-default-route})
#   m1-apcie-bridge/root-build/build-private-policy-objects-001.nix
#                                       build-private-probe-bundle-001.nix checks this file's hash
SELF_INPUT = (
    'm1-keyboard/build-kext.nix',
    'm1-network-default-route/build.nix',
    'm1-apcie-bridge/root-build/build-private-policy-objects-001.nix',
)


def main(argv):
    write = '--write' in argv
    root = Path(argv[argv.index('--root') + 1]) if '--root' in argv else Path(__file__).resolve().parents[2]
    changed = skipped = 0
    for path in sorted(root.rglob('*.nix')):
        parts = path.relative_to(root).parts
        if parts[0] in OTHERS or any(p in FROZEN or p.startswith('host-revision') or p.startswith('host_revision') for p in parts):
            continue
        text = path.read_text()
        if not CALL.search(text):
            continue
        if str(path.relative_to(root)) in SELF_INPUT:
            print('kept (its text is a build input): %s' % path.relative_to(root)); skipped += 1
            continue
        if '${pin}' in CALL.search(text).group(0) and not re.search(r'\bpin = "' + PIN + '";', text):
            print('SKIP (pin bound elsewhere): %s' % path.relative_to(root)); skipped += 1
            continue
        relative = os.path.relpath(root / 'rebase' / 'pd.nix', path.parent)
        if not relative.startswith('.'):
            relative = './' + relative
        new = CALL.sub('(import %s).pd' % relative, text)
        changed += 1
        print('%s %s' % ('rewrote' if write else 'would rewrite', path.relative_to(root)))
        if write:
            path.write_text(new)
    print('%d recipes%s, %d skipped' % (changed, '' if write else ' (dry run)', skipped))


if __name__ == '__main__':
    main(sys.argv[1:])
