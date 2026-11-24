#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The gallery of the four SiliconForest themes: one HTML page with its pictures beside it.

    make_gallery.py --shots DIR --tag TAG --chrome DIR --dock DIR --out DIR [--clip FILE] [--note TEXT]

--shots/--tag: full-screen captures of the M1 named <tag>-<theme id>.png with the window lists
<tag>-<theme id>.txt (runs/m1-desktop-010/theme-tests/m1/gal.sh); --chrome: the window chrome in every
state as the compositor's theme code draws it (crates/wm-theme/examples/forest_preview.rs, host);
--dock: the dock's tiles (chonk-dock's column_preview, host). The wallpapers are the files beside
this script. Nothing here is drawn by this script: it crops, scales and lays out.
"""
import argparse
import html
import os
import re
import shutil

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
THEMES = [
    ('sf-clearing', 'Clearing', 'light', '#2c7a54',
     'The forest at dawn: mist between ridgelines, the first light on moss. The native theme done properly: '
     'discs at the left as on a Mac, a centred title in Inter, a soft green-black shadow; the one to live in.'),
    ('sf-canopy', 'Canopy', 'dark', '#dd8f55',
     'The forest at night, from under the trees: black conifers either side of a gap of sky, copper on the horizon. '
     'The controls are vias (rings that fill under the pointer) and the focused window catches the copper light along its top edge.'),
    ('sf-wafer', 'Wafer', 'light', '#4a5bd0',
     'The clean room: a polished wafer under white light. The controls are chip pads, graphite until the pointer '
     'comes, and the focused window carries one line of thin-film colour, teal to violet, along its top.'),
    ('sf-dieshot', 'Die Shot', 'dark', '#e7c15a',
     'The die under the microscope: metal layers, bond pads, gold wire on black. The theme that takes liberties: '
     'bond-pad controls, a monospaced title at the left like a net label, and a gold trace running from it to a via.'),
]


def rect_of(listing, app):
    for line in listing.splitlines():
        if line.startswith('window') and f'app_id="{app}"' in line and 'mapped=true' in line:
            m = re.search(r'content=Rect \{ pos: Point \{ x: (-?\d+), y: (-?\d+) \}, size: Size \{ w: (\d+), h: (\d+)', line)
            if m:
                return tuple(int(v) for v in m.groups())
    return None


def main():
    ap = argparse.ArgumentParser()
    for name in ('shots', 'tag', 'chrome', 'dock', 'out'):
        ap.add_argument('--' + name, required=True)
    ap.add_argument('--clip')
    ap.add_argument('--note', default='')
    ap.add_argument('--perf', default='')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    sections = []
    for tid, name, mood, accent, idea in THEMES:
        shot = os.path.join(a.shots, f'{a.tag}-{tid}.png')
        full = Image.open(shot).convert('RGB')
        full.save(os.path.join(a.out, f'{tid}-desk.png'), optimize=True)
        full.resize((1280, 800), Image.LANCZOS).save(os.path.join(a.out, f'{tid}-desk-1280.jpg'), quality=90)
        listing = open(os.path.join(a.shots, f'{a.tag}-{tid}.txt'), errors='replace').read()
        crops = []
        for app, label in (('com.apple.Terminal', 'Terminal, focused'), ('com.apple.ActivityMonitor', 'Activity Monitor, without focus')):
            r = rect_of(listing, app)
            if r:
                x, y, w, h = r
                box = (max(0, x - 40), max(0, y - 80), min(full.width, x + 760), min(full.height, y + 110))
                fn = f'{tid}-title-{app.split(".")[-1].lower()}.png'
                full.crop(box).save(os.path.join(a.out, fn), optimize=True)
                crops.append((fn, label))
        # the dock's launcher strip and the instrument column, as they are on the screen
        full.crop((0, 0, 112, 448)).save(os.path.join(a.out, f'{tid}-dock-left.png'))
        full.crop((full.width - 112, 0, full.width, 448)).save(os.path.join(a.out, f'{tid}-dock-right.png'))
        for src, dst in ((os.path.join(a.chrome, f'{tid}-chrome.png'), f'{tid}-chrome.png'), (os.path.join(a.dock, f'{tid}-84.png'), f'{tid}-tiles.png'),
                         (os.path.join(HERE, 'wallpapers', f'{tid}.jpg'), f'{tid}-wallpaper.jpg')):
            shutil.copyfile(src, os.path.join(a.out, dst))
        Image.open(os.path.join(a.out, f'{tid}-wallpaper.jpg')).resize((1280, 800), Image.LANCZOS).save(os.path.join(a.out, f'{tid}-wallpaper-1280.jpg'), quality=90)
        crop_html = ''.join(f'<figure><img src="{fn}" alt="{html.escape(label)}" loading="lazy"><figcaption>{html.escape(label)} (M1 capture, real pixels)</figcaption></figure>' for fn, label in crops)
        sections.append(f'''
<section id="{tid}" class="{mood}" style="--accent:{accent}">
  <header><span class="dot"></span><h2>{name}</h2><code>{tid}</code><span class="mood">{mood}</span></header>
  <p class="idea">{html.escape(idea)}</p>
  <figure class="wide"><a href="{tid}-desk.png"><img src="{tid}-desk-1280.jpg" alt="{name}: the whole desk" loading="lazy"></a>
    <figcaption>The whole desk on the M1 (2560x1600 panel at 150 %): Terminal, Activity Monitor and Firefox, the dock at the left, the instrument column at the right. Click for the capture at full resolution.</figcaption></figure>
  <div class="row">{crop_html}</div>
  <div class="row">
    <figure><img src="{tid}-chrome.png" alt="{name}: the controls in every state" loading="lazy">
      <figcaption>The controls in every state, at 150 %, top to bottom: focused; pointer on a control (all three show their glyphs); close pressed; without focus; without focus, pointer on a control; a title too long for its bar; a window of fixed size (no zoom). Drawn on the workstation by the compositor's own theme code.</figcaption></figure>
    <figure class="narrow"><div class="docks"><img src="{tid}-dock-left.png" alt="{name}: launchers"><img src="{tid}-dock-right.png" alt="{name}: instruments"></div>
      <figcaption>The dock on the M1: launchers (left edge), the mark, CPU/MEM, NET and the clock (right edge).</figcaption>
      <img src="{tid}-tiles.png" alt="{name}: dock tiles" loading="lazy">
      <figcaption>The tiles with both clock faces (workstation render of the dock's code; a click on the clock switches digital and analog).</figcaption></figure>
  </div>
  <figure class="wide"><a href="{tid}-wallpaper.jpg"><img src="{tid}-wallpaper-1280.jpg" alt="{name}: the wallpaper" loading="lazy"></a>
    <figcaption>The wallpaper alone (drawn by <code>integrations/siliconforest/desktop/themes/wallpapers/make_wallpapers.py</code>; click for 2560x1600).</figcaption></figure>
</section>''')
    clip = ''
    if a.clip and os.path.exists(a.clip):
        shutil.copyfile(a.clip, os.path.join(a.out, 'switching.mp4'))
        clip = '<figure class="wide"><video src="switching.mp4" controls muted loop playsinline preload="metadata"></video><figcaption>The four themes changing on the M1, recorded by the desk itself (H.264, the panel\'s full size).</figcaption></figure>'
    nav = ''.join(f'<a href="#{tid}" style="--accent:{accent}"><span class="dot"></span>{name}</a>' for tid, name, _, accent, _ in THEMES)
    page = f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>SiliconForest themes</title>
<style>
  :root {{ --bg:#f6f7f4; --ink:#1c2420; --muted:#5a665f; --line:#d9dfd7; --card:#ffffff; }}
  @media (prefers-color-scheme: dark) {{ :root {{ --bg:#0e1311; --ink:#e7ece6; --muted:#9aa79e; --line:#27322c; --card:#151c19; }} }}
  * {{ box-sizing:border-box; }}
  body {{ margin:0; background:var(--bg); color:var(--ink); font:16px/1.55 -apple-system, "Inter", "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }}
  main {{ max-width:1320px; margin:0 auto; padding:32px 20px 80px; }}
  h1 {{ font-size:34px; line-height:1.15; margin:0 0 6px; letter-spacing:-0.02em; }}
  .lede {{ color:var(--muted); max-width:70ch; margin:0 0 18px; }}
  nav {{ display:flex; flex-wrap:wrap; gap:8px; margin:0 0 8px; position:sticky; top:0; background:var(--bg); padding:10px 0; z-index:2; border-bottom:1px solid var(--line); }}
  nav a {{ display:inline-flex; align-items:center; gap:8px; padding:6px 12px; border:1px solid var(--line); border-radius:999px; color:var(--ink); text-decoration:none; background:var(--card); font-weight:550; font-size:14.5px; }}
  .dot {{ width:11px; height:11px; border-radius:50%; background:var(--accent); display:inline-block; flex:none; }}
  .box {{ border:1px solid var(--line); background:var(--card); border-radius:12px; padding:14px 18px; margin:18px 0; }}
  .box h3 {{ margin:0 0 6px; font-size:16px; }} .box p, .box li {{ color:var(--muted); font-size:14.5px; margin:4px 0; }} .box ul {{ margin:4px 0; padding-left:20px; }}
  code {{ font:13px ui-monospace, "Source Code Pro", Menlo, monospace; background:color-mix(in srgb, var(--ink) 8%, transparent); padding:1px 6px; border-radius:5px; }}
  section {{ margin:44px 0 0; padding-top:26px; border-top:1px solid var(--line); scroll-margin-top:64px; }}
  section header {{ display:flex; align-items:center; gap:12px; flex-wrap:wrap; }}
  section h2 {{ margin:0; font-size:28px; letter-spacing:-0.01em; }}
  .mood {{ font-size:12.5px; text-transform:uppercase; letter-spacing:.08em; color:var(--muted); border:1px solid var(--line); border-radius:999px; padding:1px 9px; }}
  .idea {{ max-width:78ch; margin:8px 0 16px; font-size:17px; }}
  figure {{ margin:0 0 18px; min-width:0; }}
  figure img, video {{ max-width:100%; height:auto; display:block; border-radius:8px; border:1px solid var(--line); }}
  figure.wide img, video {{ width:100%; }}
  figcaption {{ color:var(--muted); font-size:13.5px; margin:6px 2px 10px; max-width:90ch; }}
  .row {{ display:flex; gap:18px; flex-wrap:wrap; align-items:flex-start; }}
  .row > figure {{ flex:1 1 420px; }} .row > figure.narrow {{ flex:0 1 430px; }}
  .docks {{ display:flex; gap:14px; }} .docks img {{ height:448px; width:auto; max-width:48%; object-fit:contain; }}
  table {{ border-collapse:collapse; font-size:14px; width:100%; }} th, td {{ text-align:left; padding:5px 10px 5px 0; border-bottom:1px solid var(--line); }} th {{ color:var(--muted); font-weight:550; }}
  .scroll {{ overflow-x:auto; }}
</style></head><body><main>
<h1>SiliconForest themes</h1>
<p class="lede">Four themes for the SiliconForest desk: two light, two dark, one family. Close, minimise and zoom are at the top left in a Mac's order in all four; what changes is the shape of things. Everything below was drawn by the desk's own code; what is an M1 capture and what was rendered on the workstation is said under each picture.</p>
<nav>{nav}</nav>
<div class="box"><h3>Trying them</h3>
<ul><li>At the M1: Command-Space, <b>Theme</b>, then SiliconForest Clearing / Canopy / Wafer / Die Shot (the last four rows). The change cross-fades.</li>
<li>From the workstation: <code>integrations/siliconforest/desktop/tools/desk_m1.sh theme sf-canopy</code> (ids: <code>sf-clearing</code> <code>sf-canopy</code> <code>sf-wafer</code> <code>sf-dieshot</code>).</li>
<li>Back to the look of before: <code>desk_m1.sh theme washi</code> then <code>desk_m1.sh wallpaper host:siliconforest</code>.</li></ul>
{('<p>' + a.note + '</p>') if a.note else ''}</div>
{a.perf}
{clip}
{''.join(sections)}
</main></body></html>'''
    with open(os.path.join(a.out, 'index.html'), 'w') as f:
        f.write(page)
    print(os.path.join(a.out, 'index.html'))


if __name__ == '__main__':
    main()
