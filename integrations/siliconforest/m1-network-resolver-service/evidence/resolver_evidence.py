"""Independent saved DL29 native-service evidence. No target/network execution.

Caller supplies backend policy.summary() only after closed-record hash/packet
validation, plus response_hex from the authenticated closed http-response.bin.
The frozen default-route decoder is byte-verified and receives decoded records.
"""
import hashlib
import importlib.util
import ipaddress
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[4]
DEFAULT_PATH = ROOT/'runs/m1-desktop-010/default_evidence.py'
DEFAULT_SHA = 'b6cb9bbaae47bdddf8b0b001391985e823654e34292c1ac2f3a5cceff3f87b60'
ROUTE_KEYS = ('observation_objects','owned_identity','request_pid','default_add_get_sequence',
    'owned_peer_local_sequence','route_inventory_complete','baseline_no_default',
    'baseline_restored','retired_index_absent','source_order','no_dns_phase_boundary')
ROUTE_KINDS = {'process','get_request','get_response','default_get_request','default_get_reply',
    'default_add_request','default_add_reply','default_write_result','default_interface',
    'baseline_interface','final_cleanup','baseline_ipv4_route','baseline_ipv4_route_count',
    'final_ipv4_route','final_ipv4_route_count','http_request','http_response'}
CHILD_KINDS = {prefix+suffix for prefix in ('probe_child','mdns_child')
               for suffix in ('','_spawn','_stdout','_stderr')}
CAPS = {'probe_child_stdout':8192,'mdns_child_stdout':8192,
        'probe_child_stderr':65536,'mdns_child_stderr':65536}
CHECK_KEYS = ('frozen_default_decoder','observation_records','process_context') + tuple('route_'+k for k in ROUTE_KEYS) + (
    'probe_child','mdns_child','distinct_owned_pids','probe_stdout','api_dns_membership',
    'selected_destination','http_request','http_response','backend_http_complete','child_source_order')


def observations(raw, nonce):
    if type(raw) is not bytes or len(raw)>32*1024*1024 or type(nonce) is not str or not re.fullmatch('[0-9a-f]{48}',nonce):
        raise ValueError('raw capture cap/type/nonce')
    pattern = rb'DL29_OBSERVE_'+nonce.encode()+rb' kind=([a-z0-9_]{1,63}) total=([0-9]{1,5}) offset=([0-9]{1,5}) hex=([0-9a-f]{0,128})'
    out,current,used=[],None,0
    for number,line in enumerate(raw.replace(b'\r',b'').split(b'\n')):
        if any(prefix in line for prefix in (b'DL29_DNS_BOUNDARY_',b'DL28_OBSERVE_',b'DL27_OBSERVE_',b'DL26_OBSERVE_')):
            raise ValueError('foreign observation/forbidden boundary')
        if b'DL29_OBSERVE_' not in line:
            continue
        match=re.fullmatch(pattern,line)
        if not match:
            raise ValueError('observation syntax/session')
        kind,total,offset=match[1].decode(),int(match[2]),int(match[3])
        if kind not in ROUTE_KINDS|CHILD_KINDS or total>CAPS.get(kind,4096) or offset>total or len(match[4])%2:
            raise ValueError('observation kind/cap/span')
        used+=len(line)+1
        if used>524288:
            raise ValueError('observation aggregate cap')
        data=bytes.fromhex(match[4].decode())
        if offset==0:
            if current is not None:
                raise ValueError('unfinished observation')
            current=dict(kind=kind,total=total,data=bytearray(),first=number,last=number,nonce=nonce)
        if (current is None or current['kind']!=kind or current['total']!=total or
                len(current['data'])!=offset or len(data)!=min(64,total-offset)):
            raise ValueError('observation sequence/span')
        current['data'].extend(data);current['last']=number
        if len(current['data'])==total:
            current['data']=bytes(current['data']);out.append(current);current=None
    if current is not None:
        raise ValueError('observation EOF')
    return out


