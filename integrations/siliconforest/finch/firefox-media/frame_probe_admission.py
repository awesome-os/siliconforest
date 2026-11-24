# SPDX-License-Identifier: MIT
"""Only admit candidate marker sampling from our own completed native frame0."""
import re
from frame_marker import decode_levels


def native_zero(text,parent,geometry):
    fw,fh,x,y,w,h,direction=geometry
    expected_bytes=8*((1816*w+960)//1920-(88*w+960)//1920)
    matches=[]
    for line in text.splitlines():
        if not line.startswith(('FXFRAMEID ','FXFRAMEID_PREVIEW ')):continue
        f=dict(re.findall(r'([A-Za-z0-9_]+)=(\S+)',line))
        if line.startswith('FXFRAMEID_PREVIEW ') and f.get('log_buffered')!='1':continue
        if int(f.get('pid',0))!=parent or f.get('frame')!='0':continue
        if f.get('size')!=f'{fw}x{fh}' or f.get('gl_error')!='0' or f.get('reason')!='ok':continue
        if int(f.get('readback_bytes',0))!=expected_bytes:continue
        values=[int(v) for v in f.get('rows','').split(',') if v]
        if len(values)!=36 or decode_levels([values[:18],values[18:]])!=0:continue
        matches.append(dict(native=line,geometry=geometry,independent_frame=0,
                            admitted_from_completed_owned_pixels=True,readback_bytes=expected_bytes))
    return matches[-1] if matches else None
