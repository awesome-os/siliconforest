# SPDX-License-Identifier: MIT
"""The launcher's `stage` mode (native_mode.txt: `native stage ...`): boot through b1/'s stage.

Placed next to host_progress.py with native_boot.py, pdsf_pack.py and m1n1-sf-stage.bin
(b1/build.sh). launcher-native.patch calls start() once the proxy has been collected, instead
of everything the launcher otherwise does to memory, the device tree and the boot arguments.

One upload and one vector: the stage m1n1 with its payload records (configuration, the
collection, the RAM root gzipped) is written into the launcher's own part of m1n1's RAM,
m1n1 is vectored into it, and from then on this launcher only listens (and types the console
challenge) on the M1's real UART. The stage does the rest by itself: see b1/DESIGN.md.
RAM only: nothing is written to the M1's storage or NVRAM, by this file or by the stage.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import struct
import time

import native_boot
import pdsf_pack

HERE = Path(__file__).resolve().parent
IMAGE = 'm1n1-sf-stage.bin'
BOOT_BIN, STAGE1_TEST = 'sf-boot.bin', 'sf-stage1-test.bin'      # ../install/build.sh
STAGE1_KERNEL = 'sf-kernel.bin'         # word stage1go: a copy of the sf/kernel.bin that is on the boot partition
# What a stage 1 appends behind the file it chainloads (m1n1 src/chainload.c: its variable
# lines, here the one the installer gives every stage 1), so that the stage is entered with
# the bytes behind its records that it will find there when it is installed.
STAGE1_PAYLOAD_ROOM = 64 << 20                                   # m1n1.ld: _max_payload_size
CHAINLOAD_TAIL = b'chosen.asahi,efi-system-partition=00000000-0000-0000-0000-000000000000\n'
ALIGN, ENTRY, PAGE = 0x10000, 0x800, 0x4000
BOOTARGS_TOP = 32                       # struct boot_args: top_of_kernel_data (m1n1 src/startup.c)
CHUNK = 4 << 20
require, mark = native_boot.require, native_boot.mark


def config(native, bootargs, usb_index, seed=None, xnu_base=None):
    return pdsf_pack.config_text(
        bootargs, display=1, usb_nodes='keep' if native.usb_keep else 'remove%d' % usb_index,
        sprr=int(native.sprr), amx=int(native.amx), wdt=native.wdt,
        seed_hex=seed.hex() if seed is not None else None,
        xnu_base='%#x' % xnu_base if xnu_base is not None else None)


def reconnect(backend, device, seconds=60):
    """A new m1n1 was vectored into and brings its own USB proxy up: open it again on the same
    callout device once it answers."""
    try:
        backend.iface.dev.close()
    except Exception:
        pass
    deadline, last = time.monotonic() + seconds, None
    time.sleep(3)                       # the old enumeration goes away first
    while time.monotonic() < deadline:
        try:
            if os.path.exists(device):
                backend.open(device)
                backend.p.nop()
                mark('proxy_reconnected')
                return
        except Exception as error:
            last = f'{type(error).__name__}: {error}'
            try:
                backend.iface.dev.close()
            except Exception:
                pass
        time.sleep(.5)
    raise RuntimeError('The new m1n1 did not answer on %s: %s' % (device, last))


def rechain(backend, inputs, raw_bootargs, device, directory, write_private):
    """Diagnostic (word rechain): start the pinned m1n1 once more, the way the stage image is
    started (uploaded into the running m1n1's memory, entered in place with a copy of the
    boot arguments whose top_of_kernel_data lies behind it), and go on from ITS proxy with
    the ordinary launcher-native boot. Separates "a third m1n1 started this way" from the
    stage's own logic."""
    p, iface, u = backend.p, backend.iface, backend.u
    image = inputs.contents['m1n1']
    size = (len(image) + PAGE - 1) & ~(PAGE - 1)
    total = size + PAGE
    base = u.heap.memalign(ALIGN, total)
    require(base and base % ALIGN == 0 and backend.ba.phys_base <= base and
            base + total <= backend.ba.phys_base + backend.ba.mem_size, 'No place for the m1n1 image')
    old_top, = struct.unpack_from('<Q', raw_bootargs, BOOTARGS_TOP)
    require(old_top == backend.ba.top_of_kernel_data and old_top <= base and len(raw_bootargs) <= PAGE,
            'Boot arguments differ from the collected ones')
    entered = bytearray(raw_bootargs.ljust(PAGE, b'\0'))
    struct.pack_into('<Q', entered, BOOTARGS_TOP, base + total)
    entered = bytes(entered)
    iface.writemem(base, image)
    if size > len(image):
        iface.writemem(base + len(image), bytes(size - len(image)))
    iface.writemem(base + size, entered)
    require(iface.readmem(base, len(image)) == image and iface.readmem(base + size, PAGE) == entered,
            'm1n1 image readback differs')
    p.dc_cvau(base, total)
    p.ic_ivau(base, len(image))
    print('NATIVE_RECHAIN ' + json.dumps(dict(base=hex(base), entry=hex(base + ENTRY), bootargs=hex(base + size),
                                             image_bytes=len(image))), flush=True)
    # From here the machine may be without a proxy: the record that lets the controller send
    # its one reset (vdm-return) is written before the vector, as for every native boot.
    write_private(directory, 'native-start.json', dict(
        hypervisor=False, rechain=True, base=base, entry=base + ENTRY, bootargs=base + size,
        note='pinned m1n1 started once more in place; the kernel is entered later, see native-start-boot.json'))
    mark('rechain_vector')
    p.request(p.P_VECTOR, base + ENTRY, base + size, 0, 0, 0, 0)
    reconnect(backend, device)


