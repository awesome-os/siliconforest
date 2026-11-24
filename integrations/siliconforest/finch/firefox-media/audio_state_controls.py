#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Controls only the already admitted diagnostic Firefox and its local pages.

No synthetic input or OS volume/device changes. HTML state is correlated with
the separate native PCM meter; state alone never proves silence or HAL output.
"""
import time


def run(client,url,pause):
    rows=[]
    def js(script):
        client.command('Marionette:SetContext',{'value':'content'})
        return client.command('WebDriver:ExecuteScript',dict(script=script,args=[],newSandbox=False))
    def phase(name,script='',seconds=1.6):
        start=time.monotonic()
        before=js(script+'; return fxLifecycleState();') if script else js('return fxLifecycleState();')
        pause(seconds);after=js('return fxLifecycleState();')
        row=dict(name=name,start=start,end=time.monotonic(),before=before,after=after)
        rows.append(row)
        if before['error'] or after['error']:raise RuntimeError('Owned audio element error: '+name)
        return row
    def ready():
        deadline=time.monotonic()+8
        while not js('return typeof fxLifecycleStart === "function";'):
            if time.monotonic()>deadline:raise TimeoutError('Own second audio page not ready')
            pause(.1)
    def play():
        return client.command('WebDriver:ExecuteAsyncScript',dict(
            script='const done=arguments[arguments.length-1];fxLifecycleStart().then(done,e=>done({error:String(e)}));',
            args=[],newSandbox=False,scriptTimeout=8000))
    first=client.command('WebDriver:GetWindowHandle')
    phase('full','audio.volume=.1; audio.muted=false;')
    phase('half','audio.volume=.05;')
    phase('muted','audio.muted=true;')
    phase('unmuted','audio.muted=false;')
    paused=phase('paused','audio.pause();')
    if not paused['after']['paused'] or abs(paused['after']['time']-paused['before']['time'])>.01:
        raise RuntimeError('Own paused document clock still advances')
    if play().get('error'):raise RuntimeError('Own restart failed')
    phase('restarted')
    second=client.command('WebDriver:NewWindow',{'type':'tab'})['handle']
    client.command('WebDriver:SwitchToWindow',{'handle':second})
    client.command('WebDriver:Navigate',{'url':url});ready()
    if play().get('error'):raise RuntimeError('Own second tab failed to play')
    phase('two-tabs')
    client.command('WebDriver:SwitchToWindow',{'handle':first})
    js('audio.pause();return fxLifecycleState();')
    client.command('WebDriver:SwitchToWindow',{'handle':second})
    phase('second-with-first-paused')
    client.command('WebDriver:SwitchToWindow',{'handle':first})
    client.command('WebDriver:CloseWindow')
    client.command('WebDriver:SwitchToWindow',{'handle':second})
    phase('second-with-first-closed')
    # Keep only our blank tab after closing the last media document, so a warm
    # cubeb stream can be distinguished from nonzero audio before normal quit.
    blank=client.command('WebDriver:NewWindow',{'type':'tab'})['handle']
    client.command('WebDriver:SwitchToWindow',{'handle':second})
    start=time.monotonic();client.command('WebDriver:CloseWindow')
    client.command('WebDriver:SwitchToWindow',{'handle':blank});pause(4)
    rows.append(dict(name='all-media-tabs-closed',start=start,end=time.monotonic(),
                     remaining_handles=client.command('WebDriver:GetWindowHandles')))
    return dict(phases=rows,owned_handles=[first,second,blank],synthetic_input=False,
                output_qualified=False,acoustic_proof=False)
