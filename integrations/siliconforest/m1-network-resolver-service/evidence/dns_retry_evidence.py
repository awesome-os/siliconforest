"""Independent bounded DNS retry evidence; original parser, no network/I/O.

Inputs are ordered IPv4 UDP packet bytes from authenticated closed streams and
backend event records. Recorded backend timestamps are not independent wire clocks.
Only parsed RR-header TTL words are normalized; DNS ID/question/other bytes stay.
"""
from collections import Counter, defaultdict
import hashlib
import ipaddress
import math
import json
import re
import struct

GUEST=b'\x0a\x00\x02\x0f'
DNS=b'\x0a\x00\x02\x03'
QUESTION=b'\x07example\x03com\0\0\x01\0\x01'
POLICY='pure_example_A_multiset_else_wire_identity_nonincreasing_key_TTLs_original_lease'
CHECK_KEYS=('wire_packet_bounds','query_identity','reply_dns_structure','tuple_and_id',
 'retry_budgets','non_ttl_identity','ttl_nonincrease','original_answers_preserved',
 'backend_query_events','backend_reply_events','backend_original_records',
 'reply_window','original_lease','source_policy_revision')


def _checksum(raw):
    if len(raw)%2:raw+=b'\0'
    value=sum(int.from_bytes(raw[i:i+2],'big') for i in range(0,len(raw),2))
    while value>>16:value=(value&65535)+(value>>16)
    return (~value)&65535


def _packet(raw,reply=False):
    if type(raw) is not bytes or not 28<=len(raw)<=1500 or raw[0]!=0x45:
        raise ValueError('IPv4 fixed header/type/cap')
    if int.from_bytes(raw[2:4],'big')!=len(raw) or _checksum(raw[:20]) or int.from_bytes(raw[6:8],'big')&0xbfff or raw[9]!=17:
        raise ValueError('IPv4 length/checksum/fragments/UDP')
    if raw[12:16]!=(DNS if reply else GUEST) or raw[16:20]!=(GUEST if reply else DNS):
        raise ValueError('IPv4 DNS tuple')
    udp=raw[20:];sp,dp,size,crc=struct.unpack_from('!4H',udp)
    if size!=len(udp) or not sp or not dp:
        raise ValueError('UDP length/ports')
    if reply:
        if sp!=53 or dp<1024:raise ValueError('reply UDP endpoint')
    elif dp!=53 or sp<1024:raise ValueError('query UDP endpoint')
    pseudo=raw[12:20]+b'\0\x11'+len(udp).to_bytes(2,'big')
    if crc and _checksum(pseudo+udp):raise ValueError('UDP checksum')
    return dict(source_port=sp,destination_port=dp,payload=udp[8:],packet_sha256=hashlib.sha256(raw).hexdigest(),udp_checksum_present=bool(crc))


def _query(raw):
    if type(raw) is not bytes or len(raw)!=29:
        raise ValueError('DNS query cap')
    ident,flags,qd,an,ns,ar=struct.unpack_from('!6H',raw)
    if not ident or (flags,qd,an,ns,ar)!=(0x100,1,0,0,0) or raw[12:]!=QUESTION:
        raise ValueError('DNS selected question/ID/flags')
    return ident


def _name(raw,start):
    labels=[];seen=set();cursor=start;end=None
    for _ in range(64):
        if cursor in seen or not 12<=cursor<len(raw):raise ValueError('DNS name bounds/cycle')
        seen.add(cursor);length=raw[cursor]
        if length&0xc0==0xc0:
            if cursor+1>=len(raw):raise ValueError('DNS pointer truncated')
            target=((length&63)<<8)|raw[cursor+1]
            if not 12<=target<cursor:raise ValueError('DNS pointer not backward')
            if end is None:end=cursor+2
            cursor=target;continue
        if length&0xc0 or length>63 or cursor+1+length>len(raw):raise ValueError('DNS label encoding')
        cursor+=1
        if not length:
            name=b'.'.join(labels).lower()
            if len(name)>253:raise ValueError('DNS name cap')
            return name.decode('ascii'),cursor if end is None else end
        label=raw[cursor:cursor+length]
        if any(c<33 or c>126 or c==46 for c in label):raise ValueError('DNS label character')
        labels.append(label);cursor+=length
    raise ValueError('DNS decompression budget')


