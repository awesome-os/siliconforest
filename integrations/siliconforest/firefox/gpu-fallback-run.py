#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Finite isolated presentation controls using the installed Mozilla payload.

Caller holds gui.lock and cold-verifies this driver and its tools. These are
explicit runtime-plan controls, separate from the installed outer/dock path.
No installed manifest, owner profile or existing process is changed.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--mode', choices=['full', 'dirty-off', 'export-off', 'presentation-off', 'cpu'], required=True)
    parser.add_argument('--software', action='store_true')
    parser.add_argument('--kill-first-gpu', action='store_true')
    parser.add_argument('--provider-control', choices=['current', 'system-gpu', 'system-gpu-private-gl-api',
                        'system-surface-private-gl', 'system-gl-private-surface', 'nv12-cohort',
                        'system-surface-candidate-gl'], default='current')
    parser.add_argument('--fixture-reference', action='store_true')
    args = parser.parse_args()
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Target-only diagnostic')
    for path in [args.tools, args.run]:
        if not path.is_absolute() or '..' in path.parts or not path.is_relative_to('/private/var/db/sffx'):
            raise ValueError('Expected an owned private target path')
    if args.software and args.kill_first_gpu:
        raise ValueError('Separate forced software and GPU-failure controls')
    sys.path.insert(0, str(args.tools))
    def module(name, file):
        spec = importlib.util.spec_from_file_location(name, args.tools / file)
        result = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(result)
        return result
    root = Path('/usr/local/lib/siliconforest/firefox/160-system1')
    manifest = json.loads((root / 'release.json').read_text())
    if args.provider_control != 'current':
        manifest['system_ui'] = True
        manifest['system_gpu_providers'] = True
        manifest.pop('graphics_override', None)
    if args.provider_control == 'nv12-cohort':
        manifest['system_gpu_providers'] = False
        manifest['graphics_override'] = str(root / 'overrides/nv12-v1')
    original = Path(manifest['original_bundle'])
    if original != Path('/Applications/Firefox.app/Contents/Resources/Mozilla.app'):
        raise ValueError('Unexpected installed Mozilla payload')
    planned, environment = module('daily', 'daily-launch.py').plan(root,
        ['--no-remote', '--profile', str(args.run / 'profile')], manifest=manifest)
    if args.provider_control in ['system-gpu-private-gl-api', 'system-surface-private-gl']:
        environment['DYLD_LIBRARY_PATH'] = str(root / 'gl/lib') + ':' + environment['DYLD_LIBRARY_PATH']
    if args.provider_control == 'system-surface-private-gl':
        environment['DYLD_FRAMEWORK_PATH'] = str(root / 'gl/Frameworks') + ':' + environment['DYLD_FRAMEWORK_PATH']
    if args.provider_control == 'system-surface-candidate-gl':
        # Cold staged only: qualify our canonical-prefix CGL/API against the
        # real SYSTEM IOSurface and broker without selecting it for the owner.
        gl = Path('/private/var/db/sffx/canonical-nv12-v1/gl')
        if not (gl / 'Frameworks/OpenGL.framework/Versions/A/OpenGL').is_file():
            raise ValueError('Cold candidate CGL must be staged before this control')
        environment['DYLD_FRAMEWORK_PATH'] = str(gl / 'Frameworks') + ':' + environment['DYLD_FRAMEWORK_PATH']
        environment['DYLD_LIBRARY_PATH'] = str(gl / 'lib') + ':' + environment['DYLD_LIBRARY_PATH']
    if args.provider_control == 'system-gl-private-surface':
        environment['DYLD_FRAMEWORK_PATH'] = str(root / 'overrides/surface-lookup-v1/Frameworks') + ':' + environment['DYLD_FRAMEWORK_PATH']
    # Keep the same provider paths in every mode. In particular, disabling
    # presentation must not silently select the canonical WindowServer too.
    controls = dict(presentation=manifest.get('gpu_presentation', False) and args.mode not in ['presentation-off', 'cpu'],
                    storage=manifest.get('gpu_presentation', False) and args.mode != 'cpu',
                    dirty=manifest.get('gpu_dirty_frames', False) and args.mode not in ['dirty-off', 'cpu'],
                    export=manifest.get('gpu_export_cache', False) and args.mode not in ['export-off', 'cpu'])
    for field, name in [('presentation', 'SFFX_GPU_PRESENT'), ('storage', 'SFFX_IOSURFACE_GPU'),
                        ('dirty', 'SFFX_GPU_DIRTY_FRAMES'), ('export', 'SFFX_GPU_EXPORT_CACHE')]:
        if controls[field]:
            environment[name] = '1'
        else:
            environment.pop(name, None)
    os.environ.update(environment)
    options = ['remote-run.py', '--bundle', str(original), '--run', str(args.run),
        '--seconds', '60', '--functional-qa', '--page-load-strategy', 'none', '--quiet-trace',
        '--capture-renderer', '--capture-pages', '--presentation-seat',
        '--sample-parent', '--sample-path', '/private/var/db/sffx/probes-v3/sf-sample',
        '--expect-appkit-prefix', '/System/Library/Frameworks/AppKit.framework',
        '--framework-path', environment['DYLD_FRAMEWORK_PATH'],
        '--library-path', environment['DYLD_LIBRARY_PATH'],
        '--insert-library', environment['DYLD_INSERT_LIBRARIES'],
        '--font-config', environment['FONTCONFIG_FILE'],
        '--surface-session', str(root / 'surface/bin/surface-session'),
        '--surface-broker', planned[1],
        '--pref', 'webgl.disable-angle=true', '--pref', 'browser.tabs.inTitlebar=0',
        '--pref', 'termsofuse.bypassNotification=true',
        '--url', (args.tools / 'test-page.html').as_uri() if args.fixture_reference else 'https://example.com/']
    if args.fixture_reference:
        options += ['--scene-reference', '--finish-after-scene-reference']
    else:
        options += ['--site-legibility']
    for enabled, option in [(controls['presentation'], '--gpu-presentation'),
            (controls['storage'], '--gpu-surfaces'), (manifest.get('gpu_vector_compare', False), '--gpu-vector-compare'),
            (manifest.get('gpu_cache_lookups', False), '--gpu-cache-lookups'), (controls['dirty'], '--gpu-dirty-frames'),
            (controls['export'], '--gpu-export-cache')]:
        if enabled:
            options.append(option)
    if args.software:
        options += ['--software']
    if args.kill_first_gpu:
        options += ['--gpu-startup-fault', 'kill', '--pref', 'layers.gpu-process.max_restarts=0',
                    '--pref', 'layers.gpu-process.max_launch_attempts=0']
    selection = dict(mode=args.mode, forced_software=args.software, kill_first_gpu=args.kill_first_gpu,
                     provider_control=args.provider_control, fixture_reference=args.fixture_reference,
                     framework_path=environment['DYLD_FRAMEWORK_PATH'], library_path=environment['DYLD_LIBRARY_PATH'],
                     launcher_kind='installed-payload-runtime-plan-control', installed_outer_or_dock=False,
                     controls=controls,
                     environment_controls={k: v for k, v in environment.items() if k.startswith('SFFX_')})
    print(json.dumps(selection), flush=True)
    sys.argv = options
    status = module('remote', 'remote-run.py').main()
    (args.run / 'fallback-control.json').write_text(json.dumps(selection, indent=2) + '\n')
    return status


if __name__ == '__main__':
    raise SystemExit(main())
