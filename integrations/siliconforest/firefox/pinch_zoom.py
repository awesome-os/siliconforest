# SPDX-License-Identifier: MIT
"""Observe Firefox zoom from a synthetic native pinch on its admitted window."""
import json
from pathlib import Path
import time
from presentation_seat import _request,pulse


def measure(client,directory):
    directory=Path(directory)
    client.command('Marionette:SetContext',{'value':'content'})
    for handle in client.command('WebDriver:GetWindowHandles'):
        client.command('WebDriver:SwitchToWindow',{'handle':handle})
        if client.command('WebDriver:ExecuteScript',dict(script='return typeof fxMeasureFrames==="function";',args=[],newSandbox=False)):break
    else:raise RuntimeError('Owned pinch fixture absent')
    admission=json.loads((directory/'presentation-seat.json').read_text())
    wake=pulse(directory/'presentation-seat.json',directory/'pinch-wake.json')
    x,y,width,height=admission['frame'];px=x+width//2;py=y+height//2
    if not (width>100 and height>100 and 0<=px<=2559 and 0<=py<=1599):raise RuntimeError('Owned pinch point outside panel')
    def state():
        client.command('Marionette:SetContext',{'value':'content'})
        content=client.command('WebDriver:ExecuteScript',dict(script='''return {url:location.href,visibility:document.visibilityState,
          width:innerWidth,height:innerHeight,scrollX,scrollY,scale:visualViewport?.scale??1,
          viewportWidth:visualViewport?.width,viewportHeight:visualViewport?.height};''',args=[],newSandbox=False))
        client.command('Marionette:SetContext',{'value':'chrome'})
        chrome=client.command('WebDriver:ExecuteScript',dict(script='''const w=Services.wm.getMostRecentWindow("navigator:browser");
          return {pid:Services.appinfo.processID,focused:w.document.hasFocus(),fullZoom:w.gBrowser.selectedBrowser.fullZoom};''',args=[],newSandbox=False))
        return dict(content=content,chrome=chrome)
    before=state()
    if before['chrome']['pid']!=wake['parent'] or not before['chrome']['focused'] or before['content']['visibility']!='visible':
        raise RuntimeError('Owned Firefox pinch focus/identity admission failed')
    inject=Path('/private/var/run/gpu-desktop/inject')
    point='abs %d %d\nwait 100' % (round(px*65535/2559),round(py*65535/1599))
    lines=[point,'pinch begin 2 0 0 1.0 0','wait 40']
    for scale in [1.05,1.1,1.15,1.2,1.25,1.3,1.35]:lines+=['pinch update 2 0 0 %.2f 0'%scale,'wait 30']
    lines+=['pinch end 2 0 0 1.0 0','wait 400']
    _request(inject,'\n'.join(lines))
    until=time.monotonic()+2
    while True:
        after=state()
        ratio=max(after['content']['scale']/before['content']['scale'],after['chrome']['fullZoom']/before['chrome']['fullZoom'])
        if ratio>1.1 or time.monotonic()>until:break
        time.sleep(.1)
    from renderer_capture import capture
    renderer=capture(directory/'pinch-renderer.ppm')
    result=dict(before=before,after=after,scale_ratio=ratio,complete=ratio>1.1,
        synthetic_input=True,physical_trackpad_qualified=False,point=[px,py],wake=wake,
        renderer=renderer,commands=lines,method='Existing compositor inject pinch to the admitted owned Firefox window; observe actual visual viewport or browser zoom. No preference is used to simulate a zoom; physical kernel pinch remains separate.')
    (directory/'pinch-zoom.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
