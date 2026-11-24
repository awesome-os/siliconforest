from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch,Mock
import copy,hashlib,importlib.util,io,json,re,sys,tempfile,unittest,threading
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[4]
LAB=ROOT/'runs/m1-desktop-010/net-resolver-001/lab'
spec=importlib.util.spec_from_file_location('isolated_network_host',LAB/'integrations/puredarwin/m1-bringup/host_progress.py');h=importlib.util.module_from_spec(spec);sys.modules[spec.name]=h;spec.loader.exec_module(h)
NONCE='1234567890abcdef'*3
PROFILE={'version':1,'profile':'native-resolver-example-http-v1','transport':'dlnet001_space_lf_ack_v2','nonce':NONCE,'relay_port':20000,'seconds':75,'mtu':1500,'close_seconds':15,'backend_idle_seconds':30,'domain':'example.com','dns_alias':'10.0.2.3','http_port':80,'http_max_bytes':2048,'tcp_maxseg_request':256,'resolver':'libc_getaddrinfo','daemon':'mdnsd_debug','selected_answer':'first_getaddrinfo','gateway':'10.0.2.2','host_hid_injection':False,'feature_writes':False}
BASE=ROOT/'artifacts/m1-desktop-010/network-base-candidate-001'
RECORD=json.loads((BASE/'build-record.json').read_text());KERNEL=(BASE/'kernel.macho').read_bytes();REPORT=json.loads((BASE/'offline-inspection.json').read_text())
RECORD['host_input_sources'][h.LINK_HELPER]=h.LINK_HELPER_SHA
class Serial:
 def __init__(self,*a,**k):self.written=[];self.closed=False
 def write(self,data):self.written.append(data);return len(data)
 def close(self):self.closed=True
class Log:
 def __init__(self):self.data=bytearray()
 def write(self,data):self.data.extend(data);return len(data)
 def fileno(self):return 99999
