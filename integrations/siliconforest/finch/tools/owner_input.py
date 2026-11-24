# SPDX-License-Identifier: MIT
"""Read DESK's real-input marker; absence or activity never grants permission."""
from pathlib import Path
import re
import time


def snapshot(path=Path('/private/var/run/gpu-desktop/owner-input'),now_ms=None,idle_ms=180000):
    values={key:int(value) for key,value in re.findall(r'^([a-z_]+)=(\d+)$',path.read_text(),re.M)}
    required={'active','window_ms','last_real_input_unix_ms','real_pointer_records','real_key_records','updated_unix_ms'}
    if not required<=values.keys():raise RuntimeError('Malformed owner-input marker')
    now=time.time()*1000 if now_ms is None else now_ms
    last=values['last_real_input_unix_ms'] or values['updated_unix_ms']
    if (values['active']!=0 or values['window_ms']<10000 or
        not 0<values['updated_unix_ms']<=now+1500 or not 0<last<=now or
        now-last<max(idle_ms,values['window_ms'])):
        raise RuntimeError('Owner active or required idle period not established')
    return values


class OwnerInputGuard:
    def __init__(self,path=Path('/private/var/run/gpu-desktop/owner-input'),now_ms=None,idle_ms=180000):
        self.path=path;self.idle_ms=idle_ms;self.before=snapshot(path,now_ms,idle_ms)

    def check(self,now_ms=None):
        after=snapshot(self.path,now_ms,self.idle_ms)
        if any(after[key]!=self.before[key] for key in
               ['last_real_input_unix_ms','real_pointer_records','real_key_records']):
            raise RuntimeError('Real owner input observed during our run')
        return after


class OwnerActivityRecorder:
    """Owner lifted idle waits 2026-11-16 00:25Z; record activity, never wait.

    Used by current media CLI diagnostics. The strict historical guard above
    remains available for reproducing old policies, not current authorization.
    Concurrent owner input can affect timing qualification, not permission.
    """
    def __init__(self,path=Path('/private/var/run/gpu-desktop/owner-input')):
        self.path=path;self.before=self.check()

    def check(self,now_ms=None):
        del now_ms
        try:
            text=self.path.read_text()
            values={key:int(value) for key,value in re.findall(r'^([a-z_]+)=(\d+)$',text,re.M)}
            required={'active','last_real_input_unix_ms','real_pointer_records','real_key_records'}
            if not required<=values.keys():raise RuntimeError('Malformed owner-input marker')
            return values
        except (OSError,RuntimeError) as error:
            return dict(marker_unavailable=type(error).__name__,reason=str(error))
