"""Native resolver DNS-correlation and fixed example.com HTTP egress policy.

No sockets/configuration or system resolver calls. Packet decisions use bytes
observed on the opted-in slirp channel, never a host getaddrinfo substitute.
Every exception is terminal at the backend; direct callers must discard state.
"""
import ipaddress
import hashlib
import json
import struct
import time

GUEST = bytes([10, 0, 2, 15])
DNS = bytes([10, 0, 2, 3])
QUESTION = b"\x07example\x03com\0\0\1\0\1"
MAX_HTTP = 65536


def checksum(data):
    if len(data) & 1:
        data += b"\0"
    total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


def parse_packet(packet):
    if not 20 <= len(packet) <= 1500 or packet[0] != 0x45:
        raise ValueError("IPv4 fixed header/length")
    if struct.unpack_from("!H", packet, 2)[0] != len(packet) or checksum(packet[:20]):
        raise ValueError("IPv4 total/checksum")
    if struct.unpack_from("!H", packet, 6)[0] & 0xbfff:
        raise ValueError("IPv4 fragments/reserved forbidden")
    proto, src, dst, body = packet[9], packet[12:16], packet[16:20], packet[20:]
    if proto not in (6, 17) or len(body) < (20 if proto == 6 else 8):
        raise ValueError("TCP/UDP packet shape")
    sport, dport = struct.unpack_from("!HH", body)
    if not sport or not dport:
        raise ValueError("zero transport port")
    if proto == 17:
        if struct.unpack_from("!H", body, 4)[0] != len(body):
            raise ValueError("UDP length")
        payload = body[8:]
    else:
        hlen = (body[12] >> 4) * 4
        if not 20 <= hlen <= 60 or hlen > len(body) or body[12] & 15:
            raise ValueError("TCP header/reserved")
        payload = body[hlen:]
    pseudo = src + dst + struct.pack("!BBH", 0, proto, len(body))
    if (proto == 6 or body[6:8] != b"\0\0") and checksum(pseudo + body):
        raise ValueError("transport checksum")
    return proto, src, dst, sport, dport, body, payload


def dns_name(data, offset):
    labels, used, visited, cursor = [], None, set(), offset
    for _ in range(64):
        if cursor in visited or cursor >= len(data):
            raise ValueError("DNS name loop/bounds")
        visited.add(cursor)
        length = data[cursor]
        if length & 0xc0 == 0xc0:
            if cursor + 1 >= len(data):
                raise ValueError("DNS short pointer")
            target = ((length & 63) << 8) | data[cursor + 1]
            if target < 12 or target >= cursor:
                raise ValueError("DNS pointer direction/bounds")
            if used is None:
                used = cursor + 2
            cursor = target
            continue
        if length & 0xc0 or length > 63 or cursor + 1 + length > len(data):
            raise ValueError("DNS label shape")
        cursor += 1
        if not length:
            name = b".".join(labels).lower()
            if len(name) > 253:
                raise ValueError("DNS long name")
            return name, cursor if used is None else used
        label = data[cursor:cursor+length]
        if any(c < 33 or c > 126 or c == 46 for c in label):
            raise ValueError("DNS label characters")
        labels.append(label); cursor += length
    raise ValueError("DNS name step budget")


def global_address(raw):
    ip = ipaddress.IPv4Address(raw)
    return (ip.is_global and not ip.is_multicast and not ip.is_reserved and
            not ip.is_loopback and not ip.is_link_local and not ip.is_unspecified)


