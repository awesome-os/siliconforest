# SPDX-License-Identifier: MIT
"""Finite playback of an owned public, non-DRM YouTube video in a fresh profile.

No account, cookies from another profile, ad playback, or consent bypass supplies
evidence. Public player methods are inspected at runtime; unsupported pages fail.
"""
import json
import subprocess
import time

VIDEO_ID = 'aqz-KE-bpKQ'


def measure(client, output, deadline):
    result = dict(site='https://www.youtube.com/watch?v=' + VIDEO_ID, muted=True,
                  required_play_seconds=15, synthetic_play=True, drm=False)

    def save():
        output.write_text(json.dumps(result, indent=2) + '\n')

    def script(source, asynchronous=False):
        return client.command('WebDriver:ExecuteAsyncScript' if asynchronous else 'WebDriver:ExecuteScript',
                              dict(script=source, args=[VIDEO_ID], newSandbox=False))

    client.command('Marionette:SetContext', {'value': 'content'})
    for handle in client.command('WebDriver:GetWindowHandles'):
        client.command('WebDriver:SwitchToWindow', {'handle': handle})
        location = script('return location.href;')
        if location.startswith('https://www.youtube.com/'):
            break
    else:
        result.update(complete=False, error='Owned public YouTube tab not found; no consent or account page activated')
        save()
        return result
    until = min(deadline - 45, time.monotonic() + 50)
    while time.monotonic() < until:
        state = script('''const p=document.querySelector("#movie_player"),v=document.querySelector("video");
          let data=null;try{data=p?.getVideoData?.();}catch(error){}
          return {title:document.title,url:location.href,player:!!p,video:!!v,
            videoId:data?.video_id,videoTitle:data?.title,ad:p?.classList.contains("ad-showing"),
            readyState:v?.readyState,error:v?.error?.code};''')
        result['readiness'] = state
        save()
        if state.get('videoId') == VIDEO_ID and state.get('video') and not state.get('ad'):
            break
        time.sleep(1)
    state = result.get('readiness', {})
    if state.get('videoId') != VIDEO_ID or state.get('ad') or not state.get('video'):
        result.update(complete=False, error='Requested public video was not ready without advertising/consent interaction')
        save()
        return result
    counters = lambda: subprocess.run(['sysctl', 'kern.sf_avd.stats'], capture_output=True, text=True).stdout.strip()
    result['avd_before'] = counters()
    client.command('WebDriver:SetTimeouts', {'script': 40000})
    result['playback'] = script('''const wanted=arguments[0],done=arguments[arguments.length-1];
      (async()=>{
        const v=document.querySelector("video"),p=document.querySelector("#movie_player"),frames=[];
        v.muted=true;const before=v.getVideoPlaybackQuality(),start=performance.now(),startMedia=v.currentTime;
        let callback;
        if(v.requestVideoFrameCallback){const frame=(wall,m)=>{frames.push({wall,mediaTime:m.mediaTime});
          callback=v.requestVideoFrameCallback(frame);};callback=v.requestVideoFrameCallback(frame);}
        try{
          await Promise.race([v.play(),new Promise((_,reject)=>setTimeout(()=>reject(new Error("play timeout")),10000))]);
          await new Promise(resolve=>setTimeout(resolve,15000));
          const after=v.getVideoPlaybackQuality(),data=p?.getVideoData?.();
          done({videoId:data?.video_id,videoTitle:data?.title,ad:p?.classList.contains("ad-showing"),
            startMedia,endMedia:v.currentTime,wallSeconds:(performance.now()-start)/1000,muted:v.muted,
            width:v.videoWidth,height:v.videoHeight,readyState:v.readyState,error:v.error?.code||null,
            before:{total:before.totalVideoFrames,dropped:before.droppedVideoFrames},
            after:{total:after.totalVideoFrames,dropped:after.droppedVideoFrames},frames});
        }catch(error){done({error:String(error)});}
        finally{if(callback&&v.cancelVideoFrameCallback)v.cancelVideoFrameCallback(callback);v.pause();}
      })();''', True)
    result['avd_after'] = counters()
    playback = result['playback']
    result['complete'] = bool(playback.get('videoId') == VIDEO_ID and not playback.get('ad') and
        not playback.get('error') and playback.get('muted') and playback.get('endMedia', 0) - playback.get('startMedia', 0) >= 12 and
        playback.get('after', {}).get('total', 0) > playback.get('before', {}).get('total', 0))
    save()
    return result
