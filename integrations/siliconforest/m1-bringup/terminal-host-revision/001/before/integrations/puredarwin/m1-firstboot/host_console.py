#!/usr/bin/env python3
"""J293 first-entry experiment over an existing USB proxy; offline by default.

This is not a proxy bootstrap, recovery tool, bare-metal loader, or OS launcher.
See HOST_CONSOLE.md before considering execution. Full device logs stay private.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import io
import json
import multiprocessing
import os
from pathlib import Path, PurePosixPath
import platform
import signal
import struct
import sys
import traceback

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]
sys.path.insert(0, str(LAB))
from tools import m1_firstboot as packet
from tools import m1_proxy_preflight as preflight

Error = packet.ValidationError
PAGE = 0x4000
BOOTARGS = '-v debug=0x14e keepsyms=1 serial=3 serial-device-name=uart0 disable-uart-irq=1 cpus=1'
CONFIRMATION = 'J293_RECOVERY_READY'
HEAP_RESERVE = 128 << 20
HOST_HEAP = 1 << 30
TAG = ('##m1n1_ver##' + packet.M1N1_PIN + '\0').encode()
MANDATORY = {
    'proxyclient/m1n1/__init__.py', 'proxyclient/m1n1/proxy.py',
    'proxyclient/m1n1/proxyutils.py', 'proxyclient/m1n1/hv/__init__.py',
    'proxyclient/m1n1/adt.py', 'proxyclient/m1n1/tgtypes.py',
    'proxyclient/m1n1/macho.py', 'proxyclient/m1n1/sysreg.py',
    'proxyclient/m1n1/apple_regs.json', 'proxyclient/m1n1/arm_regs.json',
}


def require(condition, message):
    if not condition:
        raise Error(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align(value):
    return (value + PAGE - 1) & -PAGE


def integer(value, label):
    # Construct's Hex fields are int subclasses, not exact built-in ints.
    require(isinstance(value, int) and not isinstance(value, bool) and 0 <= value < 1 << 64,
            label + ' is not a u64')
    return value


def inside(addr, size, low, high, label):
    integer(addr, label)
    integer(size, label)
    require(size > 0 and low <= addr < addr + size <= high <= 1 << 64,
            label + ' lies outside bounded RAM')


@dataclass
class Inputs:
    report: dict
    contents: dict
    host_files: dict
    inventory: dict
    inventory_sha256: str


def verify_inputs(manifest, host_source, inventory):
    # Root's byte/receipt/source-selection/entry-branch checks MUST run first.
    report, contents = packet.inspect_manifest(Path(manifest))
    receipt = json.loads(contents['m1n1_build_record'], object_pairs_hook=packet.unique_object)
    hashes = receipt.get('proxy_source_sha256')
    require(isinstance(hashes, dict) and MANDATORY <= hashes.keys(),
            'm1n1 receipt lacks the pinned host source map')
    root = Path(host_source).resolve(strict=True)
    host_files = {}
    for name, expected in hashes.items():
        path = PurePosixPath(name)
        require(not path.is_absolute() and '..' not in path.parts and str(path) == name and
                (name.startswith('proxyclient/') or name == 'requirements.txt'),
                'Unsupported host source map path')
        require(isinstance(expected, str) and packet.SHA256.fullmatch(expected),
                'Invalid host source hash')
        source = root / name
        require(source.resolve(strict=True).is_relative_to(root) and
                all(not parent.is_symlink() for parent in (source, *source.parents) if parent != root),
                'Host source symlink is unsupported')
        packet.regular_file(source)
        require(source.stat().st_size <= packet.MAX_ARTIFACT_BYTES, 'Unbounded host source file')
        data = source.read_bytes()  # Empty __init__.py files are legitimate pinned sources.
        require(digest(data) == expected, 'Host source hash mismatch: ' + name)
        host_files[name] = data
    # Only these verified bytes are copied to the worker's fresh import tree.
    # Existing __pycache__, untracked modules and working-tree extras are not imported.
    raw_inventory = packet.artifact_bytes(Path(inventory))
    inherited = json.loads(raw_inventory, object_pairs_hook=packet.unique_object)['target']
    require(inherited.get('identity_confirmed') is True and
            {'apple,j293', 'apple,t8103'} <= set(inherited.get('observed_compatible', [])),
            'Saved Linux inventory is not J293/T8103')
    for key in ('reported_m1n1_stage1_version', 'reported_m1n1_stage2_version'):
        require(isinstance(inherited.get(key), str) and 0 < len(inherited[key]) <= 128,
                'Missing historical stage provenance')
    return Inputs(report, contents, host_files, inherited, digest(raw_inventory))


def validate_bootargs(ba, address, struct_size):
    for key in ('revision', 'version', 'phys_base', 'virt_base', 'mem_size',
                'mem_size_actual', 'top_of_kernel_data', 'devtree', 'devtree_size'):
        integer(ba[key], 'BootArgs.' + key)
    require(ba['revision'] in (1, 2, 3) and ba['version'] in (1, 2), 'Unknown BootArgs ABI')
    low = ba['phys_base']
    high = low + ba['mem_size']
    dram = low & ~0xffffffff
    require(low and low % PAGE == 0 and ba['virt_base'] % PAGE == 0 and
            0 < ba['mem_size'] <= ba['mem_size_actual'] <= 64 << 30 and
            high <= dram + ba['mem_size_actual'], 'Invalid BootArgs RAM layout')
    inside(address, struct_size, dram, high, 'Original BootArgs')
    require(low <= ba['top_of_kernel_data'] <= high, 'Invalid top_of_kernel_data')
    dt = ba['devtree'] - ba['virt_base'] + low
    require(0 < ba['devtree_size'] <= 16 << 20, 'Unbounded ADT size')
    inside(dt, ba['devtree_size'], dram, high, 'Original ADT')
    return dt


def validate_layout(ba, snapshot, heap_start, kernel, extra_ranges=()):
    """Predict pinned HV.load_raw addresses before it writes guest bytes.

    Pinned memcpy8 copies one byte at a time, so firmware address and size need
    not be 8-byte aligned. Ranges must still lie in bounded RAM and end at or
    before the proxy heap. A 16K preoslog size is this launcher's BootArgs rule;
    stock load_raw adds the raw preoslog size and does not require it.
    """
    low, high = ba['phys_base'], ba['phys_base'] + ba['mem_size']
    require(heap_start % PAGE == 0, 'Proxy heap is not 16K aligned')
    inside(heap_start, HEAP_RESERVE + HOST_HEAP, low, high, 'Proxy/host heaps')
    require(heap_start >= ba['top_of_kernel_data'], 'Proxy heap overlaps original kernel data')
    phys = heap_start + HEAP_RESERVE + HOST_HEAP
    mm = snapshot['memory_map']
    ranges = [(item['addr'], item['size'], name) for name, item in mm.items()]
    ranges.extend(extra_ranges)
    for addr, size, name in ranges:
        inside(addr, size, low & ~0xffffffff, high, name)
        require(addr + size <= heap_start, name + ' overlaps planned heap/guest writes')
    vmin = min(s['vmaddr'] for s in kernel['segments'])
    vmax = max(s['vmaddr'] + s['vmsize'] for s in kernel['segments'])
    require(0 < vmax - vmin <= packet.MAX_ARTIFACT_BYTES and
            all(s['name'] != 'PYLD' for s in kernel['segments']), 'Unsupported raw kernel layout')
    guest = phys + (16 << 20) + align(ba['devtree_size']) + align(mm['TrustCache']['size'])
    preoslog_size = sum(size for _, size, name in extra_ranges if name == 'preoslog')
    require(preoslog_size % PAGE == 0, 'preoslog would misalign guest BootArgs')
    bootargs_off = align(vmax - vmin) + align(mm['SEPFW']['size']) + preoslog_size
    inside(phys, guest + bootargs_off + PAGE - phys, low, high, 'Guest load region')
    require((guest - vmin) % PAGE == 0, 'Kernel displacement is not 16K aligned')
    return {'phys_base': phys, 'guest_base': guest, 'bootargs_off': bootargs_off,
            'entry': guest + kernel['entry'] - vmin, 'vmin': vmin}


def write_private(directory, name, data):
    if not isinstance(data, bytes):
        data = (json.dumps(data, indent=2) + '\n').encode()
    fd = os.open(Path(directory) / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as stream:
        stream.write(data)


class BoundedStop(BaseException):
    """Escape HV's Exception-catching handlers; never enter its interactive shell."""


