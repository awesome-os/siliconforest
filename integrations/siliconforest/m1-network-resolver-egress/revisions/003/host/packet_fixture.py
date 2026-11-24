"""Original synthetic IP packet encoder, never a Darwin runtime claim."""
import struct
from policy import checksum

def packet(src, dst, proto, body):
    header = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20+len(body), 1, 0x4000,
                         64, proto, 0, src, dst)
    return header[:10] + struct.pack("!H", checksum(header)) + header[12:] + body


def udp(src, dst, sport, dport, payload):
    body = struct.pack("!4H", sport, dport, len(payload)+8, 0) + payload
    pseudo = src + dst + struct.pack("!BBH", 0, 17, len(body))
    value = checksum(pseudo+body) or 65535
    body = body[:6] + struct.pack("!H", value) + body[8:]
    return packet(src, dst, 17, body)


def tcp(src, dst, sport, dport, seq, ack, flags, payload=b""):
    options=b'\x02\x04\x01\x00' if flags&2 else b''
    body = struct.pack("!HHIIBBHHH", sport, dport, seq, ack, ((20+len(options))//4)<<4,
                       flags, 32768, 0, 0) + options + payload
    pseudo = src + dst + struct.pack("!BBH", 0, 6, len(body))
    body = body[:16] + struct.pack("!H", checksum(pseudo+body)) + body[18:]
    return packet(src, dst, 6, body)


