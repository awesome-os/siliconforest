#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Runs ON THE M1 (SiliconForest has no netstat): the kernel's interface counters, read the way
# Activity Monitor reads them (sysctl NET_RT_IFLIST2, struct if_msghdr2 / if_data64).
#   ifcounters.py [interface ...]     one line per interface: ipackets ierrors ibytes opackets oerrors obytes
import ctypes, ctypes.util, socket, struct, sys

libc = ctypes.CDLL(ctypes.util.find_library('c'), use_errno=True)
MIB = (ctypes.c_int * 6)(4, 17, 0, 0, 6, 0)          # CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0
RTM_IFINFO2 = 0x12


def table():
    size = ctypes.c_size_t(0)
    if libc.sysctl(MIB, 6, None, ctypes.byref(size), None, 0) != 0:
        raise OSError(ctypes.get_errno(), 'sysctl size')
    buf = ctypes.create_string_buffer(size.value)
    if libc.sysctl(MIB, 6, buf, ctypes.byref(size), None, 0) != 0:
        raise OSError(ctypes.get_errno(), 'sysctl')
    raw, off, rows = buf.raw[:size.value], 0, {}
    while off + 4 <= len(raw):
        msglen, _ver, mtype = struct.unpack_from('<HBB', raw, off)
        if msglen == 0:
            break
        if mtype == RTM_IFINFO2:
            index = struct.unpack_from('<H', raw, off + 12)[0]
            d = off + 32                                   # struct if_data64 in if_msghdr2
            ipk, ier, opk, oer, _col, ib, ob = struct.unpack_from('<7Q', raw, d + 24)
            try:
                name = socket.if_indextoname(index)
            except OSError:
                name = 'if%d' % index
            rows[name] = (ipk, ier, ib, opk, oer, ob)
        off += msglen
    return rows


def main():
    rows = table()
    want = sys.argv[1:] or sorted(rows)
    for name in want:
        r = rows.get(name)
        if r is None:
            print('%s absent' % name)
            continue
        print('%s ipackets=%d ierrors=%d ibytes=%d opackets=%d oerrors=%d obytes=%d' % ((name,) + r))


if __name__ == '__main__':
    main()
