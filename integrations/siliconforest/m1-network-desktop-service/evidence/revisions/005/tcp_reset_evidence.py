"""Independent TCP7 byte/control evidence; no producer parser or model imports.

Lists contain ordered IPv4 TCP packets from authenticated closed streams. Their
separate directional orders do not establish a shared global chronology. The
backend's cross-direction emission/consumption order remains source-bound.
Revision005 extends frozen independent TCP6 source948653fb; no producer imports.
"""
import hashlib
import ipaddress
import math
import re
import struct

CHECKS=('packet_input_bounds','independent_IP_TCP_validation','exact_two_flow_tuples',
        'SYN_and_ISNs','exact_request_coverage','HTTP200_response_FIN',
        'client_FIN_packets','server_FIN_ACK_packets','terminal_guest_control_packets',
        'per_direction_control_order','exact_terminal_RST_packets',
        'bounded_RST_event_correspondence','producer_TCP7_FIN_state','source_revision_and_scope')
GUEST=b'\x0a\x00\x02\x0f'
MASK=(1<<32)-1
FRAME_HEADER=struct.Struct('!8sBBHHHQ24sII')

def need(value,message):
    if not value:raise ValueError(message)

def integer(value,lower=0,upper=MASK):
    need(type(value)is int and lower<=value<=upper,'integer bound/type');return value

def number(value):
    need(type(value)in(int,float)and math.isfinite(value)and value>=0,'finite nonnegative clock');return value

def _checksum(data):
    total=0
    for offset in range(0,len(data),2):
        total+=(data[offset]<<8)|(data[offset+1]if offset+1<len(data)else 0)
    while total>65535:total=(total&65535)+(total>>16)
    return total^65535

def _crc32c(raw):
    value=0xffffffff
    for byte in raw:
        value^=byte
        for _ in range(8):value=(value>>1)^(0x82f63b78 if value&1 else 0)
    return value^0xffffffff

def host_frames_from_stream(raw):
    """Split bounded closed host direction2 bytes; validation is below."""
    need(type(raw)is bytes and 112<=len(raw)<=65536+128*56,'host framed stream bound')
    frames=[];offset=0
    while offset<len(raw):
        need(len(frames)<128 and offset+56<=len(raw),'host frame count/header')
        size=int.from_bytes(raw[offset+12:offset+14],'big')
        need(size<=1500 and offset+56+size<=len(raw),'host frame extent')
        frames.append(raw[offset:offset+56+size]);offset+=56+size
    return frames

def _frame(raw,ordinal,nonce,expected_direction):
    """Validate one actual raw frame; this alone is no closed-stream proof."""
    need(type(raw)is bytes and 56<=len(raw)<=1556,'host raw frame bound')
    need(type(ordinal)is int and 1<=ordinal<=128,'host frame ordinal')
    need(type(nonce)is str and re.fullmatch('[0-9a-f]{48}',nonce)is not None,'frame session nonce')
    magic,kind,direction,reserved,size,header,sequence,session,crc,tail=FRAME_HEADER.unpack(raw[:56])
    need(expected_direction in(1,2) and magic==b'DLNET001' and direction==expected_direction and reserved==tail==0 and header==56,'raw frame fixed header/direction')
    need(sequence==ordinal and session.hex()==nonce and size==len(raw)-56,'host frame exact sequence/session/length')
    need(kind in(1,2,3) and (20<=size<=1500 if kind==2 else size==0),'host frame kind/payload bound')
    need(_crc32c(raw[:48]+b'\0\0\0\0'+raw[52:])==crc,'host frame CRC32C')
    return {'kind':kind,'frame_sequence':sequence,'frame_sha256':hashlib.sha256(raw).hexdigest(),
            'packet_sha256':hashlib.sha256(raw[56:]).hexdigest(),'packet':raw[56:]}

def _host_frame(raw,ordinal,nonce):
    """One host direction2 frame, retained for scoped parser controls/tests."""
    return _frame(raw,ordinal,nonce,2)

