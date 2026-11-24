#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The SiliconForest themes' palettes and their WCAG 2 contrast ratios (the table of
docs/design/siliconforest-themes.md). The values are those of the compositor's
crates/wm-theme/src/forest.rs (compositor patch 36), whose test
`forest::tests::text_and_controls_keep_their_contrast` holds the same thresholds: text 4.5:1 or
better on its bar and on the menus' panel, controls 3:1 or better on the title bar."""

THEMES = {
    'sf-clearing': dict(title='#e9ede5', inactive='#f1f3ee', panel='#fbfcf9', text='#1f2a24', muted='#56625a', line='#b9c3b8',
                        accent='#2c7a54', accent_text='#ffffff', selection='#cfe6d8', close='#cf4a3a', minimise='#a87004', zoom='#2f8a52', idle='#7c877d'),
    'sf-canopy': dict(title='#17221d', inactive='#121a16', panel='#111a16', text='#e6ede4', muted='#9aa89d', line='#2e3d35',
                      accent='#dd8f55', accent_text='#1a120b', selection='#2b3d33', close='#ec7c60', minimise='#e2b65c', zoom='#82c98f', idle='#5d6e64'),
    'sf-wafer': dict(title='#f1f3f7', inactive='#f6f7f9', panel='#ffffff', text='#1c2330', muted='#566074', line='#c3cad6',
                     accent='#4a5bd0', accent_text='#ffffff', selection='#dde2fa', close='#d2475a', minimise='#a87400', zoom='#1f8a5b', idle='#737c8c'),
    'sf-dieshot': dict(title='#12161c', inactive='#0e1116', panel='#0e1116', text='#e9eef2', muted='#93a0ad', line='#2c3540',
                       accent='#e7c15a', accent_text='#14110a', selection='#26303a', close='#ff7a6b', minimise='#e7c15a', zoom='#58d6a0', idle='#5b6876'),
}
# (foreground, background, the least ratio)
PAIRS = [('text', 'title', 4.5), ('muted', 'inactive', 4.5), ('text', 'panel', 4.5), ('muted', 'panel', 4.5), ('accent_text', 'accent', 4.5),
         ('text', 'selection', 4.5), ('accent', 'panel', 3.0), ('close', 'title', 3.0), ('minimise', 'title', 3.0), ('zoom', 'title', 3.0), ('idle', 'inactive', 3.0)]


def luminance(colour):
    channels = [int(colour[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    linear = [c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4 for c in channels]
    return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]


def ratio(a, b):
    a, b = luminance(a) + 0.05, luminance(b) + 0.05
    return max(a, b) / min(a, b)


def main():
    failed = 0
    for name, palette in THEMES.items():
        print(name)
        for fg, bg, least in PAIRS:
            r = ratio(palette[fg], palette[bg])
            ok = r >= least
            failed += not ok
            print(f"  {fg:12s} {palette[fg]} on {bg:9s} {palette[bg]}  {r:5.2f}  (>= {least}) {'ok' if ok else 'FAILS'}")
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()
