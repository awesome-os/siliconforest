"""Original bounded two-flow packet policy. No sockets/processes/host resolver.

Wire completeness is not a desktop, PID attribution, daemon or route verdict.
"""
import hashlib
import ipaddress
import math
import struct
import time
from frozen import (PacketPolicy,parse_packet,dns_reply_details,dns_duplicate_comparison,
                    GUEST,DNS,QUESTION,SOURCE_SHA256)
PROFILE='desktop-native-resolver-two-http-v1'
ABSOLUTE_SECONDS,ACCEPT_SECONDS,ACTIVE_SECONDS,QUIET_SECONDS,CLOSE_SECONDS=420,240,180,150,40
MAX_QUERY_PACKETS,MAX_FLOWS,MAX_PACKETS,MAX_PACKET_BYTES=4,2,126,65536
MAX_RESPONSE=2048
MAX_TERMINAL_RESETS=8
MAX_GUEST_TERMINAL_RESETS,MAX_STATELESS_RESETS,MAX_TOTAL_PEER_RESETS=127,127,128


def crc32c(raw):
    value=0xffffffff
    for byte in raw:
        value^=byte
        for _ in range(8):value=(value>>1)^(0x82f63b78 if value&1 else 0)
    return value^0xffffffff


def raw_chain_ttl(records):
    current='example.com';ttls=[];seen=set()
    for _ in range(8):
        if current in seen:raise ValueError('DNS CNAME cycle')
        seen.add(current)
        cn=[r for r in records if r['section']=='answer' and r['owner']==current and r['type']==5]
        aa=[r for r in records if r['section']=='answer' and r['owner']==current and r['type']==1]
        if cn:
            ttls.extend(r['ttl'] for r in cn);current=cn[0]['decoded_domain'];continue
        if not aa:raise ValueError('DNS no terminal A chain')
        ttls.extend(r['ttl'] for r in aa)
        if min(ttls)<=0:raise ValueError('DNS zero raw TTL')
        return min(ttls)
    raise ValueError('DNS CNAME budget')