def stop_guest_control(*args, **kwargs):
    raise BoundedStop('Interactive recovery/secondary CPU control is outside this experiment')


class ParkCapture:
    """One main-thread alarm, one existing reader, no continuation after capture.

    This adapter is specific to the receipt-pinned proxy.py/HV methods. Ordinary
    HOOK_VM service must finish before the alarm; all other contexts stop here.
    Unsafe alarm boundaries fail closed rather than injecting into a transaction.
    """
    DELAY = 10
    HV, HOOK_VM, USER_INTERRUPT = 3, 1, 3

    def __init__(self, hv, iface, proxy, context_format, directory, kernel, layout, cpu):
        self.hv, self.iface, self.proxy = hv, iface, proxy
        self.context_format, self.directory = context_format, directory
        self.expected = {'elr': layout['guest_base'] + kernel['park_address'] - layout['vmin'],
                         'x0': layout['guest_base'] + layout['bootargs_off'],
                         'x19': 3, 'spsr': 0x3c5, 'cpu_id': cpu}
        self.start_args = (proxy.P_HV_START, layout['entry'], self.expected['x0'], 0, 0, 0, 0)
        self.phase = 'preparing'
        self.reply_depth = self.cmd_depth = self.dispatch_depth = self.io_depth = 0
        self.requested = self.injected = self.closed = self.alarm_owned = False
        self.event = self.raw = self.context_error = None

    def wrap(self, obj, name, replacement):
        original = getattr(obj, name)
        setattr(obj, name, replacement)
        return original

    def disarm(self):
        if self.alarm_owned:
            signal.setitimer(signal.ITIMER_REAL, 0)

    def stop(self, message):
        if self.closed:
            raise BoundedStop(message)
        self.closed = True  # Even a caught BaseException may never send more bytes.
        self.disarm()
        record = {
            'status': 'BOUNDED_CONTEXT_OBSERVATION_NO_BOOT_VERDICT',
            'claim_class': 'unqualified_proxy_observation', 'boot_proof': False,
            'hardware_boot_observed': False, 'message': message,
            'event': self.event, 'expected': self.expected, 'observed': None,
            'spsr_comparison_mask': 0x3df,  # DAIF + mode; preserve NZCV in the raw observation.
            'alarm_requested': self.requested, 'interrupt_write_completed': self.injected,
            'expected_park_conditions_match': False, 'context_error': self.context_error,
            'boundary': {'phase': self.phase, 'reply_depth': self.reply_depth,
                         'cmd_depth': self.cmd_depth, 'io_depth': self.io_depth,
                         'dispatch_depth': self.dispatch_depth, 'in_handler': self.hv._in_handler},
            'full_console_line_requires_independent_review': True,
            'target_state_after_disconnect': 'unknown; pinned target may autonomously reboot',
        }
        try:
            if self.raw is not None:
                write_private(self.directory, 'exception-context.bin', self.raw)
                record['raw_context_sha256'] = digest(self.raw)
                try:
                    ctx = self.context_format.parse(self.raw)
                    observed = {'elr': int(ctx.elr), 'spsr': int(ctx.spsr),
                                'x0': int(ctx.regs[0]), 'x19': int(ctx.regs[19]),
                                'cpu_id': int(ctx.cpu_id)}
                    record['observed'] = observed
                    matches = {key: observed[key] == value for key, value in self.expected.items()}
                    matches['spsr'] = observed['spsr'] & 0x3df == self.expected['spsr']
                    matches['requested_user_interrupt'] = bool(self.injected and self.event and
                        (self.event['reason'], self.event['code']) == (self.HV, self.USER_INTERRUPT))
                    record['conditions'] = matches
                    record['expected_park_conditions_match'] = all(matches.values()) and not self.context_error
                except Exception as error:
                    record['context_error'] = f'{type(error).__name__}: {error}'
            else:
                record['context_error'] = self.context_error or 'No original exception context was read'
            write_private(self.directory, 'exception-context.json', record)
        finally:
            # Disk/parse failures must also escape HV's Exception handlers.
            raise BoundedStop(message)

    def safe_boundary(self):
        return (self.phase == 'waiting' and self.hv.started and self.reply_depth == 1 and
                self.cmd_depth == self.io_depth == self.dispatch_depth == 0 and
                not self.hv._in_handler and not self.injected and not self.closed)

    def alarm(self, signum=None, frame=None):
        if self.closed:
            return
        if self.requested:
            self.stop('Interrupt boundary unsafe; no additional interrupt byte sent')
        self.requested = True
        if not self.safe_boundary():
            self.stop('Interrupt boundary unsafe; no interrupt byte sent')
        self.original_sigint()  # Pinned pending flag + guarded interrupt, on main thread only.

    def interrupt(self):
        if not self.requested or not self.safe_boundary():
            self.stop('Interrupt boundary unsafe; no additional interrupt byte sent')
        self.original_interrupt()
        self.injected = True

    def write(self, data):
        if self.closed:
            raise BoundedStop('Write after bounded stop prohibited')
        self.io_depth += 1
        try:
            count = self.original_write(data)
            if count != len(data):
                self.stop('Short serial write; frame/interrupt completion not established')
            return count
        finally:
            self.io_depth -= 1

    def cmd(self, command, payload=b''):
        self.cmd_depth += 1  # Guard before inspecting or sending any frame.
        try:
            if self.closed:
                raise BoundedStop('Command after bounded stop prohibited')
            if self.phase == 'frame-written':
                self.stop('Another command preceded the HV_START reply')
            opcode = struct.unpack_from('<Q', payload)[0] if len(payload) >= 8 else None
            if command == self.iface.REQ_PROXY and opcode == self.proxy.P_EXIT:
                ordinary = self.event and (self.event['reason'], self.event['code']) == (self.HV, self.HOOK_VM)
                if not ordinary or self.requested or self.injected or self.hv._sigint_pending:
                    self.stop('Guest continuation outside ordinary pre-capture HOOK_VM prohibited')
                if len(payload) != 56 or struct.unpack_from('<Q', payload, 8)[0] != 2:
                    self.stop('Only the ordinary HOOK_VM HANDLED acknowledgement is permitted')
            start = command == self.iface.REQ_PROXY and opcode == self.proxy.P_HV_START
            if start and (self.phase != 'preparing' or not self.hv.started or
                          len(payload) != 56 or struct.unpack('<7Q', payload) != self.start_args):
                self.stop('Unexpected HV_START command or arguments')
            result = self.original_cmd(command, payload)
            if start:
                self.phase = 'frame-written'
            return result
        finally:
            self.cmd_depth -= 1

    def reply(self, command):
        outer = self.phase == 'frame-written'
        if outer and (command != self.iface.REQ_PROXY or self.reply_depth or
                      self.cmd_depth or self.io_depth or self.dispatch_depth or self.hv._in_handler):
            self.stop('HV_START reply boundary is not isolated')
        self.reply_depth += 1
        try:
            if outer:
                self.phase = 'waiting'
                signal.setitimer(signal.ITIMER_REAL, self.DELAY)  # One shot; no thread or self-kill.
            return self.original_reply(command)
        finally:
            self.reply_depth -= 1
            if outer:
                self.disarm()
                self.phase = 'finished'

    def transaction(self, original, *args, **kwargs):
        # Bulk payload transfer extends beyond cmd(); guard the entire operation.
        self.io_depth += 1
        try:
            if self.closed:
                raise BoundedStop('I/O after bounded stop prohibited')
            return original(*args, **kwargs)
        finally:
            self.io_depth -= 1

    def dispatch(self, original, boot, *args):
        self.dispatch_depth += 1
        try:
            if boot:
                self.event = self.raw = self.context_error = None
                self.hv.ctx = self.hv._info_data = None  # Never reuse another callback's context.
                reason, code, info = struct.unpack('<IIQ', args[0][:16])
                self.event = {'reason': reason, 'code': code, 'info': info}
                if ((reason in (1, 2) and code in (0, 1, 2, 3)) or
                        (reason == self.HV and code in (2, 3, 5, 6, 7))):
                    # Exception, VTIMER, USER_INTERRUPT, CPU_SWITCH, VIRTIO and
                    # PANIC use ExcInfo. Read through the pinned loader before
                    # dispatch (PANIC/VIRTIO normally bypass that loader).
                    self.hv.exc_info, self.hv._in_handler = info, True
                    self.load_context()
                if (reason, code) != (self.HV, self.HOOK_VM):
                    self.stop('Callback has no supported exception context ABI')
            return original(*args)
        finally:
            self.dispatch_depth -= 1
            if boot and not self.closed:
                self.event = self.raw = None

    def load_context(self):
        if not self.event or not self.event['info']:
            self.stop('Exception callback has no context pointer')
        try:
            ctx = self.original_load_context()
        except Exception as error:
            self.raw = self.hv._info_data
            self.context_error = f'{type(error).__name__}: {error}'
            self.stop('Original exception context could not be loaded')
        self.raw = bytes(self.hv._info_data)  # Original bytes, before any hook/commit mutation.
        ordinary = (self.event['reason'], self.event['code']) == (self.HV, self.HOOK_VM)
        if ordinary and not self.requested and not self.hv._sigint_pending:
            return ctx  # Keep normal UART/MMIO service working before the capture request.
        self.stop('Exception context captured; no continuation requested')

    def run(self):
        require(signal.getitimer(signal.ITIMER_REAL) == (0.0, 0.0), 'Worker already has an alarm')
        # This dedicated worker exits after this operation. Ignore any pending
        # SIGALRM on teardown rather than restoring a terminating default handler.
        signal.signal(signal.SIGALRM, self.alarm)  # Also enforces Python's main-thread rule.
        self.alarm_owned = True
        try:
            self.original_write = self.wrap(self.iface.dev, 'write', self.write)
            self.original_cmd = self.wrap(self.iface, 'cmd', self.cmd)
            self.original_reply = self.wrap(self.iface, 'reply', self.reply)
            for name in ('readmem', 'writemem'):
                original = getattr(self.iface, name)
                self.wrap(self.iface, name, lambda *a, _fn=original, **k: self.transaction(_fn, *a, **k))
            for name, boot in (('handle_boot', True), ('handle_event', False)):
                original = getattr(self.iface, name)
                self.wrap(self.iface, name, lambda *a, _fn=original, _boot=boot: self.dispatch(_fn, _boot, *a))
            self.original_load_context = self.wrap(self.hv, '_load_context', self.load_context)
            self.wrap(self.hv, 'run_shell', lambda *a, **k: self.stop('Pinned handler requested a shell'))
            self.original_interrupt = self.wrap(self.hv, 'interrupt', self.interrupt)
            self.original_sigint = self.wrap(self.hv, '_handle_sigint', self.alarm)
            self.hv.start()
            self.stop('HV.start returned; no retry or continuation')
        except Exception as error:
            self.context_error = f'{type(error).__name__}: {error}'
            self.stop('Host capture operation failed')
        finally:
            self.disarm()
            signal.signal(signal.SIGALRM, signal.SIG_IGN)
            self.closed = True
            # Keep write/command guards installed through backend.close(). This
            # worker never starts another session; stale handlers cannot resume it.


