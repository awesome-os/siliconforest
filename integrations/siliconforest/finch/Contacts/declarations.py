#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Author Contacts declarations from Finch's .m signatures and method macros.

Run from the repo root with the pinned Finch checkout; no SDK metadata is read.
"""
from pathlib import Path
import re
import subprocess
import sys

here = Path(__file__).resolve().parent
source = Path(sys.argv[1])
classes = dict.fromkeys('CNLabeledValue CNContact CNPhoneNumber CNPostalAddress CNContactRelation CNSocialProfile CNInstantMessageAddress CNGroup CNContainer CNContactProperty CNContactVCardSerialization CNFetchRequest CNFetchResult CNSaveRequest CNChangeHistoryEvent CNContactStore CNContactsUserDefaults'.split(), 'NSObject')
classes.update(CNMutableContact='CNContact', CNMutablePostalAddress='CNPostalAddress', CNMutableGroup='CNGroup', CNContactFormatter='NSFormatter', CNPostalAddressFormatter='NSFormatter', CNContactFetchRequest='CNFetchRequest', CNChangeHistoryFetchRequest='CNFetchRequest')
store = (source / 'CNContactStore.m').read_text()
events = re.findall(r'EVENT_CLASS\((CN\w+),\s*(\w+),\s*(?:YES|NO),\s*(.*?)\)\s*(?=EVENT_CLASS|/\*)', store, re.S)
classes['CNChangeHistoryDropEverythingEvent'] = 'CNChangeHistoryEvent'
classes.update({name: 'CNChangeHistoryEvent' for name, _, _ in events})
out = here / 'include/Contacts/Contacts.h'
subprocess.run([sys.executable, str(here.parent / 'tools/declarations.py'), str(source), str(here / 'types.h'), str(out), '--categories', '--class-map', ','.join(f'{name}:{base}' for name, base in classes.items())], check=True)
header = out.read_text()
# Shared private initializers carry ARC init-family attributes in Contacts_Finch.h.
header = re.sub(r'^.*_finchInitWithIdentifier:.*\n', '', header, flags=re.M)
header = re.sub(r'^.*\)sel(?:;|:.*;)\n', '', header, flags=re.M)
contact = (source / 'CNContact.m').read_text()
for cls, macros in [('CNContact', [('STRING_GETTER', 'NSString *'), ('ARRAY_GETTER', 'NSArray *')]), ('CNMutableContact', [('STRING_SETTER', 'NSString *'), ('ARRAY_SETTER', 'NSArray *')])]:
    methods = []
    for macro, typ in macros:
        for sel in re.findall(r'^'+macro+r'\((\w+),', contact, re.M):
            methods.append(f'- ({typ}){sel};' if 'GETTER' in macro else f'- (void){sel}:({typ})value;')
    header = header.replace(f'@interface {cls} : {classes[cls]}\n', f'@interface {cls} : {classes[cls]}\n'+'\n'.join(methods)+'\n')
for name, _, body in events:
    methods = re.findall(r'-\s*\([^)]*\)\s*\w+(?=\s*\{)', body)
    header = header.replace(f'@interface {name} : CNChangeHistoryEvent\n', f'@interface {name} : CNChangeHistoryEvent\n'+';\n'.join(methods)+';\n')
props = '''
@property(copy) NSData *startingToken;
@property(copy) NSArray *additionalContactKeyDescriptors;
@property BOOL shouldUnifyResults, mutableObjects, includeGroupChanges;
@property(copy) NSArray *excludedTransactionAuthors;
'''
header = header.replace('@interface CNChangeHistoryFetchRequest : CNFetchRequest\n', '@interface CNChangeHistoryFetchRequest : CNFetchRequest\n'+props)
# These protocols are implemented in Finch, and are needed for ARC conversions.
for name in ['CNLabeledValue', 'CNContact', 'CNPhoneNumber', 'CNPostalAddress', 'CNContactRelation', 'CNSocialProfile', 'CNInstantMessageAddress', 'CNGroup', 'CNContainer', 'CNContactProperty', 'CNChangeHistoryEvent']:
    header = header.replace(f'@interface {name} : {classes[name]}\n', f'@interface {name} : {classes[name]} <NSCopying, NSSecureCoding>\n')
for name in ['CNContact', 'CNPostalAddress', 'CNGroup']:
    header = header.replace(f'@interface {name} : NSObject <NSCopying, NSSecureCoding>', f'@interface {name} : NSObject <NSCopying, NSMutableCopying, NSSecureCoding>')
out.write_text(header)
