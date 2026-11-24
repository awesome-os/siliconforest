#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read only selected live AVE ADT metadata; no MMIO mapping or image bytes."""
import ctypes as C
import json
import os
import struct


def property_names(kit, entry):
    """Names/type lengths only; arbitrary binary property values stay on M1."""
    cf=C.CDLL('/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation')
    kit.IORegistryEntryCreateCFProperties.argtypes=[C.c_uint,C.POINTER(C.c_void_p),C.c_void_p,C.c_uint]
    kit.IORegistryEntryCreateCFProperties.restype=C.c_int
    cf.CFDictionaryGetCount.argtypes=[C.c_void_p];cf.CFDictionaryGetCount.restype=C.c_long
    cf.CFDictionaryGetKeysAndValues.argtypes=[C.c_void_p,C.c_void_p,C.c_void_p]
    cf.CFStringGetCString.argtypes=[C.c_void_p,C.c_void_p,C.c_long,C.c_uint32];cf.CFStringGetCString.restype=C.c_bool
    cf.CFRelease.argtypes=[C.c_void_p]
    cf.CFGetTypeID.argtypes=[C.c_void_p];cf.CFGetTypeID.restype=C.c_ulong
    cf.CFDataGetTypeID.restype=cf.CFStringGetTypeID.restype=cf.CFNumberGetTypeID.restype=C.c_ulong
    cf.CFDataGetLength.argtypes=[C.c_void_p];cf.CFDataGetLength.restype=C.c_long
    cf.CFStringGetLength.argtypes=[C.c_void_p];cf.CFStringGetLength.restype=C.c_long
    dictionary=C.c_void_p();result=kit.IORegistryEntryCreateCFProperties(entry,C.byref(dictionary),None,0)
    if result or not dictionary.value:return {'status':result,'available':False}
    try:
        count=cf.CFDictionaryGetCount(dictionary)
        if count<0 or count>512:raise ValueError('Property count exceeds metadata bound')
        keys=(C.c_void_p*count)();values=(C.c_void_p*count)()
        cf.CFDictionaryGetKeysAndValues(dictionary,keys,values)
        names=[];types={}
        selected={'firmware-version','dram-base','chip-id','pre-loaded','segment-ranges','segment-names'}
        for i,key in enumerate(keys):
            name=C.create_string_buffer(256)
            if cf.CFStringGetCString(key,name,len(name),0x08000100):
                label=name.value.decode('ascii');names.append(label)
                if label in selected:
                    kind=cf.CFGetTypeID(values[i]);entry={'kind':'other','type_id':kind}
                    if kind==cf.CFDataGetTypeID():entry={'kind':'data','bytes':cf.CFDataGetLength(values[i])}
                    elif kind==cf.CFStringGetTypeID():entry={'kind':'string','characters':cf.CFStringGetLength(values[i])}
                    elif kind==cf.CFNumberGetTypeID():entry={'kind':'number'}
                    types[label]=entry
        return {'status':0,'available':True,'names':sorted(names),'selected_types':types}
    finally:cf.CFRelease(dictionary)


def main():
    if os.uname().sysname!='Darwin':raise SystemExit('Run on the M1 SiliconForest root')
    kit=C.CDLL('/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit')
    kit.IORegistryEntryFromPath.argtypes=[C.c_uint,C.c_char_p];kit.IORegistryEntryFromPath.restype=C.c_uint
    kit.IORegistryEntryGetProperty.argtypes=[C.c_uint,C.c_char_p,C.c_void_p,C.POINTER(C.c_uint)];kit.IORegistryEntryGetProperty.restype=C.c_int
    kit.IOObjectRelease.argtypes=[C.c_uint]
    rows=[]
    for path in ['IODeviceTree:/arm-io/ave','IODeviceTree:/chosen']:
        entry=kit.IORegistryEntryFromPath(0,path.encode())
        item={'path':path,'entry_found':bool(entry),'properties':{}}
        try:
            if entry:
                item['property_inventory']=property_names(kit,entry)
                discovered=item['property_inventory'].get('names',[])
                keys=['name','compatible','pre-loaded','firmware-name','firmware-version','system-firmware-version',
                      'os-firmware-version','os-version','os-build-version','firmware-version-string','segment-ranges','segment-names']
                keys+=sorted(set(k for k in discovered if any(token in k.lower() for token in ['firmware','version','fw-name']))-set(keys))
                for key in keys:
                    buf=C.create_string_buffer(65536);length=C.c_uint(len(buf))
                    rc=kit.IORegistryEntryGetProperty(entry,key.encode(),buf,C.byref(length))
                    if rc:continue
                    if length.value>len(buf):raise ValueError('Registry output exceeds bound')
                    value=buf.raw[:length.value]
                    if key=='segment-ranges':
                        if len(value)%32 or len(value)>4*32:raise ValueError('Invalid segment-ranges')
                        item['properties'][key]={'byte_length':len(value),'segments':[
                            dict(zip(['physical','iova','remap','size','flags'],t)) for t in struct.iter_unpack('<QQQII',value)]}
                    elif key=='pre-loaded':item['properties'][key]=int.from_bytes(value,'little')
                    else:
                        # Unknown binary properties never leave the process.
                        try:
                            text=value.rstrip(b'\0').decode('ascii')
                            if len(value)>256 or any(ord(c)<32 or ord(c)>126 for c in text):raise UnicodeDecodeError('ascii',b'x',0,1,'not printable metadata')
                            item['properties'][key]=text
                        except UnicodeDecodeError:item['properties'][key]={'byte_length':len(value),'not_printed':True}
        finally:
            if entry:kit.IOObjectRelease(entry)
        rows.append(item)
    print(json.dumps(rows,indent=2))


if __name__=='__main__':main()
