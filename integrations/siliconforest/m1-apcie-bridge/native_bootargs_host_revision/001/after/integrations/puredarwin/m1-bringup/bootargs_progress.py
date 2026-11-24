"""Bounded BootArgs address translation for the continuing launcher.

The frozen007/008 launchers remain unchanged. Pinned m1n1 proxyutils.py
get_adt/push_adt use unsigned 64-bit VA translation. iBoot can supply a
virt_base near UINT64_MAX with a low, wrapped devtree VA. Only VA arithmetic
wraps here: physical addresses, sizes, and range ends never wrap.

This validates metadata, not the peer's identity, MCC exclusions, or actual
readability. Those remain the caller's independent checks. No I/O or mutation.
"""

PAGE = 0x4000
U64_LIMIT = 1 << 64
U64_MASK = U64_LIMIT - 1
MAX_RAM = 64 << 30
MAX_ADT = 16 << 20
FIELDS = ('revision', 'version', 'phys_base', 'virt_base', 'mem_size',
          'mem_size_actual', 'top_of_kernel_data', 'devtree', 'devtree_size')


class BootArgsError(ValueError):
    """Malformed or unbounded BootArgs metadata; do not access the target."""


def _require(value, message):
    if not value:
        raise BootArgsError(message)


def _integer(value, label):
    # Construct Hex values are int subclasses. bool is not an address.
    _require(isinstance(value, int) and not isinstance(value, bool) and
             0 <= value < U64_LIMIT, label + ' is not a u64')
    return value


def _inside(address, size, low, high, label):
    _integer(address, label)
    _integer(size, label + ' size')
    _require(size > 0 and low <= address < address + size <= high <= U64_LIMIT,
             label + ' lies outside bounded RAM')


def _layout(ba):
    for key in FIELDS:
        try:
            value = ba[key]
        except (KeyError, TypeError) as exc:
            raise BootArgsError('Missing BootArgs.' + key) from exc
        _integer(value, 'BootArgs.' + key)
    _require(ba['revision'] in (1, 2, 3) and ba['version'] in (1, 2),
             'Unknown BootArgs ABI')
    low = ba['phys_base']
    high = low + ba['mem_size']
    dram = low & ~0xffffffff
    _require(low and low % PAGE == 0 and ba['virt_base'] % PAGE == 0 and
             0 < ba['mem_size'] <= ba['mem_size_actual'] <= MAX_RAM and
             high <= dram + ba['mem_size_actual'] <= U64_LIMIT,
             'Invalid BootArgs RAM layout')
    _require(low <= ba['top_of_kernel_data'] <= high,
             'Invalid top_of_kernel_data')
    return low, high, dram


def validate_bootargs(ba, address, struct_size):
    """Return a bounded physical ADT address; accept legitimate VA wrapping.

    All serialized fields must already be u64 integers. The VA difference is
    modulo 2**64, but must then fit the advertised usable RAM span (and the
    64 GiB policy cap). In particular, masking cannot rescue an arbitrary VA
    or an overflowing physical address. Metadata is never rewritten.
    """
    low, high, dram = _layout(ba)
    _inside(address, struct_size, dram, high, 'Original BootArgs')
    size = ba['devtree_size']
    _require(0 < size <= MAX_ADT, 'Unbounded ADT size')
    offset = (ba['devtree'] - ba['virt_base']) & U64_MASK
    _require(offset < MAX_RAM and offset + size <= ba['mem_size'],
             'Original ADT virtual offset lies outside bounded RAM')
    dt = low + offset
    _inside(dt, size, dram, high, 'Original ADT')
    return dt


def physical_to_virtual(ba, address, size):
    """Encode a bounded physical span as a BootArgs VA without changing ba.

    The destination must be inside usable RAM at/above phys_base. Validate
    its entire physical span before the sole wrapping addition to virt_base.
    The caller still owns allocation/overlap checks and raw BootArgs/ADT
    round-trip verification before any target write.
    """
    low, high, _ = _layout(ba)
    _inside(address, size, low, high, 'Translated physical span')
    offset = address - low
    _require(offset < MAX_RAM and offset + size <= ba['mem_size'],
             'Translated physical offset lies outside bounded RAM')
    return (ba['virt_base'] + offset) & U64_MASK
