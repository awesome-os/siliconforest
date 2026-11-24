# SPDX-License-Identifier: MIT
"""Verify providers from the owned processes' actual dyld image lists.

The external sampler reads TASK_DYLD_INFO. A short sample is diagnostic only;
do not enable it for benchmark or pacing measurements.
"""
import json
from pathlib import Path
import re
import subprocess


def inspect(sample, output, identities, parent_pid, expected_appkit, system_gpu=False, private_windowserver=None):
    if not sample or not sample.startswith('/private/var/db/sffx/'):
        raise ValueError('Provider inspection requires a private sampler')
    if (not expected_appkit.startswith('/private/var/db/sffx/') and expected_appkit not in [
            '/usr/local/lib/siliconforest/firefox/160-system1/appkit',
            '/System/Library/Frameworks/AppKit.framework']) or '..' in Path(expected_appkit).parts:
        raise ValueError('Expected AppKit must be a private prefix')
    result = dict(diagnostic_sampling_seconds=.05, expected_appkit=expected_appkit, processes=[])
    result['expected_system_gpu'] = system_gpu
    if private_windowserver is not None:
        path = Path(private_windowserver)
        if (not path.is_relative_to('/private/var/db/sffx') or '..' in path.parts or
                path.name != 'libWindowServer.dylib'):
            raise ValueError('Private WindowServer control requires an owned private library')
        result['private_windowserver_expected'] = private_windowserver
        result['canonical_cocoa_complete'] = False
    if system_gpu and expected_appkit != '/System/Library/Frameworks/AppKit.framework':
        raise ValueError('System GPU verification requires canonical Cocoa verification')
    for pid, start in sorted(identities.items()):
        current = subprocess.run(['ps', '-p', str(pid), '-o', 'lstart='], capture_output=True, text=True).stdout.strip()
        if current != start.strip():
            continue
        command = subprocess.run(['ps', '-p', str(pid), '-o', 'command='], capture_output=True, text=True).stdout
        gpu = 'gpu-helper.app/Contents/MacOS/Firefox GPU Helper' in command
        if pid != parent_pid and not gpu:
            continue
        destination = output.with_name('provider-images-%d.txt' % pid)
        sampled = subprocess.run([sample, str(pid), '.05', '10', '-file', str(destination)],
                                 capture_output=True, text=True, timeout=10)
        text = destination.read_text() if destination.exists() else ''
        images = text.partition('\nBinary Images:\n')[2].splitlines()
        selected = [line.strip() for line in images if re.search(
            r'\s(?:AppKit|Onyx2D|CoreText|QuartzCore(?:\.pd)?|OpenGL|IOSurface|libGL\.1\.dylib|libWindowServer\.dylib|AVFoundation(?:\.pd)?)\s+<', line)]
        appkit = [line.rsplit('> ', 1)[-1] for line in selected if re.search(r'\sAppKit\s+<', line)]
        row=dict(pid=pid, role='gpu' if gpu else 'parent', status=sampled.returncode,
            images=selected, appkit=appkit, passed=sampled.returncode == 0 and len(appkit) == 1 and
            appkit[0].startswith(expected_appkit.rstrip('/') + '/'))
        if private_windowserver is not None:
            paths = [line.rsplit('> ', 1)[-1] for line in selected
                     if re.search(r'\slibWindowServer\.dylib\s+<', line)]
            row['private_windowserver'] = paths
            row['passed'] &= paths == [private_windowserver]
        if expected_appkit == '/System/Library/Frameworks/AppKit.framework':
            required = {name: '/System/Library/Frameworks/' + name + '.framework/'
                        for name in ['AppKit', 'Onyx2D', 'CoreText', 'QuartzCore']}
            required['libWindowServer.dylib'] = private_windowserver or '/usr/lib/libWindowServer.dylib'
            if system_gpu:
                for name in ['IOSurface', 'OpenGL']:
                    required[name] = '/System/Library/Frameworks/' + name + '.framework/'
            actual = {}
            for name, prefix in required.items():
                paths = [line.rsplit('> ', 1)[-1] for line in selected
                         if re.search(r'\s' + re.escape(name) + r'\s+<', line)]
                actual[name] = paths
                row['passed'] &= len(paths) == 1 and (paths[0] == prefix if name == 'libWindowServer.dylib'
                                                     else paths[0].startswith(prefix))
            pd = [line.rsplit('> ', 1)[-1] for line in selected if re.search(r'\sQuartzCore\.pd\s+<', line)]
            row['passed'] &= all(path.startswith('/System/Library/Frameworks/QuartzCore.framework/') for path in pd)
            row['ui_providers' if private_windowserver else 'system_providers'] = actual
        result['processes'].append(row)
    result['complete'] = bool(any(row['role'] == 'parent' for row in result['processes']) and
        any(row['role'] == 'gpu' for row in result['processes']) and all(row['passed'] for row in result['processes']))
    output.write_text(json.dumps(result, indent=2) + '\n')
    return result
