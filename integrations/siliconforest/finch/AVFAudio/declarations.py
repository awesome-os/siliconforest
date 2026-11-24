#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own source-derived device-independent classes; no SDK/catalogue input."""
from pathlib import Path
import subprocess,re,sys
here=Path(__file__).resolve().parent
classes={'AVAudioTime':'NSObject','AVAudioChannelLayout':'NSObject','AVAudioFormat':'NSObject','AVAudioBuffer':'NSObject','AVAudioPCMBuffer':'AVAudioBuffer','AVAudioCompressedBuffer':'AVAudioBuffer','AVAudioConverter':'NSObject','AVAudioFile':'NSObject'}
out=here/'include/AVFAudio/AVFAudio.h'
subprocess.run([sys.executable,str(here.parent/'tools/declarations.py'),sys.argv[1],str(here/'types.h'),str(out),'--class-map',','.join(k+':'+v for k,v in classes.items())],check=True)
s=out.read_text()
fields={'AVAudioTime':'AudioTimeStamp _ats; double _sampleRate;',
'AVAudioChannelLayout':'AudioChannelLayout *_layout; AudioChannelLayoutTag _layoutTag;',
'AVAudioFormat':'AudioStreamBasicDescription _asbd; AVAudioCommonFormat _commonFormat; AVAudioChannelLayout *_layout; void *_reserved;',
'AVAudioBuffer':'@protected void *_impl;', 'AVAudioConverter':'void *_impl;'}
for name,field in fields.items():s=s.replace('@interface '+name+' : '+classes[name]+'\n','@interface '+name+' : '+classes[name]+' { '+field+' }\n')
for name in ['AVAudioFormat','AVAudioChannelLayout']:
 s=s.replace('@interface '+name+' : NSObject {','@interface '+name+' : NSObject <NSCopying,NSSecureCoding> {')
# Publish the actual getters used in the source, including their property spellings.
s=s.replace('@interface AVAudioFormat :', '@interface AVAudioFormat :')
for name in ['AVAudioFormat','AVAudioTime']:
 start=s.index('@interface '+name+' :');end=s.index('@end',start)
 body=s[start:end]
 for method in re.findall(r'- \(BOOL\)(is[A-Z]\w*)\s*;',body):
  prop=method[2].lower()+method[3:]
  body+='@property(readonly,getter='+method+') BOOL '+prop+';\n'
 s=s[:start]+body+s[end:]
out.write_text(s)
for name in ['AVAudioFormat','AVAudioSettings','AVAudioBuffer','AVAudioConverter','AVAudioFile','AVAudioTime','AVAudioTypes']:
 (here/'include/AVFAudio'/(name+'.h')).write_text('/* SPDX-License-Identifier: MIT */\n#pragma once\n#import <AVFAudio/AVFAudio.h>\n')