def dns_answer(data, request):
    if not 12 <= len(data) <= 512:
        raise ValueError("DNS response size")
    ident, flags, qd, an, ns, ar = struct.unpack_from("!6H", data)
    if ident != struct.unpack_from("!H", request)[0] or not flags & 0x8000:
        raise ValueError("DNS transaction/QR")
    if flags & 0x7a4f or not flags & 0x0100:  # opcode, TC, reserved Z, rcode
        raise ValueError("DNS opcode/truncation/rcode/RD")
    if qd != 1 or not 1 <= an <= 32 or ns > 16 or ar > 16 or an + ns + ar > 32:
        raise ValueError("DNS record count")
    name, end = dns_name(data, 12)
    if name != b"example.com" or data[12:end+4] != request[12:] or data[end:end+4] != b"\0\1\0\1":
        raise ValueError("DNS question correlation")
    offset, answers = end + 4, []
    for index in range(an + ns + ar):
        owner, offset = dns_name(data, offset)
        if offset + 10 > len(data):
            raise ValueError("DNS RR header")
        kind, klass, ttl, n = struct.unpack_from("!HHIH", data, offset)
        offset += 10
        if kind == 41:
            raise ValueError("unsupported DNS OPT/EDNS response")
        if offset + n > len(data):
            raise ValueError("DNS RDATA bounds")
        value = data[offset:offset+n]
        if index < an:
            if klass != 1:
                raise ValueError("DNS answer class")
            if kind == 5:
                value, consumed = dns_name(data, offset)
                if consumed != offset + n:
                    raise ValueError("DNS CNAME exact RDATA")
            elif kind == 1 and n != 4:
                raise ValueError("DNS A length")
            answers.append((owner, kind, ttl, value))
        offset += n
    if offset != len(data):
        raise ValueError("DNS trailing bytes")
    current, seen, cname_ttls = b"example.com", set(), []
    for _ in range(8):
        if current in seen:
            raise ValueError("DNS CNAME cycle")
        seen.add(current)
        cnames = [(ttl, value) for owner, kind, ttl, value in answers if owner == current and kind == 5]
        addresses = [(ttl, value) for owner, kind, ttl, value in answers if owner == current and kind == 1]
        if cnames:
            if addresses or len(set(v for _, v in cnames)) != 1:
                raise ValueError("DNS CNAME ambiguity")
            cname_ttls.extend(ttl for ttl, _ in cnames)
            current = cnames[0][1]
            continue
        if not 1 <= len(addresses) <= 32 or any(not global_address(v) for _, v in addresses):
            raise ValueError("DNS no bounded global A answer")
        ttl = min([t for t, _ in addresses] + cname_ttls + [60])
        if ttl == 0:
            raise ValueError("DNS zero TTL authorization")
        return [v for _, v in addresses], ttl
    raise ValueError("DNS CNAME budget")


def dns_reply_details(data, request):
    """Validated DNS identity with only actual RR TTL words normalized.

    Header, question, compression bytes, RR order/types/classes/lengths/RDATA
    stay byte-exact. Parsed owners and CNAME values also remain identity so
    a compression pointer cannot disguise a semantic change via a TTL word.
    """
    addresses, lease = dns_answer(data, request)
    counts = struct.unpack_from("!3H", data, 6)
    offset = dns_name(data, 12)[1] + 4
    normalized, records = bytearray(data), []
    for section, count in zip(("answer", "authority", "additional"), counts):
        for index in range(count):
            rr_start = offset
            owner, offset = dns_name(data, offset)
            kind, klass, ttl, n = struct.unpack_from("!HHIH", data, offset)
            ttl_offset = offset + 4
            normalized[ttl_offset:ttl_offset+4] = b"\0" * 4
            offset += 10
            value = data[offset:offset+n]
            wire = bytearray(data[rr_start:offset+n])
            relative_ttl = ttl_offset - rr_start
            wire[relative_ttl:relative_ttl+4] = b"\0" * 4
            decoded = None
            if kind == 5:
                decoded, consumed = dns_name(data, offset)
                if consumed != offset+n:
                    raise ValueError("DNS CNAME exact RDATA")
                decoded = decoded.decode("ascii")
            records.append({"section":section,"index":index,"owner":owner.decode("ascii"),
                "type":kind,"class":klass,"ttl_offset":ttl_offset,"ttl":ttl,
                "rdata_hex":value.hex(),"decoded_domain":decoded,"normalized_wire_hex":wire.hex()})
            offset += n
    if offset != len(data):
        raise ValueError("DNS normalized identity bounds")
    return addresses, lease, bytes(normalized), records


