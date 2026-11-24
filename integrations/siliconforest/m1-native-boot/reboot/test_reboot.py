#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host-only checks of the reboot pieces; no hardware.
    python3 integrations/siliconforest/m1-native-boot/reboot/test_reboot.py -v
The PMU/SPMI code against a model of the controller (test/test_pmu_scratch.c), the launcher
patch against the launcher tree, the patched console session against a serial device that
disappears, and the patched controller's recovery rules against scripted bench behaviour."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

HERE = Path(__file__).resolve().parent
NATIVE = HERE.parent
ROOT = HERE.parents[3]
LAB = ROOT/'runs/m1-desktop-010/pcie-m2-host-001/lab'
DIFF = HERE/'launcher-self-reboot.diff'
PROGRESS = 'integrations/siliconforest/m1-bringup/host_progress.py'
CYCLE = 'tools/m1_bringup_cycle.py'
PINNED = ('tools/m1_kernel_cycle.py', 'integrations/siliconforest/m1-firstboot/host_console.py',
          'integrations/siliconforest/m1-bringup/normal_image.py')


def patched_tree(directory, self_reboot=True, words='', with_diff=True):
    """A boot's private launcher tree as tools/nb_boot.sh NB_REBOOT=1 makes it: the patch of
    combine_patch.sh, native_boot.py, native_mode.txt and (self_reboot) self_reboot_mode.txt."""
    Path(directory).mkdir(parents=True, exist_ok=True)
    copy = Path(directory)/'lab'
    shutil.copytree(LAB, copy, ignore=shutil.ignore_patterns('__pycache__'))
    patch = Path(directory)/'launcher.patch'
    if with_diff:
        subprocess.run([str(HERE/'combine_patch.sh'), str(patch)], check=True)
    else:
        patch = NATIVE/'launcher-native.patch'
    subprocess.run(['patch', '-s', '-d', str(copy), '-p1', '-i', str(patch)], check=True)
    bringup = copy/'integrations/siliconforest/m1-bringup'
    for name in ('native_boot.py', 'native_stub.py'):
        shutil.copy(NATIVE/name, bringup/name)
    (bringup/'native_mode.txt').write_text(words + '\n')
    if self_reboot:
        (bringup/'self_reboot_mode.txt').write_text('test\n')
    return copy