def _default():
    if hashlib.sha256(DEFAULT_PATH.read_bytes()).hexdigest()!=DEFAULT_SHA:
        raise ValueError('frozen default decoder hash')
    spec=importlib.util.spec_from_file_location('dl29_frozen_default_evidence',DEFAULT_PATH)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


def _objects(obs):
    if type(obs) is not list or not obs:
        raise ValueError('observation list')
    nonces=set();last=-1;used=0
    for item in obs:
        if (type(item) is not dict or item.get('kind') not in ROUTE_KINDS|CHILD_KINDS or
                type(item.get('data')) is not bytes or type(item.get('total')) is not int or
                item['total']!=len(item['data']) or item['total']>CAPS.get(item['kind'],4096) or
                type(item.get('first')) is not int or type(item.get('last')) is not int or
                not last<item['first']<=item['last'] or type(item.get('nonce')) is not str or
                not re.fullmatch('[0-9a-f]{48}',item['nonce'])):
            raise ValueError('decoded observation object/session/order')
        last=item['last'];nonces.add(item['nonce'])
        if 'sha256' in item and item['sha256']!=hashlib.sha256(item['data']).hexdigest():
            raise ValueError('observation digest')
        for offset in range(0,max(1,item['total']),64):
            used+=(len('DL29_OBSERVE_')+48+len(' kind='+item['kind'])+
                len(' total='+str(item['total'])+' offset='+str(offset)+' hex=')+
                2*min(64,item['total']-offset)+1)
    if len(nonces)!=1 or used>524288:
        raise ValueError('decoded observation session/aggregate cap')
    return nonces.pop()


def _one(obs,kind):
    found=[(i,x) for i,x in enumerate(obs) if x['kind']==kind]
    if len(found)!=1:
        raise ValueError('missing/duplicate '+kind)
    return found[0]


def _digest(item):
    return dict(kind=item['kind'],bytes=len(item['data']),sha256=hashlib.sha256(item['data']).hexdigest(),
                first=item['first'],last=item['last'])


