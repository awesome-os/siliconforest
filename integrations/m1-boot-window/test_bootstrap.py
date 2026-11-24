#!/usr/bin/env python3
"""Host-only tests. Fakes establish refusal/order policy, never hardware behavior."""
from pathlib import Path
import ast
import contextlib
import importlib.util
import io
import json
import os
import struct
import sys
import tempfile
from types import SimpleNamespace as NS
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("window_bootstrap_tested", HERE / "bootstrap.py")
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)


class AttrDict(dict):
    __getattr__ = dict.__getitem__
    __setattr__ = dict.__setitem__
    def copy(self):
        return AttrDict(self)


def fixture():
    base = 0x800000000
    ba = AttrDict(phys_base=base, mem_size=8 << 30, mem_size_actual=8 << 30,
                  top_of_kernel_data=base + 0x1000000, virt_base=0xfffffe0000000000,
                  devtree_size=0x2000)
    heap = base + 0x2000000
    mm = {"SEPFW": (base + 0x800000, 0x100001), "TrustCache": (base + 0xa00000, 0x4000),
          "Kernel-text": (base, 0x100000), "preoslog": (base + 0xb00000, 0x4000)}
    return dict(ba=ba, base=base, heap=heap, ba_addr=base + 0xc00000, ba_size=0x500,
                dtaddr=base + 0xd00000, dt_size=0x2000, memory_map=mm)


class MetadataNode:
    """Deliberately fake codec tree; tests comparison policy, not the pinned ADT codec."""
    def __init__(self, path, properties, children=()):
        self._path, self._properties, self._types = path, properties, {}
        self._children = list(children)
    def __iter__(self):
        return iter(self._children)
    def __getitem__(self, name):
        return next(n for n in self._children if n._path.rsplit("/", 1)[-1] == name)
    def walk_tree(self):
        yield self
        for child in self:
            yield from child.walk_tree()
    def build(self):
        rows = {}
        for node in self.walk_tree():
            rows[node._path] = {"properties": {name: [bool(node._types.get(name, (None, False))[1]),
                fake_build_prop(node._path, name, value).hex()] for name, value in node._properties.items()},
                "children": [child._path for child in node]}
        return json.dumps(rows, sort_keys=True).encode()


def fake_build_prop(path, name, value, t=None):
    if isinstance(value, bytes):
        return value
    if isinstance(value, str):
        return value.encode() + b"\0"
    return struct.pack("<QQ", *value)


def fake_load_adt(data):
    rows = json.loads(data)
    nodes = {path: MetadataNode(path, {name: bytes.fromhex(value[1])
             for name, value in row["properties"].items()}) for path, row in rows.items()}
    for path, row in rows.items():
        nodes[path]._children = [nodes[child] for child in row["children"]]
        nodes[path]._types = {name: (None, value[0]) for name, value in row["properties"].items()}
    return nodes["/device-tree"]


def metadata_fixture():
    args = fixture(); layout = b.plan_memory(**args); ba = args["ba"]
    ba.update(cmdline="-v", video={"display": 1, "stride": 1280, "_io": object()}, _io=object())
    mm = MetadataNode("/device-tree/chosen/memory-map", {**args["memory_map"],
                      "untouched": (0x808000000, 0x4000), "name": "memory-map",
                      "AAPL,phandle": struct.pack("<I", 0x1234), "kernel-only": "true"})
    chosen = MetadataNode("/device-tree/chosen", {"name": "chosen"}, [mm])
    nested = MetadataNode("/device-tree/arm-io/uart0", {"name": "uart0", "unrelated": b"original"})
    nested._types["unrelated"] = (None, True)
    arm = MetadataNode("/device-tree/arm-io", {"name": "arm-io"}, [nested])
    tree = MetadataNode("/device-tree", {"name": "device-tree", "identity": b"J293"}, [chosen, arm])
    fmt = NS(build=lambda value: json.dumps(b.bootargs_fields(value)).encode(), parse=json.loads)
    codec = NS(build_prop=fake_build_prop, load_adt=fake_load_adt)
    construct = NS(Array=lambda *args: object(), Int64ul=object())
    return ba, fmt, tree, layout, codec, construct


class FakeInterface:
    def __init__(self, events, corrupt=False):
        self.events, self.memory, self.corrupt = events, {}, corrupt
    def writemem(self, addr, data):
        self.events.append(("write", addr, len(data)))
        self.memory[addr] = data
    def readmem(self, addr, size):
        self.events.append(("read", addr, size))
        data = self.memory[addr]
        return bytes([data[0] ^ 1]) + data[1:] if self.corrupt else data


