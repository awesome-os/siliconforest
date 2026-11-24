# SPDX-License-Identifier: MIT
"""Real inject finger-axis protocol -> AppKit -> trusted DOM pixel wheel.

Exact delta control disables smooth scrolling only in this owned profile.
Fractional native NSEvent controls are separate; this injector accepts integers.
"""
def measure(client, request, frame):
    client.command('Marionette:SetContext', {'value': 'content'})

    def script(source):
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=[], newSandbox=False))

    result = dict(synthetic_finger_protocol=True, physical_trackpad=False, injected_pixels=37)
    try:
        point = script('''window.__sffxWheel=[];
          const e=document.createElement("div");e.id="sffxScrollTarget";
          Object.assign(e.style,{position:"fixed",left:"24px",top:"180px",width:"500px",height:"220px",
            overflow:"auto",zIndex:"2147483646",background:"#ffc857",color:"#101820"});
          const inner=document.createElement("div");inner.style.height="2400px";
          inner.textContent="Owned precise pixel wheel target";e.append(inner);document.body.append(e);e.scrollTop=100;
          e.addEventListener("wheel",event=>window.__sffxWheel.push({trusted:event.isTrusted,
            mode:event.deltaMode,x:event.deltaX,y:event.deltaY}),{passive:true});
          const r=e.getBoundingClientRect();return {x:(mozInnerScreenX+r.x+r.width/2)*devicePixelRatio,
            y:(mozInnerScreenY+r.y+r.height/2)*devicePixelRatio,before:e.scrollTop};''')
        x, y, width, height = frame
        if not (x + 10 <= point['x'] < x + width - 10 and y + 30 <= point['y'] < y + height - 10):
            return dict(result, complete=False, error='Scroll target outside admitted frame', geometry=point)
        request('abs %d %d\nwait 100\nscroll 0 37\nwait 150\nscroll stop\nwait 500' %
                (round(point['x'] * 65535 / 2559), round(point['y'] * 65535 / 1599)))
        result.update(geometry=point, actual=script('return {events:window.__sffxWheel,after:document.getElementById("sffxScrollTarget").scrollTop};'))
        events = [e for e in result['actual']['events'] if e['x'] or e['y']]
        result['complete'] = bool(len(events) == 1 and events[0]['trusted'] and events[0]['mode'] == 0 and
            events[0]['x'] == 0 and abs(events[0]['y'] - 37) < .01 and
            abs(result['actual']['after'] - point['before'] - 37) <= 1)
        return result
    finally:
        script('document.getElementById("sffxScrollTarget")?.remove();return true;')
