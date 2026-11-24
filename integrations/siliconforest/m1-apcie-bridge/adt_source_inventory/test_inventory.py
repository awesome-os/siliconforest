"""Synthetic pure-host inventory tests; no captured device values printed."""
import builtins
from dataclasses import FrozenInstanceError
import hashlib
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
import inventory as inv

def prop(key,value):
    return key.encode().ljust(32,b'\0')+struct.pack('<I',len(value))+value+b'\0'*((-len(value))&3)
def node(name,properties=(),children=()):
    p=(prop('name',name.encode()+b'\0'),)+tuple(properties)
    return struct.pack('<II',len(p),len(children))+b''.join(p)+b''.join(children)
def pmap(count=1):
    return b''.join(struct.pack('<QQII',0x600000000+i*0x4000,0x4000,0xdead0000+i,0x11223344+i) for i in range(count))
def rows(count=2):
    return b''.join(struct.pack('<QQQII',0x800100000+i*0x4000,(1<<64)-1 if i else 0,0 if i==0 else 0x800200000+i*0x4000,0x4000,0xf0000000+i) for i in range(count))
def device(name='dev',ranges=None,names=None,extra=()):
    ranges=rows() if ranges is None else ranges
    names=b'__TEXT;__DATA\0' if names is None else names
    return node(name,(prop('segment-ranges',ranges),prop('segment-names',names),*extra))
def fixture(*,pmap_value=None,devices=None,defaults_extra=(),root_children=(),chosen_extra=(),root_properties=None,vram_properties=None,dram_properties=None):
    root_properties=(prop('#address-cells',struct.pack('<I',2)),prop('#size-cells',struct.pack('<I',2))) if root_properties is None else root_properties
    dram_properties=(prop('dram-base',struct.pack('<Q',0x800000000)),prop('dram-size',struct.pack('<Q',0x400000000))) if dram_properties is None else dram_properties
    vram_properties=(prop('device_type',b'vram\0'),prop('reg',struct.pack('<QQ',0x900000000,0x200000))) if vram_properties is None else vram_properties
    return node('device-tree',root_properties,children=(node('chosen',(*dram_properties,*chosen_extra)),node('defaults',(prop('pmap-io-ranges',pmap() if pmap_value is None else pmap_value),*defaults_extra)),node('arm-io',children=(device(),) if devices is None else devices),node('vram',vram_properties),*root_children))