class PinnedBackend:
    """Imports only a fresh copy of hash-checked host source, never m1n1.setup."""

    def __init__(self, inputs, directory):
        require(sys.platform == 'darwin' and platform.machine() == 'arm64',
                'Execution requires the reviewed Apple Silicon macOS controller')
        require(not any(k.startswith('M1N1') for k in os.environ), 'Unexpected m1n1 environment overrides')
        require(not any(k == 'm1n1' or k.startswith('m1n1.') for k in sys.modules),
                'm1n1 modules were imported before source verification')
        tree = directory / 'host-source'
        tree.mkdir(mode=0o700)
        for name, data in inputs.host_files.items():
            dest = tree / name
            dest.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
            write_private(dest.parent, dest.name, data)
        sys.dont_write_bytecode = True
        sys.path.insert(0, str(tree / 'proxyclient'))
        from m1n1 import adt, proxy, proxyutils, tgtypes
        from m1n1.hv import HV
        self.adt_module, self.proxy_module = adt, proxy
        self.utils_class, self.types = proxyutils.ProxyUtils, tgtypes
        self.hv_class = HV  # Resolve host dependencies before opening a device.
        require('m1n1.setup' not in sys.modules, 'Unexpected automatic proxy setup import')
        require(proxy.CPUFeatures.sizeof() == 20, 'Host CPUFeatures ABI is not 20 bytes')
        self.iface = None

    def open(self, device):
        self.iface = self.proxy_module.UartInterface(device=device)
        self.iface.nop()
        self.p = self.proxy_module.M1N1Proxy(self.iface)

    def collect(self, inputs):
        base = self.p.get_base()
        require(type(base) is int and base > 0 and base % PAGE == 0 and
                base + len(inputs.contents['m1n1']) < 1 << 64, 'Invalid live proxy base')
        tag_at = inputs.contents['m1n1'].index(TAG)
        require(self.iface.readmem(base + tag_at, len(TAG)) == TAG,
                'Live proxy BUILD_TAG differs from the pinned image')
        self.features = self.p.get_cpu_features()  # Target rejects any size other than 20.
        require(self.p.iodev_whoami() in (self.proxy_module.IODEV.USB0, self.proxy_module.IODEV.USB1),
                'J293 launcher requires an already working USB0/USB1 proxy')
        addr, rev = self.p.get_bootargs_rev()
        require(rev in (1, 2, 3), 'Unknown live BootArgs revision')
        fmt = getattr(self.types, 'BootArgs_r' + str(rev))
        raw_ba = self.iface.readmem(addr, fmt.sizeof())
        self.ba = fmt.parse(raw_ba)
        require(self.ba.revision == rev, 'BootArgs revision changed while reading')
        dtaddr = validate_bootargs(self.ba, addr, fmt.sizeof())
        raw_adt = self.iface.readmem(dtaddr, self.ba.devtree_size)
        tree = self.adt_module.load_adt(raw_adt)
        chosen, product, uart = tree['chosen'], tree['product'], tree['/arm-io/uart0']
        mm = chosen['memory-map']
        uart_addr, uart_size = uart.get_reg(0)
        inherited = inputs.inventory
        snapshot = {
            'schema_version': 1,
            'product': {'target_type': tree.target_type, 'model': tree.model,
                        'compatible': inherited['observed_compatible'],
                        **{k: getattr(product, k) for k in
                           ('product_name', 'product_description', 'product_soc_name')}},
            'chip': {'adt_chip_id': chosen.chip_id, 'proxy_chip_id': self.p.get_chipid()},
            'cpus': [{k: getattr(cpu, k) for k in ('name', 'state', 'cpu_id', 'cluster_id', 'reg')}
                     for cpu in tree['cpus']],
            'uart0': {'translated_addr': uart_addr, 'size': uart_size,
                      'irq': uart.interrupts[0], 'aapl_phandle': getattr(uart, 'AAPL,phandle')},
            'bootargs': {'revision': rev, 'version': self.ba.version},
            'memory_map': {name: {'addr': getattr(mm, name)[0], 'size': getattr(mm, name)[1]}
                           for name in ('SEPFW', 'TrustCache')},
            'identity': {'build_tag': packet.M1N1_PIN, 'packet_source_pin': packet.M1N1_PIN,
                         'build_source_pin': packet.M1N1_PIN,
                         'stage1': inherited['reported_m1n1_stage1_version'],
                         'stage2': inherited['reported_m1n1_stage2_version']},
            'hardware_tested': False,
        }
        require(tree.target_type == 'J293' and tree.model == 'MacBookPro17,1' and
                chosen.chip_id == preflight.CHIP_ID == snapshot['chip']['proxy_chip_id'] and
                uart_addr == preflight.UART_ADDR and
                sum(cpu['state'] == 'running' for cpu in snapshot['cpus']) == 1,
                'Live board/chip/UART/CPU selection mismatch before utility RAM allocation')
        extra = [(*mm.preoslog, 'preoslog')] if hasattr(mm, 'preoslog') else []
        heap_start = self.p.heapblock_alloc(0)
        self.layout = validate_layout(self.ba, snapshot, heap_start, inputs.report['kernel'], extra)
        self.u = self.utils_class(self.p, heap_size=HOST_HEAP, m1n1_heap=HEAP_RESERVE)
        require(self.u.ba == self.ba and self.u.heap_top == self.layout['phys_base'] and
                self.u.cpu_features == self.features, 'Live metadata/heap changed during collection')
        self.u.adt_data = raw_adt  # Preserve the exact observed bytes, not a later reread.
        snapshot['guest'] = self.guest_registers()
        self.snapshot = snapshot
        return snapshot, raw_ba, raw_adt, self.layout

    def guest_registers(self):
        return {'sctlr': self.u.mrs('SCTLR_EL12'), 'vpidr': self.u.mrs('VPIDR_EL2')}

    def new_hv(self):
        hv = self.hv_class(self.iface, self.p, self.u)
        hv.smp, hv.wdt_cpu, hv.emulate_sprr = False, None, False
        hv.run_shell = stop_guest_control
        hv.start_secondary = stop_guest_control
        hv.reboot = stop_guest_control
        return hv

    def close(self):
        if self.iface is not None:
            self.iface.dev.close()  # Host file descriptor only; no target cleanup commands.

    def start_guest(self, hv, inputs, layout, snapshot, directory):
        cpu = next(cpu['cpu_id'] for cpu in snapshot['cpus'] if cpu['state'] == 'running')
        ParkCapture(hv, self.iface, self.p, self.proxy_module.ExcInfo, directory,
                    inputs.report['kernel'], layout, cpu).run()


