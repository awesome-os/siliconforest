# SPDX-License-Identifier: MIT
"""Observe laid-out text and content pixels; loading alone is not legibility.

Text masking changes only paint properties in this owned diagnostic document.
The original style is restored in finally. Paired content images are evidence,
not a substitute for reviewing the compositor's renderer capture.
"""
import base64
import json
import time
from urllib.parse import urlsplit


def measure(client, directory, handles=None, prefix="legibility", seat_activity=False):
    client.command('Marionette:SetContext', {'value': 'content'})
    rows=[]
    def script(source):
        return client.command('WebDriver:ExecuteScript',dict(script=source,args=[],newSandbox=False))
    def paint():
        client.command('WebDriver:ExecuteAsyncScript',dict(script='const done=arguments[arguments.length-1];requestAnimationFrame(()=>requestAnimationFrame(()=>done(true)));',args=[],newSandbox=False,scriptTimeout=5000))
    def shot(path):
        data=client.command('WebDriver:TakeScreenshot',dict(id=None,highlights=[],full=False))
        path.write_bytes(base64.b64decode(data))
    for handle in (handles or client.command('WebDriver:GetWindowHandles')):
        client.command('WebDriver:SwitchToWindow', {'handle': handle})
        url=client.command('WebDriver:GetCurrentURL')
        if urlsplit(url).scheme not in ['http','https','file']:continue
        time.sleep(4)
        image_prefix=prefix+'-%02d'%len(rows)
        admission=None
        if seat_activity:
            # A tab switch through Marionette is not an AppKit input event.
            # Admit each page's actual window, just as in the Page Down gate,
            # rather than assuming the initial tab's admission covers it.
            from presentation_seat import admit
            admission=admit(client,directory/(image_prefix+'-admission.json'),fixture=False,handle=handle)
            if client.command('WebDriver:GetWindowHandle') != handle:
                raise RuntimeError('Presentation admission changed the requested legibility tab')
        collect_source='''const runs=[],excluded=[],walk=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);let node;
          function visibleBox(rect,element){
            // A partially offscreen glyph is not a measurable complete run.
            if(rect.left<0||rect.top<0||rect.right>innerWidth||rect.bottom>innerHeight)return null;
            const left=rect.left,top=rect.top,right=rect.right,bottom=rect.bottom;
            for(let e=element;e;e=e.parentElement){
              const s=getComputedStyle(e);
              if(s.contentVisibility==='hidden')return null;
              // Accessibility-only labels commonly use an empty legacy clip.
              if(s.clip!=='auto'&&s.clip.startsWith('rect(')){
                const values=s.clip.slice(5,-1).split(/[, ]+/).map(parseFloat);
                if(values.length===4&&values.every(Number.isFinite)&&(values[1]<=values[3]||values[2]<=values[0]))return null;
              }
            }
            return right-left>2&&bottom-top>2?{x:left,y:top,width:right-left,height:bottom-top}:null;
          }
          while((node=walk.nextNode())&&runs.length<80){
            if(!node.textContent.trim()||!node.parentElement)continue;
            const s=getComputedStyle(node.parentElement);let opacity=1;for(let e=node.parentElement;e;e=e.parentElement)opacity*=Number(getComputedStyle(e).opacity);if(opacity<.01)continue;if(s.display==='none'||s.visibility!=='visible'||Number(s.opacity)===0)continue;
            const range=document.createRange();range.selectNodeContents(node);
            const boxes=[...range.getClientRects()].map(r=>visibleBox(r,node.parentElement)).filter(Boolean);
            if(!boxes.length){excluded.push({text:node.textContent.trim().slice(0,120),reason:'No complete viewport rectangle, hidden content, or empty accessibility clip'});continue;}
            if(boxes.some(r=>{const hit=document.elementFromPoint(r.x+r.width/2,r.y+r.height/2);return hit&&!(node.parentElement.contains(hit)||hit.contains(node.parentElement));}))continue;
            // A run with one missing glyph still has ink. Observe individual Han
            // ranges too; these are not Latin/Arabic ligature or combining units.
            const han_samples=[];let offset=0;
            for(const character of node.textContent.slice(0,256)){
              const start=offset;offset+=character.length;const cp=character.codePointAt(0);
              if(!((cp>=0x3400&&cp<=0x9fff)||(cp>=0xf900&&cp<=0xfaff)||(cp>=0x20000&&cp<=0x323af)))continue;
              const glyphRange=document.createRange();glyphRange.setStart(node,start);glyphRange.setEnd(node,offset);
              const glyphBoxes=[...glyphRange.getClientRects()].map(r=>visibleBox(r,node.parentElement)).filter(Boolean).filter(r=>{
                const hit=document.elementFromPoint(r.x+r.width/2,r.y+r.height/2);
                return hit&&(node.parentElement.contains(hit)||hit.contains(node.parentElement));});
              if(glyphBoxes.length)han_samples.push({text:character,boxes:glyphBoxes});
            }
            runs.push({text:node.textContent.trim().slice(0,120),boxes,han_samples,font:s.font,fontFamily:s.fontFamily,fontWeight:s.fontWeight,color:s.color,background:s.backgroundColor,fontVariantCaps:s.fontVariantCaps,fontFeatureSettings:s.fontFeatureSettings,fontVariationSettings:s.fontVariationSettings,transitionProperty:s.transitionProperty,transitionDuration:s.transitionDuration});
          }
          return {url:location.href,title:document.title,readyState:document.readyState,bodyTextLength:document.body?.innerText.length||0,
            devicePixelRatio,viewport:{width:innerWidth,height:innerHeight},fontsStatus:document.fonts.status,
            faces:[...document.fonts].map(f=>({family:f.family,status:f.status,weight:f.weight,style:f.style,stretch:f.stretch})),
            fontResources:performance.getEntriesByType('resource').map(r=>r.name).filter(n=>/\\.(woff2?|ttf|otf)([?#]|$)/i.test(n)),han_sampling:true,runs,excluded};'''
        row=script(collect_source)
        from used_fonts import observe
        row['actual_font_faces']=observe(client)
        from legibility_documents import discover, document, translate_runs, mask, freeze_transitions
        documents, skipped = discover(client, script)
        row['frame_documents']=[]
        row['unsupported_frames']=skipped
        for item in documents[1:]:
            with document(client, item['path']):
                child=script(collect_source)
                child_faces=observe(client)
            child_runs=translate_runs(child['runs'], item['offset'], item['clip'])
            row['runs'].extend(child_runs)
            row['faces'].extend(child['faces'])
            row['fontResources'].extend(child['fontResources'])
            row['frame_documents'].append(dict(url=child['url'],offset=item['offset'],clip=item['clip'],
                measured_runs=len(child_runs),fontsStatus=child['fontsStatus'],actual_font_faces=child_faces))
        row['per_page_virtual_admission']=admission
        row['transitions_suppressed_during_capture']=True
        with freeze_transitions(client,script,documents):
            from renderer_capture import capture
            from screen_calibration import calibrate
            anchors=script('''const anchors=[{x:5,y:5,width:8,color:[17,241,113]},{x:101,y:101,width:8,color:[233,19,157]}];
              window.__sffxLegibilityAnchors=anchors.map(a=>{const n=document.createElement('div');n.style.cssText=`all:initial!important;position:fixed!important;left:${a.x}px!important;top:${a.y}px!important;width:${a.width}px!important;height:${a.width}px!important;background:rgb(${a.color.join(',')})!important;z-index:2147483647!important;pointer-events:none!important`;document.documentElement.append(n);return n});return anchors;''')
            try:
                paint()
                row['marker_document']=script('''return {url:location.href,title:document.title,hidden:document.hidden,
                  visibility:document.visibilityState,markers:window.__sffxLegibilityAnchors.map(n=>{
                    const s=getComputedStyle(n),r=n.getBoundingClientRect();return {connected:n.isConnected,
                      rect:{x:r.x,y:r.y,width:r.width,height:r.height},background:s.backgroundColor,
                      display:s.display,visibility:s.visibility,opacity:s.opacity,zIndex:s.zIndex};})};''')
                row['calibration_attempts']=[]
                before=time.monotonic()
                for attempt,delay in enumerate([0,.125,.375]):
                    if delay:time.sleep(delay)
                    path=directory/(image_prefix+'-calibration'+('-'+str(attempt) if attempt else '')+'.ppm')
                    capture(path)
                    record=dict(path=path.name,elapsed_ms=(time.monotonic()-before)*1000)
                    row['calibration_attempts'].append(record)
                    try:
                        row['calibration']=calibrate(path,anchors,row['devicePixelRatio'])
                        record['complete']=True
                    except ValueError as error:
                        record.update(complete=False,error=str(error))
                    (directory/(image_prefix+'-calibration-attempts.json')).write_text(json.dumps(dict(
                        marker_document=row['marker_document'],attempts=row['calibration_attempts']),indent=2)+'\n')
                    if record['complete']:break
                else:raise RuntimeError('DOM calibration markers did not reach the renderer in the bounded observation')
            finally:
                script('window.__sffxLegibilityAnchors?.forEach(n=>n.remove());delete window.__sffxLegibilityAnchors;return true;')
            paint();shot(directory/(image_prefix+'-visible.png'))
            capture(directory/(image_prefix+'-visible-renderer.ppm'))
            try:
                mask(client,script,documents,True)
                paint();shot(directory/(image_prefix+'-hidden.png'))
                capture(directory/(image_prefix+'-hidden-renderer.ppm'))
            finally:
                mask(client,script,documents,False)
            paint()
            from renderer_capture import capture
            row['renderer']=capture(directory/(image_prefix+'-renderer.ppm'))
        row['renderer_images']={'visible':image_prefix+'-visible-renderer.ppm',
            'text_hidden':image_prefix+'-hidden-renderer.ppm','restored':image_prefix+'-renderer.ppm'}
        row['content_images_readback_only']=True
        row['content_images']={'visible':image_prefix+'-visible.png','text_hidden':image_prefix+'-hidden.png'}
        row['loaded_only']=True;row['legibility_qualified']=False;row['visual_review_pending']=True
        row['requested_handle']=handle
        rows.append(row);(directory/(prefix+'.json')).write_text(json.dumps(rows,indent=2)+'\n')
    if not rows:raise RuntimeError('No owned ordinary page found for legibility checks')
    from legibility_pixels import analyze
    pixels=analyze(directory,prefix+'.json')
    return dict(pages=rows,pixel_checks=pixels,
                complete=all(p['paint_check_pass'] for p in pixels) and not any(r['unsupported_frames'] for r in rows),
                diagnostic_complete=True,legibility_qualified=False,
                method='Laid-out visible text ranges including admitted frame documents, paired paint-only text masks and actual renderer captures. Pixel analysis and visual review required; no load-only PASS.')