class InventoryTests(unittest.TestCase):
    def valid(self,raw):
        try:return inv.extract(raw,profile=inv.PROFILE)
        except inv.Refused as error:self.fail('known-valid synthetic source refused: '+str(error))
    def bad(self,raw):
        with self.assertRaises(inv.Refused):inv.extract(raw,profile=inv.PROFILE)
    def test_exact_layout_raw_properties_fields_and_pure_outputs(self):
        raw=fixture();before=bytes(raw);out=self.valid(raw)
        self.assertEqual(raw,before);self.assertEqual(out.base_adt_sha256,hashlib.sha256(raw).hexdigest())
        self.assertEqual((len(out.pmap),len(out.segments),len(out.physical),len(out.remap)),(1,2,2,2))
        self.assertEqual(out.pmap[0].raw,pmap());self.assertEqual((out.pmap[0].wimg,out.pmap[0].signature),(0xdead0000,0x11223344))
        self.assertEqual(out.segments[1].raw,rows()[32:64]);self.assertEqual(out.segments[1].opaque,0xf0000001)
        self.assertEqual(out.segments[1].iova_opaque,(1<<64)-1)
        self.assertEqual(out.segments[0].iova_opaque,0)
        self.assertEqual(out.physical[0].span,inv.Span(out.segments[0].phys,out.segments[0].bytes))
        self.assertEqual(out.remap[0].span,inv.Span(0,0x4000))
        self.assertEqual(out.remap[0].status,'ZERO_NUMERICAL_VALUE_NOT_ABSENCE')
        self.assertEqual(out.metadata()['remap_nonzero_count'],1)
        self.assertEqual(out.metadata()['projected_source_records'],6)
        self.assertEqual(out.dram,inv.Span(0x800000000,0x400000000))
        self.assertEqual(out.vram,inv.Span(0x900000000,0x200000))
        self.assertTrue({('', '#address-cells'),('', '#size-cells'),('/chosen','dram-base'),('/chosen','dram-size'),('/vram','reg'),('/vram','device_type')}.issubset({(p.path,p.key) for p in out.properties}))
        self.assertFalse(out.metadata()['source_completeness_or_policy_authority'])
        self.assertFalse(out.metadata()['physical_access_performed'])
        for p in out.properties:
            self.assertEqual(p.value_sha256,hashlib.sha256(p.value).hexdigest())
            self.assertEqual(p.serialized_sha256,hashlib.sha256(p.serialized).hexdigest())
            self.assertEqual(p.serialized,prop(p.key,p.value))
        with self.assertRaises(FrozenInstanceError):out.profile='changed'
        with self.assertRaises(FrozenInstanceError):out.segments[0].phys=0
    def test_preserve_duplicate_labels_rows_zero_and_opaque_IOVA(self):
        identical=struct.pack('<QQQII',0x800100000,(1<<64)-1,0,0x4000,0xffffffff)
        raw=fixture(devices=(device(ranges=identical*2,names=b'SAME;SAME\0'),))
        out=self.valid(raw)
        self.assertEqual(len(out.segments),2);self.assertEqual(len(out.remap),2)
        self.assertEqual(out.segments[0].name,out.segments[1].name)
        self.assertEqual(out.segments[0].raw,out.segments[1].raw)
        self.assertEqual([s.slot for s in out.segments],[0,1]);self.assertEqual([s.ordinal for s in out.remap],[0,1])
        self.assertEqual(out.metadata()['zero_remap_retained_count'],2)
    def test_deterministic_path_order_and_no_name_normalization(self):
        a=device('a',names=b'lower;UPPER\0');b=device('b',names=b'lower;UPPER\0')
        first=self.valid(fixture(devices=(b,a)));second=self.valid(fixture(devices=(a,b)))
        self.assertEqual(first.segments,second.segments);self.assertEqual(first.physical,second.physical)
        self.assertEqual([s.name for s in first.segments],['lower','UPPER','lower','UPPER'])
        self.assertEqual([s.path for s in first.segments],['/arm-io/a','/arm-io/a','/arm-io/b','/arm-io/b'])
    def test_exact_capacity_all_remaps_count_including_zero(self):
        self.assertEqual(self.valid(fixture(pmap_value=pmap(251))).metadata()['projected_source_records'],256)
        self.bad(fixture(pmap_value=pmap(252)))
        self.assertEqual(self.valid(fixture(pmap_value=pmap(255),devices=())).metadata()['projected_source_records'],256)
        self.bad(fixture(pmap_value=pmap(256),devices=()))
        empty=self.valid(fixture(pmap_value=b'',devices=()))
        self.assertEqual(empty.pmap,())
        self.assertEqual(empty.metadata()['pmap_observation_status'],'OBSERVED_EMPTY')
        retained=next(p for p in empty.properties if p.key=='pmap-io-ranges')
        self.assertEqual(retained.value,b'');self.assertEqual(retained.value_sha256,hashlib.sha256(b'').hexdigest())
        # No union8 restriction: these are full raw inventories, not policy union.
        many=self.valid(fixture(pmap_value=pmap(109),devices=(device(ranges=rows(37),names=b';'.join([b'n']*37)+b'\0'),)))
        self.assertEqual(many.metadata()['projected_source_records'],184)
    def test_unknown_partial_dangling_duplicate_and_required_sources(self):
        for raw in (fixture(pmap_value=pmap()+b'X'),
                    fixture(devices=(device(ranges=rows()+b'X'),)),
                    fixture(devices=(device(ranges=b''),)),
                    fixture(devices=(device(extra=(prop('segment-unknown',b'X'),)),)),
                    fixture(devices=(node('dev',(prop('segment-ranges',rows()),)),)),
                    fixture(devices=(node('dev',(prop('segment-names',b'__TEXT;__DATA\0'),)),)),
                    fixture(defaults_extra=(prop('pmap-io-ranges',pmap()),)),
                    fixture(root_children=(node('other',(prop('pmap-io-ranges',pmap()),)),)),
                    fixture(devices=(device(),device())),
                    fixture(chosen_extra=(prop('darwin-lab-platform-facts',b'present'),)),
                    node('device-tree',children=(node('chosen'),node('defaults'))),
                    node('device-tree',children=(node('defaults',(prop('pmap-io-ranges',pmap()),)),))):
            self.bad(raw)
    def test_names_are_exact_semicolon_terminalNUL_cardinality(self):
        for names in (b'__TEXT;__DATA',b'__TEXT;__DATAX',b'__TEXT;__DATA\0X',b'__TEXT\0;__DATA\0',
                      b';__DATA\0',b'__TEXT;\0',b'__TEXT\0',b'A;B;C\0',b'A;\xff\0',b'\0'):
            self.bad(fixture(devices=(device(names=names),)))
    def test_zero_overflow_PA36_and_no_rounding(self):
        for base,size in ((0x800000000,0),((1<<36)-1,2),(1<<36,1),(0x1000,(1<<64)-1)):
            self.bad(fixture(pmap_value=struct.pack('<QQII',base,size,0,0)))
        for phys,mapped,size in ((0x800100000,0,0),((1<<36)-1,1,2),(0x800100000,(1<<36)-1,2)):
            self.bad(fixture(devices=(device(ranges=struct.pack('<QQQII',phys,0,mapped,size,0),names=b'A\0'),)))
        # No alignment/normalization: retain exact metadata-only byte extents.
        out=self.valid(fixture(pmap_value=struct.pack('<QQII',0x600000001,3,1,2)))
        self.assertEqual(out.pmap[0].span,inv.Span(0x600000001,3))
    def test_serialized_property_padding_is_preserved_not_canonicalized(self):
        names=prop('segment-names',b'__TEXT;__DATA\0')
        self.assertEqual(names[-2:],b'\0\0')
        dirty=names[:-2]+b'\xfa\xfb'
        raw=fixture(devices=(node('dev',(prop('segment-ranges',rows()),dirty)),))
        out=self.valid(raw)
        got=next(p for p in out.properties if p.key=='segment-names')
        self.assertEqual(got.serialized,dirty)
        self.assertEqual(got.serialized_sha256,hashlib.sha256(dirty).hexdigest())

    def test_vram_dram_fixed_shapes_and_root_translation(self):
        for props in ((),(prop('device_type',b'other\0'),prop('reg',struct.pack('<QQ',0x900000000,0x200000))),
                      (prop('device_type',b'vram'),prop('reg',struct.pack('<QQ',0x900000000,0x200000))),
                      (prop('device_type',b'vram\0'),prop('reg',b'')),
                      (prop('device_type',b'vram\0'),prop('reg',struct.pack('<QQQQ',1,2,3,4)))):
            self.bad(fixture(vram_properties=props))
        for props in ((),(prop('#address-cells',struct.pack('<I',1)),prop('#size-cells',struct.pack('<I',2))),
                      (prop('#address-cells',struct.pack('<I',2)),prop('#size-cells',b'bad')),
                      (prop('#address-cells',struct.pack('<I',2)),prop('#size-cells',struct.pack('<I',2)),prop('ranges',b'')),
                      (prop('#address-cells',struct.pack('<I',2)),prop('#size-cells',struct.pack('<I',2)),prop('dma-ranges',b''))):
            self.bad(fixture(root_properties=props))
        for props in ((),(prop('dram-base',b'1234567'),prop('dram-size',struct.pack('<Q',0x400000000))),
                      (prop('dram-base',struct.pack('<Q',0x800000000)),prop('dram-size',struct.pack('<Q',0)))):
            self.bad(fixture(dram_properties=props))
        out=self.valid(fixture(pmap_value=struct.pack('<QQII',0,3,1,2),devices=(device(ranges=struct.pack('<QQQII',0,(1<<64)-1,0,3,0),names=b'A\0'),),vram_properties=(prop('device_type',b'vram\0'),prop('reg',struct.pack('<QQ',0,3)))))
        self.assertEqual(out.pmap[0].span,inv.Span(0,3));self.assertEqual(out.physical[0].span,inv.Span(0,3))
        self.assertEqual(out.vram,inv.Span(0,3));self.assertEqual(len(out.remap),1)
        self.assertEqual(out.remap[0].span,inv.Span(0,3))

    def test_profile_input_and_parser_pin_exactbytes_loader(self):
        raw=fixture()
        for profile in ('other',True,1,None):
            with self.assertRaises(inv.Refused):inv.extract(raw,profile=profile)
        for bad in (bytearray(raw),raw[:-1],raw+b'X',b'',b'\0'*80):self.bad(bad)
        with patch.object(inv,'PARSER_SHA256','0'*64):self.bad(raw)
        helper=Path(inv.__file__).resolve().parents[1]/'native_policy_handoff_002/handoff.py'
        expected=helper.read_bytes();seen=[];original=builtins.compile
        def checked(source,filename,*args,**kwargs):
            if filename==str(helper):seen.append(source)
            return original(source,filename,*args,**kwargs)
        with patch('builtins.compile',checked):self.valid(raw)
        self.assertEqual(seen,[expected])

if __name__=='__main__':unittest.main(verbosity=2)