def run_worker(args, inputs, backend_factory=PinnedBackend):
    authorize(args)
    directory = args.private_directory.resolve(strict=True)
    require(directory.stat().st_mode & 0o077 == 0 and directory.stat().st_uid == os.getuid(),
            'Private directory must be owned by the operator with mode 0700')
    backend = backend_factory(inputs, directory)
    try:
        backend.open(args.device)
        snapshot, raw_ba, raw_adt, layout = backend.collect(inputs)
        observed = preflight.observe(snapshot)
        # Must be saved on the host before HV.init mutates its ADT copy/controllers.
        write_private(directory, 'original-bootargs.bin', raw_ba)
        write_private(directory, 'original-adt.bin', raw_adt)
        write_private(directory, 'original-observations.json', {
            'snapshot': snapshot, 'validation': observed, 'layout': layout,
            'packet': inputs.report, 'inventory_sha256': inputs.inventory_sha256,
            'provenance': {'product.compatible': 'saved Linux inventory, not live ADT',
                           'identity.stage1/stage2': 'saved historical Linux boot properties',
                           'identity.build_tag': 'live bytes at verified image tag offset; not full-image attestation'},
        })
        hv = backend.new_hv()
        require(hv.smp is False and hv.wdt_cpu is None and hv.emulate_sprr is False,
                'Unexpected HV experiment options')
        hv.init()
        cpus = list(hv.adt['cpus'])
        require(len(cpus) == 1 and cpus[0].state == 'running', 'HV did not retain exactly one guest CPU')
        hv.set_bootargs(BOOTARGS)
        require(hv.tba.cmdline == BOOTARGS, 'HV boot arguments differ')
        # Check again after initialization, before writing the image.
        preflight.observe({**snapshot, 'guest': backend.guest_registers()})
        hv.load_macho(io.BytesIO(inputs.contents['kernel']))  # No KC, symbol file, or patch hook.
        for field in ('phys_base', 'guest_base', 'bootargs_off', 'entry'):
            require(getattr(hv, field) == layout[field], 'HV load layout changed: ' + field)
        require((hv.guest_base - layout['vmin']) % PAGE == 0, 'Loaded displacement is not 16K aligned')
        preflight.observe({**snapshot, 'guest': backend.guest_registers()})
        write_private(directory, 'before-start.json', {
            'status': 'HOST_ABOUT_TO_REQUEST_SINGLE_CPU_ENTRY', 'layout': layout,
            'bootargs': BOOTARGS, 'hardware_boot_observed': False,
        })
        backend.start_guest(hv, inputs, layout, snapshot, directory)
        raise BoundedStop('HV.start returned; no automatic retry or continuation')
    finally:
        backend.close()