def _host_frame_bindings(frames,host_packets,nonce):
    """Derive frame identities from rawbytes, without trusting producer receipts."""
    need(type(frames)is list and 2<=len(frames)<=128,'mandatory host frame list bound')
    need(type(nonce)is str and re.fullmatch('[0-9a-f]{48}',nonce)is not None,'frame session nonce')
    tcp=[];packet_bytes=0;ip_count=0
    for ordinal,raw in enumerate(frames,1):
        identity=_host_frame(raw,ordinal,nonce);kind=identity['kind'];size=len(identity['packet'])
        if ordinal in(1,len(frames)):
            need(kind==(1 if ordinal==1 else 3) and size==0,'host exact OPEN/CLOSE')
            continue
        need(kind==2 and 20<=size<=1500,'host IP-only interior')
        packet=raw[56:];head=(packet[0]&15)*4
        need(packet[0]>>4==4 and 20<=head<=60 and head<=len(packet) and int.from_bytes(packet[2:4],'big')==size and _checksum(packet[:head])==0,'host framed IPv4 bounds/checksum')
        need(not int.from_bytes(packet[6:8],'big')&0xbfff and packet[9]in(6,17),'host framed unfragmented TCP/UDP')
        ip_count+=1;packet_bytes+=size;need(ip_count<=126 and packet_bytes<=65536,'unchanged whole host IP/frame byte budgets')
        if packet[9]==6:
            tcp.append({key:value for key,value in identity.items()if key!='kind'})
    need([row['packet']for row in tcp]==host_packets,'raw frame TCP subsequence exactly equals caller packets')
    return tcp,{'frame_count':len(frames),'IPv4_count':ip_count,'IPv4_bytes':packet_bytes,
                'stream_sha256':hashlib.sha256(b''.join(frames)).hexdigest()}

def _packet(raw,reply):
    need(type(raw)is bytes and 40<=len(raw)<=1500 and raw[0]==0x45,'IPv4 fixed header/length')
    need(int.from_bytes(raw[2:4],'big')==len(raw)and _checksum(raw[:20])==0,'IPv4 total/checksum')
    need(int.from_bytes(raw[6:8],'big')&0xbfff==0 and raw[9]==6,'unfragmented TCP')
    src,dst=raw[12:16],raw[16:20];body=raw[20:]
    header=(body[12]>>4)*4
    need(20<=header<=60 and header<=len(body)and body[12]&15==0,'TCP data offset/reserved/NS')
    pseudo=src+dst+b'\0\x06'+len(body).to_bytes(2,'big')
    need(_checksum(pseudo+body)==0,'TCP checksum')
    sp,dp,seq,ack=struct.unpack_from('!HHII',body)
    need(sp>0 and dp>0,'nonzero ports')
    local,remote=(dst,src)if reply else(src,dst)
    lp,rp=(dp,sp)if reply else(sp,dp)
    need(local==GUEST and rp==80 and lp>=1024 and ipaddress.IPv4Address(remote).is_global,'fixed guest/public HTTP tuple')
    return dict(raw=raw,key=(lp,str(ipaddress.IPv4Address(remote)),rp),seq=seq,ack=ack,
                flags=body[13],payload=body[header:],header=header)

def _request(raw):
    need(type(raw)is str and len(raw)==372 and re.fullmatch('[0-9a-f]+',raw)is not None,'186-byte request hex')
    data=bytes.fromhex(raw)
    match=re.fullmatch(rb'GET / HTTP/1\.1\r\nHost: example\.com\r\nUser-Agent: darwin-lab-egress/1\r\nAccept-Encoding: identity\r\nX-Darwin-Lab-Nonce: ([0-9a-f]{48})\r\nConnection: close\r\n\r\n',data)
    need(match is not None,'canonical request');return data,match[1].decode()

