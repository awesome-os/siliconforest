#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Author declarations from Finch's implementation, with own types and ABI values.

Numeric constants checked against objc2-open-directory 0.3.2 (the binding's source).
No Apple SDK input. Usage: declarations.py sources/finch/userland/OpenDirectory
"""
from pathlib import Path
import re
import subprocess
import sys

base = Path(__file__).resolve().parent
source = Path(sys.argv[1])
code = (source / 'CFOpenDirectory.c').read_text()
values = {'kODNodeTypeAuthentication':0x2201, 'kODNodeTypeLocalNodes':0x2200,
 'kODNodeTypeNetwork':0x2205, 'kODNodeTypeContacts':0x2204,
 'kODMatchEqualTo':0x2001, 'kODMatchBeginsWith':0x2002,
 'kODMatchContains':0x2004, 'kODMatchEndsWith':0x2003,
 'kODErrorNodeUnknownName':2000, 'kODErrorNodeUnknownType':2001,
 'kODErrorQueryInvalidMatchType':3100, 'kODErrorRecordParameterError':4100,
 'kODErrorRecordNoLongerExists':4104, 'kODErrorRecordAttributeUnknownType':4200,
 'kODErrorRecordAttributeValueSchemaError':4202, 'kODErrorCredentialsInvalid':5000,
 'kODErrorCredentialsNotAuthorized':5101, 'kODErrorCredentialsServerError':5202,
 'kODErrorCredentialsPasswordQualityFailed':5402}
header = '''/* SPDX-License-Identifier: MIT */
/* Own declarations from Finch implementation signatures; no SDK input. */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t ODNodeType, ODMatchType;
typedef CFIndex ODFrameworkErrors;
#ifdef __OBJC__
#import <Foundation/Foundation.h>
typedef NSString *ODAttributeType, *ODRecordType;
#else
typedef CFStringRef ODAttributeType, ODRecordType;
#endif
typedef struct __ODSession *ODSessionRef;
typedef struct __ODNode *ODNodeRef;
typedef struct __ODRecord *ODRecordRef;
enum {
'''+''.join(f'    {n} = {v},\n' for n,v in values.items())+'};\n'
manual = re.findall(r'^const (CFStringRef|ODRecordType|ODAttributeType) (\w+) =', code, re.M)
header += ''.join(f'extern const {t} {n};\n' for t,n in manual)+'extern ODSessionRef kODSessionDefault;\n'
generated = re.findall(r'NSString \*const (\w+) =', (source / 'CFOpenDirectoryConstants_Generated.m').read_text())
header += '#ifdef __OBJC__\n'+''.join(f'extern NSString *const {n};\n' for n in generated)+'#else\n'+''.join(f'extern const CFStringRef {n};\n' for n in generated)+'#endif\n'
for typ,n,args in re.findall(r'^((?:CF\w+|OD\w+|bool)\s*\*?)\s+(OD\w+)\(([^{}]*?)\)\s*\{', code, re.M):
    header += typ+' '+n+'('+re.sub(r'\s+', ' ', args)+');\n'
(base / 'include/CFOpenDirectory/CFOpenDirectory.h').write_text(header)
(base / 'types.h').write_text('''/* SPDX-License-Identifier: MIT */
#import <CFOpenDirectory/CFOpenDirectory.h>
@protocol ODQueryDelegate <NSObject>
- (void)query:(ODQuery *)query foundResults:(NSArray *)results error:(NSError *)error;
@end
'''+''.join(f'extern NSString *const {n};\n' for n in re.findall(r'NSString \*const (\w+) =', (source / 'OpenDirectoryConstants_Generated.m').read_text())))
out = base / 'include/OpenDirectory/OpenDirectory.h'
subprocess.run([sys.executable,str(base.parent / 'tools/declarations.py'),str(source),str(base / 'types.h'),str(out),'--class-map','ODSession:NSObject,ODNode:NSObject,ODRecord:NSObject,ODQuery:NSObject'], check=True)
text = out.read_text()
for name in ['ODSession','ODNode']:
    text = text.replace('@interface '+name+' : NSObject\n', '@interface '+name+' : NSObject { id _internal; }\n')
out.write_text(text)
