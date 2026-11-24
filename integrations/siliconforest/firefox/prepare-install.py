#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare a reviewable Firefox runtime in a NEW private prefix.

Default prints the plan. --build assembles a host tree; put.py performs the
separate cold-verified transfer. Never writes system files, dock pins/defaults,
or Firefox's original bundle. Runtime qualification follows transfer.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import re
import shutil
import subprocess

REPO = Path(__file__).resolve().parents[3]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--release', required=True, help='new /private/var/db/sffx/<release> target')
    ap.add_argument('--acquisition', type=Path, required=True, help='verified acquire.py output directory')
    ap.add_argument('--output', type=Path, required=True, help='new ignored host staging directory')
    ap.add_argument('--nix', default=str(Path.home() / '.nix-profile/bin/nix'))
    ap.add_argument('--build', action='store_true')
    ap.add_argument('--system-layout', action='store_true', help='prepare a new versioned system runtime and /Applications bundle')
    ap.add_argument('--gpu-presentation', action='store_true')
    ap.add_argument('--gpu-dirty-frames', action='store_true')
    ap.add_argument('--gpu-export-cache', action='store_true')
    ap.add_argument('--gpu-coalesce-ticks', action='store_true')
    ap.add_argument('--gpu-context-cleanup', action='store_true')
    ap.add_argument('--font-quality', action='store_true', help='private fontconfig and face-preserving CoreText')
    ap.add_argument('--provider-overrides', type=Path,
                    help='JSON map of provider names to retained, qualified Nix output directories')
    args = ap.parse_args()
    if (args.gpu_dirty_frames or args.gpu_export_cache or args.gpu_coalesce_ticks or args.gpu_context_cleanup) and not args.gpu_presentation:
        raise ValueError('GPU caches require GPU presentation')
    release = PurePosixPath(args.release)
    parent = '/usr/local/lib/siliconforest/firefox' if args.system_layout else '/private/var/db/sffx'
    if release.parent != PurePosixPath(parent) or not release.name.replace('-', '').isalnum():
        raise ValueError('Expected one new simple release directory under ' + parent)
    acquisition = args.acquisition.resolve()
    bundle = acquisition / 'unpacked/Firefox/Firefox.app'
    info = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
    version = info['CFBundleShortVersionString']
    checksum_line = (acquisition / 'VERIFIED.sha512').read_text().split()
    with (acquisition / checksum_line[1]).open('rb') as stream:
        if hashlib.file_digest(stream, 'sha512').hexdigest() != checksum_line[0]:
            raise RuntimeError('Acquired DMG changed')
    sums = (acquisition / 'SHA512SUMS').read_text().splitlines()
    published = [line.split(None, 1)[0] for line in sums
                 if line.split(None, 1)[1].strip() == f'mac/en-US/Firefox {version}.dmg']
    if published != [checksum_line[0]]:
        raise RuntimeError('DMG does not match the retained Mozilla checksum')
    original = json.loads((acquisition / 'bundle-sha256.json').read_text())
    actual = {}
    for path in sorted(bundle.rglob('*')):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                actual[str(path.relative_to(bundle))] = hashlib.file_digest(stream, 'sha256').hexdigest()
    if actual != original:
        raise RuntimeError('Official Firefox bundle differs from the acquired pristine files')
    recipes = {
        'gl': 'import ./cgl.nix {prefix=' + json.dumps(str(release / 'gl')) + ';}',
        'sandbox': 'import ./sandbox.nix {prefix=' + json.dumps(str(release / 'sandbox')) + ';coldAVDFirmware=true;}',
        'appkit': 'import ./appkit.nix', 'onyx': 'import ./onyx.nix',
        'quartz': 'import ./quartz-lifetime.nix', 'surface': 'import ./surface-session.nix',
        'avfoundation': 'import ./avfoundation.nix', 'compat': 'import ./compat.nix {}',
        'io': 'import ./io-policy.nix', 'open': 'import ./open-url.nix',
    }
    if args.gpu_presentation:
        recipes.update(gl='import ./cgl-gpu.nix {prefix=' + json.dumps(str(release / 'gl')) + ';}',
            surface='import ./surface-gpu.nix', quartz='import ./quartzcore.nix {gpuPresentation=true;}',
            windowserver='import ./windowserver-ui.nix',
            appkit='import ./appkit-ui.nix {menuGeometry=true;preciseScroll=true;gpuPresentation=true;pasteboardProvider=true;}')
        if args.gpu_context_cleanup or args.gpu_coalesce_ticks:
            recipes['quartz'] = ('import ./quartzcore.nix {gpuPresentation=true;releaseContextResources='
                + str(args.gpu_context_cleanup).lower() + ';coalesceNotifications='
                + str(args.gpu_coalesce_ticks).lower() + ';}')
    if args.font_quality:
        recipes.update(fonts='import ./fontconfig.nix {prefix=' + json.dumps(str(release / 'fonts')) + ';}',
            coretext='import ./coretext.nix', onyx='import ./onyx-font.nix')
        recipes['sandbox'] = 'import ./sandbox.nix {prefix=' + json.dumps(str(release / 'sandbox')) + ';coldAVDFirmware=true;fontDirectory=' + json.dumps(str(release / 'fonts')) + ';}'
    overrides = json.loads(args.provider_overrides.read_text()) if args.provider_overrides else {}
    if not isinstance(overrides, dict) or set(overrides) - set(recipes):
        raise ValueError('Provider overrides must name providers in the plan')
    if set(overrides) & {'gl', 'sandbox', 'fonts'}:
        raise ValueError('CGL, sandbox and font configuration must be built for this exact target prefix')
    for name, value in overrides.items():
        if not isinstance(value, str) or not re.fullmatch(r'/nix/store/[a-z0-9]{32}-[^/]+', value):
            raise ValueError('Expected a direct Nix output directory for ' + name)
        if not Path(value).is_dir():
            raise FileNotFoundError(value)
    manifest = dict(version=version, target=str(release), official_bundle_files=len(actual),
                    system_layout=args.system_layout,
                    mozilla_sha512=checksum_line[0], recipes=recipes,
                    state='candidate: requires qualification at this exact prefix',
                    installed_media=True, skip_unchanged_transactions=False, provider_overrides=overrides,
                    gpu_presentation=args.gpu_presentation, gpu_vector_compare=args.gpu_presentation,
                    gpu_cache_lookups=args.gpu_presentation, gpu_dirty_frames=args.gpu_dirty_frames,
                    gpu_export_cache=args.gpu_export_cache,
                    gpu_coalesce_ticks=args.gpu_coalesce_ticks,
                    gpu_context_cleanup=args.gpu_context_cleanup,
                    private_fonts=args.font_quality, precise_scroll=args.gpu_presentation,
                    known_limits=['video NV12 upload remains; no end-to-end zero-copy claim',
                                  'this exact prefix requires launcher, URL and quality qualification',
                                  'physical audio/A-V/trackpad unverified',
                                  'private bundle Command-W intercepted by desktop until catalogued',
                                  'compositor-clock 60fps and GPU-path sustained browsing require qualification',
                                  'software H264/HEVC callback fix is separate experimental candidate'])
    if not args.build:
        print(json.dumps(manifest, indent=2))
        return
    ignored = REPO / 'private'
    if not args.output.resolve().is_relative_to(ignored) and not args.output.resolve().is_relative_to(REPO / 'runs'):
        raise ValueError('Staging output must be under ignored private/ or runs/')
    args.output.mkdir(parents=True, exist_ok=False)
    builds = {}
    for name, expression in recipes.items():
        if name in overrides:
            source = Path(overrides[name])
            (args.output / (name + '.build.log')).write_text('Retained provider: ' + str(source) + '\n')
        else:
            result = subprocess.run([args.nix, 'build', '--impure', '--no-link', '--cores', '4',
                '--max-jobs', '1', '--print-out-paths', '--extra-experimental-features', 'nix-command flakes',
                '--expr', expression], cwd=Path(__file__).resolve().parent, capture_output=True, text=True)
            (args.output / (name + '.build.log')).write_text(result.stdout + result.stderr)
            result.check_returncode()
            source = Path(result.stdout.strip().splitlines()[-1])
        builds[name] = str(source)
        if name in ['appkit', 'onyx', 'coretext']:
            shutil.copytree(source / 'System/Library/Frameworks', args.output / name / 'Frameworks', symlinks=True)
        else:
            shutil.copytree(source, args.output / name, symlinks=True)
    # Firefox's unchanged load command uses C, while our AppKit provider uses A.
    # Retained provider overrides need the alias too; otherwise dyld can silently
    # select the installed framework and invalidate a supposedly private trial.
    versions = args.output / 'appkit/Frameworks/AppKit.framework/Versions'
    if not (versions / 'A/AppKit').is_file():
        raise RuntimeError('Selected AppKit has no expected A implementation')
    alias = versions / 'C'
    if not alias.exists():
        versions.chmod(versions.stat().st_mode | 0o200)
        alias.symlink_to('A')
    if alias.resolve() != (versions / 'A').resolve():
        raise RuntimeError('Unexpected AppKit C implementation')
    manifest['framework_aliases'] = {'AppKit.framework/Versions/C': 'A'}
    shutil.copytree(bundle, args.output / 'original/Firefox.app', symlinks=True)
    outer = args.output / 'Firefox.app/Contents'
    (outer / 'MacOS').mkdir(parents=True)
    (outer / 'Resources').mkdir()
    shutil.copy2(Path(__file__).with_name('daily-launch.py'), outer / 'MacOS/firefox-sf')
    (outer / 'MacOS/firefox-sf').chmod(0o755)
    icon = info.get('CFBundleIconFile', 'firefox.icns')
    if not icon.endswith('.icns'):
        icon += '.icns'
    if not (bundle / 'Contents/Resources' / icon).is_file():
        raise RuntimeError('Official Firefox icon missing')
    (outer / 'Resources' / icon).symlink_to('../../../original/Firefox.app/Contents/Resources/' + icon)
    outer_info = {key: info[key] for key in ['CFBundleIdentifier', 'CFBundleName', 'CFBundleShortVersionString',
                                          'CFBundleVersion', 'CFBundleURLTypes', 'CFBundleDocumentTypes'] if key in info}
    outer_info.update(CFBundleExecutable='firefox-sf', CFBundlePackageType='APPL', CFBundleIconFile=icon)
    (outer / 'Info.plist').write_bytes(plistlib.dumps(outer_info))
    if args.system_layout:
        manifest['original_bundle'] = '/Applications/Firefox.app/Contents/Resources/Mozilla.app'
        (outer / 'Resources' / icon).unlink()
        (outer / 'Resources' / icon).symlink_to('Mozilla.app/Contents/Resources/' + icon)
        (outer / 'MacOS/system-runtime.json').write_text(json.dumps(dict(target=str(release))) + '\n')
    manifest['builds'] = builds
    (args.output / 'release.json').write_text(json.dumps(manifest, indent=2) + '\n')
    desktop = Path(__file__).with_name('org.mozilla.firefox.desktop').read_text()
    if args.system_layout:
        desktop = desktop.replace('TryExec=/Applications/Firefox.app/Contents/MacOS/firefox',
                                  'TryExec=/Applications/Firefox.app/Contents/MacOS/firefox-sf')
    else:
        desktop = desktop.replace('TryExec=/Applications/Firefox.app/Contents/MacOS/firefox',
                              'TryExec=' + str(release / 'Firefox.app/Contents/MacOS/firefox-sf'))
        desktop = desktop.replace('Exec=/usr/bin/chonkstep-darwin-desktop --desktop-request application org.mozilla.firefox',
                              'Exec=' + str(release / 'Firefox.app/Contents/MacOS/firefox-sf') + ' %U')
        desktop = desktop.replace('Icon=/Applications/Firefox.app', 'Icon=' + str(release / 'original/Firefox.app'))
    (args.output / 'org.mozilla.firefox.desktop').write_text(desktop)
    shutil.copy2(Path(__file__).with_name('owner-install.py'), args.output / 'owner-install.py')
    if args.system_layout:
        for name in ['install-system.py', 'uninstall.py']:
            shutil.copy2(Path(__file__).with_name(name), args.output / name)
    (args.output / 'OWNER-INSTALL.txt').write_text(
        'Candidate only. Qualify this exact prefix before daily activation.\n'
        'The original Mozilla bundle is unchanged; the outer launcher supplies private providers.\n'
        'Review the concrete plan: python3 ' + str(release / 'owner-install.py') + ' --release ' + str(release) + '\n'
        'After launcher/default-URL qualification, provide its qualification JSON and add --apply.\n'
        'The work agent must not execute --apply against owner paths.\n'
        'Owner-managed integration after qualification:\n'
        '1. Add /Applications/Firefox.app pointing to ' + str(release / 'Firefox.app') + ' only if absent.\n'
        '2. Add the generated desktop entry and append its id to the existing dock pins, preserving them.\n'
        '3. Merge/qualify the open(1) HTTP(S) patch through TERM; choose org.mozilla.firefox for http/https.\n'
        '4. Test first and running-instance open URLs; keep previous defaults/pins for rollback.\n'
        'No installer action here changes any of these owner files.\n')
    if args.system_layout:
        (args.output/'OWNER-INSTALL.txt').write_text(
            'System-layout installation, explicit owner authorization required.\n'
            'After put.py cold-verifies a NEW staging path, hold gui.lock and run:\n'
            'python3 <staging>/install-system.py --source <staging> --apply\n'
            'Then qualify compositor application launch, visible browsing and same-parent URL routing.\n'
            'Uninstall after quitting Firefox (profiles and logs are preserved):\n'
            'python3 '+str(release)+'/uninstall.py --runtime '+str(release)+' --apply\n'
            'Canonical shared framework and open(1) adoption remain coordinated with their owners.\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
