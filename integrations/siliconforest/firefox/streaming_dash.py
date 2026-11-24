# SPDX-License-Identifier: MIT
"""Official DASH-IF reference player, pinned public clear H264 stream.

The versioned player and query contract are inspectable at:
https://github.com/Dash-Industry-Forum/dash.js/blob/v4.7.4/samples/dash-if-reference-player/app/main.js
No account, advertising, license request or DRM bypass supplies evidence.
"""
import json
import re
import subprocess
import time

MANIFEST = 'https://dash.akamaized.net/envivio/EnvivioDash3/manifest.mpd'
PLAYER = 'https://reference.dashif.org/dash.js/v4.7.4/samples/dash-if-reference-player/index.html'


def measure(client, output, deadline):
    result = dict(site=PLAYER, manifest=MANIFEST, drm=False, muted=True, synthetic_play=True)

    def save():
        output.write_text(json.dumps(result, indent=2) + '\n')

    def script(source, asynchronous=False):
        return client.command('WebDriver:ExecuteAsyncScript' if asynchronous else 'WebDriver:ExecuteScript',
                              dict(script=source, args=[MANIFEST], newSandbox=False))

    client.command('Marionette:SetContext', {'value': 'content'})
    for handle in client.command('WebDriver:GetWindowHandles'):
        client.command('WebDriver:SwitchToWindow', {'handle': handle})
        if script('return location.href;').startswith(PLAYER):
            break
    else:
        raise RuntimeError('Owned versioned DASH-IF player tab not found')
    until = min(deadline - 35, time.monotonic() + 45)
    while time.monotonic() < until:
        state = script('''const v=document.querySelector("video"),names=performance.getEntriesByType("resource").map(e=>e.name);
          let source=null,codec=null;
          try{const s=angular.element(document.querySelector("[ng-controller]")).scope();
            source=s.player.getSource();codec=s.player.getCurrentTrackFor("video")?.codec;}catch(error){}
          return {url:location.href,video:!!v,source,codec,readyState:v?.readyState,currentSrc:v?.currentSrc,
            encrypted:!!v?.mediaKeys,error:v?.error?.code||null,
            manifestFetched:names.some(n=>n.startsWith(arguments[0])),
            mediaResources:names.filter(n=>n.startsWith(arguments[0].slice(0,arguments[0].lastIndexOf("/")+1))).slice(-12)};''')
        result['readiness'] = state
        save()
        if state.get('manifestFetched') and state.get('readyState', 0) >= 2 and str(state.get('currentSrc', '')).startswith('blob:'):
            break
        if state.get('error') or state.get('encrypted'):
            break
        time.sleep(.5)
    state = result.get('readiness', {})
    if not state.get('manifestFetched') or state.get('readyState', 0) < 2 or state.get('encrypted') or state.get('error'):
        result.update(complete=False, error='Requested clear DASH source did not become ready')
        save()
        return result
    counters = lambda: subprocess.run(['sysctl', 'kern.sf_avd.stats'], capture_output=True, text=True).stdout.strip()
    result['avd_before'] = counters()
    client.command('WebDriver:SetTimeouts', {'script': 35000})
    result['playback'] = script('''const done=arguments[arguments.length-1];
      (async()=>{const v=document.querySelector("video");v.muted=true;
       const start=v.currentTime,before=v.getVideoPlaybackQuality(),wall=performance.now();
       try{await Promise.race([v.play(),new Promise((_,reject)=>setTimeout(()=>reject(new Error("play timeout")),8000))]);
        await new Promise(resolve=>setTimeout(resolve,15000));const after=v.getVideoPlaybackQuality();
        done({startMedia:start,endMedia:v.currentTime,wallSeconds:(performance.now()-wall)/1000,
          width:v.videoWidth,height:v.videoHeight,muted:v.muted,encrypted:!!v.mediaKeys,error:v.error?.code||null,
          playbackRate:v.playbackRate,
          currentSrc:v.currentSrc,before:{total:before.totalVideoFrames,dropped:before.droppedVideoFrames},
          after:{total:after.totalVideoFrames,dropped:after.droppedVideoFrames}});
       }catch(error){done({error:String(error)});}finally{v.pause();}})();''', True)
    result['avd_after'] = counters()
    before = re.search(r'jobs (\d+)', result['avd_before'])
    after = re.search(r'jobs (\d+)', result['avd_after'])
    result['hardware_jobs'] = int(after[1]) - int(before[1]) if before and after else None
    playback = result['playback']
    result['complete'] = bool(not playback.get('error') and not playback.get('encrypted') and playback.get('muted') and
        str(playback.get('currentSrc', '')).startswith('blob:') and
        playback.get('endMedia', 0) - playback.get('startMedia', 0) >= 12 and
        playback.get('endMedia', 0) - playback.get('startMedia', 0) <= playback.get('wallSeconds', 0) + 1 and
        playback.get('playbackRate') == 1 and
        playback.get('after', {}).get('total', 0) > playback.get('before', {}).get('total', 0) and
        result.get('hardware_jobs', 0) and result['hardware_jobs'] > 0)
    save()
    return result
