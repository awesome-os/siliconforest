"""Offline source/staging tests; no XNU build and no target execution."""
from __future__ import annotations
from contextlib import contextmanager
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]
BASE = LAB / "sources/puredarwin-baseline"
spec = importlib.util.spec_from_file_location("m1_stage", HERE / "stage.py")
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


@contextmanager
def fixture():
    """Tiny local repo with real pinned before-bytes; synthetic commit identity."""
    with tempfile.TemporaryDirectory(prefix="dl-m1-stage-test-") as temp:
        root = Path(temp) / "checkout"
        sources = Path(temp) / "integration"
        root.mkdir()
        sources.mkdir()
        manifest = json.loads((HERE / "manifest.json").read_text())
        for name, record in manifest["files"].items():
            if record["before_sha256"] is not None:
                dest = root / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(stage.git(BASE, "show", stage.PIN + ":" + name))
        stage.git(root, "init", "-q")
        stage.git(root, "add", ".")
        stage.git(root, "-c", "user.name=Offline Fixture", "-c",
                  "user.email=fixture@invalid", "commit", "-qm", "fixture")
        pin = stage.git(root, "rev-parse", "HEAD").decode().strip()
        manifest["pin"] = pin
        (sources / "manifest.json").write_text(json.dumps(manifest))
        shutil.copyfile(HERE / "candidate.patch", sources / "candidate.patch")
        with patch.object(stage, "HERE", sources), patch.object(stage, "PIN", pin):
            yield root, sources, manifest


