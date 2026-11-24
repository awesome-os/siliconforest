"""Synthetic ORIGINAL trees and an independent model of the pinned HV edits.

Test data only: no proxy, device or target. The PREPARED tree is derived here
by replaying the pinned setup_adt/load_raw edits on a tree model, written from
the m1n1 source and not shared with producer.py.
"""
import copy
import struct

DRAM_BYTES = 0x400000000
OS_LOG = "__OS_LOG"
# setup_adt USB0 (index 0) deletions, written out literally.
HV_USB0_DELETE = (
    "/arm-io/dart-usb0", "/arm-io/atc-phy0", "/arm-io/usb-drd0", "/arm-io/acio0",
    "/arm-io/acio-cpu0", "/arm-io/dart-acio0", "/arm-io/apciec0", "/arm-io/dart-apciec0",
    "/arm-io/apciec0-piodma", "/arm-io/i2c0/hpmBusManager/hpm0", "/arm-io/i2c0/hpmBusManager1/hpm0",
    "/arm-io/i2c3/hpmBusManager0/hpm0", "/arm-io/nub-spmi-a0/hpm0", "/arm-io/atc0-dpxbar",
    "/arm-io/atc0-dpphy", "/arm-io/atc0-dpin0", "/arm-io/atc0-dpin1", "/arm-io/atc-phy0",
)


class Node:
    def __init__(self, name, props=(), children=()):
        self.name, self.props, self.children = name, [list(p) for p in props], list(children)

    def get(self, key):
        return next((v for k, v in self.props if k == key), None)

    def set(self, key, value):
        for prop in self.props:
            if prop[0] == key:
                prop[1] = value
                return
        self.props.append([key, value])

    def child(self, name):
        return next(c for c in self.children if c.name == name)


def find(tree, path):
    node = tree
    for part in path.split("/")[1:]:
        node = node.child(part)
    return node


def delete(tree, path):
    parent, name = path.rsplit("/", 1)
    try:
        holder = find(tree, parent)
    except StopIteration:
        return
    holder.children = [c for c in holder.children if c.name != name]


def serialize(tree, junk_padding=(), junk_key=()):
    def prop(path, key, value):
        field = bytearray(key.encode().ljust(32, b"\0"))
        if (path, key) in junk_key:
            field[31] = 0x41
        pad = (-len(value)) & 3
        fill = b"\xaa" if (path, key) in junk_padding and pad else b"\0"
        return bytes(field) + struct.pack("<I", len(value)) + value + fill * pad

    def node(item, path):
        props = [["name", item.name.encode() + b"\0"]] + item.props
        own = "" if path is None else path
        body = b"".join(prop(own, k, v) for k, v in props)
        kids = b"".join(node(c, own + "/" + c.name) for c in item.children)
        return struct.pack("<II", len(props), len(item.children)) + body + kids

    return node(tree, None)


def row(phys, iova, remap, size, opaque):
    return struct.pack("<QQQII", phys, iova, remap, size, opaque)


def segments(rows):
    return (("segment-names", ";".join(label for label, _ in rows).encode() + b"\0"),
            ("segment-ranges", b"".join(raw for _, raw in rows)))


def remove_oslog(node):
    names = node.get("segment-names")
    if names is None:
        return
    labels = names[:-1].decode().split(";")
    if OS_LOG not in labels:
        return
    index = labels.index(OS_LOG)
    labels = labels[:index] + labels[index + 1:]
    ranges = node.get("segment-ranges")
    node.set("segment-names", ";".join(labels).encode() + b"\0")
    node.set("segment-ranges", ranges[:index * 32] + ranges[32 + index * 32:])


def hv_setup_adt(original, sprr=False):
    """HV.init -> setup_adt: product strings, serial device, USB0 deletions."""
    tree = copy.deepcopy(original)
    product = find(tree, "/product")
    for key in ("product-name", "product-description"):
        product.set(key, product.get(key)[:-1] + b" on m1n1 hypervisor\0")
    cursed = b" on cursed m1n1 hypervisor\0" if sprr else b" on m1n1 hypervisor\0"
    product.set("product-soc-name", b"Virtual " + product.get("product-soc-name")[:-1] + cursed)
    find(tree, "/defaults").set("serial-device", struct.pack("<I", 0x55))
    for path in HV_USB0_DELETE:
        delete(tree, path)
    return tree


