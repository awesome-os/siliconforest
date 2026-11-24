#!/usr/bin/env python3
"""Synthetic byte-level policy negatives; no sockets or external requests."""
from pathlib import Path
import socket
import struct
import sys
import unittest
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from policy import Policy, GUEST, DNS, QUESTION, dns_answer, dns_name, dns_reply_details, http_response
from packet_fixture import udp, tcp

NONCE = "a" * 48
PUBLIC = socket.inet_aton("93.184.216.34")  # synthetic fixture, not a lookup
QUERY = struct.pack("!6H", 0x1234, 0x0100, 1, 0, 0, 0) + QUESTION


def rr(owner=b"\xc0\x0c", kind=1, ttl=300, data=PUBLIC):
    return owner + struct.pack("!HHIH", kind, 1, ttl, len(data)) + data


def answer(answers=None, flags=0x8180, ident=0x1234, extra=b"", ar=0):
    answers = [rr()] if answers is None else answers
    return (struct.pack("!6H", ident, flags, 1, len(answers), 0, ar) + QUESTION +
            b"".join(answers) + extra)


def authorized():
    p = Policy(NONCE)
    p.outgoing(udp(GUEST, DNS, 43001, 53, QUERY))
    p.incoming(udp(DNS, GUEST, 53, 43001, answer()))
    return p


def connected():
    p = authorized()
    p.outgoing(tcp(GUEST, PUBLIC, 43002, 80, 10000, 0, 2))
    p.incoming(tcp(PUBLIC, GUEST, 80, 43002, 20000, 10001, 0x12))
    return p


class DNSPolicy(unittest.TestCase):
    def test_correlated_bound_and_lease(self):
        p = authorized()
        self.assertIsNone(p.endpoint)
        self.assertEqual(p.allowed_addresses,[PUBLIC])
        self.assertEqual(p.lease_seconds, 60)
        p.clock = lambda: p.expires
        with self.assertRaisesRegex(ValueError, "expired"):
            p.outgoing(tcp(GUEST, PUBLIC, 43002, 80, 1, 0, 2))

    def test_no_tcp_before_dns_or_other_host_listener(self):
        for address, port in ((PUBLIC, 80), (bytes([10,0,2,2]), 22)):
            with self.subTest(address=address), self.assertRaises(ValueError):
                Policy(NONCE).outgoing(tcp(GUEST, address, 43002, port, 1, 0, 2))
        p = authorized()
        for address, port in ((bytes([10,0,2,2]),80), (PUBLIC,443), (socket.inet_aton("1.1.1.1"),80)):
            with self.subTest(address=address,port=port), self.assertRaises(ValueError):
                p.outgoing(tcp(GUEST,address,43002,port,1,0,2))

    def test_wrong_transaction_question_flags_counts(self):
        bad = [answer(ident=3), answer(flags=0x8380), answer(flags=0x8183),
               answer(flags=0x8980), answer(flags=0x8080), answer(flags=0x81c0),
               answer(answers=[]), answer()+b"trailing"]
        changed = bytearray(answer()); changed[13] = ord('X'); bad.append(bytes(changed))
        for value in bad:
            with self.subTest(value=value[:12]), self.assertRaises(ValueError):
                dns_answer(value,QUERY)

    def test_private_multicast_reserved_or_zero_ttl(self):
        for addr in ("127.0.0.1","10.0.2.2","10.0.2.3","169.254.1.2","224.0.0.1","0.0.0.0","192.0.2.1"):
            with self.subTest(addr=addr), self.assertRaises(ValueError):
                dns_answer(answer([rr(data=socket.inet_aton(addr))]),QUERY)
        with self.assertRaises(ValueError):
            dns_answer(answer([rr(ttl=0)]),QUERY)
        with self.assertRaises(ValueError):
            dns_answer(answer([rr(),rr(data=bytes([10,0,2,2]))]),QUERY)

    def test_cname_chain_and_unrelated_glue(self):
        alias=b"\x03www\x07example\x03com\0"
        data=answer([rr(kind=5,data=alias),rr(owner=alias)],
            extra=rr(owner=b"\x04evil\x04test\0",data=bytes([10,0,2,2])),ar=1)
        addresses,ttl=dns_answer(data,QUERY)
        self.assertEqual(addresses,[PUBLIC]);self.assertEqual(ttl,60)
        for bad in (answer([rr(kind=5,data=b"\xc0\x0c")]),
                    answer([rr(kind=5,data=alias),rr()]),
                    answer([rr(owner=alias)])):
            with self.assertRaises(ValueError):
                dns_answer(bad,QUERY)

    def test_pointer_and_rr_bounds(self):
        for data,offset in ((b"x"*12+b"\xc0\x0c",12),
                            (b"x"*12+b"\xc0\xff",12),
                            (b"x"*12+b"\xc0",12),
                            (b"x"*12+b"\x40",12)):
            with self.assertRaises(ValueError):
                dns_name(data,offset)
        for data in (answer()[:-1],answer([rr(data=b"123")])):
            with self.assertRaises(ValueError):
                dns_answer(data,QUERY)

    def test_wrong_reply_port_and_query_retry(self):
        p=Policy(NONCE);p.outgoing(udp(GUEST,DNS,43001,53,QUERY))
        for src,sp,dp in ((DNS,54,43001),(DNS,53,43002),(bytes([10,0,2,2]),53,43001)):
            with self.assertRaises(ValueError):
                p.incoming(udp(src,GUEST,sp,dp,answer()))
        p.outgoing(udp(GUEST,DNS,43001,53,QUERY));p.outgoing(udp(GUEST,DNS,43001,53,QUERY))
        with self.assertRaisesRegex(ValueError,"retry"):
            p.outgoing(udp(GUEST,DNS,43001,53,QUERY))


