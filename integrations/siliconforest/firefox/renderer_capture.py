# SPDX-License-Identifier: MIT
"""Capture the compositor's actual framebuffer under the caller's GUI lock.

The ordinary screenshot endpoint reconstructs a CPU scene and omits external
GPU buffers. The existing inject capture command reads the renderer instead.
No input, renderer setting, or installed file is changed.
"""
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import time


def capture(destination):
    destination = Path(destination)
    inject = Path('/private/var/run/gpu-desktop/inject')
    from presentation_seat import owner_idle, functional_qa_enabled
    owner_idle(inject)
    name = 'sffx-frame-%d-%s' % (os.getpid(), secrets.token_hex(5))
    if destination.exists():
        raise FileExistsError(destination)
    if (inject / 'request').exists() or (inject / 'request.part').exists():
        raise RuntimeError('Another inject request is pending')
    (inject / 'done').unlink(missing_ok=True)
    with (inject / 'request.part').open('x') as stream:
        stream.write('capture ' + name + '\n')
    os.replace(inject / 'request.part', inject / 'request')
    until = time.monotonic() + 6
    while not (inject / 'done').exists() and time.monotonic() < until:
        time.sleep(.05)
    status = (inject / 'done').read_text()
    if not re.search(r'errors=0\b', status):
        raise RuntimeError('Renderer capture failed: ' + status)
    owner_idle(inject)
    owner_overlap = bool(re.search(r'owner_seen_active=1\b',status))
    if owner_overlap and not functional_qa_enabled():
        raise RuntimeError('Real owner input overlapped renderer capture; discard it')
    shutil.copy2(inject / (name + '.ppm'), destination)
    with Path('/var/log/desktop-session.log').open('rb') as stream:
        stream.seek(0, 2)
        stream.seek(max(0, stream.tell() - 2 * 1024 * 1024))
        log = stream.read().decode('utf-8', errors='replace')
    lines = [line for line in log.splitlines()
             if 'WMINJECT capture=' in line and name + '.ppm' in line]
    evidence = dict(path=str(destination), source='unverified', inject_status=status, log=lines,
                    owner_input_overlap=owner_overlap, functional_qa=functional_qa_enabled(),
                    timing_qualified=False)
    if lines and 'source=renderer' in lines[-1]:
        evidence['source'] = 'renderer'
    destination.with_suffix('.source.json').write_text(json.dumps(evidence, indent=2) + '\n')
    if evidence['source'] != 'renderer':
        raise RuntimeError('Actual renderer readback was not established')
    return evidence
