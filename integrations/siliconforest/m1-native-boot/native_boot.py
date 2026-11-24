# SPDX-License-Identifier: MIT
"""Native boot of XNU on the J293 (no m1n1 hypervisor), and the probe that proves its console.

Placed next to host_progress.py in a boot's private copy of the launcher tree
(PCIE_LAB_FILES), together with native_stub.py and native_mode.txt; launcher-native.patch
adds the four places host_progress.py calls into this file. Without native_mode.txt, or with
`off` in it, the launcher behaves exactly as without this file.

native_mode.txt is one line of words:
  probe        hypervisor boot as always, plus a reader on the controller's
               /dev/cu.debug-console (the M1's real uart0, over the USB-C debug pins) for the
               whole worker, and one marker line written to that UART through the proxy
  native       no hypervisor: load exactly what the hypervisor boot loads, at the same
               addresses, then vector m1n1 into stub/el2_drop.S, which enters XNU at EL1.
               The console (and the console challenge) is the real UART.
  vdm-serial   before listening (probe) or before the vector (native), ask the M1's USB-C
               port controller for serial mode: the controller's own pinned
               `macvdmtool serial`, the mode half of the `reboot serial` it sends at the end
               of every boot. Boot nb01: without it nothing arrives on the debug console
               once Linux has booted and rebooted in between.
  wdt=<s>      native only: arm the SoC watchdog (WD1 of /arm-io/wdt) for <s> seconds, 30..170,
               just before the vector. Nothing in this kernel services it, so the M1 resets
               itself that long after the kernel starts, whatever state it is in.
  nosprr       native only: do not enable SPRR and GXF before entering EL1 (the hypervisor
               boot enables both)
  noamx        native only: do not enable AMX for EL1 (the hypervisor boot does)
  noiolog      native only: serial=0x13 instead of serial=3 (SERIALMODE_NO_IOLOG): driver
               IOLog lines stay in the kernel log and off the UART. At 115200 baud the full
               log holds the boot back by about 8 s (boot nb07); kernel printf, launchd and
               the shell still print.
  multiuser    native only: boot multi-user (the launcher's built-in -s is dropped), for a
               session that launchd starts (guest/org.siliconforest.desktop.plist). There is
               no shell on the console then: the challenge is never typed, the result is
               NO_COMMAND_LINE, and the capture ends on the guest script's SF_BENCH_DONE line.
  stage        with native: the launcher loads one image and nothing else, b1/'s stage m1n1
               followed by its payload records (configuration, the collection, the RAM root
               gzipped), vectors m1n1 into it and from then on only listens on the UART; the
               stage boots the kernel by itself (b1/native_stage.py, b1/DESIGN.md). nosprr,
               noamx, wdt= and noiolog apply as for native; wdt= is armed by the stage.
               usbkeep: the device-tree nodes of the USB port the proxy used stay in the tree
               (removed otherwise, as in every boot so far). hostseed: the kernel's boot seed
               comes from the controller instead of the M1's SEP (bench fallback). lowmem:
               the stage chooses where XNU's memory begins (just above its own, about 0.8 GB
               lower); without it the stage is told the launcher's own phys_base, so that
               every address is the one of the native boots so far.
               noramdisk: the image carries NO RAM root record (the stage, its configuration
               and the collection only): for a boot whose root is on the disk (rd=uuid
               boot-uuid=<the filesystem the stage was built for>; the stage refuses rd=md0
               without the record). Nothing of the RAM root reaches the M1.
               bootbin (with noramdisk): nothing is packed on the controller. The file
               sf-boot.bin next to the launcher (install/build.sh: the file an installed
               stage 1 reads from the boot partition) is uploaded byte for byte, followed by
               a variable line as stage 1's chainload appends one, and entered. Its own
               configuration record is the boot's: the launcher's boot arguments are not used.
               stage1: the file sf-stage1-test.bin (the installed stage 1 with its
               chainload= line, as the installer writes the boot object) is uploaded and
               entered; nothing else. It is expected to find no partition and stop
               (SF_STOPPED on the UART); the kernel is never entered. It is entered as
               iBoot enters it: top_of_kernel_data directly behind the image.
               stage1room (with stage1): 64 MiB of room behind the image instead (boot dr34;
               the installed machine has no such room).
               stage1go (with stage1 and multiuser): stage 1 is expected to start the system
               from the boot partition it names (sf/boot.bin, which reads sf/kernel.bin);
               the capture runs until the session's own restart. sf-kernel.bin next to the
               launcher is a copy of the kernel file on that partition (for the record and
               the check that its kernel is this boot's); it is not uploaded.
  vdm-return   native only, read by the controller (tools/m1_bringup_cycle.py as patched by
               launcher-native.patch): when the capture is over and neither Linux nor a
               proxy answers, wait 45 s for a Linux that is coming back by itself, then
               send the one `macvdmtool reboot serial`. Without this word the controller
               resets only a machine that shows the m1n1 proxy, which a native XNU never
               does: the boot must then end by wdt=.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import threading
import time

HERE = Path(__file__).resolve().parent
UART_DEVICE = '/dev/cu.debug-console'
# The controller's reset tool and its pin (tools/m1_kernel_cycle.py: NATIVE, NATIVE_SHA)
VDMTOOL = Path('/Users/chriskearney/darwin-lab-firstboot-007/native-control-src/macvdmtool')
VDMTOOL_SHA = 'db6da3ff2f86d9663f748d975ce1ce988c82047014912018775b61f207b6ff4a'
T8103_WDT = 0x23d2b0000         # /arm-io/wdt reg 0; m1n1 prints the same at start
WDT_COUNT, WDT_ALARM, WDT_CTL, WDT_RESET_EN, WDT_HZ = 0x10, 0x14, 0x1c, 4, 24_000_000
STUB_BOOT_ENTRY, STUB_HOLD_ENTRY = 4, 8
HOLD_ACKS = 0x3f40              # one word per held core
RVBAR_ADDR = 0x00000ffffffff000
SPIN_TABLE_ENTRY = 64           # m1n1 src/smp.c struct spin_table: mpidr, flag, target, args[4], retval
PARAMS = 0x3f00                 # magic, uart, flags, secondary entry (stub/el2_drop.S)
FLAG_SPRR_GXF, FLAG_AMX = 1, 2


def require(ok, message):
    if not ok:
        raise ValueError(message)


class Mode:
    def __init__(self, text):
        words = self.words = text.split()
        self.probe = 'probe' in words
        self.native = 'native' in words
        self.sprr = 'nosprr' not in words
        self.amx = 'noamx' not in words
        self.wdt = None
        known = {'probe', 'native', 'nosprr', 'noamx', 'off', 'vdm-return', 'vdm-serial', 'noiolog', 'multiuser'}
        self.vdm_serial = 'vdm-serial' in words
        known |= {'stage', 'rechain', 'stagestop', 'usbkeep', 'hostseed', 'lowmem', 'noramdisk', 'bootbin', 'stage1', 'stage1room', 'stage1go'}
        self.stage, self.usb_keep, self.host_seed = ('stage' in words, 'usbkeep' in words, 'hostseed' in words)
        self.low_mem = 'lowmem' in words
        self.no_ramdisk = 'noramdisk' in words
        for word in words:
            match = re.fullmatch(r'wdt=(\d{2,3})', word)
            if match:
                self.wdt = int(match[1])
                require(30 <= self.wdt <= 170, 'wdt= is 30..170 seconds')
            else:
                require(word in known, 'Unknown word in native_mode.txt: ' + word)
        require(not (self.probe and self.native), 'native_mode.txt: probe or native, not both')
        require(not self.stage or self.native, 'stage needs native')
        self.rechain = 'rechain' in words
        require(not self.rechain or (self.native and not self.stage), 'rechain is for a launcher-native boot')
        require(self.stage or not (self.usb_keep or self.host_seed or self.low_mem or self.no_ramdisk),
                'usbkeep, hostseed, lowmem and noramdisk need stage')
        self.boot_bin, self.stage1 = 'bootbin' in words, 'stage1' in words
        require(not (self.boot_bin or self.stage1) or self.stage, 'bootbin and stage1 need stage')
        self.stage1_room, self.stage1_go = 'stage1room' in words, 'stage1go' in words
        require(self.stage1 or not (self.stage1_room or self.stage1_go), 'stage1room and stage1go need stage1')
        require(not self.stage1_go or 'multiuser' in words, 'stage1go needs multiuser')
        require(not self.boot_bin or (self.no_ramdisk and not self.stage1), 'bootbin needs noramdisk')
        require(not (self.boot_bin or self.stage1) or not (self.host_seed or self.low_mem or self.usb_keep or
                                                           'stagestop' in words or self.wdt is not None),
                'bootbin and stage1 take nothing from the launcher: no hostseed, lowmem, usbkeep, stagestop, wdt=')
        self.vdm_return = 'vdm-return' in words
        require(self.wdt is None or self.native, 'wdt= needs native')
        require(not self.vdm_return or self.native, 'vdm-return needs native')
        require(not self.native or self.vdm_return or self.wdt is not None,
                'A native boot needs a way back: vdm-return, wdt=<s>, or both')

    def __bool__(self):
        return self.probe or self.native


def bootargs(native, text):
    if 'noiolog' in native.words:
        require(text.count(' serial=3 ') == 1, 'Boot arguments lack serial=3')
        text = text.replace(' serial=3 ', ' serial=0x13 ')
    if 'multiuser' in native.words:
        require(text.count(' -s ') == 1, 'Boot arguments lack -s')
        text = text.replace(' -s ', ' ')
    return text


def mode():
    try:
        text = (HERE / 'native_mode.txt').read_text()
    except OSError:
        text = ''
    return Mode(text)


def mark(name, **extra):
    try:
        print('BQ_T %s %.3f%s' % (name, time.time(), (' ' + json.dumps(extra)) if extra else ''), flush=True)
    except Exception:
        pass


class UartTap:
    """Read-only listener on the M1's real UART for a hypervisor boot. Never writes."""
    def __init__(self, directory, serial_factory=None):
        self.directory, self.error, self.count = directory, None, 0
        self.finish = threading.Event()
        self.data = bytearray()
        self.first = self.last = None
        try:
            if serial_factory is None:
                import serial
                serial_factory = serial.Serial
            self.dev = serial_factory(UART_DEVICE, baudrate=115200, timeout=.2,
                                      xonxoff=False, rtscts=False, dsrdtr=False)
        except Exception as error:
            self.dev, self.error = None, f'{type(error).__name__}: {error}'
            print('UART_TAP ' + json.dumps(dict(opened=False, error=self.error)), flush=True)
            return
        print('UART_TAP ' + json.dumps(dict(opened=True, device=UART_DEVICE, baud=115200)), flush=True)
        self.thread = threading.Thread(target=self.read, name='uart-tap', daemon=True)
        self.thread.start()

    def read(self):
        try:
            fd = os.open(self.directory / 'uart.bin', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, 'wb', buffering=0) as log:
                while not self.finish.is_set():
                    data = self.dev.read(4096)
                    if data:
                        now = time.time()
                        self.first = self.first or now
                        self.last = now
                        log.write(data)
                        if len(self.data) < 1 << 20:
                            self.data.extend(data)
                        self.count += len(data)
        except BaseException as error:
            self.error = f'{type(error).__name__}: {error}'

    def stop(self, marker=None):
        if self.dev is None:
            return
        self.finish.set()
        self.thread.join(3)
        try:
            self.dev.close()
        except Exception:
            pass
        text = bytes(self.data)
        print('UART_TAP ' + json.dumps(dict(
            bytes=self.count, first=self.first, last=self.last, error=self.error,
            marker_seen=(marker.encode() in text) if marker else None)), flush=True)
        # What arrived, for worker.log (the only file of this directory the bench fetches
        # besides the console): printable lines, at most 200 of them.
        for line in text.replace(b'\r', b'').split(b'\n')[:200]:
            clean = ''.join(chr(c) if 32 <= c < 127 else '.' for c in line[:200])
            if clean.strip():
                print('UART> ' + clean, flush=True)