def _child(obs,prefix,kind):
    position,item=_one(obs,prefix)
    pattern=(rb'kind=([12]) pid=([1-9][0-9]{0,9}) created=([01]) reaped=([01]) lost=([01]) error=(-?[0-9]{1,10}) wait_status=(-?[0-9]{1,10})'
      rb' output_bytes=([0-9]{1,5}) stderr_bytes=([0-9]{1,5}) output_eof=([01]) stderr_eof=([01]) term_sent=([01]) kill_sent=([01]) stop_requested=([01])'
      rb' started_ms=([1-9][0-9]{0,19}) last_ms=([1-9][0-9]{0,19})')
    match=re.fullmatch(pattern,item['data'])
    if not match:
        raise ValueError('child metadata shape')
    names=('kind','pid','created','reaped','lost','error','wait_status','output_bytes','stderr_bytes',
           'output_eof','stderr_eof','term_sent','kill_sent','stop_requested','started_ms','last_ms')
    child=dict(zip(names,map(int,match.groups())))
    if item['data']!=' '.join(f'{key}={child[key]}' for key in names).encode():
        raise ValueError('child noncanonical decimal')
    if (child['kind']!=kind or not 2<=child['pid']<=0x7fffffff or child['created']!=1 or child['reaped']!=1 or
        child['lost'] or child['error'] or child['output_eof']!=1 or child['stderr_eof']!=1 or
        child['output_bytes']>8192 or child['stderr_bytes']>65536 or
        not 0<child['started_ms']<=child['last_ms']<=0xffffffffffffffff or child['kill_sent']):
        raise ValueError('child ownership/error/EOF')
    if kind==2:
        if child['wait_status'] or child['term_sent'] or child['stop_requested'] or child['stderr_bytes']:
            raise ValueError('probe did not exit cleanly without signals/stderr')
    elif child['wait_status']!=4<<8 or child['term_sent']!=1 or child['stop_requested']!=1:
        raise ValueError('daemon did not TERM/reap normal EINTR4')
    spawn_pos,spawn=_one(obs,prefix+'_spawn')
    if len(spawn['data'])!=56:
        raise ValueError('spawn ABI length')
    fields=struct.unpack('<II6i6I',spawn['data'])
    expected=(1,10,child['pid'],0,0,0,0,0,0,0x400e,0x400e,1,4,0)
    if fields!=expected:
        raise ValueError('spawn PID/stage/errors/flags/FD dup count')
    stream={};group=[position,spawn_pos];expected_kinds=[prefix,prefix+'_spawn']
    for suffix,count in (('_stdout',child['output_bytes']),('_stderr',child['stderr_bytes'])):
        entries=[(i,x) for i,x in enumerate(obs) if x['kind']==prefix+suffix]
        if count:
            if len(entries)!=1 or len(entries[0][1]['data'])!=count:
                raise ValueError('child capture bytes missing/mismatched')
            group.append(entries[0][0]);expected_kinds.append(prefix+suffix)
            stream[suffix]=entries[0][1]['data']
        elif entries:
            raise ValueError('unexpected zero-byte child stream observation')
        else:
            stream[suffix]=b''
    if group!=list(range(position,position+len(group))) or [obs[i]['kind'] for i in group]!=expected_kinds:
        raise ValueError('child observation group order')
    child.update(position=position,last_position=group[-1],spawn_sha256=hashlib.sha256(spawn['data']).hexdigest(),
        stdout_hex=stream['_stdout'].hex(),stdout_sha256=hashlib.sha256(stream['_stdout']).hexdigest(),
        stderr_hex=stream['_stderr'].hex(),stderr_sha256=hashlib.sha256(stream['_stderr']).hexdigest())
    return child,stream['_stdout']


def _unique(pairs):
    out={}
    for key,value in pairs:
        if key in out:
            raise ValueError('duplicate JSON key')
        out[key]=value
    return out


def _ipv4(text):
    if type(text) is not str:
        raise ValueError('IPv4 text type')
    address=ipaddress.IPv4Address(text)
    if str(address)!=text:
        raise ValueError('noncanonical IPv4 text')
    return address


def _probe(raw,nonce,pid):
    if not 1<=len(raw)<=8192 or not raw.endswith(b'\n') or raw.count(b'\n')!=1:
        raise ValueError('probe JSON framing/cap')
    record=json.loads(raw.decode('ascii'),object_pairs_hook=_unique,
                      parse_constant=lambda _:(_ for _ in ()).throw(ValueError('JSON nonfinite')))
    keys=['version','nonce','name','pid','status','gai_rc','system_errno','error','address_list_complete','addresses']
    if (type(record) is not dict or list(record)!=keys or
        any(type(record[k]) is not int for k in ('version','pid','gai_rc','system_errno')) or
        record['version']!=1 or record['nonce']!=nonce or record['name']!='example.com' or record['pid']!=pid or
        record['status']!='PASS' or record['gai_rc'] or record['system_errno'] or record['error']!='none' or
        record['address_list_complete'] is not True or type(record['addresses']) is not list or
        not 1<=len(record['addresses'])<=32 or raw!=(json.dumps(record,separators=(',',':'))+'\n').encode('ascii')):
        raise ValueError('probe JSON exact success schema/session')
    for address in record['addresses']:
        _ipv4(address)
    return record


def _unhex(value,cap,label):
    if type(value) is not str or len(value)%2 or len(value)>2*cap or not re.fullmatch('[0-9a-f]*',value):
        raise ValueError(label+' hex/cap')
    return bytes.fromhex(value)


