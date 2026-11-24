# SPDX-License-Identifier: MIT
"""Collect the compositor owner's existing stats interface during a locked run.

The global histograms reset at each stats request. Caller holds gui.lock and
admits only its own input; no compositor setting or process is changed. Counts
cover desktop commits, not individual Firefox buffers or DRM sequence numbers.
"""
from pathlib import Path
import time
import secrets
from presentation_seat import _request


def flush_command(command):
    inject=Path('/private/var/run/gpu-desktop/inject')
    name='sffx-clock-'+secrets.token_hex(8)
    # A unique subsequent dump proves this request was processed. An old done
    # file alone can race the compositor's request pickup/log writes.
    _request(inject,command+'\ndump '+name)
    until=time.monotonic()+3
    while not (inject/(name+'.txt')).exists() and time.monotonic()<until:
        time.sleep(.01)
    if not (inject/(name+'.txt')).exists():raise RuntimeError('Compositor stats acknowledgement absent')


def start():
    path=Path('/var/log/desktop-session.log')
    flush_command('stats')
    info=path.stat()
    return dict(path=str(path),inode=info.st_ino,offset=info.st_size,wall_start=time.time())


def stop(admission):
    path=Path(admission['path'])
    flush_command('stats')
    info=path.stat()
    if info.st_ino!=admission['inode'] or info.st_size<admission['offset']:
        raise RuntimeError('Compositor log rotated during the measurement')
    with path.open('rb') as stream:
        stream.seek(admission['offset']);raw=stream.read().decode(errors='replace')
    lines=[line for line in raw.splitlines() if line.startswith(('WMPACE ','WMLATENCY ','WMINJECT stats'))]
    return dict(admission,wall_end=time.time(),lines=lines,complete=any('commit_lead_ms' in line for line in lines),
        method='Existing inject stats resets/reads global compositor commit-lead and render-time histograms. Global desktop counts; not per-Firefox buffer or hardware-sequence evidence. No pacing setting changed.')