def load(copy, name, path):
    for loaded in [m for m in sys.modules if m == 'tools' or m.startswith('tools.') or m in (
            'native_boot', 'normal_image', 'bootargs_progress', 'virtual_input_progress', 'terminal_input_progress')]:
        del sys.modules[loaded]
    sys.path[:] = [p for p in sys.path if 'lab' not in Path(p).parts[-4:] or not p.startswith(tempfile.gettempdir())]
    spec = importlib.util.spec_from_file_location(name, copy/path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


@unittest.skipUnless(shutil.which('cc'), 'no C compiler')
class PmuScratch(unittest.TestCase):
    def test_spmi_and_pmu_bytes_against_the_controller_model(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory)/'pmu_test'
            subprocess.run(['cc', '-Wall', '-Werror', '-I', str(HERE), '-o', str(binary),
                            str(HERE/'test/test_pmu_scratch.c'), str(HERE/'sf_pmu_scratch.c')], check=True)
            done = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertIn('all checks passed', done.stdout)

    def test_kext_sources_name_what_linux_names(self):
        header = (HERE/'sf_pmu_scratch.h').read_text()
        for text in ('0x23d0d9300ULL', 'SF_T8103_PMU_SID        0xf', 'SF_T8103_LEG_SCRPAD     0x9f00'):
            self.assertIn(text, header)
        glue = (HERE/'SFAppleReboot.cpp').read_text()
        # the four MBSE values and the notification key, spelled as Linux spells them
        for a, b, c, d in ('rest', 'offw', 'phra', 'off1', 'MBSE', 'NTAP'):
            self.assertIn(f"SF_SMC_KEY('{a}', '{b}', '{c}', '{d}')", glue)
        # no PMU write without sfpmu=2: both writers sit behind the mode
        self.assertEqual(glue.count('sf_pmu_kernel_started('), 1)
        self.assertEqual(glue.count('sf_pmu_prepare_halt('), 1)
        self.assertIn('if (gPmuMode == 2) {\n\t\tret = sf_pmu_kernel_started(', glue)
        self.assertIn('if (gPmuUsable && gPmuMode == 2) {\n\t\tint ret = sf_pmu_prepare_halt(', glue)
        # and, whatever the glue does, the core has one write command behind one gate
        core = (HERE/'sf_pmu_scratch.c').read_text()
        self.assertEqual(core.count('SPMI_CMD_EXT_WRITEL |'), 1)
        self.assertNotIn('sf_spmi_ext_writel', core + header + glue)
        self.assertIn('gPmu.level = gPmuMode == 2 ? SF_PMU_WRITES_AS_LINUX : SF_PMU_READ_ONLY;', glue)
        self.assertIn('sysctl_register_oid(&sysctl__kern_sf_pmu_writes);', glue)
        self.assertIn('static uint32_t gPmuMode = 1;', glue)
        # power-off is refused before anything is sent unless sfpoweroff=1
        refusal = glue.index('if (off && !gPowerOffAllowed) {')
        self.assertLess(refusal, glue.index('IOLockTryLock(gSmcLock)'))
        self.assertIn('static uint32_t gPowerOffAllowed;', glue)

    def test_cpupower_patch_applies(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ('SFCpuPower.cpp', 'CMakeLists.txt'):
                shutil.copy(ROOT/'integrations/siliconforest/m1-cpufreq/kernel'/name, Path(directory)/name)
            patch = HERE/'cpupower-patches/0001-SFCpuPower-restart-and-power-off-through-the-SMC.patch'
            subprocess.run(['patch', '-s', '-d', directory, '-p1', '-i', str(patch)], check=True)
            text = (Path(directory)/'SFCpuPower.cpp').read_text()
            thread = text[text.index('sf_thermal_thread(void *, wait_result_t)'):text.index('/* ---- sysctl')]
            # every pass of the sensor loop takes the session once and gives it back once;
            # the thread that parks for shutdown gives it back first
            self.assertEqual(thread.count('SFAppleRebootSmcBegin(smc);'), 1)
            self.assertEqual(thread.count('SFAppleRebootSmcEnd();'), 2)
            begin = thread.index('SFAppleRebootSmcBegin(smc);')
            self.assertNotIn('continue;\n\t\t}', thread[begin:].replace('\t\t\t\t\tcontinue;', ''))


@unittest.skipUnless(LAB.is_dir() and shutil.which('patch'), 'no launcher tree here')
class LauncherPatch(unittest.TestCase):
    def test_patch_applies_and_leaves_the_pinned_files_alone(self):
        with tempfile.TemporaryDirectory() as directory:
            copy = patched_tree(directory)
            for name in (PROGRESS, CYCLE):
                compile((copy/name).read_text(), name, 'exec')
            for name in PINNED:
                self.assertEqual((copy/name).read_bytes(), (LAB/name).read_bytes())

    def test_it_is_launcher_native_with_the_diff_on_top(self):
        with tempfile.TemporaryDirectory() as directory:
            ours = patched_tree(Path(directory)/'ours')
            base = patched_tree(Path(directory)/'base', with_diff=False)
            for name in (PROGRESS, CYCLE):                      # a superset, in order
                theirs, position = (base/name).read_text().splitlines(), 0
                mine = (ours/name).read_text().splitlines()
                changed = [line for line in theirs if line not in mine]
                self.assertLessEqual(len(changed), 2, changed)  # the two lines the diff rewrites
                for line in theirs:
                    if line not in changed:
                        position = mine.index(line, position) + 1
            subprocess.run(['patch', '-s', '-d', str(base), '-p1', '-i', str(DIFF)], check=True)
            for name in (PROGRESS, CYCLE):
                self.assertEqual((base/name).read_bytes(), (ours/name).read_bytes())

    def test_the_shared_mode_parser_is_not_involved(self):
        # the request travels in a file of its own; native_mode.txt has no word for it
        self.assertNotIn('self-reboot', (NATIVE/'native_boot.py').read_text())
        self.assertNotIn('native_mode.txt', DIFF.read_text())


class Serial:
    """The hypervisor's console endpoint: a prompt, the challenge's answer, the guest
    script's output, and then the device is gone (pyserial raises SerialException, an
    OSError, when the USB device disappears)."""
    def __init__(self, announce, vanish=True, *args, **kwargs):
        self.announce, self.pending, self.sent, self.closed = announce, [b'\r\nsh-3.2# '], [], False
        self.nonce, self.vanish = None, vanish

    def read(self, size):
        if self.pending:
            return self.pending.pop(0)
        if self.nonce == 'gone' and self.vanish:
            raise OSError(6, 'Device not configured')
        time.sleep(.01)
        return b''

    def write(self, data):
        self.sent.append(bytes(data))
        if b'DL9_BEGIN' in data:
            parts = data.split(b"'")
            nonce = parts[3] + parts[5]
            self.pending.append(b'\r\nDL9_BEGIN_' + nonce + b'\r\nDarwin\r\narm64\r\n25.0.0\r\nDL9_END_' + nonce + b'\r\nsh-3.2# ')
        else:
            self.pending += [b'\r\nPDNB_GUEST_BEGIN\r\n', b'uname and the rest of the guest script ' * 4 + b'\r\n']
            if self.announce:
                self.pending.append(b'\r\nPD_BENCH_REBOOT\r\n')
            self.pending.append(b'SFReboot: issuing restart (phra)\r\n')
            self.nonce = 'gone'
        return len(data)

    def close(self):
        self.closed = True
        if self.vanish:
            raise OSError(6, 'Device not configured')


@unittest.skipUnless(LAB.is_dir() and shutil.which('patch'), 'no launcher tree here')
class ConsoleGone(unittest.TestCase):
    def session(self, self_reboot, announce):
        with tempfile.TemporaryDirectory() as directory:
            copy = patched_tree(directory, self_reboot=self_reboot)
            module = load(copy, 'reboot_test_progress', PROGRESS)
            module.SETTLE = .05
            out = Path(directory)/'guest'
            out.mkdir()
            console = module.ConsoleSession('/dev/cu.usbmodemTEST3', out, desktop=True,
                                            serial_factory=lambda *a, **k: Serial(announce))
            console.thread.join(10)
            self.assertFalse(console.thread.is_alive())
            try:
                console.close()
                closed = None
            except Exception as error:
                closed = error
            result = json.loads((out/'console-result.json').read_text()) if (out/'console-result.json').exists() else None
            return console, closed, result

    def test_announced_restart_ends_the_console_without_an_error(self):
        console, closed, result = self.session(True, True)
        self.assertIsNone(closed)
        self.assertTrue(console.reboot_seen and console.gone and console.error is None)
        self.assertFalse(console.done_seen)             # nothing pulled the capture's alarm forward
        self.assertTrue(result['command_line_observed'] and result['reboot_announced'])
        self.assertIsNone(result['error'])
        self.assertIn('Device not configured', result['console_gone_after_reboot'])

    def test_a_vanished_console_is_an_error_without_the_announcement(self):
        console, closed, result = self.session(True, False)
        self.assertIsNotNone(console.error)
        self.assertIsNotNone(closed)                    # the device's close failure is not swallowed

    def test_a_vanished_console_is_an_error_without_the_mode_file(self):
        console, closed, result = self.session(False, True)
        self.assertFalse(console.reboot_seen)
        self.assertIsNotNone(console.error)

    def test_uart_console_ends_some_seconds_after_the_announcement(self):
        with tempfile.TemporaryDirectory() as directory:
            copy = patched_tree(directory)
            module = load(copy, 'reboot_test_progress', PROGRESS)
            module.SETTLE, module.REBOOT_LINGER = .05, .4
            out = Path(directory)/'guest'
            out.mkdir()
            console = module.ConsoleSession(module.native_boot.UART_DEVICE, out, desktop=True,
                                            serial_factory=lambda *a, **k: Serial(True, vanish=False))
            deadline = time.monotonic() + 10
            while not console.reboot_seen and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertTrue(console.reboot_seen)
            self.assertFalse(console.done_seen)         # the kernel's last lines are still wanted
            time.sleep(.7)
            self.assertTrue(console.done_seen)          # what native_boot.start() ends its capture on
            console.close()
            result = json.loads((out/'console-result.json').read_text())
            self.assertTrue(result['command_line_observed'] and result['reboot_announced'])
            self.assertIsNone(result['console_gone_after_reboot'])
            self.assertIn(b'SFReboot: issuing restart (phra)', (out/'console.bin').read_bytes())


def linux(boot):
    return dict(kernel=None, loader_sha256=None, compatible=['apple,j293', 'apple,t8103'], boot_id=boot,
                mounts={m: dict(type='vfat' if m == '/boot/efi' else 'ext4', options=['rw']) for m in ('/', '/boot', '/boot/efi')},
                watchdogs=dict(RuntimeWatchdogUSec='30s', RebootWatchdogUSec='2min'), units=None)


class Bench:
    """The controller's Ops, scripted. Time advances only through sleep() and through the
    cost of a failed SSH (3 s, its connect timeout)."""
    def __init__(self, module, directory, linux_back_at, proxy_at, announce=True, native=False,
                 worker='HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT', worker_seconds=60):
        self.worker, self.worker_seconds = worker, worker_seconds
        self.module, self.base = module, module.base
        self.cycle = Path(directory)/'cycle'
        self.now, self.end = 0.0, None
        self.old, self.new = str(uuid.uuid4()), str(uuid.uuid4())
        self.linux_back_at, self.proxy_at = linux_back_at, proxy_at
        self.announce, self.native = announce, native
        self.resets, self.saved, self.refreshes, self.rebooted = [], [], 0, False
        self.records, self.wrong_linux = {}, False
        self.args = type('Args', (), dict(launcher_sha256='0' * 64, inspector_sha256='0' * 64))()
        self.clock, self.sleep = (lambda: self.now), self.advance

    def advance(self, seconds):
        self.now += seconds

    def start_cycle(self, number):
        self.cycle.mkdir()

    def save(self, name, data):
        self.assert_new(name)
        self.saved.append(name)
        self.records[name] = data
        (self.cycle/name).write_text(json.dumps(data, default=str) if not isinstance(data, bytes) else '')

    def assert_new(self, name):
        if name in self.saved:
            raise FileExistsError(name)             # the real records are exclusive creations

    def arm(self): self.armed = True
    def clear(self): self.armed = False
    def reboot(self): self.rebooted = True

    def value(self, boot):
        value = linux(boot)
        value.update(kernel=self.base.LINUX, loader_sha256=self.base.LOADER_SHA,
                     units={unit: 'inactive' for unit in self.base.UNITS})
        return value

    def observe(self):
        if not self.rebooted:
            return self.value(self.old)
        since = None if self.end is None else self.now - self.end
        back = self.linux_back_at(self, since) if since is not None else False
        if not back:
            self.now += 3
            raise ConnectionError('Known-host Linux SSH unavailable')
        value = self.value(self.new)
        if self.wrong_linux:
            value['kernel'] = 'another kernel'      # what check_linux refuses
        return value

    def discover(self):
        if self.end is None:                        # acquisition: bootstrap window, then the launcher's port
            self.acquired = getattr(self, 'acquired', 0) + 1
            return dict(callout='/dev/cu.usbmodemA', IORegistryEntryID=10 + self.acquired, sessionID=100 + self.acquired)
        return self.proxy_at(self, self.now - self.end)

    def child(self, phase, port):
        directory = self.cycle/phase
        directory.mkdir()
        if phase == 'bootstrap':
            (directory/'vector-ack.json').write_text(json.dumps(dict(
                status='VECTOR_ACKNOWLEDGED_RUNTIME_UNVERIFIED', runtime_verified=False)))
            return directory
        (directory/'host-result.json').write_text(json.dumps(dict(status=self.worker, worker_returncode=1)))
        (directory/'console.bin').write_bytes(b'sh-3.2# script\r\n' + (b'\r\nPD_BENCH_REBOOT\r\n' if self.announce else b'') +
                                              b'MACH Reboot\r\n')
        if self.native:
            (directory/'native-start.json').write_text(json.dumps(dict(hypervisor=False)))
        self.now += self.worker_seconds
        self.end = self.now
        return directory

    def reset(self):
        self.resets.append(self.now - self.end)
        self.reset_at = self.now

    def refresh_neighbor(self):
        self.refreshes += 1


WINDOW = dict(callout='/dev/cu.usbmodemW', IORegistryEntryID=77, sessionID=777)


@unittest.skipUnless(LAB.is_dir() and shutil.which('patch'), 'no launcher tree here')
class ControllerRules(unittest.TestCase):
    """What the patched run_attempt does after the guest worker has ended."""
    def run_attempt(self, words, linux_back_at, proxy_at, self_reboot=True, certified=True, **kwargs):
        with tempfile.TemporaryDirectory() as directory:
            copy = patched_tree(directory, self_reboot=self_reboot, words=words)
            module = load(copy, 'reboot_test_cycle', CYCLE)
            if certified:
                module.console_observation = lambda guest, candidate: True
            bench = Bench(module, directory, linux_back_at, proxy_at, **kwargs)
            result = module.run_attempt(bench, {}, {})
            return bench, result

    @staticmethod
    def own_reboot(seconds, window=(8, 11)):
        """Linux answers `seconds` after the capture ended; the installed m1n1 shows its
        proxy window for a few seconds on the way."""
        return (lambda bench, since: since >= seconds,
                lambda bench, since: WINDOW if window[0] <= since < window[1] else None)

    def test_hypervisor_self_reboot_ends_with_no_reset(self):
        bench, result = self.run_attempt('', *self.own_reboot(33))
        self.assertEqual(result['status'], 'DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED', result)
        self.assertEqual((result['reset_attempts'], bench.resets), (0, []))
        self.assertTrue(bench.records['self-reboot-return.json']['linux_returned_by_itself'])
        self.assertNotIn('reset-intent.json', bench.saved)
        self.assertFalse(bench.armed)

    def test_a_restart_the_capture_could_not_certify_arms_no_latch(self):
        # Under the hypervisor the proxy vanishes inside the capture: the worker ends with a
        # traceback (or is killed at its timeout) and capture-core.json may never be written.
        # The controller's own console_observation (not replaced here) then says no; that
        # costs the verdict, never the latch, and never a reset.
        for worker, seconds, back in (('HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT', 60, 33),
                                      ('HOST_WORKER_TIMEOUT', 240, 0)):      # Linux long back when the worker is killed
            bench, result = self.run_attempt('', *self.own_reboot(back), certified=False,
                                             worker=worker, worker_seconds=seconds)
            self.assertEqual(result['status'], 'NO_COMMAND_LINE_OBSERVED_LINUX_VERIFIED', result)
            self.assertEqual((result['reset_attempts'], bench.resets), (0, []), worker)
            self.assertTrue(result['linux_verified'])
            self.assertFalse(bench.armed)
            self.assertNotIn('reset-intent.json', bench.saved)

    def test_the_proxy_window_is_not_reset_whenever_it_is_seen(self):
        for start in (0, 2, 5, 8):                  # the controller's one look falls into the window
            bench, result = self.run_attempt('', *self.own_reboot(33, window=(start, start + 6)))
            self.assertEqual((result['status'], bench.resets), ('DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED', []), start)

    def test_native_self_reboot_ends_with_no_reset(self):
        for seconds in (20, 33, 47, 80):            # 47 s: longer than the native case's own 45 s wait
            bench, result = self.run_attempt('native vdm-return vdm-serial', *self.own_reboot(seconds), native=True)
            self.assertEqual((result['status'], bench.resets), ('DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED', []), seconds)

    def test_one_neighbour_refresh_and_no_second_record_of_the_same_name(self):
        bench, result = self.run_attempt('', *self.own_reboot(33))
        self.assertEqual(bench.refreshes, 1)
        self.assertIn('self-reboot-neighbor-refresh-intent.json', bench.saved)
        # Linux late: the wait runs out, the base controller's own wait and refresh follow
        bench, result = self.run_attempt('', *self.own_reboot(130))
        self.assertEqual((result['status'], bench.resets), ('DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED', []))
        self.assertEqual(bench.refreshes, 2)
        self.assertIn('neighbor-refresh-intent.json', bench.saved)

    def test_restart_that_did_not_happen_under_the_hypervisor_is_reset_once_at_once(self):
        def proxy(bench, since):                    # the hypervisor's own device, still there
            return bench.records['guest-port.json']
        bench, result = self.run_attempt('', lambda bench, since: hasattr(bench, 'reset_at') and
                                         bench.now - bench.reset_at >= 33, proxy)
        self.assertEqual(result['status'], 'DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED', result)
        self.assertEqual(len(bench.resets), 1)
        self.assertLess(bench.resets[0], 5)
        self.assertNotIn('self-reboot-return.json', bench.saved)

    def test_machine_stuck_in_the_proxy_after_its_own_restart_is_reset_once(self):
        bench, result = self.run_attempt('', lambda bench, since: hasattr(bench, 'reset_at') and
                                         bench.now - bench.reset_at >= 33, lambda bench, since: WINDOW)
        self.assertEqual(result['status'], 'DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED', result)
        self.assertEqual(len(bench.resets), 1)
        self.assertGreaterEqual(bench.resets[0], 90)

    def test_native_kernel_that_did_not_restart_is_reset_once_after_both_waits(self):
        bench, result = self.run_attempt('native vdm-return', lambda bench, since: hasattr(bench, 'reset_at') and
                                         bench.now - bench.reset_at >= 33, lambda bench, since: None, native=True)
        self.assertEqual(result['status'], 'DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED', result)
        self.assertEqual(len(bench.resets), 1)
        self.assertGreaterEqual(bench.resets[0], 135)

    def test_without_the_announcement_or_the_mode_file_nothing_changes(self):
        for asked, announce in ((True, False), (False, True)):
            bench, result = self.run_attempt('', lambda bench, since: hasattr(bench, 'reset_at') and
                                             bench.now - bench.reset_at >= 33, lambda bench, since: WINDOW,
                                             self_reboot=asked, announce=announce)
            self.assertEqual(len(bench.resets), 1, asked)
            self.assertLess(bench.resets[0], 5)
            self.assertNotIn('self-reboot-return.json', bench.saved)

    def test_a_linux_that_is_not_ours_stops_the_cycle(self):
        def back(bench, since):
            bench.wrong_linux = True
            return since >= 20                      # found during the wait for the M1's own Linux
        bench, result = self.run_attempt('', back, lambda bench, since: None)
        self.assertEqual(result['status'], 'RECOVERY_INCOMPLETE_NO_RETRY')
        self.assertIn('Linux kernel or installed candidate changed', result['recovery_error'])
        self.assertEqual(bench.resets, [])
        self.assertTrue(bench.armed)


if __name__ == '__main__':
    unittest.main()
