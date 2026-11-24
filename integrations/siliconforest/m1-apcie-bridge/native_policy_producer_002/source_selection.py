"""Pure host-boundary normalization of the pinned HV source selection.

The pinned m1n1 proxy returns IODEV(...) from iodev_whoami(), and HV.init()
stores that IntEnum member as hv.iodev. select() accepts only the USB0 member
of the IODEV class the caller took from its verified pinned proxy module,
converts it with int.__int__ (never a class's own __int__), and returns the
exact built-in int 4 that producer002 requires.

The selection is trusted source evidence, not authentication. This module
makes no proxy call, reads no device and never infers the selection from an
edited ADT. Binding proxy_module to the pinned proxy.py bytes is the caller's job.
"""
from dataclasses import dataclass
import enum

PROXY_SHA256 = "a3051c5b45a3d66bcb877bc89067dd36887bc1f5346f5b874dfa866d40ad31c2"
HV_SHA256 = "18da80e496122233cbd3d69a287e5737ca78da083e8e36c8a33b01b35744c4a6"
PRODUCER_PROFILE = "t8103-original-prepared-subset-source-facts-v2"
# Exact IODEV members, in order, from the pinned proxy.py.
PINNED_IODEV = (("UART", 0), ("DOCKCHANNEL_UART", 1), ("FB", 2), ("USB_VUART", 3),
                ("USB0", 4), ("USB1", 5), ("USB2", 6), ("USB3", 7),
                ("USB4", 8), ("USB5", 9), ("USB6", 10), ("USB7", 11))
SELECTED_MEMBER = "USB0"
SOURCE_IODEV = 4
_MISSING = object()


class Refused(ValueError):
    """Missing, plain, foreign, boolean or unsupported source selection."""


def _require(condition, message):
    if not condition:
        raise Refused(message)


@dataclass(frozen=True)
class Selection:
    enum_module: str
    enum_class: str
    member: str
    source_iodev: int

    def witness(self):
        """Bounded private record of what was checked; contains no device data."""
        return dict(version=2, producer_profile=PRODUCER_PROFILE, selection_source="pinned_HV.iodev",
                    enum_module=self.enum_module, enum_class=self.enum_class, enum_member=self.member,
                    numeric_value=self.source_iodev, normalized_type="int",
                    normalization="int.__int__", expected_proxy_sha256=PROXY_SHA256,
                    expected_hv_sha256=HV_SHA256, source_bytes_verified_here=False,
                    inferred_from_adt=False, authentication=False)


def select(hv, proxy_module):
    """Return (Selection, 4) for the pinned IODEV.USB0 member; refuse anything else."""
    source_type = getattr(proxy_module, "IODEV", _MISSING)
    # Standard enum metaclass only, so iteration and lookup cannot be overridden;
    # the class must be defined in the very module object the caller passed.
    _require(type(source_type) is enum.EnumMeta and issubclass(source_type, enum.IntEnum) and
             source_type.__name__ == "IODEV", "pinned IODEV IntEnum class required")
    _require(source_type.__module__ == getattr(proxy_module, "__name__", None),
             "IODEV is not defined by the supplied proxy module")
    members = tuple((name, int.__int__(member)) for name, member in source_type.__members__.items())
    _require(members == PINNED_IODEV, "IODEV members differ from the pinned proxy source")
    witness = getattr(hv, "iodev", _MISSING)
    _require(witness is not _MISSING and witness is not None, "missing pinned HV source selection witness")
    # Plain ints, bools, foreign enums and int subclasses fail here, before any conversion.
    _require(type(witness) is source_type and witness is source_type.__members__[SELECTED_MEMBER] and
             witness._name_ == SELECTED_MEMBER, "HV selection must be the pinned IODEV.USB0 enum member")
    value = int.__int__(witness)
    _require(type(value) is int and value == SOURCE_IODEV, "IODEV.USB0 must normalize to built-in int 4")
    return Selection(source_type.__module__, source_type.__qualname__, witness._name_, value), value
