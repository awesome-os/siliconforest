#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""FXPERF Marionette driver for MY Firefox (port 28291, own profile). Commands (one per argv item):
  wait            wait (<=60 s) for Marionette and start a session
  nav:<url>       navigate the selected tab (pageLoadStrategy none)
  tab:<url>       open a new foreground tab with url
  tabs:<n>:<url>  open n background tabs (url may contain {i})
  sel:<i>         select tab i
  prof:on[:interval_ms[:threads]]     start the Gecko profiler (all processes)
  prof:dump:<path>                    write the gzipped profile (parent + children) and stop
  mem:<path>      about:memory reports of all processes -> gz json
  proc:<path>     ChromeUtils.requestProcInfo -> json (per-thread CPU as Gecko sees it)
  startup:<path>  startup timeline (nsIAppStartup.getStartupInfo) + Troubleshoot graphics
  sleep:<s>
  quit            Marionette:Quit (clean shutdown)
"""
import json, socket, sys, time

PORT = 28291


class M:
    def __init__(s):
        s.k = socket.create_connection(('127.0.0.1', PORT), timeout=120); s.n = 0; s.rx()

    def rx(s):
        h = b''
        while True:
            c = s.k.recv(1)
            if not c: raise EOFError
            if c == b':': break
            h += c
        n = int(h); d = b''
        while len(d) < n:
            c = s.k.recv(n - len(d))
            if not c: raise EOFError
            d += c
        return json.loads(d)

    def cmd(s, name, p=None):
        s.n += 1
        b = json.dumps([0, s.n, name, p or {}]).encode()
        s.k.sendall(str(len(b)).encode() + b':' + b)
        r = s.rx()
        if r[2]: raise RuntimeError(r[2])
        v = r[3]
        return v['value'] if isinstance(v, dict) and set(v) == {'value'} else v

    def chrome(s, script, args=(), asyn=False, timeout=60000):
        s.cmd('Marionette:SetContext', {'value': 'chrome'})
        return s.cmd('WebDriver:ExecuteAsyncScript' if asyn else 'WebDriver:ExecuteScript',
                     {'script': script, 'args': list(args), 'scriptTimeout': timeout, 'newSandbox': True})


def connect():
    end = time.time() + 60
    while True:
        try:
            m = M(); break
        except OSError:
            if time.time() > end: raise
            time.sleep(0.5)
    m.cmd('WebDriver:NewSession', {'pageLoadStrategy': 'none'})
    m.cmd('WebDriver:SetTimeouts', {'script': 120000})
    return m


def main():
    m = None
    for item in sys.argv[1:]:
        kind, _, arg = item.partition(':')
        t0 = time.time()
        if kind == 'wait':
            m = connect(); print('marionette up', flush=True); continue
        if m is None:
            m = connect()
        if kind == 'nav':
            m.cmd('Marionette:SetContext', {'value': 'content'}); m.cmd('WebDriver:Navigate', {'url': arg})
        elif kind == 'tab':
            m.chrome('const w=Services.wm.getMostRecentWindow("navigator:browser");'
                     'w.gBrowser.selectedTab=w.gBrowser.addTab(arguments[0],{triggeringPrincipal:Services.scriptSecurityManager.getSystemPrincipal()});', [arg])
        elif kind == 'tabs':
            n, _, url = arg.partition(':')
            m.chrome('const w=Services.wm.getMostRecentWindow("navigator:browser");'
                     'for(let i=0;i<arguments[0];i++)w.gBrowser.addTab(arguments[1].replace("{i}",String(i+1)),{triggeringPrincipal:Services.scriptSecurityManager.getSystemPrincipal(),inBackground:true});',
                     [int(n), url])
        elif kind == 'sel':
            m.chrome('const w=Services.wm.getMostRecentWindow("navigator:browser");w.gBrowser.selectedTab=w.gBrowser.tabs[arguments[0]];', [int(arg)])
        elif kind == 'prof':
            sub, _, rest = arg.partition(':')
            if sub == 'on':
                parts = rest.split(':') if rest else []
                interval = float(parts[0]) if parts and parts[0] else 1.0
                threads = parts[1].split(',') if len(parts) > 1 else ['*']
                feats = ['js', 'stackwalk', 'cpuallthreads', 'processcpu', 'ipcmessages', 'markersallthreads']
                r = m.chrome('Services.profiler.StartProfiler(1<<24, arguments[0], arguments[1], arguments[2]); return Services.profiler.IsActive();',
                             [interval, feats, threads])
                print('profiler active', r)
            elif sub == 'dump':
                r = m.chrome('''const done=arguments[arguments.length-1];
                  Services.profiler.dumpProfileToFileAsync(arguments[0]).then(()=>{Services.profiler.StopProfiler(); done("ok");},
                    e=>done("ERR "+e));''', [rest], asyn=True, timeout=180000)
                print('profile dump', r)
        elif kind == 'mem':
            r = m.chrome('''const done=arguments[arguments.length-1];
              const d=Cc["@mozilla.org/memory-info-dumper;1"].getService(Ci.nsIMemoryInfoDumper);
              d.dumpMemoryReportsToNamedFile(arguments[0], ()=>done("ok"), null, false, false);''', [arg], asyn=True, timeout=180000)
            print('mem', r)
        elif kind == 'proc':
            r = m.chrome('''const done=arguments[arguments.length-1];
              ChromeUtils.requestProcInfo().then(p=>{
                const f=x=>({pid:x.pid,type:x.type,origin:x.origin||"",cpuTime:x.cpuTime,cpuCycleCount:x.cpuCycleCount,memory:x.memory,
                  threads:(x.threads||[]).map(t=>({name:t.name,tid:t.tid,cpuTime:t.cpuTime,cpuCycleCount:t.cpuCycleCount}))});
                done({t:Date.now(),parent:f(p),children:p.children.map(f)});}, e=>done("ERR "+e));''', asyn=True)
            open(arg, 'w').write(json.dumps(r)); print('proc', len(json.dumps(r)))
        elif kind == 'startup':
            r = m.chrome('''const done=arguments[arguments.length-1];
              const info=Services.startup.getStartupInfo(); const s={};
              for(const [k,v] of Object.entries(info)) s[k]=v instanceof Date?v.getTime():String(v);
              ChromeUtils.importESModule("resource://gre/modules/Troubleshoot.sys.mjs").Troubleshoot.snapshot().then(
                t=>done({startup:s,graphics:t.graphics,now:Date.now()}), e=>done({startup:s,err:String(e)}));''', asyn=True)
            open(arg, 'w').write(json.dumps(r)); print('startup', json.dumps(r.get('startup'))[:600])
        elif kind == 'sleep':
            time.sleep(float(arg))
        elif kind == 'quit':
            try:
                m.cmd('Marionette:Quit', {'flags': ['eForceQuit']})
            except (EOFError, OSError, RuntimeError) as e:
                print('quit:', repr(e))
            m = None
        print('%s done in %.2fs at %d' % (kind, time.time() - t0, int(time.time() * 1000)), flush=True)


if __name__ == '__main__':
    main()
