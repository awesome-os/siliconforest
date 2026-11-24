# SPDX-License-Identifier: MIT
"""Observe a fresh own profile immediately before the actual installed launch."""
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import subprocess

OUTER='/Applications/Firefox.app/Contents/MacOS/firefox-sf'


def checks(run,override=False):
    run=Path(run)
    pre=json.loads((run/'prelaunch-codec-profile.json').read_text()) if (run/'prelaunch-codec-profile.json').exists() else {}
    post=json.loads((run/'postlaunch-codec-profile.json').read_text()) if (run/'postlaunch-codec-profile.json').exists() else {}
    raw=json.loads((run/'browser/result.json').read_text())
    command=raw.get('command',[])
    actual=command[command.index('--profile')+1] if command.count('--profile')==1 else None
    return dict(prelaunch_absent=pre.get('passed') is True and pre.get('av1_key_absent') is True and pre.get('prefs_js_absent') is True,
        fresh_own_profile=pre.get('fresh_profile') is True and pre.get('profile')==actual and
            bool(actual) and actual.startswith('/private/var/db/sffx/') and actual.endswith('/'+run.name+'/browser/profile'),
        exact_environment=pre.get('av1_environment')==('1' if override else None),
        installed_codec_marker=post.get('codec_marker_applied') is True and post.get('actual_av1_value') is override,
        no_test_pref_assignment=post.get('test_pref_assigned') is False)


def inspect(profile,environment,override):
    profile=Path(profile)
    if not profile.is_dir() or profile.resolve()!=profile:
        raise ValueError('Fresh own profile is absent or aliased')
    settings=profile/'user.js';saved=profile/'prefs.js'
    if settings.is_symlink() or not settings.is_file() or saved.exists() or saved.is_symlink():
        raise ValueError('Fresh profile must have only the generated user.js and no prior prefs.js')
    data=settings.read_bytes()
    if b'media.av1.enabled' in data:
        raise ValueError('Test runner assigned AV1 before the installed launcher')
    expected='1' if override else None
    if environment.get('SFFX_AV1')!=expected:
        raise ValueError('Unexpected codec-policy environment override')
    return dict(fresh_profile=True,av1_key_absent=True,prefs_js_absent=True,
        profile=str(profile),user_js_sha256=hashlib.sha256(data).hexdigest(),
        av1_environment=expected,installed_outer=OUTER,passed=True)


@contextmanager
def observe(run,override=False):
    run=Path(run);original=subprocess.Popen;seen=[]
    class OwnPopen(original):
        def __init__(self,command,*args,**kwargs):
            if isinstance(command,(list,tuple)) and command and command[0]==OUTER:
                if seen or command.count('--profile')!=1 or '--no-remote' not in command:
                    raise ValueError('Exactly one fresh own no-remote installed launch required')
                profile=Path(command[command.index('--profile')+1])
                if profile!=run/'browser/profile':raise ValueError('Policy gate cannot reuse another profile')
                proof=inspect(profile,kwargs.get('env',{}),override)
                (run/'prelaunch-codec-profile.json').write_text(json.dumps(proof,indent=2)+'\n')
                seen.append(proof)
            super().__init__(command,*args,**kwargs)
    subprocess.Popen=OwnPopen
    try:yield
    finally:subprocess.Popen=original