class Flow(PacketPolicy):
    """Reuse frozen TCP3 behavior; DNS state is never fabricated or replayed."""
    def __init__(self,nonce,key,authorization,deadline,clock):
        super().__init__(nonce,clock=clock)
        self.key=key;self.authorization=authorization
        self.endpoint=key[1];self.allowed_addresses=[key[1]]
        # This applies only to an already-authorized tuple, not a new SYN lease.
        self.expires=deadline
        self.client_FIN_seen=False;self.server_FIN_ACK_seen=False
        self.final_ACK_after_server_FIN_ACK=False;self.repeated_client_FIN_after_server_FIN_ACK=False;self.terminal_reset_events=[];self.guest_terminal_RST_events=[];self.peer_stateless_RST_events=[];self.finACK_emission_events=[]
    def incoming(self,packet):
        view=parse_packet(packet)
        if view[0]!=6:raise ValueError('flow accepts TCP only')
        body,payload=view[5:]
        flags=body[13];seq=int.from_bytes(body[4:8],'big');ack=int.from_bytes(body[8:12],'big')
        if flags==0x14:
            credits=[e for e in self.guest_terminal_RST_events if not e['peer_reply_consumed']and e['sequence']==ack]
            if not (self.complete() and self.client_FIN_seen and self.server_FIN_ACK_seen and
                    len(body)==20 and body[12]==0x50 and not payload and seq==0 and credits):
                raise ValueError('peer stateless RSTACK outside exact credited case')
            if len(self.peer_stateless_RST_events)>=MAX_STATELESS_RESETS or len(self.terminal_reset_events)+len(self.peer_stateless_RST_events)>=MAX_TOTAL_PEER_RESETS:
                raise ValueError('peer stateless RSTACK global-derived storage cap')
            credit=credits[0];credit['peer_reply_consumed']=True
            self.peer_stateless_RST_events.append({'elapsed_seconds':self.clock()-self.started,'packet_hex':packet.hex(),
                'sequence':seq,'acknowledgement':ack,'flags':flags,'payload_bytes':0,'policy_accepted':True,'guest_RST_credit_id':credit['credit_id']})
            return
        if flags&4:
            final_server=(self.server_isn+1+self.fin_offset+1)&0xffffffff if self.server_isn is not None and self.fin_offset is not None else None
            if not (flags==4 and body[12]&15==0 and not payload and ack==0 and seq==final_server and
                    self.complete() and self.client_FIN_seen and self.server_FIN_ACK_seen and (self.final_ACK_after_server_FIN_ACK or self.repeated_client_FIN_after_server_FIN_ACK)):
                raise ValueError('external TCP reset outside exact terminal case')
            if len(self.terminal_reset_events)>=MAX_TERMINAL_RESETS or len(self.terminal_reset_events)+len(self.peer_stateless_RST_events)>=MAX_TOTAL_PEER_RESETS:raise ValueError('terminal TCP reset bare count8/global-derived storage cap')
            self.terminal_reset_events.append({'elapsed_seconds':self.clock()-self.started,'packet_hex':packet.hex(),
                'sequence':seq,'acknowledgement':ack,'flags':flags,'payload_bytes':0,'policy_accepted':True,
                'trigger':'pure_final_ACK'if self.final_ACK_after_server_FIN_ACK else'repeated_client_FIN'})
            return
        if self.server_isn is not None and not body[13]&2:
            start=(int.from_bytes(body[4:8],'big')-self.server_isn-1)&0xffffffff
            if (self.terminal_reset_events or self.guest_terminal_RST_events or self.peer_stateless_RST_events) and payload and start+len(payload)>self.fin_offset:
                raise ValueError('new response payload after terminal reset')
            if (payload and start+len(payload)>MAX_RESPONSE) or start>MAX_RESPONSE+1:
                raise ValueError('HTTP per-flow2048 response cap')
        result=super().incoming(packet)
        if (self.client_FIN_seen and self.complete() and flags==0x10 and not payload and
                seq==(self.server_isn+2+self.fin_offset)&0xffffffff and ack==(self.client_isn+2+len(self.request))&0xffffffff):
            self.server_FIN_ACK_seen=True
        return result
    def outgoing(self,packet):
        view=parse_packet(packet)
        if view[0]!=6:raise ValueError('flow accepts TCP only')
        body,payload=view[5:];flags=body[13];seq=int.from_bytes(body[4:8],'big');ack=int.from_bytes(body[8:12],'big')
        if flags&4:
            if not (self.complete() and self.client_FIN_seen and self.server_FIN_ACK_seen and flags==4 and
                    len(body)==20 and body[12]==0x50 and not payload and ack==0 and
                    seq==(self.client_isn+2+len(self.request))&0xffffffff):
                raise ValueError('guest terminalRST outside exact closed-flow case')
            credits=[e for e in self.finACK_emission_events if not e['baseline']and not e['guest_RST_consumed']]
            if not credits:raise ValueError('guest terminalRST lacks committed duplicateFINACK credit')
            if len(self.guest_terminal_RST_events)>=MAX_GUEST_TERMINAL_RESETS:raise ValueError('guest terminalRST global-derived storage cap')
            credit=credits[0];credit['guest_RST_consumed']=True
            self.guest_terminal_RST_events.append({'elapsed_seconds':self.clock()-self.started,'packet_hex':packet.hex(),
                'sequence':seq,'acknowledgement':ack,'flags':flags,'payload_bytes':0,'policy_accepted':True,
                'credit_id':len(self.guest_terminal_RST_events)+1,'peer_reply_consumed':False,
                'finACK_credit_id':credit['credit_id'],'finACK_frame_sequence':credit['frame_sequence'],'finACK_frame_sha256':credit['frame_sha256']})
            return
        result=super().outgoing(packet)
        final_server=(self.server_isn+2+self.fin_offset)&0xffffffff if self.server_isn is not None and self.fin_offset is not None else None
        if (self.complete() and flags==0x11 and not payload and body[12]&15==0 and
                seq==(self.client_isn+1+len(self.request))&0xffffffff and ack==final_server):
            if self.client_FIN_seen and self.server_FIN_ACK_seen:self.repeated_client_FIN_after_server_FIN_ACK=True
            self.client_FIN_seen=True
        if (self.server_FIN_ACK_seen and flags==0x10 and not payload and body[12]&15==0 and
                seq==(self.client_isn+2+len(self.request))&0xffffffff and ack==final_server):
            self.final_ACK_after_server_FIN_ACK=True
        return result
    def commit_finACK_emission(self,packet,frame,sequence):
        proto,src,dst,sp,dp,body,payload=parse_packet(packet)
        if not (proto==6 and self.complete() and self.client_FIN_seen and self.server_FIN_ACK_seen and
                len(body)==20 and body[12]==0x50 and body[13]==0x10 and not payload and
                int.from_bytes(body[4:8],'big')==(self.server_isn+2+self.fin_offset)&0xffffffff and
                int.from_bytes(body[8:12],'big')==(self.client_isn+188)&0xffffffff):return False
        index=len(self.finACK_emission_events)+1
        self.finACK_emission_events.append({'elapsed_seconds':self.clock()-self.started,'packet_hex':packet.hex(),
            'packet_sha256':hashlib.sha256(packet).hexdigest(),'frame_direction':2,'frame_sequence':sequence,
            'frame_sha256':hashlib.sha256(frame).hexdigest(),'frame_bytes':len(frame),'commit_index':sequence-1,
            'emission_index':index,'baseline':index==1,'credit_id':None if index==1 else index-1,'guest_RST_consumed':False})
        return True
    def complete(self):
        return (all(self.coverage) and self.http is not None and
                self.highest==self.contiguous==self.fin_offset)
    def summary(self):
        return {'local_ipv4':'10.0.2.15','local_port':self.key[0],
            'remote_ipv4':str(ipaddress.IPv4Address(self.key[1])),'remote_port':80,
            'initial_tcp_flags':self.initial_tcp_flags,'client_ISN':self.client_isn,
            'server_ISN':self.server_isn,'authorization':self.authorization,
            'request_expected_hex':self.request.hex(),'request_observed_hex':bytes(self.observed_request).hex(),
            'request_covered_bytes':sum(self.coverage),'response_contiguous_bytes':self.contiguous,
            'response_highest_bytes':self.highest,'response_FIN_offset':self.fin_offset,
            'HTTP':self.http,'wire_complete':self.complete(),
            'completion_deadline':self.expires,'PID_or_slot_claimed':False,'TCP_policy_revision':7,'client_FIN_seen':self.client_FIN_seen,
            'server_FIN_ACK_seen':self.server_FIN_ACK_seen,'final_ACK_after_server_FIN_ACK':self.final_ACK_after_server_FIN_ACK,
            'repeated_client_FIN_after_server_FIN_ACK':self.repeated_client_FIN_after_server_FIN_ACK,
            'terminal_reset_count':len(self.terminal_reset_events),'terminal_reset_events':self.terminal_reset_events,
            'guest_terminal_RST_count':len(self.guest_terminal_RST_events),'guest_terminal_RST_events':self.guest_terminal_RST_events,
            'peer_stateless_RST_count':len(self.peer_stateless_RST_events),'peer_stateless_RST_events':self.peer_stateless_RST_events,
            'peer_RST_total_count':len(self.terminal_reset_events)+len(self.peer_stateless_RST_events),
            'terminal_budgets':{'bare_peer':8,'guest':127,'stateless_peer':127,'total_peer':128},
            'finACK_emission_count':len(self.finACK_emission_events),'finACK_emission_events':self.finACK_emission_events,
            'finACK_credit_count':max(0,len(self.finACK_emission_events)-1),'finACK_credits_consumed':len(self.guest_terminal_RST_events),
            'finACK_credits_available':sum(not e['baseline']and not e['guest_RST_consumed']for e in self.finACK_emission_events)}



