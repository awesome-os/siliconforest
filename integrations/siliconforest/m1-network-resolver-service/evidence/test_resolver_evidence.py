"""Synthetic saved evidence/mutants only; no DNS, M1 or daemon execution."""
from pathlib import Path
import copy
import hashlib
import json
import re
import struct
import sys
import unittest
from unittest.mock import patch
import resolver_evidence as e

ROOT=Path(__file__).resolve().parents[4]
OLDTEST=ROOT/'runs/m1-desktop-010/test_default_evidence.py'
assert hashlib.sha256(OLDTEST.read_bytes()).hexdigest()=='939446da477eb55ff460ea527d3f025ec54347002509135f642536e8e24041a0'
sys.path.insert(0,str(OLDTEST.parent))
from test_default_evidence import fixture as route_fixture
NONCE='0123456789abcdef'*3
A,B='104.20.23.154','172.66.147.243'


def request():
    return (b'GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: darwin-lab-egress/1\r\n'
       b'Accept-Encoding: identity\r\nX-Darwin-Lab-Nonce: '+NONCE.encode()+b'\r\nConnection: close\r\n\r\n')


def output(addresses=None):
    return (json.dumps(dict(version=1,nonce=NONCE,name='example.com',pid=16,status='PASS',gai_rc=0,
        system_errno=0,error='none',address_list_complete=True,addresses=addresses or [B,A,B]),separators=(',',':'))+'\n').encode()


def child(prefix,kind,pid,stdout=b'',stderr=b''):
    values=dict(kind=kind,pid=pid,created=1,reaped=1,lost=0,error=0,wait_status=1024 if kind==1 else 0,
        output_bytes=len(stdout),stderr_bytes=len(stderr),output_eof=1,stderr_eof=1,term_sent=1 if kind==1 else 0,
        kill_sent=0,stop_requested=1 if kind==1 else 0,started_ms=1000 if kind==1 else 2000,last_ms=30000 if kind==1 else 8000)
    raw=' '.join(f'{key}={value}' for key,value in values.items()).encode()
    spawn=struct.pack('<II6i6I',1,10,pid,0,0,0,0,0,0,0x400e,0x400e,1,4,0)
    result=[dict(kind=prefix,data=raw),dict(kind=prefix+'_spawn',data=spawn)]
    if stdout:result.append(dict(kind=prefix+'_stdout',data=stdout))
    if stderr:result.append(dict(kind=prefix+'_stderr',data=stderr))
    return result


def fixture():
    old=route_fixture();items=[];inserted=False
    body=b'<h1>Example Domain</h1>'
    response=b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: '+str(len(body)).encode()+b'\r\nConnection: close\r\n\r\n'+body
    for item in old:
        if item['kind'] in ('dns_query','http_response'):
            if not inserted:
                items+=child('probe_child',2,16,output())
                items+=[dict(kind='http_request',data=request()),dict(kind='http_response',data=response)]
                items+=child('mdns_child',1,14,stderr=b'mdnsd starting\nmdnsd stopping\n')
                inserted=True
            continue
        items.append(item)
    facts=dict(status=200,body_bytes=len(body),header_bytes=response.index(b'\r\n\r\n')+4)
    backend=dict(domain='example.com',answer_ipv4=[A,B],selected_ipv4=B,selected_answer_index=1,
        permitted_tcp_port=80,request_expected_hex=request().hex(),response_hex=response.hex(),http=facts,
        complete=True,request_complete=True,request_observed_contiguous_bytes=len(request()),
        request_observed_covered_bytes=len(request()),response_contiguous_bytes=len(response),
        response_highest_bytes=len(response),response_fin_offset=len(response))
    return items,backend


def capture(items,nonce=NONCE):
    raw=bytearray(b'unrelated kernel diagnostic\r\r\n')
    for item in items:
        data=item['data']
        for offset in range(0,max(1,len(data)),64):
            raw.extend((f'DL29_OBSERVE_{nonce} kind={item["kind"]} total={len(data)} offset={offset} hex={data[offset:offset+64].hex()}\r\r\n').encode())
    return bytes(raw)


def select(items,kind):return next(x for x in items if x['kind']==kind)

def metadata(items,kind,key,value):
    item=select(items,kind)
    item['data']=re.sub(key.encode()+rb'=[^ ]+',f'{key}={value}'.encode(),item['data'])

def stdout_change(items,change=None,raw=None):
    item=select(items,'probe_child_stdout')
    if raw is None:
        parsed=json.loads(item['data']);change(parsed)
        raw=(json.dumps(parsed,separators=(',',':'))+'\n').encode()
    item['data']=raw;metadata(items,'probe_child','output_bytes',len(raw))


