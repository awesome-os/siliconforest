#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only local SSH hold gate. A quoted completion marker never clears a hold."""
import argparse
from datetime import datetime, timedelta, timezone
from pathlib import Path
import re


def pending_hold(text, install_reservation=False, now=None, max_seconds=None):
    now = now or datetime.now(timezone.utc)
    active = {}
    desktop_before_reboot = {}
    offered_until = {}
    owners = '[A-Z][A-Z0-9_-]*'
    agent_names = set(re.findall(r'^\S+\s+('+owners+r')[: ]', text, re.M))
    for line in text.splitlines():
        # Completion must be the actual agent event, not a phrase inside a notice.
        event = re.match(r'^\S+\s+('+owners+r')[: ]+(.+)$', line)
        if event:
            owner, body = event[1], event[2]
            lower_body = body.lower()
            # The kernel owner also uses an undated two-minute install notice.
            # Refuse new work at that announcement, before a reboot command.
            if re.match(r'^\d+-min install notice\b',lower_body):
                active[(owner,'install-notice')] = (line,[],owner)
            # A coordinator-declared frozen desktop blocks even CLI media
            # work until the designated recovery owner confirms restoration.
            # Do not infer recovery from time passing or a physical lock.
            if owner == 'COORDINATOR' and lower_body.startswith('gpu device slots exhausted'):
                active[(owner, 'devfs-recovery')] = (line, ['M1 back in SiliconForest (devfs clone-slot fix installed)'], 'CODEX-TB')
            if owner == 'COORDINATOR' and lower_body.startswith('desktop frozen'):
                active[(owner, 'recovery-freeze')] = (line, ['M1 back in SiliconForest'], 'CODEX-TB')
            elif owner == 'COORDINATOR' and lower_body.startswith('m1 back in siliconforest'):
                active.pop((owner, 'recovery-freeze'), None)
            # A kernel owner's dated reservation blocks new work immediately.
            # It is not an actual reboot epoch and has no timeout-based release.
            scheduled = re.match(r'^notice\s+.+?\bearliest\s*(?:\d{4}-\d{2}-\d{2}t)?(\d{2}):(\d{2})(?::(\d{2}))?z\b', lower_body)
            if scheduled:
                active[(owner, 'scheduled-reboot')] = (line, [], owner)
                offered_until.pop(owner, None)
                # A reservation may explicitly offer bounded work before its
                # earliest boot. This is permission from that actual notice,
                # never an inferred expiration or a fabricated completion.
                compact = re.sub(r'\s+', '', lower_body)
                offer = re.search(r'(?:^|[.;])otheragentsmayusem1normallybefore(\d{2}):(\d{2})(?::(\d{2}))?z?(?:[.;,]|$)', compact)
                if offer:
                    try:
                        announced = datetime.fromisoformat(line.split()[0].replace('Z', '+00:00'))
                        deadline = announced.replace(hour=int(offer[1]), minute=int(offer[2]), second=int(offer[3] or 0), microsecond=0)
                        earliest = announced.replace(hour=int(scheduled[1]), minute=int(scheduled[2]), second=int(scheduled[3] or 0), microsecond=0)
                        explicit_date = re.search(r'\bearliest\s*(\d{4}-\d{2}-\d{2})t', lower_body)
                        if explicit_date:
                            earliest = datetime.fromisoformat(explicit_date[1]+'T'+earliest.strftime('%H:%M:%S')+'+00:00')
                        dated = re.search(r'\d{4}-\d{2}-\d{2}t', scheduled[0])
                        if dated:
                            earliest = datetime.fromisoformat(dated[0][:-1]+'T'+earliest.strftime('%H:%M:%S')+'+00:00')
                        if announced <= now and announced < deadline <= earliest:
                            offered_until[owner] = deadline
                    except ValueError:
                        pass
            elif ((owner, 'scheduled-reboot') in active and
                  lower_body.startswith('scheduling correction:') and
                  re.search(r'earliest\s*[a-z0-9_-]+\s*\d{2}:\d{2}(?::\d{2})?z\b', lower_body)):
                active[(owner, 'scheduled-reboot')] = (line, [], owner)
                offered_until.pop(owner, None)
            elif ((owner, 'scheduled-reboot') in active and
                  (owner, 'reboot') not in active and lower_body.startswith('ack ') and
                  'willwaitcoreshbeforeguiex/bench' in re.sub(r'\s+', '', lower_body) and
                  'normaldesktopavailabletoothers' in re.sub(r'\s+', '', lower_body)):
                # Explicit owner stand-down while another owner's offline QEMU
                # rehearsal holds core.lock. This ends only the reservation,
                # not a real reboot, guarded install or desktop session hold.
                active.pop((owner, 'scheduled-reboot'), None)
            # A physical flock alone does not end an announced desktop install.
            # Only the same owner's actual release/correction closes its hold.
            if ((lower_body.startswith('gui.lock: install ') and
                    ('compositor' in lower_body or 'relaunch' in lower_body)) or
                    lower_body.startswith('gui.lock: compositor relaunch')):
                active[(owner, 'desktop-install')] = (line, [], owner)
            if (lower_body.startswith('gui.lock released') or
                    lower_body.startswith('checks done, gui.lock released;') or
                    lower_body.startswith('correction: gui.lock was released at the end of the install') or
                    (lower_body.startswith('done (runs/m1-coord/done-') and
                     'm1 left on the installed daily compositor' in lower_body)):
                active.pop((owner, 'desktop-install'), None)
            if (lower_body.startswith('gui.lock: start ') and 'idle' in lower_body and
                    ('measurement' in lower_body or 'cpu/rss check' in lower_body)):
                duration = re.search(r'(\d+(?:\.\d+)?)\s*(?:-minute|minutes?|min)\b', lower_body)
                try:
                    started = datetime.fromisoformat(line.split()[0].replace('Z', '+00:00'))
                except ValueError:
                    started = None
                if started and duration and now < started + timedelta(minutes=float(duration[1]), seconds=30):
                    active[(owner, 'timed-idle')] = (line, [], owner)
            if lower_body.startswith('idle a/b start '):
                active[(owner, 'measurement')] = (line, [], owner)
            elif (lower_body.startswith('idle-') and
                  'owned uart reader stopped before gui.lock release' in lower_body):
                active.pop((owner, 'measurement'), None)
                active.pop((owner, 'reboot'), None)
            if lower_body.startswith('gui.lock released'):
                active.pop((owner, 'gpu-session'), None)
            elif ('announced gpu tests' in lower_body and 'did not run' in lower_body
                  and 're-announced' in lower_body):
                active.pop((owner, 'gpu-session'), None)
            elif lower_body.startswith(('gpu tests postponed', 'postponed:')):
                active.pop((owner, 'gpu-session'), None)
            elif lower_body.startswith('in ') and 'risky gpu test' in lower_body:
                # Some notices explicitly offer the slot to an install that
                # claims gui.lock first. Only that locked installer may reserve
                # the offer; an actual GPU run remains a separate hold below.
                if not (install_reservation and 'skipped if an install starts' in lower_body):
                    active[(owner, 'gpu-session')] = (line, [], owner)
            if lower_body.startswith('cancelled ') and 'no reboot' in lower_body:
                active.pop((owner, 'explicit'), None)
                active.pop((owner, 'reboot'), None)
                active.pop((owner, 'scheduled-reboot'), None)
                desktop_before_reboot.pop(owner, None)
            for key, (notice, markers, release_owner) in list(active.items()):
                if release_owner in (None, owner) and any(body.startswith(m) for m in markers):
                    del active[key]
            if (lower_body.startswith(('core install finished', 'guarded core install finished', 'guarded install finished', 'libobjc install finished')) or
                re.fullmatch(r'core\.lock released(?: \([^()\n]*\))?', lower_body) or
                (lower_body.startswith('core.lock released:') and ' installed' in lower_body)):
                active.pop((owner, 'core'), None)
                active.pop((owner, 'core-announced'), None)
            elif re.match(r'^(?:core\.lock:\s*)?guarded(?: core)? install\b', lower_body):
                # Actual guarded pipelines serialize on the shared core.lock.
                # A later owner's actual start supersedes a missing historical
                # end notice; future announcements must remain independent.
                for key in list(active):
                    if key[1] == 'core':
                        del active[key]
                active[(owner, 'core')] = (line, [], owner)
            elif re.match(r'^in\b.*guarded(?: core)? install\b', lower_body):
                active[(owner, 'core-announced')] = (line, [], owner)
            requested_boot = (owner == 'CODEX-TB' and lower_body.startswith('install start ')
                              and '/bin/reboot requested' in lower_body)
            if (lower_body.startswith('m1 back in siliconforest') or
                (lower_body.startswith('install end ') and 'm1 back in siliconforest' in lower_body) or
                re.fullmatch(r'[a-z0-9-]+ recovered [a-z0-9-]+; m1 back in siliconforest', lower_body) or
                (lower_body.startswith('idle-') and ' restored ' in lower_body and
                 lower_body.endswith('; m1 back in siliconforest'))):
                active.pop((owner,'install-notice'),None)
                if owner in {'CODEX-TB', 'COORDINATOR'}:
                    active.pop(('COORDINATOR', 'recovery-freeze'), None)
                # A verified matching reboot ends old desktop processes even
                # when their owner omitted an end notice. Preserve holds that
                # started during recovery, all core pipelines, and all holds
                # when no actual reboot start was recorded for this owner.
                if (owner, 'reboot') in active:
                    active.pop((owner, 'scheduled-reboot'), None)
                    for key, notice in desktop_before_reboot.pop(owner, {}).items():
                        if key in active and active[key][0] == notice:
                            del active[key]
                active.pop((owner, 'reboot'), None)
                active.pop((owner, 'gpu'), None)
                active.pop((owner, 'gpu-session'), None)
            elif requested_boot or any(phrase in lower_body for phrase in
                     ['m1 reboot', 'siliconforest rebooting', 'clean reboot to linux', 'reboot now',
                      'will reboot the m1']):
                if requested_boot or re.match(r'^(?:[a-z0-9-]+ )?(?:clean reboot to linux|m1 reboot now|reboot now|siliconforest rebooting)\b', lower_body):
                    desktop_before_reboot.setdefault(owner, {
                        key: value[0] for key, value in active.items()
                        if key[1] == 'desktop-install'
                    })
                active[(owner, 'reboot')] = (line, [], owner)
            if lower_body.startswith('risky gpu test ') and ' finished:' in lower_body:
                active.pop((owner, 'gpu'), None)
            elif lower_body.startswith('risky gpu test over ssh'):
                active[(owner, 'gpu')] = (line, [], owner)
        lower = line.lower()
        if 'stay off' in lower and 'until' in lower:
            suffix = line[lower.index('until')+5:]
            quoted = re.findall(r"['\"]([^'\"]+)['\"]", suffix)
            selected = []
            for marker in quoted:
                if not any(word in marker.lower() for word in ('back', 'done', 'siliconforest')):
                    continue
                prefix = marker.split(' ', 1)[0]
                selected.append(marker.removeprefix(prefix+' ') if prefix in agent_names else marker)
            if selected:
                owner = event[1] if event else None
                active[(owner, 'explicit')] = (line, selected, None)
        if 'CODEX received coordinator M1 SSH hold;' in line:
            active[(None, 'explicit')] = (line, ['M1 back in SiliconForest'], None)
    pending = []
    for (owner, kind), (notice, _, _) in active.items():
        if (kind == 'scheduled-reboot' and max_seconds is not None and
            max_seconds > 0 and owner in offered_until and
            now + timedelta(seconds=max_seconds) <= offered_until[owner]):
            continue
        pending.append(notice)
    return '\n'.join(pending) or None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--log', type=Path, default=Path('runs/m1-coord/log.txt'))
    ap.add_argument('--install-reservation', action='store_true',
                    help='Caller holds gui.lock and may claim an explicitly offered future slot')
    ap.add_argument('--max-seconds', type=int,
                    help='Actual caller runtime bound; may use an explicitly offered pre-boot window')
    args = ap.parse_args()
    if args.max_seconds is not None and not 1 <= args.max_seconds <= 600:
        ap.error('--max-seconds must be between1 and600')
    pending = pending_hold(args.log.read_text(), install_reservation=args.install_reservation,
                           max_seconds=args.max_seconds)
    if pending:
        print('M1 SSH hold active; no connection attempted. '+pending)
        return 77
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