def _http(data):
    need(type(data)is bytes and 0<len(data)<=2048,'HTTP2048 bound')
    boundary=data.find(b'\r\n\r\n');need(boundary>=0,'HTTP complete headers')
    lines=data[:boundary].split(b'\r\n');need(re.fullmatch(rb'HTTP/1\.[01] 200(?: [ -~]*)?',lines[0])is not None,'HTTP200 status')
    fields={}
    for line in lines[1:]:
        key,sep,value=line.partition(b':');key=key.lower();value=value.strip(b' \t')
        need(sep and re.fullmatch(rb'[a-z0-9-]+',key)is not None and key not in fields,'HTTP header name/duplicate')
        need(all(32<=c<=126 or c==9 for c in value),'HTTP header controls');fields[key]=value
    need(fields.get(b'content-type',b'').split(b';',1)[0].strip().lower()==b'text/html','HTML content type')
    need(fields.get(b'content-encoding',b'identity').lower()==b'identity','identity encoding')
    body=data[boundary+4:]
    if b'transfer-encoding'in fields:
        need(fields[b'transfer-encoding'].lower()==b'chunked'and b'content-length'not in fields,'one HTTP framing')
        decoded=bytearray();offset=0
        for _ in range(1024):
            end=body.find(b'\r\n',offset);need(end>=0,'chunk header EOF');token=body[offset:end]
            need(re.fullmatch(rb'[0-9a-fA-F]{1,8}',token)is not None,'chunk size token');size=int(token,16);offset=end+2
            need(size<=2048 and len(decoded)+size<=2048,'decoded chunk bound')
            if size==0:
                need(body[offset:]==b'\r\n','no trailer/suffix');body=bytes(decoded);break
            need(len(body)>=offset+size+2 and body[offset+size:offset+size+2]==b'\r\n','chunk complete')
            decoded.extend(body[offset:offset+size]);offset+=size+2
        else:raise ValueError('chunk count bound')
    elif b'content-length'in fields:
        token=fields[b'content-length'];need(re.fullmatch(rb'[0-9]{1,6}',token)is not None and int(token)==len(body),'exact body length')
    need(b'Example Domain'in body,'example body bytes')
    return {'status':200,'body_bytes':len(body),'header_bytes':boundary+4}

