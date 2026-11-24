"""Independent multi-transaction DNS/lease evidence, no network or producer model.

Uses the exact original independent DNS2 wire parser, but derives this profile's
raw-chain TTL and remaining-whole lease without its predecessor's60-second cap.
Backend elapsed clocks are recorded source facts, not independent wire clocks.
"""
from pathlib import Path
import hashlib,json,math,types,struct
SOURCE=Path(__file__).resolve().parents[2]/'m1-network-resolver-service/evidence/dns_retry_evidence.py'
SOURCE_SHA='9d00287524373b991ed92f1de889c53c1fc5192aa1ea33033a4b60e833250e92'
raw=SOURCE.read_bytes()
if hashlib.sha256(raw).hexdigest()!=SOURCE_SHA:raise RuntimeError('Independent DNS2 parser source changed')
wire=types.ModuleType('frozen_independent_DNS2');exec(compile(raw,str(SOURCE),'exec'),wire.__dict__)
CHECKS=('packets','query_identity_port_budget','query_events','reply_identity_and_events',
 'duplicate_identity_and_TTLs','raw_chain_leases','no_duplicate_lease_extension','whole_clock_bounds',
 'first_answer_orders','flow_lease_authority','scope_and_revision')
def require(b,s):
 if not b:raise ValueError(s)
def same(a,b):return wire._same_json(a,b)
def number(n):return wire._number(n)
def raw_ttl(parsed):
 records=parsed['records'];node='example.com';seen=set();values=[]
 for _ in range(8):
  require(node not in seen,'CNAME cycle');seen.add(node)
  cn=[x for x in records if x['section']=='answer'and x['owner']==node and x['type']==5]
  aa=[x for x in records if x['section']=='answer'and x['owner']==node and x['type']==1]
  if cn:values.extend(x['ttl']for x in cn);node=cn[0]['decoded_domain'];continue
  require(bool(aa),'No terminal A');values.extend(x['ttl']for x in aa);require(min(values)>0,'zero TTL');return min(values)
 raise ValueError('CNAME budget')
