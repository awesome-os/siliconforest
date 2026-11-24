#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Launch the untouched Firefox bundle with this private release's providers."""
import json
import os
from pathlib import Path
import socket
import tempfile
import sys
import time


# This block owns one platform workaround, never the rest of an owner's file.
COMPOSITOR_BEGIN = '// BEGIN SiliconForest FX-011/012 compositor preference\n'
COMPOSITOR_END = '// END SiliconForest FX-011/012 compositor preference\n'
COMPOSITOR_KEY = 'gfx.webrender.compositor'
PLATFORM_BEGIN = '// BEGIN SiliconForest window and GL defaults\n'
PLATFORM_END = '// END SiliconForest window and GL defaults\n'
CODEC_BEGIN = '// BEGIN SiliconForest hardware video preference\n'
CODEC_END = '// END SiliconForest hardware video preference\n'


def av1_preference(manifest, environment):
    value = manifest.get('av1_enabled')
    if value is not None and type(value) is not bool:
        raise ValueError('av1_enabled must be a boolean when selected')
    override = environment.get('SFFX_AV1')
    if override is not None:
        if override not in ['0', '1']:
            raise ValueError('SFFX_AV1 must be 0 or 1')
        value = override == '1'
    return value


def update_codec_preference(settings, value):
    # AV1 is software-only on this M1; VP9/H264/HEVC have real hardware
    # decoders. This reversible platform policy does not lower source quality.
    # Explicit user.js choices win. No policy removes only our old block.
    return update_managed_preferences(settings,
        {} if value is None else {'media.av1.enabled': value}, CODEC_BEGIN, CODEC_END)


def native_compositor_preference(manifest, environment):
    value = manifest.get('native_compositor')
    if value is not None and type(value) is not bool:
        raise ValueError('native_compositor must be a boolean when selected')
    override = environment.get('SFFX_NATIVE_COMPOSITOR')
    if override is not None:
        if override not in ['0', '1']:
            raise ValueError('SFFX_NATIVE_COMPOSITOR must be 0 or 1')
        value = override == '1'
    return value


def update_compositor_preference(settings, value):
    """Manage our marked block only; preserve explicit user choices and bytes."""
    if value is None:
        return 'unmanaged'
    if type(value) is not bool:
        raise ValueError('Expected a boolean compositor preference')
    return update_managed_preferences(settings, {COMPOSITOR_KEY: value},
                                      COMPOSITOR_BEGIN, COMPOSITOR_END)


def update_platform_preferences(settings):
    # Firefox's full-frame titlebar layout overlaps AppKit's in-window menu
    # strip under server decorations. ANGLE has no usable display backend here.
    # These defaults also apply when an existing user.js contains other keys.
    return update_managed_preferences(settings,
        {'browser.tabs.inTitlebar': 0, 'webgl.disable-angle': True},
        PLATFORM_BEGIN, PLATFORM_END)


def update_managed_preferences(settings, preferences, begin_marker, end_marker):
    """Keep explicit choices and unrelated bytes; own only the marked block."""
    if settings.is_symlink():
        raise ValueError('Refusing to replace a symlinked user.js')
    original = settings.read_bytes() if settings.exists() else b''
    begin, end = begin_marker.encode(), end_marker.encode()
    if original.count(begin) != original.count(end) or original.count(begin) > 1:
        raise ValueError('Malformed SiliconForest preference block; leave user.js intact')
    if begin in original:
        first, last = original.index(begin), original.index(end)
        if last < first:
            raise ValueError('Reversed SiliconForest preference markers')
        last += len(end)
        prefix, suffix = original[:first], original[last:]
    else:
        prefix, suffix = original, b''
    defaults = {key: value for key, value in preferences.items()
                if key.encode() not in prefix + suffix}
    if not defaults:
        # An explicit owner/test preference wins. Remove our previous block
        # too, or its later assignment could override that explicit choice.
        if begin not in original:
            return 'explicit-user-preference'
        updated = prefix + suffix
    else:
        if prefix and not prefix.endswith(b'\n'):
            prefix += b'\n'
        block = begin + ''.join('user_pref(%s,%s);\n' %
            (json.dumps(key), json.dumps(value)) for key, value in defaults.items()).encode() + end
        updated = prefix + block + suffix
    if updated == original:
        return 'unchanged'
    mode = settings.stat().st_mode & 0o777 if settings.exists() else 0o600
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=settings.parent, prefix='.sffx-user-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(updated)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(mode)
        if settings.is_symlink() or (settings.read_bytes() if settings.exists() else b'') != original:
            raise RuntimeError('user.js changed while preparing the platform preference')
        os.replace(temporary, settings)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return 'updated'