class StagingTests(unittest.TestCase):
    def test_authoritative_patch_applies_to_exact_local_pin(self):
        self.assertEqual(stage.git(BASE, "rev-parse", "HEAD").decode().strip(), stage.PIN)
        data = (HERE / "candidate.patch").read_bytes()
        manifest = json.loads((HERE / "manifest.json").read_text())
        self.assertEqual(stage.sha256(data), manifest["patch_sha256"])
        stage.git(BASE, "apply", "--check", "--whitespace=error-all", "-", data=data)

    def test_dryrun_then_exact_stage_and_restage_refusal(self):
        with fixture() as (root, _, manifest):
            before = stage.git(root, "status", "--porcelain")
            self.assertFalse(stage.stage(root)["executed"])
            self.assertEqual(stage.git(root, "status", "--porcelain"), before)
            result = stage.stage(root, True)
            self.assertTrue(result["executed"])
            self.assertFalse(result["runtime_proof"])
            for name, record in manifest["files"].items():
                self.assertEqual(stage.sha256((root / name).read_bytes()), record["after_sha256"])
            self.assertEqual((root / "src/Kernel/xnu/osfmk/arm64/m1_first_entry.inc").read_bytes(),
                             (HERE / "m1_first_entry.inc").read_bytes())
            self.assertEqual((root / "nix/m1-firstboot.nix").read_bytes(),
                             (HERE / "kernel-firstboot.nix").read_bytes())
            with self.assertRaises(stage.StageError):
                stage.stage(root, True)

    def test_wrong_pin_and_corrupt_patch_refused_before_mutation(self):
        with fixture() as (root, sources, _):
            with patch.object(stage, "PIN", "0" * 40):
                with self.assertRaisesRegex(stage.StageError, "Wrong commit"):
                    stage.stage(root, True)
            with (sources / "candidate.patch").open("ab") as output:
                output.write(b"\n# unmanifested drift\n")
            with self.assertRaisesRegex(stage.StageError, "manifest mismatch"):
                stage.stage(root, True)
            self.assertFalse(stage.git(root, "status", "--porcelain").strip())

    def test_assume_unchanged_does_not_hide_target_drift(self):
        with fixture() as (root, _, _):
            name = "flake.nix"
            stage.git(root, "update-index", "--assume-unchanged", name)
            with (root / name).open("a") as output:
                output.write("# unreviewed drift\n")
            self.assertFalse(stage.git(root, "status", "--porcelain").strip())
            with self.assertRaisesRegex(stage.StageError, "source bytes differ"):
                stage.stage(root, True)
            self.assertFalse((root / "nix/m1-firstboot.nix").exists())

    def test_protected_checkout_and_link_refused(self):
        with self.assertRaisesRegex(stage.StageError, "read-only"):
            stage.stage(BASE, True)
        with fixture() as (root, _, _):
            link = root.parent / "linked-checkout"
            link.symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(stage.StageError, "symlink refused"):
                stage.stage(link, True)
            self.assertFalse(stage.git(root, "status", "--porcelain").strip())

    def test_dirty_tree_refused_before_additions(self):
        with fixture() as (root, _, _):
            (root / "unrelated.txt").write_text("owned by someone else")
            with self.assertRaisesRegex(stage.StageError, "must be clean"):
                stage.stage(root, True)
            self.assertFalse((root / "nix/m1-firstboot.nix").exists())

    @unittest.skipUnless(shutil.which("clang"), "needs existing clang")
    def test_experiment_only_backend_selection_and_public_sources(self):
        with fixture() as (root, _, _):
            stage.stage(root, True)
            board = "src/Kernel/xnu/pexpert/pexpert/arm64/board_config.h"
            def macros(path, experiment=False):
                command = ["clang", "-target", "arm64-apple-darwin", "-mcpu=apple-m1",
                           "-x", "c", "-E", "-dM", "-DKERNEL_PRIVATE=1", "-DASSEMBLER=1",
                           "-DARM64_BOARD_CONFIG_T8103=1", "-DARM_LARGE_MEMORY=1", "-DDEBUG=1",
                           "-I" + str(BASE / "src/Kernel/xnu/pexpert")]
                if experiment:
                    command.append("-DDARWIN_LAB_M1_FIRSTBOOT=1")
                result = subprocess.run(command + ["-"], input=f'#include "{path}"\n',
                                        text=True, capture_output=True, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                return set(result.stdout.splitlines())
            native = macros(BASE / board)
            self.assertEqual(macros(root / board), native, "Normal T8103 features must stay identical")
            experiment = macros(root / board, True)
            self.assertEqual(native - experiment, {"#define PMAP_CS 1", "#define PMAP_CS_ENABLE 1",
                                                  "#define XNU_MONITOR 1", "#define KERNEL_INTEGRITY_CTRR 1"})
            self.assertEqual(experiment - native, {"#define DARWIN_LAB_M1_FIRSTBOOT 1"})
            self.assertIn("#define HAS_CTRR 1", experiment)
            self.assertIn("#define __ARM_16K_PG__ 1", experiment)
            master = (root / "src/Kernel/xnu/config/MASTER.arm64.t8103").read_text()
            original = (BASE / "src/Kernel/xnu/config/MASTER.arm64.MacOSX").read_text()
            self.assertEqual(master.splitlines(keepends=True)[2:],
                             original.replace("#  ARM_EXTRAS_BASE = [ config_pmap_ppl ]",
                                              "#  ARM_EXTRAS_BASE = [ nos_arm_pmap nos_arm_asm config_pmap_ppl ]").replace("sched_hygiene_debug ", "").splitlines(keepends=True))


class EntryAssemblyTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("clang"), "needs existing clang")
    def test_experiment_register_alias_has_source_supported_encoding(self):
        with fixture() as (root, _, _):
            stage.stage(root, True)
            board = root / "src/Kernel/xnu/pexpert/pexpert/arm64/board_config.h"
            asm = root / "override.S"
            obj = root / "override.o"
            asm.write_text(f'#include "{board}"\n.text\nmrs x9, CPU_OVRD\nmsr CPU_OVRD, x9\n'
                           'mrs x13, CPU_CFG\nmsr CPU_CFG, x13\nmrs x1, ACC_OVRD\nmsr ACC_OVRD, x1\n')
            command = ["clang", "-target", "arm64-apple-darwin", "-mcpu=apple-m1",
                       "-DASSEMBLER=1", "-DARM64_BOARD_CONFIG_T8103=1",
                       "-DARM_LARGE_MEMORY=1", "-DDEBUG=1", "-DKERNEL_PRIVATE=1",
                       "-I"+str(BASE / "src/Kernel/xnu/pexpert"),
                       "-c", str(asm), "-o", str(obj)]
            result = subprocess.run(command + ["-DDARWIN_LAB_M1_FIRSTBOOT=1"], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            words = struct.unpack("<6I", self.text_section(obj))
            # Independently encode m1n1 CYC_OVRD, ACC_CFG and ACC_OVRD tuples.
            expected = []
            for crm, rt in [(5, 9), (4, 13), (6, 1)]:
                reg = (3<<19) | (5<<16) | (15<<12) | (crm<<8) | rt
                expected.extend([0xd5200000 | reg, 0xd5000000 | reg])
            self.assertEqual(words, tuple(expected))
            native = subprocess.run(command, capture_output=True)
            self.assertNotEqual(native.returncode, 0, "No alias in normal T8103 selection")

    @unittest.skipUnless(shutil.which("clang") and shutil.which("llvm-objdump"), "needs existing tools")
    def test_missing_cache_operations_are_explicit_nonreturning_failstops(self):
        with tempfile.TemporaryDirectory(prefix="dl-m1-failstop-") as temp:
            obj, _ = self.assemble(Path(temp), (HERE / "m1_first_entry.inc").read_text())
            symbols = subprocess.check_output(["llvm-objdump", "--syms", str(obj)], text=True)
            names = ["CleanPoC_DcacheRegion_Force_nopreempt", "CleanPoC_DcacheRegion_Force_nopreempt_nohid",
                     "enable_dc_mva_ops", "disable_dc_mva_ops", "darwin_lab_m1_unsupported_cache"]
            addresses = []
            for name in names:
                match = re.search(r"^([0-9a-f]+)\s+.* _" + name + r"$", symbols, re.MULTILINE)
                self.assertIsNotNone(match, name)
                addresses.append(int(match[1], 16))
            self.assertEqual(len(set(addresses)), 1)
            code = self.text_section(obj)[addresses[0]:addresses[0]+12]
            # Mask DAIF, diagnostic failure 0xef, self-loop. No return, call,
            # memory access, hardware operation or successful completion.
            self.assertEqual(struct.unpack("<3I", code), (0xd5034fdf, 0xd2801df3, 0x14000000))

    @staticmethod
    def text_section(obj):
        data = obj.read_bytes()
        magic, cpu, _, kind, count = struct.unpack_from("<5I", data)
        assert (magic, cpu, kind) == (0xfeedfacf, 0x100000c, 1)  # ARM64 MH_OBJECT
        offset = 32
        for _ in range(count):
            command, size = struct.unpack_from("<2I", data, offset)
            if command == 0x19:  # LC_SEGMENT_64
                sections = struct.unpack_from("<I", data, offset + 64)[0]
                for index in range(sections):
                    section = offset + 72 + 80 * index
                    name, segment, addr, length, file_offset = struct.unpack_from("<16s16sQQI", data, section)
                    if name.rstrip(b"\0") == b"__text" and segment.rstrip(b"\0") == b"__TEXT":
                        assert addr == 0
                        return data[file_offset:file_offset + length]
            offset += size
        raise AssertionError("Missing assembled text section")

    def assert_entry_contract(self, obj):
        """Inspect assembled bytes and their control-flow graph, not source tokens."""
        section = self.text_section(obj)
        end = section.index(b"HVLOG: DARWIN_LAB_M1_EARLY_CONSOLE ")
        self.assertEqual(end % 4, 0)
        words = struct.unpack("<" + "I" * (end // 4), section[:end])
        output = subprocess.check_output(
            ["llvm-objdump", "--disassemble", f"--stop-address={end}", str(obj)], text=True)
        instructions = []
        for line in output.splitlines():
            match = re.match(r"\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)\s*(.*)", line)
            if match:
                address, word, mnemonic, operands = match.groups()
                operands = re.sub(r"0x[0-9a-f]+", lambda m: str(int(m[0], 16)), operands.split(";")[0])
                operands = re.sub(r"\s+", " ", operands).strip()
                instructions.append((int(address, 16), int(word, 16), mnemonic, operands))
        self.assertEqual([i[0] for i in instructions], list(range(0, end, 4)))
        self.assertEqual(tuple(i[1] for i in instructions), words)
        self.assertEqual(words[0], 0xd5034fdf, "DAIFSet #15 must be the first instruction")
        ops = [(i[2], i[3]) for i in instructions]
        allowed = {"msr", "mrs", "mov", "movz", "movk", "cmp", "b.ne", "b.eq",
                   "tbnz", "adrp", "add", "isb", "ubfx", "adr", "ldrb", "cbz",
                   "ldr", "subs", "b", "str", "dsb"}
        for _, _, mnemonic, operands in instructions:
            self.assertIn(mnemonic, allowed, "No call, return, indirect branch or unknown instruction")
            self.assertIsNone(re.search(r"\b(?:[wx]0|w?sp)\b", operands), "x0 and the stack stay untouched")
        self.assertEqual([o for m, o in ops if m == "msr"], ["DAIFSet, #15", "VBAR_EL1, x9"])
        el = ops.index(("mrs", "x9, CurrentEL"))
        self.assertEqual(ops[el + 1], ("cmp", "x9, #4"))
        self.assertEqual(ops[el + 2][0], "b.ne")
        self.assertIn(("mrs", "x9, SCTLR_EL1"), ops)
        self.assertEqual(sum(m == "tbnz" and o.startswith("w9, #0,") for m, o in ops), 1)
        midr = ops.index(("mrs", "x9, MIDR_EL1"))
        self.assertEqual(ops[midr + 1:midr + 3], [("ubfx", "x10, x9, #24, #8"), ("cmp", "x10, #97")])
        self.assertIn(("ubfx", "x10, x9, #4, #12"), ops)
        self.assertEqual([o for m, o in ops if m == "cmp" and o.startswith("x10,")],
                         ["x10, #97", "x10, #34", "x10, #35"])
        # Independently evaluate the wide-immediate writes that construct x1.
        uart = 0
        assignments = 0
        for _, word, mnemonic, operands in instructions:
            if mnemonic in {"mov", "movz", "movk", "add", "mrs", "adr", "adrp", "ldr", "ldrb", "subs", "ubfx"} and operands.startswith("x1,"):
                self.assertIn(word & 0x7f800000, (0x52800000, 0x72800000))
                self.assertEqual(word >> 31, 1)
                shift = ((word >> 21) & 3) * 16
                value = ((word >> 5) & 0xffff) << shift
                uart = value if (word & 0x7f800000) == 0x52800000 else (uart & ~(0xffff << shift)) | value
                assignments += 1
        self.assertEqual((uart, assignments), (0x235200000, 2))
        self.assertEqual([o for m, o in ops if m == "ldr"], ["w5, [x1, #16]"] * 2)
        self.assertEqual([o for m, o in ops if m == "str"], ["w3, [x1, #32]"])
        self.assertEqual([o.split(",")[1].strip() for m, o in ops if m == "tbnz" and o.startswith("w5,")], ["#1", "#2"])
        self.assertEqual([o for m, o in ops if m == "ldrb"], ["w3, [x2], #1"])
        # Decode all direct branch successors from instruction words. Every edge
        # must stay in code; in particular the last instruction cannot fall into
        # the inline marker. The whitelist excludes BL/BR/BLR/RET/ERET as well.
        def signed(value, bits):
            return value - (1 << bits) if value & (1 << (bits - 1)) else value
        for address, word, mnemonic, _ in instructions:
            successors = [address + 4]
            if mnemonic == "b":
                self.assertEqual(word & 0xfc000000, 0x14000000)
                successors = [address + 4 * signed(word & 0x3ffffff, 26)]
            elif mnemonic in ("b.ne", "b.eq", "cbz"):
                successors.append(address + 4 * signed((word >> 5) & 0x7ffff, 19))
            elif mnemonic == "tbnz":
                successors.append(address + 4 * signed((word >> 5) & 0x3fff, 14))
            self.assertTrue(all(0 <= pc < end and pc % 4 == 0 for pc in successors),
                            f"Control flow escapes entry code at {address:#x}: {successors}")
        self.assertEqual(words[-1], 0x14000000, "Final instruction must park, never fall into marker data")

    @staticmethod
    def assemble(root, source):
        assembly, obj = root / "entry.S", root / "entry.o"
        assembly.write_text("#define EXT(x) _##x\n#define LEXT(x) _##x:\n.text\n" + source +
                            "\n.align 12\n_LowExceptionVectorBase:\nb .\n")
        command = ["clang", "-target", "arm64-apple-darwin", "-mcpu=apple-m1",
                   "-c", str(assembly), "-o", str(obj)]
        result = subprocess.run(command + ["-DDEBUG=1", "-DARM64_BOARD_CONFIG_T8103=1"],
                                capture_output=True, check=False)
        assert result.returncode == 0, result.stderr.decode()
        return obj, command

    @unittest.skipUnless(shutil.which("clang") and shutil.which("llvm-objdump"),
                         "needs existing clang and llvm-objdump; never installs them")
    def test_macho_assembly_and_configuration_guards(self):
        with tempfile.TemporaryDirectory(prefix="dl-m1-entry-test-") as temp:
            root = Path(temp)
            obj, command = self.assemble(root, (HERE / "m1_first_entry.inc").read_text())
            symbols = subprocess.check_output(["llvm-objdump", "--syms", str(obj)], text=True)
            self.assertIn("_darwin_lab_m1_first_entry", symbols)
            self.assert_entry_contract(obj)
            for defines in (["-DDEBUG=0", "-DARM64_BOARD_CONFIG_T8103=1"], ["-DDEBUG=1"]):
                bad = subprocess.run(command + defines, capture_output=True, check=False)
                self.assertNotEqual(bad.returncode, 0)
                self.assertIn(b"requires the native T8103 DEBUG", bad.stderr)

    @unittest.skipUnless(shutil.which("clang") and shutil.which("llvm-objdump"), "needs existing tools")
    def test_assembled_contract_rejects_regressions(self):
        original = (HERE / "m1_first_entry.inc").read_text()
        mutations = {
            "late_mask": ("\tmsr DAIFSet, #0xf\n", ""),
            "uart_base": ("#0x3520", "#0x3521"),
            "uart_read": ("[x1, #0x10]", "[x1, #0x14]"),
            "tx_bit": ("w5, #1,", "w5, #3,"),
            "wrong_el": ("cmp x9, #4", "cmp x9, #8"),
            "wrong_midr": ("cmp x10, #0x23", "cmp x10, #0x25"),
            "x0_write": ("mov x19, #0\n", "mov x0, #0\n"),
            "call": ("\tdsb sy\n", "\tbl Ldl_m1_park\n"),
            "return": ("\tdsb sy\n", "\tret\n"),
            "fallthrough": ("\tb Ldl_m1_park\nLdl_m1_message:", "\tisb\nLdl_m1_message:"),
        }
        with tempfile.TemporaryDirectory(prefix="dl-m1-entry-mutations-") as temp:
            for name, (old, new) in mutations.items():
                with self.subTest(mutation=name):
                    self.assertIn(old, original)
                    source = original.replace(old, new, 1)
                    if name == "late_mask":
                        source = source.replace("\tmrs x9, SCTLR_EL1", "\tmsr DAIFSet, #0xf\n\tmrs x9, SCTLR_EL1", 1)
                    obj, _ = self.assemble(Path(temp), source)
                    with self.assertRaises((AssertionError, ValueError)):
                        self.assert_entry_contract(obj)


if __name__ == "__main__":
    unittest.main(verbosity=2)
