#!/usr/bin/env python3
"""Original independent DL32 byte/span decoder; never a WiFi verdict."""
import hashlib,re,struct
SIZE=768
PROPERTY='PDBCMPrivateObservation'
COMPONENTS=bytes.fromhex('e892e8e31f0ba8626d4e8a83188b6fe9462163394b1685e5b1094486fcaea93c'
 '7d41f394d77e20bd5776964c5a46194b120a3df705ccb996889cb6d852a09c72'
 '9844eb28efb1190097369b419b90aeeebb605f4aafa147dbd908a7841b3d2c57'
 '0ee2101c9cda5cf282c3db48b7640cef98146c3b04724e3fe59138aaad772434'
 'c585fd8b80479975a46ad19c83b7610a53f3a4a1a420241cbe0022a2d1fb82fb')
PRE=[3,0,0,1,0,0,3,3,0,0,0,0,0,0]
POST=[6,0,0,1,1,0,0,3,3,3,0,0,0,0]
def classify(raw,nonce):
    if not isinstance(raw,bytes) or len(raw)!=SIZE or not re.fullmatch('[0-9a-f]{48}',nonce):raise ValueError('shape/nonce')
    w=lambda off:struct.unpack_from('<I',raw,off)[0]
    if raw[:8]!=b'BCMPRV01' or w(8)!=1 or w(12)!=SIZE or list(struct.unpack_from('<4I',raw,32))!=[1,1,0,0] or raw[264:424]!=COMPONENTS or w(484)!=1 or any(raw[488:496])+any(raw[608:]):raise ValueError('header/context/reserved')
    statuses=list(struct.unpack_from('<8I',raw,424));pre=list(struct.unpack_from('<14I',raw,496));post=list(struct.unpack_from('<14I',raw,552))
    result,stage,attempts,error=struct.unpack_from('<4I',raw,16)
    summary={'classification':'FAILED_OR_INVALID','record_nonce_bound':raw[48:72]==bytes.fromhex(nonce) and result!=1,
      'result':result,'stage':stage,'attempts':attempts,'error':struct.unpack_from('<i',raw,28)[0],
      'operation_statuses':list(struct.unpack_from('<8i',raw,424)),'pre_snapshot':pre,'post_snapshot':post,
      'data_readback':False,'effect_commands':False,'trusted_host_labels':True,'raw_sha256':hashlib.sha256(raw).hexdigest()}
    if result==1:
        if stage or attempts or error or any(raw[48:264]) or statuses!=[0x80000000]*8 or any(raw[456:484]) or any(pre+post):raise ValueError('gated work/identity')
        summary.update(classification='GATED_NO_ATTEMPT',record_nonce_bound=False);return summary
    if result not in (0,2,3) or stage not in range(1,11) or attempts>255:raise ValueError('result/phase')
    for i,s in enumerate(statuses):
        if bool(attempts&(1<<i))==(s==0x80000000):raise ValueError('status/attempt mismatch')
    if attempts&0xfe and not attempts&1:raise ValueError('missing label attempt')
    if attempts&0xfc and (not attempts&2 or statuses[1]):raise ValueError('acquire before config')
    if attempts&0xf8 and (not attempts&4 or statuses[2]):raise ValueError('private ops before acquire')
    if attempts&16 and (not attempts&8 or statuses[3]):raise ValueError('copy before zero')
    if attempts&1 and statuses[0]==0:
        if not any(raw[48:72]) or any(not any(raw[72+i*32:104+i*32]) for i in range(6)):raise ValueError('empty labels')
    if result in (2,3):
        if result==3 and not error:raise ValueError('failed without sticky error')
        if result==2 and error:raise ValueError('pending with error')
        return summary
    if stage!=10 or attempts!=255 or error or any(statuses) or not summary['record_nonce_bound'] or not any(bytes.fromhex(nonce)) or w(480)!=16:raise ValueError('success lifecycle/nonce')
    version,profile,ram,reserved,pages,queries=struct.unpack_from('<6I',raw,456)
    if (version,profile)!=(1,1) or not 1<=ram<=8 or not 1<=reserved<=8 or not 1<=pages<=8 or queries!=2*pages or pre!=PRE or post!=POST:raise ValueError('success counts/snapshots')
    summary['classification']='CONFIG_AND_PRIVATE_LIFECYCLE_SELF_REPORT';return summary
def decode_span(span,nonce):
    if not isinstance(span,bytes) or len(span)>65536 or not re.fullmatch('[0-9a-f]{48}',nonce) or not span.endswith(b'\n'):raise ValueError('bounded exact span')
    # Normalizes trailing console CR only. Interior/foreign lines stay errors.
    lines=[s.rstrip(b'\r').decode('ascii') for s in span[:-1].split(b'\n')]
    base=rf'{nonce} {PROPERTY}'
    m=re.fullmatch(rf'DL32_PRIVATE_RECORD_BEGIN {base} bytes=(0|[1-9][0-9]*) chunk_bytes=64',lines[0])
    if not m:raise ValueError('exact BEGIN required')
    n=int(m[1]);count=(n+63)//64
    if n>4096 or len(lines)!=count+3:raise ValueError('bytes/line count')
    chunks=[]
    for i in range(count):
        m=re.fullmatch(rf'DL32_PRIVATE_RECORD_HEX {base} index={i} ([0-9a-f]+)',lines[i+1])
        if not m:raise ValueError('exact chunk order/content')
        b=bytes.fromhex(m[1]);wanted=min(64,n-64*i)
        if len(b)!=wanted:raise ValueError('chunk length')
        chunks.append(b)
    raw=b''.join(chunks)
    if lines[-2]!=f'DL32_PRIVATE_RECORD_END {nonce} {PROPERTY} bytes={n} chunks={count} sha256={hashlib.sha256(raw).hexdigest()}':raise ValueError('END/digest')
    m=re.fullmatch(rf'DL32_PRIVATE_VERDICT {nonce} verdict=(CONFIG_AND_PRIVATE_LIFECYCLE_SELF_REPORT|GATED_NO_ATTEMPT|FAILED_OR_INVALID|CLEANUP_ERROR) result=([0-9]+) stage=([0-9]+) attempts=([0-9]+) error=(-?[0-9]+) record_nonce_bound=(true|false) data_readback=false effect_commands=false trusted_host_labels=true',lines[-1])
    if not m:raise ValueError('exact verdict')
    try:summary=classify(raw,nonce)
    except ValueError:
        if m[1] not in ('FAILED_OR_INVALID','CLEANUP_ERROR'):raise
        return {'classification':m[1],'raw_sha256':hashlib.sha256(raw).hexdigest(),'raw_record_valid':False,'data_readback':False,'effect_commands':False}
    if m[1]=='CLEANUP_ERROR':summary['classification']='CLEANUP_ERROR'
    if m[1]!=summary['classification'] or [int(m[i]) for i in (2,3,4,5)]!=[summary[k] for k in ('result','stage','attempts','error')] or (m[6]=='true')!=summary['record_nonce_bound']:raise ValueError('record/verdict mismatch')
    return summary
