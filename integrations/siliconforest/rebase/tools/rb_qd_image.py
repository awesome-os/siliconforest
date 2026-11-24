#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The QEMU stream's qd_image.py, unmodified, with its output root at runs/qemu-dev-rebase.

qd_image.py keeps three things under runs/qemu-dev/base: its remembered Nix answers
(inputs.json), the template image (it deletes every older template of the same kernel and
variant when it makes one) and work directories. A run on another base image through the
stream's own entry point would evict that stream's template each time. This imports the
module as it is and changes one name, OUT, before calling its main(); same arguments.
QD_BASE_IMAGE (that tool's own override) names the tip image. SF_PD is removed from the
environment: everything qd_image.py asks Nix for is the pin's on purpose (the M1's audio
server and tools, as staged today); what the tip replaces is passed with --file.
"""
import os
import sys
from pathlib import Path

LAB = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(LAB / 'integrations/siliconforest/qemu-dev'))
os.environ.pop('SF_PD', None)
import qd_image  # noqa: E402

qd_image.OUT = LAB / 'runs/qemu-dev-rebase'
qd_image.ENV.pop('SF_PD', None)
if __name__ == '__main__':
    if not os.environ.get('QD_BASE_IMAGE'):
        sys.exit('rb_qd_image.py: QD_BASE_IMAGE must name the base image (rb_qemu.sh sets it)')
    qd_image.main()
