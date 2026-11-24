#!/usr/bin/env python3
"""Bounded continuing-XNU experiment on J293 via pinned m1n1; offline default.

The separate 007 diagnostic/bootstrap packet remains mandatory for verifying
the pinned proxy sources. This launcher has its own continuing-KC manifest and
captures the second CDC interface. It never substitutes a host shell for a
guest shell and never performs recovery or changes boot storage.
"""
from __future__ import annotations

import argparse
import importlib.util
import io
import json
import multiprocessing
import os
from pathlib import Path
import plistlib
import re
import secrets
import signal
import struct
import subprocess
import sys
import threading
import time
import traceback

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]
sys.path.insert(0, str(HERE))
import normal_image as image
from bootargs_progress import validate_bootargs
VIRTUAL_INPUT_SHA = '689a8fb0fd7728d655d3baefbee18308cb2bce7333f1c0f4a76f37bc8e5673e1'
image.require(image.sha(image.bounded_file(HERE/'virtual_input_progress.py')) == VIRTUAL_INPUT_SHA,
              'Virtual-input helper source changed')
from virtual_input_progress import VirtualInput
TERMINAL_INPUT_SHA = '2b8901e7c29357a3440c40e1db14f5e2416f38beb71af0fee7674cb05233568c'
image.require(image.sha(image.bounded_file(HERE/'terminal_input_progress.py')) == TERMINAL_INPUT_SHA,
              'Terminal-input helper source changed')
from terminal_input_progress import TerminalInput

FROZEN = LAB / 'integrations/puredarwin/m1-firstboot/host_console.py'
FROZEN_SHA = 'a9f68695c6465bc5a3c1a9df04f67da6a72f99265e899cf3b89ba671ab6d4417'
image.require(image.sha(image.bounded_file(FROZEN)) == FROZEN_SHA,
              'Frozen diagnostic backend source changed')
spec = importlib.util.spec_from_file_location('bringup_frozen_host', FROZEN)
old = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = old
spec.loader.exec_module(old)


PRIVATE_POLICY_HELPER_SHA = 'e099e953fbefe323e03e097b861606781762a53b243fec2039d0c2ee2cbb5c8c'
PRIVATE_POLICY_HELPER_PATH = 'integrations/puredarwin/m1-bringup/private_policy_progress.py'

def private_policy_helper():
    raw = image.bounded_file(LAB / PRIVATE_POLICY_HELPER_PATH, 128 << 10)
    image.require(image.sha(raw) == PRIVATE_POLICY_HELPER_SHA, 'Private-policy integration helper changed')
    spec = importlib.util.spec_from_loader('bound_private_policy_host', loader=None, origin=str(LAB / PRIVATE_POLICY_HELPER_PATH))
    module = importlib.util.module_from_spec(spec)
    module.__file__ = str(LAB / PRIVATE_POLICY_HELPER_PATH)
    sys.modules[spec.name] = module
    exec(compile(raw, module.__file__, 'exec'), module.__dict__)
    return module

def private_policy_enabled(inputs):
    # No dependency load or extra behavior when this field is absent.
    record = json.loads(inputs.contents['build_record'], object_pairs_hook=image.unique)
    if 'native_policy_experiment' not in record:
        return None
    return private_policy_helper().verified_mode(inputs, native_bootargs_enabled)


NATIVE_BOOTARGS_PROFILE = 'darwin-xnu-e7-arm64-bootargs1152-r2v2-v1'
NATIVE_BOOTARGS_SOURCES = {'integrations/puredarwin/m1-apcie-bridge/native_bootargs/native_bootargs.py': 'f168d523eacf1c8fe1228ca48e467fb98fea47b4675bb942ca945aae63233fae', 'integrations/puredarwin/m1-apcie-bridge/native_bootargs_abi/verify_kernel.py': '03df91f4e0edc48fe69a661305e2f4d7dab874799ab97f5fe0cb33dc64308e3f', 'integrations/puredarwin/m1-apcie-bridge/native_bootargs_abi/PROFILE.json': 'c4abef6755105ce1b221ee90f2cdc3b5408746b286eb845715ff3ce1f902d990'}
NATIVE_BOOTARGS_HV_SHA = '18da80e496122233cbd3d69a287e5737ca78da083e8e36c8a33b01b35744c4a6'
NATIVE_BOOTARGS_EXPERIMENT = {'version': 1, 'wire_profile': NATIVE_BOOTARGS_PROFILE,
                             'source_revision': 2, 'source_version': 2}


def _native_bootargs_module(path, name):
    raw = image.bounded_file(LAB / path, 128 << 10)
    image.require(image.sha(raw) == NATIVE_BOOTARGS_SOURCES[path], 'Native BootArgs source changed: ' + path)
    spec = importlib.util.spec_from_loader(name, loader=None, origin=str(LAB / path))
    module = importlib.util.module_from_spec(spec)
    module.__file__ = str(LAB / path)
    sys.modules[name] = module
    # Execute precisely the bytes just hashed, never re-read via exec_module.
    exec(compile(raw, module.__file__, 'exec'), module.__dict__)
    return module


def _native_bootargs_helpers():
    for name, expected in NATIVE_BOOTARGS_SOURCES.items():
        image.require(image.sha(image.bounded_file(LAB / name, 128 << 10)) == expected,
                      'Native BootArgs dependency changed: ' + name)
    codec = _native_bootargs_module('integrations/puredarwin/m1-apcie-bridge/native_bootargs/native_bootargs.py',
                                   'bound_native_bootargs_codec')
    verifier = _native_bootargs_module('integrations/puredarwin/m1-apcie-bridge/native_bootargs_abi/verify_kernel.py',
                                      'bound_native_bootargs_abi')
    return codec, verifier


def native_bootargs_enabled(inputs):
    record = json.loads(inputs.contents['build_record'], object_pairs_hook=image.unique)
    if 'native_bootargs_wire_experiment' not in record:
        return None
    experiment = record['native_bootargs_wire_experiment']
    image.require(type(experiment) is dict and experiment.keys() == NATIVE_BOOTARGS_EXPERIMENT.keys() and
                  all(type(experiment[k]) is type(v) and experiment[k] == v
                      for k, v in NATIVE_BOOTARGS_EXPERIMENT.items()), 'Invalid native BootArgs wire experiment')
    hv_source = inputs.host_files.get('proxyclient/m1n1/hv/__init__.py')
    image.require(type(hv_source) is bytes and image.sha(hv_source) == NATIVE_BOOTARGS_HV_SHA,
                  'Native BootArgs requires the exact reviewed HV last-writer path')
    command = guest_bootargs(inputs).encode('ascii')
    # Pinned load_raw still performs its initial legacy r2 write before our final rewrite.
    image.require(len(command) <= 607 and b'\0' not in command, 'Initial r2 BootArgs command exceeds607 bytes')
    codec, verifier = _native_bootargs_helpers()
    proof = verifier.verify_kernel(inputs.contents['kernel'])
    image.require(codec.WIRE_PROFILE == proof['wire_profile'] == NATIVE_BOOTARGS_PROFILE and
                  proof['status'] == 'COMPILED_KERNEL_ABI_MATCH_ONLY' and
                  proof['image_sha256'] == image.sha(inputs.contents['kernel']) and
                  proof['runtime_or_boot_admitted'] is False, 'Selected kernel native BootArgs ABI mismatch')
    return dict(abi=proof, command_bytes=len(command), command_sha256=image.sha(command),
                sources_sha256=dict(NATIVE_BOOTARGS_SOURCES), initial_legacy_cmdline_capacity=607)


def _native_integer(value, bits, label):
    image.require(isinstance(value, int) and not isinstance(value, bool), 'Invalid native field: ' + label)
    # Bypass a hypothetical subclass __int__/comparison override; Construct Hex values are integers.
    result = int.__int__(value)
    image.require(type(result) is int and 0 <= result < (1 << bits), 'Invalid native field width: ' + label)
    return result


def _native_values(tba):
    values = {k: _native_integer(getattr(tba, k), 16 if k in ('revision', 'version') else
                                32 if k in ('machine_type', 'devtree_size') else 64, k)
              for k in ('revision', 'version', 'virt_base', 'phys_base', 'mem_size', 'top_of_kernel_data',
                        'machine_type', 'devtree', 'devtree_size', 'boot_flags', 'mem_size_actual')}
    values['video'] = {k: _native_integer(getattr(tba.video, k), 64, 'video.' + k)
                       for k in ('base', 'display', 'stride', 'width', 'height', 'depth')}
    image.require(type(tba.cmdline) is str, 'Invalid native cmdline type')
    values['cmdline'] = tba.cmdline
    return values


def _native_bootargs_pointer_state(hv):
    guest_base = _native_integer(hv.guest_base, 64, 'guest_base')
    bootargs_off = _native_integer(hv.bootargs_off, 64, 'bootargs_off')
    address = guest_base + bootargs_off
    image.require(address < 1 << 64, 'Native BootArgs address overflow')
    return dict(guest_base=guest_base, bootargs_off=bootargs_off, address=address,
                adt_base=_native_integer(hv.adt_base, 64, 'adt_base'),
                entry=_native_integer(hv.entry, 64, 'entry'))


def _native_bootargs_start_wrapper(hv, metadata):
    # Capture private plain values, not a reference to caller-mutable evidence.
    expected = dict(metadata['start_pointer_state'])
    original_start = hv.start
    def checked_start(*args, **kwargs):
        image.require(_native_bootargs_pointer_state(hv) == expected,
                      'Native BootArgs pointer state changed before HV.start')
        return original_start(*args, **kwargs)
    return checked_start


