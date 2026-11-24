# SPDX-License-Identifier: MIT
"""Observe and mask visible frame documents in an owned legibility session.

Coordinates remain in the top document's CSS viewport. Scaled/rotated frames
are recorded as unsupported rather than assigned invented pixel rectangles.
"""
from contextlib import contextmanager
import math


@contextmanager
def document(client, path):
    client.command('WebDriver:SwitchToFrame', {'id': None})
    try:
        for frame in path:
            client.command('WebDriver:SwitchToFrame', {'id': frame})
        yield
    finally:
        client.command('WebDriver:SwitchToFrame', {'id': None})


def discover(client, script):
    root = dict(path=[], offset=[0, 0], clip=None, depth=0)
    documents, skipped = [root], []
    for current in documents:
        with document(client, current['path']):
            frames = script('''return [...document.querySelectorAll('iframe,frame')].map(e=>{
              const r=e.getBoundingClientRect(),s=getComputedStyle(e);let alpha=1;
              for(let n=e;n;n=n.parentElement)alpha*=Number(getComputedStyle(n).opacity);
              const hit=document.elementFromPoint(Math.max(0,Math.min(innerWidth-1,r.x+r.width/2)),
                Math.max(0,Math.min(innerHeight-1,r.y+r.height/2)));
              return {element:e,src:e.src,x:r.x+e.clientLeft,y:r.y+e.clientTop,
                width:e.clientWidth,height:e.clientHeight,scaleX:r.width/e.offsetWidth,
                scaleY:r.height/e.offsetHeight,transform:s.transform,
                visible:s.display!=='none'&&s.visibility==='visible'&&alpha>=.01&&
                  r.right>0&&r.bottom>0&&r.left<innerWidth&&r.top<innerHeight&&
                  !!hit&&(e===hit||e.contains(hit)),viewport:[innerWidth,innerHeight]};});''')
        for frame in frames:
            if not frame['visible'] or frame['width'] < 4 or frame['height'] < 4:
                continue
            if (current['depth'] >= 3 or len(documents) >= 12 or
                    frame['transform'] != 'none' or
                    any(not math.isfinite(frame[k]) or abs(frame[k] - 1) > .01
                        for k in ['scaleX', 'scaleY'])):
                skipped.append(dict(src=frame['src'], reason='Frame transform or traversal bound'))
                continue
            ox, oy = current['offset']
            x, y = ox + frame['x'], oy + frame['y']
            clip = [max(ox, x), max(oy, y),
                    min(ox + frame['viewport'][0], x + frame['width']),
                    min(oy + frame['viewport'][1], y + frame['height'])]
            if current['clip'] is not None:
                clip = [max(clip[0], current['clip'][0]), max(clip[1], current['clip'][1]),
                        min(clip[2], current['clip'][2]), min(clip[3], current['clip'][3])]
            documents.append(dict(path=current['path'] + [frame['element']], offset=[x, y],
                                  clip=clip, depth=current['depth'] + 1, src=frame['src']))
    return documents, skipped


def translate_runs(runs, offset, clip):
    translated = []
    def translate_boxes(boxes):
        boxes = [dict(box, x=box['x'] + offset[0], y=box['y'] + offset[1]) for box in boxes]
        return [b for b in boxes if clip[0] <= b['x'] and clip[1] <= b['y'] and
                b['x'] + b['width'] <= clip[2] and b['y'] + b['height'] <= clip[3]]
    for run in runs:
        boxes = translate_boxes(run['boxes'])
        if boxes:
            result = dict(run, boxes=boxes)
            if 'han_samples' in run:
                result['han_samples'] = [dict(sample, boxes=sample_boxes)
                    for sample in run['han_samples']
                    if (sample_boxes := translate_boxes(sample['boxes']))]
            translated.append(result)
    return translated


def mask(client, script, documents, enabled):
    for item in documents:
        with document(client, item['path']):
            if enabled:
                script('''if(window.__sffxLegibilityMask)throw new Error('Text mask already exists');
                  const style=document.createElement('style');
                  style.textContent='*{color:transparent!important;-webkit-text-fill-color:transparent!important;text-shadow:none!important}';
                  document.documentElement.append(style);window.__sffxLegibilityMask=style;return true;''')
            else:
                script('window.__sffxLegibilityMask?.remove();delete window.__sffxLegibilityMask;return true;')


@contextmanager
def freeze_transitions(client, script, documents):
    """Keep color transitions from turning our paint control into a fade."""
    try:
        for item in documents:
            with document(client, item['path']):
                script('''if(window.__sffxLegibilityTransitions)throw new Error('Transition control already exists');
                  const style=document.createElement('style');style.textContent='*{transition:none!important}';
                  document.documentElement.append(style);window.__sffxLegibilityTransitions=style;return true;''')
        yield
    finally:
        for item in documents:
            with document(client, item['path']):
                script('window.__sffxLegibilityTransitions?.remove();delete window.__sffxLegibilityTransitions;return true;')