def serial_mode():
    """`macvdmtool serial`: the M1's port controller routes uart0 to the debug pins. No reset.
    Never raises; the boot goes on without a console if it fails."""
    try:
        require(hashlib.sha256(VDMTOOL.read_bytes()).hexdigest() == VDMTOOL_SHA, 'macvdmtool changed')
        began = time.monotonic()
        result = subprocess.run([str(VDMTOOL), 'serial'], stdin=subprocess.DEVNULL, capture_output=True, timeout=12)
        report = dict(returncode=result.returncode, seconds=round(time.monotonic() - began, 2),
                      output=(result.stdout + result.stderr).decode('utf-8', 'replace')[-400:])
    except Exception as error:
        report = dict(error=f'{type(error).__name__}: {error}')
    print('VDM_SERIAL ' + json.dumps(report), flush=True)
    return report.get('returncode') == 0


def uart_marker(backend, text):
    """One line on the M1's real uart0, written by m1n1 itself (proxy P_IODEV_WRITE)."""
    try:
        data = text.encode('ascii')
        with backend.u.heap.guarded_malloc(len(data)) as address:
            backend.iface.writemem(address, data)
            written = backend.p.iodev_write(backend.proxy_module.IODEV.UART, address, len(data))
        print('UART_MARKER ' + json.dumps(dict(sent=True, bytes=len(data), written=written)), flush=True)
    except Exception as error:
        print('UART_MARKER ' + json.dumps(dict(sent=False, error=f'{type(error).__name__}: {error}')), flush=True)


