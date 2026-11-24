# SPDX-License-Identifier: MIT
# Runs on the M1's Linux from standard input (ssh ... 'sudo -n python3 -'), so no file is
# written there. Sends three READ-ONLY admin commands through the kernel's passthrough
# (Identify Controller, Identify Namespace 1, Get Log Page 2 = SMART) on a read-only open
# of /dev/nvme0 and prints the fields in the format of kern.sf_nvme.idctrl / idns / smart,
# then the raw pages in hexadecimal (those lines carry the serial number: runs/ only).
import ctypes, fcntl, os, struct
fd = os.open('/dev/nvme0', os.O_RDONLY)
def admin(opcode, nsid, cdw10, size):
    buf = ctypes.create_string_buffer(size)
    cmd = struct.pack('<BBHIIIQQIIIIIIIIII', opcode, 0, 0, nsid, 0, 0, 0, ctypes.addressof(buf), 0, size, cdw10, 0, 0, 0, 0, 0, 5000, 0)
    fcntl.ioctl(fd, 0xC0484E41, ctypes.create_string_buffer(cmd, 72))
    return buf.raw
c = admin(0x06, 0, 1, 4096); n = admin(0x06, 1, 0, 4096); s = admin(0x02, 0xffffffff, 2 | (127 << 16), 512)
u16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
u64 = lambda b, o: struct.unpack_from('<Q', b, o)[0]
txt = lambda b: ''.join(chr(x) if 0x20 <= x <= 0x7e else ' ' for x in b).rstrip()
print('NV_IDCTRL vid=0x%x ssvid=0x%x mn="%s" fr="%s" rab=%u ieee=%02x%02x%02x cmic=0x%x mdts=%u cntlid=0x%x ver=0x%x rtd3r=0x%x rtd3e=0x%x '
      'oaes=0x%x ctratt=0x%x oacs=0x%x acl=%u aerl=%u frmw=0x%x lpa=0x%x elpe=%u npss=%u avscc=0x%x apsta=0x%x wctemp=%u cctemp=%u '
      'sqes=0x%x cqes=0x%x maxcmd=%u nn=%u oncs=0x%x fuses=0x%x fna=0x%x vwc=0x%x awun=%u awupf=%u nvscc=%u sgls=0x%x' % (
      u16(c, 0), u16(c, 2), txt(c[24:64]), txt(c[64:72]), c[72], c[75], c[74], c[73], c[76], c[77], u16(c, 78), u32(c, 80), u32(c, 84),
      u32(c, 88), u32(c, 92), u32(c, 96), u16(c, 256), c[258], c[259], c[260], c[261], c[262], c[263], c[264], c[265], u16(c, 266),
      u16(c, 268), c[512], c[513], u16(c, 514), u32(c, 516), u16(c, 520), u16(c, 522), c[524], c[525], u16(c, 526), u16(c, 528),
      c[530], u32(c, 536)))
lbaf = u32(n, 128 + 4 * (n[26] & 0xf))
print('NV_IDNS nsze=%u ncap=%u nuse=%u nsfeat=0x%x nlbaf=%u flbas=0x%x mc=0x%x dpc=0x%x dps=0x%x lbaf_ms=%u lbaf_lbads=%u lbaf_rp=%u' % (
      u64(n, 0), u64(n, 8), u64(n, 16), n[24], n[25], n[26], n[27], n[28], n[29], lbaf & 0xffff, (lbaf >> 16) & 0xff, (lbaf >> 24) & 3))
print('NV_SMART critical_warning=0x%x temperature_k=%u available_spare=%u spare_thresh=%u percent_used=%u data_units_read=%u '
      'data_units_written=%u host_read_commands=%u host_write_commands=%u controller_busy_time=%u power_cycles=%u power_on_hours=%u '
      'unsafe_shutdowns=%u media_errors=%u num_err_log_entries=%u (low 64 bits of each counter)' % (
      s[0], u16(s, 1), s[3], s[4], s[5], u64(s, 32), u64(s, 48), u64(s, 64), u64(s, 80), u64(s, 96), u64(s, 112), u64(s, 128),
      u64(s, 144), u64(s, 160), u64(s, 176)))
print('RAW_IDCTRL', c.hex()); print('RAW_IDNS', n.hex()); print('RAW_SMART', s.hex())
