# SPDX-License-Identifier: MIT
"""Calibrate CSS positions from trusted movement inside an admitted window."""
def calibrate(client, request, frame):
    client.command('Marionette:SetContext', {'value': 'content'})
    def script(source):
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=[], newSandbox=False))
    script('''window.__sffxPointer=null;window.__sffxPointerHandler=e=>{
      window.__sffxPointer={trusted:e.isTrusted,x:e.clientX,y:e.clientY,scale:devicePixelRatio};};
      document.addEventListener("mousemove",window.__sffxPointerHandler,true);return true;''')
    x,y,width,height=frame;px=x+width//2+11;py=y+height//2+17
    def move(a,b):
        return 'abs %d %d' % (round(a*65535/2559),round(b*65535/1599))
    try:
        request(move(px-15,py-12)+'\nwait 60\n'+move(px,py)+'\nwait 120')
        event=script('return window.__sffxPointer;')
        if not event or not event['trusted'] or event['scale']<=0:
            raise RuntimeError('No trusted owned-content pointer calibration')
        return dict(x=px-event['x']*event['scale'],y=py-event['y']*event['scale'],scale=event['scale'],event=event)
    finally:
        script('document.removeEventListener("mousemove",window.__sffxPointerHandler,true);delete window.__sffxPointerHandler;return true;')
