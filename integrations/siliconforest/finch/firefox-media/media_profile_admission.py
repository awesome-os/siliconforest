# SPDX-License-Identifier: MIT
"""Reuse only our completed1080 diagnostic's profile, never an owner's profile.

The normal no-remote launch and Marionette descendant/start-identity admission
remain required. A live Firefox still owns its profile lock; do not remove it.
"""
import json
from pathlib import Path


def admit(profile,private_root=Path('/private/var/db/sffx')):
    profile=Path(profile)
    if not profile.is_absolute() or '..' in profile.parts or not profile.is_relative_to(private_root):
        raise ValueError('Only our previous private media profile may be reused')
    parts=profile.relative_to(private_root).parts
    if len(parts)!=4 or not parts[0].startswith('firefox-media-cadence-packet-') or parts[1:]!=('youtube1080-run','browser','profile'):
        raise ValueError('Expected our exact1080 streaming profile layout')
    if not profile.is_dir() or profile.resolve()!=profile:
        raise ValueError('Profile absent or aliases another prefix')
    def read(path):
        if path.is_symlink() or not path.is_file() or path.stat().st_size>8_000_000:
            raise ValueError('Missing, aliased or oversized prior own-run receipt')
        return json.loads(path.read_text())
    prior=read(profile.parent/'result.json');quit=read(profile.parents[1]/'natural-quit.json')
    command=prior.get('command',[])
    if '--no-remote' not in command or command.count('--profile')!=1 or command[command.index('--profile')+1]!=str(profile):
        raise ValueError('Profile was not the recorded prior no-remote launch')
    if prior.get('automation_complete') is not True or prior.get('streaming',{}).get('complete') is not True:
        raise ValueError('Prior diagnostic did not complete')
    if any(type(prior.get(k)) is not int or prior[k]!=0 for k in ['exit_before_cleanup','exit_after_cleanup']):
        raise ValueError('Prior diagnostic did not exit naturally with status0')
    parent=prior.get('marionette',{}).get('capabilities',{}).get('moz:processID')
    if (type(parent) is not int or parent<=0 or quit.get('parent')!=parent or quit.get('requested') is not True or
            not quit.get('family_before') or quit.get('family_remaining')!=[] or quit.get('cleanup_signals')!=[]):
        raise ValueError('Prior family disappearance without cleanup was not observed')
    return profile
