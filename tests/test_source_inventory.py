import unittest
from tools.source_inventory import SourceError, mask_noncode, expand_use, inventory

class MaskTests(unittest.TestCase):
    def test_comments_preserve_lines(self):
        s='one /* kernel::fake\n nested /* x */ */ two // gone\nthree'
        masked=mask_noncode(s)
        self.assertEqual(len(s),len(masked)); self.assertEqual(s.count('\n'),masked.count('\n'))
        self.assertNotIn('fake',masked); self.assertIn('two',masked)
    def test_strings_raw_and_escaped(self):
        s='r##"kernel::fake /* */"##; "kernel::no\\\"pe"; kernel::real;'
        self.assertNotIn('fake',mask_noncode(s)); self.assertNotIn('no',mask_noncode(s))
        self.assertIn('kernel::real',mask_noncode(s))
    def test_lifetimes_and_characters(self):
        s="fn a<'a>(x: &'a str) { '\\n'; 'x'; }"
        masked=mask_noncode(s); self.assertIn("<'a>",masked); self.assertNotIn("'x'",masked)
    def test_unterminated_rejected(self):
        for s in ['/*','"oops','r#"oops','/* /* */']:
            with self.subTest(s=s), self.assertRaises(SourceError): mask_noncode(s)

class ImportTests(unittest.TestCase):
    def test_nested_tree(self):
        result=[p for p,_ in expand_use('kernel::{drm::{self,gem::shmem::Object},sync::{Arc,aref::ARef},prelude::*}')]
        self.assertEqual(result,['kernel::drm','kernel::drm::gem::shmem::Object','kernel::sync::Arc','kernel::sync::aref::ARef','kernel::prelude::*'])
    def test_alias_and_absolute(self):
        self.assertEqual([p for p,_ in expand_use('::kernel::sync::Arc as KArc')],['kernel::sync::Arc'])
    def test_bad_import_fails(self):
        for body in ['kernel::{sync::Arc','kernel::{A B}','kernel::foo + x','kernel::{A}, junk']:
            with self.subTest(body=body), self.assertRaises(SourceError): expand_use(body)
    def test_real_style_with_comments(self):
        s='use kernel::{\n drm::{self, gem::{shmem::{self,VMapOwned, // why\n},BaseObject,}}, sync::Mutex,};'
        symbols=inventory(s,'test')['unique_symbols']
        self.assertIn('kernel::drm::gem::shmem::VMapOwned',symbols)
        self.assertIn('kernel::sync::Mutex',symbols)

class InventoryTests(unittest.TestCase):
    def test_direct_references_and_offset(self):
        r=inventory('use kernel::sync::Arc;\nlet x = kernel::bindings::foo;','a.rs',40,'excerpt')
        self.assertEqual(r['references'][0]['line'],41)
        self.assertEqual(r['references'][1]['line'],42)
        self.assertEqual(r['references'][1]['category'],'generated-linux-bindings')
    def test_ignores_other_roots_and_literals(self):
        r=inventory('use std::io; // kernel::false\nconst X: &str="kernel::false";','x')
        self.assertEqual(r['reference_count'],0)
    def test_license_recorded_without_changing_it(self):
        r=inventory('// SPDX-License-Identifier: GPL-2.0-only OR MIT\nuse kernel::bindings;','x')
        self.assertEqual(r['license_declaration'],'GPL-2.0-only OR MIT')
    def test_import_not_double_counted(self):
        self.assertEqual(inventory('use kernel::bindings;','x')['reference_count'],1)
    def test_invalid_scope_and_offset(self):
        with self.assertRaises(SourceError): inventory('','x',-1)
        with self.assertRaises(SourceError): inventory('','x',scope='proven')
    def test_macro_reference(self):
        self.assertEqual(inventory('kernel::declare_drm_ioctls! {}','x')['unique_symbols'],['kernel::declare_drm_ioctls'])
