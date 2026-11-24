#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Runs ON THE M5 (copied to ~/vk-serial/ by sfvk-uart.sh): read the M1's real uart0 from
# /dev/cu.debug-console (115200 8N1, raw) and write every byte, with one timestamped line per
# received line, to stdout and to the file given. Python standard library only (termios).
#   m5_uart_capture.py <out-file> <seconds>     (0 seconds: until killed)
# It does not route the UART: `macvdmtool serial` does that (no reset), see sfvk-uart.sh.
import os, sys, termios, time, select

DEV = '/dev/cu.debug-console'


def main():
    out, seconds = sys.argv[1], float(sys.argv[2])
    fd = os.open(DEV, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0                                   # iflag: raw
    attrs[1] = 0                                   # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0                                   # lflag: no echo, no canonical mode
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    began = time.time()
    total, pending = 0, b''
    with open(out, 'ab', buffering=0) as f:
        f.write(('UARTCAP open %s at %s\n' % (DEV, time.strftime('%H:%M:%S', time.gmtime()))).encode())
        while seconds == 0 or time.time() - began < seconds:
            ready, _, _ = select.select([fd], [], [], 0.5)
            if not ready:
                continue
            data = os.read(fd, 4096)
            if not data:
                continue
            total += len(data)
            pending += data
            while b'\n' in pending:
                line, pending = pending.split(b'\n', 1)
                stamped = ('%s ' % time.strftime('%H:%M:%S', time.gmtime())).encode() + line.rstrip(b'\r') + b'\n'
                f.write(stamped)
                sys.stdout.buffer.write(stamped)
                sys.stdout.flush()
        if pending:
            f.write(pending + b'\n')
        f.write(('UARTCAP end bytes=%d\n' % total).encode())
    print('UARTCAP end bytes=%d' % total, flush=True)


if __name__ == '__main__':
    main()