def selected_profile(args):
    for index,arg in enumerate(args):
        if arg in ['--profile','-profile'] and index+1<len(args):
            return Path(args[index+1]).expanduser().resolve()
        if arg.startswith('--profile='):
            return Path(arg.partition('=')[2]).expanduser().resolve()
    return None


def plan(root, args, manifest=None):
    args = list(args)
    # Preserve the caller's interpretation before changing to the bundle's
    # directory. A relative profile must never end up in the original bundle.
    for index, arg in enumerate(args):
        if arg in ['--profile', '-profile'] and index + 1 < len(args):
            args[index + 1] = str(Path(args[index + 1]).expanduser().resolve())
        elif arg.startswith('--profile='):
            args[index] = '--profile=' + str(Path(arg.partition('=')[2]).expanduser().resolve())
    if manifest is None:
        manifest = json.loads((root / 'release.json').read_text())
    native_compositor_preference(manifest, os.environ)  # validate even dry launch plans
    av1_preference(manifest, os.environ)
    diagnostic = manifest.get('diagnostics', {})
    if diagnostic:
        allowed = {'SF_WS_TRACE','SF_WS_TIMING','SF_CA_TRACE','SF_SANDBOX_TRACE','MOZ_REMOTE_ALLOW_SYSTEM_ACCESS'}
        if set(diagnostic.get('environment',{}))-allowed or diagnostic.get('args',[]) not in [[],['--marionette']]:
            raise ValueError('Unsupported temporary installation diagnostics')
        args = diagnostic.get('args',[]) + args
    if str(root) != manifest['target']:
        raise RuntimeError('Runtime moved: rebuild its CGL/sandbox paths for the new prefix')
    system_ui = manifest.get('system_ui', False)
    if type(system_ui) is not bool or (system_ui and not manifest.get('system_layout')):
        raise ValueError('System UI selection requires an installed system runtime')
    system_gpu = manifest.get('system_gpu_providers', False)
    if type(system_gpu) is not bool or (system_gpu and not system_ui):
        raise ValueError('System GPU providers require the canonical system UI cohort')
    damage = manifest.get('gpu_damage', False)
    if type(damage) is not bool or (damage and not (system_ui and system_gpu)):
        raise ValueError('GPU damage requires an explicit canonical UI/graphics runtime')
    image_cache = manifest.get('gpu_image_cache', False)
    if type(image_cache) is not bool or (image_cache and not (system_ui and system_gpu)):
        raise ValueError('GPU image cache requires an explicit canonical UI/graphics runtime')
    render_thread = manifest.get('gpu_render_thread', False)
    if type(render_thread) is not bool or (render_thread and not (
            system_ui and system_gpu and manifest.get('gpu_presentation') is True)):
        raise ValueError('GPU render thread requires canonical providers and GPU presentation')
    graphics = manifest.get('graphics_override')
    if graphics is not None:
        graphics = Path(graphics)
        if (not system_ui or system_gpu or '..' in graphics.parts or
                not graphics.is_relative_to(root / 'overrides') or graphics == root / 'overrides'):
            raise ValueError('Graphics cohort requires system UI and a versioned private runtime prefix')
    env = os.environ.copy()
    env['MOZ_CRASHREPORTER_DISABLE'] = '1'
    env.update(diagnostic.get('environment',{}))
    compositor = native_compositor_preference(manifest, env)
    if compositor is not None:
        env['SFFX_NATIVE_COMPOSITOR'] = '1' if compositor else '0'
    for field, name in [('gpu_presentation', 'SFFX_GPU_PRESENT'), ('gpu_presentation', 'SFFX_IOSURFACE_GPU'),
                        ('gpu_vector_compare', 'SFFX_GPU_VECTOR_COMPARE'), ('gpu_cache_lookups', 'SFFX_GPU_CACHE_LOOKUPS'),
                        ('gpu_dirty_frames', 'SFFX_GPU_DIRTY_FRAMES'), ('gpu_export_cache', 'SFFX_GPU_EXPORT_CACHE'),
                        ('gpu_coalesce_ticks', 'SFFX_GPU_COALESCE_TICKS'),
                        ('gpu_damage', 'SFFX_GPU_DAMAGE'),
                        ('gpu_image_cache', 'SFFX_GPU_IMAGE_CACHE'),
                        ('gpu_render_thread', 'SFFX_GPU_RENDER_THREAD'),
                        ('precise_scroll', 'SFFX_PRECISE_SCROLL')]:
        if manifest.get(field, False):
            env[name] = '1'
        else:
            env.pop(name, None)
    frames = [root / (name + '/Frameworks') for name in
              ['surface', 'avfoundation', 'appkit', 'quartz', 'onyx', 'gl']]
    frames.append(root / 'compat/System/Library/Frameworks')
    surface_framework = root / 'surface/Frameworks'
    if manifest.get('surface_override') is not None:
        surface_framework = Path(manifest['surface_override'])
        if (not manifest.get('system_layout') or '..' in surface_framework.parts or
                not surface_framework.is_relative_to(root / 'overrides') or surface_framework.name != 'Frameworks'):
            raise ValueError('IOSurface override must be a versioned provider inside this app runtime')
        frames[0] = surface_framework
    onyx = manifest.get('onyx_override')
    if onyx is not None:
        requested = Path(onyx)
        if (not manifest.get('system_layout') or '..' in requested.parts or
                not requested.is_relative_to(root / 'overrides') or requested.name != 'Frameworks'):
            raise ValueError('Onyx override must be a versioned provider inside this app runtime')
        frames[4] = requested
    ui = manifest.get('ui_override')
    if ui is not None:
        ui = Path(ui)
        if (not manifest.get('system_layout') or '..' in ui.parts or
                not ui.is_relative_to(root / 'overrides') or ui == root / 'overrides'):
            raise ValueError('UI override must be a versioned provider inside this app runtime')
        frames[2] = ui / 'appkit/Frameworks'
    coretext_source = 'system' if system_ui else manifest.get('coretext_source', 'private')
    if coretext_source not in ['private', 'system']:
        raise ValueError('Unknown CoreText selection')
    if manifest.get('private_fonts', False):
        font_framework = root / 'coretext/Frameworks'
        override = manifest.get('coretext_override')
        if override is not None:
            requested = Path(override)
            if (not manifest.get('system_layout') or '..' in requested.parts or
                    not requested.is_relative_to(root / 'overrides') or requested.name != 'Frameworks'):
                raise ValueError('CoreText override must be a versioned provider inside this app runtime')
            font_framework = requested
        if coretext_source == 'private':
            frames.insert(0, font_framework)
        env['FONTCONFIG_FILE'] = str(root / 'fonts/fonts.conf')
    libraries = [root / 'avfoundation/Frameworks/AVFoundation.framework/Versions/A',
        root / 'quartz/Frameworks/QuartzCore.framework/Versions/A', root / 'gl/lib', root / 'compat/usr/lib']
    if manifest.get('gpu_presentation', False):
        libraries.insert(0, (ui or root) / 'windowserver/usr/lib')
    if system_ui:
        # The missing-provider tree contains no AppKit, Onyx2D, CoreText or
        # QuartzCore. Remove private library directories too: QuartzCore.pd
        # and libWindowServer must follow the selected system frameworks.
        frames = [surface_framework, root / 'avfoundation/Frameworks', root / 'gl/Frameworks']
        frames += [root / 'compat/System/Library/Frameworks', Path('/System/Library/Frameworks')]
        libraries = [root / 'avfoundation/Frameworks/AVFoundation.framework/Versions/A',
                     root / 'gl/lib', root / 'compat/usr/lib']
        if system_gpu:
            frames = [root / 'avfoundation/Frameworks', root / 'compat/System/Library/Frameworks',
                      Path('/System/Library/Frameworks')]
            # OpenGL re-exports the matched Mesa dispatch at this canonical
            # prefix. Direct /usr/lib/libGL loads must resolve to the same
            # image too, rather than introducing a second dispatch provider.
            libraries = [Path('/usr/local/lib/siliconforest/opengl/v1/lib'),
                         root / 'avfoundation/Frameworks/AVFoundation.framework/Versions/A',
                         root / 'compat/usr/lib']
        elif graphics is not None:
            # Select storage, CGL, GL dispatch and broker as one cohort. Never
            # combine a new plane layout with the previous runtime's broker.
            frames = [graphics / 'surface/Frameworks', root / 'avfoundation/Frameworks',
                      graphics / 'gl/Frameworks', root / 'compat/System/Library/Frameworks',
                      Path('/System/Library/Frameworks')]
            libraries = [root / 'avfoundation/Frameworks/AVFoundation.framework/Versions/A',
                         graphics / 'gl/lib', root / 'compat/usr/lib']
    network = manifest.get('network_override')
    if network is not None:
        network = Path(network)
        if (not manifest.get('system_layout') or '..' in network.parts or
                not network.is_relative_to(root / 'overrides') or network.name != 'Frameworks' or
                network.parent == root / 'overrides'):
            raise ValueError('Network override must be a versioned framework inside this runtime')
        frames.insert(0, network)
    audio = manifest.get('audio_override')
    if audio is not None:
        audio = Path(audio)
        if (not manifest.get('system_layout') or '..' in audio.parts or
                not audio.is_relative_to(root / 'overrides') or audio.name != 'Frameworks' or
                audio.parent == root / 'overrides'):
            raise ValueError('Audio override must be a versioned complete family inside this runtime')
        frames.insert(0, audio)
    video = manifest.get('video_override')
    if video is not None:
        video = Path(video)
        if (not manifest.get('system_layout') or not system_ui or not system_gpu or
                '..' in video.parts or not video.is_relative_to(root / 'overrides') or
                video.name != 'Frameworks' or video.parent == root / 'overrides'):
            raise ValueError('Video override requires a versioned family and the qualified system graphics cohort')
        frames.insert(0, video)
    shared_video = manifest.get('video_zero_copy')
    if shared_video is not None:
        if type(shared_video) is not bool or (shared_video and video is None):
            raise ValueError('Shared video requires an explicit complete video family')
        if shared_video:
            # Best effort for ordinary playback; strict tests can require it.
            # Unsupported formats/devices retain the producer's copy fallback.
            if env.get('SF_VT_ZERO_COPY', '1') not in ['0', '1', 'require']:
                raise ValueError('Unknown video frame-allocation mode')
            env.setdefault('SF_VT_ZERO_COPY', '1')
        else:
            env.pop('SF_VT_ZERO_COPY', None)
    normalized_clock = manifest.get('audio_output_clock')
    if normalized_clock is not None:
        if not isinstance(normalized_clock, bool) or (normalized_clock and audio is None):
            raise ValueError('Normalized output clock requires an explicit audio family')
        if normalized_clock:
            env['SF_AU_NORMALIZE_HAL_CLOCK'] = '1'
        else:
            env.pop('SF_AU_NORMALIZE_HAL_CLOCK', None)
    env.update(DYLD_FRAMEWORK_PATH=':'.join(map(str, frames)),
        DYLD_LIBRARY_PATH=':'.join(map(str, libraries)) + ':/usr/local/lib/mesa-asahi',
        DYLD_INSERT_LIBRARIES=str(root / 'sandbox/libsffx-sandbox.dylib') + ':' + str(root / 'io/libsffx-io-policy.dylib'),
        XDG_RUNTIME_DIR='/private/var/run/gpu-desktop',
        WAYLAND_DISPLAY='wayland-0', SFFX_IO_POLICY='default',
        SFFX_IOSURFACE_SERVICE='org.siliconforest.iosurfaced.sffx.' + str(os.getpid()))
    observer = env.get('SFFX_DIAGNOSTIC_OBSERVER')
    if observer:
        observers = observer.split(':')
        explicit_profile = selected_profile(args)
        private = Path('/private/var/db/sffx')
        if (not 1 <= len(observers) <= 4 or explicit_profile is None or
                not explicit_profile.is_relative_to(private)):
            raise ValueError('Diagnostic audio observer requires an explicit isolated Firefox profile')
        for name in observers:
            requested = Path(name)
            if (not name or not requested.is_absolute() or '..' in requested.parts or
                    not requested.is_relative_to(private) or requested.suffix != '.dylib'):
                raise ValueError('Every diagnostic observer must be inside the private test prefix')
        env['DYLD_INSERT_LIBRARIES'] += ':' + ':'.join(observers)
    for name in list(env):
        if name.startswith('MOZ_DISABLE_') and name.endswith('_SANDBOX'):
            raise RuntimeError('Refusing inherited disabled sandbox: ' + name)
    original = Path(manifest.get('original_bundle', str(root / 'original/Firefox.app')))
    broker = Path('/usr/libexec/iosurfaced') if system_gpu else root / 'surface/bin/iosurfaced'
    if graphics is not None:
        broker = graphics / 'surface/bin/iosurfaced'
    command = [str(root / 'surface/bin/surface-session'), str(broker),
               str(original / 'Contents/MacOS/firefox')]
    if selected_profile(args) is None:
        command += ['--profile', str(default_profile(root, manifest))]
    command += args
    return command, env