def _global(raw):
    ip=ipaddress.IPv4Address(raw)
    if not ip.is_global or ip.is_multicast or ip.is_reserved or ip.is_loopback or ip.is_link_local or ip.is_unspecified:
        raise ValueError('DNS nonglobal A permission')
    return str(ip)


def parse_dns_reply(raw,query):
    """Parse every RR in <=512 bytes; return JSON-safe byte identities/details."""
    ident=_query(query)
    if type(raw) is not bytes or not 12<=len(raw)<=512:raise ValueError('DNS reply size')
    rid,flags,qd,an,ns,ar=struct.unpack_from('!6H',raw)
    if rid!=ident or not flags&0x8000 or flags&0x7a4f or not flags&0x100:
        raise ValueError('DNS reply ID/flags')
    if qd!=1 or not 1<=an<=32 or ns>16 or ar>16 or an+ns+ar>32:
        raise ValueError('DNS section counts')
    name,offset=_name(raw,12)
    if name!='example.com' or raw[12:offset+4]!=query[12:] or raw[offset:offset+4]!=b'\0\1\0\1':
        raise ValueError('DNS question correlation')
    offset+=4;question_end=offset;normalized=bytearray(raw);records=[];semantics=[];wire_rr=[]
    for section,count in (('answer',an),('authority',ns),('additional',ar)):
        for index in range(count):
            start=offset;owner,offset=_name(raw,offset)
            if offset+10>len(raw):raise ValueError('DNS RR header truncated')
            kind,klass,ttl,length=struct.unpack_from('!HHIH',raw,offset);ttl_at=offset+4;offset+=10
            finish=offset+length
            if kind==41 or finish>len(raw):raise ValueError('DNS OPT/EDNS or RDATA bounds')
            if section=='answer' and klass!=1:raise ValueError('DNS answer class')
            data=raw[offset:finish];decoded=None;semantic=None
            if kind==1:
                if length!=4:raise ValueError('DNS A RDLENGTH')
                semantic=str(ipaddress.IPv4Address(data))
            elif kind in (2,5,12):
                domain,used=_name(raw,offset)
                if used!=finish:raise ValueError('DNS domain RDATA exact length')
                semantic=domain
                if kind==5:decoded=domain
            elif kind==6:
                first,used=_name(raw,offset);second,used=_name(raw,used)
                if used+20!=finish:raise ValueError('DNS SOA exact RDATA')
                semantic=[first,second,*struct.unpack_from('!5I',raw,used)]
            elif kind in (15,33):
                fixed=2 if kind==15 else 6
                if length<fixed+1:raise ValueError('DNS MX/SRV RDATA bounds')
                domain,used=_name(raw,offset+fixed)
                if used!=finish:raise ValueError('DNS MX/SRV RDATA exact length')
                semantic=[raw[offset:offset+fixed].hex(),domain]
            normalized[ttl_at:ttl_at+4]=bytes(4)
            records.append(dict(section=section,index=index,owner=owner,type=kind,**{'class':klass},
                ttl_offset=ttl_at,ttl=ttl,rdata_hex=data.hex(),decoded_domain=decoded))
            semantics.append(semantic)
            identity=bytearray(raw[start:finish]);identity[ttl_at-start:ttl_at-start+4]=bytes(4)
            wire_rr.append(bytes(identity).hex());records[-1]['normalized_wire_hex']=bytes(identity).hex();offset=finish
    if offset!=len(raw):raise ValueError('DNS trailing data')
    current='example.com';visited=set();cname_ttls=[];answers=[];lease=None
    answer_records=[r for r in records if r['section']=='answer']
    for _ in range(8):
        if current in visited:raise ValueError('DNS CNAME cycle')
        visited.add(current)
        cname=[r for r in answer_records if r['owner']==current and r['type']==5]
        addresses=[r for r in answer_records if r['owner']==current and r['type']==1]
        if cname:
            if addresses or len({r['decoded_domain'] for r in cname})!=1:raise ValueError('DNS CNAME ambiguity')
            cname_ttls.extend(r['ttl'] for r in cname);current=cname[0]['decoded_domain'];continue
        if not 1<=len(addresses)<=32:raise ValueError('DNS missing terminal A set')
        answers=[_global(bytes.fromhex(r['rdata_hex'])) for r in addresses]
        lease=min([r['ttl'] for r in addresses]+cname_ttls+[60])
        if lease==0:raise ValueError('DNS zero authorization TTL')
        break
    if lease is None:raise ValueError('DNS CNAME budget')
    pure=(ns==0 and ar==0 and all(r['owner']=='example.com' and r['type']==1 and r['class']==1 and len(bytes.fromhex(r['rdata_hex']))==4 for r in records))
    return dict(transaction_id=ident,counts=dict(answer=an,authority=ns,additional=ar),
      raw_hex=raw.hex(),raw_sha256=hashlib.sha256(raw).hexdigest(),question_prefix_hex=raw[:question_end].hex(),
      normalized_hex=bytes(normalized).hex(),normalized_sha256=hashlib.sha256(normalized).hexdigest(),
      records=records,rr_semantics=semantics,rr_normalized_hex=wire_rr,
      answer_ipv4=answers,lease_seconds=lease,pure_example_a=pure)


