"""Original trusted/internal host collector. No transport or hardware adapter.

All callback offsets are relative to a separately trusted, bound reg1 aperture.
Source identity equality and numeric bounds do not establish mapping/read safety.
"""
from dataclasses import dataclass
import struct

PROFILE = 1
NODE_PATH = '/arm-io/mcc'
COMPATIBLE = 'mcc,t8103'
REG_INDEX = 1
MIN_EXTENT = 0xc06dc
PA_LIMIT = 1 << 36
READS = 96
BYTES = 384


@dataclass(frozen=True)
class Expected:
    adt_sha256: bytes
    nonce: bytes


@dataclass(frozen=True)
class Region:
    profile: int
    node_path: str
    compatible: str
    reg_index: int
    physical_base: int
    extent: int
    adt_sha256: bytes
    nonce: bytes


@dataclass(frozen=True)
class Result:
    status: str
    attempted: int
    completed: int
    # Exact prefix of values returned with u32 type/range validity. On binding
    # failure it is evidence only, even if all96 words happened to return.
    prefix: bytes
    raw: bytes | None
    passes_equal: bool | None
    planes_equal: bool | None
    region: Region | None
    expected: Expected | None


def _uint(value, limit):
    return type(value) is int and 0 <= value < limit


def _blob(value, size):
    return type(value) is bytes and len(value) == size and any(value)


def _snap(region, expected):
    if type(region) is not Region or type(expected) is not Expected:
        raise ValueError('DESCRIPTOR_TYPE')
    if not _uint(region.profile, 1 << 32) or region.profile != PROFILE:
        raise ValueError('PROFILE')
    if type(region.node_path) is not str or region.node_path != NODE_PATH:
        raise ValueError('NODE_PATH')
    if type(region.compatible) is not str or region.compatible != COMPATIBLE:
        raise ValueError('COMPATIBLE')
    if not _uint(region.reg_index, 1 << 32) or region.reg_index != REG_INDEX:
        raise ValueError('REG_INDEX')
    if not _uint(region.physical_base, PA_LIMIT) or not region.physical_base or region.physical_base & 3:
        raise ValueError('REGION_BASE')
    if (not _uint(region.extent, 1 << 64) or region.extent < MIN_EXTENT or
            region.extent > PA_LIMIT - region.physical_base or region.extent & 3):
        raise ValueError('REGION_EXTENT')
    for obj in (region, expected):
        if not _blob(obj.adt_sha256, 32) or not _blob(obj.nonce, 24):
            raise ValueError('SOURCE_IDENTITY')
    if region.adt_sha256 != expected.adt_sha256 or region.nonce != expected.nonce:
        raise ValueError('SOURCE_BINDING')
    # Copy typed values before callbacks; never rely on frozen dataclass alone
    # to resist a trusted callback deliberately replacing attributes.
    return (Region(region.profile, region.node_path, region.compatible,
                   region.reg_index, region.physical_base, region.extent,
                   bytes(region.adt_sha256), bytes(region.nonce)),
            Expected(bytes(expected.adt_sha256), bytes(expected.nonce)))


def relative_plan(profile, extent):
    if not _uint(profile, 1 << 32) or profile != PROFILE:
        raise ValueError('PROFILE')
    if not _uint(extent, 1 << 64) or extent < MIN_EXTENT or extent & 3:
        raise ValueError('REGION_EXTENT')
    result = tuple(plane * 0x40000 + 0x6a0 + slot * 0x10 + field * 4
                   for _pass in range(2) for plane in range(4)
                   for slot in range(4) for field in range(3))
    if len(result) != READS or any(offset < 0 or offset > extent - 4 for offset in result):
        raise ValueError('PLAN_BOUNDS')
    return result


class Collector:
    """Single-use, serialized, synchronous trusted callback boundary.

    A callback may not substitute mapping/context behind the bound callable.
    Arbitrary callable internals and concurrent mutation are not sandboxed.
    Reentry is refused and poisons the current invocation; retries are refused.
    """
    __slots__ = ('_region', '_expected', '_reader', '_state', '_violation', 'last')

    def __init__(self, region, expected, read32):
        self._region, self._expected, self._reader = region, expected, read32
        self._state, self._violation, self.last = 'NEW', False, None

    def capture(self):
        if self._state != 'NEW':
            if self._state == 'RUNNING':
                self._violation = True
            return Result('STATE_REFUSED', 0, 0, b'', None, None, None, None, None)
        self._state = 'RUNNING'
        attempted, words, bound_region, bound_expected = 0, [], None, None
        region, expected, reader = self._region, self._expected, self._reader

        def finish(status, raw=None):
            prefix = struct.pack('<' + 'I' * len(words), *words)
            passes = planes = None
            if raw is not None:
                passes = raw[:192] == raw[192:]
                planes = all(raw[p*192:p*192+48] == raw[p*192+q*48:p*192+(q+1)*48]
                             for p in range(2) for q in range(1, 4))
            self.last = Result(status, attempted, len(words), prefix, raw,
                               passes, planes, bound_region, bound_expected)
            self._state = 'DONE'
            return self.last

        def unchanged():
            if self._violation:
                return 'REENTRY'
            if self._region is not region or self._expected is not expected or self._reader is not reader:
                return 'BINDING_CHANGED'
            try:
                now_region, now_expected = _snap(region, expected)
            except (ValueError, AttributeError):
                return 'BINDING_CHANGED'
            if now_region != bound_region or now_expected != bound_expected:
                return 'BINDING_CHANGED'
            return None

        try:
            try:
                bound_region, bound_expected = _snap(region, expected)
                offsets = relative_plan(bound_region.profile, bound_region.extent)
            except (ValueError, AttributeError) as exc:
                return finish(str(exc) if type(exc) is ValueError else 'DESCRIPTOR_FIELDS')
            if not callable(reader):
                return finish('READER_TYPE')
            for offset in offsets:
                reason = unchanged()
                if reason:
                    return finish(reason)
                attempted += 1  # Journal the attempted read BEFORE calling it.
                try:
                    value = reader(offset)
                except Exception:
                    reason = unchanged()  # Check after throwing callbacks too.
                    return finish(reason or 'READ_EXCEPTION')
                reason = unchanged()
                if not _uint(value, 1 << 32):
                    return finish(reason or 'READ_VALUE')
                words.append(value)
                if reason:
                    return finish(reason)
            raw = struct.pack('<96I', *words)
            return finish('RAW_COMPLETE_UNVALIDATED', raw)
        finally:
            if self._state == 'RUNNING':
                # KeyboardInterrupt/SystemExit propagate, but retain bounded
                # attempted/prefix evidence and permanently consume this object.
                finish('CALLBACK_ABORTED')
