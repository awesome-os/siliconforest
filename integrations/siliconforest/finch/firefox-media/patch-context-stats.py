#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Opt-in live-context metadata at create/import/pre-destroy; no decode changes."""
from pathlib import Path
import sys

p=Path(sys.argv[1]);s=p.read_text()
def replace(old,new):
    global s
    if s.count(old)!=1:raise ValueError('Original libsfavd anchor changed: '+old[:80])
    s=s.replace(old,new)

replace('\tstruct rec_ctx ctx[16];','\tstruct rec_ctx ctx[16];\n\tint sf_trace;\n\tuint64_t sf_generation[16];')
replace('sfavd_dev *\npdavd_open(', '#include "SFContextStatsTrace.h"\n\npdavd_dev *\npdavd_open(')
replace('''\tif (dev->fd < 0) {
\t\tfree(dev);
\t\treturn NULL;
\t}
\treturn dev;''','''\tif (dev->fd < 0) {
\t\tfree(dev);
\t\treturn NULL;
\t}
\tsf_context_init(dev);
\treturn dev;''')
replace('\treturn ioctl(dev->fd, SFAVD_IOC_CREATE, create);','''\tint result=ioctl(dev->fd, SFAVD_IOC_CREATE, create);
\tif(!result)sf_context_snapshot(dev,create->ctx,"create",1);
\treturn result;''')
replace('    return ioctl(dev->fd, SFAVD_IOC_IMPORT, &req);','''    int result=ioctl(dev->fd, SFAVD_IOC_IMPORT, &req);
    if(!result)sf_context_snapshot(dev,ctx,"import",0);
    return result;''')
replace('\treturn ioctl(dev->fd, SFAVD_IOC_DESTROY, &destroy);','''\tsf_context_snapshot(dev,ctx,"before-destroy",0);
\tint result=ioctl(dev->fd, SFAVD_IOC_DESTROY, &destroy);
\tif(!result&&ctx<16)__atomic_store_n(&dev->sf_generation[ctx],0,__ATOMIC_RELEASE);
\treturn result;''')
p.write_text(s)