class PacedSerial:
    """The real UART for typing. XNU reads its 16-byte receive FIFO every 16 ms
    (osfmk/console/serial_general.c, serial_keyboard_poll) and there is no flow control: a
    command written in one piece loses most of its bytes (boot nb04). Eight bytes every
    25 ms."""
    CHUNK, GAP = 8, .025

    def __init__(self, dev):
        self._dev = dev

    def write(self, data):
        sent = 0
        for offset in range(0, len(data), self.CHUNK):
            sent += self._dev.write(data[offset:offset + self.CHUNK])
            self._dev.flush()
            time.sleep(self.GAP)
        return sent

    def __getattr__(self, name):
        return getattr(self._dev, name)


def native_init(hv, backend):
    """HV.init() without the hypervisor: the part that only prepares the device tree and the
    boot arguments. No p.hv_init() (stage 2, secondary cores started in m1n1, PCIe shutdown),
    no traps, no virtual UART. The device tree is edited exactly as for the hypervisor boots."""
    hv.emulate_sprr = False
    hv.adt = backend.adt_module.load_adt(hv.u.get_adt())
    hv.iodev = hv.p.iodev_whoami()
    hv.tba = hv.u.ba.copy()
    hv.add_tracer = hv.del_tracer = lambda *args, **kwargs: None
    hv.setup_adt()
    print('NATIVE init: no hypervisor; device tree prepared as for a hypervisor boot', flush=True)


