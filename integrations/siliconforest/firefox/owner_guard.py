# SPDX-License-Identifier: MIT
"""DESK7 real-input counters: idle now does not erase input during a run."""
from pathlib import Path
import re
import time


class OwnerInputGuard:
    def __init__(self, path=Path('/private/var/run/gpu-desktop/owner-input'), now_ms=None, require_idle=True):
        self.path=Path(path)
        self.require_idle=require_idle
        self.before=self.snapshot(now_ms)

    def snapshot(self, now_ms=None):
        text=self.path.read_text()
        fields={k:int(v) for k,v in re.findall(r'^([a-z_]+)=(\d+)$',text,re.M)}
        required={'active','window_ms','last_real_input_unix_ms','real_pointer_records','real_key_records','updated_unix_ms'}
        if 'DESK7' not in text or not required<=fields.keys():
            raise RuntimeError('Missing or unrecognized real-input guard')
        now=time.time()*1000 if now_ms is None else now_ms
        last=fields['last_real_input_unix_ms'] or fields['updated_unix_ms']
        if (fields['active'] not in [0,1] or fields['window_ms']<10000 or not 0<last<=now or
                not 0<fields['updated_unix_ms']<=now+1500 or
                (self.require_idle and (fields['active']!=0 or now-last<fields['window_ms']))):
            raise RuntimeError('Real owner input active or idle window unestablished')
        return fields

    def check(self, now_ms=None):
        after=self.snapshot(now_ms)
        if self.require_idle and any(after[k]!=self.before[k] for k in ['last_real_input_unix_ms','real_pointer_records','real_key_records']):
            raise RuntimeError('Real owner input overlapped this run; discard and stop')
        return after