class Policy:
    def __init__(self,nonce,clock=time.monotonic):
        if type(nonce)is not str or len(nonce)!=48 or any(c not in '0123456789abcdef'for c in nonce):raise ValueError('nonce')
        self.nonce,self.clock=nonce,clock;self.created=clock();self.last_clock=self.created
        if not math.isfinite(self.created):raise ValueError('clock')
        self.absolute_deadline=self.created+ABSOLUTE_SECONDS
        self.opened=self.active_deadline=self.last_packet=self.closing_at=None;self.quiet_at_close=None
        self.last_validated_host_packet=None;self.last_committed_host_frame_sequence=1
        self.error=None;self.transactions={};self.queries=[];self.replies=[];self.dns_port=None
        self.flows={};self.packet_counts=[0,0];self.packet_bytes=[0,0]
        self.request=PacketPolicy(nonce,clock=clock).request
    def fail(self,message):
        self.error=self.error or message;raise ValueError(self.error)
    def now(self):
        now=self.clock()
        if not math.isfinite(now) or now<self.last_clock:self.fail('monotonic clock')
        self.last_clock=now;return now
    def check_limits(self):
        if self.error:self.fail(self.error)
        now=self.now()
        if now>=self.absolute_deadline:self.fail('absolute420 deadline')
        if self.opened is None:
            if now-self.created>=ACCEPT_SECONDS:self.fail('accept240 deadline')
        else:
            if now>=self.active_deadline:self.fail('active180 deadline')
            if self.closing_at is not None and now-self.closing_at>CLOSE_SECONDS:self.fail('CLOSE40 deadline')
            if self.closing_at is None and now-self.last_packet>QUIET_SECONDS:self.fail('quiet150 deadline')
        return now
    def open(self):
        now=self.check_limits()
        if self.opened is not None:self.fail('duplicate OPEN')
        self.opened=self.last_packet=now
        self.active_deadline=min(now+ACTIVE_SECONDS,self.absolute_deadline)
    def begin_close(self):
        now=self.check_limits()
        if self.opened is None or self.closing_at is not None:self.fail('CLOSE phase')
        self.closing_at=now;self.quiet_at_close=now-self.last_packet
    def _packet(self,packet,direction):
        now=self.check_limits()
        if type(packet)is not bytes:self.fail('packet bytes')
        if self.opened is None:self.fail('packet before OPEN')
        self.packet_counts[direction]+=1;self.packet_bytes[direction]+=len(packet)
        if self.packet_counts[direction]>MAX_PACKETS or self.packet_bytes[direction]>MAX_PACKET_BYTES:self.fail('packet/byte cap')
        return now,parse_packet(packet)
    def outgoing(self,packet):
        try:
            now,v=self._packet(packet,0);proto,src,dst,sp,dp,body,payload=v
            if self.closing_at is not None:raise ValueError('packet after guest CLOSE')
            if src!=GUEST:raise ValueError('guest IPv4 source')
            if proto==17:
                if dst!=DNS or dp!=53 or sp<1024 or len(payload)!=29:raise ValueError('fixed mdns DNS tuple/size')
                ident,flags,qd,an,ns,ar=struct.unpack_from('!6H',payload)
                if not ident or (flags,qd,an,ns,ar)!=(0x100,1,0,0,0) or payload[12:]!=QUESTION:raise ValueError('fixed DNS A question')
                if self.dns_port is not None and sp!=self.dns_port:raise ValueError('owned mdns shared port changed')
                if len(self.queries)>=MAX_QUERY_PACKETS:raise ValueError('four actual query packet budget')
                if ident in self.transactions:
                    tx=self.transactions[ident]
                    if payload!=tx['query'] or now-tx['first_query']>10 or (tx['expires']is not None and now>=tx['expires']):raise ValueError('ambiguous expired/reused DNS ID')
                else:
                    tx={'id':ident,'query':payload,'first_query':now,'first_reply':None,'reply':None,
                        'addresses':[],'expires':None,'raw_ttl':None,'mode':None,'identity':None,'groups':None,'reply_count':0}
                    self.transactions[ident]=tx
                self.dns_port=sp
                self.queries.append({'elapsed_seconds':now-self.created,'transaction_id':ident,'source_port':sp,'query_hex':payload.hex()})
            else:
                key=(sp,dst,dp)
                if key not in self.flows:
                    if dp!=80 or sp<1024 or body[13]not in(2,0xc2)or payload:raise ValueError('new flow requires exact SYN02/C2')
                    if len(self.flows)>=MAX_FLOWS:raise ValueError('two TCP flow budget')
                    leases=[tx for tx in self.transactions.values()if tx['expires']is not None and now<tx['expires']and dst in tx['addresses']]
                    if not leases:raise ValueError('new SYN lacks unexpired observed A lease')
                    tx=min(leases,key=lambda x:x['first_reply'])
                    auth={'transaction_id':tx['id'],'first_canonical_answer_index':tx['addresses'].index(dst),
                        'admitted_at':now,'original_lease_expires_at':tx['expires']}
                    flow=Flow(self.nonce,key,auth,self.active_deadline,self.clock)
                    flow.outgoing(packet);self.flows[key]=flow
                else:self.flows[key].outgoing(packet)
            self.last_packet=now
        except (ValueError,IndexError,KeyError,struct.error)as error:self.fail(str(error))
    def incoming(self,packet):
        try:
            now,v=self._packet(packet,1);proto,src,dst,sp,dp,body,payload=v
            if dst!=GUEST:raise ValueError('reply destination')
            if proto==17:
                if self.closing_at is not None:raise ValueError('DNS after guest CLOSE')
                ident=int.from_bytes(payload[:2],'big')
                if src!=DNS or sp!=53 or dp!=self.dns_port or ident not in self.transactions:raise ValueError('DNS tuple/ID correlation')
                tx=self.transactions[ident]
                if now-tx['first_query']>10:raise ValueError('per-query DNS10 deadline')
                addresses,_old_cap,normalized,records=dns_reply_details(payload,tx['query'])
                mode,identity,groups,order=dns_duplicate_comparison(payload,normalized,records)
                event={'elapsed_seconds':now-self.created,'transaction_id':ident,'destination_port':dp,
                    'reply_hex':payload.hex(),'RRs':records,'comparison_mode':mode,
                    'sorted_TTL_groups':groups,'accepted':False,'duplicate':tx['reply']is not None}
                self.replies.append(event)
                if tx['reply']is not None:
                    if now>=tx['expires']:raise ValueError('duplicate outside original lease')
                    if mode!=tx['mode']or identity!=tx['identity']:raise ValueError('DNS duplicate identity changed')
                    if len(groups)!=len(tx['groups'])or any(len(a)!=len(b)or any(x>y for x,y in zip(a,b))for a,b in zip(groups,tx['groups'])):raise ValueError('DNS duplicate TTL increased')
                    if tx['reply_count']>=3:raise ValueError('DNS duplicate budget')
                else:
                    ttl=raw_chain_ttl(records)
                    tx.update(first_reply=now,reply=payload,addresses=addresses,raw_ttl=ttl,
                        expires=min(now+ttl,self.active_deadline,self.absolute_deadline),mode=mode,identity=identity)
                tx['groups']=groups;tx['reply_count']+=1;event['accepted']=True
                event['original_lease_expires_elapsed_seconds']=tx['expires']-self.created
                event['original_raw_chain_TTL']=tx['raw_ttl']
            else:
                key=(dp,src,sp)
                if key not in self.flows:raise ValueError('TCP reply has no admitted tuple')
                self.flows[key].incoming(packet)
            self.last_packet=now;self.last_validated_host_packet=packet
        except (ValueError,IndexError,KeyError,struct.error)as error:self.fail(str(error))
    def commit_forwarded(self,packet,frame):
        try:
            self.check_limits()
            if type(packet)is not bytes or packet!=self.last_validated_host_packet:raise ValueError('commit requires last validated host packet')
            if type(frame)is not bytes or len(frame)!=56+len(packet):raise ValueError('commit exact frame size')
            magic,kind,direction,reserved,size,header,sequence,nonce,crc,tail=struct.unpack('!8sBBHHHQ24sII',frame[:56])
            if (magic!=b'DLNET001'or kind!=2 or direction!=2 or reserved or tail or header!=56 or size!=len(packet)or
                nonce!=bytes.fromhex(self.nonce)or frame[56:]!=packet or not self.last_committed_host_frame_sequence<sequence<=127 or
                crc32c(frame[:48]+b'\0'*4+frame[52:])!=crc):raise ValueError('commit frame identity/CRC/order/global bound')
            view=parse_packet(packet)
            if view[0]==6:self.flows[(view[4],view[1],view[3])].commit_finACK_emission(packet,frame,sequence)
            self.last_committed_host_frame_sequence=sequence;self.last_validated_host_packet=None
        except (ValueError,KeyError,struct.error)as error:self.fail(str(error))
    def complete(self):
        now=self.clock()
        alive=(math.isfinite(now)and self.opened is not None and now<self.absolute_deadline and now<self.active_deadline and
            (self.closing_at is not None and now-self.closing_at<=CLOSE_SECONDS or self.closing_at is None and now-self.last_packet<=QUIET_SECONDS))
        return bool(alive and not self.error and len(self.flows)==MAX_FLOWS and all(f.complete()for f in self.flows.values()))
    def summary(self):
        return {'profile':PROFILE,'scope':'two-flow packet policy only, no PID/desktop/daemon/route verdict',
            'complete':self.complete(),'first_error':self.error,'DNS_source_port':self.dns_port,
            'query_count':len(self.queries),'query_events':self.queries,'reply_events':self.replies,
            'leases':[{'transaction_id':x['id'],'first_query_elapsed':x['first_query']-self.created,
                'first_reply_elapsed':x['first_reply']-self.created if x['first_reply']is not None else None,
                'original_raw_TTL':x['raw_ttl'],'original_expiry_elapsed':x['expires']-self.created if x['expires']is not None else None,
                'first_A_order':[str(ipaddress.IPv4Address(a))for a in x['addresses']]}for x in self.transactions.values()],
            'flows':[dict(flow_id=i+1,**f.summary())for i,f in enumerate(self.flows.values())],
            'packet_counts':self.packet_counts,'packet_bytes':self.packet_bytes,
            'created_clock':self.created,'opened_clock':self.opened,'active_deadline':self.active_deadline,
            'absolute_deadline':self.absolute_deadline,'closing_clock':self.closing_at,'last_packet_clock':self.last_packet,
            'quiet_before_close_seconds':self.quiet_at_close,
            'request_expected_hex':self.request.hex(),'parser_dependency_SHA256':SOURCE_SHA256,
            'last_committed_host_frame_sequence':self.last_committed_host_frame_sequence}
