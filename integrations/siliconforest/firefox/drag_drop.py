# SPDX-License-Identifier: MIT
"""Real seat drag between two owned DOM targets; no general clipboard use."""
import secrets
import time


def measure(client, request, frame):
    client.command('Marionette:SetContext', {'value': 'content'})

    def script(source, args=None):
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=args or [], newSandbox=False))

    token = 'SF-FX drag-' + secrets.token_hex(8)
    result = dict(synthetic_seat_input=True, physical_input=False, general_clipboard_untouched=True)
    from seat_geometry import calibrate
    mapping = calibrate(client, request, frame)
    result['calibration'] = mapping
    try:
        geometry = script('''const token=arguments[0];window.__sffxDrag={starts:0,drops:0,entered:0,over:0,ended:0};
          for(const [id,left,width,text] of [["sffxDragSource",24,160,"Drag owned text"],["sffxDragTarget",280,220,"Drop here"]]){
           const e=document.createElement("div");e.id=id;e.textContent=text;
           Object.assign(e.style,{position:"fixed",left:left+"px",top:"24px",width:width+"px",height:"72px",
             zIndex:"2147483646",background:"#ffc857",color:"#101820",border:"3px solid #101820"});document.body.append(e);}
          const a=document.getElementById("sffxDragSource"),b=document.getElementById("sffxDragTarget");a.draggable=true;
          a.addEventListener("dragstart",e=>{window.__sffxDrag.starts++;window.__sffxDrag.startTrusted=e.isTrusted;
            e.dataTransfer.setData("text/plain",token);e.dataTransfer.effectAllowed="copy";});
          b.addEventListener("dragenter",e=>{window.__sffxDrag.entered++;window.__sffxDrag.types=[...e.dataTransfer.types];});
          b.addEventListener("dragover",e=>{window.__sffxDrag.over++;e.preventDefault();e.dataTransfer.dropEffect="copy";});
          a.addEventListener("dragend",e=>{window.__sffxDrag.ended++;window.__sffxDrag.endEffect=e.dataTransfer.dropEffect;});
          b.addEventListener("drop",e=>{e.preventDefault();window.__sffxDrag.drops++;
            window.__sffxDrag.dropTrusted=e.isTrusted;window.__sffxDrag.matches=e.dataTransfer.getData("text/plain")===token;});
          return [a,b].map(e=>{const r=e.getBoundingClientRect();return {
            x:r.x+r.width/2,y:r.y+r.height/2};});''', [token])
        geometry = [dict(x=mapping['x']+p['x']*mapping['scale'],y=mapping['y']+p['y']*mapping['scale']) for p in geometry]
        x, y, width, height = frame
        for point in geometry:
            if not (x + 10 <= point['x'] < x + width - 10 and y + 30 <= point['y'] < y + height - 10):
                return dict(result, complete=False, error='DOM targets outside admitted owned frame', geometry=geometry)

        def move(px, py):
            return 'abs %d %d' % (round(px * 65535 / 2559), round(py * 65535 / 1599))

        source, target = geometry
        lines = [move(source['x'], source['y']), 'wait 80', 'buttons 1', 'wait 100']
        for step in range(1, 13):
            t = step / 12
            lines += [move(source['x'] + (target['x'] - source['x']) * t,
                           source['y'] + (target['y'] - source['y']) * t), 'wait 35']
        lines += ['wait 100', 'buttons 0', 'wait 500']
        request('\n'.join(lines))
        result.update(geometry=geometry, events=script('return window.__sffxDrag;'))
        events = result['events']
        result['complete'] = bool(events.get('starts') == 1 and events.get('drops') == 1 and
            events.get('startTrusted') and events.get('dropTrusted') and events.get('matches'))
        return result
    finally:
        # Release only this test's button, including when native drag code stalls.
        request('buttons 0\nwait 50')
        script('for(const id of ["sffxDragSource","sffxDragTarget"])document.getElementById(id)?.remove();return true;')