def physical(address, virt_base, phys_base):
    """A boot-arguments virtual address as a physical one, in 64-bit arithmetic as m1n1 and
    XNU do it. iBoot's virt_base differs from boot to boot and can lie above the address
    (sfb05c: virt_base 0xfffffffffe078000, devtree 0x3ec4000), so the difference wraps."""
    return (int(address) - int(virt_base) + int(phys_base)) & 0xffffffffffffffff


def readback(backend, inputs, raw_bootargs, console, directory, write_private, bootargs_text, layout, native=None):
    """Diagnostic (word stagestop): the stage placed everything and returned to its proxy
    instead of entering XNU. Read back what it placed and compare it with what the launcher
    would have placed itself. Prints STAGE_READBACK lines; saves the trees."""
    import io
    from m1n1.macho import MachO, MachOFile
    iface, p = backend.iface, backend.p
    text = bytes(console.proof.output).replace(b'\r', b'').decode('latin1')
    match = re.search(r'PDSF: XNU memory (0x[0-9a-f]+)\.\.(0x[0-9a-f]+); kernel (0x[0-9a-f]+), bootargs (0x[0-9a-f]+), '
                      r'adt (0x[0-9a-f]+),\s*PDSF: trustcache (0x[0-9a-f]+), ramdisk (0x[0-9a-f]+), top (0x[0-9a-f]+); '
                      r'EL2 page (0x[0-9a-f]+)', text)
    require(match is not None, 'The stage did not print its layout')
    phys, ram_end, kernel, bootargs, adt, tc, ramdisk, top, el2 = (int(v, 16) for v in match.groups())
    out = dict(phys_base=hex(phys), kernel=hex(kernel), bootargs=hex(bootargs), adt=hex(adt), trustcache=hex(tc),
               ramdisk=hex(ramdisk), top=hex(top), el2=hex(el2))

    def differ(name, address, expected, chunk=8 << 20):
        first, wrong = None, 0
        for offset in range(0, len(expected), chunk):
            got = iface.readmem(address + offset, min(chunk, len(expected) - offset))
            want = expected[offset:offset + len(got)]
            if got != want:
                for i in range(0, len(got), 4096):
                    if got[i:i + 4096] != want[i:i + 4096]:
                        wrong += 1
                        if first is None:
                            j = next(k for k in range(i, min(i + 4096, len(got))) if got[k] != want[k])
                            first = dict(offset=hex(offset + j), got=got[j:j + 16].hex(), want=want[j:j + 16].hex())
        out[name] = dict(bytes=len(expected), equal=first is None, pages_differing=wrong, first=first)
        print('STAGE_READBACK ' + json.dumps({name: out[name]}), flush=True)

    raw = inputs.contents['kernel']
    outer = MachO.__new__(MachO)
    outer.io = io.BytesIO(raw)
    outer.off, outer.end, outer.size = 0, len(raw), len(raw)
    outer.obj = MachOFile.parse_stream(outer.io)
    outer.symbols, outer.subfiles = {}, {}
    outer.load_info()
    differ('kernel_image', kernel, outer.prepare_image())
    # boot arguments: the page the stage wrote, field by field
    fmt = getattr(backend.types, 'BootArgs_r' + str(struct.unpack_from('<H', raw_bootargs)[0]))
    page = iface.readmem(bootargs, fmt.sizeof())
    write_private(directory, 'stage-bootargs.bin', page)
    ba, old = fmt.parse(page), fmt.parse(raw_bootargs[:fmt.sizeof()])
    fields = {}
    for name in ('revision', 'version', 'virt_base', 'phys_base', 'mem_size', 'top_of_kernel_data', 'machine_type',
                 'devtree', 'devtree_size', 'boot_flags', 'mem_size_actual'):
        fields[name] = [hex(int(getattr(old, name))), hex(int(getattr(ba, name)))]
    for name in ('base', 'display', 'stride', 'width', 'height', 'depth'):
        fields['video.' + name] = [hex(int(getattr(old.video, name))), hex(int(getattr(ba.video, name)))]
    fields['cmdline'] = str(ba.cmdline)
    fields['cmdline_at_108'] = page[108:108 + 96].split(b'\0')[0].decode('latin1')
    fields['cmdline_starts_at_xnu_offset'] = page[108:109] != b'\0'
    fields['raw_tail_equal'] = page[fmt.sizeof() - 16:] == raw_bootargs[fmt.sizeof() - 16:fmt.sizeof()]
    print('STAGE_READBACK ' + json.dumps(dict(bootargs_was_now=fields)), flush=True)
    # The whole page against the page the launcher builds for the same inputs
    # (host_progress.load_payloads and m1n1 hv load_raw: the same fields, set on the collected
    # boot arguments and built by the pinned Construct definition).
    align = lambda n: (n + PAGE - 1) & ~(PAGE - 1)
    want = fmt.parse(raw_bootargs[:fmt.sizeof()])
    want.cmdline = bootargs_text
    want.phys_base = phys
    want.virt_base = 0xfffffe0010000000 + (phys & ((32 << 20) - 1))
    want.mem_size = int(old.phys_base) + int(old.mem_size) - phys
    want.devtree = adt - phys + want.virt_base
    want.devtree_size = int(ba.devtree_size)            # the tree's length (ours has Apple's names)
    want.top_of_kernel_data = top
    want.video.display = 1
    expected = fmt.build(want)
    first = next((i for i in range(len(expected)) if expected[i] != page[i]), None)
    predicted = dict(
        phys_base=layout['phys_base'] == phys, kernel=layout['guest_base'] == kernel,
        bootargs=layout['guest_base'] + layout['bootargs_off'] == bootargs,
        adt=align(bootargs + PAGE) == adt)
    print('STAGE_READBACK ' + json.dumps(dict(bootargs_page=dict(
        bytes=len(page), equal_to_the_launchers=first is None, first_difference=first,
        got=page[first:first + 16].hex() if first is not None else None,
        want=expected[first:first + 16].hex() if first is not None else None,
        addresses_as_the_launcher_predicts=predicted))), flush=True)
    # device trees: the one the stage built, and the original it built it from, as they are now
    built = iface.readmem(adt, int(ba.devtree_size))
    write_private(directory, 'stage-adt.bin', built)
    original_at = physical(old.devtree, old.virt_base, old.phys_base)
    original = iface.readmem(original_at, int(old.devtree_size))
    write_private(directory, 'stage-original-adt.bin', original)
    print('STAGE_READBACK ' + json.dumps(dict(
        adt=dict(bytes=len(built), sha256=hashlib.sha256(built).hexdigest(),
                 devtree_field_points_at=hex(physical(ba.devtree, ba.virt_base, ba.phys_base))),
        original_adt=dict(at=hex(original_at), bytes=len(original), sha256=hashlib.sha256(original).hexdigest(),
                          equal_to_collected=original == backend.u.adt_data if hasattr(backend, 'u') and
                          getattr(backend.u, 'adt_data', None) is not None else None))), flush=True)
    tree = backend.adt_module.load_adt(built)
    before = backend.adt_module.load_adt(original)
    mm, mm0 = tree['chosen']['memory-map'], before['chosen']['memory-map']
    for name in ('TrustCache', 'SEPFW'):
        (a1, n1), (a0, n0) = getattr(mm, name), getattr(mm0, name)
        differ(name.lower() + '_copy', a1, iface.readmem(a0, n0) if n0 <= 16 << 20 else b'')
        out[name] = [hex(a0), hex(n0), hex(a1), hex(n1)]
    out['memory_map'] = {name: [hex(v) for v in getattr(mm, name)] for name in
                         ('SEPFW', 'TrustCache', 'DeviceTree', 'BootArgs', 'RAMDisk') if hasattr(mm, name)}
    if inputs.contents.get('ramdisk') is not None and not getattr(native, 'no_ramdisk', False):
        differ('ramdisk', ramdisk, inputs.contents['ramdisk'])
    elif getattr(native, 'no_ramdisk', False):
        # no RAM root record: the tree must not name one
        out['ramdisk_in_memory_map'] = hasattr(mm, 'RAMDisk')
        print('STAGE_READBACK ' + json.dumps(dict(no_ramdisk=dict(memory_map_names_a_ramdisk=out['ramdisk_in_memory_map']))), flush=True)
    # the EL2 page as placed (parameters filled in)
    page = iface.readmem(el2, PAGE)
    out['el2_page_params'] = page[0x3f00:0x3f20].hex()
    out['el2_page_code_sha256'] = hashlib.sha256(page[:0x3f00]).hexdigest()
    write_private(directory, 'stage-readback.json', out)
    print('STAGE_READBACK ' + json.dumps(out), flush=True)


