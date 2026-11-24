#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own headless QEMU only: put its xHCI behind a PCIe root port."""
import re,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'qemu-dev'))
import qd_run

original=qd_run.qemu_command
def command(args,out):
    cmd=original(args,out)
    at=cmd.index('qemu-xhci,id=xhci')
    cmd[at]='qemu-xhci,id=xhci,bus=upstream-port'
    cmd[at-1:at-1]=['-device','pcie-root-port,id=upstream-port,chassis=2']
    return cmd
qd_run.qemu_command=command
qd_run.PROMPT=re.compile(rb'# ?(\x1b\[[0-9;?]*[a-zA-Z]|\x00)*\s*$')
sys.exit(qd_run.main())