def _write_native_bootargs(hv, backend, inputs, directory, reservation, carveouts, adt_address):
    image.require(directory is not None and type(reservation) is dict and type(carveouts) is list,
                  'Native BootArgs requires private evidence and checked reservation')
    proof = native_bootargs_enabled(inputs)
    image.require(proof is not None, 'Native BootArgs opt-in missing at final writer')
    codec, _ = _native_bootargs_helpers()
    values = _native_values(hv.tba)
    command = values['cmdline'].encode('ascii')
    image.require(len(command) <= 607 and image.sha(command) == proof['command_sha256'], 'Changed native cmdline')
    image.require((values['revision'], values['version']) == (2, 2), 'Native source ABI is not R2/V2')
    pointer_state = _native_bootargs_pointer_state(hv)
    address = pointer_state['address']
    image.require(pointer_state['adt_base'] == adt_address, 'Native BootArgs ADT destination changed before write')
    pair = hv.adt['chosen']['memory-map'].BootArgs
    image.require(isinstance(pair, (tuple, list)) and len(pair) == 2, 'Missing exact BootArgs allocation')
    base = _native_integer(pair[0], 64, 'BootArgs.base')
    capacity = _native_integer(pair[1], 64, 'BootArgs.bytes')
    image.require(base == address and capacity == old.PAGE and base % old.PAGE == 0 and
                  address + capacity <= adt_address, 'Wrong existing BootArgs allocation')
    high = _native_integer(backend.ba.phys_base, 64, 'original.phys_base') + _native_integer(backend.ba.mem_size, 64, 'original.mem_size')
    image.require(values['phys_base'] + values['mem_size'] == high and values['top_of_kernel_data'] <= high,
                  'Changed guest envelope/top')
    old.inside(base, capacity, values['phys_base'], high, 'Native BootArgs guest envelope')
    reserved = (_native_integer(reservation['start'], 64, 'reservation.start'),
                _native_integer(reservation['end'], 64, 'reservation.end'))
    exclusions = tuple(tuple(pair) for pair in carveouts)
    old.inside(base, capacity, reserved[0], reserved[1], 'Native BootArgs reviewed reservation')
    for start, size in carveouts:
        image.require(type(start) is int and type(size) is int and start > 0 and size > 0 and
                      start + size < 1 << 64 and max(base, start) >= min(base + capacity, start + size),
                      'Native BootArgs allocation intersects MCC or invalid exclusion')
    contract = dict(version=1, scope='source-contract binding, NOT hardware authorization',
                    baseline_launcher_sha256='0cf9ccc9bbb966dbf43d707488956baede0cd917ffce08ce3260ff94cc9a946f',
                    source_sha256=NATIVE_BOOTARGS_SOURCES, HV_sha256=NATIVE_BOOTARGS_HV_SHA,
                    rule='existing chosen BootArgs exactly one16KiB allocation; same predicted address; inside guest/payload reservation; MCC disjoint')
    contract_sha = image.sha(json.dumps(contract, sort_keys=True, separators=(',', ':')).encode())
    plan = codec.plan_write(values, wire_profile=NATIVE_BOOTARGS_PROFILE, address=address,
                            allocation=dict(base=base, bytes=capacity, review_sha256=contract_sha))
    backend.iface.writemem(plan.address, plan.payload)  # Exactly one opted-in final BA write.
    actual = backend.iface.readmem(plan.address, len(plan.payload))  # Exact bounded readback.
    if type(actual) is bytes and 0 < len(actual) <= codec.WIRE_SIZE:
        old.write_private(directory, 'guest-bootargs-readback.bin', actual)
    image.require(type(actual) is bytes and actual == plan.payload, 'Native BootArgs short/corrupt readback')
    final_pair = hv.adt['chosen']['memory-map'].BootArgs
    image.require(_native_bootargs_pointer_state(hv) == pointer_state and
                  _native_values(hv.tba) == values and isinstance(final_pair, (tuple, list)) and
                  len(final_pair) == 2 and
                  tuple(_native_integer(v, 64, 'final BootArgs allocation') for v in final_pair) == (base, capacity) and
                  (reservation['start'], reservation['end']) == reserved and
                  tuple(tuple(pair) for pair in carveouts) == exclusions and
                  image.sha(inputs.contents['kernel']) == proof['abi']['image_sha256'],
                  'Native BootArgs fields/allocation/provenance changed during write/readback')
    old.write_private(directory, 'guest-bootargs.bin', actual)
    metadata = dict(plan.metadata(), abi=proof['abi'], source_contract=contract,
                    start_pointer_state=pointer_state,
                    checked_allocation=dict(base=base, bytes=capacity, expected_address=address,
                                            guest_low=values['phys_base'], guest_high=high,
                                            reservation_start=reserved[0], reservation_end=reserved[1],
                                            MCC_exclusion_count=len(exclusions),
                                            MCC_exclusions_sha256=image.sha(json.dumps(exclusions, separators=(',', ':')).encode()),
                                            MCC_scope='existing bounded four-TZ list, not global completeness'),
                    readback_sha256=image.sha(actual), readback_exact=True,
                    source_fields_sha256=image.sha(json.dumps(values, sort_keys=True, separators=(',', ':')).encode()),
                    initial_legacy_write_preserved=True, runtime_or_boot_admitted=False)
    old.write_private(directory, 'guest-bootargs-record.json', metadata)
    return metadata


class ContinuingBackend(old.PinnedBackend):
    """Frozen collection method, with bounded uint64 BootArgs translation."""
    def collect(self, inputs):
        base = self.p.get_base()
        old.require(type(base) is int and base > 0 and base % old.PAGE == 0 and
                base + len(inputs.contents['m1n1']) < 1 << 64, 'Invalid live proxy base')
        tag_at = inputs.contents['m1n1'].index(old.TAG)
        old.require(self.iface.readmem(base + tag_at, len(old.TAG)) == old.TAG,
                'Live proxy BUILD_TAG differs from the pinned image')
        self.features = self.p.get_cpu_features()  # Target rejects any size other than 20.
        old.require(self.p.iodev_whoami() in (self.proxy_module.IODEV.USB0, self.proxy_module.IODEV.USB1),
                'J293 launcher requires an already working USB0/USB1 proxy')
        addr, rev = self.p.get_bootargs_rev()
        old.require(rev in (1, 2, 3), 'Unknown live BootArgs revision')
        fmt = getattr(self.types, 'BootArgs_r' + str(rev))
        raw_ba = self.iface.readmem(addr, fmt.sizeof())
        self.ba = fmt.parse(raw_ba)
        old.require(self.ba.revision == rev, 'BootArgs revision changed while reading')
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
            'identity': {'build_tag': old.packet.M1N1_PIN, 'packet_source_pin': old.packet.M1N1_PIN,
                         'build_source_pin': old.packet.M1N1_PIN,
                         'stage1': inherited['reported_m1n1_stage1_version'],
                         'stage2': inherited['reported_m1n1_stage2_version']},
            'hardware_tested': False,
        }
        old.require(tree.target_type == 'J293' and tree.model == 'MacBookPro17,1' and
                chosen.chip_id == old.preflight.CHIP_ID == snapshot['chip']['proxy_chip_id'] and
                uart_addr == old.preflight.UART_ADDR and
                sum(cpu['state'] == 'running' for cpu in snapshot['cpus']) == 1,
                'Live board/chip/UART/CPU selection mismatch before utility RAM allocation')
        extra = [(*mm.preoslog, 'preoslog')] if hasattr(mm, 'preoslog') else []
        heap_start = self.p.heapblock_alloc(0)
        self.layout = old.validate_layout(self.ba, snapshot, heap_start, inputs.report['kernel'], extra)
        self.u = self.utils_class(self.p, heap_size=old.HOST_HEAP, m1n1_heap=old.HEAP_RESERVE)
        old.require(self.u.ba == self.ba and self.u.heap_top == self.layout['phys_base'] and
                self.u.cpu_features == self.features, 'Live metadata/heap changed during collection')
        self.u.adt_data = raw_adt  # Preserve the exact observed bytes, not a later reread.
        snapshot['guest'] = self.guest_registers()
        self.snapshot = snapshot
        return snapshot, raw_ba, raw_adt, self.layout


BOOTARGS = ('-v -s debug=0x14e keepsyms=1 serial=3 serial-device-name=uart0 '
            'disable-uart-irq=1 cpus=1 rd=md0 serial_video_mirror=1 pdtextboot=1')


def framebuffer_geometry(video, backend, carveouts):
    fields = {name: int(getattr(video, name)) for name in
              ('base', 'stride', 'width', 'height', 'depth')}
    image.require(fields['base'] > 0 and fields['base'] % 4 == 0 and
                  0 < fields['width'] <= 8192 and 0 < fields['height'] <= 8192 and
                  fields['depth'] & 0xff in (30, 32) and
                  fields['width'] * 4 <= fields['stride'] <= 65536 and
                  fields['stride'] * fields['height'] <= 64 << 20,
                  'Invalid firmware framebuffer geometry for text console')
    size = fields['stride'] * fields['height']
    low = backend.ba.phys_base & ~0xffffffff
    old.inside(fields['base'], size, low, low + backend.ba.mem_size_actual,
               'Firmware framebuffer physical DRAM span')
    for address, length in carveouts:
        image.require(max(fields['base'], address) >= min(fields['base'] + size, address + length),
                      'Firmware framebuffer intersects MCC carveout')
    return fields