def dns_duplicate_comparison(data, normalized, records):
    """Only a pure example.com A answer may permute its exact multiset.

    Per-key cardinality is identity, so repeated addresses cannot mask a
    removed/added record. Each key's sorted TTL vector is compared separately.
    Other DNS shapes retain normalized wire and parsed semantic identity.
    """
    counts = struct.unpack_from("!3H", data, 6)
    pure_a = (counts[1:] == (0, 0) and records and
        all(r["section"] == "answer" and r["owner"] == "example.com" and
            r["type"] == 1 and r["class"] == 1 and len(r["rdata_hex"]) == 8 for r in records))
    order = [(r["owner"],r["type"],r["class"],r["rdata_hex"]) for r in records]
    if pure_a:
        grouped = {}
        grouped_wire = {}
        for key,r in zip(order,records):
            grouped.setdefault(key,[]).append(r["ttl"])
            grouped_wire.setdefault(key,[]).append(r["normalized_wire_hex"])
        keys = sorted(grouped)
        ttl_groups = [sorted(grouped[k]) for k in keys]
        descriptor = {"mode":"a_only_answer_multiset",
            "header_question_hex":data[:dns_name(data,12)[1]+4].hex(),
            "keys_and_counts":[list(k)+[len(grouped[k])] for k in keys],
            "per_key_normalized_wire":[sorted(grouped_wire[k]) for k in keys]}
    else:
        ttl_groups = [[r["ttl"] for r in records]]
        descriptor = {"mode":"normalized_wire_identity","wire_hex":normalized.hex(),
            "semantic_RRs":[{k:v for k,v in r.items() if k != "ttl"} for r in records]}
    identity = json.dumps(descriptor,sort_keys=True,separators=(",",":")).encode("ascii")
    return descriptor["mode"], identity, ttl_groups, order