def _terminal_credits(s):
    record=s['record'];guests=s['guest_RSTs'];peers=s['stateless_RSTs']
    emissions=record.get('finACK_emission_events');guest_events=record.get('guest_terminal_RST_events');peer_events=record.get('peer_stateless_RST_events')
    need(type(emissions)is list and type(guest_events)is list and type(peer_events)is list,'emission/terminal event lists')
    need(len(emissions)==len(s['server_FIN_ACKs'])<=128,'every actual committed pureFINACK recorded')
    need(len(guests)==len(guest_events)<=127 and len(peers)==len(peer_events)<=127 and len(s['RSTs'])<=128,'derived bookkeeping ceilings only')
    budgets=record.get('terminal_budgets');expected=dict(bare_peer=8,guest=127,stateless_peer=127,total_peer=128)
    need(type(budgets)is dict and set(budgets)==set(expected) and all(type(budgets[key])is int and budgets[key]==value for key,value in expected.items()),'derived credit bookkeeping declaration')
    for field,value in(('finACK_emission_count',len(emissions)),('finACK_credit_count',max(0,len(emissions)-1)),
       ('guest_terminal_RST_count',len(guests)),('peer_stateless_RST_count',len(peers)),('peer_RST_total_count',len(s['RSTs']))):
        need(type(record.get(field))is int and record[field]==value,'source count '+field)
    emission_keys={'elapsed_seconds','packet_hex','packet_sha256','frame_direction','frame_sequence','frame_sha256',
       'frame_bytes','commit_index','emission_index','baseline','credit_id','guest_RST_consumed'}
    credits={};emit_clocks=[];s['FINACK_links']=[]
    for ordinal,((_,packet),event)in enumerate(zip(s['server_FIN_ACKs'],emissions),1):
        need(type(event)is dict and set(event)==emission_keys,'exact committed FINACK schema')
        frame=packet['frame'];elapsed=number(event['elapsed_seconds']);emit_clocks.append(elapsed)
        need(event['packet_hex']==packet['raw'].hex()and event['packet_sha256']==frame['packet_sha256'],'emission exact rawpacket identity')
        for key,value in(('frame_direction',2),('frame_sequence',frame['frame_sequence']),('frame_bytes',56+len(packet['raw'])),
                          ('commit_index',frame['frame_sequence']-1),('emission_index',ordinal)):
            need(type(event[key])is int and event[key]==value,'committed frame field '+key)
        need(event['frame_sha256']==frame['frame_sha256']and type(event['guest_RST_consumed'])is bool,'committed frame SHA/consumed type')
        need(event['baseline']is(ordinal==1),'first committed FINACK alone is baseline')
        if ordinal==1:
            need(event['credit_id']is None and event['guest_RST_consumed']is False,'baseline grants no credit')
        else:
            need(type(event['credit_id'])is int and event['credit_id']==ordinal-1,'numbered duplicate FINACK credits')
            credits[ordinal-1]=event
        s['FINACK_links'].append({'emission_index':ordinal,'baseline':ordinal==1,'credit_id':event['credit_id'],
            'frame_sequence':frame['frame_sequence'],'frame_sha256':frame['frame_sha256'],'packet_sha256':frame['packet_sha256'],
            'source_commit_elapsed_seconds':elapsed})
    need(emit_clocks==sorted(emit_clocks),'source commit clocks ordered')
    guest_credits={};guest_clocks=[];peer_clocks=[];used_FINACK=set();used_guest=set();s['credit_links']=[]
    core={'elapsed_seconds','packet_hex','sequence','acknowledgement','flags','payload_bytes','policy_accepted'}
    for ordinal,((_,packet),event)in enumerate(zip(guests,guest_events),1):
        need(type(event)is dict and set(event)==core|{'credit_id','peer_reply_consumed','finACK_credit_id','finACK_frame_sequence','finACK_frame_sha256'},'guest credit exact schema')
        need(type(event['credit_id'])is int and event['credit_id']==ordinal,'guest numbered credits')
        credit=event['finACK_credit_id'];need(type(credit)is int and credit==ordinal and credit in credits and credit not in used_FINACK,'FIFO one-use committed FINACK credit per guestRST')
        source=credits[credit];elapsed=number(event['elapsed_seconds']);guest_clocks.append(elapsed)
        need(elapsed>=source['elapsed_seconds'],'source commit must precede recorded guest consumption')
        need(type(event['finACK_frame_sequence'])is int and event['finACK_frame_sequence']==source['frame_sequence'] and
             event['finACK_frame_sha256']==source['frame_sha256'],'guest consumed exact committed frame identity')
        need(event['packet_hex']==packet['raw'].hex()and event['policy_accepted']is True and type(event['peer_reply_consumed'])is bool,'guest raw/event flags')
        for field,value in(('sequence',s['final_client']),('acknowledgement',0),('flags',4),('payload_bytes',0)):
            need(type(event[field])is int and event[field]==value,'guest event scalar')
        used_FINACK.add(credit);guest_credits[ordinal]=(event,packet)
        s['credit_links'].append({'credit_id':ordinal,'FINACK_credit_id':credit,'FINACK_frame_sequence':source['frame_sequence'],
            'FINACK_frame_sha256':source['frame_sha256'],'guest_packet_sha256':hashlib.sha256(packet['raw']).hexdigest(),'peer_packet_sha256':None})
    for credit,event in credits.items():need(event['guest_RST_consumed']is(credit in used_FINACK),'FINACK credit consumed flag exact')
    for field,value in(('finACK_credits_consumed',len(used_FINACK)),('finACK_credits_available',len(credits)-len(used_FINACK))):
        need(type(record.get(field))is int and record[field]==value,'source credit balance '+field)
    for ordinal,((_,packet),event)in enumerate(zip(peers,peer_events),1):
        need(type(event)is dict and set(event)==core|{'guest_RST_credit_id'},'peer credited exact schema')
        credit=event['guest_RST_credit_id'];need(type(credit)is int and credit==ordinal and credit in guest_credits and credit not in used_guest,'FIFO one-use actual guest credit')
        source,guest=guest_credits[credit];elapsed=number(event['elapsed_seconds']);peer_clocks.append(elapsed)
        need(elapsed>=source['elapsed_seconds'],'source guest observation precedes peer consumption')
        need(event['packet_hex']==packet['raw'].hex()and event['policy_accepted']is True and packet['ack']==guest['seq'],'peer raw and correlated guestSEQ')
        for field,value in(('sequence',0),('acknowledgement',guest['seq']),('flags',0x14),('payload_bytes',0)):
            need(type(event[field])is int and event[field]==value,'peer event scalar')
        used_guest.add(credit);s['credit_links'][credit-1]['peer_packet_sha256']=hashlib.sha256(packet['raw']).hexdigest()
    for credit,(event,_)in guest_credits.items():need(event['peer_reply_consumed']is(credit in used_guest),'guest credit consumed flag exact')
    need(guest_clocks==sorted(guest_clocks)and peer_clocks==sorted(peer_clocks),'source credit clocks ordered')