def configure_display_console(hv, backend, carveouts):
    # Pinned HV.set_bootargs(-v) uses legacy display=0. This kernel requires
    # nonzero to enter PE_create_console's video/mirror branch; 2 means text.
    # Retain firmware's actual framebuffer, pixel format, stride and geometry.
    video = hv.tba.video
    fields = framebuffer_geometry(video, backend, carveouts)
    video.display = 2
    return dict(guest_display=2, mode='text', geometry=fields,
                serial_video_mirror=True, pdtextboot=True,
                framebuffer_geometry_preserved=True, physical_panel_observed=False)


def configure_entropy(hv):
    # ARM64 prng_random.c requests 4*SHA512_DIGEST_LENGTH (256 bytes).
    # Fresh OS CSPRNG output, never repetition/padding or a weakened XNU check.
    seed = secrets.token_bytes(256)
    image.require(isinstance(seed, bytes) and len(seed) == 256, 'OS boot entropy unavailable')
    chosen = hv.adt['chosen']
    chosen.random_seed = seed
    chosen._types['random-seed'] = (None, False)  # Exact raw bytes, no integer guessing.
    return dict(source='M5 OS CSPRNG via secrets.token_bytes', bytes=256, raw_value_recorded=False)


def configure_kernel_text_policy(hv, inputs):
    # DEBUG XNU initializes ml_unsafe_kernel_text() only from this four-byte
    # bootloader property. Describe this guest's existing build selection;
    # never inherit a firmware policy for a different kernel or claim CTRR.
    record = json.loads(inputs.contents['build_record'], object_pairs_hook=image.unique)
    policy = record.get('components', {}).get('native_kernel', {}).get(
        'development_protection_configuration', {})
    omitted = policy.get('implementation_selectors_omitted')
    image.require(isinstance(omitted, list) and all(isinstance(s, str) for s in omitted) and
                  {'KERNEL_INTEGRITY_CTRR', 'XNU_MONITOR', 'PMAP_CS', 'PMAP_CS_ENABLE'} <= set(omitted) and
                  policy.get('kernel_text_hardware_locked') is False and
                  policy.get('PPL_protection_claimed') is False,
                  'Guest kernel text policy requires an explicit unprotected development build receipt')
    chosen = hv.adt['chosen']
    chosen.kernel_ctrr_to_be_enabled = struct.pack('<I', 0)
    chosen._types['kernel-ctrr-to-be-enabled'] = (None, False)
    return dict(property='/chosen/kernel-ctrr-to-be-enabled', bytes=4, encoding='little-endian uint32',
                value=0, guest_only=True, source='verified build receipt development_protection_configuration',
                kernel_text_hardware_locked=False, PPL_protection_claimed=False)


def verify_inputs(args):
    proxy = old.verify_inputs(args.bootstrap_manifest, args.host_source, args.inventory)
    report, contents = image.verify_manifest(args.manifest)
    report['bootstrap_packet'] = proxy.report
    inputs = old.Inputs(report, {**proxy.contents, **contents}, proxy.host_files,
                        proxy.inventory, proxy.inventory_sha256)
    # Validate opt-ins on the controller before an orderly target reboot too.
    keyboard_enabled(inputs)
    desktop_enabled(inputs)
    virtual_input_enabled(inputs)
    terminal_input_enabled(inputs)
    native_keyboard_desktop_enabled(inputs)
    dart_survey_enabled(inputs)
    trackpad_enumeration_enabled(inputs)
    native_trackpad_child_enabled(inputs)
    native_wire = native_bootargs_enabled(inputs)
    private_policy = private_policy_enabled(inputs)
    if private_policy is not None:
        report['native_private_policy_sources'] = private_policy
    if native_wire is not None:
        report['native_bootargs_wire_abi'] = native_wire
    return inputs


def cdc_pair(raw, primary):
    """Identify both CDC data endpoints on one exact USB enumeration."""
    roots = plistlib.loads(raw) if raw.strip() else []
    image.require(isinstance(roots, list), 'Invalid USB registry roots')
    pairs = []
    for root in roots:
        if (root.get('idVendor'), root.get('idProduct'), root.get('locationID')) != (0x1209, 0x316d, 0x100000):
            continue
        identity = {key: root.get(key) for key in ('IORegistryEntryID', 'sessionID')}
        image.require(all(type(n) is int and n > 0 for n in identity.values()), 'Missing USB identity')
        found = {1: [], 3: []}
        def walk(node, interface=None):
            interface = node.get('bInterfaceNumber', interface)
            if 'IOCalloutDevice' in node and interface in found:
                port = node['IOCalloutDevice']
                image.require(isinstance(port, str) and re.fullmatch(r'/dev/cu\.usbmodem[A-Za-z0-9_.-]+', port),
                              'Invalid CDC callout')
                found[interface].append(port)
            for child in node.get('IORegistryEntryChildren', []):
                walk(child, interface)
        walk(root)
        image.require(all(len(ports) <= 1 for ports in found.values()), 'Ambiguous CDC interface')
        if found[1] == [primary] and len(found[3]) == 1:
            image.require(found[3][0] != primary, 'VUART cannot be the proxy endpoint')
            pairs.append(dict(primary=primary, console=found[3][0], **identity))
    image.require(len(pairs) == 1, 'No unique proxy/VUART pair on the selected enumeration')
    return pairs[0]


class ConsoleProof:
    """An echoed command cannot satisfy the split-literal response challenge."""
    def __init__(self, nonce=None):
        self.nonce = nonce or secrets.token_hex(24)
        image.require(bool(re.fullmatch('[0-9a-f]{48}', self.nonce)), 'Invalid console challenge')
        self.output = bytearray()
        self.sent_at = None
        self.complete = False

    def command(self):
        a, b = self.nonce[:24], self.nonce[24:]
        # Never send a complete expected marker as contiguous input bytes.
        return ("PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH; "
                f"printf '\\nDL9_BEGIN_%s%s\\n' '{a}' '{b}'; "
                "{ uname -s; uname -m; uname -r; } 2>/dev/null; "
                f"printf 'DL9_END_%s%s\\n' '{a}' '{b}'\r").encode('ascii')

    def sent(self):
        image.require(self.sent_at is None, 'Challenge is single-use')
        self.sent_at = len(self.output)

    def feed(self, data):
        self.output.extend(data)
        image.require(len(self.output) <= 32 << 20, 'Console log exceeds experiment bound')
        if self.sent_at is None:
            return False
        normalized = bytes(self.output[self.sent_at:]).replace(b'\r', b'')
        response = (b'\nDL9_BEGIN_' + self.nonce.encode() +
                    rb'\nDarwin\narm64\n[0-9][0-9A-Za-z_.+-]{0,100}\nDL9_END_' +
                    self.nonce.encode() + b'\n')
        self.complete = re.search(response, normalized) is not None
        return self.complete


def console_prompt_text(data):
    # The real single-user zsh prompt includes CSI colors/erase/bracketed-paste
    # modes and terminfo NUL padding. Normalize only prompt detection. Keep the
    # raw log and strict, nonce-bound response proof separate.
    data = data.replace(b'\0', b'')
    data = re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]', b'', data)
    return data.replace(b'\r', b'\n')