class NativeResolverPolicy(unittest.TestCase):
    def test_daemon_id_and_ephemeral_port_are_observed(self):
        for ident in (1,0x1234,0xffff):
            p=Policy(NONCE)
            query=struct.pack('!6H',ident,0x100,1,0,0,0)+QUESTION
            p.outgoing(udp(GUEST,DNS,65535,53,query))
            p.incoming(udp(DNS,GUEST,53,65535,answer(ident=ident)))
            self.assertEqual(p.summary()['dns_transaction_id'],ident)
            self.assertEqual(p.summary()['dns_source_port'],65535)
            self.assertIsNone(p.endpoint)
            self.assertEqual(p.summary()['dns_query_events'][0]['query_hex'],query.hex())

    def test_rfc3484_may_select_second_answer_but_only_one_flow(self):
        other=socket.inet_aton('1.1.1.1')
        p=Policy(NONCE)
        p.outgoing(udp(GUEST,DNS,53000,53,QUERY))
        p.incoming(udp(DNS,GUEST,53,53000,answer([rr(),rr(data=other,ttl=37)])))
        self.assertIsNone(p.endpoint)
        p.outgoing(tcp(GUEST,other,53001,80,1,0,2))
        self.assertEqual(p.endpoint,other)
        self.assertEqual(p.summary()['selected_answer_index'],1)
        self.assertEqual(p.summary()['dns_answer_records'][1]['ttl'],37)
        self.assertEqual(p.lease_seconds,37)
        with self.assertRaises(ValueError):
            p.outgoing(tcp(GUEST,PUBLIC,53001,80,1,0,2))

    def test_second_flow_same_address_also_refused(self):
        p=authorized();p.outgoing(tcp(GUEST,PUBLIC,43002,80,1,0,2))
        with self.assertRaises(ValueError):
            p.outgoing(tcp(GUEST,PUBLIC,43003,80,1,0,2))

    def test_queries_must_stay_identical_including_id_and_port(self):
        variants=[(43002,QUERY),(43001,b'\xab\xcd'+QUERY[2:])]
        for port,query in variants:
            p=Policy(NONCE);p.outgoing(udp(GUEST,DNS,43001,53,QUERY))
            with self.assertRaisesRegex(ValueError,'changed'):
                p.outgoing(udp(GUEST,DNS,port,53,query))

    def test_identical_inflight_query_retry_after_reply_is_recorded(self):
        p=authorized();p.outgoing(udp(GUEST,DNS,43001,53,QUERY))
        self.assertTrue(p.summary()['dns_query_events'][1]['retry'])
        self.assertTrue(p.summary()['dns_query_events'][1]['after_validated_reply'])
        self.assertEqual(p.dns_attempts,2)

    def test_reply_duplicates_do_not_extend_lease(self):
        p=authorized();expires=p.expires
        p.incoming(udp(DNS,GUEST,53,43001,answer()))
        p.incoming(udp(DNS,GUEST,53,43001,answer()))
        self.assertEqual(p.expires,expires)
        self.assertEqual(len(p.summary()['dns_reply_events']),3)
        with self.assertRaises(ValueError):
            p.incoming(udp(DNS,GUEST,53,43001,answer()))

    def test_changed_correlated_answer_is_refused(self):
        p=authorized()
        with self.assertRaises(ValueError):
            p.incoming(udp(DNS,GUEST,53,43001,answer([rr(data=socket.inet_aton("1.1.1.1"))])))

    def test_edns_query_reply_and_other_local_traffic_refused(self):
        opt=b'\0'+struct.pack('!HHIH',41,1232,0,0)
        edns=struct.pack('!6H',0x1234,0x100,1,0,0,1)+QUESTION+opt
        for query in (edns,b'\0\0'+QUERY[2:],QUERY.replace(b'example',b'EXAMPLE'),
                QUERY[:-4]+b'\0\x1c\0\1'):
            with self.assertRaises(ValueError):
                Policy(NONCE).outgoing(udp(GUEST,DNS,43001,53,query))
        with self.assertRaisesRegex(ValueError,'OPT'):
            dns_answer(answer(extra=opt,ar=1),QUERY)
        with self.assertRaises(ValueError):
            Policy(NONCE).outgoing(udp(GUEST,socket.inet_aton('224.0.0.251'),5353,5353,QUERY))

    def test_dns_reply_deadline(self):
        p=Policy(NONCE,clock=lambda:10)
        p.outgoing(udp(GUEST,DNS,43001,53,QUERY));p.clock=lambda:20.001
        with self.assertRaisesRegex(ValueError,'deadline'):
            p.incoming(udp(DNS,GUEST,53,43001,answer()))

    def test_no_endpoint_without_syn(self):
        for flags in (0,0x10,0x12,0x18,0x42):
            p=authorized()
            with self.assertRaises(ValueError):
                p.outgoing(tcp(GUEST,PUBLIC,43002,80,1,0,flags))
            self.assertIsNone(p.endpoint)

    def test_cname_ttls_and_raw_rdata_remain_observed(self):
        alias=b'\x03www\x07example\x03com\0'
        p=Policy(NONCE);p.outgoing(udp(GUEST,DNS,43001,53,QUERY))
        raw=answer([rr(kind=5,ttl=9,data=alias),rr(owner=alias,ttl=30)])
        p.incoming(udp(DNS,GUEST,53,43001,raw))
        self.assertEqual(p.lease_seconds,9)
        self.assertEqual(p.summary()['dns_answer_records'][0]['rdata_hex'],alias.hex())
        self.assertEqual(p.summary()['dns_reply_events'][0]['reply_hex'],raw.hex())

    def test_full_request_is_same_186_bytes(self):
        p=Policy(NONCE)
        self.assertEqual(len(p.request),186)
        self.assertEqual(p.request.count(NONCE.encode()),1)
        self.assertIn(b'User-Agent: darwin-lab-egress/1\r\n',p.request)


