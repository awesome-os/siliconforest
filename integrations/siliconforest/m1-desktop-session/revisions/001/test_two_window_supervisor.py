#!/usr/bin/env python3
"""Host stub exercise for darwin-two-window-live-test. Logic only; no guest binaries."""
import os
import signal
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "darwin-two-window-live-test"
NONCE = "0123456789abcdef0123456789abcdef0123456789abcdef"

SOURCE_ABSENT = (
    "pdhid-keyboard",
    "darwin-keyboard-inventory",
    "--phase closed",
    "DL12_",
    "DL15_",
    "DL11_",
    "DLT_",
)


def alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True


class SupervisorStub(unittest.TestCase):
    def tearDown(self):
        for pid in getattr(self, "_pids", ()):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def test_syntax_and_source_contract(self):
        syntax = subprocess.run(["/bin/sh", "-n", str(SCRIPT)], check=False)
        self.assertEqual(syntax.returncode, 0)
        text = SCRIPT.read_text()
        for banned in SOURCE_ABSENT:
            self.assertNotIn(banned, text)
        self.assertLess(
            text.index("/usr/bin/darwin-wayland-client-ssd"),
            text.index("/usr/bin/darwin-wayland-terminal-native-startup"),
        )
        self.assertIn("--input pdhid", text)
        self.assertEqual(text.count("--seconds 120"), 2)
        self.assertIn("sleep 85", text)
        self.assertIn("--phase live", text)
        self.assertIn("cleanup_claimed=false", text)
        self.assertIn("[ ! -c /dev/usb_hid_kbd ]", text)
        self.assertIn("[ ! -c /dev/usb_hid_mouse ]", text)
        self.assertIn("/usr/bin/darwin-trackpad-enum", text)
        self.assertIn("/usr/bin/darwin-native-input-inventory", text)
        self.assertNotIn("[ ! -e /dev/usb_hid_mouse ]", text)

    def test_bad_nonce_exits_before_markers(self):
        for bad in ("", "abc", "A" * 48, "g" * 48, "0" * 47):
            with self.subTest(bad=bad):
                result = self.run_script(bad, nodes="both")
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertNotIn("DL18_", result.stdout)

    def test_enum_77_skips_gate_and_windows(self):
        result = self.run_script(NONCE, enum_rc=77)
        self.assertEqual(result.returncode, 77, result.stdout)
        self.assertIn(f"DL18_TWO_WINDOW_ENUM_{NONCE} rc=77", result.stdout)
        self.assertNotIn("IDENTITY", result.stdout)
        self.assertNotIn("compositor ", result.log)

    def test_enum_failure_skips_gate(self):
        result = self.run_script(NONCE, enum_rc=1)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertNotIn("IDENTITY", result.stdout)
        self.assertEqual(result.log.count("gate "), 0)

    def test_nodes_present_do_not_sleep(self):
        result = self.run_script(NONCE, nodes="both", gate_rc=77)
        self.assertEqual(result.returncode, 77, result.stdout)
        self.assertIn("missing_checks=0", result.stdout)
        self.assertEqual(result.sleeps01, 0)
        self.assertIn("NOT_READY", result.stdout)
        self.assertNotIn("compositor ", result.log)

    def test_nodes_after_one_sleep_and_gate_failure(self):
        result = self.run_script(NONCE, nodes="after1", gate_rc=1)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("missing_checks=1", result.stdout)
        self.assertEqual(result.sleeps01, 1)
        self.assertIn("FAILED", result.stdout)
        self.assertEqual(result.log.count("gate "), 1)
        self.assertNotIn("compositor ", result.log)

    def test_nodes_never_still_runs_gate_once(self):
        result = self.run_script(NONCE, nodes="none", gate_rc=77)
        self.assertEqual(result.returncode, 77, result.stdout)
        self.assertIn("missing_checks=101", result.stdout)
        self.assertEqual(result.sleeps01, 100)
        self.assertEqual(result.log.count("gate "), 1)

    def test_keyboard_node_only_still_waits_then_one_gate(self):
        result = self.run_script(NONCE, nodes="kbd", gate_rc=77)
        self.assertEqual(result.returncode, 77, result.stdout)
        self.assertEqual(result.sleeps01, 100)
        self.assertEqual(result.log.count("gate "), 1)

    def test_compositor_exit_before_socket_launches_no_client(self):
        result = self.run_script(NONCE, nodes="both", compositor="exit1")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertNotIn("SERVER_SOCKET", result.stdout)
        self.assertNotIn("darwin-wayland-client-ssd ", result.log)
        self.assertNotIn("darwin-wayland-terminal-native-startup ", result.log)

    def test_observer_death_before_export_reaps_the_other_window(self):
        result = self.run_script(NONCE, nodes="both", observer="exit1")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertNotIn("LIVE_RECORDS", result.stdout)
        self.assertNotIn("records ", result.log)
        self.assertFalse(alive(result.pids["compositor"]))
        self.assertFalse(alive(result.pids["darwin-wayland-client-ssd"]))

    def test_live_records_failure_is_not_success(self):
        result = self.run_script(NONCE, nodes="both", records_rc=1)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("phase=live rc=1 cleanup_claimed=false", result.stdout)
        self.assertNotIn("TWO WINDOW LIVE ONLY", result.stdout)
        self.assertNotIn("closed", result.log)

    def test_success_launches_ssd_then_observer_and_live_export(self):
        result = self.run_script(NONCE, nodes="both")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn(f"DL18_TWO_WINDOW_BEGIN_{NONCE}", result.stdout)
        self.assertIn("missing_checks=0", result.stdout)
        self.assertIn(f"DL18_TWO_WINDOW_IDENTITY_{NONCE} rc=0", result.stdout)
        self.assertIn("SERVER_SOCKET", result.stdout)
        ssd_at = result.stdout.index("SSD_PID")
        observer_at = result.stdout.index("OBSERVER_PID")
        self.assertLess(ssd_at, observer_at)
        self.assertLess(observer_at, result.stdout.index("LIVE_RECORDS"))
        self.assertIn(
            f"DL18_TWO_WINDOW_STATUS_{NONCE} ssd=0 observer=0 compositor=0 records=0",
            result.stdout,
        )
        self.assertIn(f"DL18_TWO_WINDOW_RETURN_{NONCE}", result.stdout)
        self.assertIn(f"DL18_TWO_WINDOW_END_{NONCE} physical_input_proven=false", result.stdout)
        self.assertEqual(result.pids["darwin-wayland-client-ssd"] != result.pids[
            "darwin-wayland-terminal-native-startup"], True)
        comp = self.line(result.log, "compositor ")
        self.assertIn("--run", comp)
        self.assertIn("--input pdhid", comp)
        self.assertNotIn("pdhid-keyboard", comp)
        for name in (
            "darwin-wayland-client-ssd ",
            "darwin-wayland-terminal-native-startup ",
        ):
            line = self.line(result.log, name)
            self.assertIn(f"--socket {result.runtime}/wayland-0", line)
            self.assertIn(f"--nonce {NONCE}", line)
            self.assertIn("--seconds 120", line)
        records = self.line(result.log, "records ")
        self.assertIn(f"--directory {result.runtime}/terminal-{NONCE}", records)
        self.assertIn(f"--nonce {NONCE}", records)
        self.assertIn("--phase live", records)
        self.assertNotIn("closed", records)
        self.assertEqual(result.log.count("records "), 1)
        self.assertIn("uname -s\nuname -m\nuname -r\n", result.log)
        self.assertEqual(result.log.count("gate "), 1)
        self.assertFalse(alive(result.pids["compositor"]))
        self.assertFalse(alive(result.pids["darwin-wayland-client-ssd"]))
        self.assertFalse(alive(result.pids["darwin-wayland-terminal-native-startup"]))

    def test_signal_trap_reaps_both_windows_and_compositor(self):
        result = self.run_script(NONCE, nodes="both", sleep85="hold", signal_during_hold=True)
        self.assertEqual(result.returncode, 143, result.stdout)
        self.assertIn("SERVER_SOCKET", result.stdout)
        self.assertNotIn("STATUS", result.stdout)
        self.assertNotIn("LIVE_RECORDS", result.stdout)
        for name in (
            "compositor",
            "darwin-wayland-client-ssd",
            "darwin-wayland-terminal-native-startup",
        ):
            self.assertFalse(alive(result.pids[name]), name)

    def line(self, log, prefix):
        matches = [line for line in log.splitlines() if line.startswith(prefix)]
        self.assertEqual(len(matches), 1, log)
        return matches[0]

    def run_script(
        self,
        nonce,
        enum_rc=0,
        gate_rc=0,
        nodes="none",
        compositor="stay",
        ssd="release",
        observer="release",
        records_rc=0,
        sleep85="normal",
        signal_during_hold=False,
    ):
        if signal_during_hold:
            ssd = observer = compositor = "stay"
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            bindir = root / "bin"
            nodedir = root / "nodes"
            piddir = root / "pids"
            mode = root / "mode"
            runtime = root / "runtime"
            for path in (bindir, nodedir, piddir, mode, runtime):
                path.mkdir()
            log = root / "log"
            log.write_text("")
            (mode / "enum_rc").write_text(str(enum_rc))
            (mode / "gate_rc").write_text(str(gate_rc))
            (mode / "compositor").write_text(compositor)
            (mode / "darwin-wayland-client-ssd").write_text(ssd)
            (mode / "darwin-wayland-terminal-native-startup").write_text(observer)
            (mode / "records_rc").write_text(str(records_rc))
            (mode / "nodes").write_text(nodes)
            (mode / "sleep85").write_text(sleep85)
            env = {
                "LOG": str(log),
                "MODE": str(mode),
                "PIDDIR": str(piddir),
                "NODEDIR": str(nodedir),
                "PATH": "/usr/bin:/bin",
            }
            self.write_stubs(bindir)
            if nodes == "both":
                (nodedir / "kbd").write_text("")
                (nodedir / "mouse").write_text("")
            elif nodes == "kbd":
                (nodedir / "kbd").write_text("")
            text = SCRIPT.read_text()
            text = text.replace(
                "runtime=/private/var/run/darwin-desktop", f"runtime={runtime}"
            )
            text = text.replace("[ ! -c /dev/usb_hid_kbd ]", f"[ ! -f {nodedir}/kbd ]")
            text = text.replace("[ ! -c /dev/usb_hid_mouse ]", f"[ ! -f {nodedir}/mouse ]")
            for name in (
                "darwin-trackpad-enum",
                "darwin-native-input-inventory",
                "chonkstep-darwin",
                "darwin-wayland-client-ssd",
                "darwin-wayland-terminal-native-startup",
                "darwin-terminal-records",
                "/bin/uname",
            ):
                text = text.replace(f"/usr/bin/{name}" if not name.startswith("/") else name,
                                    str(bindir / Path(name).name))
            scratch = root / "supervisor"
            scratch.write_text(text)
            scratch.chmod(0o755)
            path = f"{bindir}:/usr/bin:/bin"
            proc = subprocess.Popen(
                ["/bin/sh", str(scratch), nonce],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                env={**env, "PATH": path},
            )
            try:
                if signal_during_hold:
                    deadline = 5.0
                    waited = 0.0
                    while waited < deadline and not (mode / "in-hold").exists():
                        if proc.poll() is not None:
                            break
                        waited += 0.05
                        import time
                        time.sleep(0.05)
                    self.assertTrue((mode / "in-hold").exists(), log.read_text())
                    os.kill(proc.pid, signal.SIGTERM)
                stdout, _ = proc.communicate(timeout=15)
            finally:
                if proc.poll() is None:
                    os.kill(proc.pid, signal.SIGTERM)
                    proc.wait(timeout=5)
            result = subprocess.CompletedProcess(proc.args, proc.returncode, stdout, "")
            result.log = log.read_text()
            result.runtime = str(runtime)
            result.sleeps01 = result.log.count("sleep 0.1\n")
            result.pids = {}
            for pidfile in piddir.iterdir():
                result.pids[pidfile.name] = int(pidfile.read_text().strip())
            self._pids = list(result.pids.values())
            return result

    def write_stubs(self, bindir):
        (bindir / "darwin-trackpad-enum").write_text(textwrap.dedent("""\
            #!/bin/sh
            printf 'enum %s\\n' "$*" >> "$LOG"
            rc=$(cat "$MODE/enum_rc")
            exit "$rc"
        """))
        (bindir / "darwin-native-input-inventory").write_text(textwrap.dedent("""\
            #!/bin/sh
            printf 'gate %s\\n' "$*" >> "$LOG"
            rc=$(cat "$MODE/gate_rc")
            exit "$rc"
        """))
        (bindir / "darwin-terminal-records").write_text(textwrap.dedent("""\
            #!/bin/sh
            printf 'records %s\\n' "$*" >> "$LOG"
            rc=$(cat "$MODE/records_rc")
            exit "$rc"
        """))
        (bindir / "uname").write_text(textwrap.dedent("""\
            #!/bin/sh
            printf 'uname %s\\n' "$*" >> "$LOG"
            printf 'stub\\n'
        """))
        (bindir / "chonkstep-darwin").write_text(textwrap.dedent(r"""
            #!/bin/sh
            printf 'compositor %s\n' "$*" >> "$LOG"
            printf '%s\n' "$$" > "$PIDDIR/compositor"
            trap 'exit 0' TERM INT
            behavior=$(cat "$MODE/compositor")
            case "$behavior" in exit1) exit 1 ;; esac
            socket=
            prev=
            for arg in "$@"; do
                if [ "$prev" = --socket ]; then socket=$arg; fi
                prev=$arg
            done
            python3 -c 'import socket,sys; s=socket.socket(socket.AF_UNIX); s.bind(sys.argv[1]); s.listen(1)' "$socket"
            while :; do /bin/sleep 0.2; done
        """))
        client = textwrap.dedent(r"""
            #!/bin/sh
            name=$(basename "$0")
            printf '%s %s\n' "$name" "$*" >> "$LOG"
            printf '%s\n' "$$" > "$PIDDIR/$name"
            trap 'exit 0' TERM INT
            behavior=$(cat "$MODE/$name")
            case "$behavior" in
                exit1) exit 1 ;;
                stay) while :; do /bin/sleep 0.2; done ;;
                release)
                    while [ ! -f "$MODE/export-reached" ]; do /bin/sleep 0.01; done
                    /bin/sleep 2
                    exit 0
                    ;;
            esac
            exit 1
        """)
        for name in ("darwin-wayland-client-ssd", "darwin-wayland-terminal-native-startup"):
            (bindir / name).write_text(client)
        (bindir / "sleep").write_text(textwrap.dedent(r"""
            #!/bin/sh
            printf 'sleep %s\n' "$*" >> "$LOG"
            if [ "$1" = 0.1 ]; then
                seen=$(grep -c '^sleep 0.1$' "$LOG" || true)
                if [ "$(cat "$MODE/nodes")" = after1 ] && [ "$seen" -eq 1 ]; then
                    : > "$NODEDIR/kbd"
                    : > "$NODEDIR/mouse"
                fi
                exit 0
            fi
            if [ "$1" = 85 ]; then
                if [ "$(cat "$MODE/sleep85")" = hold ]; then
                    printf '%s\n' "$$" > "$PIDDIR/sleep"
                    : > "$MODE/in-hold"
                    exec /bin/sleep 30
                fi
                /bin/sleep 0.05
                : > "$MODE/export-reached"
                exit 0
            fi
            printf 'unexpected sleep %s\n' "$*" >> "$LOG"
            exit 1
        """))
        for path in bindir.iterdir():
            path.chmod(0o755)


if __name__ == "__main__":
    unittest.main()
