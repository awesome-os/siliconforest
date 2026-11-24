"""Causal real ConsoleSession.send/native-FSM/ledger tests with fake serial only.

Pending whitespace and ACKs are synthetic fixtures; no network/target outcome.
Both modules are actual source copies. Real temporary files/fsync record writes.
"""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import hashlib
import importlib.util
import json
import os
import sys
import tempfile
import unittest
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[4]
LEAF=Path(__file__).resolve().parent
NEW=ROOT/'runs/m1-desktop-010/net-desktop-ledger-001/lab'
OLD=ROOT/'runs/m1-desktop-010/net-desktop-001/lab'
NAME='integrations/puredarwin/m1-bringup/host_progress.py'

def module(label,path):
 s=importlib.util.spec_from_file_location(label,path);m=importlib.util.module_from_spec(s);sys.modules[label]=m;s.loader.exec_module(m);return m
old=module('real_old_DL30_writer',OLD/NAME)
new=module('real_new_DL30_writer',NEW/NAME)
BINDING=json.loads((LEAF/'host-binding-001.json').read_text())
NONCE='1234567890abcdef'*3
PROFILE=dict(BINDING['fixed_experiment_fields'],nonce=NONCE,relay_port=20000)

class Serial:
 def __init__(self,*args,**kwargs):self.data=bytearray();self.owner=None;self.closed=False;self.calls=0
 def write(self,data):
  self.calls+=1
  # Actual durable attempted ledger and in-memory ledger must precede write.
  assert os.pread(self.owner.tx_log.fileno(),len(self.owner.tx_data),0)==bytes(self.owner.tx_data)
  assert bytes(self.owner.tx_data)==bytes(self.data)+data
  self.data.extend(data);return len(data)
 def close(self):self.closed=True

