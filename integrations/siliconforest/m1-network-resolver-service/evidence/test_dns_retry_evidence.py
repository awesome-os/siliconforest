"""Synthetic packet tests only. No DNS/socket/target operation."""
import copy
import importlib.util
import ipaddress
import json
from pathlib import Path
import struct
import unittest
import dns_retry_evidence as subject

QUERY=struct.pack('!6H',0x3456,0x100,1,0,0,0)+subject.QUESTION
A='93.184.215.14';B='172.66.147.243'


def checksum(raw):
    if len(raw)%2:raw+=b'\0'
    value=sum(struct.unpack('!%dH'%(len(raw)//2),raw))
    while value>>16:value=(value&65535)+(value>>16)
    return value^65535


def packet(payload,reply=False,port=60001,udp_crc=True):
    src,dst=(subject.DNS,subject.GUEST) if reply else (subject.GUEST,subject.DNS)
    udp=struct.pack('!4H',53 if reply else port,port if reply else 53,len(payload)+8,0)+payload
    if udp_crc:
        crc=checksum(src+dst+b'\0\x11'+len(udp).to_bytes(2,'big')+udp) or 65535
        udp=udp[:6]+crc.to_bytes(2,'big')+udp[8:]
    ip=struct.pack('!BBHHHBBH4s4s',0x45,0,20+len(udp),1,0x4000,64,17,0,src,dst)
    return ip[:10]+checksum(ip).to_bytes(2,'big')+ip[12:]+udp


def rr(ip=A,ttl=300,owner=b'\xc0\x0c',kind=1,klass=1,data=None):
    if data is None:data=ipaddress.IPv4Address(ip).packed
    return owner+struct.pack('!HHIH',kind,klass,ttl,len(data))+data


def reply(records=None,ns=(),ar=(),flags=0x8180):
    if records is None:records=[rr()]
    return struct.pack('!6H',0x3456,flags,1,len(records),len(ns),len(ar))+subject.QUESTION+b''.join(records)+b''.join(ns)+b''.join(ar)


def backend(queries,replies,qtimes=None,rtimes=None):
    """Production backend generates metadata; independent parser checks it.

    Explicit parser expectations and isolated checker assertions below prevent a
    shared metadata generator from concealing wire/TTL-rule regressions.
    """
    root=Path(__file__).resolve().parents[2]
    source=root/'m1-network-resolver-egress/revisions/002/host/policy.py'
    spec=importlib.util.spec_from_file_location('actual_backend_policy',source)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    now=[100.0];policy=module.Policy('a'*48,lambda:now[0])
    if qtimes is None:qtimes=[float(i) for i in range(len(queries))]
    if rtimes is None:rtimes=[0.25+i for i in range(len(replies))]
    operations=sorted([(t,0,q) for t,q in zip(qtimes,queries)]+[(t,1,r) for t,r in zip(rtimes,replies)])
    for t,direction,raw in operations:
        now[0]=100+t
        (policy.incoming if direction else policy.outgoing)(raw)
    return policy.summary()


def evidence(raws=None,qtimes=None,rtimes=None):
    if raws is None:raws=[reply(),reply([rr(ttl=299)])]
    q=[packet(QUERY) for _ in raws];r=[packet(v,True) for v in raws]
    return q,r,backend(q,r,qtimes,rtimes)


class RetryEvidence(unittest.TestCase):
    def accepted(self,args):
        result=subject.dns_retry_evidence(*args)
        self.assertEqual(set(result['checks']),set(subject.CHECK_KEYS))
        self.assertTrue(all(result['checks'].values()),result)
        self.assertEqual(result['details']['not_evaluated'],[])
        json.dumps(result,allow_nan=False)
        return result

    def rejected(self,args,key=None):
        result=subject.dns_retry_evidence(*args)
        self.assertFalse(all(result['checks'].values()),result)
        if key:self.assertFalse(result['checks'][key],result)
        json.dumps(result,allow_nan=False)
        return result

    def test_actual_backend_metadata_ttl_decrease_equal_and_three_replies(self):
        for ttls in ((300,299),(300,300),(300,299,298)):
            with self.subTest(ttls=ttls):self.accepted(evidence([reply([rr(ttl=t)]) for t in ttls]))

    def test_rotation_retains_first_answer_order(self):
        args=evidence([reply([rr(A,65),rr(B,65)]),reply([rr(B,64),rr(A,64)])])
        result=self.accepted(args)
        self.assertEqual(args[2]['answer_ipv4'],[A,B])
        self.assertTrue(result['details']['replies'][1]['answer_order_changed_from_first'])
        self.assertEqual(result['details']['comparison_mode'],'a_only_answer_multiset')

    def test_duplicates_cardinality_and_sorted_ttl_vector(self):
        args=evidence([reply([rr(A,50),rr(A,40),rr(B,45)]),reply([rr(B,44),rr(A,39),rr(A,49)])])
        self.accepted(args)
        q,r,b=args;r[1]=packet(reply([rr(B,44),rr(A,41),rr(A,49)]),True)
        self.rejected((q,r,b),'ttl_nonincrease')

    def test_explicit_parser_offsets_and_normalized_bytes(self):
        raw=reply([rr(A,300),rr(B,299)])
        value=subject.parse_dns_reply(raw,QUERY)
        self.assertEqual([r['ttl_offset'] for r in value['records']],[35,51])
        expected=bytearray(raw);expected[35:39]=bytes(4);expected[51:55]=bytes(4)
        self.assertEqual(value['normalized_hex'],expected.hex())
        self.assertEqual(value['records'][0],dict(section='answer',index=0,owner='example.com',
          type=1,**{'class':1},ttl_offset=35,ttl=300,rdata_hex=ipaddress.IPv4Address(A).packed.hex(),
          decoded_domain=None,normalized_wire_hex=(b'\xc0\x0c\0\1\0\1'+bytes(4)+b'\0\4'+ipaddress.IPv4Address(A).packed).hex()))

    def test_per_key_ttl_credit_cannot_transfer(self):
        q,r,b=evidence([reply([rr(A,40),rr(B,60)]),reply([rr(B,59),rr(A,39)])])
        r[1]=packet(reply([rr(B,39),rr(A,59)]),True)
        self.rejected((q,r,b),'ttl_nonincrease')

    def test_ttl_cannot_increase_after_previous_drop(self):
        q,r,b=evidence([reply([rr(ttl=t)]) for t in (60,40,39)])
        r[2]=packet(reply([rr(ttl=50)]),True)
        self.rejected((q,r,b),'ttl_nonincrease')

    def test_identity_changes_fail_before_backend_metadata(self):
        args=evidence([reply([rr(A),rr(B)]),reply([rr(B,299),rr(A,299)])])
        variants=[reply([rr(B,299),rr('1.1.1.1',299)]),reply([rr(A,299)]),
          reply([rr(B,299),rr(A,299,owner=b'\x07example\x03com\0')]),
          reply([rr(B,299),rr(A,299,owner=b'\x07EXAMPLE\x03com\0')]),
          reply([rr(B,299),rr(A,299)],flags=0x8580)]
        for raw in variants:
            with self.subTest(raw=raw.hex()):
                q,r,b=copy.deepcopy(args);r[1]=packet(raw,True)
                self.rejected((q,r,b),'non_ttl_identity')

    def test_nonpure_strict_wire_order_and_opaque_rdata(self):
        first=reply([rr(A),rr(B)],ar=[rr(kind=65000,data=b'opaque',ttl=42)])
        second=reply([rr(A,299),rr(B,299)],ar=[rr(kind=65000,data=b'opaque',ttl=41)])
        args=evidence([first,second]);self.accepted(args)
        for raw in [reply([rr(B,299),rr(A,299)],ar=[rr(kind=65000,data=b'opaque',ttl=41)]),
                    reply([rr(A,299),rr(B,299)],ar=[rr(kind=65000,data=b'opaquf',ttl=41)])]:
            q,r,b=copy.deepcopy(args);r[1]=packet(raw,True)
            self.rejected((q,r,b),'non_ttl_identity')

    def test_cname_and_soa_minimum_immutable(self):
        alias=b'\x03www\xc0\x0c'
        answer=[rr(kind=5,data=alias,ttl=50),rr(A,60,owner=alias)]
        self.accepted(evidence([reply(answer),reply([rr(kind=5,data=alias,ttl=49),rr(A,59,owner=alias)])]))
        soa=b'\xc0\x0c\xc0\x0c'+struct.pack('!5I',1,2,3,4,5)
        args=evidence([reply(ns=[rr(kind=6,data=soa,ttl=300)]),reply([rr(ttl=299)],ns=[rr(kind=6,data=soa,ttl=299)])])
        self.accepted(args)
        q,r,b=args;r[1]=packet(reply([rr(ttl=299)],ns=[rr(kind=6,data=soa[:-1]+b'\x06',ttl=299)]),True)
        self.rejected((q,r,b),'non_ttl_identity')

    def test_compression_semantic_alias_to_ttl_not_normalizable(self):
        first=reply([rr(ttl=0x01610000),rr(B,40,owner=b'\xc0\x23')])
        second=reply([rr(ttl=0x01600000),rr(B,39,owner=b'\xc0\x23')])
        q=[packet(QUERY)]*2;r=[packet(first,True),packet(second,True)]
        b=backend(q,[r[0],r[0]])
        result=self.rejected((q,r,b),'non_ttl_identity')
        self.assertTrue(result['checks']['reply_dns_structure'])

    def test_dns_structure_truncations_counts_pointer_loops_edns(self):
        valid=reply();malformed=[valid[:i] for i in range(len(valid))]
        malformed += [valid+b'\0',reply([rr(owner=b'\xc0\x1d')]),reply([rr(owner=b'\xc0\xff')]),
          reply([rr(owner=b'\x40x')]),reply([rr(owner=b'\x01.\0')]),reply([rr(data=b'xxx')]),
          reply(ar=[rr(kind=41)]),reply([rr(ttl=0)]),reply([rr('127.0.0.1')]),
          reply([rr(klass=2)]),reply([rr(kind=5,data=b'\xc0\x0c')]),
          reply([rr(kind=5,data=b'\xc0\x0c\0')]),reply([rr()],flags=0x8380)]
        for offset,value in ((4,0),(6,33),(8,17),(10,17)):
            raw=bytearray(valid);struct.pack_into('!H',raw,offset,value);malformed.append(bytes(raw))
        for raw in malformed:
            with self.subTest(raw=raw.hex()):
                with self.assertRaises(ValueError):subject.parse_dns_reply(raw,QUERY)

    def test_udp_checksums_lengths_fragment_headers_ports(self):
        args=evidence()
        for index in (0,2,6,9,10,12,16,20,22,24,26,30):
            q,r,b=copy.deepcopy(args);raw=bytearray(r[0]);raw[index]^=1;r[0]=bytes(raw)
            with self.subTest(index=index):self.rejected((q,r,b),'wire_packet_bounds')
        q,r,b=evidence();r=[packet(reply(),True,udp_crc=False),packet(reply([rr(ttl=299)]),True,udp_crc=False)]
        self.accepted((q,r,b))

    def test_changed_dns_id_question_and_session_port(self):
        args=evidence()
        for payload in (bytes([0])+QUERY[1:],QUERY[:12]+b'\x07EXAMPLE'+QUERY[20:]):
            q,r,b=copy.deepcopy(args);q[1]=packet(payload)
            self.rejected((q,r,b),'query_identity')
        q,r,b=copy.deepcopy(args);r[1]=packet(reply([rr(ttl=299)]),True,60002)
        self.rejected((q,r,b),'tuple_and_id')
        q,r,b=copy.deepcopy(args);raw=bytearray(reply());raw[0]^=1;r[1]=packet(bytes(raw),True)
        self.rejected((q,r,b),'reply_dns_structure')

    def test_budgets_and_types(self):
        args=evidence()
        for q,r in (([],args[1]),(args[0],[]),(args[0]*2,args[1]),(tuple(args[0]),args[1])):
            self.rejected((q,r,args[2]),'wire_packet_bounds')
        for key,value in (('dns_attempts',True),('dns_duplicates',0)):
            q,r,b=copy.deepcopy(args);b[key]=value;self.rejected((q,r,b),'retry_budgets')

    def test_event_raw_ttls_comparison_and_original_record_mutants(self):
        args=evidence()
        reply_mutations={'reply_hex':'00','source_port':54,'destination_port':60002,'transaction_id':1,
          'accepted':False,'duplicate':False,'rr_ttls':[300], 'rrs':[],
          'normalized_dns_sha256':'0'*64,'comparison_mode':'normalized_wire_identity',
          'comparison_identity_sha256':'0'*64,'comparison_sorted_ttl_groups':[[400]],
          'answer_order_changed_from_first':True,'answer_order_changed_from_previous':True}
        for key,value in reply_mutations.items():
            q,r,b=copy.deepcopy(args);b['dns_reply_events'][1][key]=value
            with self.subTest(key=key):self.rejected((q,r,b),'backend_reply_events')
        for key,value in {'query_hex':'00','attempt':2,'retry':True,'source_port':5,'after_validated_reply':1}.items():
            q,r,b=copy.deepcopy(args);b['dns_query_events'][0][key]=value
            self.rejected((q,r,b),'backend_query_events')
        for key,value in {'dns_first_reply_rrs':[], 'dns_answer_records':[], 'dns_reply_hex':'00',
                          'dns_last_accepted_rr_ttls':[300],'dns_source_port':1}.items():
            q,r,b=copy.deepcopy(args);b[key]=value;self.rejected((q,r,b),'backend_original_records')
        for key in ('dns_query_events','dns_reply_events'):
            q,r,b=copy.deepcopy(args);b[key].append(copy.deepcopy(b[key][0]))
            self.rejected((q,r,b))

    def test_original_answer_allowlist_and_lease_never_replaced(self):
        for key,value in (('answer_ipv4',[B]),('lease_seconds',61)):
            q,r,b=evidence();b[key]=value;self.rejected((q,r,b),'original_answers_preserved')

    def test_lease_and_time_bounds(self):
        self.accepted(evidence([reply([rr(ttl=60)]),reply([rr(ttl=59)])],qtimes=[0,9],rtimes=[.25,10]))
        args=evidence([reply([rr(ttl=2)]),reply([rr(ttl=1)])],qtimes=[0,1],rtimes=[.25,2])
        self.accepted(args)
        for key,value in (('original_lease_expires_elapsed_seconds',3.25),
                          ('first_accepted_elapsed_seconds',.5),('original_lease_remaining_seconds',2)):
            q,r,b=copy.deepcopy(args);b['dns_reply_events'][1][key]=value
            self.rejected((q,r,b),'original_lease')
        q,r,b=copy.deepcopy(args);b['dns_reply_events'][1]['elapsed_seconds']=2.25
        b['dns_reply_events'][1]['original_lease_remaining_seconds']=0
        self.rejected((q,r,b),'original_lease')
        q,r,b=evidence();b['dns_first_query_elapsed_seconds']=.26
        self.rejected((q,r,b),'reply_window')
        q,r,b=evidence();b['dns_reply_events'][1]['elapsed_seconds']=10.001
        self.rejected((q,r,b),'reply_window')
        for value in (True,-1,float('nan'),float('inf'),'1'):
            q,r,b=evidence();b['dns_reply_events'][0]['elapsed_seconds']=value
            self.rejected((q,r,b),'backend_reply_events')

    def test_policy_revision_and_missing_evidence_fail_closed(self):
        for key in ('dns_policy_revision','dns_duplicate_policy','dns_query_profile','dns_id_source','unsupported_traffic_action'):
            q,r,b=evidence();del b[key];self.rejected((q,r,b),'source_policy_revision')
        q,r,b=evidence();del b['dns_first_query_elapsed_seconds'];self.rejected((q,r,b),'reply_window')
        for b in ({},None,[],True):self.rejected(([packet(QUERY)],[packet(reply(),True)],b))
        result=self.rejected(([],[],{}));self.assertTrue(result['details']['not_evaluated'])


if __name__=='__main__':unittest.main()