def _request(nonce):
    return (b'GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: darwin-lab-egress/1\r\n'
            b'Accept-Encoding: identity\r\nX-Darwin-Lab-Nonce: '+nonce.encode()+b'\r\nConnection: close\r\n\r\n')


def _http(raw):
    if not 1<=len(raw)<=2048:
        raise ValueError('HTTP cap')
    end=raw.find(b'\r\n\r\n')
    if end<0:
        raise ValueError('HTTP incomplete header')
    lines=raw[:end].split(b'\r\n');status=lines[0].split(b' ',2)
    if (len(status)<2 or status[0] not in (b'HTTP/1.0',b'HTTP/1.1') or status[1]!=b'200' or
            any(c<32 or c>126 for c in lines[0])):
        raise ValueError('HTTP observed status')
    headers={}
    for line in lines[1:]:
        if line[:1] in (b' ',b'\t') or b':' not in line:
            raise ValueError('HTTP header shape')
        key,value=line.split(b':',1);key=key.lower();value=value.strip(b' \t')
        if not re.fullmatch(b'[a-z0-9-]+',key) or key in headers or any(c<32 and c!=9 or c>126 for c in value):
            raise ValueError('HTTP header name/duplicate/value')
        headers[key]=value
    if headers.get(b'content-encoding',b'identity').lower()!=b'identity' or headers.get(b'content-type',b'').split(b';',1)[0].strip().lower()!=b'text/html':
        raise ValueError('HTTP encoding/type')
    body=raw[end+4:]
    if b'transfer-encoding' in headers:
        if b'content-length' in headers or headers[b'transfer-encoding'].lower()!=b'chunked':
            raise ValueError('HTTP ambiguous transfer framing')
        at=0;decoded=bytearray()
        while True:
            line_end=body.find(b'\r\n',at)
            if line_end<0 or not re.fullmatch(b'[0-9a-fA-F]{1,8}',body[at:line_end]):
                raise ValueError('HTTP chunk size')
            count=int(body[at:line_end],16);at=line_end+2
            if count>2048 or len(decoded)+count>2048:
                raise ValueError('HTTP chunk cap')
            if count==0:
                if body[at:]!=b'\r\n':
                    raise ValueError('HTTP trailing/truncated chunk')
                body=bytes(decoded);break
            if body[at+count:at+count+2]!=b'\r\n':
                raise ValueError('HTTP truncated chunk data')
            decoded.extend(body[at:at+count]);at+=count+2
    elif b'content-length' in headers:
        value=headers[b'content-length']
        if not re.fullmatch(b'[0-9]{1,6}',value) or int(value)!=len(body):
            raise ValueError('HTTP exact content length')
    if b'Example Domain' not in body:
        raise ValueError('HTTP expected observed content')
    return dict(status=200,body_bytes=len(body),header_bytes=end+4)


