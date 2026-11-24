# SPDX-License-Identifier: MIT
"""Bounds admission only; actual frame0 pixels must admit coordinates/orientation."""


def parse(value):
    try:parts=[int(x) for x in value.split(',')]
    except (AttributeError,ValueError):raise ValueError('Seven explicit integer geometry fields required') from None
    if len(parts)!=7 or ','.join(map(str,parts))!=value:raise ValueError('Canonical seven-field geometry required')
    fw,fh,x,y,w,h,direction=parts
    if not (1<=fw<=8192 and 1<=fh<=8192 and 0<=x<=fw and 0<=y<=fh and
            160<=w<=fw-x and 90<=h<=fh-y and direction in [-1,1]):
        raise ValueError('Geometry exceeds its framebuffer or orientation contract')
    return parts