class FakeProxy:
    P_VECTOR = 11
    def __init__(self, heap, events, vector_error=False):
        self.heap, self.events, self.vector_error = heap, events, vector_error
    def heapblock_alloc(self, size):
        self.events.append(("allocate", size))
        old = self.heap
        self.heap += size
        return old
    def memcpy8(self, dst, src, size):
        self.events.append(("firmware_copy", dst, src, size))
    def dc_cvac(self, addr, size):
        self.events.append(("dc", addr, size))
    def ic_ivau(self, addr, size):
        self.events.append(("ic", addr, size))
    def request(self, *args):
        self.events.append(("request", *args))
        if self.vector_error:
            raise OSError("synthetic missing vector reply")


class BootstrapTests(unittest.TestCase):
    def test_offline_default_never_constructs_backend(self):
        with patch.object(b, "verify", side_effect=AssertionError("unexpected verification")), \
                patch.object(b, "connect", side_effect=AssertionError("unexpected open")), \
                contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(b.main([]), 0)
        self.assertFalse(json.loads(out.getvalue())["device_contacted"])

    def test_execute_refused_before_verification_or_backend(self):
        variants = [["--execute"], ["--execute", "--device", "/dev/cu.debug-console",
                    "--confirm-recovery", b.CONFIRMATION, "--private-directory", "/tmp/test-bootstrap"]]
        for args in variants:
            with self.subTest(args=args), patch.object(b, "verify", side_effect=AssertionError("verify reached")), \
                    contextlib.redirect_stdout(io.StringIO()) as out:
                self.assertEqual(b.main(args), 1)
            self.assertEqual(json.loads(out.getvalue())["status"], "REJECTED")

    def test_private_directory_and_environment_refusals(self):
        a = b.parser().parse_args(["--execute", "--device", "/dev/cu.usbmodemTEST",
            "--confirm-recovery", b.CONFIRMATION, "--private-directory", str(b.LAB / "bad")])
        with self.assertRaisesRegex(ValueError, "outside repository"):
            b.authorize(a)
        a.private_directory = Path("/tmp/test-bootstrap")
        with patch.dict(os.environ, {"M1N1DEVICE": "wrong"}), self.assertRaisesRegex(ValueError, "environment"):
            b.authorize(a)

    def test_real_offline_packet_and_sources_verified_without_m1n1_import(self):
        a = b.parser().parse_args(["--manifest", str(b.LAB / "artifacts/m1-firstboot-007-r6/manifest.json"),
            "--host-source", str(b.LAB / "sources/m1n1-baseline"), "--window-image",
            str(b.LAB / "artifacts/m1-unattended-008/window-candidate-004/m1n1-window.bin")])
        inputs, window, stub = b.verify(a)
        self.assertFalse(any(k == "m1n1" or k.startswith("m1n1.") for k in sys.modules))
        self.assertEqual(b.digest(window), b.WINDOW_SHA)
        self.assertEqual(b.digest(inputs.contents["m1n1"]), b.TARGET_SHA)
        self.assertEqual(b.digest(stub), b.STUB_SHA)
        for name, sha in b.SOURCE_SHA.items():
            self.assertEqual(b.digest(inputs.host_files[name]), sha)
        canonical = ast.parse(inputs.host_files["proxyclient/m1n1/bootmem.py"])
        assignment = next(node for node in canonical.body if isinstance(node, ast.Assign) and
                          any(isinstance(t, ast.Name) and t.id == "BOOT_BLOBS" for t in node.targets))
        self.assertEqual(ast.literal_eval(assignment.value), b.BLOBS)
        altered = dict(inputs.host_files)
        altered["proxyclient/tools/chainload.py"] += b"# changed\n"
        bad = NS(host_files=altered)
        with patch.object(b.frozen(), "verify_inputs", return_value=bad):
            with self.assertRaisesRegex(ValueError, "Canonical host source mismatch"):
                b.verify(a)

    def test_wrong_raw_or_trampoline_refused(self):
        raw = (b.LAB / "artifacts/m1-unattended-008/window-candidate-004/m1n1-window.bin").read_bytes()
        with self.assertRaisesRegex(ValueError, "hash/size"):
            b.raw_image(raw[:-1] + bytes([raw[-1] ^ 1]), b.WINDOW_SHA, b.WINDOW_TAG, len(raw))
        stub = (HERE / "bootstrap-trampoline.bin").read_bytes()
        with self.assertRaisesRegex(ValueError, "Unreviewed"):
            b.trampoline(stub[:-1], 0x800000800)
        with self.assertRaisesRegex(ValueError, "RVBAR"):
            b.trampoline(stub, 0x800001800)

    def test_all_trampoline_opcodes_and_only_literal_patched(self):
        stub = (HERE / "bootstrap-trampoline.bin").read_bytes()
        expected = [0xaa0203e7,0xaa0303e8,0xa8c11424,0xa9001444,0x91004042,0xf1004063,
                    0x54ffff81,0xaa0703e2,0xaa0803e3,0xd50b7a22,0x91010042,0xf1010063,
                    0x54ffffa1,0xd5033f9f,0xaa0703e2,0xaa0803e3,0xd50b7522,0x91010042,
                    0xf1010063,0x54ffffa1,0xd5033f9f,0xd5033fdf,0x58000041,0xd61f0020]
        self.assertEqual(list(struct.unpack("<24I", stub[:96])), expected)
        patched = b.trampoline(stub, 0x810000800)
        self.assertEqual(patched[:96], stub[:96])
        self.assertEqual(struct.unpack_from("<Q", patched, 96)[0], 0x810000800)
        canonical = ast.parse((b.LAB / "sources/m1n1-baseline/proxyclient/tools/chainload.py").read_text())
        call = next(n for n in ast.walk(canonical) if isinstance(n, ast.Call) and
                    isinstance(n.func, ast.Attribute) and n.func.attr == "ARMAsm")
        expected_asm = "".join(part.value if isinstance(part, ast.Constant) else "0x1122334455667788"
                               for part in call.args[0].values)
        actual_asm = (HERE / "bootstrap-trampoline.S").read_text().split("_start:\n",1)[1].split(".pool",1)[0]
        normalize = lambda text: [" ".join(line.split()) for line in text.splitlines() if line.strip()]
        self.assertEqual(normalize(actual_asm), normalize(expected_asm))

    def test_valid_plan_same_base_and_disjoint_parked_firmware(self):
        args = fixture(); layout = b.plan_memory(**args)
        self.assertEqual(layout["entry"] & ~0xfff, args["base"])
        self.assertEqual(layout["copy_size"], 0x404000)
        self.assertGreaterEqual(layout["stage"], args["heap"])
        self.assertEqual(layout["stub"], layout["stage"] + layout["copy_size"])
        self.assertGreater(layout["fw"], layout["stub"])
        self.assertEqual(layout["top"], args["heap"] + layout["reserve_size"])
        self.assertEqual([x["name"] for x in layout["blobs"]], ["SEPFW", "preoslog", "TrustCache"])

    def test_range_failures(self):
        cases = [
            ("unaligned base", lambda a: a.update(base=a["base"]+1)),
            ("unaligned heap", lambda a: a.update(heap=a["heap"]+1)),
            ("heap below top", lambda a: a.update(heap=a["ba"]["top_of_kernel_data"]-64)),
            ("heap beyond RAM", lambda a: a.update(heap=a["base"]+(9<<30))),
            ("destination too big", lambda a: a.update(raw_size=0x800000)),
            ("missing TrustCache", lambda a: a["memory_map"].pop("TrustCache")),
            ("overflow", lambda a: a["memory_map"].update(SEPFW=((1<<64)-8,16))),
            ("above heap", lambda a: a["memory_map"].update(SEPFW=(a["heap"],0x4000))),
            ("image overlaps FW", lambda a: a["memory_map"].update(SEPFW=(a["base"]+0x2000,0x4000))),
            ("unknown overlap", lambda a: a["memory_map"].update(Unknown=(a["base"],0x4000))),
            ("alias", lambda a: a["memory_map"].update(TrustCache=a["memory_map"]["SEPFW"])),
            ("ADT overlap", lambda a: a.update(dtaddr=a["base"]+0x1000)),
            ("metadata oversized", lambda a: a.update(dt_size=17<<20)),
            ("required sentinel", lambda a: a["memory_map"].update(SEPFW=((1<<64)-1,)*2)),
        ]
        for name, mutate in cases:
            a = fixture(); mutate(a)
            with self.subTest(name=name), self.assertRaises(ValueError):
                b.plan_memory(**a)

    def test_reservation_must_fit_entirely(self):
        a = fixture()
        a["ba"].mem_size = a["heap"] - a["base"] + b.PAGE
        with self.assertRaisesRegex(ValueError, "reservation"):
            b.plan_memory(**a)

    def test_live_wrong_tag_fails_before_any_allocation(self):
        events = []
        backend = NS(p=NS(get_base=lambda: 0x800000000),
                     iface=NS(readmem=lambda *args: events.append(args) or b"wrong"))
        raw = (b.LAB / "artifacts/m1-unattended-008/window-candidate-004/m1n1-window.bin").read_bytes()
        with self.assertRaisesRegex(ValueError, "not the window variant"):
            b.collect(backend, None, raw, None)
        self.assertEqual(len(events), 1)

    def test_live_identity_all_boundaries(self):
        class Tree(dict):
            target_type="J293"
            model="MacBookPro17,1"
        def make_tree():
            return Tree(chosen=NS(chip_id=0x8103), cpus=[NS(state="running"),NS(state="stopped")],
                        **{"/arm-io/uart0":NS(get_reg=lambda _: (0x235200000,0x4000))})
        b.validate_identity(make_tree(),0x8103)
        mutators = [lambda t:setattr(t,"target_type","J313"), lambda t:setattr(t,"model","MacBookAir10,1"),
                    lambda t:setattr(t["chosen"],"chip_id",0x8101),
                    lambda t:t["cpus"].append(NS(state="running")),
                    lambda t:t.update(cpus=[NS(state="stopped")]),
                    lambda t:t.update({"/arm-io/uart0":NS(get_reg=lambda _: (0,0x4000))})]
        for mutate in mutators:
            tree=make_tree();mutate(tree)
            with self.assertRaisesRegex(ValueError,"mismatch"):
                b.validate_identity(tree,0x8103)
        with self.assertRaisesRegex(ValueError,"mismatch"):
            b.validate_identity(make_tree(),0x8140)

    def test_wrong_transport_refused_before_bootargs(self):
        raw=(b.LAB/"artifacts/m1-unattended-008/window-candidate-004/m1n1-window.bin").read_bytes()
        _,tag=b.raw_image(raw,b.WINDOW_SHA,b.WINDOW_TAG,len(raw))
        backend=NS(p=NS(get_base=lambda:0x800000000,iodev_whoami=lambda:0),
            iface=NS(readmem=lambda *args:tag),proxy_module=NS(CPUFeatures=NS(sizeof=lambda:20),IODEV=NS(USB0=4,USB1=5)))
        with self.assertRaisesRegex(ValueError,"Native USB0/1"):
            b.collect(backend,None,raw,None)

    def test_metadata_relocated_copy_preserves_unrelated_properties(self):
        ba,fmt,tree,layout,codec,fake_construct=metadata_fixture()
        original=b.bootargs_fields(ba)
        mm=tree["chosen"]["memory-map"]
        with patch.dict(sys.modules,{"construct":fake_construct}):
            raw,adt=b.prepare_metadata(ba,fmt,tree,layout,codec)
        parsed=json.loads(raw)
        self.assertEqual(b.bootargs_fields(ba),original)
        self.assertEqual(parsed["devtree"],layout["adt"]-ba.phys_base+ba.virt_base)
        self.assertEqual(parsed["top_of_kernel_data"],layout["top"])
        self.assertEqual(parsed["devtree_size"],len(adt))
        self.assertEqual(mm._properties["untouched"],(0x808000000,0x4000))
        rebuilt=codec.load_adt(adt)["chosen"]["memory-map"]
        self.assertEqual(rebuilt._properties["AAPL,phandle"],struct.pack("<I",0x1234))
        self.assertEqual(rebuilt._properties["kernel-only"],b"true\0")
        self.assertEqual(mm._properties["DeviceTree"],(layout["adt"],len(adt)))
        self.assertEqual(mm._properties["BootArgs"],(layout["bootargs"],b.PAGE))
        for blob in layout["blobs"]:
            self.assertEqual(mm._properties[blob["name"]],(layout["fw"]+blob["offset"],blob["size"]))

    def test_adt_reparse_refuses_unintended_property_and_topology_changes(self):
        mutations = [
            lambda tree: tree._properties.update(identity=b"wrong"),
            lambda tree: tree["arm-io"]["uart0"]._properties.update(unrelated=b"changed"),
            lambda tree: tree["arm-io"]["uart0"]._properties.pop("unrelated"),
            lambda tree: tree["arm-io"]["uart0"]._properties.update(extra=b"new"),
            lambda tree: tree["arm-io"]["uart0"]._types.update(unrelated=(None,False)),
            lambda tree: tree._children.reverse(),
            lambda tree: tree._children.pop(),
            lambda tree: tree["chosen"]["memory-map"]._properties.update(untouched=struct.pack("<QQ",1,2)),
            lambda tree: tree["chosen"]["memory-map"]._properties.update(SEPFW=struct.pack("<QQ",1,2)),
            lambda tree: tree["chosen"]["memory-map"]._properties.update({"AAPL,phandle":struct.pack("<I",0x5678)}),
            lambda tree: tree["chosen"]["memory-map"]._properties.update({"kernel-only":b"false\0"}),
            lambda tree: tree["chosen"]["memory-map"]._properties.pop("kernel-only"),
        ]
        for index, mutate in enumerate(mutations):
            ba,fmt,tree,layout,codec,construct=metadata_fixture()
            def load(data):
                parsed=fake_load_adt(data); mutate(parsed); return parsed
            codec.load_adt=load
            with self.subTest(case=index), patch.dict(sys.modules,{"construct":construct}), \
                    self.assertRaisesRegex(ValueError,"ADT round-trip"):
                b.prepare_metadata(ba,fmt,tree,layout,codec)

    def test_bootargs_reparse_refuses_unintended_and_wrong_intended_values(self):
        mutations = [lambda fields: fields.update(cmdline="changed"),
                     lambda fields: fields["video"].update(display=0),
                     lambda fields: fields.update(phys_base=0),
                     lambda fields: fields.pop("mem_size_actual"),
                     lambda fields: fields.update(extra=1),
                     lambda fields: fields.update(top_of_kernel_data=0),
                     lambda fields: fields.update(devtree_size=0),
                     lambda fields: fields.update(devtree=0)]
        for index, mutate in enumerate(mutations):
            ba,fmt,tree,layout,codec,construct=metadata_fixture()
            def parse(data):
                fields=json.loads(data); mutate(fields); return fields
            fmt.parse=parse
            with self.subTest(case=index), patch.dict(sys.modules,{"construct":construct}), \
                    self.assertRaisesRegex(ValueError,"BootArgs round-trip"):
                b.prepare_metadata(ba,fmt,tree,layout,codec)

    def test_metadata_parse_failure_prevents_staging(self):
        args=NS(execute=True,device="/dev/cu.usbmodemTEST",confirm_recovery=b.CONFIRMATION,
                private_directory=Path("/tmp/bootstrap-unittest"),timeout=120)
        backend=NS(adt_module=object(),close=lambda:None)
        with patch.object(b,"connect"), patch.object(b,"collect",return_value=(None,None,None,None,None)), \
                patch.object(b,"prepare_metadata",side_effect=ValueError("round-trip rejected")), \
                patch.object(b,"stage_and_vector") as stage:
            with self.assertRaisesRegex(ValueError,"round-trip rejected"):
                b.run_worker(args,(None,None,None),factory=lambda *_:backend)
            stage.assert_not_called()

    def test_memory_map_structural_metadata_accepted_without_mutation(self):
        _,_,tree,_,codec,_=metadata_fixture()
        mm=tree["chosen"]["memory-map"]
        before=b.snapshot_adt(codec,tree)
        decoded=b.parse_memory_map(codec,mm)
        self.assertEqual(decoded,{**fixture()["memory_map"],"untouched":(0x808000000,0x4000)})
        self.assertEqual(b.snapshot_adt(codec,tree),before)
        self.assertNotIn("name",decoded)
        self.assertNotIn("AAPL,phandle",decoded)
        self.assertNotIn("kernel-only",decoded)

    def test_memory_map_malformed_phandle_refused(self):
        for size in (0,1,3,5,8,16):
            _,_,tree,_,codec,_=metadata_fixture()
            mm=tree["chosen"]["memory-map"]
            mm._properties["AAPL,phandle"]=bytes(size)
            with self.subTest(size=size), self.assertRaisesRegex(ValueError,"AAPL,phandle encoding"):
                b.parse_memory_map(codec,mm)

    def test_memory_map_unknown_nonregion_refused(self):
        for name in ("unknown","phandle","AAPL,other"):
            for size in (0,4,8,15,17):
                _,_,tree,_,codec,_=metadata_fixture()
                mm=tree["chosen"]["memory-map"]
                mm._properties[name]=bytes(size)
                with self.subTest(name=name,size=size), self.assertRaisesRegex(ValueError,"Unknown memory-map encoding"):
                    b.parse_memory_map(codec,mm)

    def test_memory_map_altered_kernel_only_refused(self):
        for value in (b"",b"true",b"True\0",b"true\0\0",b"false\0",b"\x01\0\0\0",bytes(16)):
            _,_,tree,_,codec,_=metadata_fixture()
            mm=tree["chosen"]["memory-map"]
            mm._properties["kernel-only"]=value
            with self.subTest(value=value), self.assertRaisesRegex(ValueError,"kernel-only encoding"):
                b.parse_memory_map(codec,mm)

    def test_private_metadata_survives_map_and_range_refusals(self):
        a=fixture();ba=a["ba"]
        ba.update(revision=2,version=2,cmdline="",devtree=a["dtaddr"]-ba.phys_base+ba.virt_base)
        memory_map=a["memory_map"]
        mm=NS(_properties=memory_map,_types={},_path="/device-tree/chosen/memory-map")
        class Tree(dict):
            target_type="J293"
            model="MacBookPro17,1"
        tree=Tree(chosen=AttrDict(chip_id=0x8103,**{"memory-map":mm}),cpus=[NS(state="running")],
                  **{"/arm-io/uart0":NS(get_reg=lambda _: (0x235200000,0x4000))})
        raw=(b.LAB/"artifacts/m1-unattended-008/window-candidate-004/m1n1-window.bin").read_bytes()
        tag_offset,tag=b.raw_image(raw,b.WINDOW_SHA,b.WINDOW_TAG,len(raw))
        def read(addr,size):
            if addr==a["base"]+tag_offset:return tag
            if addr==a["ba_addr"]:return b"\x02\0" if size==2 else bytes(a["ba_size"])
            if addr==a["dtaddr"]:return bytes(a["dt_size"])
            raise AssertionError((addr,size))
        backend=NS(p=NS(get_base=lambda:a["base"],iodev_whoami=lambda:4,get_bootargs=lambda:a["ba_addr"],
                        get_chipid=lambda:0x8103,heapblock_alloc=lambda size:a["heap"]),
            iface=NS(readmem=read),types=NS(BootArgs_r2=NS(sizeof=lambda:a["ba_size"],parse=lambda _:ba)),
            proxy_module=NS(CPUFeatures=NS(sizeof=lambda:20),IODEV=NS(USB0=4,USB1=5)),
            adt_module=NS(load_adt=lambda _:tree,build_prop=fake_build_prop))
        for bad, error in (({},"range refusal"),({"AAPL,phandle":bytes(8)},"AAPL,phandle encoding"),
                           ({"kernel-only":b"false\0"},"kernel-only encoding"),
                           ({"unknown":bytes(4)},"Unknown memory-map encoding")):
            mm._properties={**memory_map,**bad}
            with self.subTest(error=error), tempfile.TemporaryDirectory() as directory, \
                    patch.object(b,"read_features",return_value={}), \
                    patch.object(backend.p,"heapblock_alloc",return_value=a["heap"]) as heap, \
                    patch.object(b,"plan_memory",side_effect=ValueError("synthetic range refusal")) as plan:
                with self.assertRaisesRegex(ValueError,error):
                    b.collect(backend,NS(inventory_sha256="synthetic"),raw,Path(directory))
                self.assertEqual((Path(directory)/"original-bootargs.bin").read_bytes(),bytes(a["ba_size"]))
                self.assertEqual((Path(directory)/"original-adt.bin").read_bytes(),bytes(a["dt_size"]))
                if bad:
                    heap.assert_not_called()
                    plan.assert_not_called()
                    self.assertFalse((Path(directory)/"original-observation.json").exists())
                else:
                    saved=json.loads((Path(directory)/"original-observation.json").read_text())
                    self.assertEqual(saved["memory_map"]["SEPFW"],dict(zip(("addr","size"),memory_map["SEPFW"])))
                    self.assertEqual(saved["live_heap"],a["heap"])

    def test_features_pointer_and_abi_checked_before_read(self):
        a = fixture(); reads=[]; requests=[]
        p = NS(P_GET_CPU_FEATURES=42, request=lambda *args: requests.append(args) or 0)
        backend = NS(p=p, iface=NS(readmem=lambda *args: reads.append(args)),
                     proxy_module=NS(CPUFeatures=NS(sizeof=lambda:20)))
        with self.assertRaisesRegex(ValueError, "CPUFeatures"):
            b.read_features(backend, a["ba"])
        self.assertEqual(requests, [(42,20)]); self.assertEqual(reads, [])
        backend.proxy_module.CPUFeatures.sizeof=lambda:21
        with self.assertRaisesRegex(ValueError, "size changed"):
            b.read_features(backend, a["ba"])
        self.assertEqual(len(requests), 1)

    def stage(self, *, corrupt=False, vector_error=False, moved=False):
        events=[]; a=fixture(); layout=b.plan_memory(**a)
        backend=NS(p=FakeProxy(a["heap"]+(64 if moved else 0),events,vector_error),
                   iface=FakeInterface(events,corrupt))
        stub=(HERE/"bootstrap-trampoline.bin").read_bytes()
        with tempfile.TemporaryDirectory() as d:
            try:
                b.stage_and_vector(backend,b"x"*0x400000,stub,layout,b"bootargs",b"adt",Path(d))
            except (ValueError,OSError):
                if not (corrupt or vector_error or moved):
                    raise
            files={p.name:p.read_bytes() for p in Path(d).iterdir()}
        return events,layout,files

    def test_staging_copy_order_one_vector_no_firmware_host_read(self):
        events,layout,files=self.stage()
        requests=[e for e in events if e[0]=="request"]
        self.assertEqual(requests,[("request",11,layout["stub"],layout["bootargs"],layout["stage"],layout["base"],layout["copy_size"])])
        self.assertEqual(events[-1],requests[0])
        self.assertEqual(sum(e[0]=="firmware_copy" for e in events),3)
        self.assertFalse(any(e[0]=="read" and e[1] in [x["src"] for x in layout["blobs"]] for e in events))
        self.assertIn("vector-ack.json",files)
        self.assertFalse(json.loads(files["vector-ack.json"])["runtime_verified"])

    def test_write_mismatch_or_heap_drift_prevents_vector(self):
        for kwargs in ({"corrupt":True},{"moved":True}):
            events,_,files=self.stage(**kwargs)
            self.assertFalse(any(e[0]=="request" for e in events))
            self.assertNotIn("vector-attempt.json",files)
            if kwargs.get("moved"):
                self.assertFalse(any(e[0] in ("write","firmware_copy") for e in events))

    def test_missing_vector_reply_never_retries(self):
        events,_,files=self.stage(vector_error=True)
        self.assertEqual(sum(e[0]=="request" for e in events),1)
        self.assertIn("vector-attempt.json",files)
        self.assertNotIn("vector-ack.json",files)

    def test_short_write_stops_acquisition(self):
        events=[]; dev=NS(write=lambda data:len(data)-1)
        iface=NS(dev=dev,nop=lambda:dev.write(b"test-NOP"))
        backend=NS(proxy_module=NS(UartInterface=lambda **kw:iface,
                                  M1N1Proxy=lambda _:events.append("proxy constructed")))
        with self.assertRaisesRegex(ValueError,"Short serial write"):
            b.connect(backend,"/dev/cu.usbmodemTEST")
        self.assertEqual(events,[])

    def test_timeout_only_kills_host_worker(self):
        events=[]
        class Process:
            exitcode=-9
            def __init__(self,**kwargs): events.append("construct")
            def start(self): events.append("start")
            def join(self,n): events.append(("join",n))
            def is_alive(self): return True
            def kill(self): events.append("kill-host")
        result=b.supervise(NS(timeout=1),Process)
        self.assertEqual(events,["construct","start",("join",1),"kill-host",("join",5)])
        self.assertFalse(result["runtime_verified"])

    def test_no_forbidden_execution_helpers_in_source(self):
        text=(HERE/"bootstrap.py").read_text()
        tree=ast.parse(text)
        calls=[n.func.attr for n in ast.walk(tree) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute)]
        for name in ("collect","new_hv","reload","reboot","reset_panic_counter","mmu_shutdown","write64"):
            self.assertNotIn(name,calls)
        self.assertNotIn("m1n1.setup",text)
        self.assertNotIn("ARMAsm(",text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
