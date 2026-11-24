#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Frameworks the surveyed bottles link, against what the SiliconForest tree has for each.

For every /System/Library/Frameworks/<F> a bottle names and the system root of a survey
lacks (or has with symbols missing), this reports:
  in tree      a source directory for F exists (src/Frameworks/F, or src/Libraries/F)
  arm64 build  a flake package for F with an arm64 build exists (by package name)
  wanted       distinct symbols the bottles import from F
  named        how many of those identifiers occur as a whole word in F's source directory

"named" is a LEXICAL count over source text. It is not a build, not an export list and not
evidence that the symbol works: an identifier can occur in a header or a comment only. It
separates "the tree has started on this" from "the tree has nothing"; the real check is
building the framework for arm64 and running hb_survey.py against a root that contains it.

  hb_frameworks.py --survey p1   -> runs/homebrew/survey/<survey>-frameworks.md
"""
import argparse
import collections
import json
from pathlib import Path
import re

LAB = Path(__file__).resolve().parents[3]
RUNS = LAB / 'runs/homebrew'
TREE = LAB / 'sources/puredarwin-baseline'
SUFFIXES = ('.c', '.m', '.mm', '.h', '.cpp', '.cc', '.swift', '.exports', '.def', '.s')
# flake package names with an arm64 build, by framework (flake attribute names at e7aa00a6)
ARM64 = {'AppKit': 'appkit-msm-arm64', 'Cocoa': 'cocoa-msm-arm64', 'CoreGraphics': 'coregraphics-arm64', 'CoreText': 'coretext-arm64',
         'QuartzCore': 'quartzcore-msm-arm64', 'OpenGL': 'openglFramework-arm64', 'Foundation': 'foundation-arm64',
         'CoreFoundation': 'coreFoundation-arm64', 'IOKit': 'iokit-arm64', 'CoreServices': 'coreServices-arm64',
         'Security': 'security-arm64', 'SystemConfiguration': 'systemConfiguration-arm64', 'DiskArbitration': 'diskArbitration-arm64'}
X86_ONLY = {'Carbon': 'carbon', 'AudioToolbox': 'audiotoolbox', 'CoreVideo': 'corevideo', 'ApplicationServices': 'applicationservices',
            'CoreData': 'coredata', 'IOSurface': 'iosurface'}
APP_FRAMEWORKS = {'CoreMedia', 'UniformTypeIdentifiers', 'ImageIO', 'UserNotifications', 'ScreenCaptureKit', 'GameKit', 'ModelIO', 'VideoToolbox',
                  'AVFoundation', 'MediaPlayer', 'CoreMIDI', 'AuthenticationServices', 'LocalAuthentication', 'CoreLocation', 'Vision',
                  'ServiceManagement', 'IOBluetooth', 'Accessibility', 'AddressBook', 'CFNetwork', 'GSS', 'ColorSync', 'WebKit', 'GameController',
                  'ForceFeedback', 'CoreHaptics'}


def source_dir(name):
    for d in (TREE / 'src/Frameworks' / name, TREE / 'src/Libraries' / name):
        if d.is_dir():
            return d
    return None


def identifiers(directory):
    words = set()
    for p in directory.rglob('*'):
        if p.suffix in SUFFIXES and p.is_file():
            try:
                words |= set(re.findall(r'[A-Za-z_][A-Za-z0-9_]*', p.read_text(errors='replace')))
            except OSError:
                pass
    return words


def plain(sym):
    for prefix in ('_OBJC_CLASS_$_', '_OBJC_METACLASS_$_', '_OBJC_EHTYPE_$_'):
        if sym.startswith(prefix):
            return sym[len(prefix):]
    return sym[1:] if sym.startswith('_') else sym


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--survey', required=True)
    a = p.parse_args()
    formulae = json.loads((RUNS / 'survey' / (a.survey + '.json')).read_text())
    wanted = collections.defaultdict(set)
    absent = set()
    mains = collections.defaultdict(set)
    for name, f in formulae.items():
        for lib, syms in f['missing_libs'].items():
            if '.framework/' in lib:
                fw = lib.split('.framework/')[0].rsplit('/', 1)[1]
                wanted[fw] |= set(syms)
                absent.add(fw)
        for lib, syms in f['missing_syms'].items():
            if '.framework/' in lib:
                wanted[lib.split('.framework/')[0].rsplit('/', 1)[1]] |= set(syms)
        if f.get('requested'):
            for prog, v in f.get('programs', {}).items():
                for lib in v.get('missing_libs', []):
                    if '.framework/' in lib:
                        mains[lib.split('.framework/')[0].rsplit('/', 1)[1]].add(name)
                for s in v.get('missing_syms', []):
                    if '.framework:' in s:
                        mains[s.split('.framework:')[0]].add(name)
    lines = ['# Frameworks the bottles link, against the SiliconForest tree (survey `%s`)' % a.survey, '',
             '"Named in source" is a lexical count of identifiers in the tree\'s source directory for the framework: not a build, not an export list, not proof of function.', '',
             '| Framework | In the system root surveyed | Tree source | Flake package | Symbols wanted | Named in source (lexical) | Requested formulae with a program that needs it |',
             '|---|---|---|---|---|---|---|']
    for fw in sorted(wanted, key=lambda k: (-len(mains[k]), k)):
        d = source_dir(fw)
        if fw in ARM64:
            pkg = '`%s` (arm64)' % ARM64[fw]
        elif fw in X86_ONLY:
            pkg = '`%s` (x86_64 only)' % X86_ONLY[fw]
        elif fw in APP_FRAMEWORKS:
            pkg = '`app-frameworks` (x86_64 only)'
        else:
            pkg = 'none'
        if d is not None:
            words = identifiers(d)
            named = sum(1 for s in wanted[fw] if plain(s) in words)
            src = '`%s`' % d.relative_to(TREE)
            named_text = '%d of %d' % (named, len(wanted[fw]))
        else:
            src, named_text = 'none', '-'
        lines.append('| %s | %s | %s | %s | %d | %s | %s |' % (fw, 'absent' if fw in absent else 'present, symbols missing', src, pkg, len(wanted[fw]),
                                                             named_text, ' '.join(sorted(mains[fw])) or '(modules only)'))
    out = RUNS / 'survey' / (a.survey + '-frameworks.md')
    out.write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
