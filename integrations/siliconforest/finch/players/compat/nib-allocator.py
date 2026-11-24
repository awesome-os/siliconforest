#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Honor the existing Cocotron allocation hook in PD's Apple-archive reader.

Apply after the Foundation owner's application/networking patches. The current
record must be visible to the allocator; cache the allocated real class before
initialization so outlet cycles see the real object, not an undersized proxy.
"""
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
declaration='@interface NSObject (NSKeyedUnarchiverAwake)\n'
assert s.count(declaration)==1
s=s.replace(declaration,declaration+'+ (id)allocWithKeyedUnarchiver:(NSKeyedUnarchiver *)coder;\n')
old='''    id allocated = [cls alloc];
    CFDictionarySetValue(table, slot, allocated);
    [u->_containers addObject:record];
    result = [allocated initWithCoder:u];
    [u->_containers removeLastObject];'''
new='''    id allocated = nil;
    [u->_containers addObject:record];
    @try {
        allocated = [cls respondsToSelector:@selector(allocWithKeyedUnarchiver:)]
            ? [cls allocWithKeyedUnarchiver:u] : [cls alloc];
        if (allocated != nil) {
            CFDictionarySetValue(table, slot, allocated);
            result = [allocated initWithCoder:u];
        }
    } @finally {
        [u->_containers removeLastObject];
    }'''
assert s.count(old)==1,'Foundation allocation site changed; review before applying'
p.write_text(s.replace(old,new))