def hv_load_raw(tree, allocation, bootargs_page):
    """load_raw: memory-map adjustments and first __OS_LOG removal."""
    tree = copy.deepcopy(tree)
    memory_map = find(tree, "/chosen/memory-map")
    memory_map.set("DeviceTree", struct.pack("<QQ", allocation.base, allocation.bytes))
    memory_map.set("BootArgs", struct.pack("<QQ", bootargs_page, 1 << 14))
    for node in find(tree, "/arm-io").children:
        remove_oslog(node)
        for nub in node.children:
            remove_oslog(nub)
    return tree


class Case:
    """One synthetic same-boot source set plus the independent expectations."""

    def __init__(self, p, *, pmap_count=1, extra_inside=0, removed_inside=False, second_oslog=True,
                 adjacent=False, overlap=False, straddle=False, allocation_pages=1, sprr=False,
                 solo_oslog=False, hpm=False,
                 original_edit=None, prepared_edit=None, junk_padding=(), junk_key=(), display=2):
        self.p = p
        dram = p.facts.DRAM_BASE
        original_phys, original_bytes = dram + 0x10000000, 0x30000000
        self.phys, size = original_phys + 0x10000000, 0x20000000
        vram = dram + 0x300000000
        video = (vram + 0x4000, 1, 1024, 256, 64, 0x1001e)
        self.allocation = p.handoff.Allocation(self.phys + 0x20000, p.PAGE * allocation_pages)
        inside = self.phys + 0x1000000
        self.rows = rows = dict(
            A=row(0x690000000, (1 << 64) - 1, 0, 0x4000, 7),
            L1=row(inside + 0x800000 if removed_inside else dram + 0x1000000, 0, 0x100000, 0x4000, 1),
            L2=row(dram + 0x1100000, 0, 0, 0x4000, 2),
            B=row(0x691000000, 0, 0x200000, 0x4000, 3),
            L3=row(inside, (1 << 64) - 1, 0x300000, 0x4000, 4),
            L4=row(0x692000000, 0, 0, 0x4000, 5),
            C=row(0x693000000, 0, 0x400000, 0x4000, 6),
            L5=row(dram + 0x1200000, 0, 0, 0x4000, 8),
            D=row(0x694000000, 0, 0x500000, 0x4000, 9),
            W=row(0x695000000, 0, 0x600000, 0x4000, 10),
            E=row(0x696000000, 0, 0, 0x4000, 11),
            L6=row(0x697000000, 0, 0x700000, 0x4000, 12),
            H=row(0x699000000, 0, 0x800000, 0x4000, 13),
            L7=row(dram + 0x1300000, 0, 0, 0x4000, 14),
            Q=row(0x69a000000, 0, 0x900000, 0x4000, 15),
            S=row(dram + 0x1400000, 0, 0, 0x4000, 16))
        device = [("same", rows["A"]), (OS_LOG, rows["L1"]), ("same", rows["A"])]
        if second_oslog:
            device.append((OS_LOG, rows["L2"]))
        twin = [("same", rows["A"])]
        twin += [("e%d" % k, row(inside + 0x2000000 + k * 0x100000, 0, 0, 0x4000, 20 + k))
                 for k in range(extra_inside)]
        if adjacent:
            twin.append(("adj", row(inside + 0x4000, 0, 0, 0x4000, 40)))
        if overlap:
            twin.append(("ovl", row(inside + 0x2000, 0, 0, 0x4000, 41)))
        if straddle:
            twin.append(("str", row(self.phys - 0x4000, 0, 0, 0x8000, 42)))
        arm_io = Node("arm-io", (), [
            Node("device", segments(device)),
            Node("bus", (), [Node("nub", segments([("x", rows["B"]), (OS_LOG, rows["L3"])]),
                                  [Node("deep", segments([(OS_LOG, rows["L4"])]))])]),
            Node("dart-usb0", segments([("r", rows["C"]), (OS_LOG, rows["L5"])])),
            Node("usb-drd0", (), [Node("nub", segments([("y", rows["D"])]))]),
            Node("usb-drd0-aux", segments([("w", rows["W"])])),
            Node("dart-usb1", segments([("z", rows["E"])])),
            Node("twin", segments(twin)),
        ])
        if solo_oslog:
            arm_io.children.append(Node("solo", segments([(OS_LOG, rows["S"])])))
        if hpm:
            arm_io.children.append(Node("i2c0", (), [Node("hpmBusManager", segments(
                [("h", rows["H"]), (OS_LOG, rows["L7"])]), [Node("hpm0", segments([("q", rows["Q"])]))])]))
        memory_map = Node("memory-map", (
            ("DeviceTree", struct.pack("<QQ", original_phys + 0x8000000, 0x60000)),
            ("BootArgs", struct.pack("<QQ", original_phys + 0x7ffc000, p.PAGE)),
            ("TrustCache", struct.pack("<QQ", self.phys + 0x30000, p.PAGE)),
            ("RAMDisk", struct.pack("<QQ", self.phys + 0x40000, p.PAGE))))
        pmap = struct.pack("<QQII", 0x680000000, 0x4000, 0x40000000, 0x12345678) * pmap_count
        tree = Node("device-tree", (("#address-cells", struct.pack("<I", 2)), ("#size-cells", struct.pack("<I", 2))), [
            Node("product", (("product-name", b"MacBook Pro (13-inch, M1, 2020)\0"),
                             ("product-description", b"MacBook Pro (13-inch, M1, 2020)\0"),
                             ("product-soc-name", b"Apple M1\0"))),
            Node("chosen", (("dram-base", struct.pack("<Q", dram)), ("dram-size", struct.pack("<Q", DRAM_BYTES))),
                 [memory_map]),
            Node("defaults", (("pmap-io-ranges", pmap), ("serial-device", struct.pack("<I", 0x44)))),
            arm_io,
            Node("other", (), [Node("child", segments([(OS_LOG, rows["L6"])]))]),
            Node("vram", (("device_type", b"vram\0"), ("reg", struct.pack("<QQ", vram, 0x3000000)))),
        ])
        if original_edit:
            original_edit(tree)
        self.setup_only = hv_setup_adt(tree, sprr)
        prepared = hv_load_raw(self.setup_only, self.allocation, self.phys + 0x10000)
        if prepared_edit:
            prepared_edit(prepared)
        self.original_tree, self.prepared_tree = tree, prepared
        self.original_adt = serialize(tree)
        self.base = serialize(prepared, junk_padding, junk_key)
        ba = bytearray(736)
        struct.pack_into("<HH", ba, 0, 2, 2)
        struct.pack_into("<QQQQ", ba, 8, 0xfffffe0000000000, original_phys, original_bytes, original_phys + 0x400000)
        struct.pack_into("<6Q", ba, 40, *video)
        struct.pack_into("<Q", ba, 728, DRAM_BYTES)
        self.original_bootargs = bytes(ba)
        words = [0x200000, 0x200003, 1, 0, 0, 0, 0x210000, 0x210007, 1, 0, 0, 0]
        self.mcc = struct.pack("<96I", *(words * 8))
        self.context = p.Context(bytes(range(1, 25)), b"K" * 32, b"R" * 32, p.HV_SHA256)
        self.video, self.display, self.size = video, display, size

    def kw(self, **override):
        values = dict(profile=self.p.PROFILE, original_profile=self.p.ORIGINAL_PROFILE,
                      original_adt=self.original_adt, source_iodev=4)
        values.update(override)
        return values

    def measure(self, **override):
        return self.p.measure(self.base, self.original_bootargs, self.mcc, self.context, **self.kw(**override))

    def native(self, measured=None, **fields):
        p = self.p
        measured = measured or self.measure()
        video = dict(zip(("base", "display", "stride", "width", "height", "depth"), self.video))
        video["display"] = self.display
        values = dict(revision=2, version=2, virt_base=0xfffffe0010000000, phys_base=self.phys,
                      mem_size=self.size, top_of_kernel_data=self.phys + 0x400000, video=video,
                      machine_type=0, devtree=self.allocation.base - self.phys + 0xfffffe0010000000,
                      devtree_size=measured.final_adt_bytes, cmdline="cpus=1 serial=3",
                      boot_flags=0, mem_size_actual=DRAM_BYTES)
        values.update(fields)
        return p.bootargs.encode(values, wire_profile=p.bootargs.WIRE_PROFILE)

    def produce(self, native=None, **override):
        native = self.native() if native is None else native
        return self.p.produce(self.base, self.original_bootargs, native, self.mcc, self.context,
                              self.allocation, **self.kw(**override))
