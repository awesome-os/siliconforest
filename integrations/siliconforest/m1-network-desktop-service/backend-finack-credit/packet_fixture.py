"""Synthetic unit/client packet encoding only; no resolver or target evidence."""
import struct
from frozen import checksum

def packet(src,dst,proto,body):
 h=struct.pack('!BBHHHBBH4s4s',0x45,0,20+len(body),1,0x4000,64,proto,0,src,dst)
 return h[:10]+struct.pack('!H',checksum(h))+h[12:]+body

def udp(src,dst,sp,dp,payload):
 b=struct.pack('!4H',sp,dp,len(payload)+8,0)+payload
 c=checksum(src+dst+struct.pack('!BBH',0,17,len(b))+b) or 65535
 return packet(src,dst,17,b[:6]+struct.pack('!H',c)+b[8:])

def tcp(src,dst,sp,dp,seq,ack,flags,payload=b''):
 options=b'\x02\x04\x05\xb4'if flags&2 else b''
 b=struct.pack('!HHIIBBHHH',sp,dp,seq&0xffffffff,ack&0xffffffff,((20+len(options))//4)<<4,flags,32768,0,0)+options+payload
 c=checksum(src+dst+struct.pack('!BBH',0,6,len(b))+b)
 return packet(src,dst,6,b[:16]+struct.pack('!H',c)+b[18:])


def frame(nonce,sequence,payload,direction=2):
 from model import crc32c
 header=struct.pack('!8sBBHHHQ24sII',b'DLNET001',2,direction,0,len(payload),56,sequence,bytes.fromhex(nonce),0,0)
 raw=header+payload;return raw[:48]+crc32c(raw).to_bytes(4,'big')+raw[52:]