def http_response(data, eof=False):
    if len(data) > MAX_HTTP:
        raise ValueError("HTTP response cap")
    end = data.find(b"\r\n\r\n")
    if end < 0:
        if len(data) > 8192 or eof:
            raise ValueError("HTTP missing/large headers")
        return None
    if end > 8192:
        raise ValueError("HTTP header cap")
    lines = data[:end].split(b"\r\n")
    status = lines[0].split(b" ", 2)
    if any(c < 32 or c > 126 for c in lines[0]):
        raise ValueError("HTTP status controls")
    if len(status) < 2 or status[0] not in (b"HTTP/1.0", b"HTTP/1.1") or status[1] != b"200":
        raise ValueError("HTTP status must be observed200")
    fields = {}
    for line in lines[1:]:
        if b":" not in line or line[:1] in (b" ", b"\t"):
            raise ValueError("HTTP header shape")
        key, value = line.split(b":", 1)
        key, value = key.lower(), value.strip(b" \t")
        if not key or any(c not in b"abcdefghijklmnopqrstuvwxyz0123456789-" for c in key):
            raise ValueError("HTTP header name")
        if key in fields:
            raise ValueError("HTTP duplicate header")
        if any(c < 32 and c != 9 or c > 126 for c in value):
            raise ValueError("HTTP header value")
        fields[key] = value
    if fields.get(b"content-encoding", b"identity").lower() != b"identity":
        raise ValueError("HTTP encoded content forbidden")
    if fields.get(b"content-type", b"").split(b";",1)[0].strip().lower() != b"text/html":
        raise ValueError("HTTP expected HTML type")
    body = data[end+4:]
    if b"transfer-encoding" in fields:
        if b"content-length" in fields or fields[b"transfer-encoding"].lower() != b"chunked":
            raise ValueError("HTTP ambiguous transfer framing")
        offset, decoded = 0, bytearray()
        for _ in range(1024):
            line_end = body.find(b"\r\n", offset)
            if line_end < 0:
                if eof:
                    raise ValueError("HTTP truncated chunk header")
                return None
            line = body[offset:line_end]
            if not 1 <= len(line) <= 8 or any(c not in b"0123456789abcdefABCDEF" for c in line):
                raise ValueError("HTTP chunk size")
            size = int(line, 16); offset = line_end + 2
            if size > MAX_HTTP or len(decoded) + size > MAX_HTTP:
                raise ValueError("HTTP chunk cap")
            if not size:
                if len(body) < offset + 2:
                    if eof:
                        raise ValueError("HTTP truncated final chunk")
                    return None
                if body[offset:] != b"\r\n":
                    raise ValueError("HTTP trailers/extra bytes forbidden")
                body = bytes(decoded); break
            if len(body) < offset + size + 2:
                if eof:
                    raise ValueError("HTTP truncated chunk data")
                return None
            if body[offset+size:offset+size+2] != b"\r\n":
                raise ValueError("HTTP chunk delimiter")
            decoded.extend(body[offset:offset+size]); offset += size + 2
        else:
            raise ValueError("HTTP chunk count")
    elif b"content-length" in fields:
        value = fields[b"content-length"]
        if not value.isdigit() or len(value) > 6 or int(value) > MAX_HTTP:
            raise ValueError("HTTP content length")
        size = int(value)
        if len(body) < size:
            if eof:
                raise ValueError("HTTP short body")
            return None
        if len(body) != size:
            raise ValueError("HTTP extra body")
    elif not eof:
        return None
    if b"Example Domain" not in body:
        raise ValueError("HTTP observed example content missing")
    return {"status":200,"body_bytes":len(body),"header_bytes":end+4}


