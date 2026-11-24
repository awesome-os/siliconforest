"""Exercise isolated worker selection and real ProgressCapture without devices."""
import ast
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
LAB = ROOT / 'runs/m1-desktop-010/apcie-port-host-diagnostics-002/lab'
SOURCE = LAB / 'integrations/puredarwin/m1-bringup/host_progress.py'
os.environ['APCIE_TEST_LAB'] = str(LAB)
sys.path.insert(0, str(SOURCE.parent))
import host_progress as host

spec = importlib.util.spec_from_file_location('port_gate_fixtures', HERE.parent/'host/test_gate.py')
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def selection_expression(source):
    tree = ast.parse(source)
    worker, = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'run_worker']
    calls = [n for n in ast.walk(worker) if isinstance(n, ast.Call) and
             isinstance(n.func, ast.Name) and n.func.id == 'ProgressCapture']
    call, = calls
    keyword, = [k for k in call.keywords if k.arg == 'capture_framebuffer']
    # Evaluate the exact production constructor expression, without running
    # worker authorization, target preparation, a proxy or a guest.
    return compile(ast.Expression(keyword.value), '<worker-capture-policy>', 'eval')


class DiagnosticsTests(unittest.TestCase):
    def exercise(self, expression, keyboard, apcie):
        inputs = gate.fixture()
        if not apcie:
            gate.edit(inputs, lambda r: r.pop(gate.FIELD))
            inputs = gate.fixture(record=__import__('json').loads(inputs.contents['build_record']),
                                  names=host.APCIE_NATIVE_MEMBERS)
        enabled = eval(expression, {'keyboard': keyboard, 'inputs': inputs,
                                  'apcie_port_observer_enabled': host.apcie_port_observer_enabled})
        base = host.image.KC_LINK
        ctx = SimpleNamespace(sp=[0, 0, 0], regs=[0]*31)
        fields = dict(base=0x900000000, stride=16, width=4, height=2, depth=30)
        pixels = bytes(range(32))
        iface = SimpleNamespace(readmem=Mock(side_effect=lambda address, size: pixels if size == 32 else bytes(size)))
        backend = SimpleNamespace(iface=iface, p=SimpleNamespace(P_HV_START=1),
            u=SimpleNamespace(mrs=Mock(return_value=0)), display_console={},
            ba=SimpleNamespace(phys_base=0x802078000, mem_size_actual=0x400000000,
                               video=SimpleNamespace(**fields)),
            proxy_module=SimpleNamespace(ExcInfo=SimpleNamespace(parse=lambda _: ctx)))
        diagnostic_inputs = SimpleNamespace(report={'kernel':dict(vmin=base,
            segments=[dict(initprot=3,vmaddr=base,vmsize=0x4000)],
            diagnostic_symbols={f'g{i}':dict(address=base+0x100+i*8,bytes=8) for i in range(5)})})
        with tempfile.TemporaryDirectory() as directory:
            cap = host.ProgressCapture(SimpleNamespace(_in_handler=True), backend, diagnostic_inputs,
                dict(entry=1,phys_base=0x800000000,guest_base=0x801000000,bootargs_off=0),
                Path(directory),120,capture_framebuffer=enabled)
            cap.raw, cap.dispatch_depth = b'HOST ONLY CONTEXT', 1
            with patch.object(host, 'read_carveouts', return_value=[]) as carveouts:
                result = cap.diagnostics()
            self.assertTrue(result['complete'])
            self.assertEqual(backend.u.mrs.call_count, 7)
            self.assertEqual(len(result['globals']), 5)
            expected = not (keyboard or apcie)
            self.assertEqual(result['framebuffer_requested'], expected)
            if expected:
                self.assertEqual(result['framebuffer']['bytes'], 32)
                self.assertEqual((Path(directory)/'framebuffer.bin').read_bytes(), pixels)
                carveouts.assert_called_once()
                self.assertEqual(iface.readmem.call_count, 6)
                self.assertEqual(cap.diagnostic_step, 14)
            else:
                self.assertNotIn('framebuffer', result)
                self.assertFalse((Path(directory)/'framebuffer.bin').exists())
                carveouts.assert_not_called()
                self.assertEqual(iface.readmem.call_count, 5)
                self.assertEqual(cap.diagnostic_step, 12)
            self.assertEqual(len(list(Path(directory).glob('diagnostic-*-completed.json'))), cap.diagnostic_step)

    def test_actual_port_keyboard_and_desktop_routing(self):
        expression = selection_expression(SOURCE.read_text())
        for keyboard in (False, True):
            for apcie in (False, True):
                with self.subTest(keyboard=keyboard,apcie=apcie):
                    self.exercise(expression,keyboard,apcie)

    def test_old_selector_reproduces_port_failure(self):
        old = SOURCE.read_text().replace('not (keyboard or apcie_port_observer_enabled(inputs))', 'not keyboard')
        with self.assertRaises(AssertionError):
            self.exercise(selection_expression(old),False,True)

    def test_always_skip_cannot_remove_desktop_evidence(self):
        bad = SOURCE.read_text().replace('not (keyboard or apcie_port_observer_enabled(inputs))', 'False')
        with self.assertRaises(AssertionError):
            self.exercise(selection_expression(bad),False,False)


if __name__ == '__main__':
    unittest.main(verbosity=2)
