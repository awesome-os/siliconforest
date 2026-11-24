#!/usr/bin/env python3
"""Host policy checks. Fake APIs never establish Darwin exec inheritance."""
from pathlib import Path
import os
import resource
import shutil
import signal
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
CC = os.environ.get("CC", "cc")
FLAGS = ["-D_POSIX_C_SOURCE=200809L", "-DCHONK_SPAWN_TEST_API",
         "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1"]


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class SpawnPolicy(unittest.TestCase):
    def build(self, directory, source=None, extra=()):
        path = Path(directory)
        for name in ("darwin_spawn.h", "spawn_test_api.h", "test_spawn.c"):
            shutil.copyfile(HERE / name, path / name)
        (path / "darwin_spawn.c").write_text(
            source if source is not None else (HERE / "darwin_spawn.c").read_text())
        binary = path / "test-spawn"
        result = subprocess.run([CC, *FLAGS, *extra, str(path / "darwin_spawn.c"),
                                 str(path / "test_spawn.c"), "-o", str(binary)],
                                capture_output=True, text=True, timeout=30)
        return binary, result

    def run_binary(self, binary):
        return subprocess.run([str(binary)], capture_output=True, text=True,
                              timeout=10, preexec_fn=no_core)

    def test_fake_api_failure_sweep_sanitized(self):
        with tempfile.TemporaryDirectory(prefix="chonk-spawn-policy-") as tmp:
            binary, build = self.build(tmp, extra=("-fsanitize=address,undefined",
                                                  "-fno-omit-frame-pointer"))
            self.assertEqual(build.returncode, 0, build.stderr)
            result = self.run_binary(binary)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("PASS: 29 fake API failure boundaries", result.stdout)

    def test_ndebug_rejected(self):
        with tempfile.TemporaryDirectory(prefix="chonk-spawn-ndebug-") as tmp:
            _, result = self.build(tmp, extra=("-DNDEBUG",))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("assertion-based fake API tests require assertions", result.stderr)

    def test_compiled_mutants_are_killed_by_assertion(self):
        original = (HERE / "darwin_spawn.c").read_text()
        mutants = {
            "omit_cloexec_default": (
                "const short flags=POSIX_SPAWN_CLOEXEC_DEFAULT|POSIX_SPAWN_SETSIGDEF|",
                "const short flags=POSIX_SPAWN_SETSIGDEF|"),
            "overwrite_combined_flags": (
                "error=posix_spawnattr_getflags(&attributes,&observed);",
                "error=posix_spawnattr_setflags(&attributes,flags);\n"
                "    if (error) goto cleanup;\n"
                "    error=posix_spawnattr_getflags(&attributes,&observed);"),
            "ignore_flag_readback": (
                "if (observed!=flags) { error=EINVAL; goto cleanup; }",
                "if (observed!=flags) { /* mutant ignores refused close policy */ }"),
            "discard_child_on_cleanup_error": (
                "return error ? error : cleanup_error;",
                "if (cleanup_error) { r->pid=0; r->child_created=0; }\n"
                "    return error ? error : cleanup_error;"),
            "skip_owned_fd_close": (
                "for (i=3;i>=0;i--) if (duplicates[i]>=0)",
                "for (i=3;i>=1;i--) if (duplicates[i]>=0)"),
            "ignore_dup2_error": (
                "error=posix_spawn_file_actions_adddup2(&actions,duplicates[i],i);\n"
                "        if (error) goto cleanup;",
                "error=posix_spawn_file_actions_adddup2(&actions,duplicates[i],i);\n"
                "        error=0;"),
        }
        for name, (before, after) in mutants.items():
            with self.subTest(mutant=name):
                self.assertEqual(original.count(before), 1)
                with tempfile.TemporaryDirectory(prefix="chonk-spawn-mutant-") as tmp:
                    binary, build = self.build(tmp, original.replace(before, after))
                    self.assertEqual(build.returncode, 0, build.stderr)
                    result = self.run_binary(binary)
                    self.assertEqual(result.returncode, -signal.SIGABRT, result.stderr)
                    self.assertIn("Assertion", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
