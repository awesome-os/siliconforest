"""Read a saved T1 diagnostic receipt; no target access or descriptor interpretation."""
import hashlib
import re
import struct

def need(ok, message):
    if not ok:
        raise ValueError(message)

def observation(b):
    need(len(b)==556, "Diagnostic length")
    version,_,stage,mask,m,rx,stored=struct.unpack_from("<7I",b)
    result=struct.unpack_from("<i",b,4)[0]
    header,desc=b[28:44],b[44:]
    need(version==1 and stage<=5 and mask<=31 and m<=2038 and rx<=2038 and stored<=512,
         "Version/count/bounds")
    need(not any(desc[stored:]), "Unwritten descriptor tail")
    if m<16:need(not any(header[m:]), "Unwritten header tail")
    valid=m>=16 and header[1]==2 and 1<=struct.unpack_from("<H",header,6)[0]<=2038
    if stage==0:
        need(result==1 and mask in {0,1,3,7} and m==rx==stored==0 and not any(header), "Not-attempted state")
    else:
        need(mask==(15 if stage<=2 else 31), "Request-key state")
        if stage==1:
            need(result==-1 and m==rx==stored==0 and not any(header), "Info-request failure")
        elif stage==2:
            need(result==-2 and not valid and rx==stored==0, "Invalid-info failure")
        elif stage==3:
            need(result==-3 and valid and rx==stored==0, "Descriptor-request failure")
        elif stage==4:
            need(result==-4 and valid and stored==0 and not 1<=rx<=512, "Invalid-descriptor failure")
        else:
            need(result==0 and valid and stored>=1 and rx==stored, "Complete state")
    return dict(result=result,stage=stage,keys=mask,metadata_payload=m,descriptor_payload=rx,
                descriptor_bytes=stored,input_proven=False,feature_writes=0)

def decode(console, nonce):
    need(isinstance(console,bytes) and len(console)<=32<<20,"Console bound")
    need(isinstance(nonce,str) and re.fullmatch("[0-9a-f]{48}",nonce),"Nonce")
    lines=[x.decode("ascii") for x in console.replace(bytes([13]),b"").splitlines()
           if x.startswith((b"DL14_TP_RECORD_",b"DL14_TP_COPY ",b"DL14_TP_VERDICT "))]
    need(len(lines)<=30,"Receipt line bound")
    records={};active=None;chunks=[];verdict=None
    for line in lines:
        a=line.split(" ")
        need(verdict is None and len(a)>=3 and a[1]==nonce,"Order/nonce")
        if a[0]=="DL14_TP_RECORD_BEGIN":
            key="provider" if not records else "instance"
            need(active is None and len(a)==5 and a[2]==key and key not in records and
                 a[3:]==["bytes=556","chunk_bytes=64"],"Record begin")
            active=key;chunks=[]
        elif a[0]=="DL14_TP_RECORD_HEX":
            need(active==a[2] and len(a)==5 and len(chunks)<9 and a[3]=="index="+str(len(chunks)),
                 "Chunk index")
            n=44 if len(chunks)==8 else 64
            need(re.fullmatch("[0-9a-f]{"+str(2*n)+"}",a[4]),"Chunk bytes")
            chunks.append(bytes.fromhex(a[4]))
        elif a[0]=="DL14_TP_RECORD_END":
            need(active==a[2] and len(a)==6 and len(chunks)==9 and
                 a[3:5]==["bytes=556","chunks=9"],"Record end")
            raw=b"".join(chunks)
            need(a[5]=="sha256="+hashlib.sha256(raw).hexdigest(),"Digest")
            records[active]=raw;active=None
        elif a[0]=="DL14_TP_VERDICT":
            need(active is None and "provider" in records,"Missing provider")
            verdict={}
            for field in a[2:]:
                k,sep,v=field.partition("=")
                need(sep and k not in verdict,"Verdict fields")
                verdict[k]=v
        else:
            raise ValueError("Copy error or unknown receipt")
    need(active is None and "provider" in records and verdict is not None,"Incomplete receipt")
    s=observation(records["provider"])
    wanted={k:str(v) for k,v in s.items() if k not in {"input_proven","feature_writes"}}
    wanted.update(verdict="ENUMERATION_CAPTURED" if s["result"]==0 else "REFUSED",
                  input_proven="false",feature_writes="0",
                  instance="equal" if "instance"in records else "absent")
    need(verdict==wanted,"Verdict/raw mismatch")
    if "instance"in records:need(records["instance"]==records["provider"],"Instance differs")
    return dict(summary=s,records=records,verdict=wanted["verdict"],instance=wanted["instance"])
