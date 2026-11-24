#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent expected digests for the production pinned C implementations."""
import hashlib,hmac,random,struct,subprocess,sys
r=random.Random(0x8103)
boundaries=[0,1,2,7,15,16,31,32,55,56,63,64,65,127,128,129,255,256,257,1023,1024,4096]
payload=bytearray()
for i in range(1000):
 nk=boundaries[i%len(boundaries)] if i<484 else r.randrange(2049)
 nd=boundaries[i//len(boundaries)] if i<484 else r.randrange(16385)
 k=r.randbytes(nk);d=r.randbytes(nd)
 payload+=struct.pack('=II',nk,nd)+k+d+hashlib.sha256(d).digest()+hashlib.sha224(d).digest()+hmac.digest(k,d,'sha256')+hmac.digest(k,d,'sha224')
subprocess.run([sys.argv[1]],input=payload,check=True)
