"""Synthetic IR tests; actual compiler runs use tools/cross_abi.py separately."""
import copy
import struct
import unittest
from tools.cross_abi import generate, parse_ir, schema, object_identity
from tools.source_inventory import SourceError

SPEC={'header':'example.h','abi_tag':'test','types':[{'name':'test','c_type':'struct test','fields':['a','b']}],'constants':['VALUE']}
def ir(values='i64 16, i64 8, i64 0, i64 8, i64 12',count=5):
    return ('target triple = "arm64-apple-macosx11.0.0"\n'
            'target datalayout = "e-m:o-i64:64"\n'
            f'@dl_abi_values = constant [{count} x i64] [{values}], align 8\n')
def macho(**updates):
    d=dict(magic=0xfeedfacf,cpu=0x100000c,subtype=0,kind=1,ncmds=0,command_bytes=0,flags=0,reserved=0)
    d.update(updates);return struct.pack('<8I',*d.values())
def elf(machine=183,kind=1):
    x=bytearray(64);x[:7]=b'\x7fELF\x02\x01\x01';struct.pack_into('<HH',x,16,kind,machine);return bytes(x)

class ProbeTests(unittest.TestCase):
    def test_generate(self):
        code=generate(SPEC);self.assertIn('offsetof(struct test, b)',code)
        self.assertNotIn('main(',code);self.assertNotIn('stdio',code)
    def test_typedef(self):
        s=copy.deepcopy(SPEC);del s['types'][0]['c_type']
        self.assertIn('sizeof(test)',generate(s))
    def test_injection(self):
        for key,value in [('header','x.h"\n#error BAD'),('abi_tag','"')]:
            s=copy.deepcopy(SPEC);s[key]=value
            with self.subTest(key=key),self.assertRaises(SourceError):generate(s)
    def test_bad_path(self):
        for path in ['/x.h','../x.h','a/./x.h','a//x.h','a/../x.h']:
            s=copy.deepcopy(SPEC);s['header']=path
            with self.subTest(path=path),self.assertRaises(SourceError):generate(s)
    def test_bad_type(self):
        for value in ['void *','struct X;','test[4]','unsigned long','struct  x']:
            s=copy.deepcopy(SPEC);s['types'][0]['c_type']=value
            with self.subTest(value=value),self.assertRaises(SourceError):generate(s)
    def test_duplicate_type(self):
        s=copy.deepcopy(SPEC);s['types']*=2
        with self.assertRaises(SourceError):generate(s)
    def test_duplicate_field(self):
        s=copy.deepcopy(SPEC);s['types'][0]['fields']=['a','a']
        with self.assertRaises(SourceError):generate(s)
    def test_bad_field(self):
        s=copy.deepcopy(SPEC);s['types'][0]['fields']=['a);system("bad");']
        with self.assertRaises(SourceError):generate(s)
    def test_empty_types(self):
        s=copy.deepcopy(SPEC);s['types']=[]
        with self.assertRaises(SourceError):generate(s)
    def test_duplicate_constant(self):
        s=copy.deepcopy(SPEC);s['constants']*=2
        with self.assertRaises(SourceError):generate(s)
    def test_parse(self):
        r=parse_ir(ir(),SPEC);self.assertEqual(r['structs']['test'],{'size':16,'align':8,'offsets':{'a':0,'b':8}})
        self.assertFalse(r['target_code_executed']);self.assertEqual(r['constants']['VALUE'],12)
    def test_negative_u64(self):
        self.assertEqual(parse_ir(ir('i64 16, i64 8, i64 0, i64 8, i64 -1'),SPEC)['constants']['VALUE'],2**64-1)
    def test_missing_target(self):
        with self.assertRaises(SourceError):parse_ir('@dl_abi_values = constant [5 x i64] []',SPEC)
    def test_duplicate_target(self):
        with self.assertRaises(SourceError):parse_ir(ir()+'target triple = "x"\n',SPEC)
    def test_duplicate_array(self):
        with self.assertRaises(SourceError):parse_ir(ir()+ir().splitlines()[-1]+'\n',SPEC)
    def test_missing_array(self):
        with self.assertRaises(SourceError):parse_ir(ir().replace('@dl_abi_values','@other'),SPEC)
    def test_count(self):
        with self.assertRaises(SourceError):parse_ir(ir(count=6),SPEC)
    def test_truncated_initializer(self):
        with self.assertRaises(SourceError):parse_ir(ir('i64 1'),SPEC)
    def test_nonliteral(self):
        for v in ['ptrtoint (ptr @bad to i64)','i64 undef','i32 8']:
            with self.subTest(v=v),self.assertRaises(SourceError):parse_ir(ir('i64 16, i64 8, i64 0, i64 8, '+v),SPEC)
    def test_overflow(self):
        with self.assertRaises(SourceError):parse_ir(ir(f'i64 16, i64 8, i64 0, i64 8, i64 {2**64}'),SPEC)
    def test_bad_offset(self):
        with self.assertRaises(SourceError):parse_ir(ir('i64 16, i64 8, i64 0, i64 16, i64 0'),SPEC)
    def test_zero_size(self):
        with self.assertRaises(SourceError):parse_ir(ir('i64 0, i64 8, i64 0, i64 8, i64 0'),SPEC)
    def test_macho(self):
        r=object_identity(macho(),'darwin-arm64');self.assertEqual(r['format'],'MACH_O_64');self.assertFalse(r['kernel_image'])
    def test_macho_wrong_arch(self):
        with self.assertRaises(SourceError):object_identity(macho(cpu=0x1000007),'darwin-arm64')
    def test_macho_executable_rejected(self):
        with self.assertRaises(SourceError):object_identity(macho(kind=2),'darwin-arm64')
    def test_macho_truncated(self):
        with self.assertRaises(SourceError):object_identity(macho()[:-1],'darwin-arm64')
    def test_macho_command_bounds(self):
        with self.assertRaises(SourceError):object_identity(macho(command_bytes=8),'darwin-arm64')
    def test_macho_command_count(self):
        with self.assertRaises(SourceError):object_identity(macho(ncmds=1),'darwin-arm64')
    def test_elf(self):
        self.assertEqual(object_identity(elf(),'linux-aarch64')['format'],'ELF64')
    def test_elf_wrong_arch(self):
        with self.assertRaises(SourceError):object_identity(elf(machine=62),'linux-aarch64')
    def test_elf_wrong_kind(self):
        with self.assertRaises(SourceError):object_identity(elf(kind=2),'linux-aarch64')
    def test_elf_truncated(self):
        with self.assertRaises(SourceError):object_identity(elf()[:30],'linux-aarch64')
    def test_unknown_profile(self):
        with self.assertRaises(SourceError):object_identity(elf(),'nope')
if __name__=='__main__':unittest.main()
