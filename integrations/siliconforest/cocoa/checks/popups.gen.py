#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Writes checks/popups.json: every popup of the CocoaPopups application, driven by pointer and
keyboard. The step list is long and regular, so it is generated: run this file after changing it.

Places. The application's marked view is found on the screen (`view`); the controls are clicked
at their place relative to its top left corner, in points. A menu is clicked at rows worked out
from where the compositor says it mapped the popup (`WMPOPUP mapped at=X,Y size=WxH`, pixels):
its window has a 16 point shadow margin and a 5 point inset, rows are 22 points, a separator 11.
"""
import json
from pathlib import Path

MAPPED = r'WMPOPUP mapped at=(\d+),(\d+) size=(\d+)x(\d+)'
steps = []
add = steps.append


def click(x, y, **more):
    add(dict({'click_in': 'view', 'points': [x, y]}, **more))


def expect(pattern, name, timeout=60, **more):
    add(dict({'expect': pattern, 'timeout': timeout, 'name': name}, **more))


def row(menu, offset, x=40):
    """Pixel expressions for a point of a menu: x points right of its panel's left edge, offset points below the top of its first row."""
    return ['%s[0] + (16 + %g) * S' % (menu, x), '%s[1] + (16 + 5 + %g) * S' % (menu, offset)]


def opened(name, save):
    expect(MAPPED, name, save=save)
    add({'sleep': 1.5})


def closed(name):
    expect('POPT menu end', name)
    add({'sleep': 1.5})


def keys(*names):
    for n in names:
        add({'key': [n]})
        add({'sleep': 0.4})


def actions(n, name):
    add({'count': r'POPT \w+ action=', 'equals': n, 'name': name})


POPUP, PULLDOWN, COMBO, HOVER = (55, -58), (175, -58), (107, -23), (180, -24)

add({'wait': 'COCOA_APP_START 1', 'timeout': 400})
expect('POPT window-shown', 'window shown', 600)
add({'sleep': 6})
add({'locate': 'view', 'rgb': [51, 115, 204], 'tolerance': 14, 'points_wide': 280, 'scale': '@SCALE@', 'name': 'backing scale on screen'})

# pop-up button, Alpha selected: press, hold, drag two rows down, release: Gamma
add({'drag_in': 'view', 'from': list(POPUP), 'to': [POPUP[0], POPUP[1] + 44], 'hold': 1.2, 'settle': 1.5})
expect(r'WindowServer: popup \d+ at -?\d+,-?\d+ of window \d+, \d+x\d+ \(grab\)', 'the list is an xdg_popup with a grab')
expect('POPT popup action=1 selected=Gamma index=2', 'press, drag, release: Gamma chosen')
add({'sleep': 2.5})
# click to open (the selected item lies over the button), click an item; rows from where the popup is
click(*POPUP)
opened('the compositor shows the list', 'p')
add({'shot': '01-popup'})
add({'click_px': row('p', 22 + 11)})
expect('POPT popup action=2 selected=Beta index=1', 'click, click: Beta chosen')
add({'sleep': 2.5})
click(*POPUP)
opened('the list opens again', 'p')
add({'click_px': row('p', 11)})
expect('POPT popup action=3 selected=Alpha index=0', 'second opening: Alpha chosen')
add({'sleep': 2.5})
# a disabled item cannot be chosen; Escape dismisses and sends nothing
click(*POPUP)
opened('the list opens (disabled item)', 'p')
add({'click_px': row('p', 22 * 3 + 11)})
add({'sleep': 2})
actions(3, 'a click on a disabled item chooses nothing')
keys('esc')
closed('Escape dismisses')
actions(3, 'Escape sends no action')
# a press on the application's own window, outside the menu: dismissed, and the press is not passed on
click(*POPUP)
opened('the list opens (press on own window)', 'p')
click(200, 80)
closed('a press on the own window dismisses it')
add({'sleep': 1})
add({'count': 'POPT view-click', 'equals': 0, 'name': 'that press did not reach the view'})
actions(3, 'dismissal by the own window sends no action')
# keyboard. Keys that say the same when the emulated guest is slow enough to make one repeat: Down
# to the end (menus do not wrap; the disabled item is passed over), Up to the start, letters, Return
click(*POPUP)
opened('the list opens (keyboard)', 'p')
keys('down', 'down', 'down', 'down', 'down', 'down', 'ret')
expect('POPT popup action=4 selected=Epsilon index=4', 'Down to the end, over the disabled item, Return: Epsilon')
add({'sleep': 2.5})
click(*POPUP)
opened('the list opens (type-select)', 'p')
add({'type': 'g'})
add({'sleep': 1})
add({'shot': '02-popup-typed-g'})
keys('ret')
expect('POPT popup action=5 selected=Gamma index=2', 'g, Return: Gamma chosen')
add({'sleep': 2.5})
click(*POPUP)
opened('the list opens (Up)', 'p')
keys('up', 'up', 'up', 'up', 'up', 'spc')
expect('POPT popup action=6 selected=Alpha index=0', 'Up to the start, Space: Alpha chosen')
add({'sleep': 2.5})