def dns_evidence(queries,replies,egress):
 checks=dict.fromkeys(CHECKS,False);active=CHECKS[0];details={'errors':[],'transactions':[],'scope':'Closed raw IPv4 UDP bytes plus recorded source-bound Linux clocks; no hardware grade'}
 try:
  require(type(queries)is list and type(replies)is list and 1<=len(queries)<=4 and 1<=len(replies)<=12,'packet counts')
  qp=[wire._packet(x)for x in queries];rp=[wire._packet(x,True)for x in replies];require(type(egress)is dict,'egress object');checks[active]=True
  active='query_identity_port_budget';port=qp[0]['source_port'];transactions={}
  for p in qp:
   ident=wire._query(p['payload']);require(p['source_port']==port,'one owned mdnsd port')
   if ident not in transactions:transactions[ident]={'query':p['payload'],'queries':[],'replies':[]}
   require(p['payload']==transactions[ident]['query'],'same-ID query bytes')
   transactions[ident]['queries'].append(p)
  require(same(egress.get('query_count'),len(qp))and same(egress.get('DNS_source_port'),port),'query counts/port facts');checks[active]=True
  active='query_events';qe=egress.get('query_events');require(type(qe)is list and len(qe)==len(qp),'query events count');qt=[]
  for p,e in zip(qp,qe):
   ident=wire._query(p['payload']);require(type(e)is dict,'query event object');t=number(e['elapsed_seconds']);qt.append(t)
   require(same(e,dict(elapsed_seconds=t,transaction_id=ident,source_port=port,query_hex=p['payload'].hex())),'query event wire facts')
   transactions[ident].setdefault('query_times',[]).append(t)
  require(qt==sorted(qt),'query clocks ordered');checks[active]=True
  active='reply_identity_and_events';re=egress.get('reply_events');require(type(re)is list and len(re)==len(rp),'reply events count');rt=[]
  for p,e in zip(rp,re):
   require(p['destination_port']==port,'replyport');require(len(p['payload'])>=2,'reply ID');ident=int.from_bytes(p['payload'][:2],'big');require(ident in transactions,'observed query ID')
   tx=transactions[ident];parsed=wire.parse_dns_reply(p['payload'],tx['query']);comp=wire._comparison(parsed)
   require(type(e)is dict,'reply event object');t=number(e['elapsed_seconds']);rt.append(t)
   for k,v in {'transaction_id':ident,'destination_port':port,'reply_hex':p['payload'].hex(),'RRs':parsed['records'],'comparison_mode':comp[0]['mode'],'sorted_TTL_groups':comp[1],'accepted':True,'duplicate':bool(tx['replies'])}.items():require(same(e.get(k),v),'reply event '+k)
   require(tx['query_times'][0]<=t<=tx['query_times'][0]+10,'reply first-query10s window')
   tx['replies'].append((parsed,comp,t,e))
  require(rt==sorted(rt),'reply clocks ordered');checks[active]=True
  active='duplicate_identity_and_TTLs'
  for tx in transactions.values():
   require(1<=len(tx['replies'])<=3,'bounded answered transaction')
   for prev,now in zip(tx['replies'],tx['replies'][1:]):
    require(same(prev[1][0],now[1][0]),'non-TTL identity')
    require(len(prev[1][1])==len(now[1][1])and all(len(a)==len(b)and all(x<=y for x,y in zip(a,b))for a,b in zip(now[1][1],prev[1][1])),'TTL nonincrease')
  checks[active]=True
  active='whole_clock_bounds';created=number(egress['created_clock']);opened=number(egress['opened_clock']);absolute=number(egress['absolute_deadline']);end=number(egress['active_deadline'])
  require(opened>=created and absolute==created+420 and end==min(opened+180,absolute),'fixed independent whole clocks')
  require(all(opened-created<=t<end-created for t in qt+rt),'query/reply within active whole');checks[active]=True
  active='raw_chain_leases';leases=egress.get('leases');require(type(leases)is list and len(leases)==len(transactions),'one original lease per actualID');byid={}
  for l in leases:
   require(type(l)is dict and type(l.get('transaction_id'))is int and l['transaction_id']not in byid,'unique leaseID');byid[l['transaction_id']]=l
  require(set(byid)==set(transactions),'lease IDs exact')
  for ident,tx in transactions.items():
   first=tx['replies'][0];ttl=raw_ttl(first[0]);expiry=min(first[2]+ttl,end-created,absolute-created);l=byid[ident]
   require(same(l.get('first_query_elapsed'),tx['query_times'][0])and same(l.get('first_reply_elapsed'),first[2])and same(l.get('original_raw_TTL'),ttl)and same(l.get('original_expiry_elapsed'),expiry),'rawTTL/no60cap/whole expiry')
   tx.update(expiry=expiry,raw_ttl=ttl,first_answers=first[0]['answer_ipv4'])
   details['transactions'].append({'transaction_id':ident,'raw_TTL':ttl,'original_expiry_elapsed':expiry,'query_count':len(tx['queries']),'reply_count':len(tx['replies']),'first_A_order':tx['first_answers']})
  checks[active]=True
  active='no_duplicate_lease_extension'
  for tx in transactions.values():
   require(all(t-tx['query_times'][0]<=10 and t<tx['expiry']for t in tx['query_times']),'retry reuse window')
   for parsed,comp,t,e in tx['replies']:require(t<tx['expiry']and same(e.get('original_raw_chain_TTL'),tx['raw_ttl'])and same(e.get('original_lease_expires_elapsed_seconds'),tx['expiry']),'duplicate expiry exact unchanged')
  checks[active]=True
  active='first_answer_orders'
  for ident,tx in transactions.items():require(same(byid[ident].get('first_A_order'),tx['first_answers']),'first canonical answer order')
  checks[active]=True
  active='flow_lease_authority';flows=egress.get('flows');require(type(flows)is list and len(flows)==2,'two flows')
  for f in flows:
   require(type(f)is dict,'flow object');auth=f.get('authorization');require(type(auth)is dict and type(auth.get('transaction_id'))is int and auth['transaction_id']in transactions,'flow original queryID');tx=transactions[auth['transaction_id']];t=number(auth['admitted_at']);i=auth.get('first_canonical_answer_index')
   require(type(i)is int and 0<=i<len(tx['first_answers'])and f.get('remote_ipv4')==tx['first_answers'][i],'flow address index')
   require(created+tx['replies'][0][2]<=t<created+tx['expiry']and same(auth.get('original_lease_expires_at'),created+tx['expiry']),'new SYN lease not expired')
  checks[active]=True
  active='scope_and_revision';require(egress.get('profile')=='desktop-native-resolver-two-http-v1'and egress.get('first_error')is None and egress.get('parser_dependency_SHA256')=='20a0b6d5dbdff17b748f241623fc09989dd83b46957ab68862ae8799fecc6d8b','source profile/noerror/parser');checks[active]=True
 except (ValueError,TypeError,KeyError,IndexError,OverflowError,RecursionError,struct.error)as e:
  checks[active]=False;details['errors'].append({'check':active,'error':str(e)[:300]})
 details['not_evaluated']=[k for k,v in checks.items()if not v]
 return {'checks':checks,'details':details,'qualified_DNS_lease_records_only':all(checks.values())}