def start(native, backend, inputs, bootargs, raw_bootargs, layout, directory, runtime, console_factory,
          write_private):
    """Upload, vector, capture the real serial console for `runtime` seconds (or until the
    guest script's end marker). `bootargs` is the kernel command line, `raw_bootargs` the bytes
    of the boot arguments this m1n1 was started with (as collected), `layout` the launcher's
    own prediction for a proxy-loaded boot: without the word lowmem the stage is given its
    phys_base, and must then arrive at the same entry point and boot-arguments address."""
    p, iface, u = backend.p, backend.iface, backend.u
    device = getattr(getattr(iface, 'dev', None), 'port', None)
    if 'stagestop' in native.words:
        bootargs = bootargs + ' pdsfstop=1'
    m1n1 = (HERE / IMAGE).read_bytes() if not (getattr(native, 'boot_bin', False) or getattr(native, 'stage1', False)) else b''
    index = int(p.iodev_whoami()) - int(backend.proxy_module.IODEV.USB0)
    require(index in (0, 1), 'The proxy is not on a USB port')
    mark('stage_pack_begin')
    seed = os.urandom(256) if native.host_seed else None
    xnu_base = None if native.low_mem else layout['phys_base']
    prebuilt = None
    if getattr(native, 'stage1', False):
        # The installed stage 1 as the installer writes it (image, variable lines, four zero
        # bytes). No records, no kernel: it looks for its boot partition and stops.
        image = m1n1 = (HERE / STAGE1_TEST).read_bytes()
        require(image[0x808:0x810] == b'AURALOAD' and b'\nchainload=' in image[-600:] and image.endswith(bytes(4)),
                'Not a stage 1 with its chainload line')
        text, found, xnu_base = b'', [], None
        bootargs = '(stage 1 test: no kernel is entered)'
        prebuilt = dict(file=STAGE1_TEST, sha256=hashlib.sha256(image).hexdigest(), bytes=len(image))
        if getattr(native, 'stage1_go', False):
            # Stage 1 is expected to start the system from the boot partition by itself (sf/boot.bin, which
            # reads sf/kernel.bin). Nothing of that is uploaded: the copy of the kernel file next to the
            # launcher only says what was put there (its command line, and that its kernel is this boot's).
            import zlib
            data = (HERE / STAGE1_KERNEL).read_bytes()
            found, end = pdsf_pack.parse(data, 0)
            require([item[0] for item in found] == [pdsf_pack.CONFIG, pdsf_pack.KERNEL] and all(item[4] for item in found) and
                    end == len(data), 'sf-kernel.bin is not configuration + kernel')
            text = data[found[0][5]:found[0][5] + found[0][2]]
            keys = dict(line.split('=', 1) for line in text.decode('ascii').splitlines())
            bootargs = keys['bootargs']
            require('rd=uuid' in bootargs.split() and 'sfmark=1' in bootargs.split() and 'pdsfstop=1' not in bootargs.split()
                    and 'seed-hex' not in keys and 'wdt' not in keys, 'sf-kernel.bin: not a disk-root bench session')
            require(keys.get('usb-nodes') in ('keep', 'remove%d' % index), 'sf-kernel.bin removes the nodes of another USB port than the proxy uses')
            stored = data[found[1][5]:found[1][5] + found[1][2]]
            require(found[1][1] & pdsf_pack.F_GZIP and zlib.decompress(stored, 31) == inputs.contents['kernel'],
                    "sf-kernel.bin's kernel is not the collection of this boot")
            prebuilt.update(kernel_file_on_the_boot_partition=dict(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data)))
    elif getattr(native, 'boot_bin', False):
        # The file an installed stage 1 will read: nothing is packed here, and its own
        # configuration record decides everything, the kernel command line included.
        import zlib
        data = (HERE / BOOT_BIN).read_bytes()
        at = pdsf_pack.records_offset(data)
        found, end = pdsf_pack.parse(data, at)
        m1n1 = data[:at]
        require([item[0] for item in found] == [pdsf_pack.CONFIG, pdsf_pack.KERNEL] and all(item[4] for item in found) and
                data[end:] == bytes(4), 'sf-boot.bin is not stage + configuration + kernel')
        text = data[found[0][5]:found[0][5] + found[0][2]]
        keys = dict(line.split('=', 1) for line in text.decode('ascii').splitlines())
        bootargs = keys['bootargs']
        require('rd=uuid' in bootargs.split() and 'rd=md0' not in bootargs.split() and 'pdsfstop=1' not in bootargs.split()
                and 'seed-hex' not in keys and 'wdt' not in keys, 'sf-boot.bin: not a disk-root configuration')
        require(keys.get('usb-nodes') in ('keep', 'remove%d' % index), 'sf-boot.bin removes the nodes of another USB port than the proxy uses')
        xnu_base = int(keys['xnu-base'], 16) if 'xnu-base' in keys else None
        stored = data[found[1][5]:found[1][5] + found[1][2]]
        require(found[1][1] & pdsf_pack.F_GZIP and zlib.decompress(stored, 31) == inputs.contents['kernel'],
                "sf-boot.bin's kernel is not the collection of this boot")
        image = data + CHAINLOAD_TAIL
        prebuilt = dict(file=BOOT_BIN, sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                        appended_as_stage1_would=CHAINLOAD_TAIL.decode().strip())
    else:
        text = config(native, bootargs, index, seed, xnu_base)
        # noramdisk (a root on the disk): the RAM root the launcher holds is not put into the image
        ramdisk = None if getattr(native, 'no_ramdisk', False) else inputs.contents.get('ramdisk')
        require(ramdisk is not None or ('rd=uuid' in bootargs.split() and 'rd=md0' not in bootargs.split()),
                'An image without a RAM root needs rd=uuid on the command line')
        image = pdsf_pack.image(m1n1, text, inputs.contents['kernel'], ramdisk)
        at = pdsf_pack.records_offset(image)
        found, _ = pdsf_pack.parse(image, at)
        require(at == len(m1n1) and all(item[4] for item in found), 'Stage image self-check')
    mark('stage_pack_end', bytes=len(image))
    cpus = [token for token in bootargs.split() if token.startswith('cpus=')]
    require(getattr(native, 'stage1', False) or cpus == ['cpus=1'] or 'sfcpuheld=1' in bootargs.split(),
            'More than one processor through the stage needs sfcpuheld=1')

    # The image runs where it is uploaded (m1n1 is position independent). It is entered with
    # a copy of this m1n1's boot arguments that differs in one field: top_of_kernel_data is
    # put behind the image, which is where the stage's heap then starts.
    size = (len(image) + PAGE - 1) & ~(PAGE - 1)
    # A stage 1 chainloads: m1n1 copies the next image to its own base and refuses one that
    # reaches past top_of_kernel_data into its live heap (src/chainload.c; boot dr33). iBoot
    # starts the installed stage 1 as a raw object with top_of_kernel_data shortly behind the
    # image and its variable lines: there is NO room of 64 MiB (the installed machine refused
    # its 14 MB sf/boot.bin exactly as boot dr33 did). So a stage 1 is entered here as every
    # other image is: the boot arguments page directly behind it, the top behind that. The
    # word stage1room gives it the 64 MiB of m1n1.ld's _max_payload_size instead (boot dr34:
    # a layout the installed machine does not have).
    reserve = STAGE1_PAYLOAD_ROOM if getattr(native, 'stage1_room', False) else 0
    total = size + reserve + PAGE
    base = u.heap.memalign(ALIGN, total)
    require(base and base % ALIGN == 0 and backend.ba.phys_base <= base and
            base + total <= backend.ba.phys_base + backend.ba.mem_size, 'No place for the stage image')
    require(xnu_base is None or base + total + (128 << 20) <= xnu_base,
            "The stage image does not fit below the launcher's phys_base")
    old_top, = struct.unpack_from('<Q', raw_bootargs, BOOTARGS_TOP)
    require(old_top == backend.ba.top_of_kernel_data and old_top <= base,
            'Boot arguments differ from the collected ones')
    require(len(raw_bootargs) <= PAGE, 'Boot arguments larger than a page')
    entered = bytearray(raw_bootargs.ljust(PAGE, b'\0'))
    struct.pack_into('<Q', entered, BOOTARGS_TOP, base + total)
    entered = bytes(entered)

    mark('stage_upload_begin', bytes=len(image))
    for offset in range(0, len(image), CHUNK):
        iface.writemem(base + offset, image[offset:offset + CHUNK])
    if size > len(image):
        iface.writemem(base + len(image), bytes(size - len(image)))
    iface.writemem(base + size + reserve, entered)
    mark('stage_upload_end')
    # The stage checks every record's CRC itself; the code that does it is read back here.
    require(iface.readmem(base, len(m1n1)) == m1n1 and iface.readmem(base + size + reserve, PAGE) == entered,
            'Stage image readback differs')
    p.dc_cvau(base, total)
    p.ic_ivau(base, len(m1n1))

    record = dict(stage=True, hypervisor=False, base=base, entry=base + ENTRY, bootargs=base + size + reserve,
                  image_bytes=len(image), image_sha256=hashlib.sha256(image).hexdigest(),
                  m1n1_sha256=hashlib.sha256(m1n1).hexdigest(),
                  records=[dict(type=pdsf_pack.NAMES[kind], gzip=bool(flags & pdsf_pack.F_GZIP), stored=stored, raw=raw)
                           for kind, flags, stored, raw, _, _ in found],
                  config=[line for line in text.decode().splitlines() if not line.startswith('seed-hex=')],
                  seed='controller CSPRNG, in the configuration record' if seed is not None else 'SEP, by the stage',
                  usb_port=index, console=native_boot.UART_DEVICE, watchdog_seconds=native.wdt,
                  expected=(None if native.low_mem else dict(
                      xnu_base='%#x' % layout['phys_base'], entry='%#x' % layout['entry'],
                      bootargs='%#x' % (layout['guest_base'] + layout['bootargs_off']))),
                  watchdog_armed_by='the stage, just before it enters the kernel' if native.wdt else None)
    if prebuilt is not None:
        # the launcher's own prediction does not apply: the file's configuration decides
        record['prebuilt'], record['expected'] = prebuilt, None
    if native.vdm_serial:
        record['serial_mode_ok'] = native_boot.serial_mode()
    console = console_factory(native_boot.UART_DEVICE)
    console.dev = native_boot.PacedSerial(console.dev)
    try:
        # What the controller's verdict binds the console to (tools/m1_bringup_cycle.py
        # console_observation): the kernel this boot launches. Here it travels in the image's
        # kernel record, which the stage checks (CRC) before it loads it. Without this record
        # a stage boot that answers the challenge is still recorded NO_COMMAND_LINE (sfb06).
        write_private(directory, 'before-start.json', dict(
            stage=True, bootargs=bootargs, image_sha256=hashlib.sha256(inputs.contents['kernel']).hexdigest(),
            loaded_by='the stage, from the kernel record of the uploaded image', stage_image_sha256=record['image_sha256'],
            hardware_boot_observed=False))
        write_private(directory, 'native-start.json', record)
        print('NATIVE_STAGE_START ' + json.dumps(record, default=hex), flush=True)
        mark('native_vector')
        # m1n1 replies, shuts its USB, display console and MMU down and jumps (src/main.c).
        # This is the last request of the boot: nothing is sent over the proxy afterwards.
        p.request(p.P_VECTOR, base + ENTRY, base + size + reserve, 0, 0, 0, 0)
        mark('native_vector_acknowledged')
        if getattr(native, 'stage1', False) and not getattr(native, 'stage1_go', False):
            # The stage 1 test: it stops by itself (sf_stop prints SF_STOPPED) or the time runs out.
            limit = time.monotonic() + min(runtime, 90)
            while b'SF_STOPPED' not in bytes(console.proof.output) and time.monotonic() < limit:
                time.sleep(.2)
            time.sleep(2.0)
            mark('native_capture_end', done=b'SF_STOPPED' in bytes(console.proof.output), console_bytes=len(console.proof.output))
            return
        if 'stagestop' in native.words:
            # Diagnostic: the stage returns to its proxy instead of entering XNU.
            limit = time.monotonic() + 90
            while b'PDSF: stop requested' not in bytes(console.proof.output) and time.monotonic() < limit:
                time.sleep(.2)
            require(b'PDSF: stop requested' in bytes(console.proof.output), 'The stage did not reach its stop')
            reconnect(backend, device)
            readback(backend, inputs, raw_bootargs, console, directory, write_private, bootargs, layout, native)
            mark('native_capture_end', done=True, console_bytes=len(console.proof.output))
            return
        deadline = time.monotonic() + runtime
        # As native_boot.start(): a session started by launchd (multiuser) types no command,
        # so the console session does not look for the end marker itself.
        untyped, done = 'multiuser' in native.words, False
        # A guest that restarts the machine itself (reboot/launcher-self-reboot.diff puts
        # self_reboot_mode.txt next to this file): in a typed session the console session sees
        # its SF_BENCH_REBOOT line and ends the capture; in an untyped one this loop does, the
        # same ten seconds after the line (the stop of the storage controller and the restart
        # follow it on the console).
        self_reboot, reboot_at = (HERE / 'self_reboot_mode.txt').exists(), None
        while time.monotonic() < deadline and not done and console.thread.is_alive():
            time.sleep(.2)
            tail = bytes(console.proof.output[-4096:]).replace(b'\r', b'')
            done = console.done_seen or (untyped and re.search(rb'(?:^|\n)SF_BENCH_DONE\n', tail) is not None)
            if untyped and self_reboot and reboot_at is None and re.search(rb'(?:^|\n)SF_BENCH_REBOOT\n', tail) is not None:
                reboot_at = time.monotonic()
                mark('guest_reboot_announced')
            done = done or (reboot_at is not None and time.monotonic() - reboot_at >= 10)
        if done and reboot_at is None:
            time.sleep(1.0)
        mark('native_capture_end', done=done, console_bytes=len(console.proof.output))
    finally:
        error = None
        try:
            console.close()
        except Exception as failure:
            error = f'{type(failure).__name__}: {failure}'
        write_private(directory, 'capture-core.json', dict(
            status='NATIVE_XNU_CONSOLE_CAPTURED', native=True, hypervisor=False, stage=True, event=None,
            console='physical uart0 on ' + native_boot.UART_DEVICE,
            console_capture_finalized=error is None, console_close_error=error,
            diagnostics_complete=False, hardware_boot_observed=False))
