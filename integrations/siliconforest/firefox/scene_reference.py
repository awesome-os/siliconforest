# SPDX-License-Identifier: MIT
"""Freeze the owned WebGL/CSS scene and retain its actual renderer coordinates."""
import json
import time


def measure(client, output):
    client.command('Marionette:SetContext', {'value': 'content'})
    for handle in client.command('WebDriver:GetWindowHandles'):
        client.command('WebDriver:SwitchToWindow', {'handle': handle})
        found = client.command('WebDriver:ExecuteScript', dict(script='return typeof fxResult === "object";',
                                                             args=[], newSandbox=False))
        if found:
            break
    else:
        raise RuntimeError('Owned reference scene absent')
    result = client.command('WebDriver:ExecuteAsyncScript', dict(script='''
      const done=arguments[arguments.length-1];scrollTo(0,0);
      for(const animation of document.getAnimations()) {animation.pause();animation.currentTime=0;}
      requestAnimationFrame(()=>requestAnimationFrame(()=>{
        const boxes={},dom_boxes={};for(const name of ['gl','motion']){
          const e=document.querySelector(name==='gl'?'#gl':'.motion'),r=e.getBoundingClientRect();
          const inset=name==='gl'?3:2;
          dom_boxes[name]={x:r.x+inset,y:r.y+inset,width:r.width-2*inset,height:r.height-2*inset};
          boxes[name]={x:(mozInnerScreenX+r.x+inset)*devicePixelRatio,
            y:(mozInnerScreenY+r.y+inset)*devicePixelRatio,
            width:(r.width-2*inset)*devicePixelRatio,height:(r.height-2*inset)*devicePixelRatio};
        }
        const anchors=[];
        for(const [index,color] of [[253,1,251],[1,253,173]].entries()){
          const e=document.createElement('div');e.id='sf-reference-anchor-'+index;
          const pos=index*32;e.style.cssText=`all:initial;position:fixed;left:${pos}px;top:${pos}px;width:8px;height:8px;display:block;background:rgb(${color.join(',')});z-index:2147483647;pointer-events:none`;
          document.documentElement.append(e);const r=e.getBoundingClientRect();
          anchors.push({x:r.x,y:r.y,width:r.width,height:r.height,color});
        }
        done({boxes,dom_boxes,anchors,scale:devicePixelRatio,hash:fxResult.hash,glError:fxResult.glError,
          renderer:fxResult.renderer,visibility:document.visibilityState});
      }));''', args=[], newSandbox=False))
    time.sleep(.3)
    from renderer_capture import capture
    try:
        result['capture'] = capture(output.with_suffix('.ppm'))
        from screen_calibration import calibrate
        result['coordinate_calibration']=calibrate(output.with_suffix('.ppm'),result['anchors'],result['scale'])
        result['reported_boxes']=result['boxes']
        origin=result['coordinate_calibration']['origin'];scale=result['scale']
        result['boxes']={name:dict(x=origin[0]+box['x']*scale,y=origin[1]+box['y']*scale,
            width=box['width']*scale,height=box['height']*scale) for name,box in result['dom_boxes'].items()}
        for box in result['boxes'].values():
            if not (box['x']>=0 and box['y']>=0 and box['width']>0 and box['height']>0
                    and box['x']+box['width']<=2560 and box['y']+box['height']<=1600):
                raise RuntimeError('Calibrated reference region outside admitted panel')
    except Exception as error:
        result.update(complete=False,calibration_error=str(error))
        output.write_text(json.dumps(result,indent=2)+'\n')
        raise
    finally:
        client.command('WebDriver:ExecuteScript',dict(script='document.querySelectorAll("[id^=sf-reference-anchor-]").forEach(e=>e.remove());',args=[],newSandbox=False))
    result['complete'] = (result['hash'] == 'e4bc0ce1' and result['glError'] == 0
                          and result['visibility'] == 'visible')
    output.write_text(json.dumps(result, indent=2)+'\n')
    return result
