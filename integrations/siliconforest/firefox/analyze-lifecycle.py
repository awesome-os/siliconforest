#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Qualify owned close/reopen evidence before the harness sends cleanup signals.

Audio callbacks are not acoustic evidence. Heartbeat loss and a vanished window
alone never qualify audio teardown. Missing observer coverage fails closed.
"""
import argparse
import json
from pathlib import Path
import re


def analyze(directory):
    result = json.loads((directory / 'result.json').read_text())
    action = result['action']
    output = dict(complete=False, action=action, acoustic_qualified=False,
                  installed_outer=result.get('installed_outer_or_dock', False), checks={})
    checks = output['checks']
    checks['installed_outer_launcher'] = output['installed_outer']
    begin, end = result.get('action_monotonic'), result.get('after_action_monotonic')
    checks['finite_precleanup_interval'] = bool(begin is not None and end is not None and
                                               end >= begin+5 and result.get('cleanup_monotonic', 0) > end)
    if not checks['finite_precleanup_interval']:
        return output
    rows = []
    log = directory / 'launcher-runtime.log'
    if not log.exists():
        log = directory / 'firefox.log'
    for line in log.read_text(errors='replace').splitlines():
        if not line.startswith(('SFMEDIA_AU ', 'SFMEDIA_CALLBACK ')):
            continue
        fields = dict(re.findall(r'([\w-]+)=([^\s]+)', line))
        if 'monotonic' in fields:
            fields['time'] = float(fields['monotonic'])
            fields['kind'] = line.split()[0]
            rows.append(fields)
    starts = [r for r in rows if r['kind'] == 'SFMEDIA_AU' and r.get('phase') == 'state' and
              r.get('event') == 'start' and r.get('status') == '0' and r['time'] < begin]
    checks['audio_unit_started_before_action'] = bool(starts)
    callbacks = [r for r in rows if r['kind'] == 'SFMEDIA_CALLBACK']
    checks['callbacks_ran_before_action'] = any(r['time'] < begin and int(r.get('successes', 0)) > 0 for r in callbacks)
    closing = action in ['close-tab', 'close-window', 'close-box', 'cmd-w', 'quit', 'desktop-quit', 'windowless-quit', 'connection-loss']
    owned_alive = result.get('after_action_owned_alive', {})
    owned_before = result.get('before_action_owned_identities', {})
    unit_checks = []
    for start in starts:
        key = (start['pid'], start['unit'])
        same = lambda row: (row.get('pid'), row.get('unit')) == key
        samples = [r for r in callbacks if same(r) and begin+1 <= r['time'] <= end]
        # Require continuing observer coverage; stopped logging is not stopped audio.
        covered = bool(len(samples) >= 4 and samples[-1]['time'] - samples[0]['time'] >= 1)
        counts = [int(r.get('calls', 0)) for r in samples]
        stops = [r for r in rows if same(r) and r['kind'] == 'SFMEDIA_AU' and
                 r.get('phase') == 'state' and r.get('event') in ['stop', 'uninitialize', 'dispose'] and
                 r.get('status') == '0' and begin <= r['time'] <= end]
        process_ended = bool(owned_before.get(key[0]) and owned_alive.get(key[0]) != owned_before[key[0]])
        stopped = process_ended or (covered and bool(stops) and len(set(counts)) == 1 and all(r.get('active') == '0' for r in samples))
        continuing = covered and counts[-1] > counts[0] and any(r.get('active') == '1' for r in samples)
        unit_checks.append(dict(pid=key[0], unit=key[1], coverage=covered, process_ended=process_ended,
                                callback_stopped=stopped, callback_continued=continuing))
    output['units'] = unit_checks
    checks['expected_audio_behavior'] = bool(unit_checks) and all(
        r['callback_stopped' if closing else 'callback_continued'] for r in unit_checks)
    content = result.get('before_content_identities', {})
    checks['closed_content_ended_or_preserved'] = bool(content) and all(content.values()) and all(
        (owned_alive.get(pid) != start if closing else owned_alive.get(pid) == start)
        for pid, start in content.items() if start is not None)
    admission = result.get('admission', {})
    window_id = admission.get('window_id')
    native = result.get('after_action_native_windows', '')
    own_rows = [line for line in native.splitlines() if re.match(r'window %s ' % window_id, line)]
    mapped = any('mapped=true' in line for line in own_rows)
    checks['native_window_observation'] = type(window_id) is int and 'stack [' in native
    checks['closed_window_not_mapped'] = not mapped if closing else True
    if action in ['hide', 'minimize']:
        checks['hidden_or_minimized_before_reopen'] = bool(own_rows) and all(
            'mapped=false' in line or ('lifecycle=' in line and 'lifecycle=Normal' not in line) for line in own_rows)
    reopened = result.get('after_reopen_windows', [])
    ending_process = action in ['quit', 'desktop-quit', 'windowless-quit', 'connection-loss']
    checks['reopen_available_or_quit'] = (not owned_alive if ending_process else bool(reopened))
    if action in ['desktop-quit', 'windowless-quit']:
        checks['compositor_quit_route'] = result.get('native_action', {}).get('route') == 'compositor-app-request'
        checks['natural_quit_success'] = result.get('exit_before_cleanup') == 0
    if action == 'windowless-quit':
        before_quit = result.get('prequit_native_windows', '')
        before_rows = [line for line in before_quit.splitlines() if re.match(r'window %s ' % window_id, line)]
        checks['window_closed_before_desktop_quit'] = ('stack [' in before_quit and
            not any('mapped=true' in line for line in before_rows))
    if action == 'connection-loss':
        loss = log.read_text(errors='replace')
        checks['single_owned_connection_lost'] = bool(re.search(r'SFFX_WAYLAND_LOSS pid=%d fd=\d+ shutdown=0 errno=0\b' % result.get('parent_pid', -1), loss))
        checks['fatal_connection_exit'] = result.get('exit_before_cleanup') == 70
    if not ending_process:
        checks['compositor_reopen_route'] = result.get('reopen_route') == 'compositor-app-request'
        checks['reopened_native_window_reachable'] = result.get('reopen_admission', {}).get('complete') is True
        checks['single_live_browser_after_reopen'] = len(reopened) == 1 and not reopened[0].get('closed', False)
        document_url = result.get('document', {}).get('url')
        tabs = [t for w in reopened for t in w.get('tabs', [])]
        checks['reopen_fresh_or_original_tabs'] = bool(tabs) and (
            all(t.get('uri') != document_url for t in tabs) if closing
            else any(t.get('uri') == document_url for t in tabs))
    checks['native_diagnostic_completed'] = result.get('diagnostic_complete') is True and not result.get('error')
    checks['actual_system_providers'] = result.get('providers', {}).get('complete') is True
    output['complete'] = all(checks.values())
    return output


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    args = ap.parse_args()
    result = analyze(args.directory)
    (args.directory / 'lifecycle-gate.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    return 0 if result['complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
