#!/usr/bin/env python3
"""Host-only actual C metadata guards; no IOKit or native keyboard proof."""
import ctypes as C
import pathlib
import shutil
import struct
import subprocess
import tempfile
import unittest

HERE=pathlib.Path(__file__).resolve().parent
U8=C.c_uint8


class Identity(C.Structure):
    _fields_=[('vendor',C.c_uint16),('product',C.c_uint16),('version',C.c_uint16),
              ('max_input',C.c_uint16),('country',U8)]


class Message(C.Structure):
    _fields_=[(n,U8) for n in ['device','flags','command','report','subdevice','id']]+[
        ('response_length',C.c_uint16),('payload',C.POINTER(U8)),('payload_length',C.c_size_t)]


class Observation(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ['packet_available','packet_flags','packet_device',
        'offset','remain','length','message_available','device','flags','command',
        'report','subdevice','id','response_length','payload_length','parser_error']]


class MetadataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='m1kbd-metadata-')
        so=pathlib.Path(cls.temp.name)/'metadata.so'
        subprocess.run([shutil.which('cc'),'-std=c11','-O2','-Wall','-Wextra','-Werror',
            '-fPIC','-shared','-fsanitize=undefined','-fno-sanitize-recover=all',
            str(HERE/'m1kbd_hid.c'),'-o',str(so)],check=True)
        cls.lib=C.CDLL(str(so))
        for name in ['device_info','interface_info']:
            getattr(cls.lib,'m1kbd_'+name).argtypes=[C.POINTER(U8),C.c_size_t,C.POINTER(Identity)]
        cls.lib.m1kbd_response_matches.argtypes=[C.POINTER(Message),U8,U8,U8]
        cls.lib.m1kbd_keyboard_input.argtypes=[C.POINTER(Message),C.c_uint16]
        cls.lib.m1kbd_observe.argtypes=[C.POINTER(U8),C.c_size_t,C.POINTER(Message),C.c_uint,C.POINTER(Observation)]

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def device(self):
        p=bytearray(32)
        struct.pack_into('<HHHH',p,4,3,0x05ac,0x0341,0x102)
        struct.pack_into('<HHHHHH',p,12,24,2,26,2,28,4)
        p[24:]=b'abcd1234'
        return p

    def interface(self):
        p=bytearray(24); p[1]=1; p[5]=33
        struct.pack_into('<HHHHH',p,6,16,2,8,16,8)
        return p

    def parse(self,fn,p,identity):
        b=(U8*len(p)).from_buffer_copy(p)
        return getattr(self.lib,'m1kbd_'+fn)(b,len(p),C.byref(identity))

    def test_device_then_interface_metadata(self):
        i=Identity()
        self.assertEqual(self.parse('device_info',self.device(),i),0)
        self.assertEqual((i.vendor,i.product,i.version),(0x05ac,0x0341,0x102))
        self.assertEqual(self.parse('interface_info',self.interface(),i),0)
        self.assertEqual((i.max_input,i.country),(16,33))
        self.assertEqual(i.product,0x341)

    def test_truncations_wrong_ids_and_string_ranges_leave_output_unchanged(self):
        p=self.device()
        cases=[p[:i] for i in range(24)]
        for off,val in [(4,0),(4,1),(4,5),(6,0),(8,0x274),(12,0xffff),(14,0xffff),(16,33),(18,8),(20,33),(22,5)]:
            q=bytearray(p); struct.pack_into('<H',q,off,val); cases.append(q)
        for p in cases:
            i=Identity(1,2,3,4,5); before=bytes(i)
            self.assertEqual(self.parse('device_info',p,i),-1)
            self.assertEqual(bytes(i),before)

    def test_interface_bounds_and_keyboard_only(self):
        p=self.interface(); cases=[p[:i] for i in range(16)]
        for off,val in [(1,2),(6,0),(6,2039),(12,0xffff),(14,0xffff)]:
            q=bytearray(p)
            if off==1: q[off]=val
            else: struct.pack_into('<H',q,off,val)
            cases.append(q)
        for q in cases:
            i=Identity(1,2,3,4,5); before=bytes(i)
            self.assertEqual(self.parse('interface_info',q,i),-1)
            self.assertEqual(bytes(i),before)

    def test_response_requires_all_request_identity_fields(self):
        m=Message(0xd0,0x40,0x20,0x10,1,7,512,None,0)
        self.assertEqual(self.lib.m1kbd_response_matches(C.byref(m),0x10,1,7),1)
        for key in ['device','flags','command','report','subdevice','id']:
            old=getattr(m,key); setattr(m,key,old^1)
            self.assertEqual(self.lib.m1kbd_response_matches(C.byref(m),0x10,1,7),0)
            setattr(m,key,old)

    def test_only_real_device_one_input_eligible_with_bounded_payload(self):
        payload=(U8*16)()
        m=Message(1,0x20,0x10,1,0,0,0,payload,16)
        self.assertEqual(self.lib.m1kbd_keyboard_input(C.byref(m),16),1)
        for field,val in [('device',2),('device',0),('flags',0x40),('command',0x20)]:
            old=getattr(m,field); setattr(m,field,val)
            self.assertEqual(self.lib.m1kbd_keyboard_input(C.byref(m),16),0)
            setattr(m,field,old)
        for length in [0,17,2048]:
            m.payload_length=length
            self.assertEqual(self.lib.m1kbd_keyboard_input(C.byref(m),16),-1)
        m.payload_length=16; m.payload=C.POINTER(U8)()
        self.assertEqual(self.lib.m1kbd_keyboard_input(C.byref(m),16),-1)

    def test_observation_keeps_header_only_never_key_payload_or_pointer(self):
        payload=(U8*16)(*range(16))
        p=bytearray(256); p[:8]=bytes([0x20,1])+struct.pack('<HHH',246,0,26)
        m=Message(1,0x20,0x10,1,0,7,0,payload,16)
        out=Observation()
        self.lib.m1kbd_observe((U8*256).from_buffer_copy(p),256,C.byref(m),0,C.byref(out))
        before=bytes(out)
        self.assertEqual(C.sizeof(out),64)
        self.assertEqual((out.offset,out.length,out.id,out.payload_length),(246,26,7,16))
        # Even an invalid borrowed pointer is never dereferenced by diagnostics.
        m.payload=C.cast(C.c_void_p(1),C.POINTER(U8))
        p[8:]=bytes([0xa5])*248
        self.lib.m1kbd_observe((U8*256).from_buffer_copy(p),256,C.byref(m),0,C.byref(out))
        self.assertEqual(bytes(out),before)

    def test_bad_crc_observation_does_not_guess_fragment_message_header(self):
        p=(U8*256)(*([0x42]*256)); out=Observation()
        self.lib.m1kbd_observe(p,256,None,2,C.byref(out))
        self.assertEqual(out.packet_available,1); self.assertEqual(out.parser_error,2)
        self.assertEqual(out.message_available,0)
        self.assertEqual((out.command,out.report,out.subdevice,out.id,out.payload_length),(0,0,0,0,0))
        self.lib.m1kbd_observe(p,7,None,1,C.byref(out))
        self.assertEqual(out.packet_available,0); self.assertEqual(out.parser_error,1)


if __name__=='__main__': unittest.main()