def stub_image(uart, flags, secondary_entry=0):
    import native_stub
    blob = bytearray(bytes.fromhex(native_stub.STUB_HEX))
    require(len(blob) == 0x4000 and blob[PARAMS:PARAMS + 5] == b'PDNB1', 'Unexpected native stub image')
    blob[PARAMS + 8:PARAMS + 32] = b''.join(v.to_bytes(8, 'little') for v in (uart, flags, secondary_entry))
    return bytes(blob)


def wdt_arm(p, base, seconds):
    p.write32(base + WDT_CTL, 0)
    p.write32(base + WDT_COUNT, 0)
    p.write32(base + WDT_ALARM, seconds * WDT_HZ)
    p.write32(base + WDT_CTL, WDT_RESET_EN)
    require(p.read32(base + WDT_ALARM) == seconds * WDT_HZ and p.read32(base + WDT_CTL) & WDT_RESET_EN,
            'SoC watchdog did not take the alarm')


def hold_secondaries(hv, backend, stub):
    """The secondary cores for a native kernel. m1n1 starts each one as it does for Linux and
    for its hypervisor (smp_start_secondaries: the core's chicken bits are applied), then
    each is sent into the EL2 page (hold_entry), where it waits for the kernel to write its
    reset vector register. m1n1 will not vector with a call outstanding, and these never
    return, so each core's slot in m1n1's spin table is found (by the call's own arguments)
    and its target cleared once the core has acknowledged."""
    import struct
    p, iface = backend.p, backend.iface
    nodes = [node for node in hv.adt['cpus'] if node.state != 'running']
    p.smp_start_secondaries()
    p.smp_set_wfe_mode(True)
    table, held = None, []
    for node in nodes:
        cpu, register = node.cpu_id, node.cpu_impl_reg[0]
        require(0 < cpu < 8, 'Unexpected secondary cpu id')
        p.write64(register, stub)
        require(p.read64(register) & RVBAR_ADDR == stub, 'Reset vector register did not take the hold value')
        ack, magic = stub + HOLD_ACKS + 8 * cpu, 0x50444e4200000000 | cpu
        p.smp_call(cpu, stub + STUB_HOLD_ENTRY, register, stub, ack, magic)
        deadline = time.monotonic() + 3
        while p.read64(ack) != magic:
            require(time.monotonic() < deadline, f'cpu {cpu} did not reach the EL2 page')
            time.sleep(.01)
        pattern = struct.pack('<5Q', stub + STUB_HOLD_ENTRY, register, stub, ack, magic)
        if table is None:
            base = p.get_base()
            for offset in range(0, 96 << 20, 1 << 20):
                block = iface.readmem(base + offset, (1 << 20) + len(pattern))
                at = block.find(pattern)
                if at >= 0:
                    table = base + offset + at - SPIN_TABLE_ENTRY * cpu
                    break
            require(table is not None, "m1n1's spin table not found")
        target = table + SPIN_TABLE_ENTRY * cpu
        require(iface.readmem(target, len(pattern)) == pattern, f"cpu {cpu}: unexpected slot in m1n1's spin table")
        p.write64(target, 0)
        held.append(cpu)
    print('NATIVE_HOLD ' + json.dumps(dict(cpus=held, spin_table=table and hex(table - 16))), flush=True)
    return held


