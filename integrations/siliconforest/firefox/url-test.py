#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Target-only open(1) URL dispatch checks using a windowless argv bundle.

All applications, defaults, logs and output are private to this test home.
No browser/network/window is started and the owner's default browser is untouched.
"""
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import time


def main():
    root = Path(__file__).resolve().parent
    home = root / 'home'
    home.mkdir(exist_ok=False)
    bundle = home / 'Applications/URL Probe.app'
    executable = bundle / 'Contents/MacOS/url-argv-probe'
    executable.parent.mkdir(parents=True)
    # The fixture binary was cold-verified at the prefix before this copy.
    shutil.copy2(root / 'url-argv-probe', executable)
    identifier = 'org.siliconforest.fx-url-probe'
    (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps(dict(
        CFBundleIdentifier=identifier, CFBundleExecutable='url-argv-probe',
        CFBundleName='URL Probe', CFBundlePackageType='APPL',
        CFBundleURLTypes=[dict(CFBundleURLSchemes=['http', 'https'])])))
    preferences = home / 'Library/Preferences'
    preferences.mkdir(parents=True)
    (preferences / 'com.apple.LaunchServices.plist').write_bytes(plistlib.dumps(dict(
        LSHandlers=[dict(LSHandlerURLScheme=scheme, LSHandlerRoleAll=identifier)
                    for scheme in ['http', 'https']])))
    env = dict(os.environ, HOME=str(home), CFFIXED_USER_HOME=str(home),
               XDG_RUNTIME_DIR='/private/var/run/gpu-desktop', WAYLAND_DISPLAY='wayland-0')
    results = []

    def check(name, args, expected):
        output = root / (name + '.json')
        result = subprocess.run([str(root / 'open-url')] + args, env=dict(env, SFFX_ARGV_OUTPUT=str(output)),
                                capture_output=True, text=True, timeout=5)
        deadline = time.monotonic() + 3
        while not output.exists() and time.monotonic() < deadline:
            time.sleep(.025)
        actual = json.loads(output.read_text()) if output.exists() else None
        record = dict(name=name, status=result.returncode, stderr=result.stderr, actual=actual,
                      expected=expected, passed=result.returncode == 0 and actual is not None
                      and actual['argv'][1:] == expected)
        results.append(record)
        return record['passed']

    normal = 'https://example.invalid/path?q=a%20b&x=one;two'
    check('default-https', [normal], [normal])
    urls = ['http://example.invalid/one', 'https://example.invalid/two']
    check('default-multiple', urls, urls)
    literal = 'https://example.invalid/?literal=$(touch ' + str(root / 'unexpected-shell') + '); tail'
    check('explicit-literal-argv', ['-a', str(bundle), literal], [literal])
    document = root / 'a; document.txt'
    document.write_text('owned document fixture\n')
    check('document-control', ['-a', str(bundle), str(document)], ['-NSOpen', str(document)])
    shell_safe = not (root / 'unexpected-shell').exists()
    report = dict(tests=results, shell_safe=shell_safe,
                  passed=all(r['passed'] for r in results) and shell_safe)
    (root / 'url-tests.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
