"""Original bounded two-flow packet policy. No sockets/processes/host resolver.

Wire completeness is not a desktop, PID attribution, daemon or route verdict.
"""
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
    def incoming(self,packet):
        view=parse_packet(packet)
        if view[0]!=6:raise ValueError('flow accepts TCP only')
        body,payload=view[5:]
        if self.server_isn is not None and not body[13]&2:
            start=(int.from_bytes(body[4:8],'big')-self.server_isn-1)&0xffffffff
            if (payload and start+len(payload)>MAX_RESPONSE) or start>MAX_RESPONSE+1:
                raise ValueError('HTTP per-flow2048 response cap')
        return super().incoming(packet)
    def outgoing(self,packet):
        if parse_packet(packet)[0]!=6:raise ValueError('flow accepts TCP only')
        return super().outgoing(packet)
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
            'completion_deadline':self.expires,'PID_or_slot_claimed':False}


class Policy:
    def __init__(self,nonce,clock=time.monotonic):
        if type(nonce)is not str or len(nonce)!=48 or any(c not in '0123456789abcdef'for c in nonce):raise ValueError('nonce')
        self.nonce,self.clock=nonce,clock;self.created=clock();self.last_clock=self.created
        if not math.isfinite(self.created):raise ValueError('clock')
        self.absolute_deadline=self.created+ABSOLUTE_SECONDS
        self.opened=self.active_deadline=self.last_packet=self.closing_at=None
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
        self.closing_at=now
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
            self.last_packet=now
        except (ValueError,IndexError,KeyError,struct.error)as error:self.fail(str(error))
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
            'quiet_before_close_seconds':self.closing_at-self.last_packet if self.closing_at is not None else None,
            'request_expected_hex':self.request.hex(),'parser_dependency_SHA256':SOURCE_SHA256}
