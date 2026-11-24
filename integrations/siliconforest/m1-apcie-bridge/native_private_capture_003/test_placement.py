"""Pure source-displacement checks; no heap/loader/transport observations."""
import struct,unittest
import audit as a
from test_audit import load_fixture

class PlacementTests(unittest.TestCase):
    def setUp(self):
        self.p,self.f=load_fixture();_,self.old,_,_,_,_=self.f.fixture()
        self.original_phys=struct.unpack_from('<Q',self.old,16)[0]
        self.phys=self.original_phys+0x10000000;self.size=0x20000000
        self.kernel={'vmin':0xfffffe0006000000,'vmax':0xfffffe0006008000,'entry':0xfffffe0006004400}
    def sources(self,tc=16384,sep=16384,preos=None):
        props=[self.f.prop('TrustCache',struct.pack('<QQ',self.original_phys+0x10000,tc)),self.f.prop('SEPFW',struct.pack('<QQ',self.original_phys+0x20000,sep))]
        if preos is not None:props.append(self.f.prop('preoslog',struct.pack('<QQ',self.original_phys+0x30000,preos)))
        raw=self.f.node('device-tree',(),(self.f.node('chosen',(),(self.f.node('memory-map',tuple(props)),)),))
        old=bytearray(self.old);struct.pack_into('<I',old,104,len(raw));return raw,bytes(old)
    def derive(self,*args):return a.source_placement(*args,self.kernel,self.phys,self.size)
    def test_exact_formula_and_size_rounding(self):
        for tc,sep,preos,guest_delta,offset in ((1,1,None,0x1008000,0xc000),(16384,16384,None,0x1008000,0xc000),(16385,16384,None,0x100c000,0xc000),(16384,16385,None,0x1008000,0x10000),(16384,16384,16384,0x1008000,0x10000)):
            with self.subTest(tc=tc,sep=sep,preos=preos):
                r=self.derive(*self.sources(tc,sep,preos))
                self.assertEqual(r,{'guest_base':self.phys+guest_delta,'bootargs_off':offset,'entry':self.phys+guest_delta+0x4400})
    def test_preoslog_zero_unaligned_and_source_overflow(self):
        for kwargs in ({'preos':0},{'preos':1},{'preos':16385},{'tc':0},{'sep':0},{'tc':(1<<64)-1}):
            with self.subTest(kwargs=kwargs),self.assertRaises(ValueError):self.derive(*self.sources(**kwargs))
    def test_original_size_native_bounds_and_actual_span(self):
        raw,old=self.sources();changed=bytearray(old);struct.pack_into('<I',changed,104,len(raw)+1)
        with self.assertRaisesRegex(ValueError,'dt_size/raw length'):self.derive(raw,bytes(changed))
        with self.assertRaises(ValueError):a.source_placement(raw,old,self.kernel,self.phys+1,self.size-1)
        with self.assertRaises(ValueError):a.source_placement(raw,old,self.kernel,self.phys,self.size-16384)
        for key,value in (('vmax',self.kernel['vmin']+(512<<20)+1),('vmin',True),('entry',self.kernel['vmax'])):
            with self.subTest(key=key),self.assertRaises(ValueError):a.source_placement(raw,old,dict(self.kernel,**{key:value}),self.phys,self.size)

if __name__=='__main__':unittest.main(verbosity=2)
