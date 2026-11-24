"""Offline source/staging tests; no XNU build and no target execution."""
from __future__ import annotations
from contextlib import contextmanager
import importlib.util
import json
from pathlib import Path
import shutil
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


class EntryAssemblyTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("clang") and shutil.which("llvm-objdump"),
                         "needs existing clang and llvm-objdump; never installs them")
    def test_macho_assembly_and_configuration_guards(self):
        with tempfile.TemporaryDirectory(prefix="dl-m1-entry-test-") as temp:
            root = Path(temp)
            assembly = root / "entry.S"
            obj = root / "entry.o"
            assembly.write_text(
                "#define EXT(x) _##x\n#define LEXT(x) _##x:\n.text\n"
                "#include \"" + str(HERE / "m1_first_entry.inc") + "\"\n"
                ".align 12\n_LowExceptionVectorBase:\nb .\n")
            command = ["clang", "-target", "arm64-apple-darwin", "-mcpu=apple-m1",
                       "-c", str(assembly), "-o", str(obj)]
            ok = subprocess.run(command + ["-DDEBUG=1", "-DARM64_BOARD_CONFIG_T8103=1"],
                                capture_output=True, check=False)
            self.assertEqual(ok.returncode, 0, ok.stderr.decode())
            symbols = subprocess.check_output(["llvm-objdump", "--syms", str(obj)], text=True)
            self.assertIn("_darwin_lab_m1_first_entry", symbols)
            disassembly = subprocess.check_output(
                ["llvm-objdump", "--disassemble", "--stop-address=208", str(obj)], text=True)
            self.assertIn("str\tw3, [x1, #0x20]", disassembly)
            for defines in (["-DDEBUG=0", "-DARM64_BOARD_CONFIG_T8103=1"], ["-DDEBUG=1"]):
                bad = subprocess.run(command + defines, capture_output=True, check=False)
                self.assertNotEqual(bad.returncode, 0)
                self.assertIn(b"requires the native T8103 DEBUG", bad.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
