#!/usr/bin/env python3
"""Synthetic byte-level policy negatives; no sockets or external requests."""
from pathlib import Path
import socket
import struct
import sys
import unittest
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from policy import Policy, GUEST, DNS, QUESTION, dns_answer, dns_name, http_response
from test_public import udp, tcp

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
        self.assertEqual(p.endpoint, PUBLIC)
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


class HTTPPolicy(unittest.TestCase):
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