def _key(record):return (record['owner'],record['type'],record['class'],record['rdata_hex'])
def _number(value):
    if type(value) not in (int,float) or not math.isfinite(value) or value<0:raise ValueError('backend finite nonnegative elapsed clock')
    return value

def _same_json(a,b):
    # JSON provenance fields require exact types, not Python bool/int equality.
    return json.dumps(a,sort_keys=True,separators=(',',':'),allow_nan=False)==json.dumps(b,sort_keys=True,separators=(',',':'),allow_nan=False)


def _comparison(parsed):
    """Build the public wire-policy descriptor, independently from packet bytes."""
    records=parsed['records'];order=[list(_key(r)) for r in records]
    if parsed['pure_example_a']:
        groups=defaultdict(list);wire=defaultdict(list)
        for record in records:
            key=_key(record);groups[key].append(record['ttl'])
            wire[key].append(record['normalized_wire_hex'])
        keys=sorted(groups)
        descriptor={'mode':'a_only_answer_multiset',
          'header_question_hex':parsed['question_prefix_hex'],
          'keys_and_counts':[list(k)+[len(groups[k])] for k in keys],
          'per_key_normalized_wire':[sorted(wire[k]) for k in keys]}
        ttls=[sorted(groups[k]) for k in keys]
    else:
        descriptor={'mode':'normalized_wire_identity','wire_hex':parsed['normalized_hex'],
          'semantic_RRs':[{k:v for k,v in r.items() if k!='ttl'} for r in records]}
        ttls=[[r['ttl'] for r in records]]
    wire=json.dumps(descriptor,sort_keys=True,separators=(',',':')).encode('ascii')
    return descriptor,ttls,order,hashlib.sha256(wire).hexdigest()


