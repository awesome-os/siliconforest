#!/usr/bin/env python3
"""Host-only C adapter tests: synthetic DT and fake MMIO, never hardware."""
import ctypes as C
import hashlib
import pathlib
import shutil
import struct
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
LAB = HERE.parents[2]
U8, U32, U64 = C.c_uint8, C.c_uint32, C.c_uint64
PROPERTY = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.c_char_p,
                      C.POINTER(C.POINTER(U8)), C.POINTER(C.c_size_t))
READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint, U32, C.POINTER(U32))
WRITE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint, U32, U32)
NOW = C.CFUNCTYPE(U64, C.c_void_p)
DELAY = C.CFUNCTYPE(None, C.c_void_p, U32)


class Board(C.Structure):
    _fields_ = [('base', U64*5), ('size', U32*5), ('clock_hz', U32),
                ('speed_hz', U32), ('qualified', U32)]


class IO(C.Structure):
    _fields_ = [('context', C.c_void_p), ('read32', READ), ('write32', WRITE),
                ('now_us', NOW), ('delay_us', DELAY)]


class HW(C.Structure):
    _fields_ = [('io', IO), ('board', Board)] + [(s, C.c_uint) for s in
                ('initialized', 'spi_access', 'gpio_access', 'ready', 'busy',
                 'faulted', 'cleanup_failed')] + [('power_before', U32*6), ('gpio_before', U32*6)]


def words(*v):
    return struct.pack('<'+'I'*len(v), *v)