def default_profile(root, manifest):
    if manifest.get('system_layout'):
        return Path.home() / 'Library/Application Support/Firefox/Profiles/siliconforest'
    return root / 'profile'


def main():
    root = Path(__file__).resolve().parents[3]
    config = Path(__file__).with_name('system-runtime.json')
    if config.exists():
        root = Path(json.loads(config.read_text())['target'])
        if root.parent != Path('/usr/local/lib/siliconforest/firefox') or root.is_symlink():
            raise RuntimeError('Unexpected installed runtime path')
    arguments = sys.argv[1:]
    dry = arguments[:1] == ['--sf-plan']
    command, env = plan(root, arguments[1:] if dry else arguments)
    if dry:
        print(json.dumps(dict(command=command, environment={k: v for k, v in env.items()
            if k.startswith(('DYLD_', 'SFFX_')) or k in {'MOZ_CRASHREPORTER_DISABLE', 'SF_AU_NORMALIZE_HAL_CLOCK', 'SF_VT_ZERO_COPY'}}, sandbox_enabled=True), indent=2))
        return
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Runtime requires SiliconForest')
    # A stale inherited SSH environment or an absent desktop must not produce
    # a long connection retry followed by an unreachable, headless parent.
    display = Path(env['XDG_RUNTIME_DIR']) / env['WAYLAND_DISPLAY']
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
            probe.settimeout(1)
            probe.connect(str(display))
    except OSError as error:
        raise RuntimeError('Firefox desktop is unavailable: ' + str(display)) from error
    manifest = json.loads((root / 'release.json').read_text())
    profile = selected_profile(arguments) or default_profile(root, manifest)
    profile.mkdir(mode=0o700, parents=True, exist_ok=True)
    preferences = {'browser.shell.checkDefaultBrowser': False, 'app.update.auto': False,
                   'webgl.disable-angle': True, 'browser.tabs.inTitlebar': 0}
    legacy = ''.join('user_pref(%s,%s);\n' % (json.dumps(k),json.dumps(v)) for k,v in preferences.items())
    preferences.update({'browser.aboutwelcome.enabled': False, 'browser.preonboarding.enabled': False,
                        'browser.startup.homepage_override.mstone': 'ignore',
                        'startup.homepage_welcome_url': '', 'startup.homepage_welcome_url.additional': '',
                        'datareporting.policy.dataSubmissionEnabled': False, 'toolkit.telemetry.enabled': False})
    settings = profile / 'user.js'
    # Upgrade only the byte-exact four-setting file this installer generated.
    # Preserve other settings and explicit choices; add missing platform defaults below.
    if not settings.exists() or (manifest.get('system_layout') and settings.read_text() == legacy):
        settings.write_text(''.join('user_pref(%s,%s);\n' % (json.dumps(k), json.dumps(v))
                                   for k, v in preferences.items()))
    update_platform_preferences(settings)
    update_compositor_preference(settings, native_compositor_preference(manifest, os.environ))
    update_codec_preference(settings, av1_preference(manifest, os.environ))
    logs = (Path.home() / 'Library/Logs/SiliconForest/Firefox') if manifest.get('system_layout') else root / 'logs'
    logs.mkdir(mode=0o700, parents=True, exist_ok=True)
    fd = os.open(logs / ('firefox-%d-%d.log' % (time.time(), os.getpid())), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    os.dup2(fd, 1)
    os.dup2(fd, 2)
    os.close(fd)
    os.chdir(Path(manifest.get('original_bundle', str(root / 'original/Firefox.app'))) / 'Contents/MacOS')
    os.execve(command[0], command, env)


if __name__ == '__main__':
    main()