class DuplicateTTLPolicy(unittest.TestCase):
    def policy(self,first=None,clock=None):
        t=[10.] if clock is None else clock
        p=Policy(NONCE,clock=lambda:t[0]);p.outgoing(udp(GUEST,DNS,43001,53,QUERY))
        p.incoming(udp(DNS,GUEST,53,43001,answer() if first is None else first))
        return p,t

    def test_falling_ttls_accepted_first_reply_endpoint_and_lease_stay_fixed(self):
        p,t=self.policy();original=p.reply;expires=p.expires
        t[0]+=1;p.incoming(udp(DNS,GUEST,53,43001,answer([rr(ttl=299)])))
        t[0]+=1;p.incoming(udp(DNS,GUEST,53,43001,answer([rr(ttl=298)])))
        self.assertEqual((p.reply,p.expires,p.allowed_addresses),(original,expires,[PUBLIC]))
        self.assertEqual(p.summary()['dns_first_reply_rrs'][0]['ttl'],300)
        self.assertEqual(p.previous_reply_ttls,[298]);self.assertEqual(p.dns_duplicates,2)
        self.assertEqual([x['rr_ttls'] for x in p.reply_events],[[300],[299],[298]])
        self.assertTrue(all(x['accepted'] for x in p.reply_events))
        self.assertEqual(len({x['normalized_dns_sha256'] for x in p.reply_events}),1)
        self.assertEqual(p.reply_events[-1]['original_lease_remaining_seconds'],58)
        with self.assertRaisesRegex(ValueError,'duplicate budget'):
            p.incoming(udp(DNS,GUEST,53,43001,answer([rr(ttl=297)])))

    def test_individual_ttl_increase_even_below_first_is_refused(self):
        for raised in (300,301):
            p,t=self.policy();t[0]+=1
            p.incoming(udp(DNS,GUEST,53,43001,answer([rr(ttl=299)])))
            with self.assertRaisesRegex(ValueError,'TTL increased'):
                p.incoming(udp(DNS,GUEST,53,43001,answer([rr(ttl=raised)])))
            self.assertFalse(p.reply_events[-1]['accepted'])

    def test_each_RR_all_sections_not_only_minimum_or_answer(self):
        ns=rr(owner=b'\x03net\x07example\0',kind=2,ttl=500,data=b'\x02ns\xc0\x0c')
        ar=rr(owner=b'\x04glue\x07example\0',ttl=600,data=PUBLIC)
        first=answer([rr(ttl=300)],extra=ns+ar,ar=1)
        first=first[:8]+b'\0\1'+first[10:]
        p,t=self.policy(first);_,_,normalized,records=dns_reply_details(first,QUERY)
        self.assertEqual([x['section'] for x in records],['answer','authority','additional'])
        self.assertEqual([x['ttl'] for x in records],[300,500,600])
        for i in range(3):
            p,t=self.policy(first);bad=bytearray(first)
            for j,r in enumerate(records):struct.pack_into('!I',bad,r['ttl_offset'],r['ttl']+(1 if j==i else -1))
            with self.assertRaisesRegex(ValueError,'TTL increased'):
                p.incoming(udp(DNS,GUEST,53,43001,bytes(bad)))
        p,t=self.policy(first);good=bytearray(first)
        for r in records:struct.pack_into('!I',good,r['ttl_offset'],r['ttl']-1)
        p.incoming(udp(DNS,GUEST,53,43001,bytes(good)))
        self.assertEqual(dns_reply_details(bytes(good),QUERY)[2],normalized)

    def test_nonTTL_header_owner_data_and_compression_change_refused(self):
        other=socket.inet_aton('1.1.1.1')
        first=answer([rr(),rr(data=other)])
        variants=[answer([rr(),rr(data=other)],flags=0x8100),
            answer([rr(ttl=299),rr(data=socket.inet_aton('8.8.8.8'))]),
            answer([rr(owner=b'\x07example\x03com\0'),rr(data=other)])]
        for raw in variants:
            p,t=self.policy(first)
            with self.subTest(raw=raw.hex()),self.assertRaisesRegex(ValueError,'identity changed'):
                p.incoming(udp(DNS,GUEST,53,43001,raw))

    def test_duplicate_outside_original_lease_fails_without_extension(self):
        p,t=self.policy(answer([rr(ttl=2)]));expires=p.expires;t[0]=expires
        with self.assertRaisesRegex(ValueError,'outside original lease'):
            p.incoming(udp(DNS,GUEST,53,43001,answer([rr(ttl=1)])))
        self.assertEqual(p.expires,expires)

    def test_id_port_question_and_zeroTTL_still_refused(self):
        variants=[(DNS,53,43001,answer(ident=3)),(DNS,54,43001,answer([rr(ttl=299)])),
            (DNS,53,43002,answer([rr(ttl=299)])),(DNS,53,43001,answer([rr(ttl=0)])),
            (DNS,53,43001,answer([rr(ttl=299)]).replace(b'example',b'EXAMPLE'))]
        for src,sp,dp,raw in variants:
            p,t=self.policy()
            with self.subTest(sp=sp,dp=dp,raw=raw.hex()),self.assertRaises(ValueError):
                p.incoming(udp(src,GUEST,sp,dp,raw))

    def test_duplicate_after_TCP_selection_cannot_move_endpoint(self):
        p,t=self.policy();p.outgoing(tcp(GUEST,PUBLIC,43002,80,10000,0,2))
        p.incoming(udp(DNS,GUEST,53,43001,answer([rr(ttl=299)])))
        self.assertEqual((p.endpoint,p.tcp_port,p.client_isn),(PUBLIC,43002,10000))


