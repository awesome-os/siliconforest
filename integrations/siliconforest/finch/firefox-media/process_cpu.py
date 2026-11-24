# SPDX-License-Identifier: MIT
"""Own-family Gecko CPU snapshots, separate RDD/threads from family totals.

The caller proves the Marionette parent's owned launch identity first and
supplies fresh pid -> OS start-identity mappings for its descendants. No input,
process signalling, profiler or browser configuration changes occur here.
"""
import time

SOURCE = 'https://github.com/mozilla-firefox/firefox/blob/main/dom/chrome-webidl/ChromeUtils.webidl'


def capture(client, parent_pid, owned_identities):
    """Return to content context even when diagnostics fail; never focus a window."""
    try:
        return _capture_chrome(client, parent_pid, owned_identities)
    finally:
        client.command('Marionette:SetContext', {'value': 'content'})


def _capture_chrome(client, parent_pid, owned_identities):
    client.command('Marionette:SetContext', {'value': 'chrome'})
    begin = time.monotonic()
    result = client.command('WebDriver:ExecuteAsyncScript', dict(script='''
      const done=arguments[arguments.length-1];
      ChromeUtils.requestProcInfo().then(p=>{
        const copy=q=>({pid:q.pid,type:q.type,childID:q.childID??null,cpuTime:q.cpuTime,
          memory:q.memory,threads:q.threads.map(t=>({tid:t.tid,name:t.name,cpuTime:t.cpuTime}))});
        done({parent:copy(p),children:p.children.map(copy)});
      },e=>done({error:String(e)}));''', args=[], newSandbox=False, scriptTimeout=10000))
    end = time.monotonic()
    if 'error' in result:
        raise RuntimeError(result['error'])
    if int(result['parent']['pid']) != int(parent_pid) or parent_pid not in owned_identities:
        raise RuntimeError('Gecko CPU snapshot is outside the verified owned parent')
    processes = [result['parent'], *result['children']]
    for process in processes:
        process['os_start_identity'] = owned_identities.get(int(process['pid']))
    return dict(begin_monotonic=begin, end_monotonic=end, processes=processes,
                cpu_time_unit='nanoseconds', source=SOURCE)


def compare(before, after):
    # Counter snapshots happen somewhere within each request. Preserve the
    # resulting wall-time uncertainty rather than using a fictional exact point.
    shortest = after['begin_monotonic'] - before['end_monotonic']
    longest = after['end_monotonic'] - before['begin_monotonic']
    if shortest <= 0 or longest < shortest:
        raise ValueError('CPU snapshot windows overlap or reverse')
    elapsed = (shortest + longest) / 2
    def key(p):
        return p['pid'], p.get('childID'), p['type'], p.get('os_start_identity')
    previous = {key(p): p for p in before['processes'] if p.get('os_start_identity') is not None}
    current = {key(p): p for p in after['processes'] if p.get('os_start_identity') is not None}
    rows=[]; totals={}; unavailable=[]
    for identity in sorted(previous.keys() & current.keys(), key=repr):
        a,b=previous[identity],current[identity]
        first,last=int(a['cpuTime']),int(b['cpuTime'])
        if first < 0 or last < first:
            unavailable.append(dict(pid=b['pid'], reason='counter-reset-or-invalid'));continue
        if first==last==0:
            unavailable.append(dict(pid=b['pid'], reason='zero-counters-availability-unproved'));continue
        delta=last-first
        row=dict(pid=b['pid'],type=b['type'],cpu_ns=delta,percent_one_core=delta/1e9/elapsed*100,
                 percent_bounds=[delta/1e9/longest*100,delta/1e9/shortest*100],threads=[],thread_counters_unavailable=[])
        old_threads={(t['tid'],t['name']):t for t in a['threads']}
        for thread in b['threads']:
            old=old_threads.get((thread['tid'],thread['name']))
            if old is not None and int(old['cpuTime'])==int(thread['cpuTime'])==0:
                row['thread_counters_unavailable'].append(dict(tid=thread['tid'],name=thread['name'],reason='zero-counter-availability-unproved'))
            elif old is not None and 0<=int(old['cpuTime'])<=int(thread['cpuTime']):
                used=int(thread['cpuTime'])-int(old['cpuTime'])
                row['threads'].append(dict(tid=thread['tid'],name=thread['name'],cpu_ns=used,
                                           percent_one_core=used/1e9/elapsed*100))
        row['threads'].sort(key=lambda t:t['cpu_ns'],reverse=True)
        rows.append(row);totals[b['type']]=totals.get(b['type'],0)+delta
    missing_identity=[dict(pid=p['pid'],type=p['type']) for p in before['processes']+after['processes'] if p.get('os_start_identity') is None]
    births=[dict(pid=current[k]['pid'],type=current[k]['type']) for k in current.keys()-previous.keys()]
    exits=[dict(pid=previous[k]['pid'],type=previous[k]['type']) for k in previous.keys()-current.keys()]
    family=sum(totals.values())
    return dict(wall_seconds_estimate=elapsed,wall_seconds_bounds=[shortest,longest],
                processes=sorted(rows,key=lambda r:r['cpu_ns'],reverse=True),
                cpu_ns_by_type=totals,percent_one_core_by_type={k:v/1e9/elapsed*100 for k,v in totals.items()},
                matched_family_percent_one_core=family/1e9/elapsed*100,
                family_complete=not(births or exits or missing_identity or unavailable),
                unmatched_new=births,unmatched_gone=exits,missing_os_identity=missing_identity,
                counters_unavailable=unavailable,rdd_counter_present='rdd' in totals,
                limits='Matched counters only; process turnover/zero counters are not zero CPU. Profiling adds overhead; do not use profiled runs as performance qualification.')
