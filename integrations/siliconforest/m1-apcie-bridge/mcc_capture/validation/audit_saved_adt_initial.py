"""Original bounded audit of ALREADY SAVED ADT; no acquisition/adapter."""
from pathlib import Path
import struct,hashlib,json
ROOT=Path(__file__).resolve().parents[6]
P=ROOT/'runs/m1-desktop-010/apcie-port-attempt-001/guest/original-adt.bin'
raw=P.read_bytes();assert 0<len(raw)<=2*1024*1024
nodes={};count=0
def u32(off):assert off+4<=len(raw);return struct.unpack_from('<I',raw,off)[0]
def node(off,parent,depth):
 global count
 assert depth<=32;count+=1;assert count<=4096
 np,nc=u32(off),u32(off+4);off+=8;assert np<=4096 and nc<=4096
 props={}
 for _ in range(np):
  assert off+36<=len(raw);name=raw[off:off+32].split(b'\0')[0].decode('ascii');size=u32(off+32)&0x7fffffff;off+=36
  assert name not in props and size<=len(raw)-off
  props[name]=raw[off:off+size];off+=(size+3)&~3
 name=props['name'].split(b'\0')[0].decode('ascii');path='' if parent is None else parent+'/'+name
 nodes[path]=(props,parent)
 for _ in range(nc):off=node(off,path,depth+1)
 return off
end=node(0,None,0);assert end==len(raw)
props,parent=nodes['/arm-io/mcc'];compat=props['compatible'].rstrip(b'\0').split(b'\0');assert b'mcc,t8103' in compat
pp,_=nodes[parent]
assert struct.unpack('<I',pp['#address-cells'])[0]==2 and struct.unpack('<I',pp['#size-cells'])[0]==2
reg=props['reg'];assert len(reg)%16==0 and len(reg)>=48
base,extent=struct.unpack_from('<QQ',reg,16)
translated=base;anc=parent;translations=0
while anc is not None:
 ap,par=nodes[anc]
 if 'ranges' not in ap:break
 assert par is not None
 rootp,_=nodes[par]
 assert struct.unpack('<I',ap['#address-cells'])[0]==2
 assert struct.unpack('<I',ap['#size-cells'])[0]==2
 assert struct.unpack('<I',rootp['#address-cells'])[0]==2
 rr=ap['ranges'];assert len(rr)%24==0
 hits=[]
 for off in range(0,len(rr),24):
  child,host,size=struct.unpack_from('<QQQ',rr,off)
  if child<=translated and translated+extent<=child+size:hits.append(host+translated-child)
 assert len(hits)==1
 translated=hits[0];translations+=1;anc=par
assert translated and translated%4==0 and extent%4==0
assert extent>=0xc06dc and translated+extent<=1<<36
result={'input_path':str(P.relative_to(ROOT)),'input_sha256':hashlib.sha256(raw).hexdigest(),'input_bytes':len(raw),'node_path':'/arm-io/mcc','required_compatible_present':True,'reg_entries':len(reg)//16,'selected_reg_index':1,'reg_property_sha256':hashlib.sha256(reg).hexdigest(),'selected_region_extent':extent,'whole_region_fits_each_parent_translation':True,'translation_count':translations,'translated_base_nonzero_4aligned_PA36':True,'whole_region_PA36':True,'complete_plan_fits':True,'source_only_saved_metadata_not_mapping_or_read_safety':True,'raw_base_or_unique_identifiers_copied':False}
print(json.dumps(result,indent=2))