def start(native, hv, backend, layout, directory, runtime, console_factory, write_private):
    """Enter XNU without the hypervisor and capture its real serial console for `runtime`
    seconds (or until the guest script's end marker). Returns after the capture is closed."""
    p, iface = backend.p, backend.iface
    entry, bootargs = hv.entry, hv.guest_base + hv.bootargs_off
    require(entry == layout['entry'] and hv.phys_base == layout['phys_base'], 'Native entry differs from the layout')
    uart = backend.snapshot['uart0']['translated_addr']
    flags = (FLAG_SPRR_GXF if native.sprr else 0) | (FLAG_AMX if native.amx else 0)
    blob = stub_image(uart, flags)
    # The page stays at EL2 for the whole session. m1n1's own heap is below the first byte of
    # RAM the boot arguments give to XNU (phys_base), as all of m1n1 is.
    stub = p.memalign(0x4000, len(blob))
    require(stub and stub % 0x4000 == 0 and backend.ba.phys_base <= stub and
            stub + len(blob) <= hv.phys_base, 'No place for the EL2 page below XNU memory')
    iface.writemem(stub, blob)
    p.dc_cvau(stub, len(blob))
    p.ic_ivau(stub, len(blob))
    require(iface.readmem(stub, len(blob)) == blob, 'EL2 page readback differs')
    held = hold_secondaries(hv, backend, stub) if getattr(hv, 'smp', False) else []
    # As HV.start(): the panel is left to the kernel.
    p.fb_improve_logo()
    p.fb_shutdown(True)
    record = dict(stub=stub, entry=entry, bootargs=bootargs, uart=uart, flags=flags,
                  el='EL1h under an EL2 that traps nothing', console=UART_DEVICE,
                  watchdog_seconds=native.wdt, hypervisor=False, held_cpus=held)
    if native.vdm_serial:
        record['serial_mode_ok'] = serial_mode()
    console = console_factory(UART_DEVICE)
    console.dev = PacedSerial(console.dev)
    try:
        if native.wdt is not None:
            wdt = hv.adt['/arm-io/wdt'].get_reg(0)[0]
            require(wdt == T8103_WDT, 'Unexpected watchdog address')
            wdt_arm(p, wdt, native.wdt)
            record['watchdog_armed_at'] = time.time()
        # (after a rechain the first record of that name exists already)
        write_private(directory, 'native-start-boot.json' if (directory / 'native-start.json').exists()
                      else 'native-start.json', record)
        print('NATIVE_START ' + json.dumps(record, default=hex), flush=True)
        mark('native_vector')
        # m1n1 replies, then shuts its USB, display console and MMU down and jumps (src/main.c).
        p.request(p.P_VECTOR, stub + STUB_BOOT_ENTRY, entry, bootargs, 0, 0, 0)
        mark('native_vector_acknowledged')
        deadline = time.monotonic() + runtime
        untyped, done = 'multiuser' in native.words, False
        while time.monotonic() < deadline and not done and console.thread.is_alive():
            time.sleep(.2)
            # A session started by launchd: no command was typed, so the console session
            # does not look for the end marker itself.
            done = console.done_seen or (untyped and re.search(
                rb'(?:^|\n)SF_BENCH_DONE\n', bytes(console.proof.output[-4096:]).replace(b'\r', b'')) is not None)
        if done:
            time.sleep(1.0)
        mark('native_capture_end', done=done, console_bytes=len(console.proof.output))
    finally:
        error = None
        try:
            console.close()
        except Exception as failure:
            error = f'{type(failure).__name__}: {failure}'
        write_private(directory, 'capture-core.json', dict(
            status='NATIVE_XNU_CONSOLE_CAPTURED', native=True, hypervisor=False, event=None,
            console='physical uart0 on ' + UART_DEVICE,
            console_capture_finalized=error is None, console_close_error=error,
            diagnostics_complete=False, hardware_boot_observed=False))