class PureARotationPolicy(unittest.TestCase):
    def first(self,raw):
        p=Policy(NONCE);p.outgoing(udp(GUEST,DNS,43001,53,QUERY));p.incoming(udp(DNS,GUEST,53,43001,raw));return p
    def test_rotation_preserves_first_order_endpoint_and_lease(self):
        other=socket.inet_aton('1.1.1.1');first=answer([rr(ttl=300),rr(data=other,ttl=400)])
        p=self.first(first);expires=p.expires
        p.incoming(udp(DNS,GUEST,53,43001,answer([rr(data=other,ttl=399),rr(ttl=299)])))
        self.assertEqual(p.allowed_addresses,[PUBLIC,other]);self.assertEqual(p.reply,first);self.assertEqual(p.expires,expires)
        event=p.reply_events[-1]
        self.assertEqual(event['comparison_mode'],'a_only_answer_multiset')
        self.assertTrue(event['answer_order_changed_from_first']);self.assertTrue(event['accepted'])
        self.assertEqual(p.reply_events[0]['comparison_identity_sha256'],event['comparison_identity_sha256'])
        self.assertNotEqual(p.reply_events[0]['normalized_dns_sha256'],event['normalized_dns_sha256'])
        self.assertEqual(event['original_lease_expires_elapsed_seconds'],p.original_lease_expires_elapsed_seconds)
        self.assertEqual(event['original_lease_remaining_seconds'],max(0,p.original_lease_expires_elapsed_seconds-event['elapsed_seconds']))
        p.outgoing(tcp(GUEST,other,43002,80,1,0,2))
        self.assertEqual(p.summary()['selected_answer_index'],1)

    def test_duplicate_counts_and_per_key_TTL_matching(self):
        other=socket.inet_aton('1.1.1.1');first=answer([rr(ttl=300),rr(ttl=200),rr(data=other,ttl=400)])
        p=self.first(first)
        p.incoming(udp(DNS,GUEST,53,43001,answer([rr(ttl=199),rr(data=other,ttl=399),rr(ttl=299)])))
        with self.assertRaisesRegex(ValueError,'TTL increased'):
            p.incoming(udp(DNS,GUEST,53,43001,answer([rr(ttl=200),rr(data=other,ttl=398),rr(ttl=298)])))
        p=self.first(first)
        with self.assertRaisesRegex(ValueError,'identity changed'):
            p.incoming(udp(DNS,GUEST,53,43001,answer([rr(ttl=199),rr(data=other,ttl=399),rr(data=other,ttl=299)])))

    def test_mixed_or_authority_forms_never_get_permutation_mode(self):
        other=socket.inet_aton('1.1.1.1')
        extra=rr(owner=b'\x04glue\x07example\0',ttl=600,data=PUBLIC)
        for first,second in (
            (answer([rr(),rr(data=other)],extra=extra,ar=1),answer([rr(data=other,ttl=299),rr(ttl=299)],extra=extra,ar=1)),
            (answer([rr(),rr(kind=16,data=b'\x01x')]),answer([rr(kind=16,data=b'\x01x'),rr(ttl=299)]))):
            p=self.first(first);self.assertEqual(p.reply_comparison_mode,'normalized_wire_identity')
            with self.assertRaisesRegex(ValueError,'identity changed'):
                p.incoming(udp(DNS,GUEST,53,43001,second))

    def test_changed_owner_type_class_count_set_or_encoding_refused(self):
        other=socket.inet_aton('1.1.1.1');first=answer([rr(),rr(data=other)])
        variants=[answer([rr(),rr(owner=b'\x03www\x07example\x03com\0',data=other)]),
            answer([rr(),rr(kind=16,data=b'\x01x')]),answer([rr()]),
            answer([rr(),rr(data=socket.inet_aton('8.8.8.8'))]),
            answer([rr(owner=b'\x07example\x03com\0'),rr(data=other)]),
            answer([rr(),rr(data=other)],extra=rr(),ar=1)]
        klass=bytearray(first);klass[49:51]=b'\0\3';variants.append(bytes(klass))
        for raw in variants:
            p=self.first(first)
            with self.subTest(raw=raw.hex()),self.assertRaises(ValueError):
                p.incoming(udp(DNS,GUEST,53,43001,raw))


