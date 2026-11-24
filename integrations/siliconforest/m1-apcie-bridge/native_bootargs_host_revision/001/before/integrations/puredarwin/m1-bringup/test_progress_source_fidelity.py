#!/usr/bin/env python3
"""Host-only fidelity of continuing bootstrap/backend changes against frozen sources."""
import ast
import importlib.util
from pathlib import Path
import sys
import unittest
HERE=Path(__file__).resolve().parent
LAB=HERE.parents[2]
sys.path.insert(0,str(HERE))
import host_progress as host

class UnqualifyFrozen(ast.NodeTransformer):
    def visit_Attribute(self,node):
        node=self.generic_visit(node)
        if isinstance(node.value,ast.Name) and node.value.id=='old':
            return ast.copy_location(ast.Name(id=node.attr,ctx=node.ctx),node)
        return node

class ProgressFidelity(unittest.TestCase):
    def test_backend_collect_preserves_every_check(self):
        frozen=ast.parse((LAB/'integrations/puredarwin/m1-firstboot/host_console.py').read_text())
        current=ast.parse((HERE/'host_progress.py').read_text())
        def method(tree,cls):
            c=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name==cls)
            return next(n for n in c.body if isinstance(n,ast.FunctionDef) and n.name=='collect')
        before=method(frozen,'PinnedBackend')
        after=UnqualifyFrozen().visit(method(current,'ContinuingBackend'))
        self.assertEqual(ast.dump(before,include_attributes=False),ast.dump(after,include_attributes=False))
        self.assertIs(host.ContinuingBackend.__bases__[0],host.old.PinnedBackend)
        self.assertIs(host.ContinuingBackend.open,host.old.PinnedBackend.open)

    def test_bootstrap_copy_contains_only_declared_changes(self):
        before=(LAB/'integrations/m1-boot-window/bootstrap.py').read_text()
        expected=before.replace('LAB = HERE.parents[1]','LAB = HERE.parents[2]\nsys.path.insert(0, str(HERE))\nfrom bootargs_progress import validate_bootargs, physical_to_virtual')
        expected=expected.replace('dtaddr = f.validate_bootargs(ba, address, fmt.sizeof())','dtaddr = validate_bootargs(ba, address, fmt.sizeof())')
        expected=expected.replace('tba.devtree = integer(layout["adt"] - ba.phys_base + ba.virt_base)','tba.devtree = physical_to_virtual(ba, layout["adt"], len(adt))')
        expected=expected.replace('stub_path = HERE / \"bootstrap-trampoline.bin\"','stub_path = LAB / \"integrations/m1-boot-window/bootstrap-trampoline.bin\"')
        self.assertEqual((HERE/'bootstrap_progress.py').read_text(),expected)

if __name__=='__main__': unittest.main()