class Integration(unittest.TestCase):
 def inputs(self,profile=PROFILE,record=None,members=None):
  rec=copy.deepcopy(RECORD if record is None else record)
  if profile is not None:rec['network_resolver_experiment']=profile
  report=copy.deepcopy(REPORT)
  if members is not None:report['kernel']['members']=members
  return SimpleNamespace(contents={'build_record':json.dumps(rec).encode(),'kernel':KERNEL,'ramdisk':b'HOST_TEST'},report=report)
 def owner(self,profile=None):
  with patch.object(h.threading.Thread,'start'):
   s=h.ConsoleSession('/host-only',Path('/unused'),serial_factory=Serial,desktop=True,native_trackpad_child=True,network_link=profile)
  s.tx_log=Log();return s
 def write(self,s,data):
  with patch.object(h.os,'fsync'):s.send(data)
 def seal(self,s):
  self.write(s,s.proof.command());s.proof.complete=True;s.desktop_sent=True;self.write(s,h.desktop_command(s.proof.nonce));self.assertEqual(s.native_tx_phase,'sealed')
 def test_exact_optin_and_no_bootargs_change(self):
  x=self.inputs();self.assertEqual(h.network_link_profile(x),PROFILE);self.assertEqual(h.guest_bootargs(x),h.guest_bootargs(self.inputs(None)))
 def test_absence_is_legacy(self):self.assertIsNone(h.network_link_profile(self.inputs(None)))
 def test_profile_mutations(self):
  bads=[None,{},True,[],dict(PROFILE,unknown=1),dict(PROFILE,transport='dlnet001_space_lf_v1')]
  for key,value in [('version',True),('version',1.0),('version',2),('seconds',True),('seconds',74),('close_seconds',True),('close_seconds',14),('close_seconds',16),('mtu',True),('mtu',1499),('nonce','A'*48),('nonce','f'*47),('nonce',0),('relay_port',True),('relay_port',1.5),('relay_port',0),('relay_port',65536),('http_port',True),('http_max_bytes',4096),('selected_answer',False),('backend_idle_seconds',10),('domain','other.example'),('dns_alias','8.8.8.8'),('transport','raw'),('host_hid_injection',True),('host_hid_injection',0),('feature_writes',True),('feature_writes',0),('resolver',True),('resolver','host_getaddrinfo'),('daemon','mdnsd'),('selected_answer','first_wire_A')]:
   bads.append(dict(PROFILE,**{key:value}))
  for p in bads:
   rec=copy.deepcopy(RECORD);rec['network_resolver_experiment']=p
   with self.subTest(profile=p),self.assertRaises(Exception):h.network_link_profile(self.inputs(None,rec))
   with self.subTest(bootargs_profile=p),self.assertRaises(Exception):h.guest_bootargs(self.inputs(None,rec))
 def test_missing_fields_and_gateway_are_refused(self):
  for key in PROFILE:
   p=dict(PROFILE);del p[key]
   with self.subTest(key=key),self.assertRaises(Exception):h.network_link_profile(self.inputs(p))
  for value in (True,0,None,'10.0.2.3'):
   with self.subTest(gateway=value),self.assertRaises(Exception):h.network_link_profile(self.inputs(dict(PROFILE,gateway=value)))
 def test_legacy_network_collisions_and_duplicate_keys(self):
  for key in ('network_link_experiment','network_egress_experiment','network_default_experiment'):
   rec=copy.deepcopy(RECORD);rec[key]={}
   for profile in (None,PROFILE):
    with self.subTest(key=key,profile=profile),self.assertRaises(Exception):h.network_link_profile(self.inputs(profile,rec))
  x=self.inputs();raw=x.contents['build_record'].decode()
  for field in ('"network_resolver_experiment":{},','"host_input_sources":{},'):
   x.contents['build_record']=('{'+field+raw[1:]).encode()
   with self.assertRaises(Exception):h.network_link_profile(x)
  x=self.inputs();x.contents['build_record']=x.contents['build_record'].replace(b'"gateway": "10.0.2.2"',b'"gateway": "10.0.2.2", "gateway": "10.0.2.2"')
  with self.assertRaises(Exception):h.network_link_profile(x)
 def test_full_profile_constructor_validation_before_serial_open(self):
  for p in (dict(PROFILE,unknown=True),dict(PROFILE,seconds=True),dict(PROFILE,gateway='other'),dict(PROFILE,profile='example-http-ack-v2')):
   factory=Mock()
   with self.subTest(profile=p),patch.object(h.threading.Thread,'start'),self.assertRaises(Exception):h.ConsoleSession('/fake',Path('/unused'),serial_factory=factory,desktop=True,native_trackpad_child=True,network_link=p)
   factory.assert_not_called()
 def test_conflicts_and_kernel_members(self):
  for key in ['keyboard_experiment','virtual_input_experiment','terminal_input_experiment','native_keyboard_desktop_experiment','trackpad_enumeration_experiment','dart_survey_experiment','gpu_preflight_experiment']:
   rec=copy.deepcopy(RECORD);rec[key]={}
   with self.subTest(key=key),self.assertRaises(Exception):h.network_link_profile(self.inputs(record=rec))
  names=copy.deepcopy(REPORT['kernel']['members']);names['org.puredarwin.driver.PDGPUPreflight']={}
  with self.assertRaises(Exception):h.network_link_profile(self.inputs(members=names))
  rec=copy.deepcopy(RECORD);rec.pop('native_trackpad_child_experiment')
  with self.assertRaises(Exception):h.network_link_profile(self.inputs(record=rec))
 def test_binding_required(self):
  rec=copy.deepcopy(RECORD);rec['host_input_sources'][h.LINK_HELPER]='0'*64
  with self.assertRaises(Exception):h.network_link_profile(self.inputs(record=rec))
 def test_invalid_constructor_before_any_serial_open(self):
  for profile in ({}, {'nonce':'wrong','relay_port':20000},dict(PROFILE,relay_port=0)):
   factory=Mock()
   with self.subTest(profile=profile),patch.object(h.threading.Thread,'start'),self.assertRaises(Exception):h.ConsoleSession('/fake',Path('/unused'),serial_factory=factory,desktop=True,native_trackpad_child=True,network_link=profile)
   factory.assert_not_called()
 def test_legacy_writer_stays_sealed(self):
  s=self.owner();self.seal(s)
  for data in [b' \n',b'  \n',b'/bin/sh\r',b'\x01HID',h.desktop_command(s.proof.nonce)]:
   before=bytes(s.tx_log.data)
   with self.subTest(data=data),self.assertRaises(Exception):self.write(s,data)
   self.assertEqual(bytes(s.tx_log.data),before)
 def test_link_nonce_override_only_explicit(self):
  s=self.owner(PROFILE);self.assertEqual(s.proof.nonce,NONCE);self.assertIsNotNone(s.link)
  t=self.owner();self.assertIsNone(t.link);self.assertRegex(t.proof.nonce,'^[0-9a-f]{48}$')
 def test_data_refused_without_ready_pending_or_when_malicious(self):
  s=self.owner(PROFILE);self.seal(s)
  for data in [b' \n',b'  \n',b'/bin/sh\r',b'\x01HID',b'']:
   before=bytes(s.tx_log.data)
   with self.assertRaises(Exception):self.write(s,data)
   self.assertEqual(bytes(s.tx_log.data),before)
 def test_exact_issued_whitespace_records_before_write(self):
  s=self.owner(PROFILE);self.seal(s);s.link.ready=True;receipt={'sequence':1};data=b' \n  \n'*8;s.link.pending.append((data,receipt));before=len(s.tx_log.data)
  self.write(s,data);self.assertEqual(bytes(s.tx_log.data[before:]),data);self.assertEqual(s.dev.written[-1],data);self.assertEqual(s.link.data_bytes,len(data))
  with self.assertRaises(Exception):self.write(s,data)
  s.link.pending.append((b'/bin/sh\n',{}))
  with self.assertRaises(Exception):self.write(s,b'/bin/sh\n')
 def test_observation_error_keeps_reader_and_no_suffix(self):
  s=self.owner(PROFILE);self.seal(s);s.advance_network(b'DL29_TX_'+NONCE.encode()+b' '+b'00'*56+b'\n');self.assertIsNotNone(s.error);n=len(s.dev.written);s.advance_network(b'raw diagnostic tail\n');self.assertEqual(len(s.dev.written),n)
 def test_all_prior_real_packets_link_absent(self):
  for name in ['trackpad-input-trace-candidate-001','ten-minute-session-candidate-001','ten-minute-session-candidate-002','network-base-candidate-001']:
   rec=json.loads((ROOT/'artifacts/m1-desktop-010'/name/'build-record.json').read_text());self.assertIsNone(h.network_link_profile(self.inputs(None,rec)))
 def test_kernel_pin_binds_native50_bytes(self):
  x=self.inputs();x.contents['kernel']=KERNEL[:-1]+bytes([KERNEL[-1]^1])
  with self.assertRaises(Exception):h.network_link_profile(x)
 def test_resolver_key_is_exclusive_even_when_only_old_key_present(self):
  for key in ('network_link_experiment','network_egress_experiment','network_default_experiment'):
   rec=copy.deepcopy(RECORD);rec[key]=None
   with self.assertRaises(Exception):h.network_link_profile(self.inputs(None,rec))
 def test_default_gate_no_longer_recognizes_old_profile(self):
  for value in ('example-default-http-ack-v1','example-http-ack-v2'):
   with self.assertRaises(Exception):h.network_link_profile(self.inputs(dict(PROFILE,profile=value)))
if __name__=='__main__':unittest.main(verbosity=2)
