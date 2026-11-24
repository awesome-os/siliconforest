#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile canonical classes without redeclaring retained Foundation interfaces."""
from pathlib import Path
import re,sys
text=Path(sys.argv[1]).read_text()
macros=re.findall(r'^#define (NS\w+) SF\w+$',text,re.M)
pre=''.join('#define '+name+' SFPrevious'+name+'\n' for name in macros)
pre+='#import <Foundation/Foundation.h>\n'
pre+=''.join('#undef '+name+'\n' for name in macros)
text=re.sub(r'^#define NS\w+ SF\w+\n','',text,flags=re.M)
text=text.replace('#import <Foundation/Foundation.h>','',1)
Path(sys.argv[2]).write_text(pre+'#define SF_NETWORKING_CA_FILE "/System/Library/Frameworks/Foundation.framework/Versions/A/Resources/ca-bundle.crt"\n'+text)
