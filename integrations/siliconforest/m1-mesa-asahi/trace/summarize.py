#!/usr/bin/env python3
# Condense an ioctl-trace.c log: call order with consecutive repeats collapsed, then totals.
import collections, re, sys

seq, tot = [], collections.Counter()
for l in open(sys.argv[1]).read().splitlines():
    if l.startswith('mmap'):
        k = 'mmap(device fd, len, PROT_READ|PROT_WRITE, MAP_SHARED, offset from GEM_MMAP_OFFSET)'
    else:
        m = re.match(r'ioctl fd=\d+ (0x[0-9a-f]+) dir=(\S+) size=(\d+) (\S+)(.*?) -> ', l)
        nm, d, extra = m.group(4), m.group(5), ''
        if nm in ('DRM_IOCTL_GET_CAP', 'DRM_IOCTL_SYNCOBJ_CREATE', 'DRM_IOCTL_ASAHI_GET_PARAMS'):
            extra = d
        elif nm in ('DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD', 'DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE'):
            extra = re.sub(r' \[.*', '', d)
        elif nm == 'DRM_IOCTL_SYNCOBJ_WAIT':
            extra = re.sub(r' count=\d+', '', d).replace('9223372036854775807', 'INT64_MAX')
        elif nm == 'DRM_IOCTL_ASAHI_SUBMIT':
            extra = re.sub(r' queue=\d+', '', d)
        elif nm == 'DRM_IOCTL_ASAHI_GEM_BIND_OBJECT':
            extra = re.sub(r' handle=\d+ vm=\d+', '', d)
        elif nm == 'DRM_IOCTL_ASAHI_VM_BIND':
            extra = ' op flags=' + re.search(r'op0\{flags=(0x[0-9a-f]+)', d).group(1)
        elif nm == 'DRM_IOCTL_ASAHI_GEM_CREATE':
            extra = ' flags=' + re.search(r'flags=(0x[0-9a-f]+)', d).group(1)
        k = f'{nm} {m.group(1)} ({m.group(2)}, {m.group(3)} bytes){extra}'
    tot[k.split(' ')[0].split('(')[0]] += 1
    if seq and seq[-1][0] == k:
        seq[-1][1] += 1
    else:
        seq.append([k, 1])
print('# call order, consecutive repeats collapsed (Linux ioctl numbers)')
for k, n in seq:
    print(f'{n:3d}x {k}')
print('\n# totals')
for k, n in sorted(tot.items(), key=lambda x: -x[1]):
    print(f'{n:3d} {k}')