def terminal_reset_evidence(guest_packets,host_packets,egress,*,host_frames=None):
    checks=dict.fromkeys(CHECKS,False);active=CHECKS[0]
    details={'errors':[],'flows':[],'global_chronology_independently_observed':False,
             'global_FIN_order_authority':'Source-bound TCP7 queuecommit/credit-consumption hooks and clocks; separate raw streams prove per-direction order only. Queuecommit is not physical delivery proof.',
             'forwarding_scope':'Actual host frames are CRC/session/identity checked here. Caller still binds closed raw hashes and equality with actual delivered/forwarded streams and physical ACKcredit.'}
    try:
        need(type(guest_packets)is list and type(host_packets)is list,'packet list types')
        need(type(host_frames)is list and 2<=len(host_frames)<=128,'TCP7 requires actual full host frames')
        need(1<=len(guest_packets)<=126 and 1<=len(host_packets)<=126,'packet count bounds')
        need(all(type(p)is bytes for p in guest_packets+host_packets),'packet bytes only')
        need(sum(map(len,guest_packets))<=65536 and sum(map(len,host_packets))<=65536,'direction bytes bound')
        need(type(egress)is dict and type(egress.get('flows'))is list and len(egress['flows'])==2,'exact flow metadata');checks[active]=True
        active='independent_IP_TCP_validation';gp=[_packet(p,False)for p in guest_packets];hp=[_packet(p,True)for p in host_packets]
        _,frame_nonce=_request(egress.get('request_expected_hex'));frame_rows,frame_summary=_host_frame_bindings(host_frames,host_packets,frame_nonce)
        for packet,row in zip(hp,frame_rows):packet['frame']=row
        details['host_frame_binding']=frame_summary;checks[active]=True
        active='exact_two_flow_tuples';flowstates={}
        for ordinal,record in enumerate(egress['flows'],1):
            need(type(record)is dict and record.get('local_ipv4')=='10.0.2.15','flow metadata/local address')
            key=(integer(record.get('local_port'),1024,65535),record.get('remote_ipv4'),integer(record.get('remote_port'),80,80))
            need(type(key[1])is str and ipaddress.IPv4Address(key[1]).is_global and key not in flowstates,'unique public tuple')
            need(type(record.get('flow_id'))is int and record['flow_id']==ordinal,'flow ordinal')
            flowstates[key]={'record':record,'guest':[],'host':[],'ordinal':ordinal}
        for index,p in enumerate(gp):need(p['key']in flowstates,'guest unknown tuple');flowstates[p['key']]['guest'].append((index,p))
        for index,p in enumerate(hp):need(p['key']in flowstates,'host unknown tuple');flowstates[p['key']]['host'].append((index,p))
        need(all(s['guest']and s['host']for s in flowstates.values()),'both directions per flow');checks[active]=True
        active='SYN_and_ISNs';first_order=[]
        for key,s in flowstates.items():
            record=s['record'];cisn=integer(record.get('client_ISN'));sisn=integer(record.get('server_ISN'));s.update(cisn=cisn,sisn=sisn)
            initial=s['guest'][0][1];need(initial['flags']in(2,0xc2)and not initial['payload']and initial['seq']==cisn and initial['ack']==0,'initial exact SYN')
            need(type(record.get('initial_tcp_flags'))is int and record['initial_tcp_flags']==initial['flags'],'SYN flags metadata');first_order.append((s['guest'][0][0],s['ordinal']))
            initial=s['host'][0][1];need(initial['flags']in(0x12,0x52)and not initial['payload']and initial['seq']==sisn and initial['ack']==(cisn+1)&MASK,'first SYNACK')
            for _,p in s['guest']:
                if p['flags']&4:need(p['flags']==4 and p['header']==20 and not p['payload']and p['ack']==0 and p['seq']==(cisn+188)&MASK,'exact guest terminalRST')
                if p['flags']&2:need(p['flags']in(2,0xc2)and not p['payload']and p['seq']==cisn and p['ack']==0,'SYN retransmission unchanged')
            for _,p in s['host']:
                if p['flags']&2:need(p['flags']in(0x12,0x52)and not p['payload']and p['seq']==sisn and p['ack']==(cisn+1)&MASK,'SYNACK unchanged')
        need([ordinal for _,ordinal in sorted(first_order)]==[1,2],'flow order matches original SYN order');checks[active]=True
        active='exact_request_coverage';session_nonce=None
        for s in flowstates.values():
            record=s['record'];expected,nonce=_request(record.get('request_expected_hex'))
            need(session_nonce in(None,nonce),'same full session nonce');session_nonce=nonce
            need(record.get('request_observed_hex')==expected.hex()and type(record.get('request_covered_bytes'))is int and record['request_covered_bytes']==186,'request metadata')
            coverage=bytearray(186);s['request']=expected;s['client_FINs']=[];s['final_ACKs']=[];s['guest_RSTs']=[]
            for index,p in s['guest']:
                if p['flags']&2:continue
                if p['flags']&4:s['guest_RSTs'].append((index,p));continue
                start=(p['seq']-s['cisn']-1)&MASK;need(start<=187 and (not p['payload']or start+len(p['payload'])<=186),'request TCP sequence')
                if p['payload']:
                    need(p['payload']==expected[start:start+len(p['payload'])],'exact request slice');coverage[start:start+len(p['payload'])]=b'\1'*len(p['payload'])
                if p['flags']==0x11 and not p['payload']and start==186 and all(coverage):s['client_FINs'].append((index,p))
            need(all(coverage),'all GET bytes covered')
        expected,_=_request(egress.get('request_expected_hex'));need(all(s['request']==expected for s in flowstates.values()),'global expected request');details['session_nonce']=session_nonce;checks[active]=True
        active='HTTP200_response_FIN'
        for s in flowstates.values():
            present=bytearray(2048);response=bytearray(2048);highest=0;fin=None;rst=[];s['server_FIN_ACKs']=[];s['server_FIN_indices']=[]
            for index,p in s['host']:
                if p['flags']&2:continue
                if p['flags']&4:
                    need(fin is not None and all(present[:fin])and highest==fin,'RST after complete response FIN in host order');_http(bytes(response[:fin]));rst.append((index,p));continue
                need(p['flags']&16 and ((p['ack']-s['cisn']-1)&MASK)<=187,'server ACK within request+FIN')
                start=(p['seq']-s['sisn']-1)&MASK;need(start<=2049 and (not p['payload']or start+len(p['payload'])<=2048),'response sequence/size')
                for offset,byte in enumerate(p['payload'],start):
                    need(not present[offset]or response[offset]==byte,'response overlap identity');present[offset]=1;response[offset]=byte
                if p['payload']:highest=max(highest,start+len(p['payload']))
                if p['flags']&1:
                    value=start+len(p['payload']);need(fin in(None,value),'one stable FIN offset');fin=value;s['server_FIN_indices'].append(index)
            need(fin is not None and highest==fin and all(present[:fin]),'contiguous HTTP with exact FIN')
            http=_http(bytes(response[:fin]));record=s['record']
            for field in('response_contiguous_bytes','response_highest_bytes','response_FIN_offset'):need(type(record.get(field))is int and record[field]==fin,'response extent metadata')
            need(type(record.get('HTTP'))is dict and record['HTTP']==http and record.get('wire_complete')is True,'HTTP metadata')
            s.update(fin=fin,response=bytes(response[:fin]),RSTs=rst,bare_RSTs=[r for r in rst if r[1]['flags']==4],stateless_RSTs=[r for r in rst if r[1]['flags']==0x14],final_server=(s['sisn']+2+fin)&MASK,final_client=(s['cisn']+188)&MASK)
        checks[active]=True
        active='client_FIN_packets'
        for s in flowstates.values():
            s['client_FINs']=[(i,p)for i,p in s['client_FINs']if p['ack']==s['final_server']]
            if s['RSTs']or s['guest_RSTs']:need(bool(s['client_FINs']),'exact final clientFIN before terminalRST grant')
        checks[active]=True
        active='server_FIN_ACK_packets'
        for s in flowstates.values():
            s['server_FIN_ACKs']=[(i,p)for i,p in s['host']if p['flags']==0x10 and p['header']==20 and not p['payload']and p['seq']==s['final_server']and p['ack']==s['final_client']]
            if s['RSTs']or s['guest_RSTs']:need(bool(s['server_FIN_ACKs']),'exact server ACK of clientFIN')
        checks[active]=True
        active='terminal_guest_control_packets'
        for s in flowstates.values():
            s['final_ACKs']=[(i,p)for i,p in s['guest']if p['flags']==0x10 and not p['payload']and p['seq']==s['final_client']and p['ack']==s['final_server']]
            events=s['record'].get('terminal_reset_events')
            need(type(events)is list and len(events)<=8,'bounded trigger events')
            s['triggers']=[]
            for event in events:
                need(type(event)is dict and event.get('trigger')in('pure_final_ACK','repeated_client_FIN'),'known exact trigger')
                s['triggers'].append(event['trigger'])
            if 'pure_final_ACK'in s['triggers']:need(bool(s['final_ACKs']),'pure final guestACK')
            if 'repeated_client_FIN'in s['triggers']:need(len(s['client_FINs'])>=2,'initial plus repeated exact clientFIN')
        checks[active]=True
        active='per_direction_control_order'
        for s in flowstates.values():
            for ri,_ in s['guest_RSTs']:need(any(fi<ri for fi,_ in s['client_FINs']),'clientFIN before guestterminalRST')
            if s['RSTs']:
                if 'pure_final_ACK'in s['triggers']:need(any(fi<ai for fi,_ in s['client_FINs']for ai,_ in s['final_ACKs']),'guest FIN before final guestACK')
                if 'repeated_client_FIN'in s['triggers']:need(any(fi<ri for fi,_ in s['client_FINs']for ri,_ in s['client_FINs']),'prior exact clientFIN before repeat')
                for ri,_ in s['RSTs']:need(any(fi<ai<ri for fi in s['server_FIN_indices']for ai,_ in s['server_FIN_ACKs']),'host serverFIN before FINACK before every terminalRST')
        checks[active]=True
        active='exact_terminal_RST_packets'
        for s in flowstates.values():
            for _,p in s['RSTs']:
                if p['flags']==4:need(not p['payload']and p['ack']==0 and p['seq']==s['final_server'],'exact bare peer reset flags/seq/ACK/payload')
                else:need(p['flags']==0x14 and p['header']==20 and not p['payload']and p['seq']==0 and p['ack']==s['final_client'],'exact peerstateless RSTACK')
        checks[active]=True
        active='bounded_RST_event_correspondence'
        for key,s in flowstates.items():
            events=s['record'].get('terminal_reset_events');count=s['record'].get('terminal_reset_count')
            need(type(events)is list and type(count)is int and count==len(events)==len(s['bare_RSTs'])and count<=8,'terminal reset count/record mapping')
            clocks=[]
            for (_,p),event in zip(s['bare_RSTs'],events):
                need(type(event)is dict and set(event)=={'elapsed_seconds','packet_hex','sequence','acknowledgement','flags','payload_bytes','policy_accepted','trigger'},'event exact schema')
                clocks.append(number(event['elapsed_seconds']))
                need(event['packet_hex']==p['raw'].hex()and event['policy_accepted']is True,'recorded actual raw reset')
                for field,value in(('sequence',p['seq']),('acknowledgement',0),('flags',4),('payload_bytes',0)):need(type(event[field])is int and event[field]==value,'event scalar')
            need(clocks==sorted(clocks),'per-flow event clocks ordered');_terminal_credits(s)
        checks[active]=True
        active='producer_TCP7_FIN_state'
        for s in flowstates.values():
            need(type(s['record'].get('TCP_policy_revision'))is int and s['record']['TCP_policy_revision']==7,'per-flow TCP7 revision')
            if s['RSTs']or s['guest_RSTs']:
                for field in('client_FIN_seen','server_FIN_ACK_seen'):need(s['record'].get(field)is True,'source-bound cross-direction grant '+field)
                for trigger in s['triggers']:
                    field='final_ACK_after_server_FIN_ACK'if trigger=='pure_final_ACK'else'repeated_client_FIN_after_server_FIN_ACK'
                    need(s['record'].get(field)is True,'source-bound exact alternate grant '+field)
        checks[active]=True
        active='source_revision_and_scope'
        need(egress.get('profile')=='desktop-native-resolver-two-http-v1'and egress.get('first_error')is None,'profile/no error')
        for field,value in(('backend_policy_revision',5),('tcp_policy_revision',7),('dns_policy_revision',2)):need(type(egress.get(field))is int and egress[field]==value,'source policy revision')
        need(egress.get('parser_dependency_SHA256')=='20a0b6d5dbdff17b748f241623fc09989dd83b46957ab68862ae8799fecc6d8b','frozen inherited parser binding')
        for s in flowstates.values():
            details['flows'].append({'flow_id':s['ordinal'],'tuple':list(next(k for k,v in flowstates.items()if v is s)),
                'request_sha256':hashlib.sha256(s['request']).hexdigest(),'response_sha256':hashlib.sha256(s['response']).hexdigest(),
                'request_bytes':186,'response_bytes':s['fin'],'terminal_RST_count':len(s['bare_RSTs']),'guest_terminal_RST_count':len(s['guest_RSTs']),'peer_stateless_RST_count':len(s['stateless_RSTs']),'peer_RST_total_count':len(s['RSTs']),'guest_credit_links':s['credit_links'],'FINACK_emissions':s['FINACK_links'],
                'terminal_RST_triggers':s['triggers'],'terminal_RST_packet_sha256':[hashlib.sha256(p['raw']).hexdigest()for _,p in s['bare_RSTs']],
                'peer_stateless_RST_packet_sha256':[hashlib.sha256(p['raw']).hexdigest()for _,p in s['stateless_RSTs']],
                'guest_terminal_RST_packet_sha256':[hashlib.sha256(p['raw']).hexdigest()for _,p in s['guest_RSTs']],
                'all_peer_RST_packet_sha256':[hashlib.sha256(p['raw']).hexdigest()for _,p in s['RSTs']],
                'guest_FIN_indices':[i for i,_ in s['client_FINs']],'host_FIN_ACK_indices':[i for i,_ in s['server_FIN_ACKs']],
                'guest_final_ACK_indices':[i for i,_ in s['final_ACKs']],'host_RST_indices':[i for i,_ in s['RSTs']]})
        checks[active]=True
    except (ValueError,TypeError,KeyError,IndexError,OverflowError,RecursionError,struct.error)as error:
        checks[active]=False;details['errors'].append({'check':active,'error':str(error)[:300]})
    details['not_evaluated']=[key for key,value in checks.items()if not value]
    return {'checks':checks,'details':details,'qualified_TCP7_terminal_reset_records_only':all(checks.values())}