def native_service(obs,backend_egress):
    checks=dict.fromkeys(CHECK_KEYS,False);evaluated=set();details={}
    def mark(key,value):
        evaluated.add(key);checks[key]=value is True
    try:
        mark('frozen_default_decoder',False);decoder=_default();mark('frozen_default_decoder',True)
        mark('observation_records',False);nonce=_objects(obs);mark('observation_records',True)
        details['nonce']=nonce
        routes=[item for item in obs if item['kind'] in ROUTE_KINDS]
        mark('process_context',False);process_ok,rows=decoder.process_context(routes);mark('process_context',process_ok)
        details['process_rows']=rows
        route=decoder.default_lifecycle(routes)
        if set(route['checks'])!=set(ROUTE_KEYS) or any(type(value) is not bool for value in route['checks'].values()):
            raise ValueError('frozen route decoder schema')
        for key,value in route['checks'].items():
            mark('route_'+key,value)
        details['route']=route['details']
        mark('probe_child',False);probe,stdout=_child(obs,'probe_child',2);mark('probe_child',True)
        mark('mdns_child',False);daemon,_=_child(obs,'mdns_child',1);mark('mdns_child',True)
        details['probe_child'],details['mdns_child']=probe,daemon
        parent=route['details'].get('request_pid')
        old_pids={row['pid'] for row in rows}
        mark('distinct_owned_pids',probe['pid']!=daemon['pid'] and probe['pid'] not in old_pids and daemon['pid'] not in old_pids and parent is not None)
        mark('probe_stdout',False);record=_probe(stdout,nonce,probe['pid']);mark('probe_stdout',True)
        details['resolver']=record
        if type(backend_egress) is not dict:
            raise ValueError('missing authenticated backend egress object')
        answers=backend_egress.get('answer_ipv4')
        mark('api_dns_membership',False)
        if type(answers) is not list or not 1<=len(answers)<=32:
            raise ValueError('missing/bounded validated DNS A set')
        for address in answers:
            ip=_ipv4(address)
            if not ip.is_global or ip.is_multicast or ip.is_reserved or ip.is_loopback or ip.is_link_local or ip.is_unspecified:
                raise ValueError('nonglobal DNS A permission')
        mark('api_dns_membership',all(address in answers for address in record['addresses']))
        selected=backend_egress.get('selected_ipv4');index=backend_egress.get('selected_answer_index')
        mark('selected_destination',False)
        _ipv4(selected)
        if type(index) is not int:
            raise ValueError('backend selected index type')
        mark('selected_destination',selected==record['addresses'][0] and selected in answers and type(index) is int and
             0<=index<len(answers) and index==answers.index(selected) and backend_egress.get('permitted_tcp_port')==80 and type(backend_egress.get('permitted_tcp_port')) is int)
        details['selected']=dict(first_api_ipv4=record['addresses'][0],backend_ipv4=selected,wire_answer_index=index,answers=answers)
        request_pos,request_item=_one(obs,'http_request');response_pos,response_item=_one(obs,'http_response')
        expected=_request(nonce)
        mark('http_request',False)
        raw_request=_unhex(backend_egress.get('request_expected_hex'),2048,'backend request')
        mark('http_request',request_item['data']==raw_request==expected)
        mark('http_response',False)
        raw_response=_unhex(backend_egress.get('response_hex'),2048,'authenticated backend response')
        facts=_http(response_item['data'])
        backend_http=backend_egress.get('http')
        mark('http_response',response_item['data']==raw_response and type(backend_http) is dict and
             set(backend_http)==set(facts) and all(type(value) is int for value in backend_http.values()) and backend_http==facts)
        details['http']=dict(request=_digest(request_item),response=_digest(response_item),facts=facts)
        mark('backend_http_complete',backend_egress.get('domain')=='example.com' and backend_egress.get('complete') is True and
             backend_egress.get('request_complete') is True and
             all(type(backend_egress.get(k)) is int and backend_egress[k]==len(expected) for k in ('request_observed_contiguous_bytes','request_observed_covered_bytes')) and
             all(type(backend_egress.get(k)) is int and backend_egress[k]==len(raw_response) for k in ('response_contiguous_bytes','response_highest_bytes','response_fin_offset')))
        active_pos,_=_one(obs,'default_interface');final_pos,_=_one(obs,'final_cleanup')
        mark('child_source_order',active_pos<probe['position']<=probe['last_position']<request_pos<response_pos<
             daemon['position']<=daemon['last_position']<final_pos and daemon['started_ms']<=probe['started_ms'] and
             probe['last_ms']<=daemon['last_ms'])
    except (ValueError,TypeError,KeyError,IndexError,OverflowError,UnicodeError,OSError) as error:
        details['error']=str(error)
    details['not_evaluated']=[key for key in CHECK_KEYS if key not in evaluated]
    details['scope']='Source-bound saved evidence only; caller separately qualifies DNS/packet hashes, codec/credit/outerprocessstatus and Linux recovery.'
    return dict(checks=checks,details=details)