def dns_retry_evidence(queries,replies,backend_egress):
    """Check actual IPv4 UDP packets against a closed backend egress object.

    Packet ordering and raw provenance are caller obligations. Elapsed times are
    checked backend clock records, not independently measured capture timestamps.
    Missing or malformed inputs fail closed; this function never grades hardware.
    """
    checks=dict.fromkeys(CHECK_KEYS,False);details={'errors':{},'scope':'packet bytes and recorded backend clocks only'}
    evaluated=set()

    def check(key,predicate):
        evaluated.add(key)
        if not predicate:raise ValueError(key)
        checks[key]=True

    active='wire_packet_bounds'
    try:
        check(active,type(queries) is list and type(replies) is list and
              1<=len(queries)<=3 and 1<=len(replies)<=3)
        # Mark only after every actual packet has passed the independent parser.
        checks[active]=False
        qp=[_packet(q) for q in queries];rp=[_packet(r,True) for r in replies]
        checks[active]=True
        details['query_packet_sha256']=[p['packet_sha256'] for p in qp]
        details['reply_packet_sha256']=[p['packet_sha256'] for p in rp]
        active='query_identity';ident=_query(qp[0]['payload'])
        check(active,all(_query(p['payload'])==ident and p['payload']==qp[0]['payload'] and
                        p['source_port']==qp[0]['source_port'] for p in qp))
        active='reply_dns_structure';evaluated.add(active)
        parsed=[parse_dns_reply(p['payload'],qp[0]['payload']) for p in rp]
        checks[active]=True
        active='tuple_and_id'
        check(active,all(p['destination_port']==qp[0]['source_port'] for p in rp))
        active='retry_budgets'
        check(active,type(backend_egress) is dict and
          _same_json(backend_egress.get('dns_attempts'),len(qp)) and
          _same_json(backend_egress.get('dns_duplicates'),len(rp)-1))
        comparisons=[_comparison(p) for p in parsed]
        details['comparison_mode']=comparisons[0][0]['mode']
        details['replies']=[dict(raw_sha256=p['raw_sha256'],normalized_sha256=p['normalized_sha256'],
            rr_order=c[2],ttl_groups=c[1],rr_ttls=[r['ttl'] for r in p['records']],
            changed_byte_offsets=[] if i==0 else [j for j,(a,b) in enumerate(zip(rp[0]['payload'],rp[i]['payload'])) if a!=b],
            answer_order_changed_from_first=c[2]!=comparisons[0][2])
            for i,(p,c) in enumerate(zip(parsed,comparisons))]
        active='non_ttl_identity'
        check(active,all(_same_json(c[0],comparisons[0][0]) for c in comparisons) and
          (parsed[0]['pure_example_a'] or all(p['rr_semantics']==parsed[0]['rr_semantics'] for p in parsed)))
        active='ttl_nonincrease'
        check(active,all(len(now[1])==len(prev[1]) and all(len(a)==len(b) and
          all(x<=y for x,y in zip(a,b)) for a,b in zip(now[1],prev[1]))
          for prev,now in zip(comparisons,comparisons[1:])))
        active='original_answers_preserved';initial=parsed[0]
        same_answers=all((Counter(p['answer_ipv4'])==Counter(initial['answer_ipv4']) if initial['pure_example_a']
                          else p['answer_ipv4']==initial['answer_ipv4']) for p in parsed)
        check(active,same_answers and _same_json(backend_egress.get('answer_ipv4'),initial['answer_ipv4']) and
              _same_json(backend_egress.get('lease_seconds'),initial['lease_seconds']))
        active='backend_query_events'
        events=backend_egress.get('dns_query_events')
        check(active,type(events) is list and len(events)==len(qp));checks[active]=False
        qt=[]
        for i,(event,packet) in enumerate(zip(events,qp)):
            if type(event) is not dict:raise ValueError('query event object')
            qt.append(_number(event['elapsed_seconds']))
            expected=dict(attempt=i+1,source_port=packet['source_port'],transaction_id=ident,
                          query_hex=packet['payload'].hex(),retry=i>0)
            if any(not _same_json(event.get(k),v) for k,v in expected.items()) or type(event.get('after_validated_reply')) is not bool:
                raise ValueError('query event wire binding')
        check(active,qt==sorted(qt))
        active='backend_reply_events';events=backend_egress.get('dns_reply_events')
        check(active,type(events) is list and len(events)==len(rp));checks[active]=False
        rt=[]
        for i,(event,packet,p,c) in enumerate(zip(events,rp,parsed,comparisons)):
            if type(event) is not dict:raise ValueError('reply event object')
            rt.append(_number(event['elapsed_seconds']))
            expected=dict(source_port=53,destination_port=qp[0]['source_port'],transaction_id=ident,
              reply_hex=packet['payload'].hex(),duplicate=i>0,accepted=True,
              rr_ttls=[r['ttl'] for r in p['records']],rrs=p['records'],normalized_dns_sha256=p['normalized_sha256'],
              comparison_mode=c[0]['mode'],comparison_identity_sha256=c[3],comparison_sorted_ttl_groups=c[1],
              answer_order_changed_from_first=c[2]!=comparisons[0][2],
              answer_order_changed_from_previous=i>0 and c[2]!=comparisons[i-1][2])
            if any(not _same_json(event.get(k),v) for k,v in expected.items()):
                raise ValueError('reply event wire/comparison binding')
        check(active,rt==sorted(rt) and all(t>=qt[0] for t in rt) and
          all((t>=rt[0] if e['after_validated_reply'] else t<=rt[0])
              for t,e in zip(qt,backend_egress['dns_query_events'])))
        active='backend_original_records'
        answers=[]
        for r in initial['records']:
            if r['section']!='answer':continue
            value=(str(ipaddress.IPv4Address(bytes.fromhex(r['rdata_hex']))) if r['type']==1 else
                   r['decoded_domain'] if r['type']==5 else r['rdata_hex'])
            answers.append({k:r[k] for k in ('owner','type','class','ttl','rdata_hex')}|{'value':value})
        expected=dict(domain='example.com',dns_source_port=qp[0]['source_port'],dns_transaction_id=ident,
          dns_query_hex=qp[0]['payload'].hex(),dns_reply_hex=rp[0]['payload'].hex(),
          dns_answer_records=answers,dns_first_reply_rrs=initial['records'],
          dns_last_accepted_rr_ttls=[r['ttl'] for r in parsed[-1]['records']],dns_comparison_mode=comparisons[0][0]['mode'])
        check(active,all(_same_json(backend_egress.get(k),v) for k,v in expected.items()))
        active='reply_window'
        # The backend exports the actual first-query clock used in its deadline,
        # not the later clock sampled while appending the first query event.
        first_query=_number(backend_egress['dns_first_query_elapsed_seconds'])
        check(active,first_query<=qt[0] and all(first_query<=t<=first_query+10 for t in rt))
        active='original_lease';first=rt[0];expires=first+initial['lease_seconds']
        expected=dict(dns_first_accepted_elapsed_seconds=first,dns_original_lease_expires_elapsed_seconds=expires)
        check(active,all(_same_json(backend_egress.get(k),v) for k,v in expected.items()) and
          all(t<expires for t in rt) and all(
            _same_json(e.get('first_accepted_elapsed_seconds'),first) and
            _same_json(e.get('original_lease_expires_elapsed_seconds'),expires) and
            _same_json(e.get('original_lease_remaining_seconds'),initial['lease_seconds'] if i==0 else max(0,expires-rt[i]))
            for i,e in enumerate(events)))
        details['recorded_clock']={'first_query':first_query,'query_events':qt,'reply_events':rt,
          'first_accepted':first,'original_lease_expires':expires,'lease_seconds':initial['lease_seconds']}
        active='source_policy_revision'
        expected=dict(dns_policy_revision=2,dns_duplicate_policy=POLICY,
          dns_id_source='observed_wire_not_nonce_derived',dns_query_profile='RD0100_one_example.com_A_IN_no_OPT',
          unsupported_traffic_action='terminal_failure_no_forward')
        check(active,all(_same_json(backend_egress.get(k),v) for k,v in expected.items()))
    except (ValueError,TypeError,KeyError,IndexError,OverflowError,struct.error,RecursionError) as error:
        checks[active]=False;evaluated.add(active)
        details['errors'][active]=str(error)[:256]
    details['not_evaluated']=[k for k in CHECK_KEYS if k not in evaluated]
    return {'checks':checks,'details':details}