def authorize(args):
    require(args.execute and args.device and args.confirm_recovery == CONFIRMATION,
            'Execution needs --execute, --device and explicit operator recovery confirmation')
    require(args.device.startswith('/dev/cu.usbmodem') and
            '/' not in args.device[len('/dev/'):] and ':' not in args.device,
            'Device must be an explicit macOS USB modem callout path')
    require(args.private_directory is not None, 'Execution needs a private log directory')
    require(not args.private_directory.resolve().is_relative_to(LAB), 'Device logs must stay outside the repository')
    require(1 <= args.timeout <= 300, 'Worker timeout must be between 1 and 300 seconds')
    require(not any(k.startswith('M1N1') for k in os.environ), 'Remove m1n1 environment overrides before execution')


def worker_entry(args):
    """Spawn target only: no command-line worker escape hatch without a parent timer."""
    os.umask(0o077)
    os.setsid()  # Terminal Ctrl-C reaches the supervisor, not HV's SIGINT handler.
    null = os.open(os.devnull, os.O_RDONLY)
    os.dup2(null, 0)
    os.close(null)
    fd = os.open(args.private_directory / 'worker.log', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    os.dup2(fd, 1)
    os.dup2(fd, 2)
    os.close(fd)
    sys.stdout = os.fdopen(1, 'w', buffering=1, closefd=False)
    sys.stderr = os.fdopen(2, 'w', buffering=1, closefd=False)
    try:
        inputs = verify_inputs(args.manifest, args.host_source, args.inventory)
        run_worker(args, inputs)
    except (Exception, BoundedStop):
        traceback.print_exc()
    finally:
        sys.stdout.flush()
        sys.stderr.flush()
    raise SystemExit(1)


def supervise(args, process_factory=None):
    if process_factory is None:
        process_factory = multiprocessing.get_context('spawn').Process
    proc = process_factory(target=worker_entry, args=(args,))
    proc.start()
    status = 'HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT'
    try:
        proc.join(args.timeout)
        if proc.is_alive():
            status = 'HOST_WORKER_TIMEOUT'
    except KeyboardInterrupt:
        status = 'HOST_OPERATOR_INTERRUPTED'
    if status != 'HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT':
        try:
            proc.kill()  # SIGKILL to this host PID only, never target or process-group signals.
        except ProcessLookupError:
            pass  # It exited between the timeout observation and the host kill.
        proc.join(5)
    return {'status': status, 'worker_returncode': proc.exitcode,
            'hardware_boot_observed': False, 'target_state': 'unknown; independent operator recovery required'}


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path)
    p.add_argument('--host-source', type=Path)
    p.add_argument('--inventory', type=Path, default=LAB / 'reports/sprint-007/ssh-target-inventory.json')
    p.add_argument('--execute', action='store_true')
    p.add_argument('--device')
    p.add_argument('--confirm-recovery', metavar=CONFIRMATION)
    p.add_argument('--private-directory', type=Path)
    p.add_argument('--timeout', type=int, default=60)
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        if args.execute:
            authorize(args)
        require(bool(args.manifest) == bool(args.host_source), 'Supply both manifest and host source')
        require(not args.execute or args.manifest, 'Execution needs a verified packet and host source')
        inputs = verify_inputs(args.manifest, args.host_source, args.inventory) if args.manifest else None
        if not args.execute:
            print(json.dumps({'status': 'OFFLINE_PLAN_ONLY', 'device_contacted': False,
                              'packet_and_host_bytes_verified': inputs is not None,
                              'hardware_boot_observed': False, 'bootargs': BOOTARGS,
                              'entry': 'raw MH_EXECUTE LC_UNIXTHREAD -> bounded pre-VM checkpoint -> park',
                              'requirements': ['reviewed packet + pinned host source', 'existing matching USB proxy',
                                               '--execute + --device + --confirm-recovery ' + CONFIRMATION,
                                               'private logs outside repository; independent recovery'],
                              'scope': 'm1n1 HV only; no full kernel/init, bootstrap or recovery qualification'}, indent=2))
            return 0
        directory = args.private_directory.resolve()
        directory.mkdir(mode=0o700, parents=False, exist_ok=False)
        args.private_directory = directory
        result = supervise(args)
        write_private(directory, 'host-result.json', result)
        print(json.dumps(result, indent=2))
        return 1  # No worker outcome is automatically a hardware PASS.
    except (Exception, BoundedStop):
        print(json.dumps({'status': 'REJECTED', 'hardware_boot_observed': False,
                          'error': str(sys.exc_info()[1])}))
        return 1


if __name__ == '__main__':
    sys.exit(main())
