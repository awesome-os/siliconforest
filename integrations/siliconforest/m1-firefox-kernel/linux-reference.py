#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run on the M1's Linux during the future exclusive installer visit only.

Default prints a plan. --execute launches ONLY an installed Firefox with a new
profile and uses existing stock benchmark code. No SSH/reboot/install/input injection.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import subprocess
import sys
import tempfile
import time

here = Path(__file__).resolve().parent

def module(name):
    spec=importlib.util.spec_from_file_location('sf_reference_'+name, here.parent/'firefox'/(name+'.py'))
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--execute',action='store_true')
    ap.add_argument('--output',type=Path)
    ap.add_argument('--minutes',type=int,default=30)
    args=ap.parse_args()
    plan={'installed_browser_only':True,'new_owned_profile':True,'input_injection':False,
          'benchmarks':['Speedometer3.1','MotionMark1.3','JetStream2.2'],
          'viewport_requested':{'outer_width':1280,'outer_height':960},
          'scale':'native Linux; record actual, never force owner display settings',
          'budget_minutes':args.minutes,'requires':'owner exclusive window, visible Linux desktop; normal installer visit, no extra reboot'}
    if not args.execute:
        print(json.dumps(plan,indent=2));return 0
    if not args.output or not 10<=args.minutes<=40:
        ap.error('--execute needs a fresh --output directory and --minutes between10 and40')
    if platform.system()!='Linux' or platform.machine() not in ('aarch64','arm64'):
        raise SystemExit('Reference must run locally on the M1 Linux arm64 system')
    firefox=shutil.which('firefox')
    if not firefox:
        print('SKIP: no installed Firefox; do not install one for this reference');return 0
    if not (os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')):
        raise SystemExit('Visible Linux desktop unavailable; no headless score substituted')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    meta={'plan':plan,'kernel':platform.uname()._asdict(),
          'firefox_path':firefox,'firefox_version':subprocess.check_output([firefox,'--version'],text=True),
          'environment':{k:os.environ.get(k) for k in ['DISPLAY','WAYLAND_DISPLAY','XDG_SESSION_TYPE','MOZ_ENABLE_WAYLAND']},
          'power':{},'cpufreq':{}}
    for base,key,pattern in [('/sys/class/power_supply','power','*/online'),('/sys/devices/system/cpu/cpufreq','cpufreq','policy*/scaling_governor')]:
        for p in Path(base).glob(pattern):
            try:meta[key][str(p)]=p.read_text().strip()
            except OSError as e:meta[key][str(p)]=str(e)
    (out/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
    client=None;process=None;deadline=time.monotonic()+args.minutes*60
    with tempfile.TemporaryDirectory(prefix='sf-linux-firefox-reference-') as profile:
        with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
        (Path(profile)/'user.js').write_text('user_pref("marionette.port",'+str(port)+');\nuser_pref("browser.shell.checkDefaultBrowser",false);\nuser_pref("browser.startup.page",0);\n')
        with (out/'firefox.log').open('w') as log:
            try:
                process=subprocess.Popen([firefox,'--no-remote','--profile',profile,'--marionette','about:blank'],stdout=log,stderr=subprocess.STDOUT)
                api=module('marionette')
                startup_deadline=min(deadline,time.monotonic()+60)
                while time.monotonic()<startup_deadline:
                    if process.poll() is not None:raise RuntimeError('Owned Firefox exited during startup')
                    try:client=api.Marionette(port=port,timeout=5);break
                    except (ConnectionRefusedError,TimeoutError):time.sleep(.5)
                if client is None:raise RuntimeError('Owned Firefox Marionette startup timed out')
                session=client.start();meta['session']=session
                if session['capabilities'].get('moz:processID')!=process.pid:
                    raise RuntimeError('Marionette did not identify our newly launched process')
                client.command('WebDriver:SetWindowRect',{'width':1280,'height':960})
                meta['support']=client.support();meta['runtime']=client.runtime()
                (out/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
                benchmark=module('benchmark');results=[]
                names={'speedometer':'Speedometer3.1','motionmark':'MotionMark1.3','jetstream':'JetStream2.2'}
                for kind,name in names.items():
                    client.command('Marionette:SetContext',{'value':'content'})
                    client.command('WebDriver:Navigate',{'url':'https://browserbench.org/'+name+'/'})
                    result=benchmark.measure(client,kind,deadline,out/(kind+'.json'),seat_activity=False)
                    results.append(result)
                (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
                return 0 if all(r.get('complete') for r in results) else 1
            finally:
                if client:
                    try:client.command('Marionette:Quit',{'flags':['eForceQuit']})
                    except (EOFError,OSError,RuntimeError):pass
                    client.socket.close()
                if process and process.poll() is None:
                    process.terminate()
                    try:process.wait(timeout=10)
                    except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)

if __name__=='__main__':sys.exit(main())
