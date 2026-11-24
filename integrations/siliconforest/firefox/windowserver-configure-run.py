#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Private canonical-source configure-race comparison; never owner activation.

Caller holds gui.lock and verifies a fresh coordination gate/cold payload.
Automatic popup closure is recorded as a usability FAILURE even when all
protocol-stress cycles survive. Actual provider paths are sampled in both roles.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tools', type=Path, required=True)
    ap.add_argument('--run', type=Path, required=True)
    ap.add_argument('--private-library', type=Path)
    args = ap.parse_args()
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Target-only diagnostic')
    for path in [args.tools, args.run, args.private_library]:
        if path is not None and (not path.is_absolute() or '..' in path.parts or
                                 not path.is_relative_to('/private/var/db/sffx')):
            raise ValueError('Explicit owned private diagnostic paths required')
    sys.path.insert(0, str(args.tools))
    def module(name, filename):
        spec = importlib.util.spec_from_file_location(name, args.tools / filename)
        result = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(result)
        return result
    root = Path('/usr/local/lib/siliconforest/firefox/160-system1')
    manifest = json.loads((root / 'release.json').read_text())
    if manifest.get('system_ui') is not True:
        raise ValueError('Compare against the installed canonical UI cohort')
    command, env = module('daily', 'daily-launch.py').plan(root,
        ['--no-remote', '--profile', str(args.run / 'profile')], manifest=manifest)
    if args.private_library:
        if args.private_library.name != 'libWindowServer.dylib' or not args.private_library.is_file():
            raise ValueError('Expected the cold private WindowServer image')
        env['DYLD_LIBRARY_PATH'] = str(args.private_library.parent) + ':' + env['DYLD_LIBRARY_PATH']
    env['SFFX_WS_CONFIGURE_TRACE'] = '1'
    env.pop('SF_WS_SURVIVE_DISCONNECT', None)
    os.environ.update(env)
    selection = dict(installed_outer_or_dock=False, owner_policy_changed=False,
                     launcher_kind='installed-payload-private-protocol-control',
                     private_windowserver=str(args.private_library) if args.private_library else None,
                     library_path=env['DYLD_LIBRARY_PATH'], menu_usability_qualified=False)
    options = ['remote-run.py', '--bundle', manifest['original_bundle'], '--run', str(args.run),
        '--seconds', '95', '--functional-qa', '--page-load-strategy', 'none', '--quiet-trace',
        '--presentation-seat', '--popup-stress', '--popup-observe-auto-close',
        '--sample-path', '/private/var/db/sffx/probes-v3/sf-sample',
        '--expect-appkit-prefix', '/System/Library/Frameworks/AppKit.framework',
        '--framework-path', env['DYLD_FRAMEWORK_PATH'], '--library-path', env['DYLD_LIBRARY_PATH'],
        '--insert-library', env['DYLD_INSERT_LIBRARIES'], '--font-config', env['FONTCONFIG_FILE'],
        '--surface-session', command[0], '--surface-broker', command[1],
        '--pref', 'termsofuse.bypassNotification=true', '--pref', 'webgl.disable-angle=true',
        '--pref', 'browser.tabs.inTitlebar=0', '--url', 'about:support']
    if args.private_library:
        options += ['--expect-private-windowserver', str(args.private_library)]
    for field, flag in [('gpu_presentation', '--gpu-presentation'), ('gpu_presentation', '--gpu-surfaces'),
                        ('gpu_vector_compare', '--gpu-vector-compare'), ('gpu_cache_lookups', '--gpu-cache-lookups'),
                        ('gpu_dirty_frames', '--gpu-dirty-frames'), ('gpu_export_cache', '--gpu-export-cache')]:
        if manifest.get(field):
            options.append(flag)
    print(json.dumps(selection), flush=True)
    sys.argv = options
    status = module('remote', 'remote-run.py').main()
    (args.run / 'protocol-control.json').write_text(json.dumps(selection, indent=2) + '\n')
    return status


if __name__ == '__main__':
    raise SystemExit(main())