class HTTPPolicy(unittest.TestCase):
    def test_response_without_complete_request_never_passes(self):
        p=connected()
        response=b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\n\r\nExample Domain"
        p.incoming(tcp(PUBLIC,GUEST,80,43002,20001,10001,0x19,response))
        self.assertIsNotNone(p.http)
        self.assertFalse(p.complete())

    def test_only_exact_request_segmented_and_retransmitted(self):
        p=connected()
        for offset in range(0,len(p.request),17):
            payload=p.request[offset:offset+17]
            p.outgoing(tcp(GUEST,PUBLIC,43002,80,10001+offset,20001,0x18,payload))
        self.assertTrue(all(p.coverage))
        p.outgoing(tcp(GUEST,PUBLIC,43002,80,10001,20001,0x18,p.request[:17]))
        with self.assertRaisesRegex(ValueError,"read-only"):
            p.outgoing(tcp(GUEST,PUBLIC,43002,80,10001,20001,0x18,b"POST /"))
        with self.assertRaisesRegex(ValueError,"single TCP flow"):
            p.outgoing(tcp(GUEST,PUBLIC,43003,80,10001,20001,0x10))

    def test_response_reassembly_fin_and_overlap(self):
        p=connected()
        p.outgoing(tcp(GUEST,PUBLIC,43002,80,10001,20001,0x18,p.request))
        response=b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\n\r\nExample Domain"
        # Out-of-order tail first, same retransmission allowed, FIN closes only
        # once every byte is contiguous. No fabricated prefix bytes.
        p.incoming(tcp(PUBLIC,GUEST,80,43002,20021,10001+len(p.request),0x18,response[20:]))
        self.assertIsNone(p.http)
        p.incoming(tcp(PUBLIC,GUEST,80,43002,20001,10001+len(p.request),0x18,response[:20]))
        self.assertIsNotNone(p.http);self.assertFalse(p.complete())
        p.incoming(tcp(PUBLIC,GUEST,80,43002,20001+len(response),10001+len(p.request),0x11))
        self.assertTrue(p.complete())
        with self.assertRaisesRegex(ValueError,"overlap"):
            p.incoming(tcp(PUBLIC,GUEST,80,43002,20001,10001,0x18,b"X"))

    def test_chunked_content_length_and_partial(self):
        prefix=b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
        good=prefix+b"Transfer-Encoding: chunked\r\n\r\ne\r\nExample Domain\r\n0\r\n\r\n"
        self.assertEqual(http_response(good)["body_bytes"],14)
        for n in range(len(good)):
            self.assertIsNone(http_response(good[:n]))
        for bad in (good+b"x",good.replace(b"e\r\n",b"z\r\n",1),
            good.replace(b"Transfer-Encoding:",b"Content-Length: 14\r\nTransfer-Encoding:"),
            prefix+b"Content-Length: 14\r\nContent-Length: 14\r\n\r\nExample Domain",
            prefix+b"Content-Length: 14\r\n\r\nExample DomainEXTRA",
            prefix+b"Content-Encoding: gzip\r\nContent-Length: 14\r\n\r\nExample Domain",
            good.replace(b"200 OK",b"302 Found"),good.replace(b"Example Domain",b"Something Else")):
            with self.subTest(bad=bad[:70]),self.assertRaises(ValueError):
                http_response(bad,True)

    def test_request_and_response_sequence_checksum(self):
        p=connected()
        with self.assertRaisesRegex(ValueError,"sequence"):
            p.outgoing(tcp(GUEST,PUBLIC,43002,80,9999,20001,0x18,b"GET"))
        with self.assertRaisesRegex(ValueError,"ACK"):
            p.incoming(tcp(PUBLIC,GUEST,80,43002,20001,9999,0x10))
        bad=bytearray(tcp(GUEST,PUBLIC,43002,80,10001,20001,0x18,b"GET"));bad[-1]^=1
        with self.assertRaisesRegex(ValueError,"checksum"):
            p.outgoing(bytes(bad))
        with self.assertRaisesRegex(ValueError,"reset"):
            p.incoming(tcp(PUBLIC,GUEST,80,43002,20001,10001,0x14))


if __name__=="__main__":
    unittest.main()