# pull-down button: the list is under the button, without the title item
click(*PULLDOWN)
opened('pull-down list', 'd')
add({'shot': '03-pulldown'})
add({'click_px': row('d', 22 + 11)})
expect('POPT pulldown action=7 item=Duplicate title=Actions', 'pull-down: Duplicate chosen, the title stays')
add({'sleep': 2.5})

# combo box: the list under the field
click(*COMBO)
opened('combo box list', 'c')
add({'shot': '04-combo'})
add({'click_px': row('c', 22 * 2 + 11)})
expect('POPT combo action=8 value=Blue', 'combo box: Blue chosen')
add({'sleep': 2.5})
click(*COMBO)
opened('combo box list (keyboard)', 'c')
# (Space, not Return: a Return that repeats in a slow guest reaches the field after the list has closed and ends the edit)
keys('down', 'down', 'down', 'down', 'spc')
expect('POPT combo action=9 value=Violet', 'combo box: Down to the end, Space: Violet')
add({'sleep': 2.5})

# context menu: right click; rows Cut, Copy, Paste (disabled), separator, Wrap Lines (checked), More >, Select All
click(40, 30, button='right')
opened('context menu', 'm')
add({'shot': '05-context'})
add({'click_px': row('m', 22 + 11)})
expect('POPT context action=10 item=Copy', 'context menu: Copy chosen')
add({'sleep': 2.5})
# submenus with the pointer: More > Deeper > Second Leaf
click(40, 30, button='right')
opened('context menu (submenus)', 'm')
add({'move_px': row('m', 22 * 3 + 11 + 22 + 11)})
expect(r'WindowServer: popup \d+ at -?\d+,-?\d+ of window \d+, \d+x\d+ \(grab\)', 'the submenu is an xdg_popup')
expect(r'WindowServer: popup \d+ placed at (-?\d+),(-?\d+) in its parent, \d+x\d+, moved by (-?\d+),(-?\d+)', 'the compositor answers where it put the submenu')
opened('submenu shown', 'u')
add({'all': MAPPED, 'assert': 'g[0] >= 0 and g[1] >= 0 and g[0] + g[2] <= W and g[1] + g[3] <= H', 'name': 'every popup so far lies on the screen'})
# beside its item: on the right when it fits on the screen there, else flipped to the left
add({'check': '(m[0] + m[2] - 34 * S + u[2] <= W) == (u[0] > m[0])', 'name': 'the submenu is on the side that has room'})
add({'check': 'abs((u[0] + 16 * S) - (m[0] + m[2] - 18 * S)) <= 2 * S if u[0] > m[0] else abs((u[0] + u[2] - 16 * S) - (m[0] + 18 * S)) <= 2 * S',
     'name': 'its panel overlaps the menu\'s by two points'})