class Writer(unittest.TestCase):
 def setUp(self):self.tmp=tempfile.TemporaryDirectory(prefix='DL30-real-writer-');self.root=Path(self.tmp.name);self.sessions=[]
 def tearDown(self):
  for s in self.sessions:
   s.finish.set()
   if s.link:s.link.cancel()
   s.dev.close();s.tx_log.close()
  self.tmp.cleanup()
 def owner(self,m=new,network=True,nonce=NONCE):
  profile=dict(PROFILE,nonce=nonce) if network else None
  with patch.object(m.threading.Thread,'start'):
   s=m.ConsoleSession('/FAKE_NO_DEVICE',self.root,serial_factory=Serial,desktop=True,native_trackpad_child=True,network_link=profile)
  p=self.root/str(len(self.sessions));s.tx_log=p.open('x+b',buffering=0);s.dev.owner=s;self.sessions.append(s);return s
 def seal(self,m,s,shell=True):
  if shell:s.send(b'/bin/sh\r')
  s.send(s.proof.command());s.proof.complete=True;s.desktop_sent=True;s.send(m.desktop_command(s.proof.nonce))
  self.assertEqual(s.native_tx_phase,'sealed')
  if s.link:s.link.ready=True
 def issue(self,s,data,ack=True):
  seq=len(s.link.issued)+1;s.link.pending.append((data,{'sequence':seq,'type':2}))
  s.send(data)
  if ack:s.link.feed_console(f'DL30_ACK_{s.proof.nonce} bytes={s.link.data_bytes}\r\r\n'.encode())
 def pump(self,s,amount):
  while amount:
   n=min(192,amount);self.issue(s,(b' \n'*((n+1)//2))[:n]);amount-=n
 def snapshot(self,s):return(bytes(s.tx_data),os.pread(s.tx_log.fileno(),len(s.tx_data)+1024,0),bytes(s.dev.data),s.link.data_bytes if s.link else None)
 def test_exact_only_one_source_line_changed(self):
  self.assertEqual((OLD/NAME).read_text().replace('(147456 if self.link else 16384)','(278528 if self.link else 16384)'),(NEW/NAME).read_text())
  oldb=json.loads((ROOT/'integrations/puredarwin/m1-network-desktop-service/session/host/host-binding-001.json').read_text())
  changed=[n for n,h in BINDING['normalized_files_sha256'].items()if h!=oldb['normalized_files_sha256'][n]]
  self.assertEqual(changed,[NAME]);self.assertEqual(len(BINDING['normalized_files_sha256']),22)
 def test_actual007_boundary_old_fail_new_pass(self):
  capture=ROOT/'runs/m1-desktop-010/desktop-network-attempt-007/guest'
  facts=json.loads((capture/'console-result.json').read_text());nonce=facts['nonce']
  challenge=(capture/'challenge-input.bin').read_bytes();launcher=(capture/'desktop-input.bin').read_bytes();tx=(capture/'console-tx.bin').read_bytes();prefix=challenge+launcher
  if tx.startswith(b'/bin/sh\r'):prefix=b'/bin/sh\r'+prefix
  self.assertEqual(len(prefix),337);self.assertEqual(len(tx),147267)
  for m,allowed in ((old,False),(new,True)):
   s=self.owner(m,nonce=nonce);self.seal(m,s,shell=prefix.startswith(b'/bin/sh\r'));self.assertEqual(bytes(s.tx_data),prefix)
   self.pump(s,146930);self.assertEqual(len(s.tx_data),147267);before=self.snapshot(s)
   if allowed:self.issue(s,b' \n'*96);self.assertEqual(len(s.tx_data),147459);self.assertEqual(s.link.data_bytes,147122)
   else:
    with self.assertRaisesRegex(Exception,'Invalid/unrecorded'):self.issue(s,b' \n'*96)
    self.assertEqual(self.snapshot(s),before)
 def test_real_data_authority_full262144_then262145_refused(self):
  s=self.owner();self.seal(new,s);prefix=len(s.tx_data);self.pump(s,262144)
  self.assertEqual(s.link.data_bytes,262144);self.assertEqual(s.link.acked_bytes,262144);self.assertEqual(len(s.tx_data),prefix+262144)
  self.assertGreater(len(s.tx_data),147456);self.assertLessEqual(len(s.tx_data),278528)
  before=self.snapshot(s)
  with self.assertRaisesRegex(Exception,'network data alphabet/cap'):self.issue(s,b' ')
  self.assertEqual(self.snapshot(s),before);self.assertEqual(len(s.link.chunk_ledger),1366)
 def test_combined278528_bound_independent_from_data_authority(self):
  s=self.owner();self.seal(new,s)
  # Synthetic earlier-ledger state solely for the combined defensive boundary.
  # No claim that native FSM authorizes a16384-byte bootstrap command stream.
  reserve=16384-len(s.tx_data);padding=b' '*reserve
  s.tx_log.write(padding);os.fsync(s.tx_log.fileno());s.tx_data.extend(padding);s.dev.data.extend(padding)
  self.pump(s,262144);self.assertEqual(len(s.tx_data),278528)
  before=self.snapshot(s)
  with self.assertRaisesRegex(Exception,'Invalid/unrecorded'):self.issue(s,b' ')
  self.assertEqual(self.snapshot(s),before)
 def test_combined278529_refusal_precedes_legal_data_grant(self):
  s=self.owner();self.seal(new,s);padding=b' '*(278528-len(s.tx_data));s.tx_log.write(padding);os.fsync(s.tx_log.fileno());s.tx_data.extend(padding);s.dev.data.extend(padding)
  # Data is still zero: the combined ledger bound must reject before authorize.
  self.assertEqual(s.link.data_bytes,0);before=self.snapshot(s)
  with self.assertRaisesRegex(Exception,'Invalid/unrecorded'):self.issue(s,b' ')
  self.assertEqual(self.snapshot(s),before)
 def test_extra_sealed_commands_and_unissued_or_unacked_bytes_refused(self):
  s=self.owner();self.seal(new,s)
  for data in [b'/bin/sh\r',s.proof.command(),new.desktop_command(s.proof.nonce),b'echo FAIL\n',b' \n']:
   before=self.snapshot(s)
   with self.assertRaises(Exception):s.send(data)
   self.assertEqual(self.snapshot(s),before)
  self.issue(s,b' \n',ack=False);before=self.snapshot(s)
  with self.assertRaises(Exception):self.issue(s,b' \n')
  self.assertEqual(self.snapshot(s),before)
 def test_legacy16k_cap_and_seal_unchanged(self):
  for m in (old,new):
   s=self.owner(m,network=False);self.seal(m,s);before=self.snapshot(s)
   with self.assertRaises(Exception):s.send(b' \n')
   self.assertEqual(self.snapshot(s),before)
   s=self.owner(m,network=False);before=self.snapshot(s)
   with self.assertRaisesRegex(Exception,'Invalid/unrecorded'):s.send(b' ' * 16385)
   self.assertEqual(self.snapshot(s),before)

if __name__=='__main__':unittest.main(verbosity=2)
