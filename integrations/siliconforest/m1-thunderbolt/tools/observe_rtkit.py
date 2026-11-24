#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Observe real ACK results only in SFAppleCIO's staged private RTKit copy."""
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text()
include = '#include "rtkit-internal.h"'
if source.count(include) != 1:
    raise ValueError('unexpected RTKit internal header site')
sites = [
    ('apple_rtkit_send_message(rtk, APPLE_RTKIT_EP_SYSLOG, msg, NULL, false);', 2),
    ('apple_rtkit_send_message(rtk, APPLE_RTKIT_EP_IOREPORT, msg,\n'
     '\t\t\t\t\t NULL, false);', 4),
]
# Validate every site before producing any changed source. A logger version
# change must be reviewed; never apply a loose replacement to other endpoints.
for old, _ in sites:
    if source.count(old) != 1:
        raise ValueError('unexpected ACK call site')
source = source.replace(include, include + '\n#include "sf_cio_rtkit.h"')
for old, endpoint in sites:
    call = old[:-1]
    source = source.replace(old, '{ int sf_ack_result = ' + call + ';\n'
                            '\t\tsf_cio_rtkit_observe_ack(rtk, ' + str(endpoint) +
                            ', sf_ack_result); }')
path.write_text(source)
