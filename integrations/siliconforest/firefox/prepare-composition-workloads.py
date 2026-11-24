#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate private, static benchmark inputs; never copy a video into git."""
import argparse
from pathlib import Path

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--video', type=Path, required=True, help='existing owned M1 1080p60 test clip')
args=ap.parse_args()
if not args.video.is_absolute() or '..' in args.video.parts or not args.video.is_relative_to('/private/var/db'):
    raise ValueError('Use an explicitly owned target test clip')
args.out.mkdir(parents=True,exist_ok=True)
text='The quick brown fox jumps over the lazy dog. Smooth scrolling with readable text. '
scroll='<!doctype html><meta charset="utf-8"><title>SF-FX composition scroll</title><style>body{font:16px sans-serif;margin:0 40px}p{padding:12px;margin:8px 0}</style>'
scroll+=''.join('<p style="background:linear-gradient(90deg,hsl(%d,70%%,85%%),hsl(%d,70%%,75%%))">Paragraph %d: %s</p>'%(i*7%360,(i*7+90)%360,i,text*5)for i in range(240))
(args.out/'composition-scroll.html').write_text(scroll)
css='''<!doctype html><meta charset="utf-8"><title>SF-FX CSS composition</title><style>
body{margin:0;background:#101820;color:white;overflow:hidden;font:20px sans-serif}
.b{position:absolute;left:40px;width:160px;height:160px;border-radius:20px;animation:mv 2s linear infinite alternate}
@keyframes mv{from{transform:translate(0,0) rotate(0deg)}to{transform:translate(1050px,0) rotate(360deg)}}
</style><p>Four CSS animations</p>'''
css+=''.join('<div class="b" style="top:%dpx;background:%s;animation-duration:%ss"></div>'%(80+i*190,color,duration)for i,(color,duration)in enumerate([('#e74',2),('#4ae',1.5),('#ec4',3),('#a6e',2.5)]))
(args.out/'composition-css.html').write_text(css)
(args.out/'composition-video.html').write_text('<!doctype html><meta charset="utf-8"><title>SF-FX 1080p60 composition</title><body style="margin:0;background:black"><video autoplay muted loop playsinline style="width:1200px" src="'+args.video.as_uri()+'"></video>')