class ConsoleSession:
    """One reader/writer for the secondary CDC, never the proxy transport."""
    def __init__(self, port, directory, serial_factory=None, desktop=False, keyboard=False,
                 virtual_input=False, terminal_input=False, native_keyboard_desktop=False, native_trackpad_child=False, private_policy=False, policy_nonce=None):
        image.require(all(type(value) is bool for value in
                          (desktop, keyboard, virtual_input, terminal_input, native_keyboard_desktop, native_trackpad_child, private_policy)), 'Experiment flags must be booleans')
        image.require(not native_keyboard_desktop or (desktop and not keyboard and not virtual_input and not terminal_input),
                      'Native keyboard desktop forbids host HID experiments')
        image.require(not native_trackpad_child or (desktop and not keyboard and not virtual_input and
                      not terminal_input and not native_keyboard_desktop),
                      'Passive child desktop requires exclusive sealed native input')
        image.require(not terminal_input or (desktop and not keyboard and not virtual_input),
                      'Terminal input needs an exclusive desktop experiment')
        image.require(not (desktop and keyboard), 'Only one guest experiment per attempt')
        image.require(not virtual_input or (desktop and not keyboard), 'Virtual input needs the desktop experiment')
        image.require(not private_policy or not any((desktop, keyboard, virtual_input, terminal_input, native_keyboard_desktop, native_trackpad_child)), 'Private policy console requires exclusive mode')
        if private_policy:
            private_policy_helper().command(policy_nonce)
            private_proof = ConsoleProof()
            image.require(private_proof.nonce != policy_nonce, 'Independent policy and console nonces collided')
        else:
            image.require(policy_nonce is None, 'Unexpected policy nonce outside private mode')
        self.private_policy, self.policy_nonce, self.private_sent = private_policy, policy_nonce, False
        if serial_factory is None:
            import serial
            serial_factory = serial.Serial
        self.dev = serial_factory(port, baudrate=115200, timeout=.1, write_timeout=2,
                                  xonxoff=False, rtscts=False, dsrdtr=False, exclusive=True)
        self.directory, self.proof = directory, private_proof if private_policy else ConsoleProof()
        self.native_keyboard_desktop = native_keyboard_desktop
        self.native_trackpad_child = native_trackpad_child
        self.native_input_sealed = native_keyboard_desktop or native_trackpad_child or private_policy
        self.native_tx_phase = 'initial'
        self.native_shell_path_sent = False
        self.terminal_input = terminal_input
        self.virtual = (TerminalInput(self.proof.nonce) if terminal_input else
                        VirtualInput(self.proof.nonce) if virtual_input else None)
        self.desktop, self.desktop_sent = desktop, False
        self.keyboard, self.keyboard_sent = keyboard, False
        self.tx_data, self.tx_log = bytearray(), None
        self.close_attempted, self.close_error = False, None
        self.finish = threading.Event()
        self.error = None
        self.thread = threading.Thread(target=self.read, name='guest-vuart', daemon=True)
        self.thread.start()

    def read(self):
        try:
            fd = os.open(self.directory / 'console.bin', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with (os.fdopen(fd, 'wb', buffering=0) as log,
                  os.fdopen(os.open(self.directory / 'console-tx.bin',
                                   os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600),
                            'wb', buffering=0) as tx_log):
                self.tx_log = tx_log
                shell_path_sent = False
                while not self.finish.is_set():
                    data = self.dev.read(4096)
                    if not data:
                        self.advance_virtual(b'')
                        continue
                    log.write(data)
                    self.proof.feed(data)
                    tail = console_prompt_text(bytes(self.proof.output[-1024:]))
                    if not shell_path_sent and self.proof.sent_at is None and b'Enter full pathname of shell' in tail:
                        shell_path_sent = True
                        self.send(b'/bin/sh\r')
                    if self.proof.sent_at is None and re.search(rb'(?:^|\n)[^\n]{0,100}[#$] ?$', tail):
                        self.proof.sent()
                        old.write_private(self.directory, 'challenge-input.bin', self.proof.command())
                        self.send(self.proof.command())
                    elif self.private_policy and self.proof.complete and not self.private_sent and re.search(
                            rb'(?:^|\n)[^\n]{0,100}[#$] ?$', tail):
                        command = private_policy_helper().command(self.policy_nonce)
                        old.write_private(self.directory, 'private-policy-input.bin', command)
                        self.private_sent = True
                        self.send(command)
                    elif self.desktop and self.proof.complete and not self.desktop_sent and re.search(
                            rb'(?:^|\n)[^\n]{0,100}[#$] ?$', tail):
                        # Fixed, single-use RAM-root experiment. Never accept a
                        # command string from guest output or arbitrary metadata.
                        command = desktop_command(self.proof.nonce)
                        old.write_private(self.directory, 'desktop-input.bin', command)
                        self.desktop_sent = True
                        self.send(command)
                    elif self.keyboard and self.proof.complete and not self.keyboard_sent and re.search(
                            rb'(?:^|\n)[^\n]{0,100}[#$] ?$', tail):
                        command = keyboard_command(self.proof.nonce)
                        old.write_private(self.directory, 'keyboard-input.bin', command)
                        self.keyboard_sent = True
                        self.send(command)
                    self.advance_virtual(data)
        except BaseException as error:
            self.error = f'{type(error).__name__}: {error}'
            if self.virtual:
                self.virtual.fail(self.error)

    def advance_virtual(self, data):
        if not self.virtual or not self.desktop_sent or self.finish.is_set():
            return
        now = time.monotonic()
        self.virtual.feed(data, now)
        payload = self.virtual.tick(now)
        if self.virtual.error:
            # Observation failures cancel input, while the same owner keeps
            # draining raw diagnostics. A write/ledger failure still raises.
            self.virtual.cancel()
            kind = 'TerminalInputError: ' if self.terminal_input else 'VirtualInputError: '
            self.error = self.error or kind + self.virtual.error
            return
        if payload is not None:
            self.send(payload)

    def send(self, data):
        image.require(not self.finish.is_set() and isinstance(data, bytes) and self.tx_log is not None and
                      len(self.tx_data)+len(data) <= 16384, 'Invalid/unrecorded VUART write')
        if self.native_input_sealed:
            # This mode permits only the existing bootstrap and fixed launcher.
            # Seal the writer before attempting that launcher, including failure.
            if self.native_tx_phase == 'initial':
                if data == b'/bin/sh\r':
                    image.require(not self.native_shell_path_sent, 'Repeated native-mode shell selection')
                    self.native_shell_path_sent = True
                else:
                    image.require(data == self.proof.command(), 'Native mode rejects non-bootstrap VUART input')
                    self.native_tx_phase = 'await_launcher'
            elif self.native_tx_phase == 'await_launcher':
                image.require(self.proof.complete and
                              ((self.private_policy and self.private_sent and data == private_policy_helper().command(self.policy_nonce)) or
                               (not self.private_policy and self.desktop_sent and data == desktop_command(self.proof.nonce))),
                              'Native mode requires its one fixed desktop launcher')
                self.native_tx_phase = 'sealed'
            else:
                image.require(False, 'Native mode VUART writer is sealed after launcher')
        # Record the exact attempted bytes durably before sending. A short or
        # failed write sets error and prevents any successful evidence claim.
        image.require(self.tx_log.write(data) == len(data), 'Short VUART transcript write')
        os.fsync(self.tx_log.fileno())
        self.tx_data.extend(data)
        image.require(self.dev.write(data) == len(data), 'Short VUART command write')

    def close(self):
        if self.close_attempted:
            image.require(self.close_error is None, self.close_error or 'Console close failed')
            return
        self.close_attempted = True
        try:
            self._finalize()
        except BaseException as error:
            self.close_error = f'{type(error).__name__}: {error}'
            raise

    def _finalize(self):
        self.finish.set()
        self.thread.join(3)
        image.require(not self.thread.is_alive(), 'VUART worker state unknown')
        if self.virtual:
            self.virtual.cancel()
            name = 'terminal-input-result.json' if self.terminal_input else 'virtual-input-result.json'
            old.write_private(self.directory, name, self.virtual.report())
        self.dev.close()
        raw = bytes(self.proof.output)
        old.write_private(self.directory, 'console-result.json', {
            'command_line_observed': self.proof.complete and self.error is None,
            'evidence': 'guest_console_challenge; requires matching real-target launch/recovery record',
            'nonce': self.proof.nonce, 'challenge_sent': self.proof.sent_at is not None,
            'console_sha256': image.sha(raw), 'console_bytes': len(raw), 'error': self.error,
            'desktop_command_sent': self.desktop_sent,
            'keyboard_command_sent': self.keyboard_sent,
            'terminal_input_enabled': self.terminal_input,
            'native_keyboard_desktop_enabled': self.native_keyboard_desktop,
            'native_trackpad_child_enabled': self.native_trackpad_child,
            'native_mode_host_write_phase': self.native_tx_phase if self.native_input_sealed else None,
            'console_tx_bytes': len(self.tx_data),
            'console_tx_sha256': image.sha(bytes(self.tx_data)),
            'tx_scope': 'all attempted bytes to secondary VUART; exact successful writes only if error is null',
            **(dict(native_private_policy_enabled=True, policy_nonce=self.policy_nonce,
                    private_command_sent=self.private_sent) if self.private_policy else {}),
        })


def desktop_command(nonce):
    image.require(bool(re.fullmatch('[0-9a-f]{48}', nonce)), 'Invalid desktop nonce')
    return ("/usr/bin/darwin-desktop-test '" + nonce + "'\r").encode('ascii')


def _reject_child_conflict(record, key):
    image.require(type(record) is dict, 'Experiment build record must be an object')
    image.require(not ('native_trackpad_child_experiment' in record and key in record),
                  'Conflicting passive child and legacy experiment fields')


def desktop_enabled(inputs):
    record = json.loads(inputs.contents['build_record'])
    experiment = record.get('desktop_experiment')
    if experiment is None:
        return False
    image.require(isinstance(experiment, dict) and type(experiment.get('version')) is int and
                  experiment == {'version': 1, 'command': '/usr/bin/darwin-desktop-test'}
                  and 'ramdisk' in inputs.contents,
                  'Unknown desktop experiment or missing hash-bound RAM root')
    return True


def virtual_input_enabled(inputs):
    record = json.loads(inputs.contents['build_record'])
    _reject_child_conflict(record, 'virtual_input_experiment')
    image.require(not ('terminal_input_experiment' in record and 'virtual_input_experiment' in record),
                  'Conflicting terminal/virtual-input experiment fields')
    experiment = record.get('virtual_input_experiment')
    if experiment is None:
        return False
    expected = {'version': 1, 'origin': 'host_pdserialhid', 'output': [2560, 1600],
                'client': [800, 480], 'content_origin': [1, 24]}
    image.require(isinstance(experiment, dict) and type(experiment.get('version')) is int and
                  all(isinstance(experiment.get(k), list) and
                      all(type(n) is int for n in experiment[k])
                      for k in ('output', 'client', 'content_origin')) and
                  experiment == expected and desktop_enabled(inputs) and not keyboard_enabled(inputs),
                  'Unknown virtual-input experiment or missing desktop opt-in')
    members = inputs.report['kernel']['members']
    image.require('org.puredarwin.driver.PDSerialHID' in members and
                  'com.apple.iokit.IOHIDFamily' in members and
                  'org.puredarwin.driver.PDM1SPIHID' not in members,
                  'Virtual-input collection must contain the real serial HID path only')
    return True


def terminal_input_enabled(inputs):
    record = json.loads(inputs.contents['build_record'], object_pairs_hook=image.unique)
    _reject_child_conflict(record, 'terminal_input_experiment')
    image.require(type(record) is dict, 'Terminal input requires an object build record')
    if 'terminal_input_experiment' not in record:
        return False
    experiment = record['terminal_input_experiment']
    expected = {'version': 1, 'origin': 'host_pdserialhid',
                'command': 'split_nonce_uname_v1', 'output': [2560, 1600]}
    image.require(type(experiment) is dict and type(experiment.get('version')) is int and
                  type(experiment.get('origin')) is str and type(experiment.get('command')) is str and
                  type(experiment.get('output')) is list and
                  all(type(value) is int for value in experiment['output']) and experiment == expected,
                  'Unknown terminal-input experiment')
    # Field presence is a conflict even when its JSON value is null/false.
    image.require('virtual_input_experiment' not in record and 'keyboard_experiment' not in record and
                  desktop_enabled(inputs) and 'ramdisk' in inputs.contents,
                  'Terminal input requires exclusive desktop opt-in and hash-bound RAM root')
    members = inputs.report['kernel']['members']
    image.require('org.puredarwin.driver.PDSerialHID' in members and
                  'com.apple.iokit.IOHIDFamily' in members and
                  'org.puredarwin.driver.PDM1SPIHID' not in members,
                  'Terminal-input collection must contain the real serial HID path only')
    return True


def validate_input_geometry(geometry, *, virtual_input=False, terminal_input=False, native_keyboard_desktop=False, native_trackpad_child=False):
    image.require(all(type(x) is bool for x in (virtual_input, terminal_input, native_keyboard_desktop, native_trackpad_child)) and
                  sum((virtual_input, terminal_input, native_keyboard_desktop, native_trackpad_child)) <= 1,
                  'Input geometry flags must be exclusive booleans')
    if virtual_input or terminal_input or native_keyboard_desktop or native_trackpad_child:
        image.require(all(type(geometry.get(key)) is int for key in ('width', 'height', 'stride', 'depth')) and
                      (geometry['width'], geometry['height'], geometry['stride'], geometry['depth'] & 255) ==
                      (2560, 1600, 10240, 30),
                      'Input output geometry differs from qualified framebuffer')


def native_keyboard_desktop_enabled(inputs):
    record = json.loads(inputs.contents['build_record'], object_pairs_hook=image.unique)
    _reject_child_conflict(record, 'native_keyboard_desktop_experiment')
    image.require(type(record) is dict, 'Native desktop requires an object build record')
    if 'native_keyboard_desktop_experiment' not in record:
        return False
    experiment = record['native_keyboard_desktop_experiment']
    expected = {'version': 1, 'driver': 'PDM1SPIHID', 'input': 'pdhid-keyboard', 'host_hid_injection': False}
    image.require(type(experiment) is dict and type(experiment.get('version')) is int and
                  type(experiment.get('driver')) is str and type(experiment.get('input')) is str and
                  type(experiment.get('host_hid_injection')) is bool and experiment == expected,
                  'Unknown native keyboard desktop experiment')
    image.require(all(k not in record for k in ('keyboard_experiment', 'virtual_input_experiment', 'terminal_input_experiment')) and
                  desktop_enabled(inputs) and 'ramdisk' in inputs.contents,
                  'Native keyboard desktop requires exclusive desktop opt-in and bound RAM')
    members = inputs.report['kernel']['members']
    image.require(all(n in members for n in ('org.puredarwin.driver.PDM1SPIHID',
                  'com.apple.iokit.IOHIDFamily', 'com.ravynos.iokit.IOGOPFramebuffer')),
                  'Native desktop requires the real SPI HID, HID family and framebuffer members')
    image.require('org.puredarwin.driver.PDSerialHID' not in members,
                  'Native keyboard desktop must omit the virtual serial HID driver')
    return verify_spi_keyboard_member(inputs)



def dart_survey_enabled(inputs):
    record = json.loads(inputs.contents['build_record'], object_pairs_hook=image.unique)
    _reject_child_conflict(record, 'dart_survey_experiment')
    image.require(type(record) is dict, 'DART survey requires an object build record')
    if 'dart_survey_experiment' not in record:
        return False
    experiment = record['dart_survey_experiment']
    expected = {'version': 1, 'driver': 'PDDARTSurvey', 'mode': 'read_only',
                'diagnostic_property': 'PDDARTSurveyDiagnosticV1', 'device_initialization': False}
    image.require(type(experiment) is dict and type(experiment.get('version')) is int and
                  type(experiment.get('device_initialization')) is bool and experiment == expected,
                  'Unknown read-only DART survey experiment')
    image.require(native_keyboard_desktop_enabled(inputs),
                  'DART survey requires sealed native-only desktop mode')
    name = 'org.puredarwin.driver.PDDARTSurvey'
    image.require(name in inputs.report['kernel']['members'], 'DART survey member missing')
    raw = inputs.contents['kernel']
    kind, _, commands = image.commands(raw)
    image.require(kind == 12, 'DART survey requires a native fileset collection')
    found = 0
    for cmd, off, size in commands:
        if cmd != 0x80000035:
            continue
        image.require(size >= 32, 'Truncated fileset entry')
        nameoff = struct.unpack_from('<I', raw, off + 24)[0]
        image.require(32 <= nameoff < size and b'\0' in raw[off + nameoff:off + size],
                      'Invalid fileset member name')
        found += raw[off + nameoff:off + size].split(b'\0', 1)[0] == name.encode('ascii')
    image.require(found == 1, 'DART survey requires exactly one built survey member')
    return True



def trackpad_enumeration_enabled(inputs):
    record = json.loads(inputs.contents['build_record'], object_pairs_hook=image.unique)
    _reject_child_conflict(record, 'trackpad_enumeration_experiment')
    image.require(type(record) is dict, 'Trackpad enumeration requires an object build record')
    if 'trackpad_enumeration_experiment' not in record:
        return False
    experiment = record['trackpad_enumeration_experiment']
    expected = {'version': 1, 'driver': 'PDM1SPIHID', 'interface': 2,
                'mode': 'enumeration_only', 'feature_writes': False,
                'diagnostic_property': 'PDM1TrackpadEnumerationV1'}
    image.require(type(experiment) is dict and type(experiment.get('version')) is int and
                  type(experiment.get('interface')) is int and
                  type(experiment.get('feature_writes')) is bool and experiment == expected,
                  'Unknown trackpad enumeration experiment')
    image.require('dart_survey_experiment' not in record and native_keyboard_desktop_enabled(inputs),
                  'Trackpad enumeration requires exclusive sealed native-only desktop mode')
    image.require('org.puredarwin.driver.PDDARTSurvey' not in inputs.report['kernel']['members'],
                  'Trackpad enumeration collection must omit unrelated DART experiment')
    return True


def native_trackpad_child_enabled(inputs):
    record = json.loads(inputs.contents['build_record'], object_pairs_hook=image.unique)
    image.require(type(record) is dict, 'Passive child requires an object build record')
    if 'native_trackpad_child_experiment' not in record:
        return False
    experiment = record['native_trackpad_child_experiment']
    expected = {'version': 1, 'driver': 'PDM1SPIHID', 'child': 'PDM1SPITrackpad',
                'interface': 2, 'mode': 'passive_child', 'input': 'pdhid',
                'feature_writes': False, 'host_hid_injection': False,
                'diagnostic_property': 'PDM1TrackpadChildStateV1'}
    image.require(type(experiment) is dict and type(experiment.get('version')) is int and
                  type(experiment.get('interface')) is int and
                  type(experiment.get('feature_writes')) is bool and
                  type(experiment.get('host_hid_injection')) is bool and experiment == expected,
                  'Unknown passive native trackpad child experiment')
    image.require(all(key not in record for key in (
                  'native_keyboard_desktop_experiment', 'trackpad_enumeration_experiment',
                  'dart_survey_experiment', 'keyboard_experiment', 'virtual_input_experiment',
                  'terminal_input_experiment')) and desktop_enabled(inputs) and 'ramdisk' in inputs.contents,
                  'Passive child needs exclusive desktop opt-in and bound RAM')
    members = inputs.report['kernel']['members']
    image.require(all(name in members for name in ('org.puredarwin.driver.PDM1SPIHID',
                  'com.apple.iokit.IOHIDFamily', 'com.ravynos.iokit.IOGOPFramebuffer')) and
                  all(name not in members for name in ('org.puredarwin.driver.PDSerialHID',
                  'org.puredarwin.driver.PDDARTSurvey')),
                  'Passive child requires native SPI/HID/framebuffer without serial HID or survey')
    return verify_spi_keyboard_member(inputs)


def guest_bootargs(inputs):
    record = json.loads(inputs.contents['build_record'])
    if 'native_policy_experiment' in record:
        image.require(private_policy_helper().typed_mode(inputs), 'Private mode disabled')
        return BOOTARGS + ' pdbcmprivate=1'
    child = native_trackpad_child_enabled(inputs)
    native = native_keyboard_desktop_enabled(inputs)
    keyboard = keyboard_enabled(inputs)
    survey = dart_survey_enabled(inputs)
    trackpad = trackpad_enumeration_enabled(inputs)
    return (BOOTARGS + (' pdm1kbd=1' if keyboard or native or child else '') +
            (' pddartsurvey=1' if survey else '') +
            (' pdm1trackpadenum=1' if trackpad or child else '') +
            (' pdm1trackpadchild=1' if child else ''))


def keyboard_command(nonce):
    image.require(bool(re.fullmatch('[0-9a-f]{48}', nonce)), 'Invalid keyboard nonce')
    return ("/usr/bin/darwin-keyboard-test '" + nonce + "'\r").encode('ascii')


def keyboard_enabled(inputs):
    record = json.loads(inputs.contents['build_record'])
    _reject_child_conflict(record, 'keyboard_experiment')
    image.require(not ('terminal_input_experiment' in record and 'keyboard_experiment' in record),
                  'Conflicting terminal/keyboard experiment fields')
    experiment = record.get('keyboard_experiment')
    if experiment is None:
        return False
    image.require(isinstance(experiment, dict) and type(experiment.get('version')) is int and
                  experiment == {'version': 1, 'command': '/usr/bin/darwin-keyboard-test'}
                  and 'ramdisk' in inputs.contents and not desktop_enabled(inputs),
                  'Unknown keyboard experiment or conflicting/missing RAM root')
    return verify_spi_keyboard_member(inputs)


def verify_spi_keyboard_member(inputs):
    raw = inputs.contents['kernel']
    kind, _, commands = image.commands(raw)
    image.require(kind == 12, 'Keyboard requires a native fileset collection')
    found = 0
    for cmd, off, size in commands:
        if cmd != 0x80000035:
            continue
        image.require(size >= 32, 'Truncated fileset entry')
        nameoff = struct.unpack_from('<I', raw, off+24)[0]
        image.require(32 <= nameoff < size and b'\0' in raw[off+nameoff:off+size],
                      'Invalid fileset member name')
        found += raw[off+nameoff:off+size].split(b'\0', 1)[0] == b'org.puredarwin.driver.PDM1SPIHID'
    image.require(found == 1, 'Opt-in requires exactly one built SPI HID member')
    return True


class ProgressCapture(old.ParkCapture):
    """Service pinned MMIO/sysreg handlers; capture instead of entering a shell."""
    ALLOWED = {(3, 1), (2, 0), (2, 2)}  # HOOK_VM, lower SYNC/sysreg emulation, lower FIQ

    def __init__(self, hv, backend, inputs, layout, directory, runtime,
                 console=None, capture_framebuffer=True):
        self.hv, self.iface, self.proxy = hv, backend.iface, backend.p
        self.backend, self.inputs, self.layout = backend, inputs, layout
        self.context_format, self.directory = backend.proxy_module.ExcInfo, directory
        self.start_args = (self.proxy.P_HV_START, layout['entry'],
                           layout['guest_base'] + layout['bootargs_off'], 0, 0, 0, 0)
        self.phase = 'preparing'
        self.reply_depth = self.cmd_depth = self.dispatch_depth = self.io_depth = 0
        self.requested = self.injected = self.closed = self.alarm_owned = False
        self.event = self.raw = self.context_error = None
        self.stopping = False
        self.DELAY = runtime
        self.console = console
        self.capture_framebuffer = capture_framebuffer
        self.diagnostic_step = 0

    def diagnostic_read(self, label, operation):
        image.require(self.raw and self.hv._in_handler and self.dispatch_depth >= 1,
                      'Diagnostic read requires the owned paused callback')
        self.diagnostic_step += 1
        image.require(self.diagnostic_step <= 32, 'Diagnostic step bound exceeded')
        name = f'diagnostic-{self.diagnostic_step:02d}'
        old.write_private(self.directory, name+'-started.json', dict(step=label))
        result = operation()
        old.write_private(self.directory, name+'-completed.json', dict(step=label))
        return result

    def diagnostics(self):
        result = {}
        self.diagnostic_partial = result
        # Only use the owned, paused callback to issue these reads. An unsafe
        # alarm boundary or malformed context never starts extra target I/O.
        if not self.raw or not self.hv._in_handler or self.dispatch_depth < 1:
            return result
        result.update(complete=False, framebuffer_requested=self.capture_framebuffer)
        ctx = self.context_format.parse(self.raw)
        kernel, layout = self.inputs.report['kernel'], self.layout
        virt = 0xfffffe0010000000 + (layout['phys_base'] & ((32 << 20)-1))
        runtime = virt + layout['guest_base'] - layout['phys_base']
        def read_data(linked, size):
            image.require(any(not s['initprot'] & 4 and s['vmaddr'] <= linked and
                              linked+size <= s['vmaddr']+s['vmsize'] for s in kernel['segments']),
                          'Diagnostic read is outside loaded kernel data')
            physical = layout['guest_base'] + linked - kernel['vmin']
            return self.iface.readmem(physical, size), physical
        values = {}
        for name, symbol in kernel.get('diagnostic_symbols', {}).items():
            raw, _ = self.diagnostic_read('global:'+name,
                lambda: read_data(symbol['address'], symbol['bytes']))
            values[name] = int.from_bytes(raw, 'little')
        result['globals'] = values
        result['sp'] = [int(x) for x in ctx.sp]
        fp = int(ctx.regs[29])
        linked = fp-runtime+kernel['vmin']
        # Bootstrap stack only, bounded to a single known data segment. Never
        # chase pointers or inspect an arbitrary post-bootstrap stack mapping.
        for segment in kernel['segments']:
            if not segment['initprot'] & 4 and segment['vmaddr'] <= linked < segment['vmaddr']+segment['vmsize']:
                start = max(segment['vmaddr'], (linked & ~15)-4096)
                end = min(segment['vmaddr']+segment['vmsize'], start+8192)
                raw, address = self.diagnostic_read('bootstrap-stack', lambda: read_data(start, end-start))
                old.write_private(self.directory, 'bootstrap-stack.bin', raw)
                result['bootstrap_stack'] = dict(physical=address, linked=start, bytes=len(raw), sha256=image.sha(raw))
                break
        result['el12'] = {name: int(self.diagnostic_read('register:'+name,
                                      lambda: self.backend.u.mrs(name))) for name in
                          ('SCTLR_EL12','TCR_EL12','TTBR0_EL12','TTBR1_EL12','ELR_EL12','ESR_EL12','FAR_EL12')}
        if self.capture_framebuffer and getattr(self.backend, 'display_console', None) is not None:
            # Read only the firmware-provided scanout buffer, with a fresh MCC
            # check, while the single guest CPU is paused in this owned callback.
            carveouts = self.diagnostic_read('framebuffer-carveouts', lambda: read_carveouts(self.backend))
            fields = framebuffer_geometry(self.backend.ba.video, self.backend, carveouts)
            size = fields['stride'] * fields['height']
            pixels = self.diagnostic_read('framebuffer-pixels', lambda: self.iface.readmem(fields['base'], size))
            image.require(len(pixels) == size, 'Short boot framebuffer capture')
            old.write_private(self.directory, 'framebuffer.bin', pixels)
            framebuffer = dict(geometry=fields, bytes=size, sha256=image.sha(pixels),
                               source='firmware boot framebuffer, guest paused',
                               physical_panel_observed=False)
            old.write_private(self.directory, 'framebuffer.json', framebuffer)
            result['framebuffer'] = framebuffer
        result['complete'] = True
        return result

    def stop(self, message):
        if self.closed or self.stopping:
            raise old.BoundedStop(message)
        self.stopping = True
        self.disarm()
        original_event = dict(self.event) if self.event is not None else None
        original_raw, original_error = self.raw, self.context_error
        if original_raw is not None:
            # Save before any optional target request. A nested callback or a
            # supervisor kill must not erase the context that triggered stop.
            old.write_private(self.directory, 'exception-context.bin', original_raw)
        close_error = None
        try:
            if self.console is not None:
                self.console.close()
        except Exception as error:
            close_error = f'{type(error).__name__}: {error}'
        core = dict(status='CONTINUING_XNU_CORE_CAPTURED', event=original_event,
                    console_capture_finalized=self.console is not None and close_error is None,
                    console_close_error=close_error, diagnostics_complete=False,
                    hardware_boot_observed=False)
        if original_raw is not None:
            core['raw_context_sha256'] = image.sha(original_raw)
            try:
                ctx = self.context_format.parse(original_raw)
                core['observed'] = {key: int(getattr(ctx, key)) for key in
                                   ('elr', 'spsr', 'esr', 'far', 'cpu_id', 'mpidr', 'elr_phys')}
                core['registers'] = [int(x) for x in ctx.regs]
            except Exception as error:
                core['context_error'] = f'{type(error).__name__}: {error}'
        old.write_private(self.directory, 'capture-core.json', core)
        if close_error is not None:
            self.closed = True
            raise old.BoundedStop('Console evidence finalization failed: '+close_error)
        diagnostics = {}
        try:
            diagnostics = self.diagnostics()
        except (Exception, old.BoundedStop) as error:
            diagnostics = getattr(self, 'diagnostic_partial', {})
            diagnostics['error'] = f'{type(error).__name__}: {error}'
        self.event, self.raw, self.context_error = original_event, original_raw, original_error
        self.closed = True
        record = dict(status='CONTINUING_XNU_STOPPED_FOR_OBSERVATION', message=message,
                      event=self.event, context_error=self.context_error,
                      interrupt_write_completed=self.injected, hardware_boot_observed=False,
                      diagnostics=diagnostics,
                      diagnostics_complete=diagnostics.get('complete') is True,
                      command_line_verdict='separate console-result.json and independent launch evidence')
        try:
            if self.raw is not None:
                record['raw_context_sha256'] = image.sha(self.raw)
                try:
                    ctx = self.context_format.parse(self.raw)
                    record['observed'] = {key: int(getattr(ctx, key)) for key in
                                          ('elr', 'spsr', 'esr', 'far', 'cpu_id', 'mpidr', 'elr_phys')}
                    record['registers'] = [int(x) for x in ctx.regs]
                except Exception as error:
                    record['context_error'] = self.context_error or f'{type(error).__name__}: {error}'
            old.write_private(self.directory, 'exception-context.json', record)
        finally:
            raise old.BoundedStop(message)

    def load_context(self):
        if not self.event or not self.event['info']:
            self.stop('Callback lacks context pointer')
        try:
            ctx = self.original_load_context()
        except Exception as error:
            raw = self.hv._info_data
            self.raw = bytes(raw) if raw is not None else None
            self.context_error = f'{type(error).__name__}: {error}'
            self.stop('Original exception context could not be loaded/parsed')
        self.raw = bytes(self.hv._info_data)
        if self.requested or (self.event['reason'], self.event['code']) not in self.ALLOWED:
            self.stop('Captured requested interrupt or unsupported callback')
        return ctx

    def dispatch(self, original, boot, *args):
        self.dispatch_depth += 1
        try:
            if boot:
                self.event = self.raw = self.context_error = None
                self.hv.ctx = self.hv._info_data = None
                reason, code, info = struct.unpack('<IIQ', args[0][:16])
                self.event = dict(reason=reason, code=code, info=info)
                if (reason, code) not in self.ALLOWED:
                    if info and ((reason in (1, 2) and code in range(4)) or
                                 (reason == 3 and code in (2, 3, 5, 6, 7))):
                        self.hv.exc_info, self.hv._in_handler = info, True
                        self.load_context()
                    self.stop('Unsupported callback; no continuation')
            return original(*args)
        finally:
            self.dispatch_depth -= 1
            if boot and not self.closed:
                self.event = self.raw = None

    def cmd(self, command, payload=b''):
        self.cmd_depth += 1
        try:
            if self.closed:
                raise old.BoundedStop('Command after stop prohibited')
            opcode = struct.unpack_from('<Q', payload)[0] if len(payload) >= 8 else None
            if command == self.iface.REQ_PROXY and opcode == self.proxy.P_EXIT:
                ordinary = self.event and (self.event['reason'], self.event['code']) in self.ALLOWED
                if (not ordinary or self.requested or self.injected or self.hv._sigint_pending or
                        len(payload) != 56 or struct.unpack_from('<Q', payload, 8)[0] != 2):
                    self.stop('Continuation outside pinned ordinary HANDLED callback prohibited')
            start = command == self.iface.REQ_PROXY and opcode == self.proxy.P_HV_START
            if self.phase == 'frame-written':
                self.stop('Command preceded isolated HV_START reply')
            if start and (self.phase != 'preparing' or not self.hv.started or len(payload) != 56 or
                          struct.unpack('<7Q', payload) != self.start_args):
                self.stop('Unexpected HV_START arguments')
            result = self.original_cmd(command, payload)
            if start:
                self.phase = 'frame-written'
            return result
        finally:
            self.cmd_depth -= 1


def read_carveouts(backend, directory=None):
    # Pinned src/mcc.c PLANE_TZ_MAX_REGS=4, plus a zero sentinel. Never follow
    # an unbounded target list or accept a wrapped/out-of-DRAM range.
    pointer = backend.p.mcc_get_carveouts()
    low = backend.ba.phys_base & ~0xffffffff
    usable_high = backend.ba.phys_base + backend.ba.mem_size
    # MCC-protected regions can be deliberately excluded from mem_size.
    # Validate their addresses against physical DRAM, while every destination
    # remains bounded by usable_high in check_reservation/load_payloads.
    dram_high = low + backend.ba.mem_size_actual
    old.inside(pointer, 80, low, usable_high, 'MCC carveout table')
    raw = backend.iface.readmem(pointer, 80)
    image.require(len(raw) == 80, 'Short MCC carveout table')
    if directory is not None:
        old.write_private(directory, 'mcc-carveouts.bin', raw)
    ranges = []
    for offset in range(0, 80, 16):
        start, size = struct.unpack_from('<QQ', raw, offset)
        if start == 0:
            image.require(size == 0, 'Malformed MCC sentinel')
            return ranges
        old.inside(start, size, low, dram_high, 'MCC carveout')
        ranges.append((start, size))
    raise ValueError('MCC table lacks bounded sentinel')


def check_reservation(backend, inputs, layout, carveouts):
    adt_capacity = old.align(backend.ba.devtree_size)
    tc_capacity = old.align(backend.snapshot['memory_map']['TrustCache']['size'])
    end = (layout['guest_base'] + layout['bootargs_off'] + old.PAGE + adt_capacity +
           tc_capacity + old.align(len(inputs.contents.get('ramdisk', b''))))
    start = layout['phys_base']
    old.inside(start, end-start, backend.ba.phys_base,
               backend.ba.phys_base + backend.ba.mem_size, 'Complete guest reservation')
    for address, size in carveouts:
        image.require(max(start, address) >= min(end, address+size),
                      'Complete guest reservation intersects MCC carveout')
    return dict(start=start, end=end, bytes=end-start, carveouts=carveouts)


def prepare_collection(raw, report):
    # kc-builder copies FILESET commands into the embedded kernel header. The
    # pinned MachO.__init__ recursively interprets their offsets relative to
    # each member and fails before loading. Only that optional nested-symbol
    # traversal is omitted here. The validated outer commands and original
    # pinned prepare_image/load_raw still determine every loaded byte.
    from m1n1.macho import MachO, MachOFile
    outer = MachO.__new__(MachO)
    outer.io = io.BytesIO(raw)
    outer.off, outer.end, outer.size = 0, len(raw), len(raw)
    outer.obj = MachOFile.parse_stream(outer.io)
    outer.symbols, outer.subfiles = {}, {}
    outer.load_info()
    image.require((outer.vmin, outer.vmax, outer.entry) ==
                  (report['vmin'], report['vmax'], report['entry']), 'Pinned outer parser disagrees with inspector')
    prepared = outer.prepare_image()
    image.require(len(prepared) == report['vmax']-report['vmin'], 'Prepared image span differs')
    for segment in report['segments']:
        dest = segment['vmaddr']-report['vmin']
        off, size = segment['fileoff'], segment['filesize']
        image.require(prepared[dest:dest+size] == raw[off:off+size], 'Prepared file bytes differ: ' + segment['name'])
    return outer, prepared


def load_payloads(hv, backend, ramdisk=None, directory=None, native_wire=None, inputs=None, reservation=None, carveouts=None, private_policy=None):
    # Without CTRR/PPL lockdown, arm_vm_prot_finalize frees below segLOWEST.
    # Stock HV.load_raw places ADT and TrustCache there. Retain both in the
    # post-kernel region reserved through topOfKernelData (arm_vm_init:1664).
    # Firmware stays target-side: TrustCache is copied, never read by the host.
    mm = hv.adt['chosen']['memory-map']
    tc_source, tc_bytes = mm.TrustCache
    capacity = old.align(backend.ba.devtree_size)
    adt_address = old.align(hv.guest_base + hv.bootargs_off + old.PAGE)
    tc_address = adt_address + capacity
    ramdisk_address = tc_address + old.align(tc_bytes)
    end = ramdisk_address + (old.align(len(ramdisk)) if ramdisk is not None else 0)
    old.inside(adt_address, end-adt_address, hv.tba.phys_base,
               backend.ba.phys_base + backend.ba.mem_size, 'Post-kernel payloads')
    old.inside(tc_source, tc_bytes, hv.tba.phys_base, adt_address, 'Loaded TrustCache source')
    image.require(0 < capacity <= 16 << 20, 'Invalid ADT allocation capacity')
    mm.DeviceTree = (adt_address, capacity)
    mm.TrustCache = (tc_address, tc_bytes)
    if ramdisk is not None:
        mm.RAMDisk = (ramdisk_address, len(ramdisk))
        # New properties otherwise default to a tuple of uint32s in pinned
        # build_prop(). Darwin requires this descriptor to be two uint64s.
        range_type, _ = mm._types['TrustCache']
        image.require(range_type.build(mm.RAMDisk) == struct.pack('<QQ', *mm.RAMDisk),
                      'RAMDisk descriptor codec is not two little-endian u64s')
        mm._types['RAMDisk'] = (range_type, False)
    hv.adt_base = adt_address
    blob = hv.adt.build()
    image.require(0 < len(blob) <= capacity, 'Relocated ADT exceeds reserved capacity')
    if directory is not None:
        old.write_private(directory, 'guest-adt.bin', blob)
    hv.tba.devtree = adt_address - hv.tba.phys_base + hv.tba.virt_base
    hv.tba.devtree_size = len(blob)
    hv.tba.top_of_kernel_data = end
    policy_metadata = None
    if private_policy is not None:
        blob, policy_metadata = private_policy_helper().prepare(
            hv, backend, inputs, directory, reservation, private_policy, blob,
            adt_address, capacity, _native_values, old.write_private)
    backend.p.memcpy8(tc_address, tc_source, tc_bytes)
    if ramdisk is not None:
        backend.u.compressed_writemem(ramdisk_address, ramdisk, True)
        backend.p.dc_cvau(ramdisk_address, len(ramdisk))
    # hv.start uploads the final rebuilt ADT at hv.adt_base. Upload once now as
    # well, so the saved pre-start metadata describes initialized guest bytes.
    backend.iface.writemem(adt_address, blob)
    if native_wire is not None:
        native_metadata = _write_native_bootargs(hv, backend, inputs, directory, reservation, carveouts, adt_address)
    else:
        fmt = getattr(backend.types, 'BootArgs_r' + str(int(hv.tba.revision)))
        backend.iface.writemem(hv.guest_base + hv.bootargs_off, fmt.build(hv.tba))
    result = dict(adt=dict(address=adt_address, bytes=len(blob), capacity=capacity, sha256=image.sha(blob)),
                trust_cache=dict(source=tc_source, address=tc_address, bytes=tc_bytes,
                                 copied_on_target=True, read_by_host=False),
                ramdisk=(dict(address=ramdisk_address, bytes=len(ramdisk), sha256=image.sha(ramdisk))
                         if ramdisk is not None else None), end=end)
    if native_wire is not None:
        result['native_bootargs'] = native_metadata
    if private_policy is not None:
        image.require(native_metadata['payload_sha256'] == image.sha(backend.private_policy_state['native']), 'Produced native BootArgs differs from final writer')
        result['native_private_policy'] = policy_metadata
    return result


def run_worker(args, inputs):
    old.authorize(args)
    native_wire = native_bootargs_enabled(inputs)  # Refuse malformed/drifted opt-in before USB enumeration.
    private_mode = private_policy_enabled(inputs)
    policy_nonce = secrets.token_hex(24) if private_mode is not None else None
    directory = args.private_directory.resolve(strict=True)
    image.require(directory.stat().st_uid == os.getuid() and directory.stat().st_mode & 0o077 == 0,
                  'Private directory ownership/mode mismatch')
    result = subprocess.run(['/usr/sbin/ioreg', '-a', '-r', '-l', '-c', 'IOUSBHostDevice'],
                            check=True, capture_output=True, timeout=5)
    ports = cdc_pair(result.stdout, args.device)
    image.require(ports['console'] == args.console_device, 'Selected console does not match live USB topology')
    old.write_private(directory, 'usb-pair.json', ports)
    keyboard, desktop = keyboard_enabled(inputs), desktop_enabled(inputs)
    virtual_input = virtual_input_enabled(inputs)
    terminal_input = terminal_input_enabled(inputs)
    native_keyboard_desktop = native_keyboard_desktop_enabled(inputs)
    native_trackpad_child = native_trackpad_child_enabled(inputs)
    bootargs = guest_bootargs(inputs)
    backend = ContinuingBackend(inputs, directory)
    policy_capture = None
    if private_mode is not None:
        old.write_private(directory, 'private-policy-source-context.json', dict(policy_nonce=policy_nonce, sources=private_mode, design_only_not_runtime=True))
    console = None
    try:
        backend.open(args.device)
        snapshot, ba, adt, layout = backend.collect(inputs)
        if native_wire is not None:
            image.require((_native_integer(backend.ba.revision, 16, 'source.revision'),
                           _native_integer(backend.ba.version, 16, 'source.version')) == (2, 2),
                          'Observed native BootArgs source is not R2/V2')
        old.preflight.observe(snapshot)
        old.write_private(directory, 'original-bootargs.bin', ba)
        old.write_private(directory, 'original-adt.bin', adt)
        carveouts = read_carveouts(backend, directory)
        reservation = check_reservation(backend, inputs, layout, carveouts)
        if private_mode is not None:
            policy_capture = private_policy_helper().capture(backend, adt, ba, snapshot, carveouts, policy_nonce, directory, old.write_private)
        old.write_private(directory, 'original-observations.json',
                          dict(snapshot=snapshot, layout=layout, packet=inputs.report,
                               inventory_sha256=inputs.inventory_sha256, reservation=reservation))
        hv = backend.new_hv()
        hv.init()
        cpus = list(hv.adt['cpus'])
        image.require(len(cpus) == 1 and cpus[0].state == 'running', 'Expected one running guest CPU')
        hv.set_bootargs(bootargs)
        display_console = configure_display_console(hv, backend, carveouts)
        validate_input_geometry(display_console['geometry'], virtual_input=virtual_input,
                                terminal_input=terminal_input, native_keyboard_desktop=native_keyboard_desktop,
                                 native_trackpad_child=native_trackpad_child)
        backend.display_console = display_console
        entropy = configure_entropy(hv)
        kernel_text_policy = configure_kernel_text_policy(hv, inputs)
        old.preflight.observe({**snapshot, 'guest': backend.guest_registers()})
        def bounded_unmap():
            image.require(read_carveouts(backend) == carveouts, 'MCC carveouts changed before guest write')
            for address, size in carveouts:
                hv.del_tracer(range(address, address+size), 'RAM-LOW')
                hv.del_tracer(range(address, address+size), 'RAM-HIGH')
        hv.unmap_carveouts = bounded_unmap
        if private_mode is not None:
            old.write_private(directory, 'private-policy-capacity-preflight.json', private_policy_helper().preflight_capacity(hv, backend))
        outer, prepared = prepare_collection(inputs.contents['kernel'], inputs.report['kernel'])
        hv.xnu_mode, hv.macho = True, outer
        hv._load_macho_symbols()
        hv.load_raw(prepared, entryoffset=outer.entry-outer.vmin, use_xnu_symbols=False, vmin=outer.vmin)
        for key in ('phys_base', 'guest_base', 'bootargs_off', 'entry'):
            image.require(getattr(hv, key) == layout[key], 'Loader prediction mismatch: ' + key)
        # arm_init owns MH_FILESET fixups. No patchfinder, header slide or host
        # relocation is applied. Kernel-mach_header remains stock loader policy.
        payloads = load_payloads(hv, backend, inputs.contents.get('ramdisk'), directory=directory,
                                 native_wire=native_wire, inputs=inputs, reservation=reservation, carveouts=carveouts, private_policy=policy_capture)
        old.write_private(directory, 'before-start.json',
                          dict(layout=layout, bootargs=bootargs, payloads=payloads,
                               boot_entropy=entropy,
                               display_console=display_console,
                               kernel_text_policy=kernel_text_policy,
                               hardware_boot_observed=False, image_sha256=image.sha(inputs.contents['kernel']),
                               prepared_image_sha256=image.sha(prepared), prepared_image_bytes=len(prepared)))
        console = ConsoleSession(args.console_device, directory, desktop=desktop, keyboard=keyboard,
                                 virtual_input=virtual_input, terminal_input=terminal_input,
                                 native_keyboard_desktop=native_keyboard_desktop,
                                 native_trackpad_child=native_trackpad_child, private_policy=private_mode is not None, policy_nonce=policy_nonce)
        if native_wire is not None:
            hv.start = _native_bootargs_start_wrapper(hv, payloads['native_bootargs'])
        if private_mode is not None:
            hv.start = private_policy_helper().start_wrapper(hv, backend, directory, backend.private_policy_state, hv.start,
                old.write_private, lambda: private_policy_helper().check_sources())
        ProgressCapture(hv, backend, inputs, layout, directory, args.runtime,
                        console=console, capture_framebuffer=not keyboard and private_mode is None).run()
    finally:
        try:
            if console is not None:
                console.close()
        finally:
            backend.close()


def worker_entry(args):
    os.umask(0o077)
    os.setsid()
    fd = os.open(os.devnull, os.O_RDONLY)
    os.dup2(fd, 0)
    os.close(fd)
    fd = os.open(args.private_directory / 'worker.log', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    os.dup2(fd, 1)
    os.dup2(fd, 2)
    os.close(fd)
    sys.stdout = os.fdopen(1, 'w', buffering=1, closefd=False)
    sys.stderr = os.fdopen(2, 'w', buffering=1, closefd=False)
    try:
        run_worker(args, verify_inputs(args))
    except (Exception, old.BoundedStop):
        traceback.print_exc()
    finally:
        sys.stdout.flush()
        sys.stderr.flush()
    raise SystemExit(1)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--bootstrap-manifest', type=Path)
    parser.add_argument('--host-source', type=Path)
    parser.add_argument('--inventory', type=Path, default=LAB / 'reports/sprint-007/ssh-target-inventory.json')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--device')
    parser.add_argument('--console-device')
    parser.add_argument('--confirm-recovery')
    parser.add_argument('--private-directory', type=Path)
    parser.add_argument('--timeout', type=int, default=240)
    parser.add_argument('--runtime', type=int, default=90)
    args = parser.parse_args(argv)
    try:
        image.require(5 <= args.runtime <= 180 and args.runtime + 30 <= args.timeout <= 300,
                      'Invalid runtime/worker bounds')
        specified = (args.manifest, args.bootstrap_manifest, args.host_source)
        image.require(all(specified) or not any(specified), 'Supply both manifests and pinned host source')
        if all(specified):
            inputs = verify_inputs(args)
        if not args.execute:
            print(json.dumps(dict(status='OFFLINE_PLAN_ONLY', inputs_verified=all(bool(v) for v in specified),
                                  device_contacted=False, command_line_observed=False,
                                  console='USB CDC data interface3, distinct from proxy interface1')))
            return 0
        old.authorize(args)
        image.require(all(specified), 'Execution requires verified manifests/source')
        image.require(args.console_device and args.console_device != args.device and
                      re.fullmatch(r'/dev/cu\.usbmodem[A-Za-z0-9_.-]+', args.console_device),
                      'Explicit distinct USB VUART endpoint required')
        args.private_directory = args.private_directory.resolve()
        args.private_directory.mkdir(mode=0o700, parents=False, exist_ok=False)
        proc = multiprocessing.get_context('spawn').Process(target=worker_entry, args=(args,))
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
                proc.kill()
            except ProcessLookupError:
                pass
            proc.join(5)
        result = dict(status=status, worker_returncode=proc.exitcode, hardware_boot_observed=False,
                      target_state='unknown; independent controller recovery required')
        old.write_private(args.private_directory, 'host-result.json', result)
        print(json.dumps(result))
        return 1
    except Exception as error:
        print(json.dumps(dict(status='REJECTED', error=str(error), command_line_observed=False)))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