class Policy:
    def __init__(self, nonce, clock=time.monotonic):
        if len(nonce) != 48 or any(c not in "0123456789abcdef" for c in nonce):
            raise ValueError("nonce")
        self.clock, self.nonce = clock, nonce
        self.query = self.reply = None
        self.dns_port = self.dns_at = self.expires = self.endpoint = None
        self.lease_seconds = None
        self.dns_attempts, self.dns_duplicates = 0, 0
        self.tcp_port = self.client_isn = self.server_isn = None
        self.initial_tcp_flags = None
        self.request = (b"GET / HTTP/1.1\r\nHost: example.com\r\n"
            b"User-Agent: darwin-lab-egress/1\r\nAccept-Encoding: identity\r\n"
            b"X-Darwin-Lab-Nonce: " + nonce.encode() + b"\r\nConnection: close\r\n\r\n")
        self.coverage = bytearray(len(self.request))
        self.observed_request = bytearray(len(self.request))
        self.response = bytearray(MAX_HTTP)
        self.present = bytearray(MAX_HTTP)
        self.contiguous = self.highest = 0
        self.fin_offset = None
        self.http = None
        self.allowed_addresses = []
        self.started = self.clock()
        self.query_events, self.reply_events, self.answer_records = [], [], []
        self.selected_answer_index = None
        self.reply_identity = self.reply_rr_identity = self.previous_reply_ttls = None
        self.first_reply_rrs = None
        self.reply_comparison_mode = self.reply_comparison_identity = self.previous_comparison_ttls = None
        self.first_reply_order = self.previous_reply_order = None
        self.first_accepted_elapsed_seconds = self.original_lease_expires_elapsed_seconds = None

    def outgoing(self, packet):
        proto, src, dst, sp, dp, body, payload = parse_packet(packet)
        if src != GUEST:
            raise ValueError("guest source")
        if proto == 17:
            if dst != DNS or dp != 53 or sp < 1024 or len(payload) != 12 + len(QUESTION):
                raise ValueError("fixed DNS endpoint/query size")
            ident, flags, qd, an, ns, ar = struct.unpack_from("!6H", payload)
            if ident == 0 or (flags, qd, an, ns, ar) != (0x0100, 1, 0, 0, 0) or payload[12:] != QUESTION:
                raise ValueError("fixed DNS A question")
            if self.query is not None and (self.query != payload or self.dns_port != sp):
                raise ValueError("DNS session/query changed")
            self.dns_attempts += 1
            if self.dns_attempts > 3:
                raise ValueError("DNS retry budget")
            if self.query is None:
                self.query, self.dns_port, self.dns_at = payload, sp, self.clock()
            self.query_events.append({"attempt":self.dns_attempts,
                "elapsed_seconds":self.clock()-self.started, "source_port":sp,
                "transaction_id":ident,"query_hex":payload.hex(),
                "retry":self.dns_attempts>1,"after_validated_reply":self.reply is not None})
            return
        if not self.allowed_addresses or self.clock() >= self.expires:
            raise ValueError("TCP before DNS or expired lease")
        if self.endpoint is None:
            if dst not in self.allowed_addresses:
                raise ValueError("TCP destination outside validated A set")
            # libc may RFC3484-sort the wire answer order. Select only the
            # first observed SYN, never authorize another endpoint/fallback.
            if dp != 80 or sp < 1024 or body[13] not in (0x02, 0xc2) or payload:
                raise ValueError("first TCP packet must be SYN")
            self.endpoint = dst
            self.selected_answer_index = self.allowed_addresses.index(dst)
        if dst != self.endpoint or dp != 80 or sp < 1024:
            raise ValueError("answer-bound single TCP endpoint")
        seq = struct.unpack_from("!I", body, 4)[0]
        flags = body[13]
        if self.tcp_port is None:
            if not flags & 2 or flags & (1 | 4 | 16) or payload:
                raise ValueError("first TCP packet must be SYN")
            self.tcp_port, self.client_isn = sp, seq
            self.initial_tcp_flags = flags
        if sp != self.tcp_port:
            raise ValueError("single TCP flow")
        if flags & 2:
            if seq != self.client_isn or flags & (1 | 4 | 16) or payload:
                raise ValueError("TCP SYN changed")
            return
        start = (seq - self.client_isn - 1) & 0xffffffff
        if start > len(self.request) + 1 or (payload and start + len(payload) > len(self.request)):
            raise ValueError("TCP request sequence bound")
        if payload:
            if self.server_isn is None or payload != self.request[start:start+len(payload)]:
                raise ValueError("only exact read-only example HTTP request")
            self.coverage[start:start+len(payload)] = b"\1" * len(payload)
            self.observed_request[start:start+len(payload)] = payload

    def incoming(self, packet):
        proto, src, dst, sp, dp, body, payload = parse_packet(packet)
        if dst != GUEST:
            raise ValueError("reply destination")
        if proto == 17:
            if src != DNS or sp != 53 or self.query is None or dp != self.dns_port:
                raise ValueError("DNS reply endpoint correlation")
            observed_at = self.clock()
            elapsed = observed_at-self.started
            if observed_at - self.dns_at > 10:
                raise ValueError("DNS reply deadline")
            addresses, ttl, identity, records = dns_reply_details(payload, self.query)
            ttls = [r["ttl"] for r in records]
            rr_identity = [{k:v for k,v in r.items() if k != "ttl"} for r in records]
            mode, comparison_identity, comparison_ttls, order = dns_duplicate_comparison(payload,identity,records)
            event = {"elapsed_seconds":elapsed,
                "source_port":sp,"destination_port":dp,
                "transaction_id":int.from_bytes(payload[:2],"big"),
                "reply_hex":payload.hex(),"duplicate":self.reply is not None,
                "rr_ttls":ttls,"rrs":records,"accepted":False,
                "normalized_dns_sha256":hashlib.sha256(identity).hexdigest(),
                "comparison_mode":mode,"comparison_identity_sha256":hashlib.sha256(comparison_identity).hexdigest(),
                "comparison_sorted_ttl_groups":comparison_ttls,
                "answer_order_changed_from_first":order != self.first_reply_order if self.first_reply_order is not None else False,
                "answer_order_changed_from_previous":order != self.previous_reply_order if self.previous_reply_order is not None else False,
                "first_accepted_elapsed_seconds":self.first_accepted_elapsed_seconds,
                "original_lease_expires_elapsed_seconds":self.original_lease_expires_elapsed_seconds,
                "original_lease_remaining_seconds":max(0,self.original_lease_expires_elapsed_seconds-elapsed) if self.expires is not None else None}
            self.reply_events.append(event)
            if self.reply is not None:
                if observed_at >= self.expires:
                    raise ValueError("DNS duplicate outside original lease")
                if mode != self.reply_comparison_mode or comparison_identity != self.reply_comparison_identity:
                    raise ValueError("DNS duplicate non-TTL identity changed")
                if (len(comparison_ttls) != len(self.previous_comparison_ttls) or
                        any(len(a) != len(b) or any(x > y for x,y in zip(a,b))
                            for a,b in zip(comparison_ttls,self.previous_comparison_ttls))):
                    raise ValueError("DNS duplicate TTL increased")
                if self.dns_duplicates >= 2:
                    raise ValueError("DNS reply duplicate budget")
                self.dns_duplicates += 1
                self.previous_reply_ttls = ttls
                self.previous_comparison_ttls, self.previous_reply_order = comparison_ttls, order
                event["accepted"] = True
                return
            self.reply = payload
            self.reply_identity, self.reply_rr_identity = identity, rr_identity
            self.previous_reply_ttls, self.first_reply_rrs = ttls, records
            self.reply_comparison_mode, self.reply_comparison_identity = mode, comparison_identity
            self.previous_comparison_ttls = comparison_ttls
            self.first_reply_order = self.previous_reply_order = order
            self.allowed_addresses = addresses
            offset = dns_name(payload,12)[1] + 4
            an = struct.unpack_from("!H",payload,6)[0]
            for _ in range(an):
                owner, offset = dns_name(payload,offset)
                kind, klass, raw_ttl, n = struct.unpack_from("!HHIH",payload,offset)
                offset += 10
                raw = payload[offset:offset+n]
                value = (str(ipaddress.IPv4Address(raw)) if kind == 1 else
                    dns_name(payload,offset)[0].decode("ascii") if kind == 5 else raw.hex())
                self.answer_records.append({"owner":owner.decode("ascii"),"type":kind,
                    "class":klass,"ttl":raw_ttl,"rdata_hex":raw.hex(),"value":value})
                offset += n
            self.expires = observed_at + ttl
            self.lease_seconds = ttl
            self.first_accepted_elapsed_seconds = elapsed
            self.original_lease_expires_elapsed_seconds = elapsed + ttl
            event["accepted"] = True
            event["original_lease_remaining_seconds"] = ttl
            event["first_accepted_elapsed_seconds"] = elapsed
            event["original_lease_expires_elapsed_seconds"] = elapsed+ttl
            return
        if (self.endpoint is None or src != self.endpoint or sp != 80 or
                self.tcp_port is None or dp != self.tcp_port):
            raise ValueError("TCP reply single endpoint correlation")
        seq, ack = struct.unpack_from("!II", body, 4)
        flags = body[13]
        if flags & 4:
            raise ValueError("external TCP reset")
        if flags & 2:
            if not flags & 16 or ack != (self.client_isn + 1) & 0xffffffff or payload:
                raise ValueError("TCP SYNACK correlation")
            if self.server_isn is not None and self.server_isn != seq:
                raise ValueError("TCP SYNACK changed")
            self.server_isn = seq
            return
        if self.server_isn is None:
            raise ValueError("TCP response before SYNACK")
        if not flags & 16 or ((ack - self.client_isn - 1) & 0xffffffff) > len(self.request) + 1:
            raise ValueError("TCP response ACK bound")
        start = (seq - self.server_isn - 1) & 0xffffffff
        if start + len(payload) > MAX_HTTP:
            raise ValueError("TCP response sequence/size cap")
        for i, byte in enumerate(payload, start):
            if self.present[i] and self.response[i] != byte:
                raise ValueError("TCP inconsistent overlap")
            self.response[i], self.present[i] = byte, 1
        if payload:
            self.highest = max(self.highest, start + len(payload))
        if flags & 1:
            value = start + len(payload)
            if self.fin_offset is not None and self.fin_offset != value:
                raise ValueError("TCP FIN changed")
            self.fin_offset = value
        while self.contiguous < MAX_HTTP and self.present[self.contiguous]:
            self.contiguous += 1
        data = bytes(self.response[:self.contiguous])
        if data:
            self.http = http_response(data, self.fin_offset == self.contiguous)

    def complete(self):
        return (self.reply is not None and all(self.coverage) and self.http is not None and
                self.highest == self.contiguous and self.fin_offset == self.contiguous)

    def summary(self):
        prefix = 0
        while prefix < len(self.coverage) and self.coverage[prefix]:
            prefix += 1
        return {"domain":"example.com","dns_attempts":self.dns_attempts,
            "dns_source_port":self.dns_port,
            "dns_first_query_elapsed_seconds":self.dns_at-self.started if self.dns_at is not None else None,
            "dns_transaction_id":int.from_bytes(self.query[:2],"big") if self.query else None,
            "dns_query_hex":self.query.hex() if self.query else None,
            "dns_reply_hex":self.reply.hex() if self.reply else None,
            "answer_ipv4":[str(ipaddress.IPv4Address(x)) for x in self.allowed_addresses],
            "selected_ipv4":str(ipaddress.IPv4Address(self.endpoint)) if self.endpoint else None,
            "selected_answer_index":self.selected_answer_index,"lease_seconds":self.lease_seconds,
            "dns_id_source":"observed_wire_not_nonce_derived",
            "dns_query_events":self.query_events,"dns_reply_events":self.reply_events,
            "dns_answer_records":self.answer_records,"dns_duplicates":self.dns_duplicates,
            "dns_duplicate_policy":"pure_example_A_multiset_else_wire_identity_nonincreasing_key_TTLs_original_lease",
            "dns_comparison_mode":self.reply_comparison_mode,
            "dns_first_accepted_elapsed_seconds":self.first_accepted_elapsed_seconds,
            "dns_original_lease_expires_elapsed_seconds":self.original_lease_expires_elapsed_seconds,
            "dns_first_reply_rrs":self.first_reply_rrs,"dns_last_accepted_rr_ttls":self.previous_reply_ttls,
            "dns_policy_revision":2,"backend_policy_revision":3,
            "permitted_initial_tcp_flags":[0x02,0xc2],"initial_tcp_flags":self.initial_tcp_flags,
            "dns_query_profile":"RD0100_one_example.com_A_IN_no_OPT",
            "unsupported_traffic_action":"terminal_failure_no_forward",
            "permitted_tcp_port":80,"guest_tcp_port":self.tcp_port,
            "request_complete":all(self.coverage),"request_expected_hex":self.request.hex(),
            "request_observed_contiguous_bytes":prefix,"request_observed_covered_bytes":sum(self.coverage),
            "response_contiguous_bytes":self.contiguous,"response_highest_bytes":self.highest,
            "response_fin_offset":self.fin_offset,"http":self.http,
            "complete":self.complete(),"tls_or_dnssec_identity_claimed":False}
