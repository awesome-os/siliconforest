#!/usr/bin/env python3
"""Validate a JSON snapshot of claimed J293 proxy facts. Never contact hardware.

The snapshot is user or proxy metadata. A consistent report is an observation
of those claims. It is not an attestation, not execution permission, and not a
hardware boot result. stage1 and stage2 are inherited boot-property provenance.
Only the live BUILD_TAG and the packet and build source pins must be the pinned
revision. This module does not verify live image bytes and has no device,
network, subprocess, or launcher.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

try:
    from .lab import ValidationError, read_json
except ImportError:
    from lab import ValidationError, read_json

PIN = '121fcbf35d4501814b58ca5347926a4173b6a512'
CHIP_ID = 0x8103
UART_ADDR = 0x235200000
UART_MIN_SIZE = 0x1000
APPLE_IMPLEMENTER = 0x61
# midr.h: T8103 Icestorm is part 0x22, T8103 Firestorm is part 0x23.
# MIDR_PART is bits [15:4]. Apple's MIDR implementer is bits [31:24].
MIDR_PARTS = {0x22: 'T8103 Icestorm', 0x23: 'T8103 Firestorm'}
SCTLR_M = 1 << 0
U64 = 1 << 64
HYPERVISOR_SUFFIX = ' on m1n1 hypervisor'
STATUS = 'PROXY_METADATA_OBSERVATIONS_RECORDED'
CLAIM_CLASS = 'user_or_proxy_metadata'
LIMITS = (
    'Recorded fields are user or proxy metadata claims, not an attestation.',
    'This report does not permit execution.',
    'This report is not a hardware boot result.',
    'A consistent snapshot is not clearance to initialize, load, or start a guest.',
    'stage1 and stage2 are inherited boot-property provenance.',
    'Live image verification is a separate collector observation.',
    'hardware_tested false is the expected claim for a first qualification.',
)
FORBIDDEN_CLAIMS = (
    'execution_permitted',
    'hardware_boot_pass',
    'hardware_boot_passed',
    'boot_pass',
    'attested',
)


def _u64(value, label: str) -> int:
    if isinstance(value, str):
        text = value.strip()
        if len(text) > 2 and text[0] == '0' and text[1] in 'xX':
            digits = text[2:]
            if digits and all(c in '0123456789abcdefABCDEF' for c in digits):
                value = int(digits, 16)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValidationError(f'{label} is missing or not an integer')
    if value < 0 or value >= U64:
        raise ValidationError(f'{label} is outside the 64-bit range')
    return value


def _object(value, label: str, required: tuple[str, ...], optional: tuple[str, ...] = ()) -> dict:
    if not isinstance(value, dict):
        raise ValidationError(f'{label} is missing')
    allowed = set(required) | set(optional)
    for key in required:
        if key not in value:
            raise ValidationError(f'{label}.{key} is missing')
    for key in value:
        if key not in allowed:
            raise ValidationError(f'{label}.{key} is not a supported fact')
    return value


def _text(value, label: str, limit: int = 128) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValidationError(f'{label} is missing')
    if value != value.strip() or len(value) > limit:
        raise ValidationError(f'{label} is not a bounded fact')
    return value


def _pin(value, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValidationError(f'{label} is missing')
    if 'dirty' in value.lower():
        raise ValidationError(f'{label} is a dirty stage tag')
    if value != PIN:
        raise ValidationError(f'{label} does not match {PIN}')
    return value


def _range(value, label: str) -> tuple[int, int]:
    item = _object(value, label, ('addr', 'size'))
    addr = _u64(item['addr'], f'{label}.addr')
    size = _u64(item['size'], f'{label}.size')
    if addr == 0 or size == 0 or addr + size > U64:
        raise ValidationError(f'{label} is not a valid address range')
    return addr, size


def _overlaps(left: tuple[int, int], right: tuple[int, int]) -> bool:
    left_addr, left_size = left
    right_addr, right_size = right
    return left_addr < right_addr + right_size and right_addr < left_addr + left_size


def _product(value) -> dict:
    product = _object(value, 'product', (
        'target_type', 'model', 'compatible',
        'product_name', 'product_description', 'product_soc_name',
    ))
    target = _text(product['target_type'], 'product.target_type', 32)
    model = _text(product['model'], 'product.model', 64)
    if target != 'J293':
        raise ValidationError('product.target_type is not J293')
    if model != 'MacBookPro17,1':
        raise ValidationError('product.model is not MacBookPro17,1')
    compatible = product['compatible']
    if not isinstance(compatible, list) or not compatible or len(compatible) > 16:
        raise ValidationError('product.compatible is missing')
    names = []
    for item in compatible:
        if not isinstance(item, str):
            raise ValidationError('product.compatible entry is not a string')
        names.append(_text(item, 'product.compatible', 64))
    if len(set(names)) != len(names):
        raise ValidationError('product.compatible is not a bounded set')
    for required in ('apple,j293', 'apple,t8103'):
        if required not in names:
            raise ValidationError(f'product.compatible is missing {required}')
    strings = {}
    for key in ('product_name', 'product_description', 'product_soc_name'):
        text = _text(product[key], f'product.{key}', 160)
        if HYPERVISOR_SUFFIX in text:
            raise ValidationError(f'product.{key} already carries a hypervisor mutation')
        strings[key] = text
    return {
        'target_type': target,
        'model': model,
        'compatible': names,
        **strings,
    }


def _chip(value) -> int:
    chip = _object(value, 'chip', ('adt_chip_id', 'proxy_chip_id'))
    adt_id = _u64(chip['adt_chip_id'], 'chip.adt_chip_id')
    proxy_id = _u64(chip['proxy_chip_id'], 'chip.proxy_chip_id')
    if adt_id != proxy_id:
        raise ValidationError('ADT chip-id and proxy chip-id differ')
    if adt_id != CHIP_ID:
        raise ValidationError('chip-id is not 0x8103')
    return adt_id


def _cpus(value) -> dict:
    if not isinstance(value, list) or not value or len(value) > 16:
        raise ValidationError('cpus is missing')
    parsed = []
    seen_names = set()
    seen_ids = set()
    for index, item in enumerate(value):
        cpu = _object(item, f'cpus[{index}]', ('name', 'state', 'cpu_id', 'cluster_id', 'reg'))
        name = _text(cpu['name'], f'cpus[{index}].name', 32)
        state = _text(cpu['state'], f'cpus[{index}].state', 32)
        cpu_id = _u64(cpu['cpu_id'], f'cpus[{index}].cpu_id')
        cluster_id = _u64(cpu['cluster_id'], f'cpus[{index}].cluster_id')
        reg = _u64(cpu['reg'], f'cpus[{index}].reg')
        if cpu_id > 0xffffffff or cluster_id > 0xffffffff:
            raise ValidationError(f'cpus[{index}] identity is outside 32 bits')
        if name in seen_names or cpu_id in seen_ids:
            raise ValidationError(f'cpus[{index}] repeats a CPU identity')
        seen_names.add(name)
        seen_ids.add(cpu_id)
        parsed.append({
            'name': name, 'state': state, 'cpu_id': cpu_id,
            'cluster_id': cluster_id, 'reg': reg,
        })
    running = [cpu for cpu in parsed if cpu['state'] == 'running']
    if len(running) != 1:
        raise ValidationError('expected exactly one CPU with state running')
    return running[0]


def _uart(value) -> dict:
    uart = _object(value, 'uart0', ('translated_addr', 'size'), ('irq', 'aapl_phandle'))
    addr = _u64(uart['translated_addr'], 'uart0.translated_addr')
    size = _u64(uart['size'], 'uart0.size')
    if addr != UART_ADDR:
        raise ValidationError('uart0 translated address is not 0x235200000')
    if size < UART_MIN_SIZE or addr + size > U64:
        raise ValidationError('uart0 range is shorter than 0x1000 or exceeds 64-bit space')
    observed = {'translated_addr': '0x235200000', 'size': size}
    for key in ('irq', 'aapl_phandle'):
        if key in uart:
            number = _u64(uart[key], f'uart0.{key}')
            if number > 0xffffffff:
                raise ValidationError(f'uart0.{key} is outside 32 bits')
            observed[key] = number
    return observed


def _bootargs(value) -> dict:
    bootargs = _object(value, 'bootargs', ('revision',), ('version',))
    revision = _u64(bootargs['revision'], 'bootargs.revision')
    if revision not in (1, 2, 3):
        raise ValidationError('bootargs revision is not 1, 2, or 3')
    observed = {'revision': revision}
    if 'version' in bootargs:
        observed['version'] = _u64(bootargs['version'], 'bootargs.version')
    return observed


def _memory_map(value) -> dict:
    node = _object(value, 'memory_map', ('SEPFW', 'TrustCache'))
    sepfw = _range(node['SEPFW'], 'memory_map.SEPFW')
    trust = _range(node['TrustCache'], 'memory_map.TrustCache')
    if _overlaps(sepfw, trust):
        raise ValidationError('SEPFW and TrustCache ranges overlap')
    return {
        'SEPFW': {'addr': f'0x{sepfw[0]:x}', 'size': sepfw[1]},
        'TrustCache': {'addr': f'0x{trust[0]:x}', 'size': trust[1]},
    }


def _identity(value) -> dict:
    identity = _object(value, 'identity', (
        'build_tag', 'packet_source_pin', 'build_source_pin', 'stage1', 'stage2',
    ))
    live = {key: _pin(identity[key], f'identity.{key}') for key in (
        'build_tag', 'packet_source_pin', 'build_source_pin',
    )}
    if live['packet_source_pin'] != live['build_source_pin']:
        raise ValidationError('packet and build source pins differ')
    if live['build_tag'] != live['packet_source_pin']:
        raise ValidationError('BUILD_TAG and source pin differ')
    # stage1 and stage2 survive in boot properties after a RAM chainload.
    return {
        **live,
        'stage1': _text(identity['stage1'], 'identity.stage1'),
        'stage2': _text(identity['stage2'], 'identity.stage2'),
        'stage_provenance': 'inherited_boot_property',
        'live_image_verification_observed': False,
    }


def _guest(value) -> dict:
    guest = _object(value, 'guest', ('sctlr', 'vpidr'))
    sctlr = _u64(guest['sctlr'], 'guest.sctlr')
    vpidr = _u64(guest['vpidr'], 'guest.vpidr')
    if vpidr > 0xffffffff:
        raise ValidationError('guest.vpidr is wider than an MIDR')
    if sctlr & SCTLR_M:
        raise ValidationError('guest SCTLR.M is not 0')
    implementer = (vpidr >> 24) & 0xFF
    part = (vpidr >> 4) & 0xFFF
    if implementer != APPLE_IMPLEMENTER or part not in MIDR_PARTS:
        raise ValidationError('guest VPIDR is not Apple MIDR part 0x22 or 0x23')
    return {
        'sctlr_m': 0,
        'vpidr_implementer': '0x61',
        'vpidr_part': f'0x{part:x}',
        'vpidr_part_name': MIDR_PARTS[part],
    }


def observe(snapshot: dict) -> dict:
    """Return observations for one snapshot. Reject missing or mismatched claims."""
    if not isinstance(snapshot, dict):
        raise ValidationError('snapshot is missing')
    for key in FORBIDDEN_CLAIMS:
        if key in snapshot:
            raise ValidationError(f'snapshot.{key} is not an observation')
    root = _object(snapshot, 'snapshot', (
        'schema_version', 'product', 'chip', 'cpus', 'uart0',
        'bootargs', 'memory_map', 'identity', 'guest',
    ), ('hardware_tested',))
    if type(root['schema_version']) is not int or root['schema_version'] != 1:
        raise ValidationError('snapshot.schema_version is not 1')
    if 'hardware_tested' in root and root['hardware_tested'] is not False:
        raise ValidationError('hardware_tested is not an accepted claim')
    observations = {
        'product': _product(root['product']),
        'chip_id': f'0x{_chip(root["chip"]):x}',
        'boot_cpu': _cpus(root['cpus']),
        'running_cpu_count': 1,
        'uart0': _uart(root['uart0']),
        'bootargs': _bootargs(root['bootargs']),
        'memory_map': _memory_map(root['memory_map']),
        'identity': _identity(root['identity']),
        'guest': _guest(root['guest']),
    }
    if 'hardware_tested' in root:
        observations['hardware_tested_claim'] = False
    return {
        'status': STATUS,
        'claim_class': CLAIM_CLASS,
        'attested': False,
        'hardware_boot_observed': False,
        'observations': observations,
        'limits': list(LIMITS),
    }


def observe_path(path: Path) -> dict:
    return observe(read_json(path))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(observe_path(args.snapshot), indent=2))
        return 0
    except (ValidationError, OSError, UnicodeError, json.JSONDecodeError) as exc:
        print(json.dumps({
            'status': 'REJECTED',
            'error': str(exc),
            'claim_class': CLAIM_CLASS,
            'attested': False,
            'hardware_boot_observed': False,
        }))
        return 1


if __name__ == '__main__':
    sys.exit(main())
