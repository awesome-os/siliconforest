"""Historical source replay only, counts/digests; no raw device values or grade."""
from pathlib import Path
import hashlib
import json
import inventory as inv
ROOT=Path(__file__).resolve().parents[4]
PATH=ROOT/'runs/m1-desktop-010/apcie-port-attempt-001/guest/guest-adt.bin'
EXPECTED='17e4be202fa53a20bee179ff8eccaeb7a077fa78d26dbe167517cc2cccdba4c0'
raw=PATH.read_bytes()
assert hashlib.sha256(raw).hexdigest()==EXPECTED
out=inv.extract(raw,profile=inv.PROFILE);meta=out.metadata()
assert (meta['pmap_count'],meta['segment_count'],meta['physical_count'],meta['remap_count'],meta['remap_nonzero_count'])==(109,37,37,37,36)
assert meta['projected_source_records']==184 and meta['zero_remap_retained_count']==1
assert sum(p.key=='segment-ranges' for p in out.properties)==15
assert all(b''.join(s.raw for s in out.segments if s.path==p.path)==p.value for p in out.properties if p.key=='segment-ranges')
assert sum(p.path=='/vram' and p.key=='reg' for p in out.properties)==1
assert sum(p.path=='' and p.key in ('#address-cells','#size-cells') for p in out.properties)==2
meta.update(source=str(PATH.relative_to(ROOT)),source_sha256=EXPECTED,segment_properties=15,inventory_replay_only=True,old_grade_changed=False,actual_native_facts_handoff_available=False)
print(json.dumps(meta,sort_keys=True))