add({'move_px': row('u', 11)})
add({'sleep': 1})
add({'move_px': row('u', 22 * 2 + 11)})
opened('second submenu shown', 'v')
add({'shot': '06-submenus'})
add({'move_px': row('v', 11)})
add({'sleep': 0.8})
add({'click_px': row('v', 22 + 11)})
expect('POPT context action=11 item=Second Leaf', 'More > Deeper > Second Leaf chosen with the pointer')
add({'sleep': 2.5})
# the same with the keyboard: m = More; Right; d = Deeper; Right; Left; Right; Return = First Leaf
click(40, 30, button='right')
opened('context menu (keyboard)', 'm')
add({'type': 'm'})
keys('right')
opened('submenu by m, Right', 'u')
add({'type': 'd'})
keys('right')
opened('second submenu by Right', 'v')
keys('left')
expect('WMPOPUP gone remaining=2', 'Left closes the second submenu only')
keys('right')
opened('second submenu again', 'v')
keys('ret')
expect('POPT context action=12 item=First Leaf', 'keyboard through two submenus: First Leaf chosen')
add({'sleep': 2.5})
# Escape in a submenu closes everything
click(40, 30, button='right')
opened('context menu (escape)', 'm')
add({'type': 'm'})
keys('right')
opened('submenu by m, Right (escape)', 'u')
keys('esc')
closed('tracking ended (escape)')
add({'check': "n('WMPOPUP new at=') == n('WMPOPUP gone remaining=')", 'name': 'Escape closed the submenu and the menu'})
actions(12, 'Escape in a submenu sends no action')

# tool tip: a popup without a grab, gone when the pointer leaves
add({'move_in': 'view', 'points': list(HOVER)})
add({'sleep': 0.5})
add({'move_in': 'view', 'points': [HOVER[0] + 2, HOVER[1] + 1]})
expect(r'WindowServer: popup \d+ at -?\d+,-?\d+ of window \d+, [\d.]+x[\d.]+\r', 'the tool tip is an xdg_popup without a grab', 30)
opened('tool tip shown', 't')
add({'shot': '07-tooltip'})
add({'move_in': 'view', 'points': [200, 80]})
expect('WMPOPUP gone remaining=0', 'the tool tip goes when the pointer leaves', 30)

# afterwards the window takes clicks as before
add({'sleep': 1})
click(200, 80)
expect('POPT view-click clicks=1', 'the window works after all that')
# last, because it raises the terminal, which may then cover this window: a press outside the application
# (on the terminal's window, top left at every scale): the compositor ends the grab; no action
click(*POPUP)
opened('the list opens (press outside)', 'p')
add({'click': [60, 60]})
expect('WMPOPUP dismissed by a press outside', 'a press on another application\'s window dismisses it')
closed('tracking ended (outside)')
actions(12, 'dismissal from outside sends no action')
add({'all': MAPPED, 'assert': 'g[0] >= 0 and g[1] >= 0 and g[0] + g[2] <= W and g[1] + g[3] <= H', 'name': 'every popup lay on the screen'})
add({'check': "n('POPT menu begin') == n('POPT menu end') and n('POPT menu begin') >= 16", 'name': 'every menu that began tracking ended it'})
add({'check': "n('WMPOPUP new at=') == n('WMPOPUP gone remaining=')", 'name': 'every popup the compositor was given is gone'})
add({'absent': r'Segmentation|Bus error|Abort trap|protocol error|wrong configure|COCOA_APP_EXIT 1', 'name': 'no crash, no protocol error'})

check = {'apps': ['/Applications/CocoaPopups.app/Contents/MacOS/CocoaPopups'], 'env': 'SF_WS_TRACE=1 SF_POPUP_TRACE=1',
         'seconds': 560, 'steps': steps}
text = json.dumps(check, indent=1).replace('"@SCALE@"', '@SCALE@')
Path(__file__).with_name('popups.json').write_text(text + '\n')
print('popups.json: %d steps' % len(steps))
