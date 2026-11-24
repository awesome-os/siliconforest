#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Finite target-only Firefox diagnostic. Run under workstation gui.lock.

The bundle must have been copied pristine and hashed before this program runs.
Only processes descended from this launch are signalled, and their PIDs are saved.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import time


def process_identities():
    rows = subprocess.check_output(['ps', '-axo', 'pid=,ppid=,lstart='], text=True)
    return {int(p): (int(pp), start) for p, pp, start in (r.split(None, 2) for r in rows.splitlines())}


def descendants(pid, identities):
    parents = {p: pp for p, (pp, _) in identities.items()}
    owned = {pid}
    while True:
        more = {p for p, pp in parents.items() if pp in owned}
        if more <= owned:
            return owned
        owned |= more


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bundle', type=Path, required=True)
    ap.add_argument('--executable', default='firefox', choices=['firefox','firefox-sf'])
    ap.add_argument('--allow-remote', action='store_true', help='owned-profile release URL remoting qualification')
    ap.add_argument('--integration-launcher', help='private release open(1) for isolated default URL delivery')
    ap.add_argument('--integration-bundle', help='private outer Firefox.app for isolated LaunchServices registration')
    ap.add_argument('--run', type=Path, required=True)
    ap.add_argument('--seconds', type=int, default=40)
    ap.add_argument('--url', action='append', default=[])
    ap.add_argument('--framework-path')
    ap.add_argument('--library-path')
    ap.add_argument('--gpu-surfaces', action='store_true', help='experimental actualGPU IOSurface allocation')
    ap.add_argument('--gpu-presentation', action='store_true', help='experimental private whole-window GPU layers')
    ap.add_argument('--gpu-vector-compare', action='store_true', help='diagnostic NEON equality scan of the real CPU base')
    ap.add_argument('--gpu-cache-lookups', action='store_true', help='diagnostic one-time GPU frame export symbol lookup')
    ap.add_argument('--compositor-stats', action='store_true', help='existing compositor commit-lead/render histograms; gui.lock required')
    ap.add_argument('--frame-trace', action='store_true', help='private WindowServer actual compositor frame callback timestamps')
    ap.add_argument('--event-trace', action='store_true', help='private AppKit bounded headless event-loop diagnostics')
    ap.add_argument('--window-trace', action='store_true', help='diagnostic WindowServer create/map/tab/flush trace')
    ap.add_argument('--software', action='store_true')
    ap.add_argument('--no-sandbox', action='store_true', help='diagnosis only; labelled in result')
    ap.add_argument('--insert-library', help='private diagnostic DSO (e.g. the real SBPL compiler)')
    ap.add_argument('--pref', action='append', default=[], help='NAME=JSON diagnostic preference')
    ap.add_argument('--catcher', help='external Mach exception catcher for this launch only')
    ap.add_argument('--surface-session', help='private named-IOSurface-broker session wrapper')
    ap.add_argument('--surface-broker', help='private standalone iosurfaced; requires --surface-session')
    ap.add_argument('--capture-pages', action='store_true', help='capture each owned browser tab after diagnostics')
    ap.add_argument('--capture-renderer', action='store_true', help='actual compositor framebuffer via existing inject capture')
    ap.add_argument('--page-load-strategy', choices=['normal', 'eager', 'none'], default='normal',
                    help='explicit WebDriver strategy; measurements still verify real page/media state')
    ap.add_argument('--quiet-trace', action='store_true', help='disable verbose media/GL/layer traces for timing runs')
    ap.add_argument('--io-policy', choices=['passthrough', 'utility', 'standard', 'default'], help='process-local diagnostic interposer mode')
    ap.add_argument('--ui-qos', choices=['interactive','initiated'], help='diagnostic startup QoS; no affinity/frequency writes')
    ap.add_argument('--measure-video', action='store_true')
    ap.add_argument('--measure-all-videos', action='store_true', help='measure each owned video tab, with separate CPU/AVD counters')
    ap.add_argument('--gpu-transfer-spec', help='UUID-matched private CGL counter specification for video')
    ap.add_argument('--policy-probe-path', help='read-only native thread-policy/counter probe')
    ap.add_argument('--browse-stability', action='store_true', help='announced30minute ordinary-site browsing with abnormal-content-exit observer')
    ap.add_argument('--jit-probe', action='store_true', help='actual Baseline/Ion frames from a separate Gecko profile')
    ap.add_argument('--streaming', action='store_true', help='owned public non-DRM YouTube playback')
    ap.add_argument('--streaming-dash', action='store_true', help='official DASH-IF clear H264 MediaSource playback')
    ap.add_argument('--media-trace', action='store_true', help='decoder identity logs without GL/window traces')
    ap.add_argument('--vt-frame-mode', choices=['copy','best-effort','require'], default='copy', help='private VT frame allocation diagnostic; require fails closed without import capability')
    ap.add_argument('--quality', action='store_true', help='owned new/private windows and content download')
    ap.add_argument('--panels', action='store_true', help='owned native open/save picker selection through synthetic seat input')
    ap.add_argument('--render-quality', type=int, choices=[0, 2], help='controlled ICC off/on pixels and glyph raster evidence')
    ap.add_argument('--video-fullscreen', action='store_true', help='owned trusted DOM video fullscreen and native frame controls')
    ap.add_argument('--pinch-zoom', action='store_true', help='synthetic native pinch; observe real browser/visual viewport zoom')
    ap.add_argument('--native-ui', action='store_true', help='finite synthetic native seat/menu checks on the owned Firefox PID')
    ap.add_argument('--popup-stress', action='store_true', help='20 trusted virtual-seat application-menu open/close cycles on the owned browser')
    ap.add_argument('--popup-observe-auto-close', action='store_true', help='protocol diagnostic only: continue recording after automatic popup closure; never qualifies menu usability')
    ap.add_argument('--clipboard-guard', help='RAM-only restore helper for native clipboard controls')
    ap.add_argument('--native-drag', action='store_true', help='native-UI owned DOM drag/drop through the real seat')
    ap.add_argument('--clipboard-empty', action='store_true', help='native copy/paste only after focused backend proves empty clipboard')
    ap.add_argument('--precise-scroll', action='store_true', help='paired private finger pixel/phase provider and native-UI DOM control')
    ap.add_argument('--benchmark', choices=['speedometer', 'motionmark', 'jetstream'])
    ap.add_argument('--measure-fixture', action='store_true')
    ap.add_argument('--fixture-repetitions', type=int, default=1,
                    help='one to four separately timed measurements in the same browser')
    ap.add_argument('--fixture-pacing-sequence', help='temporary compositor latch modes, e.g. on,off,on; restores automatic mode')
    ap.add_argument('--fixture-qos-sequence', help='paired main-thread modes, e.g. default,interactive,default,interactive')
    ap.add_argument('--qos-library', help='already-inserted private diagnostic DSO for main-thread QoS')
    ap.add_argument('--fixture-seconds', type=int, default=10, choices=range(1,61), metavar='1..60')
    ap.add_argument('--profile-fixture', action='store_true', help='diagnostic parent stack sampling during fixture; excludes timing qualification')
    ap.add_argument('--pipeline-profile', action='store_true', help='diagnostic native/JS/IPC Gecko profile of this browser family')
    ap.add_argument('--site-legibility', action='store_true', help='capture text paint masks and actual renderer for owned ordinary pages')
    ap.add_argument('--paired-site-legibility', action='store_true', help='font-on/off paint controls in the same owned profile')
    ap.add_argument('--page-down-captures', action='store_true', help='four owned HTTPS tabs, five trusted virtual-seat Page Downs each; inspected pixels required')
    ap.add_argument('--composition-workloads', action='store_true', help='four ten-second installed composition controls, retaining per-window WMPACING and family CPU')
    ap.add_argument('--font-logs', action='store_true', help='bounded font activation diagnostics for owned legibility controls')
    ap.add_argument('--profile-video', action='store_true', help='diagnostic all-process Gecko profile around owned video clips')
    ap.add_argument('--profile-benchmark', action='store_true', help='diagnostic Gecko profile around a stock benchmark; score is not timing-qualified')
    ap.add_argument('--scroll-fixture', action='store_true', help='labelled programmatic scroll on the owned fixture')
    ap.add_argument('--no-startup-cache', action='store_true', help='diagnostic only: fail startup-cache disk writes in this isolated run')
    ap.add_argument('--sample-parent', action='store_true', help='also sample this owned browser parent')
    ap.add_argument('--gpu-startup-fault', choices=['stop', 'kill'],
                    help='diagnostic only: signal the first GPU helper of this isolated launch')
    ap.add_argument('--functional-qa', action='store_true',
                    help='owner-authorized functional checks without idle waits; no qualified timings')
    ap.add_argument('--sample-path', help='private sf-sample; samples only owned GPU helpers')
    ap.add_argument('--expect-appkit-prefix', help='diagnostic dyld-list proof in parent/GPU; requires --sample-path')
    ap.add_argument('--expect-system-gpu', action='store_true', help='also require canonical IOSurface/OpenGL in parent/GPU image lists')
    ap.add_argument('--expect-private-windowserver', help='explicit isolated protocol diagnostic provider, with the other UI frameworks canonical')
    ap.add_argument('--capture-after', type=float, help='one early renderer capture, seconds after exec')
    ap.add_argument('--font-config', help='private process fontconfig file')
    ap.add_argument('--gpu-dirty-frames', action='store_true', help='experimental generation-based GPU frame reuse')
    ap.add_argument('--presentation-seat', action='store_true', help='admit/focus/wake the owned fixture before presentation timing')
    ap.add_argument('--profile-target', choices=['parent', 'content'], default='parent')
    ap.add_argument('--profile-delay', type=float, default=2)
    ap.add_argument('--gpu-export-cache', action='store_true')
    ap.add_argument('--gpu-coalesce-ticks', action='store_true', help='experimental60Hz GPU notification batching')
    ap.add_argument('--scene-reference', action='store_true')
    ap.add_argument('--finish-after-scene-reference', action='store_true', help='bounded functional renderer/provider control')
    args = ap.parse_args()
    if args.paired_site_legibility:
        args.site_legibility = True
    if args.functional_qa and (args.benchmark or args.measure_fixture or args.measure_video or args.measure_all_videos):
        raise ValueError('Functional QA does not qualify benchmark or video/frame timings')
    if args.finish_after_scene_reference and not (args.functional_qa and args.scene_reference):
        raise ValueError('Early scene exit requires a functional reference-scene control')
    if args.popup_stress and (not args.functional_qa or not args.presentation_seat or args.native_ui):
        raise ValueError('Popup stress requires a separate functional-QA presentation-seat run')
    if args.composition_workloads and (not args.functional_qa or not args.presentation_seat or args.page_down_captures or args.popup_stress):
        raise ValueError('Composition comparison requires its own owned virtual-seat run')
    if args.page_down_captures and (not args.functional_qa or not args.presentation_seat or args.popup_stress or args.native_ui):
        raise ValueError('Page Down captures require their own functional-QA virtual-seat run')
    if args.popup_observe_auto_close and not args.popup_stress:
        raise ValueError('Automatic-close observation is only a popup protocol diagnostic')
    if args.expect_private_windowserver and (not args.functional_qa or
            args.expect_appkit_prefix != '/System/Library/Frameworks/AppKit.framework'):
        raise ValueError('Private WindowServer requires functional QA and canonical UI verification')
    if os.uname().sysname != 'Darwin':
        raise RuntimeError('Target must be SiliconForest')
    if bool(args.surface_session) != bool(args.surface_broker):
        raise ValueError('Supply both private surface-session and surface-broker paths')
    if args.expect_appkit_prefix and not args.sample_path:
        raise ValueError('AppKit provider verification requires --sample-path')
    if args.expect_system_gpu and args.expect_appkit_prefix != '/System/Library/Frameworks/AppKit.framework':
        raise ValueError('Canonical GPU verification requires the system Cocoa provider gate')
    if args.pinch_zoom and (not args.presentation_seat or args.native_ui or args.measure_fixture or args.benchmark):
        raise ValueError('Pinch control requires its own presentation-seat run without another workload')
    if args.gpu_transfer_spec and (not args.policy_probe_path or not args.expect_appkit_prefix or not (args.measure_video or args.measure_all_videos)):
        raise ValueError('GPU counters require owned provider admission, native probe and video measurement')
    if args.gpu_coalesce_ticks and not args.gpu_presentation:
        raise ValueError('GPU notification batching requires --gpu-presentation')
    if (args.native_drag or args.clipboard_empty or args.precise_scroll) and not args.native_ui:
        raise ValueError('Drag/clipboard seat checks require --native-ui admission')
    if args.profile_fixture and (not args.measure_fixture or not args.sample_path):
        raise ValueError('Fixture profiling requires --measure-fixture and --sample-path')
    if args.pipeline_profile and (not args.measure_fixture or args.profile_fixture or args.fixture_repetitions != 1):
        raise ValueError('Pipeline profiling requires one fixture measurement without a second sampler')
    if args.profile_video and (not args.measure_all_videos or args.pipeline_profile or args.profile_benchmark or args.measure_fixture or args.benchmark):
        raise ValueError('Video profiling requires one measure-all-videos workload')
    if args.profile_benchmark and (not args.benchmark or args.pipeline_profile or args.measure_fixture or args.profile_fixture):
        raise ValueError('Benchmark profiling requires one benchmark without another profiled workload')
    if args.fixture_pacing_sequence:
        pacing_modes=args.fixture_pacing_sequence.split(',')
        if not 2<=len(pacing_modes)<=4 or any(mode not in ['on','off'] for mode in pacing_modes):
            raise ValueError('Pacing sequence requires2..4 on/off modes')
        if not args.measure_fixture or args.pipeline_profile or args.profile_fixture or args.fixture_qos_sequence or args.fixture_repetitions!=1:
            raise ValueError('Pacing control requires one unprofiled fixture, without another sequence')
    if args.fixture_qos_sequence:
        modes=args.fixture_qos_sequence.split(',')
        if not args.qos_library or args.qos_library not in (args.insert_library or '').split(':'):
            raise ValueError('QoS diagnostic requires its explicitly inserted library')
        if any(mode not in ('default','initiated','interactive') for mode in modes) or not 2 <= len(modes) <= 4:
            raise ValueError('QoS diagnostic requires two to four valid modes')
        if not args.measure_fixture or args.profile_fixture or args.pipeline_profile or args.ui_qos:
            raise ValueError('Paired QoS requires a fixture without startup QoS or diagnostic profiling')
        args.fixture_repetitions=len(modes)
    if not 1 <= args.fixture_repetitions <= 4 or (args.fixture_repetitions != 1 and
            (not args.measure_fixture or args.profile_fixture)):
        raise ValueError('Repeated fixture timing requires --measure-fixture without diagnostic profiling')
    if not 0 <= args.profile_delay <= 6:
        raise ValueError('Fixture profiling delay must be0..6seconds')
    args.run.mkdir(parents=True, exist_ok=False)
    profile = args.run / 'profile'
    profile.mkdir(mode=0o700)
    home = args.run / 'home'
    home.mkdir(mode=0o700)
    # Private wrappers can call daily.plan() without daily.main()'s profile
    # preparation. Match the installed window path; --pref still permits
    # explicit CSD/ANGLE controls below.
    prefs = {'browser.tabs.inTitlebar': 0, 'webgl.disable-angle': True,
             'browser.shell.checkDefaultBrowser': False, 'browser.aboutwelcome.enabled': False,
             'browser.preonboarding.enabled': False, 'browser.startup.homepage_override.mstone': 'ignore',
             'startup.homepage_welcome_url': '', 'startup.homepage_welcome_url.additional': '',
             'datareporting.policy.dataSubmissionEnabled': False, 'toolkit.telemetry.enabled': False,
             'app.update.auto': False, 'marionette.port': 28290}
    if args.software:
        prefs['gfx.webrender.software'] = True
    for setting in args.pref:
        name, separator, value = setting.partition('=')
        if not name or not separator:
            raise ValueError('Expected --pref NAME=JSON')
        prefs[name] = json.loads(value)
    if args.render_quality is not None:
        prefs['gfx.color_management.mode'] = args.render_quality
    (profile / 'user.js').write_text(''.join('user_pref(%s, %s);\n' % (json.dumps(k), json.dumps(v))
                                          for k, v in prefs.items()))
    env = os.environ.copy()
    if args.pipeline_profile:
        env['SFFX_GPU_PHASE_TRACE']='1'
    else:
        env.pop('SFFX_GPU_PHASE_TRACE',None)
    if args.font_config:
        if (not args.font_config.startswith('/private/var/db/sffx/') and args.font_config != '/usr/local/lib/siliconforest/firefox/160-system1/fonts/fonts.conf') or '..' in Path(args.font_config).parts:
            raise ValueError('Font configuration must be inside the private prefix')
        env['FONTCONFIG_FILE'] = args.font_config
    else:
        env.pop('FONTCONFIG_FILE', None)
    env.update(HOME=str(home), CFFIXED_USER_HOME=str(home), XDG_RUNTIME_DIR='/private/var/run/gpu-desktop',
               WAYLAND_DISPLAY='wayland-0', MOZ_CRASHREPORTER_DISABLE='1', MOZ_NO_REMOTE='1',
               MOZ_LOG='PlatformDecoderModule:5,AppleMedia:5,DMABuf:4',
               SF_WS_TIMING='1', SF_CGL_TRACE='1', SF_SANDBOX_TRACE='1', SF_CA_TRACE='1',
               MOZ_REMOTE_ALLOW_SYSTEM_ACCESS='1')
    if args.allow_remote:
        env.pop('MOZ_NO_REMOTE', None)
    if args.gpu_surfaces:
        env['SFFX_IOSURFACE_GPU'] = '1'
    else:
        env.pop('SFFX_IOSURFACE_GPU', None)
    if args.gpu_presentation:
        env['SFFX_GPU_PRESENT'] = '1'
    else:
        env.pop('SFFX_GPU_PRESENT', None)
    for active, name in [(args.gpu_vector_compare, 'SFFX_GPU_VECTOR_COMPARE'), (args.gpu_cache_lookups, 'SFFX_GPU_CACHE_LOOKUPS'), (args.gpu_dirty_frames, 'SFFX_GPU_DIRTY_FRAMES'), (args.gpu_export_cache, 'SFFX_GPU_EXPORT_CACHE'), (args.gpu_coalesce_ticks, 'SFFX_GPU_COALESCE_TICKS')]:
        if active:
            env[name] = '1'
        else:
            env.pop(name, None)
    if args.frame_trace:
        env['SFFX_FRAME_TRACE'] = '1'
    if args.event_trace:
        env['SFFX_EVENT_TRACE'] = '1'
    if args.window_trace:
        env['SF_WS_TRACE']='1'
    if args.precise_scroll:
        env.update(SFFX_PRECISE_SCROLL='1', SFFX_SCROLL_TRACE='1')
    if args.native_drag:
        env['SF_DRAG_TRACE'] = '1'
    if args.quiet_trace:
        for name in ['MOZ_LOG', 'SF_WS_TIMING', 'SF_CGL_TRACE', 'SF_SANDBOX_TRACE', 'SF_CA_TRACE']:
            env.pop(name, None)
    if args.media_trace:
        env['MOZ_LOG'] = 'PlatformDecoderModule:5,AppleMedia:5'
    if args.font_logs:
        env['MOZ_LOG']='fontlist:5,userfonts:5,textrun:5'
    if args.vt_frame_mode=='copy':env.pop('SF_VT_ZERO_COPY',None)
    else:env['SF_VT_ZERO_COPY']='1' if args.vt_frame_mode=='best-effort' else 'require'
    if args.io_policy:
        env['SFFX_IO_POLICY'] = args.io_policy
    if args.ui_qos:
        env['SFFX_UI_QOS']=args.ui_qos
    else:
        env.pop('SFFX_UI_QOS',None)
    if args.no_startup_cache:
        # Mozilla's supported cache-path override. The parent deliberately does
        # not exist, so writes fail promptly; profile data remains isolated.
        env['MOZ_STARTUP_CACHE'] = str(args.run / 'cache-disabled-parent/startupCache')
    if args.framework_path:
        env['DYLD_FRAMEWORK_PATH'] = args.framework_path
    if args.library_path:
        env['DYLD_LIBRARY_PATH'] = args.library_path
    if args.insert_library:
        env['DYLD_INSERT_LIBRARIES'] = args.insert_library
    if args.catcher:
        env['SF_NO_CRASH_REPORT'] = '1'
    if args.surface_session:
        env['SFFX_IOSURFACE_SERVICE'] = 'org.siliconforest.iosurfaced.sffx.' + str(os.getpid())
    if args.no_sandbox:
        for kind in ['CONTENT', 'GPU', 'RDD', 'SOCKET_PROCESS', 'UTILITY', 'GMP']:
            env['MOZ_DISABLE_' + kind + '_SANDBOX'] = '1'
    cmd = [str(args.bundle / ('Contents/MacOS/' + args.executable))] + ([] if args.allow_remote else ['--no-remote']) + ['--profile', str(profile),
           '--marionette'] + (args.url or ['about:support'])
    result = dict(command=cmd, seconds=args.seconds, software_requested=args.software,
                  sandbox_disabled=args.no_sandbox, gpu_surfaces=args.gpu_surfaces, gpu_presentation=args.gpu_presentation,
                  gpu_vector_compare=args.gpu_vector_compare,gpu_cache_lookups=args.gpu_cache_lookups,
                  gpu_dirty_frames=args.gpu_dirty_frames, font_config=args.font_config,
                  gpu_export_cache=args.gpu_export_cache,
                  gpu_coalesce_ticks=args.gpu_coalesce_ticks,
                  synthetic_input=args.presentation_seat or args.scroll_fixture or bool(args.benchmark) or args.native_ui or args.panels, prefs=prefs,
                  diagnostic_library=args.insert_library, startup_cache_write_disabled=args.no_startup_cache,
                  io_policy=args.io_policy, ui_qos=args.ui_qos, verbose_trace=not args.quiet_trace, window_trace=args.window_trace)
    result['surface_session'] = dict(wrapper=args.surface_session, broker=args.surface_broker,
                                    service=env.get('SFFX_IOSURFACE_SERVICE'))
    from owner_guard import OwnerInputGuard
    owner_guard=OwnerInputGuard(require_idle=not args.functional_qa)
    if args.functional_qa:
        from presentation_seat import functional_qa
        functional_qa()
        result.update(functional_qa=True,timing_qualified=False)
    result['owner_input_before']=owner_guard.before
    owned = {}
    start = time.monotonic()
    wall_start = time.time()
    result['launcher_wall_start_ms'] = wall_start * 1000
    early_captured = False
    gpu_fault_injected = False
    gpu_sampled = False
    with (args.run / 'firefox.log').open('w') as log:
        launch = ([args.catcher] if args.catcher else []) + cmd
        if args.surface_session:
            launch = [args.surface_session, args.surface_broker] + launch
        result['launcher'] = launch
        p = subprocess.Popen(launch, env=env, cwd=args.bundle / 'Contents/MacOS',
                             stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        result['pid'] = p.pid
        support_attempted = False
        session_started = False
        automation_failed = False
        try:
            deadline = start + args.seconds
            samples = []
            while p.poll() is None and time.monotonic() < deadline:
                owner_guard.check()
                identities = process_identities()
                owned.update({pid: identities[pid][1] for pid in descendants(p.pid, identities)
                              if pid in identities})
                samples.append(dict(elapsed=time.monotonic() - start,
                                    ps=subprocess.run(['ps', '-p', ','.join(map(str, sorted(owned))),
                                                       '-o', 'pid,ppid,time,rss,stat,command'],
                                                      capture_output=True, text=True).stdout))
                if args.gpu_startup_fault and not gpu_fault_injected:
                    # Admission requires both ancestry and the unchanged start
                    # time, not a global process-name match. The owner cannot
                    # be selected even when another Firefox is running.
                    for pid in sorted(owned):
                        if pid == p.pid:
                            continue
                        current = process_identities()
                        if pid not in current or current[pid][1] != owned[pid]:
                            continue
                        command = subprocess.run(['ps', '-p', str(pid), '-o', 'command='],
                            capture_output=True, text=True, check=True).stdout
                        if 'gpu-helper.app/Contents/MacOS/Firefox GPU Helper' not in command:
                            continue
                        owner_guard.check()
                        sig = signal.SIGSTOP if args.gpu_startup_fault == 'stop' else signal.SIGKILL
                        os.kill(pid, sig)
                        result['gpu_startup_fault'] = dict(pid=pid, start=owned[pid],
                            signal=sig.name, elapsed=time.monotonic() - start,
                            wall_ms=time.time() * 1000,
                            timing_qualified=False, software_fallback_qualified=False)
                        gpu_fault_injected = True
                        break
                if args.capture_after is not None and not early_captured and time.monotonic() - start >= args.capture_after:
                    early_captured = True
                    capture = subprocess.run(['/usr/bin/chonkstep-darwin-desktop', '--screencapture',
                                              str(args.run / 'screen-early.png')],
                                             capture_output=True, text=True, timeout=15, env=env)
                    result['early_capture'] = dict(status=capture.returncode, stdout=capture.stdout,
                                                   stderr=capture.stderr, elapsed=time.monotonic() - start)
                if args.sample_path and not gpu_sampled and time.monotonic() - start > 18:
                    gpu_sampled = True
                    current_ids = process_identities()
                    for pid in sorted(owned):
                        if pid not in current_ids or current_ids[pid][1] != owned[pid]:
                            continue
                        command = subprocess.run(['ps', '-p', str(pid), '-o', 'command='], capture_output=True, text=True).stdout
                        if args.bundle == Path('/Applications/Firefox.app') and args.executable == 'firefox-sf':
                            browser_command = str(args.bundle / 'Contents/Resources/Mozilla.app/Contents/MacOS/firefox')
                        else:
                            browser_command = str((args.bundle.parent / 'original/Firefox.app/Contents/MacOS/firefox')
                                                  if args.executable == 'firefox-sf' else args.bundle / 'Contents/MacOS/firefox')
                        is_parent = args.sample_parent and command.strip().startswith(browser_command)
                        if pid != p.pid and (is_parent or 'gpu-helper.app/Contents/MacOS/Firefox GPU Helper' in command):
                            sample = subprocess.run([args.sample_path, str(pid), '2', '20', '-file',
                                                     str(args.run / ('gpu-sample-%d.txt' % pid))],
                                                    capture_output=True, text=True, timeout=12)
                            result['gpu_sample'] = dict(pid=pid, status=sample.returncode, stderr=sample.stderr)
                # Give the independent native sampler its turn before a
                # synchronous Marionette command can wait on a stalled parent.
                admission_after = 21 if args.gpu_startup_fault else 8
                if not support_attempted and time.monotonic() - start > admission_after:
                    try:
                        from marionette import Marionette
                        client = Marionette(timeout=1)
                    except (OSError, EOFError, ValueError, ImportError):
                        pass
                    else:
                        support_attempted = True
                        original_command=client.command
                        def guarded_command(*arguments,**keywords):
                            owner_guard.check()
                            reply=original_command(*arguments,**keywords)
                            owner_guard.check()
                            return reply
                        client.command=guarded_command
                        try:
                            client.socket.settimeout(max(1, min(45, deadline - time.monotonic())))
                            result['marionette'] = client.start(args.page_load_strategy)
                            session_started = True
                            result['automation'] = 'Marionette owned-profile diagnostics and measurements'
                            client.socket.settimeout(max(1, min(10, deadline - time.monotonic())))
                            (args.run / 'support.json').write_text(json.dumps(client.support(), indent=2) + '\n')
                            (args.run / 'runtime.json').write_text(json.dumps(client.runtime(), indent=2) + '\n')
                            if args.expect_appkit_prefix:
                                from provider_images import inspect
                                current = process_identities()
                                owned.update({pid: current[pid][1] for pid in descendants(p.pid, current) if pid in current})
                                result['providers'] = inspect(args.sample_path, args.run / 'providers.json', owned,
                                    result['marionette']['capabilities']['moz:processID'], args.expect_appkit_prefix,
                                    args.expect_system_gpu, args.expect_private_windowserver)
                            if args.policy_probe_path:
                                from scheduling_snapshot import inspect as inspect_policies
                                current=process_identities()
                                owned.update({pid:current[pid][1] for pid in descendants(p.pid,current) if pid in current})
                                result['scheduling']=inspect_policies(args.policy_probe_path,current,owned,args.run/'scheduling.json')
                            if args.presentation_seat:
                                from presentation_seat import admit
                                result['presentation_seat'] = admit(client, args.run / 'presentation-seat.json',
                                    fixture=args.measure_fixture or args.scene_reference)
                            def snapshot():
                                current = process_identities()
                                owned.update({pid: current[pid][1] for pid in descendants(p.pid, current)
                                              if pid in current})
                                active = [pid for pid in sorted(owned)
                                          if pid in current and current[pid][1] == owned[pid]]
                                if not active:
                                    return ''
                                return subprocess.run(['ps', '-p', ','.join(map(str, active)),
                                    '-o', 'pid,ppid,time,rss,stat,command'], capture_output=True, text=True).stdout
                            if args.composition_workloads:
                                from composition_workloads import measure as measure_composition
                                result['composition_workloads'] = measure_composition(client, snapshot, args.run, deadline)
                                deadline = min(deadline, time.monotonic()+2)
                            if args.page_down_captures:
                                from page_down_captures import measure as measure_page_down
                                result['page_down_captures'] = measure_page_down(client, args.run, deadline)
                                deadline = min(deadline, time.monotonic()+2)
                            if args.site_legibility:
                                if args.paired_site_legibility:
                                    from paired_legibility import measure as measure_legibility
                                else:
                                    from font_legibility import measure as measure_legibility
                                result['legibility']=measure_legibility(client,args.run,
                                                                       seat_activity=args.presentation_seat)
                                if not args.popup_stress:
                                    deadline=min(deadline,time.monotonic()+2)
                            if args.benchmark:
                                from benchmark import measure
                                client.socket.settimeout(max(1, min(90, deadline - time.monotonic())))
                                admission=None
                                if args.profile_benchmark:
                                    from pipeline_profile import start as start_pipeline
                                    admission=start_pipeline(client)
                                    result['profile_interval']=[time.time()*1000,None]
                                try:
                                    result['benchmark'] = measure(client, args.benchmark, deadline, args.run / 'benchmark.json',
                                        seat_activity=args.presentation_seat)
                                finally:
                                    if admission:
                                        result['profile_interval'][1]=time.time()*1000
                                        from pipeline_profile import stop as stop_pipeline
                                        result['pipeline_profile']=stop_pipeline(client,args.run,admission)
                                        if 'benchmark' in result:
                                            result['benchmark'].update(diagnostic_only=True,timing_qualification=False)
                                            (args.run/'benchmark.json').write_text(json.dumps(result['benchmark'],indent=2)+'\n')
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.browse_stability:
                                from stability import measure
                                client.socket.settimeout(max(1, min(25, deadline - time.monotonic())))
                                result['stability'] = measure(client, args.run / 'stability.json', deadline, snapshot,
                                    seat_activity=args.presentation_seat)
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.jit_probe:
                                from jit_probe import measure
                                client.socket.settimeout(max(1, min(30, deadline - time.monotonic())))
                                result['jit_probe'] = measure(client, args.run / 'jit-probe.json')
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.integration_launcher:
                                from integration_probe import measure
                                client.socket.settimeout(35)
                                result['integration'] = measure(client, args.run / 'integration.json', args.integration_launcher, args.integration_bundle, home)
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.streaming:
                                from streaming import measure
                                client.socket.settimeout(max(1, min(50, deadline - time.monotonic())))
                                result['streaming'] = measure(client, args.run / 'streaming.json', deadline)
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.streaming_dash:
                                from streaming_dash import measure
                                client.socket.settimeout(max(1, min(40, deadline - time.monotonic())))
                                result['streaming_dash'] = measure(client, args.run / 'streaming-dash.json', deadline)
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.quality:
                                from quality import measure
                                result['quality'] = measure(client, args.run / 'quality.json', deadline)
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.panels:
                                from panels import measure
                                result['panels'] = measure(client, args.run / 'panels.json', deadline)
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.render_quality is not None:
                                from render_quality import measure
                                result['render_quality'] = measure(client, args.run / 'render-quality.json', args.render_quality)
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.video_fullscreen:
                                from fullscreen import measure
                                result['video_fullscreen'] = measure(client, args.run / 'fullscreen.json', deadline)
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.native_ui:
                                from native_ui import measure
                                result['native_ui'] = measure(client, args.run / 'native-ui.json', deadline,
                                    args.clipboard_guard, args.native_drag, args.clipboard_empty, args.precise_scroll)
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.popup_stress:
                                from popup_stress import measure as measure_popups
                                result['popup_stress'] = measure_popups(client, args.run / 'popup-stress.json', deadline,
                                    result['presentation_seat'], args.popup_observe_auto_close)
                                deadline = min(deadline, time.monotonic() + 2)
                            if args.pinch_zoom:
                                from pinch_zoom import measure as measure_pinch
                                result['pinch_zoom']=measure_pinch(client,args.run)
                                deadline=min(deadline,time.monotonic()+2)
                            if args.measure_fixture:
                                if args.fixture_pacing_sequence:
                                    from fixture_pacing import measure as measure_pacing
                                    client.socket.settimeout(args.fixture_seconds+10)
                                    result.update(measure_pacing(client,snapshot,lambda:time.monotonic()-start,args.run,
                                        args.scroll_fixture,args.fixture_seconds,pacing_modes))
                                    deadline=min(deadline,time.monotonic()+2)
                                else:
                                    client.socket.settimeout(max(1, min(args.fixture_seconds+6, deadline - time.monotonic())))
                                    profiler = None
                                    if args.profile_fixture:
                                        import threading
                                        cancel_profile = threading.Event()
                                        profile_result = dict(diagnostic_only=True, timing_qualification=False,
                                                              target=args.profile_target, delay=args.profile_delay)
                                        target_pid = result['marionette']['capabilities']['moz:processID']
                                        if args.profile_target == 'content':
                                            client.command('Marionette:SetContext', {'value': 'chrome'})
                                            target_pid = client.command('WebDriver:ExecuteScript', dict(script='''
                                              const w=Services.wm.getMostRecentWindow("navigator:browser"),b=w.gBrowser.selectedBrowser;
                                              for(const read of [()=>b.browsingContext.currentWindowGlobal.domProcess.osPid,
                                                  ()=>b.frameLoader.remoteTab.osPid,()=>b.browsingContext.currentWindowGlobal.osPid]){
                                                try{const pid=read();if(Number.isInteger(pid)&&pid>0)return pid;}catch(e){}
                                              }return null;''', args=[], newSandbox=False))
                                        current = process_identities()
                                        if target_pid not in descendants(p.pid, current):
                                            raise RuntimeError('Profiler target is not in the owned browser family')
                                        profile_result['pid'] = target_pid
                                        target_identity = process_identities().get(target_pid)
                                        def profile_fixture():
                                            if cancel_profile.wait(args.profile_delay):
                                                return
                                            if target_identity is None or process_identities().get(target_pid) != target_identity:
                                                profile_result['error'] = 'Owned profiler target identity changed'
                                                return
                                            try:
                                                sampled = subprocess.run([args.sample_path, str(target_pid), '3', '5', '-file',
                                                    str(args.run / ('fixture-' + args.profile_target + '-profile.txt'))], capture_output=True, text=True, timeout=12)
                                                profile_result.update(status=sampled.returncode, stderr=sampled.stderr)
                                            except subprocess.TimeoutExpired:
                                                profile_result['error'] = 'Sampler timeout'
                                        profiler = threading.Thread(target=profile_fixture)
                                        profiler.start()
                                    if args.fixture_qos_sequence:
                                        from fixture_qos import set_main
                                        result['fixture_qos']=set_main(client,args.qos_library,modes[0])
                                    if args.compositor_stats:
                                        from compositor_stats import start as start_stats, stop as stop_stats
                                        clock_admission=start_stats()
                                    result['fixture_cpu_before'] = dict(elapsed=time.monotonic()-start, ps=snapshot())
                                    pipeline_admission=None
                                    if args.pipeline_profile:
                                        from pipeline_profile import start as start_pipeline
                                        client.socket.settimeout(50)
                                        pipeline_admission=start_pipeline(client)
                                    try:
                                        result['fixture_measurement'] = client.measure_fixture(args.scroll_fixture,args.fixture_seconds*1000)
                                        if args.pipeline_profile:
                                            from renderer_capture import capture as capture_stats
                                            result['webrender_stats_capture']=capture_stats(args.run/'webrender-interval-end.ppm')
                                            result['webrender_stats_capture']['method']='Diagnostic renderer snapshot immediately after fixture completion, before profiler serialization; excluded from the fixture interval.'
                                    finally:
                                        if pipeline_admission:
                                            from pipeline_profile import stop as stop_pipeline
                                            result['pipeline_profile']=stop_pipeline(client,args.run,pipeline_admission)
                                        if profiler:
                                            cancel_profile.set()
                                            profiler.join(timeout=15)
                                            result['fixture_profile'] = profile_result
                                    result['fixture_cpu_after'] = dict(elapsed=time.monotonic()-start, ps=snapshot())
                                    if args.compositor_stats:result['compositor_stats']=stop_stats(clock_admission)
                                    if args.fixture_repetitions > 1:
                                        fields = ['fixture_measurement', 'fixture_cpu_before', 'fixture_cpu_after']
                                        if args.fixture_qos_sequence:fields.append('fixture_qos')
                                        if args.compositor_stats:fields.append('compositor_stats')
                                        result['fixture_repetitions'] = [{key: result[key] for key in fields}]
                                        for index in range(1, args.fixture_repetitions):
                                            client.socket.settimeout(max(1, min(args.fixture_seconds+6, deadline-time.monotonic())))
                                            qos=None
                                            if args.fixture_qos_sequence:qos=set_main(client,args.qos_library,modes[index])
                                            if args.compositor_stats:clock_admission=start_stats()
                                            entry = dict(fixture_cpu_before=dict(elapsed=time.monotonic()-start, ps=snapshot()))
                                            if qos:entry['fixture_qos']=qos
                                            entry['fixture_measurement'] = client.measure_fixture(args.scroll_fixture,args.fixture_seconds*1000)
                                            entry['fixture_cpu_after'] = dict(elapsed=time.monotonic()-start, ps=snapshot())
                                            if args.compositor_stats:entry['compositor_stats']=stop_stats(clock_admission)
                                            result['fixture_repetitions'].append(entry)
                                            (args.run / 'fixture-repetitions.json').write_text(
                                                json.dumps(result['fixture_repetitions'], indent=2)+'\n')
                                    deadline = min(deadline, time.monotonic() + 2)
                            if args.scene_reference:
                                from scene_reference import measure
                                result['scene_reference'] = measure(client, args.run / 'scene-reference.json')
                                if args.finish_after_scene_reference:
                                    deadline = min(deadline, time.monotonic()+2)
                            if args.gpu_export_cache and args.expect_appkit_prefix:
                                from cgl_stats import inspect
                                result['cgl_export_cache'] = inspect(client, result['providers'], args.run / 'cgl-export-cache.json')
                            if args.measure_video:
                                client.socket.settimeout(max(1, min(76, deadline - time.monotonic())))
                                if args.gpu_transfer_spec:
                                    from gpu_transfer_stats import snapshot as gpu_snapshot
                                    result['video_gpu_before']=gpu_snapshot(result['providers'],args.gpu_transfer_spec,args.policy_probe_path,process_identities(),owned)
                                result['video_cpu_before'] = dict(elapsed=time.monotonic()-start, ps=snapshot())
                                result['video_measurement'] = client.measure_video()
                                result['video_cpu_after'] = dict(elapsed=time.monotonic()-start, ps=snapshot())
                                if args.gpu_transfer_spec:
                                    result['video_gpu_after']=gpu_snapshot(result['providers'],args.gpu_transfer_spec,args.policy_probe_path,process_identities(),owned)
                            if args.measure_all_videos:
                                result['video_measurements'] = []
                                video_handles=client.video_handles()
                                if not video_handles:raise RuntimeError('No owned video fixture found')
                                result['video_tab_count']=len(video_handles)
                                video_admission=None
                                if args.profile_video:
                                    from pipeline_profile import start as start_video_profile
                                    video_admission=start_video_profile(client,media=True)
                                    result['profile_interval']=[time.time()*1000,None]
                                try:
                                    for handle in video_handles:
                                        client.socket.settimeout(max(1, min(76, deadline - time.monotonic())))
                                        counters = lambda: subprocess.run(['sysctl', 'kern.sf_avd.stats'],
                                            capture_output=True, text=True).stdout.strip()
                                        if args.compositor_stats:
                                            from compositor_stats import start as start_stats, stop as stop_stats
                                            clock_admission=start_stats()
                                        entry = dict(cpu_before=dict(elapsed=time.monotonic()-start, ps=snapshot()),
                                                     avd_before=counters())
                                        result['video_measurements'].append(entry)
                                        if args.gpu_transfer_spec:
                                            from gpu_transfer_stats import snapshot as gpu_snapshot
                                            entry['gpu_before']=gpu_snapshot(result['providers'],args.gpu_transfer_spec,args.policy_probe_path,process_identities(),owned)
                                        entry['video'] = client.measure_video(handle)
                                        entry['avd_after'] = counters()
                                        entry['cpu_after'] = dict(elapsed=time.monotonic()-start, ps=snapshot())
                                        if args.gpu_transfer_spec:
                                            entry['gpu_after']=gpu_snapshot(result['providers'],args.gpu_transfer_spec,args.policy_probe_path,process_identities(),owned)
                                        if args.compositor_stats:entry['compositor_stats']=stop_stats(clock_admission)
                                finally:
                                    if video_admission:
                                        result['profile_interval'][1]=time.time()*1000
                                        from pipeline_profile import stop as stop_video_profile
                                        result['pipeline_profile']=stop_video_profile(client,args.run,video_admission)
                                        result['video_profile_diagnostic_only']=True
                                deadline = min(deadline, time.monotonic() + 2)
                        except Exception as error:
                            result['marionette_error'] = str(error)
                            automation_failed = True
                            deadline = min(deadline, time.monotonic() + 2)
                        finally:
                            try:
                                if not session_started:
                                    raise RuntimeError('Session handshake incomplete; connection closed without further commands')
                                if automation_failed:
                                    raise RuntimeError('Automation failed; connection closed without further commands or stale replies')
                                client.socket.settimeout(5)
                                def capture_page(index, url):
                                    shot = args.run / ('page-%d.png' % index)
                                    captured = subprocess.run(['/usr/bin/chonkstep-darwin-desktop', '--screencapture', str(shot)],
                                        capture_output=True, text=True, timeout=15, env=env)
                                    result.setdefault('page_captures', []).append(dict(url=url, status=captured.returncode, path=str(shot)))
                                    if args.capture_renderer or args.gpu_presentation:
                                        from renderer_capture import capture as capture_renderer
                                        result.setdefault('renderer_page_captures', []).append(dict(url=url,
                                            **capture_renderer(args.run / ('page-%d-renderer.ppm' % index))))
                                pages = client.pages(capture_page if args.capture_pages else None)
                                (args.run / 'pages.json').write_text(json.dumps(pages, indent=2) + '\n')
                            except Exception as error:
                                result['page_diagnostics_error'] = str(error)
                            client.close()
                time.sleep(.05 if args.gpu_startup_fault and not gpu_fault_injected else 1)
            result['exit_before_cleanup'] = p.poll()
            result['elapsed_before_cleanup'] = time.monotonic() - start
            result['samples'] = samples
            if p.poll() is None:
                shot = args.run / 'screen.png'
                capture = subprocess.run(['/usr/bin/chonkstep-darwin-desktop', '--screencapture', str(shot)],
                                         capture_output=True, text=True, timeout=15, env=env)
                result['capture'] = dict(status=capture.returncode, stdout=capture.stdout, stderr=capture.stderr)
                if args.capture_renderer or args.gpu_presentation:
                    from renderer_capture import capture as capture_renderer
                    result['renderer_capture'] = capture_renderer(args.run / 'screen-renderer.ppm')
        except BaseException as error:
            automation_failed = True
            result['interruption'] = repr(error)
            raise
        finally:
            # Refresh ownership before signalling. No global name matching or pkill.
            if p.poll() is None:
                identities = process_identities()
                owned.update({pid: identities[pid][1] for pid in descendants(p.pid, identities)
                              if pid in identities})
            result['owned_pids'] = sorted(owned)
            for sig in [signal.SIGTERM, signal.SIGKILL]:
                identities = process_identities()
                for pid in sorted(owned, reverse=True):
                    if pid not in identities or identities[pid][1] != owned[pid]:
                        continue
                    try:
                        os.kill(pid, sig)
                    except ProcessLookupError:
                        pass
                time.sleep(1)
            try:
                result['exit_after_cleanup'] = p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                result['cleanup_timeout'] = True
            if args.executable == 'firefox-sf':
                log_directory = (home / 'Library/Logs/SiliconForest/Firefox'
                                 if args.bundle == Path('/Applications/Firefox.app')
                                 else args.bundle.parent / 'logs')
                logs = [path for path in log_directory.glob('firefox-*-%d.log' % p.pid)
                        if path.stat().st_mtime >= wall_start - 1]
                if len(logs) == 1:
                    shutil.copy2(logs[0], args.run / 'launcher-runtime.log')
                    result['daily_runtime_log'] = str(logs[0])
                else:
                    result['daily_runtime_log_error'] = 'Expected one log for this owned launcher PID'
            try:
                result['owner_input_after']=owner_guard.check()
            except (OSError,RuntimeError) as error:
                automation_failed=True
                result['owner_input_invalidated']=str(error)
                (args.run/'qualification-invalidated.json').write_text(json.dumps(dict(valid=False,reason=str(error)))+'\n')
            result['automation_complete'] = session_started and not automation_failed
            result['browser_survived_run'] = result.get('exit_before_cleanup') is None
            result['measurement_passed'] = result['automation_complete'] and result['browser_survived_run']
            for field in ['benchmark', 'native_ui', 'popup_stress', 'jit_probe', 'stability', 'quality', 'streaming', 'streaming_dash', 'integration', 'panels', 'render_quality', 'video_fullscreen', 'providers', 'cgl_export_cache', 'scene_reference', 'compositor_pacing', 'pinch_zoom', 'legibility', 'page_down_captures', 'composition_workloads']:
                if field in result:
                    result['measurement_passed'] &= bool(result[field].get('complete'))
            videos = [entry['video'] for entry in result.get('video_measurements', [])]
            if 'video_measurement' in result:
                videos.append(result['video_measurement'])
            for video in videos:
                result['measurement_passed'] &= bool(video.get('ended')) and not video.get('error') and not video.get('playError')
            (args.run / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k not in ['samples', 'prefs']}, indent=2))
    return 0 if result['measurement_passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