class Tests(unittest.TestCase):
    def evaluate(self,items,backend):return e.native_service(e.observations(capture(items),NONCE),backend)
    def rejected(self,items,backend):
        result=self.evaluate(items,backend)
        self.assertEqual(set(result['checks']),set(e.CHECK_KEYS))
        self.assertFalse(all(result['checks'].values()),result);json.dumps(result)
        return result
    def test_valid_reordered_API_duplicates_preserved_and_full_route_checks(self):
        items,backend=fixture();result=self.evaluate(items,backend)
        self.assertTrue(all(result['checks'].values()),result);json.dumps(result)
        self.assertEqual(result['details']['resolver']['addresses'],[B,A,B])
        self.assertEqual(result['details']['selected']['wire_answer_index'],1)
        self.assertFalse(result['details']['not_evaluated'])
        self.assertEqual(len(result['checks']),24)
    def test_frozen_default_hash_failure_is_explicit(self):
        items,backend=fixture()
        with patch.object(e,'DEFAULT_SHA','0'*64):result=self.rejected(items,backend)
        self.assertIn('hash',result['details']['error']);self.assertFalse(result['checks']['frozen_default_decoder'])
    def test_observation_chunks_nonce_caps_unknown_and_boundary(self):
        items,_=fixture();raw=capture(items)
        for bad in (raw[:-20],raw.replace(b'offset=64',b'offset=65',1),raw.replace(NONCE.encode(),b'f'*48,1),
                    raw.replace(b'DL29_OBSERVE_',b'DL28_OBSERVE_',1),raw+b'DL29_DNS_BOUNDARY_'+NONCE.encode()+b' frames=2 bytes=1\n'):
            with self.assertRaises(ValueError):e.observations(bad,NONCE)
        for kind,size in [('probe_child_stdout',8193),('mdns_child_stderr',65537),('http_response',4097),('unknown',1)]:
            with self.assertRaises(ValueError):e.observations(capture([dict(kind=kind,data=b'x'*size)]),NONCE)
        raw=capture([dict(kind='mdns_child_stderr',data=bytes(range(256))*256)])
        self.assertEqual(len(e.observations(raw,NONCE)[0]['data']),65536)
        with self.assertRaises(ValueError):e.observations(raw*3,NONCE)
    def test_CR_normalization_preserves_encoded_payload_CR(self):
        data=b'x\ry';raw=capture([dict(kind='mdns_child_stderr',data=data)])
        self.assertEqual(e.observations(raw,NONCE)[0]['data'],data)
    def test_missing_or_duplicate_every_child_record(self):
        for kind in ('probe_child','probe_child_spawn','probe_child_stdout','mdns_child','mdns_child_spawn','mdns_child_stderr'):
            items,backend=fixture();items.remove(select(items,kind));self.rejected(items,backend)
            items,backend=fixture();items.append(copy.deepcopy(select(items,kind)));self.rejected(items,backend)
    def test_probe_wait_error_FD_EOF_signal_and_capture_count(self):
        for key,value in [('created',0),('reaped',0),('lost',1),('error',1),('wait_status',9),('output_eof',0),
                          ('stderr_eof',0),('term_sent',1),('kill_sent',1),('stop_requested',1),('output_bytes',1),('stderr_bytes',1)]:
            items,backend=fixture();metadata(items,'probe_child',key,value);self.rejected(items,backend)
    def test_daemon_reap4_TERM_noKILL_and_stdio(self):
        for key,value in [('wait_status',0),('wait_status',4),('wait_status',9),('term_sent',0),('kill_sent',1),
                          ('stop_requested',0),('reaped',0),('lost',1),('error',1),('output_eof',0),('stderr_eof',0),('stderr_bytes',1)]:
            items,backend=fixture();metadata(items,'mdns_child',key,value);self.rejected(items,backend)
    def test_spawn_every_field_mutation_and_truncation(self):
        for kind in ('probe_child_spawn','mdns_child_spawn'):
            for offset in range(0,56,4):
                items,backend=fixture();item=select(items,kind);data=bytearray(item['data']);data[offset]^=1;item['data']=bytes(data);self.rejected(items,backend)
            for size in range(56):
                items,backend=fixture();item=select(items,kind);item['data']=item['data'][:size];self.rejected(items,backend)
    def test_distinct_spawn_and_metadata_PIDs(self):
        items,backend=fixture();metadata(items,'probe_child','pid',14)
        item=select(items,'probe_child_spawn');data=bytearray(item['data']);struct.pack_into('<i',data,8,14);item['data']=bytes(data)
        stdout_change(items,lambda r:r.update(pid=14));self.rejected(items,backend)
        items,backend=fixture();stdout_change(items,lambda r:r.update(pid=999));self.rejected(items,backend)
    def test_strict_probe_JSON_unique_keys_types_counts_order_and_nonce(self):
        mutations=[lambda r:r.update(version=True),lambda r:r.update(pid=True),lambda r:r.update(gai_rc=False),
                   lambda r:r.update(system_errno=1),lambda r:r.update(status='FAILED'),lambda r:r.update(error='other'),
                   lambda r:r.update(address_list_complete=False),lambda r:r.update(nonce='f'*48),
                   lambda r:r.update(name='other.example'),lambda r:r.update(addresses=[]),
                   lambda r:r.update(addresses=[A]*33),lambda r:r.update(addresses=['104.020.23.154']),
                   lambda r:r.update(addresses=[True]),lambda r:r.update(extra=1)]
        for change in mutations:
            items,backend=fixture();stdout_change(items,change);self.rejected(items,backend)
        for suffix in (b' ',b'\n',b'junk'):
            items,backend=fixture();raw=select(items,'probe_child_stdout')['data']+suffix;stdout_change(items,raw=raw);self.rejected(items,backend)
        items,backend=fixture();raw=select(items,'probe_child_stdout')['data'].replace(b'{',b'{"version":1,',1);stdout_change(items,raw=raw);self.rejected(items,backend)
        items,backend=fixture();parsed=json.loads(select(items,'probe_child_stdout')['data']);raw=(json.dumps(dict(reversed(list(parsed.items()))),separators=(',',':'))+'\n').encode();stdout_change(items,raw=raw);self.rejected(items,backend)
    def test_probe_stderr_even_one_byte_and_noncanonical_metadata(self):
        items,backend=fixture();at=next(i for i,x in enumerate(items) if x['kind']=='http_request');items.insert(at,dict(kind='probe_child_stderr',data=b'x'));metadata(items,'probe_child','stderr_bytes',1);self.rejected(items,backend)
        items,backend=fixture();metadata(items,'mdns_child','wait_status','01024');self.rejected(items,backend)
    def test_API_first_must_match_actual_TCP_destination_not_wirefirst(self):
        for changes in (dict(selected_ipv4=A,selected_answer_index=0),dict(selected_answer_index=0),
                        dict(answer_ipv4=[A]),dict(answer_ipv4=['127.0.0.1',B]),dict(permitted_tcp_port=443)):
            items,backend=fixture();backend.update(changes);self.rejected(items,backend)
        items,backend=fixture();stdout_change(items,lambda r:r.update(addresses=[B,'8.8.8.8']));self.rejected(items,backend)
    def test_missing_backend_fields_stay_failed_without_fake_defaults(self):
        for key in fixture()[1]:
            items,backend=fixture();del backend[key];self.rejected(items,backend)
        items,backend=fixture();self.rejected(items,None)
        items,backend=fixture();backend['selected_ipv4']=b'bad';self.rejected(items,backend)
    def test_HTTP_byte_equality_nonce_and_status_framing_content(self):
        items,backend=fixture();select(items,'http_request')['data']=select(items,'http_request')['data'].replace(NONCE.encode(),b'f'*48);self.rejected(items,backend)
        items,backend=fixture();backend['response_hex']=backend['response_hex'][:-2];self.rejected(items,backend)
        for before,after in ((b'200 OK',b'500 OK'),(b'text/html',b'text/plain'),(b'Example Domain',b'Wrong Content!'),(b'Content-Length: 23',b'Content-Length: 22')):
            items,backend=fixture();item=select(items,'http_response');item['data']=item['data'].replace(before,after);backend['response_hex']=item['data'].hex();self.rejected(items,backend)
    def test_backend_HTTP_fact_types_are_exact(self):
        for facts in ({'status':200.0,'body_bytes':23,'header_bytes':83},dict(fixture()[1]['http'],status=200.0),dict(fixture()[1]['http'],extra=1)):
            items,backend=fixture();backend['http']=facts;self.rejected(items,backend)

    def test_source_order_probe_before_HTTP_daemon_afterHTTP_beforecleanup(self):
        items,backend=fixture();probe=[x for x in items if x['kind'].startswith('probe_child')];items=[x for x in items if not x['kind'].startswith('probe_child')];at=next(i for i,x in enumerate(items) if x['kind']=='mdns_child');items[at:at]=probe;self.rejected(items,backend)
        items,backend=fixture();group=[x for x in items if x['kind'].startswith('mdns_child')];items=[x for x in items if not x['kind'].startswith('mdns_child')];items+=group;self.rejected(items,backend)
        items,backend=fixture();metadata(items,'probe_child','started_ms',999);self.rejected(items,backend)
    def test_route_adapter_cannot_ignore_bad_default_or_inventory(self):
        for kind,offset in [('default_add_reply',8),('final_ipv4_route',4)]:
            items,backend=fixture();item=select(items,kind);raw=bytearray(item['data']);raw[offset]^=1;item['data']=bytes(raw);self.rejected(items,backend)
        items,backend=fixture();items.remove(select(items,'final_ipv4_route_count'));self.rejected(items,backend)
    def test_direct_decoded_session_digest_and_position_mutants(self):
        items,backend=fixture();obs=e.observations(capture(items),NONCE)
        for key,value in [('nonce','f'*48),('sha256','0'*64),('last',b'bad'),('total',False)]:
            changed=copy.deepcopy(obs);changed[0][key]=value;result=e.native_service(changed,backend);self.assertFalse(all(result['checks'].values()));json.dumps(result)


if __name__=='__main__':unittest.main(verbosity=2)