def fixture():
    p = {}
    def setp(path, **props):
        for k, v in props.items():
            p[path, k] = v
    setp('/', **{'target-type': b'J293\0', 'compatible': b'J293AP\0MacBookPro17,1\0AppleARM\0'})
    setp('/arm-io', compatible=b'arm-io,t8103\0', ranges=struct.pack('<QQQ', 0, 0x200000000, 0x100000000))
    setp('/arm-io/spi3', compatible=b'spi-1,spimc\0', reg=struct.pack('<QQ', 0x3510c000, 0x4000),
         **{'clock-gates': words(52), 'clock-ids': words(379), 'interrupts': words(617),
            'interrupt-parent': words(0x70), 'function-spi_cs0': words(0x77, 0x4750494f, 49, 1)})
    setp('/arm-io/spi3/ipd', compatible=b'hid-transport,spi\0', device_type=b'ipd\0',
         reg=words(0, 125, 0x08010000, 0, 20000, 0, 0, 0),
         **{'interrupt-parent': words(0x79), 'interrupts': words(13, 0x06000001),
            'function-enable_cs': words(0x77, 0x4750494f, 49, 0x101),
            'function-spi_en': words(0x77, 0x4750494f, 195, 0x10101)})
    for name, ph, base, size, count in [('gpio', 0x77, 0x3c100000, 0x100000, 212),
                                        ('nub-gpio', 0x79, 0x3d1f0000, 0x4000, 23)]:
        setp('/arm-io/'+name, compatible=b'gpio,t8101\0', reg=struct.pack('<QQ', base, size),
             **{'AAPL,phandle': words(ph), '#gpio-pins': words(count)})
    setp('/arm-io/aic', compatible=b'aic,1\0', **{'AAPL,phandle': words(0x70)})
    devices = bytearray()
    for ident, a, b, name, psidx, off in [(33, 0, 0, 'SIO_BUSIF', 0, 0xc0),
            (34, 33, 0, 'SIO', 0, 0xc8), (44, 34, 0, 'SPI_P', 1, 0x18),
            (52, 34, 44, 'SPI3', 1, 0x58), (15, 0, 0, 'GPIO', 0, 0x30),
            (147, 0, 0, 'NUB_GPIO', 2, 0x28)]:
        d = bytearray(48)
        struct.pack_into('<HH', d, 4, a, b)
        d[10:12] = bytes([off//8, psidx])
        struct.pack_into('<H', d, 26, ident)
        d[32:32+len(name)] = name.encode()
        devices += d
    setp('/arm-io/pmgr', compatible=b'pmgr1,t8103\0', devices=bytes(devices),
         reg=struct.pack('<QQQQ', 0x3b700000, 0x8c000, 0x3d280000, 0x74000),
         **{'ps-regs': words(0, 0x100, 0, 0, 0x200, 0, 1, 0x58, 0)})
    return p


def validate(lib, props):
    buffers = {k: (U8*len(v)).from_buffer_copy(v) for k, v in props.items()}
    @PROPERTY
    def lookup(_ctx, path, key, out, n):
        k = path.decode(), key.decode()
        if k not in buffers:
            return -1
        out[0] = C.cast(buffers[k], C.POINTER(U8))
        n[0] = len(buffers[k])
        return 0
    b = Board()
    return lib.m1kbd_validate_board(lookup, None, C.byref(b)), b


class Fake:
    def __init__(self):
        self.reg = {(3, off): 0xff for off in [0x1c0, 0x1c8, 0x218, 0x258, 0x130]}
        self.reg[4, 0x80] = 0xff
        self.time = 0
        self.events = []
        self.running = False
        self.tx_fifo = []
        self.rx_fifo = []
        self.wire_tx = bytearray()
        self.rx_source = bytes(range(256))
        self.status = bytes.fromhex('ac2768d5')
        self.rx_index = 0
        self.tx_goal = self.rx_goal = 0
        self.fault = None
        self.power_stuck = False
        self.fail_write = None
        self.fail_read = None
        self.callbacks = (READ(self.read), WRITE(self.write), NOW(lambda _: self.time), DELAY(self.delay))
        self.io = IO(None, *self.callbacks)

    def advance(self):
        if not self.running or self.fault == 'timeout':
            return
        while self.tx_fifo:
            self.wire_tx.append(self.tx_fifo.pop(0))
        if self.tx_goal and len(self.wire_tx) == self.tx_goal:
            self.reg[0, 0x134] = 2
        if self.rx_goal:
            source = self.status if self.rx_goal == 4 else self.rx_source
            end = self.rx_goal-1 if self.fault == 'short' else self.rx_goal
            while self.rx_index < end and len(self.rx_fifo) < 16:
                self.rx_fifo.append(source[self.rx_index])
                self.rx_index += 1
            if self.rx_index == end:
                self.reg[0, 0x134] = 1

    def read(self, _ctx, r, off, out):
        self.events.append(('r', r, off, self.time))
        if (r, off) == self.fail_read:
            return -1
        if r == 0 and off == 0x13c:
            self.advance()
        if r == 0 and off == 0x10c:
            out[0] = len(self.tx_fifo)<<8 | len(self.rx_fifo)<<24
            if self.running and self.fault == 'rx_overflow': out[0] = 17<<24
            if self.running and self.fault == 'tx_overflow': out[0] = 17<<8
        elif r == 0 and off == 0x20:
            if not self.rx_fifo: return -1
            out[0] = self.rx_fifo.pop(0)
        elif r == 0 and off == 0x13c and self.running and self.fault == 'fifo_error':
            out[0] = 1<<16
        elif r == 0 and off == 0x134 and self.running and self.fault == 'early_tx_complete':
            out[0] = 2
        else:
            out[0] = self.reg.get((r, off), 0)
        return 0

    def write(self, _ctx, r, off, v):
        self.events.append(('w', r, off, v, self.time))
        if (r, off) == self.fail_write:
            return -1
        if r == 0 and off == 0:
            self.running = v == 1
            if v == 12:
                self.tx_fifo.clear(); self.rx_fifo.clear(); self.rx_index = 0
                self.wire_tx.clear()
        if r == 0 and off == 0x10:
            self.tx_fifo.append(v)
        if r == 0 and off == 0x4c: self.tx_goal = v
        if r == 0 and off == 0x34: self.rx_goal = v
        self.reg[r, off] = 0 if r == 0 and off in [0x134, 0x13c] else v
        if r == 3 and off in [0x218, 0x130, 0x258] and not self.power_stuck:
            self.reg[r, off] = v | 0xf0
        return 0

    def delay(self, _ctx, us):
        self.events.append(('d', us, self.time))
        self.time += us

    def writes(self, region=None):
        return [e for e in self.events if e[0] == 'w' and (region is None or e[1] == region)]


class AdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='m1kbd-host-')
        so = pathlib.Path(cls.tmp.name)/'adapter.so'
        cc = shutil.which('cc')
        if not cc: raise RuntimeError('host C compiler required')
        subprocess.run([cc, '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fPIC', '-shared', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        str(HERE/'m1kbd_board.c'), str(HERE/'m1kbd_pio.c'), '-o', str(so)], check=True)
        cls.lib = C.CDLL(str(so))
        cls.lib.m1kbd_validate_board.argtypes = [PROPERTY, C.c_void_p, C.POINTER(Board)]
        cls.lib.m1kbd_hw_init.argtypes = [C.POINTER(HW), C.POINTER(Board), C.POINTER(IO)]
        for fn in ['start', 'stop']:
            getattr(cls.lib, 'm1kbd_hw_'+fn).argtypes = [C.POINTER(HW)]
        cls.lib.m1kbd_hw_ready.argtypes = [C.POINTER(HW), C.POINTER(C.c_int)]
        for fn in ['read_packet', 'write_packet']:
            getattr(cls.lib, 'm1kbd_hw_'+fn).argtypes = [C.POINTER(HW), C.POINTER(U8)]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setup_hw(self, start=True):
        e, board = validate(self.lib, fixture())
        self.assertEqual(e, 0)
        f, h = Fake(), HW()
        self.assertEqual(self.lib.m1kbd_hw_init(C.byref(h), C.byref(board), C.byref(f.io)), 0)
        if start: self.assertEqual(self.lib.m1kbd_hw_start(C.byref(h)), 0)
        return f, h

    def test_board_fixture_and_foreign_topology_refusals(self):
        e, b = validate(self.lib, fixture()); self.assertEqual(e, 0)
        self.assertEqual(b.base[0], 0x23510c000)
        mutations = [('/', 'target-type', b'J314\0'), ('/arm-io', 'compatible', b'arm-io,t6000\0'),
            ('/arm-io/spi3/ipd', 'function-spi_en', words(0x77,0x4750494f,194,0x10101)),
            ('/arm-io/spi3/ipd', 'interrupts', words(6,0x06000001)),
            ('/arm-io/spi3', 'reg', struct.pack('<QQ', 0x35100000,0x4000)),
            ('/arm-io/spi3', 'clock-gates', words(51)),
            ('/arm-io/gpio', 'AAPL,phandle', words(66))]
        for path, key, value in mutations:
            with self.subTest(key=key):
                p=fixture(); p[path,key]=value; e,b=validate(self.lib,p)
                self.assertEqual(e,-2); self.assertEqual(b.qualified,0)

    def test_power_table_refuses_duplicate_wrong_parent_and_overflow(self):
        for kind in ['duplicate','parent','virtual','index','range']:
            with self.subTest(kind=kind):
                p=fixture(); d=bytearray(p['/arm-io/pmgr','devices'])
                if kind=='duplicate': d+=d[:48]
                if kind=='parent': struct.pack_into('<H',d,3*48+4,15)
                if kind=='virtual': d[3*48] |= 0x10
                if kind=='index': d[3*48+11]=255
                if kind=='range': p['/arm-io/pmgr','reg']=struct.pack('<QQ', 0xffffffffffffffff, 0x1000)
                p['/arm-io/pmgr','devices']=bytes(d)
                self.assertEqual(validate(self.lib,p)[0],-2)

    def test_every_truncated_required_property_refused(self):
        p=fixture()
        for k,v in p.items():
            with self.subTest(key=k):
                q=dict(p); q[k]=v[:-1]
                self.assertEqual(validate(self.lib,q)[0],-2)

    def test_saved_actual_adt_guard(self):
        path=LAB/'runs/m1-display-010/attempt-002/guest/original-adt.bin'
        if not path.exists(): self.skipTest('private saved ADT absent')
        b=path.read_bytes()
        self.assertEqual(hashlib.sha256(b).hexdigest(),'cc573a454eded057d6ebc3ab68abb552c4ab4eee2d8cf7aa150f57a2ec989dc1')
        props={}
        def node(o,parent=None):
            n,c=struct.unpack_from('<II',b,o); o+=8; d={}
            for _ in range(n):
                name=b[o:o+32].split(b'\0',1)[0].decode(); size=struct.unpack_from('<I',b,o+32)[0]&0x7fffffff
                o+=36; d[name]=b[o:o+size]; o+=(size+3)&~3
            path='/' if parent is None else parent.rstrip('/')+'/'+d['name'].split(b'\0',1)[0].decode()
            props.update({(path,k):v for k,v in d.items()})
            for _ in range(c): o=node(o,path)
            return o
        self.assertEqual(node(0),len(b))
        self.assertEqual(validate(self.lib,props)[0],0)

    def test_bad_board_no_backend_access(self):
        f,h=self.setup_hw(False); b=h.board; b.qualified=0; fresh=HW()
        self.assertEqual(self.lib.m1kbd_hw_init(C.byref(fresh),C.byref(b),C.byref(f.io)),-2)
        self.assertEqual(self.lib.m1kbd_hw_start(C.byref(fresh)),-8)
        self.assertFalse(f.events)

    def test_shared_and_always_on_power_only_read(self):
        f,h=self.setup_hw()
        self.assertFalse(f.writes(3)); self.assertFalse(f.writes(4))
        self.assertEqual(list(h.power_before),[0xff]*6)

    def test_activate_spi_parent_gpio_before_spi_no_status_replay(self):
        f,h=self.setup_hw(False)
        for off in [0x218,0x130,0x258]: f.reg[3,off]=0x10000300
        self.assertEqual(self.lib.m1kbd_hw_start(C.byref(h)),0)
        self.assertEqual([(e[2],e[3]) for e in f.writes(3)],[(0x218,15),(0x130,15),(0x258,15)])
        last_power=max(i for i,e in enumerate(f.events) if e[0]=='w' and e[1]==3)
        first_spi=min(i for i,e in enumerate(f.events) if e[0] in ['r','w'] and e[1]==0)
        self.assertLess(last_power,first_spi)

    def test_power_refusals_and_timeout_precede_spi_or_gpio(self):
        for label in ['sio_off','nub_off','reset','busy','timeout']:
            with self.subTest(label=label):
                f,h=self.setup_hw(False)
                if label=='sio_off': f.reg[3,0x1c8]=0
                if label=='nub_off': f.reg[4,0x80]=0
                if label=='reset': f.reg[3,0x258]=1<<31
                if label=='busy': f.reg[3,0x258]=1<<11
                if label=='timeout': f.reg[3,0x258]=0; f.power_stuck=True
                self.assertLess(self.lib.m1kbd_hw_start(C.byref(h)),0)
                self.assertFalse([e for e in f.events if e[0] in ['r','w'] and e[1]<3])
                self.assertLessEqual(f.time,100)

    def test_lock_refuses_before_gpio_writes(self):
        for r,pin in [(1,46),(1,49),(1,195),(2,13)]:
            with self.subTest(pin=pin):
                f,h=self.setup_hw(False); f.reg[r,pin*4]=1<<21
                self.assertEqual(self.lib.m1kbd_hw_start(C.byref(h)),-7)
                self.assertFalse(f.writes(1)); self.assertFalse(f.writes(2))
                self.assertFalse(f.writes(0))

    def test_busy_settles_and_rmw_uses_fresh_power_word(self):
        f,h=self.setup_hw(False)
        f.reg[3,0x258]=0
        original=f.read
        samples=0
        @READ
        def read(ctx,r,off,out):
            nonlocal samples
            ret=original(ctx,r,off,out)
            if (r,off)==(3,0x258):
                samples+=1
                if samples<3: out[0]=1<<11
                elif samples==4: out[0]=1<<20  # changed since preflight
            return ret
        f.callbacks=(read,)+f.callbacks[1:]; f.io.read32=read; h.io.read32=read
        self.assertEqual(self.lib.m1kbd_hw_start(C.byref(h)),0)
        self.assertEqual([e[3] for e in f.writes(3) if e[2]==0x258],[0x10000f])
        self.assertGreaterEqual(f.time,60002)

    def test_unexpected_tx_receive_has_distinct_failure(self):
        f,h=self.setup_hw(); original=f.advance
        def received():
            original()
            if f.tx_goal and f.running: f.rx_fifo=[42]
        f.advance=received
        packet=(U8*256)()
        self.assertEqual(self.lib.m1kbd_hw_write_packet(C.byref(h),packet),-10)
        self.assert_failed_idle(f,h)

    def test_gpio_masks_preserve_pull_drive_and_group_reset_timing(self):
        f,h=self.setup_hw(False)
        preserved=0x00458180
        for r,pin in [(1,46),(1,47),(1,48),(1,49),(1,195),(2,13)]: f.reg[r,pin*4]=preserved
        self.assertEqual(self.lib.m1kbd_hw_start(C.byref(h)),0)
        for r,pin in [(1,46),(1,47),(1,48),(1,49),(1,195),(2,13)]:
            self.assertEqual(f.reg[r,pin*4]&preserved,preserved)
        en=[e for e in f.writes(1) if e[2]==195*4]
        self.assertEqual([e[3]&1 for e in en],[0,1,0,1])
        self.assertEqual(en[-2][4]-en[-3][4],5000)
        self.assertEqual(en[-1][4]-en[-2][4],5000)
        self.assertEqual(f.time-en[-1][4],50000)
        self.assertEqual(f.reg[0,0x30],15); self.assertEqual(f.reg[0,4],32)
        self.assertEqual(f.reg[2,52]&(7<<1),7<<1)

    def test_not_ready_makes_no_spi_transaction(self):
        f,h=self.setup_hw(); f.events.clear(); f.reg[2,52]|=1
        out=(U8*256)()
        self.assertEqual(self.lib.m1kbd_hw_read_packet(C.byref(h),out),1)
        self.assertFalse(f.writes()); self.assertEqual(h.faulted,0)

    def test_read_256_exact_and_timing(self):
        f,h=self.setup_hw(); f.events.clear(); before=f.time
        out=(U8*256)()
        self.assertEqual(self.lib.m1kbd_hw_read_packet(C.byref(h),out),0)
        self.assertEqual(bytes(out),bytes(range(256)))
        pins=[e for e in f.writes(0) if e[2]==12]
        self.assertEqual([e[3]&2 for e in pins],[0,2])
        run=next(e for e in f.writes(0) if e[2]==0 and e[3]==1)
        self.assertGreaterEqual(run[4]-before,65)
        self.assertEqual(f.time-pins[-1][4],250)
        self.assertEqual(f.reg[0,0],0)

    def test_write_and_status_share_cs_with_200us_delay(self):
        f,h=self.setup_hw(); f.events.clear(); packet=(U8*256)(*range(256))
        self.assertEqual(self.lib.m1kbd_hw_write_packet(C.byref(h),packet),0)
        self.assertEqual([e[3] for e in f.writes(0) if e[2]==16],list(range(256)))
        self.assertEqual([e[3]&2 for e in f.writes(0) if e[2]==12],[0,2])
        runs=[e for e in f.writes(0) if e[2]==0 and e[3]==1]
        stop=next(e for e in f.writes(0) if e[2]==0 and e[3]==0)
        self.assertEqual(runs[1][4]-stop[4],200)

    def assert_failed_idle(self,f,h):
        self.assertEqual(h.faulted,1); self.assertEqual(h.ready,0)
        self.assertEqual(f.reg[0,0],0); self.assertEqual(f.reg[0,12]&2,2)
        self.assertEqual(f.reg[0,0x130],0); self.assertEqual(f.reg[0,0x138],0)
        self.assertEqual(f.reg[1,195*4]&1,0)
        self.assertFalse(f.writes(4))

    def test_failures_remain_failures_and_stop(self):
        for fault in ['timeout','short','rx_overflow','tx_overflow','fifo_error','early_tx_complete','bad_status']:
            with self.subTest(fault=fault):
                f,h=self.setup_hw(); f.events.clear(); before=f.time
                f.fault=fault
                if fault=='bad_status': f.status=b'nope'
                out=(U8*256)()
                fn=self.lib.m1kbd_hw_write_packet if fault in ['early_tx_complete','bad_status'] else self.lib.m1kbd_hw_read_packet
                self.assertLess(fn(C.byref(h),out),0)
                self.assert_failed_idle(f,h)
                self.assertLessEqual(f.time-before,200315)
                f.events.clear()
                self.assertEqual(self.lib.m1kbd_hw_read_packet(C.byref(h),out),-8)
                self.assertFalse(f.events)

    def test_four_byte_status_rx_overflow_does_not_touch_canary(self):
        # Overflow is checked before RXDATA consumption, including tiny status.
        f,h=self.setup_hw(); original=f.advance
        def overfill():
            original()
            if f.rx_goal==4 and f.running: f.rx_fifo=[0]*5
        f.advance=overfill
        packet=(U8*256)()
        self.assertEqual(self.lib.m1kbd_hw_write_packet(C.byref(h),packet),-5)
        self.assertFalse([e for e in f.events if e[0]=='r' and e[1:3]==(0,32)])
        self.assert_failed_idle(f,h)

    def test_mmio_error_and_cleanup_error_recorded(self):
        f,h=self.setup_hw(); f.fail_read=(0,0x13c); out=(U8*256)()
        self.assertEqual(self.lib.m1kbd_hw_read_packet(C.byref(h),out),-3)
        self.assert_failed_idle(f,h)
        f,h=self.setup_hw(); f.fail_write=(0,0x138)
        self.assertEqual(self.lib.m1kbd_hw_stop(C.byref(h)),-3)
        self.assertEqual(h.cleanup_failed,1)
        self.assertEqual(h.ready,0)

    def test_stop_busy_and_reinit_refused_without_io(self):
        f,h=self.setup_hw(); f.events.clear(); h.busy=1
        self.assertEqual(self.lib.m1kbd_hw_stop(C.byref(h)),-8)
        self.assertEqual(self.lib.m1kbd_hw_init(C.byref(h),C.byref(h.board),C.byref(f.io)),-8)
        self.assertFalse(f.events)


if __name__ == '__main__':
    unittest.main()