def _full_stream(raw,nonce,direction):
    frames=host_frames_from_stream(raw);tcp=[];ip_bytes=0;ip_count=0
    for ordinal,frame in enumerate(frames,1):
        row=_frame(frame,ordinal,nonce,direction);kind=row['kind'];packet=row['packet']
        if ordinal in(1,len(frames)):
            need(kind==(1 if ordinal==1 else 3),'strict fullstream OPEN/CLOSE phase')
            continue
        need(kind==2,'only IP between OPEN/CLOSE')
        head=(packet[0]&15)*4
        need(packet[0]>>4==4 and 20<=head<=60 and head<=len(packet) and int.from_bytes(packet[2:4],'big')==len(packet) and _checksum(packet[:head])==0,'stream IPv4 bounds/checksum')
        need(not int.from_bytes(packet[6:8],'big')&0xbfff and packet[9]in(6,17),'stream unfragmented TCP/UDP')
        ip_count+=1;ip_bytes+=len(packet);need(ip_count<=126 and ip_bytes<=65536,'stream existing global IP budgets')
        if packet[9]==6:tcp.append(packet)
    return frames,tcp,{'direction':direction,'frame_count':len(frames),'IPv4_count':ip_count,'IPv4_bytes':ip_bytes,'stream_sha256':hashlib.sha256(raw).hexdigest()}

def terminal_reset_stream_evidence(guest_frames,host_frames,egress,nonce):
    """Pure closed-stream entrypoint; no file/network/production imports."""
    try:
        _,guest_tcp,guest_summary=_full_stream(guest_frames,nonce,1)
        actual_host_frames,host_tcp,host_summary=_full_stream(host_frames,nonce,2)
        report=terminal_reset_evidence(guest_tcp,host_tcp,egress,host_frames=actual_host_frames)
        report['details']['full_streams']={'guest':guest_summary,'host':host_summary}
        return report
    except (ValueError,TypeError,KeyError,IndexError,OverflowError,RecursionError,struct.error)as error:
        return {'checks':dict.fromkeys(CHECKS,False),
            'details':{'errors':[{'check':'packet_input_bounds','error':str(error)[:300]}],
                'not_evaluated':list(CHECKS),'global_chronology_independently_observed':False,
                'scope':'Fullstream framing failed; no credit/TCP success inference.'},
            'qualified_TCP7_terminal_reset_records_only':False}
