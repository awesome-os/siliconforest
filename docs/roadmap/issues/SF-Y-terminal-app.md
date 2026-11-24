# SF-Y: Apple's Terminal.app on SiliconForest (GitHub issue 52)

## Goal
The unmodified Terminal.app binary from the M1's own macOS launches and works on the SiliconForest
desktop: the demonstration that macOS applications run here. The binary is never committed; every
step that makes it run is part of SiliconForest. Phases: 1 measure (done by the coordinator),
2 load (dyld reaches `main`), 3 launch (a window on the compositor), 4 a working shell in it.

Test copy: `private/terminal-app/m5-26.5.2/Terminal.app` (macOS 26.5.2, from the controller Mac;
git-ignored). The final binary comes from the M1's macOS 27.0: nothing below may depend on one build.

## State (14 November 2026)
**On the M1, booted from its disk: the unmodified Terminal.app (the macOS 26.5.2 copy, 129 files
hash-equal to the copy here, at `/Applications/Utilities/Terminal.app`) is started from the desktop
menu (Applications > Terminal) or with `open -a Terminal`. It shows its menu bar as the first row
inside the window's frame, opens windows with its twelve built-in profiles (the default is Clear
Dark, as on macOS 26), runs root's zsh through `/usr/bin/login`, and ends when its last window is
closed.** Everything below was seen in captures of the screen as the compositor rendered it
(`runs/sf-terminal/shots/`, not committed). Later on 14 November: output on the screen about 80 ms
after it reaches the pty, launch about 1 s, Command-Q quits, tabs (one toplevel per tab group), bold
and italic faces (table below). Input in every test is synthetic (the compositor's
inject module, a test instrument: `terminal-app/compositor/README.md`). Nobody looked at the panel
and no physical keyboard or trackpad was used.

**Fourth round (night of 14 November): the section "Fourth round" below is the current state;
where it and the rows here differ, it is right.**

**14 November, third round (the rows below it are the earlier rounds').** In Terminal on the M1, with synthetic input: rows a program did not redraw no longer
stay grey (tmux, a click in vim: `fin-tmux-vim`); tmux finds `tmux-256color` (the whole ncurses
database is in the root); 24-bit colour (`COLORTERM=truecolor`, `tc3`); its questions are macOS
alerts with Terminal's icon, centred on their window, with Return and Escape (`al10`, `al11`);
Command-F find bar with Find Next and Previous (`fd1`, `fd7`..`fd9`); Preferences lists the twelve
profiles, its labels fit, its tab view has the macOS 26 look and a changed setting is in the
preferences file after a relaunch (`pf8`, `ps4`); Chinese, Japanese, Korean and emoji
(monochrome) draw at the right widths (`wc1`); `open x.command` runs the file in Terminal (`op1`);
the Window menu shows Shift-Command-[ (`wm2`); split pane (`sp1`). Measurements below ("Performance,
third round"). The desktop's own terminal no longer leaves a white block where it was clicked
(`ct-before`, `ct-after`).

| What | State | Evidence (`runs/sf-terminal/shots/`) |
|---|---|---|
| Menu bar | done: every titled window that is not a panel carries the main menu as a strip (AppKit 0028, one mechanism for all Mac applications). Pointer: press a title, move along the bar, choose. Keyboard: Control-F2, arrows, Return, Escape | `mb`, `mc2`..`mc4` (pointer), `md1`..`md6` (keyboard; Shell > New Window chosen by Return opened a window), `qt1`, `qt2` (Terminal > Quit Terminal ended the program and both shells) |
| Windows | Command-N, Command-W, closing the last window (the program ends, no shell is left), Terminal > Quit. Not right: Command-Q (below), Command-T | `cw1`..`cw3`, `pr6` |
| Profiles | the twelve built-in profiles are imported at first launch; Shell > New Window > <profile> opens a window with that look (Ocean: blue); the default window is Clear Dark, 120x30 | `pr4`, `pr5`, `pr6` |
| Preferences | Command-comma; the toolbar shows General, Profiles, Window Groups, Encodings and switches panes; the pop-ups and checkboxes show the stored values (bindings work since key-value coding finds AppKit's `_key` accessors); the window grows for its toolbar; titles that were cut are drawn up to an eighth narrower instead. Open: below | `ph2` (General), `ph3` (Profiles: font, colour wells, cursor) |
| Desktop | the desktop menu has Applications (one row per bundle in `/Applications`, `/Applications/Utilities`) | `ap1`..`ap3` |
| Shell | `login -pf root` (ours) starts root's shell as a login shell; root's shell is zsh (`/etc/passwd`, `/etc/master.passwd`); `TERM=xterm-256color`, `TERM_PROGRAM=Apple_Terminal` | `pr4`, `th1` |
| In the terminal | 256 colours, italic, underline, reverse, dim; htop full screen; text selection, paste, scrollback, resize as before | `tc2`, `th2` |

### What was wrong with text and keys (all in the system, none in Terminal)
| Symptom | Cause | Fix |
|---|---|---|
| White window, cursor cut off at the top | Terminal sizes its window from a key-value observation of `activePane.view.contentSize`. Foundation's KVO knew neither key paths nor setters called directly, so the window kept the nib's 524x327 while the view laid out 24 rows of 15 points: rows 0 and 1 (where the text was) lay above the top edge | Foundation: KVO with key paths, dependent key paths and setters that notify (the class's setter wrapped by a thunk; no per-instance subclass) |
| A cell 12 points wide | Terminal asks for the font "SF Mono Terminal" (its own, in its bundle); CoreText resolved a name no installed face carries through fontconfig's default: DejaVu Sans, proportional. A terminal measures its cell from the advance of "W" | CoreText (0023): Menlo, Monaco, SF Mono and any family whose name says fixed-pitch resolve to an installed fixed-pitch face; AppKit (0024) asks CoreText for unknown names; AppKit (0021) registers the fonts an application carries (`ATSApplicationFontsPath`), so Terminal draws with its own face from its own bundle |
| The program ended in the bindings of the preferences window once notifications arrived | a selection index beyond the content asserted in CoreFoundation; a binding to a key a control lacks raised through a dispatch callout | AppKit (0024): such a selection selects nothing; a failing binding is reported once |
| Title "80\U00D724" | the title's parts come from `Localizable.loctable` (one property list for all localizations); only `.strings` files were read | Foundation: NSBundle reads localization tables |
| Scrolling back raised | `-[NSView scrollRect:by:]` ends in `-copyBitsInRect:toPoint:gState:`, defined only for Onyx2D's abstract context | Onyx2D (0026) |
| Command-V pasted nothing | Terminal asks the pasteboard for `_sourceSigningIdentifier` (private) before it pastes | AppKit (0020): answers nil (nothing records who wrote a pasteboard) |
| Keys | nothing: `-interpretKeyEvents:` already reached `-insertText:replacementRange:` and the command selectors | - |

### What stands between this and "perfectly working" (in the order a person meets it)
| Item | What is known | Estimate |
|---|---|---|
| Launch | done for what was cheap: exec to the window with its shell 0.97 to 1.06 s (was 3.4 s, measured the same way: `tools/sf-latency.py launch`; the 5 s of before was another measure). Causes found with the profiler (`probe/sf-prof.c`): the five drawing frameworks compiled without `-O`; the application's fonts scanned three times at each launch (now their directory, cached by fontconfig; the first launch after an install is 2.4 s); a fontconfig match, a file read and a FreeType face for every NSFont made by name; a font made of every installed face to find one character; the menu archived for a menu bar that does not exist. Left: nib decoding (about 0.3 s), the text system for the menu strip | - |
| Output, typing, menus | done: 40 lines written to the pty are committed 66 to 72 ms later and on the screen (the compositor's release of the frame before) at about 80 ms; 33 ms of that is Terminal's own timer (it draws at most 30 times a second), drawing a screenful about 20 ms. Typed Return to output in a capture of the screen: 100 to 150 ms (zsh starting `ls` included; before: more than 300 ms, less than 1 s). Typing echo, key to commit: 15 ms (was 39). A menu, click to its popup: about 150 ms (was about 820 ms by this measure). Scrolling with the wheel: a frame every 50 ms. Maximize (the frame's button): the first frame at the new size (2556x1544 pixels) 218 ms after the click, in a second try 137 ms, of which the main thread drew about 70 ms (profile: no single cause; the menu strip about 18 ms) The 1.9 to 3.4 s of before were partly the capture instrument (each capture holds the compositor about 0.5 s) and a WindowServer that dropped a flush when every frame was busy | - |
| Command-Q | done: the compositor passes Command-Q to a Mac application (patch 5); Terminal's own Quit then needed `login` to stay the shell's parent and `+[NSCountedSet setWithCapacity:]`. Two idle windows: Command-Q ends the program and both shells; with a process running: the alert "Do you want to terminate running processes", Return ends everything (`cq4`). Third round: the alert is laid out as on macOS 26 (AppKit 0044: NSBeginAlertSheet and NSRunAlertPanel are NSAlert; 420 points, Terminal's icon, wrapped text, buttons sized to their titles, Return, Escape), has no title bar (0041) and is centred on its window by the compositor (WindowServer 0043, `xdg_toplevel.set_parent`): `al10`..`al13` | - |
| Tabs | done (AppKit 0039, WindowServer 0036): a tab group is one toplevel; Command-T, Command-Shift-[ and ], a click on a tab, + (Terminal's `newWindowForTab:`), a tab's x, Command-W, Window > Move Tab to New Window, Merge All Windows (`tb1`, `tb6`, `tb7`, `tc3`..`tc5`, `mv1`, `mg1`, `mg2`). Third round: the Window menu shows Shift-Command-[ and ] (AppKit 0046, `wm2`). Not done: dragging tabs to reorder or out of the window, Show All Tabs. The bar's light look in a dark profile: Terminal sets no appearance on its windows (it asks only `bestMatchFromAppearancesWithNames:` while drawing), so the bar follows the system appearance, light here; whether macOS draws it dark in light mode was not checked | dragging: 1 day |
| Preferences, rest | third round: the profile list shows the twelve profiles (every table column width and row height of a macOS nib was read as 0: AppKit 0046, the NIBArchive reader's `-decodeFloatForKey:` returned a double where NSCoder declares a float; then the list drew into an image's cache window: 0041), labels fit (the same float fix; 0046 narrows by up to an eighth what still clips), the tab view has the macOS 26 look (0050), a checkbox changes the profile and the change is in `com.apple.Terminal.plist` after a relaunch (0047: a control's bound value goes to the model when it sends its action; the selection proxy sets each selected object): `pf8`, `ps3`, `ps4`. "Antialias text" is disabled as on macOS (bound to `fontAllowsDisableAntialias`). Open: the profile thumbnails are grey frames (Terminal draws a TTView into an image; only Red Sands shows its top row: cause not established); toolbar icons blank (symbol images); bindings `enabled2` and `NSMenuItem.value` not looked at; Window Groups and Encodings panes not looked at; colour wells not tried (`NSColorPanel.nib` is missing) | thumbnails 1 day; the rest 3 days |
| True colour | done (third round): libSystem reads feature flags (`libsystem-default/patches/feature-flags.patch`, Finch's reader) and the root has `/System/Library/FeatureFlags/Domain/Terminal.plist` (DirectColor FeatureComplete, macOS 26.5's value). Terminal asks no other flag (one call of `_os_feature_enabled_impl`, in `-[TTShell init...]`). Rehearsed in QEMU (`ty7`, `ty7-2`), installed by the guarded installer; `COLORTERM=truecolor` and 64-step gradients without any `DYLD_*` (`tc3`) | - |
| Bold, italic | done (`at1` before, `at3` after): bold was drawn in the regular face (`-convertFont:toHaveTrait:` took the first face with the trait, the variable font's Heavy instance, and CoreText drew a named instance from the file's default instance), italic in Light Italic. Strike-through (SGR 9), double underline (21) and overline (53) are not drawn: Terminal keeps no such cell attribute, as on macOS. Underline, dim, reverse, 256 colours: drawn. Cursor shapes (DECSCUSR block, underline, bar) and blinking: drawn (`cu1`..`cu5`) | - |
| Wide characters | done (third round): `build-fonts-extra.nix` lays Noto Sans Mono CJK and Noto Emoji (monochrome; OFL 1.1) into `/System/Library/Fonts`; CoreText's fallback finds them. Chinese, Japanese, Korean at double width, emoji, box drawing and block elements, combining marks, powerline glyphs; no ligatures (`wc1`). Colour emoji: not drawn (Noto Color Emoji is CBDT bitmaps; the drawing path draws outlines) | colour emoji: 2 days |
| First key after a click | not reproduced: six times a click into the window followed by typing at once, every character arrived (synthetic input) | - |
| Symbol images, `Assets.car` | `Could not find image named 'gearshape'` and others: SF Symbols and the CoreUI catalogue are not read; AppKit's `NSColorPanel.nib` is missing. Not worked on in the third round. The application icon is read now (its `.icns`: Onyx2D 0042, PNG entries) | 1 week for a `.car` reader (Finch's CoreUI is the reference), symbols need an open symbol font |
| Scroller look | Terminal drives its scroller through `NSScrollerImpPair` (private), which answers nothing; the knob is drawn by AppKit's own scroller, in the Cocotron look | 3 days |
| Bell | `NSBeep()` and `AudioServices*` make no sound (Terminal logs "Error ... getting system alert sound" at each bell); Terminal's own visual bell was not tried. Not worked on in the third round | 2 days over the sound stream's server |
| Root's home in new windows | none: `login` starts in the home directory | - |
| Resize handle is 2 pixels | the compositor's frame has a 2-pixel border as its resize zone | compositor |
| Full-screen programs | done (third round). vim, nvim, htop, less, tmux and `claude` (its first-run TUI) draw their screens (`fv1`..`fv3`, `cl2`, `fin-tmux-vim`); mouse reporting works. The grey rows: Onyx2D clips to one rectangle, so a list of dirty rects became their bounding box, which the window frame filled with its grey while Terminal filled only the rects `-getRectsBeingDrawn:` listed. AppKit 0040: that method answers the rect drawn; dirty rects far apart are drawn one at a time; dirty rects are whole device pixels (at 150% whole points left one-pixel seams). AppKit 0041: windows start opaque, so Terminal's `-setOpaque:NO` for its translucent profiles takes effect (the window is now clear behind the text, as on macOS: windows behind show through faintly, unblurred). tmux: the whole ncurses database (`build-terminfo.nix`). Command-F: done (row "Find") | - |
| Find | done (third round, AppKit 0048, Finch's NSTextFinder): Command-F shows the bar, Return finds ("6 found"), Command-G and Command-Shift-G next and previous, Escape closes it (`fd7`..`fd9`). Use Selection for Find (Command-E) set a one-character search string after a synthetic double-click (`fd10`): not right, cause not established | 1 day |
| Documents | third round: `/usr/bin/open` hands documents over as `-NSOpen` (AppKit gives them to `application:openFile:`) and finds the application by the document types bundles declare: `open x.command` runs it in Terminal (`op1`); `open -a Terminal <folder>` opens a window, but in the folder's parent (`op2`: cause not established). Each `open` starts a new instance (no Apple Events) | folder: half a day |
| Other menus | third round, synthetic input: View > Split Pane (Command-D) splits (`sp1`); Shell > New Command opens its window (`nc1`; its layout is cramped); session restore: a relaunch restored a window with its text (`g2`). Print (Command-P): no panel appeared (`pr1`) | - |
| Not tried | drag and drop, services, Secure Keyboard Entry, New Remote Connection, marks navigation, full screen, AppleScript, Look Up, help (`-[NSHelpManager openHelpAnchor:inBook:]` is not implemented) | as they are met |
| Private frameworks | DataDetectorsCore, CoreAnalytics, libScreenReader: by design | - |
| The M1's own Terminal (macOS 27.0) | pending: macOS was not running. Rescan with `sf_terminal.sh scan` once it is there | rescan |

### Faults found on 14 November (all in the system, none in Terminal)
| Symptom | Cause | Fix |
|---|---|---|
| No menu was shown | the desktop has no menu bar process | AppKit 0028: the main menu as a strip of each window; needs `xdg_popup` in the compositor (`compositor/wm-darwin-popups.patch`, the Cocoa stream's commits, now in the M1's compositor) |
| Terminal started `sh` although the user database said zsh | Terminal runs `/usr/bin/login -pf <user>`; there was no `login` | `system/login.c` |
| Closing a window ended the program | `-[NSTabView removeTabViewItem:]` compared an `int` with `NSNotFound` and selected item -1 | AppKit 0029 |
| Closing the last window crashed in `CFRelease` | `-[NSAttributedString init]` replaced an application's subclass instance by a CoreFoundation string, into which the application then wrote its instance variables; `-retain`/`-release` of a subclass went to CoreFoundation | Foundation |
| Bindings of scalar properties raised | key-value coding reached only object instance variables | Foundation: scalars and structs in place |
| No binding of a pop-up button, matrix or tab view found its key | AppKit keeps those accessors as `-_key`/`-_setKey:`, which Apple's key-value coding searches and ours did not | Foundation |
| One profile, "New profile" | `-[NSBundle pathsForResourcesOfType:inDirectory:]` returned leaf names; `-[NSWorkspace typeOfFile:error:]` returned the extension, not the type the application declares | Foundation; AppKit 0029 |
| Preferences without a toolbar | a nib's toolbar lost its items when its delegate outlet was connected (a delegate that names no items); item visibility classes | AppKit 0029 |
| Pop-ups of Preferences empty or wrong | content arrays were handed to `-addItemsWithTitles:` as strings; the selection was not re-applied when the content came later | AppKit 0031 |
| `-[__NSCFBoolean longLongValue]` unrecognized | CoreFoundation's boolean class answered only `-boolValue` | Foundation |
| 20 s of processor time at launch | every string measurement is a pass of the whole text system (5 ms); Terminal measures 1384 strings for its profiles | AppKit 0032: sizes of unbounded one-style strings are remembered |
| Each bell an exception report | `-[NSDisplay beep]` raised "not implemented" | AppKit 0029: said once |
| Output 0.3 s late, menus 0.8 s, launch 3.4 s | AppKit, CoreText, Onyx2D, CoreGraphics and WindowServer were compiled without `-O`; a CTFont per drawn line was a fontconfig search and a FreeType face; glyph advances loaded each glyph's outline; application fonts scanned three times per launch; a fallback font searched through every installed face | `patched.nix` (`-O2`), CoreText 0034, AppKit 0035, Onyx2D 0037 |
| The last lines of a burst stayed off the screen until the cursor blinked | a flush that found its three frames busy waited 100 ms on the main thread and was dropped | WindowServer 0036: presented on the next release |
| Command-Q did nothing | Terminal sends SIGHUP to the child of the process it started and waits for that process; our `login` exec'd the shell | `system/login.c`: login stays the parent; AppKit 0038 (`+[NSCountedSet setWithCapacity:]`) |
| Bold drawn regular, italic light | `-convertFont:toHaveTrait:` took the first face with the trait; a variable font's named instance was drawn from its default instance | AppKit 0038, CoreText 0034 |
| True colour as bands | `_os_feature_enabled_impl` answers no to every feature flag (Terminal's DirectColor) | libSystem `feature-flags.patch` (Finch's reader), `FeatureFlags/Domain/Terminal.plist` |
| Rows not redrawn stayed grey; one-pixel seams at row edges | Onyx2D clips to a bounding box; `-getRectsBeingDrawn:` listed less than was cleared; dirty rects of whole points at 150% | AppKit 0040 |
| Quit and close alerts cut ("Termi...") | the legacy alert functions were a Cocotron panel with the message in its title bar; NSAlert's text did not wrap; words broke inside (a kerning pair ended a layout fragment and a space opening the next was no wrap point) | AppKit 0044, 0045, 0041, WindowServer 0043 |
| Profile list empty, labels cut | every float of a NIBArchive nib read as 0 (`-decodeFloatForKey:` returned a double); an image's cache window took the current context while a view drew | AppKit 0046, 0041 |
| A changed preference was lost | a click changes the cell, which no key-value observation of the control hears; the selection proxy set the value on an array | AppKit 0047 |
| Command-G typed "g" in the find bar | the field's text view inserted Command keys; the client was not in the bar's responder chain | AppKit 0048 |
| Menus 390 ms from click to popup (with the CJK fonts installed) | a fontconfig listing of every installed face per font name at each opening (the shortcut glyphs' fallback); eight shadow fills of the whole menu | AppKit 0049, 0046 |
| Terminal's icon was AppKit's generic one | the pin's Onyx2D reads no PNG entries of an `.icns` | Onyx2D 0042 (upstream tip's change) |

## Taken from Finch
Finch (an open Darwin for Apple Silicon; its own code "MIT OR Apache-2.0", SPDX per file) is cloned
at `sources/finch` (git-ignored). Taken from it, at Finch commit `2d9b1bb`, with its copyright and
SPDX notice kept in the file that carries it:

| Finch file | Where here | What |
|---|---|---|
| `userland/AppKit/NSWindowTabbing.m` | `cocoa/patches/0039-appkit-window-tabs.patch` (new file `AppKit/NSWindowTabbing.m`) | the structure of `NSWindowTab`, `NSWindowTabGroup` and NSWindow's tabbing calls: the group's bookkeeping (windows not retained, the window owns its group), the tabbing identifier's form, where `addTabbedWindow:ordered:` puts the tab, which tab is selected when one leaves, Move Tab to New Window, Merge All Windows. Adapted to this AppKit (manual retain/release, no `__weak`); the hand-over of one toplevel per group, the tab bar and the Window menu's tab items are ours |
| `userland/libsystem/featureflags/featureflags.c` | `libsystem-default/patches/feature-flags.patch` (new file `src/Libraries/libSystem/stub/pd_featureflags.c`) | the whole file unchanged: `_os_feature_enabled_impl` and `_os_feature_enabled_simple_impl` reading the FeatureFlags property lists (XML or binary, mapped, no allocation) |
| `userland/AppKit/NSTextFinder.m` | `cocoa/patches/0048-appkit-find-bar.patch` (new file `AppKit/NSTextFinder.m`) | the find bar view and NSTextFinder's actions; adapted to this AppKit's ivars (`NSMacOS26Additions.h`), manual retain/release, action constants, a container that is not a scroll view; Finch's NSTextView category not taken; the bar's own find actions are ours |
| `userland/AppKit/NSAlert.m` (`-layout`, `text_height`, `button_width`) | `cocoa/patches/0044-appkit-alerts-macos-layout.patch` (`AppKit/NSAlert.m`, notice above the function) | the alert's metrics and the order of its parts (420 points, 64-point icon, margins, button widths); written against this AppKit's ivars and string drawer |

## Performance, third round (14 November; on the M1, synthetic input, `tools/sf-latency.py`)
| Measure | Before this round (doc above) | Now | Note |
|---|---|---|---|
| Launch, exec to first commit | 0.97 to 1.06 s | 0.85 to 0.88 s (3 runs) | nib decoding not worked on |
| Output, 40 lines to commit / release | 66 to 72 ms / about 80 ms | 65 ms / 77 ms (86/104 the first time) | |
| Typing echo, key to commit | 15 ms | 7 to 8 ms | |
| Menu, click to its popup's commit | about 150 ms | 390 ms with the CJK fonts installed, about 195 ms after AppKit 0049 and the ring shadow | the old AppKit measured 387 ms under the same conditions: the fonts, not this round's AppKit, made it slower |
| New tab, Command-T to commit | - | 223 ms | |
| Tab switch, Command-Shift-[ | - | 47 to 102 ms | |
| Maximize, click to the first full-size frame | 218 / 137 ms | 219 ms | |
| Scroll with the wheel | a frame every 50 ms | not re-measured (the inject instrument has no wheel event) | |

## TERM6 round (night of 15 November): crashes first, then menus, colours, first launch
Input is synthetic throughout (the compositor's inject module). Measured in the application's own `WSTIME` lines
(`PD_WS_TIMING=1`), its own pictures (`PD_WS_DUMP_DIR`, and `PD_WS_DUMP_ALPHA=1` for the alpha), and screen
captures. Tools are in `runs/sf-term6` (not committed). Patches: cocoa 0123 (AppKit) and 0124 (WindowServer),
installed; 0125 onwards follow.

### Crash reports attributed and fixed (stability first)
| Report (M1, `~/Library/Logs/DiagnosticReports`) | Cause | Fix |
|---|---|---|
| Activity Monitor 03:15Z, SIGBUS in `O2RasterizeWriteCoverageSpan8888_Copy`, pc garbage | `-[NSProgressIndicator _runThreadedAnimation:]` ran a run loop on a thread of its own. Its `-setNeedsDisplay:` scheduled `_displayAllWindowsIfNeeded` on that thread's run loop, so every window was drawn there while the main thread drew too (the stack shows a pop-up button drawn from that thread) | 0123: the animation runs on the main thread. A view marked for display from another thread has its display scheduled on the main thread |
| Activity Monitor 03:18Z, SIGTRAP in `CFArrayGetValueAtIndex` under `-[NSTableView mouseDown:]` | a click on a row but beside every column gives column -1, which was used as an index (`objectAtIndex:-1`) | 0123: no column means no cell to track; the row is selected and a double click sends the double action, as on macOS |
| Terminal (robustness pass, reproduced with the same seed under `sf-catch`), SIGTRAP in `CFArrayGetValueAtIndex` under `-[NSMenu performKeyEquivalent:]` | the item count was taken before `menuNeedsUpdate:`. Terminal's delegates rebuild their menus there, so fewer items were left than the loop went to | 0123: `-performKeyEquivalent:` and `-update` go over a copy of the items taken after the delegate. The same seed then ran 216 actions with no crash |
| Activity Monitor 02:34Z, SIGSEGV in `objc_msgSend` under `_pdLayoutTreeFitting:apply:` (tab switch) | TERM5's case, fixed at 02:38Z (a removed view's constraints). The report predates that install | - |
| Activity Monitor 01:20Z | the report has no frames (an early version of the reporter): not attributable | - |
| (no report written for the Terminal trap above) | the reporter made only the last directory level: a new home has no `~/Library/Logs` | 0123: every level is made |

Robustness pass: `runs/sf-term6/fuzz.sh` sends random clicks and keys to my own Activity Monitor and Terminal, in
bursts of 12. Each burst is generated from a window dump and only when my app's window is on top. Points under the
desk's layer surfaces are never clicked. Nothing is sent that closes, moves or resizes a window. Terminal gets no
Return, Escape, Control key or paste, and its shell is `/bin/cat` (its `Shell` preference in a test home), so
nothing typed ever runs. Activity Monitor runs with `sf-nokill.dylib` (`activity-monitor/tools/sf-nokill.c`):
kill(), killpg() and proc_terminate() of other processes only log. Both apps ran with `MallocScribble=1` (freed
memory reads 0x55: checked on the M1 with a small program) under `sf-catch`. Gate on the build installed afterwards
(test prefix): Activity Monitor 600 s and 1032 actions, Terminal 600 s and 936 actions. Both stayed alive with no
crash report. Terminal raised `NSInternalInconsistencyException: divider index out of range` twice, caught by
the run loop: `-[NSSplitView setPosition:ofDividerAtIndex:]` asserted for a divider that is not there (fixed in
0125: it does nothing, as on macOS). Two incidents in early versions of the pass, before I added the window rule:
arrow keys and letters reached the desk's startup terminals (the root shells of `Terminal 000100000000` and
`000200000000`). No Return was sent, so nothing ran, and I cleared both lines with Control-U (`runs/m1-coord/log.txt`).

### Menus (item 1)
Profile (`sf-prof`, 0.5 ms, with a new `SFPROF_FLAG` per agent and `SFPROF_STACKS_ONLY`; 14 menus opened) and the
new `WSTIME menu-*` lines showed where the time goes. `orderFront` took 20-88 ms: the panel's eight shadow-ring
path fills plus the panel fill over the whole menu (54 % of the drawing). `+[NSColor controlAccentColor]`
re-read the global preferences file at every highlighted row (`CFPreferencesAppSynchronize`, 24 % of row
drawing). The text system ran per row, and the flush waited for the compositor's configure of the new
xdg_popup. 0123 changes:
- The panel is drawn once into a small template (per appearance, scale and pixel phase). It is then cut into nine
  pieces and copied for every size and every dirty row. `PD_MENU_VECTOR=1` draws the old vectors.
- The accent colour is read again at most once a second.
- The popup's role is requested from the window server before AppKit draws (0124 `kWSWindowPrepareRole`), so the
  configure arrives while the menu draws.
- The main menu is revalidated four times a second only while a menu bar server is connected. A window's strip
  and the desk's title-bar menu validate a menu when they show it.

| Shell menu opened by a click, 14 opens (my own Terminal, WSTIME) | Installed before (2cd9ea2a) | 0123 (test prefix) |
|---|---|---|
| tracking start to the popup's first commit, median / p90 / max | 44.1 / 72.8 / 96.1 ms | 17.0 / 35.4 / 56.6 ms |
| tracking start to the release of that frame (the compositor took it), median / p90 | 57.8 / 84.8 ms | 28.6 / 47.9 ms |
| `-openSubmenuAtIndex:` alone, median | 47.8 ms | 16.0 ms |

Later the whole M1 ran slower than at 03:29Z: the same old build took 60-70 ms where it had taken 44 (load average
about 1.3 with nothing of mine running, after CODEX-TB's 03:33Z kernel). The comparison was therefore repeated with
both builds from test prefixes, alternating in one hold. Tracking start to commit, median / p90: old 2cd9ea2a
60.3 / 74.1 and 70.5 / 77.1 ms; new (0123 + 0125) 38.8 / 45.5 and 32.5 / 41.0 ms. That is under 50 ms at p90.
0125 also lights the strip's title with only the strip drawn (not the whole window). It no longer validates the
whole main menu before every menu (1-3 ms; each menu is validated when it opens). The accent colour is read again
only when the global preferences file changes: CFPreferencesAppSynchronize read every domain, Terminal's own
profiles included, about 2 ms per menu.

A mistake shipped with 0123 and was fixed by 0125: the pieces were drawn antialiased, so half-transparent lines
showed where they met. They were in the installed menus from 04:26Z to 04:54Z. I had compared a dump of the old
vector drawing with the pieces only after installing. After the fix the two panels match (`runs/sf-term6/
menucmp.sh`; `PD_MENU_VECTOR=1` draws the vectors). 0125 also gives the shadow rings a hole of the panel's own
shape. The old plain hole, a rectangle 8 points inside the panel, left a darker band down the panel's inside edges
in both versions. In the build after 0125 (not yet installed), menus are opaque where the compositor cannot blur
behind them, as macOS's are with Reduce Transparency. At 92 % the text of the window behind showed through sharp.

Highlight against the pointer. The pointer was swept down the Shell menu and back by the inject module's `stream
abs` (records from a thread of their own at 125 and 250 Hz, as a device sends them, 0.33 px/ms). Old and new were
run from prefixes in one hold; latency from the motion the app received to the highlight's commit, and to the
release of that frame (the compositor has shown it):

| Sweep | Build | motion to commit, median / p95 / max | motion to release, median / p95 / max |
|---|---|---|---|
| 125 Hz | old 2cd9ea2a | 2.8 / 23.3 / 44.0 ms | 9.7 / 28.9 / 57.6 ms |
| 125 Hz | new | 2.6 / 10.8 / 31.7 ms | 9.1 / 19.9 / 36.6 ms |
| 250 Hz | old | 3.0 / 16.5 / 47.2 ms | 9.3 / 25.9 / 52.3 ms |
| 250 Hz | new | 2.6 / 12.4 / 61.1 ms | 6.3 / 27.9 / 66.2 ms |

The median is within one frame; p95 is about one frame, and the max is up to four frames. "Under the pointer in
every presented frame" is not possible here and was not reached. The compositor draws the pointer where it is
when it renders, but the menu can only show what it drew for an earlier motion. In the frames right after the
pointer crosses into a row, the highlight is still on the previous row: for a pointer moving v px/ms, about
v × latency / row height of the frames (rows are 33 px at 150 %). At 0.33 px/ms and 9 ms that is about 9 % of
frames, one row behind. For a fast flick of 2 px/ms it is about half the frames for a moment, again one row.
I also tried a screen capture every few frames during the sweep. The captures stall the compositor's loop while
the pointer thread keeps moving, so they show the menu 3 rows behind. That measures the capture, not the
desktop, and was not used.

### A third crash found by the robustness pass (0127)
Terminal, `MallocScribble=1`: SIGSEGV in `objc_msgSend` at 0x5555555555555555 (freed and scribbled memory) under
`-[NSApplication sendAction:to:from:]` ← `-[NSButton performClick:]` ← `-[NSButton performKeyEquivalent:]`. This
time the report was written (the reporter's directory fix). Same seed with `PD_KEYEQ_TRACE=1`: Escape clicked
an alert sheet's Cancel. `-[NSAlert _alertButton:]` ends the sheet, and `-sheetDidEnd:` releases the alert and
with it its window and buttons, while the click still ran in them. 0127: the alert and its window stay until the
event is done; a button clicked by its key equivalent stays until its action returns; the find bar's controls
stop messaging an NSTextFinder that is gone. The same seed then ran 240 actions with no crash.

### Table headers: the sort indicator over the title (owner's finding)
The owner saw the sort chevron printed on top of the column title in Activity Monitor ("% CPU"). The header view
drew the chevron at a fixed place 16 points from the column's right edge. The cell drew the title over the whole
width, so a right-aligned title ended under the chevron. 0128 implements `-sortIndicatorRectForBounds:` (an
18-point slot at the trailing edge, the chevron 9 x 6 points centred in it and on whole pixels) and
`-drawSortIndicatorWithFrame:inView:ascending:priority:` (a stroked chevron, round caps, the secondary label
colour of the appearance). The header view tells the cell whether its column shows an indicator, and the cell's
title rect then excludes the slot. A title too wide is cut with an ellipsis. Before/after crops of Activity
Monitor's header at 150 % (`runs/sf-term6/shots/hdb-*-hdr.png`, `hda-*-hdr.png`): "% CPU" with the chevron on
"U" before; "% CPU ⌄" after, the chevron in its own place and level with the text. Not walked yet: the other four
tabs' tables, the inspector's tables, and an ascending header after a click. My click landed on the column divider,
so it resized instead of sorting.

### Colour panel and opacity (item 2)
Terminal's Profiles > Background well (`TTBackgroundColorWell`) received the click (`PD_COLORPANEL_TRACE=1`) but never
called `-activate:`. The compositor sends motion at every button change. The well took that motion "drag" in place
as the start of a colour drag, which opens nothing. 0127: a drag needs a move of three points.
After that fix the panel opens attached to the well, with Terminal's accessory (Blur, Inactive Windows Opacity
and Blur) under the panel's own controls. Its opacity slider then set every value to 0, and the window went to
Terminal's minimum of 5 %. The cause: this AppKit declared `-floatValue`/`-setFloatValue:` of NSCell and NSControl
as CGFloat. macOS declares float, and an application built for macOS reads `s0` while we answered in `d0`.
`PD_SLIDER_TRACE=1` showed the slider computing 0.86 while the panel read 0. 0127: float, as in macOS (NSCell,
NSControl, NSActionCell, NSScroller; code built against the old header must be built again).

Reduce Transparency. AppKit answers YES while the compositor cannot blur and nobody has decided (TERM5); Terminal
then draws every profile opaque. A user who moves the panel's opacity below 100 % has decided. 0125 records that
in the application's own defaults (`PDTranslucencyChosen`) and posts
`NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification`. Terminal observes it (`-[TTView
handleAXDisplayOptionsChange:]`) and redraws with the profile's opacity. The user's explicit
`com.apple.universalaccess reduceTransparency` still wins either way. To return to opaque:
`defaults delete com.apple.Terminal PDTranslucencyChosen`. Checked on the M1 with a new home (test prefix; the drag
was synthetic): the slider dragged to 74.7 % gave the Terminal window alpha 190 (74.5 %), live, with the profile
saved (12 profiles in the new home's plist, `PDTranslucencyChosen` true). The desk's wallpaper shows through, sharp,
because there is no blur. The Text well opens the panel with the text colour and without the accessory.
Still wrong: a value typed into the panel's opacity field and ended with Return did nothing in these test-prefix runs.
The same runs log "no key bindings loaded" and lack AppKit's checkbox images, so AppKit's resources are not found
under `DYLD_FRAMEWORK_PATH`. This is to be checked with the installed files. Image well, bold, selection and
cursor wells, and Inactive Windows were not walked.

### A new user's empty profile list (item 3)
In a new home, Preferences > Profiles was empty and every control of the Text pane was blank: no font, grey wells.
Terminal builds its Preferences window, and binds the list, before it imports its built-in profiles. It then adds
each one with `-[TTProfileManager insertObject:inProfilesAtIndex:]` called on itself. Our Foundation's key-value
observing only heard setters, so the array controller never learned of them. Foundation (`terminal-app/patches/
foundation-macos-applications.patch`) now wraps the to-many accessors as macOS does: `insertObject:in<Key>AtIndex:`,
`removeObjectFrom<Key>AtIndex:`, `replaceObjectIn<Key>AtIndex:withObject:`, `insert<Key>:atIndexes:`,
`remove<Key>AtIndexes:`, `replace<Key>AtIndexes:with<Key>:`, `add<Key>Object:`, `remove<Key>Object:`, `add<Key>:`,
`remove<Key>:`. Observers hear the key's new value. `probe/sf-kvo-probe.m` checks this ("to-many accessors", 0
wanted): the installed library answers 31, the new one 0. On the M1 with a new home, the installed libFoundation
gives an empty list. The new one from a test prefix gives the twelve profiles with Clear Dark the default selected,
and the font and colours filled in. QEMU rehearsal t6f: libFoundation replaced, the compatibility check failed=0. Two
verdicts failed: libobjc 2eca1460 (PERF's, already on the M1) did not take over through `DYLD_LIBRARY_PATH` on that
root's old dyld. t6f-2 (the second boot) had one failure, the same probe. t6g repeated the rehearsal without
libobjc in the list; that root keeps the old libobjc, so `sf-objc-probe` still failed: failed=1 in t6g and t6g-2. In
all four, libFoundation was replaced and `sf-kvo-probe` was ok with the new to-many check. Guarded install on the
M1 at about 06:05Z (`sf_terminal.sh install core libFoundation.dylib`, core.lock). The probe failed with the library
in use and passed with the new one, and `/bin/sh`, node, claude, python3, bash, git, vim and nvim ran with it
(tmux status 1 both with the old and the new). Installed as inode 85284; the previous file is
`/usr/lib/libFoundation.dylib.old8` (a8aea7dc). `sf_compat_push.sh check` failed=0 afterwards. Revert:
`ln -f /usr/lib/libFoundation.dylib.old8 /usr/lib/libFoundation.dylib.back && mv -f /usr/lib/libFoundation.dylib.back /usr/lib/libFoundation.dylib`.

### Installed (04:26Z, direct, the previous files kept as `.pre-t6`)
AppKit f0bcba93 (0123; .pre-t6 = 2cd9ea2a) and libWindowServer df87120c (0124; .pre-t6 = 5d359f88). Both are the
nix builds of the committed patches, byte-equal to the gated test builds. After-install checks
(`runs/sf-term6/tcheck6.sh`, only my own pids): Terminal opened, Preferences opened, closed and reopened,
Inspector, new tab, colour panel, Shell menu, Activity Monitor and its inspector opened and closed with the app
alive (`shots/inst1-sheet.png`). `sf_compat_push.sh check` 18/18. `/usr/bin/sample` is new (SF-AA). 04:52Z: AppKit
7320ca67 and libWindowServer f834f297 (0125, 0126) replaced them, after a further robustness pass on that build
(Activity Monitor 228 actions, Terminal 204, no crash and no exception). The same checks passed (`shots/inst2-sheet.png`;
the Shell menu without lines). `.pre-t6` is still the file from before TERM6. Revert:
`ln -f <path>.pre-t6 <path>.back && mv -f <path>.back <path>`.

### Split panes: a grey box, and the window shrank (owner's finding; cocoa 0130)
Reproduced on the installed build (synthetic, `runs/sf-term6/splitpane.sh`, my own Terminal): Command-D, then
Shift-Command-D left the remaining pane at its half and the other half an empty grey box; each split and each close
made the window shorter (719 -> 599 px in three rounds, `shots/spold-*`). Traced with `PD_SPLIT_TRACE=1` and
`PD_WINFRAME_TRACE=1` (who set the window's frame, with the calling frames):
- `-[NSSplitView adjustSubviews]` did nothing with fewer than two panes: the pane left after a close kept its half.
  Now a lone pane fills the split view, as on macOS.
- `-[NSSplitView addSubview:]` laid the panes out again at once (Cocotron's; macOS leaves that to the application,
  "send -adjustSubviews after adding or removing"). Terminal splits by giving both halves their frames (232 + 7 +
  232 points in a 457-point view), adding them, and making its window 14 points taller; the immediate layout made
  them 298 and 152, Terminal fitted its window to their rows, and the window shrank. Now adding or removing a pane
  only marks the split view; what the application leaves inconsistent (a pane alone after a close) is laid out
  before the split view is next drawn.
- the panes' sizes were each rounded down and the last pane did not take the remainder; it does now.
After (`sp30`, `sp2*`, synthetic): five rounds of split and close, the window 809 <-> 830 px (Terminal makes it 14
points taller for the divider while split) and back to the same size each time; in a second tab (761 <-> 782); after
two Command-= (852 <-> 872); full screen (2560x1600 throughout, 242x69 -> 242x34 per pane -> 242x69; leaving full
screen gave back 719); no grey box in any (`shots/sp2*-split.png`, `-end.png`). The first split of a fresh window
also moves it down the screen when it starts near the top (seen once, `sp30`); not chased.

### Visual check after every install (from 15 November, ~09:30Z)
0130's string-layout cache drew from freed layouts after a few minutes (SF-AA, "Regression"); the checks before that
install were crash gates and timings of fresh launches and did not look at pixels late in a session. Every install
since is preceded and followed by `runs/sf-term6/visgate.sh` on my own instances (Activity Monitor after refreshes, its
five tabs, scrolled, a column widened, the inspector; Terminal's window, a new tab, the Shell menu, Preferences) for the
build before and the new one in the same hold, compared side by side (`visdiff.py`) and looked at. A test prefix has
no AppKit resources: its radio buttons and check boxes have no images (Preferences differs by that alone). Terminal
after 0130-0132 and the Foundation install: identical to the build before in all four views.

### Not done this round (TERM6)
Item 5 (prompt marks, the title-bar menu's invoke, multi-line labels, Preferences readable at 150 %) was not worked on:
the owner's Activity Monitor findings came first. Terminal's figures with 0130 (SF-AA): launch 300-424 ms, echo 4-9
ms, `seq 1 300000` 0.98-1.04 s, a wheel step 14-23 ms (14-16 before).

## TERM7 round (15 November, midday)
Synthetic input to my own instances only; tools in `runs/sf-term7` (not committed). Activity Monitor's side is in SF-AA,
"TERM7 round".

### Firefox's Cocoa patches in the main series (CODEX-FX's 0200-0207)
Merged unchanged with their numbers (`cocoa/patched.nix`, after 0132): 0200 (Onyx2D: FreeType faces and the glyph cache
under one lock), 0201 (a run loop without a display), 0202/0203 (layer context at the window's pixel scale), 0204 (a
root view's layer replacement reaches its context), 0207 (a context lets go of the tree it held: Firefox's close-tab
crash). QuartzCore is now built by `patched.nix` (`-A quartzcore`; upstream's recipe, not optimised; the installed
`QuartzCore.pd` ae393493 reproduces from the pin byte for byte) and installs as `QuartzCore.pd` behind the compat
wrapper. Not merged: 0205 (opt-in; its tree walk reads layer properties other threads change), 0206 (not yet
qualified; it changes `+frameRectForContentRect:styleMask:` for every application). For Terminal and Activity Monitor
0201-0204 and 0207 are not reached (no OpenGL on the M1: they draw on the CPU); 0200 adds an uncontended lock per glyph
run. Terminal's figures, the installed build and the candidate alternating in one hold (`term-fig.py`, two rounds):
launch 318-419 / 318-540 ms (first runs 1762 cold / 540), echo 5-14 / 3-10 ms, `seq 1 300000` 0.96, 1.04 / 0.98, 1.06 s,
wheel step 6-43 / 6-17 ms: no difference beyond the spread of either.
`build-compat.nix` takes QuartzCore's library from `patched.nix`, so a later `sf_terminal.sh install direct` does not
put the unpatched QuartzCore back.

### Layers on the CPU, CAOpenGLLayer, the names VLC imports (cocoa 0133-0135)
- TERM6's hosted-layer probe stayed the window's grey because a view with a layer drew into a bitmap of its own
  (`graphicsContextForView`), which only the OpenGL layer renderer composites; without OpenGL it was drawn and never
  shown. 0133: without OpenGL such a view draws into its window; what `-updateLayer`/`-displayLayer:` put in its layer
  is drawn too; CVPixelBuffer contents are drawn (a CF object's class is not Nil, so that branch was never taken).
  Observed in the window's own picture (`PD_WS_DUMP_DIR`): the probe's red/green/blue frames, its rounded badge and its
  magenta CVPixelBuffer sublayer (all window grey before).
- 0134: CALayer `-display` draws `-drawInContext:` (or the delegate's `-drawLayer:inContext:`) into a bitmap at the
  layer's pixel size and makes it the contents; `-displayIfNeeded`; layers marked `-setNeedsDisplay` are displayed
  before a tree is drawn, on the CPU path and by CALayerContext. `CAOpenGLLayer`: one OpenGL path, CGL (whatever
  OpenGL.framework is loaded): a context of the layer's pixel format, its offscreen surface at the layer's pixel size,
  read back as the contents. Where CGL cannot run (the installed CGL without `/usr/lib/libEGL.1.dylib` calls a null EGL
  pointer) it makes no context and says so once. Observed with `probe/sf-gllayer-probe.m` (my own window, no input):
  without OpenGL the message, no crash, the delegate-drawn layer shown; with CODEX-FX's private CGL and the Asahi EGL
  in the DYLD paths, 44 frames drawn by the layer's subclass and the delegate-drawn layer both in the window picture.
- Names VLC 3 imports: `NSImageNameNetwork` with the other standard image names, `NSAccessibilityFullScreenButtonAttribute`,
  `NSCharacterEncodingDocumentOption`, `NSWorkspaceApplicationKey`, `NSWorkspaceDidActivateApplicationNotification`,
  `NSWorkspaceWillSleepNotification` (AppKit); `CGDisplayUsesOpenGLAcceleration`, the display fade reservation calls (none
  available) (0135, CoreGraphics). Not done: VLC's window needs CGL working system-wide (its video output is an
  NSOpenGLView): CODEX-FX's `cgl-patches/0001`, not yet qualified for installation; NSAppleScript, AddressBook,
  ScriptingBridge, WebKit (requests-for-CODEX.md).
Installed 13:12Z with Activity Monitor's fixes (SF-AA, "TERM7 round": hashes, gates, revert). After the install:
Terminal's window, tab, Shell menu and Preferences identical to the build before (visual check, mean difference 0.00).

### Terminal checks nobody had finished (installed files; synthetic input to my own Terminal in a test home)
- Prompt marks: after `echo one`, `seq 1 3`, `echo two` a faint bar sits left of each of the three command lines
  only, none on output lines or on the waiting prompt (`t1-marks-crop.png`): pass.
- The desk's title-bar menu (right click on my window's title bar): Maximize ... Close, Hide Menu Bar, then one row per
  application menu. Hide Menu Bar hides the strip; the menu then offers Show Menu Bar, which brings it back; Shell opens
  Terminal's own Shell menu (New Window, New Tab, New Command... with their keys) and choosing New Command... ran it
  (its panel opened; Escape closed it) (`m2-*`, `m3-*`): pass.
- A fresh window at the top of the screen (1171,37) keeps its place through its first split and the close of it (it
  grows 719 -> 740 px for the divider and back): the move seen once by TERM6 was not reproduced.
- Preferences at 150 %: General, Profiles (Text tab), Window Groups, Encodings readable (`pf1-pane*-crop.png`). Window
  Groups' two-line explanation showed one line, cut: a wrapping label lost its last line to the point its text is moved
  down (32 points for two 16-point lines, 31 left). 0136 keeps the whole height: both lines shown (test prefix `c5`,
  then installed 14:07Z: AppKit 96afb830, gated as above: visual check identical but for this pane, random input
  Activity Monitor 252/240 and Terminal 228/228 actions before/after, no crash; `.pre-t7` still the file of before TERM7). Profiles' thumbnails are grey boxes
  (only the first shows a strip of its preview): as in TERM6's captures, not changed by this round: FAIL.
- Not walked: a typed opacity value + Return in the colour panel; Preferences' other Profiles tabs.
- Wheel step, installed files (`term-fig.py`, two rounds): 9 14 35 16 14 and 16 6 14 14 16 ms (TERM6: 14-16, then
  14-23). The median is 14-15 ms in every round measured; single steps of 23-43 ms appear in both the build before and
  this one (43 in the build before, 35 in this): no regression found, the spread is not explained.

### Foundation networking (installed 14:5xZ through the rehearsal and the guarded installer)
`terminal-app/build-foundation-networking.nix` puts CODEX's networking (Finch's NSURLSession, NSURLSessionSockets,
NSURLCache, NSHTTPCookie, NSURLCredential, WebSocket tasks; public names) into libFoundation. CODEX's proposal did not
link (the error domain and two keys lived in the file it emptied), would have exported OpenSSL's ~9400 symbols and
linked CFNetwork (which re-exports libFoundation and loads SystemConfiguration: every Foundation program would load
both, in a cycle). This recipe keeps the three constants, hides OpenSSL (`-load_hidden`) and takes CFNetwork's four
CFHTTPMessage functions when the first request is written (`foundation-networking/cfnetwork-lazy.c`). The CA bundle
(nixpkgs' Mozilla bundle) is where the system build reads it, as CODEX had it:
`/System/Library/Frameworks/Foundation.framework/Versions/A/Resources/ca-bundle.crt` (new file, with the licences in
`Resources/Licenses/FoundationNetworking`). `build-macos-apps-root.nix` carries it (`SF_FOUNDATION=plain`: the one without).
- Rehearsal: QEMU `t7b` failed=2 and `t7b-2` failed=1, only the known libobjc probe (as TERM6's `t6h`); libFoundation
  replaced in both. `probe/sf-kvo-probe.m` now also checks the networking classes (cookie, cache, WebSocket; status 1
  with the file of before, 0 with the new one), so the guarded installer has something to prove.
- A first guarded install (14:37Z) put the CA bundle in `/usr/share/certs`, where I wrongly thought the code looked:
  HTTPS failed closed (-1202) for every program while the file of before answered it. Reverted at 14:40Z (`.old10`).
  Installed again after the CA bundle was put in Foundation's Resources: libFoundation 43a5fc1b, the file of before
  `/usr/lib/libFoundation.dylib.old11` (d7fe3c48). Proven by the installer with /bin/ls, /bin/sh, node, claude, python3,
  bash, git, vim, nvim (tmux status 1 with both, as before). On the installed files, no environment:
  `networking-probe --native https://example.com` HTTP 200, 577 bytes; with `SF_URLSESSION_CA_FILE=/nonexistent` -1202,
  0 bytes. `sf_compat_push.sh check`: failed=0. (The file of before also answered HTTPS 200, but ignored a missing CA
  file: it did not verify the peer by the bundle.)
- Cost: libFoundation 2.5 -> 9.7 MB on disk, load-time fixups 12.4k -> 39.5k; a small Foundation program 39.0-42.0 ->
  40.7-44.4 ms a run (40 runs each, both from a prefix; +0.9 to +2.6 ms); its resident memory 9.8 -> 6.3 MB (it no
  longer starts what the old NSURLSession did). The M1 then rebooted (CODEX-TB's kernel cycle, 14:45Z) and came up
  with it (desktop, dock, terminals). After the install (my own instances): the visual check identical for Terminal
  (all four views 0.00) and Activity Monitor (figures only); Terminal launch 317-440 ms, echo 4-9 ms, `seq 1 300000`
  1.029/1.052 s, wheel 5-25 ms (median 14-15); before it, the same day: 308-433 ms, 3-10 ms, 1.011/1.023 s, 6-35 ms.
  Activity Monitor's CPU 5.8 % (40 s at the 5 s interval). Random input on the installed files afterwards: Activity
  Monitor 252 and Terminal 240 actions, `MallocScribble=1`, layout cache forced small: alive, no report.
- Revert: `ln -f /usr/lib/libFoundation.dylib.old11 /usr/lib/libFoundation.dylib.back && mv -f
  /usr/lib/libFoundation.dylib.back /usr/lib/libFoundation.dylib` (and remove `Resources/ca-bundle.crt`, `Resources/Licenses`).

## TERM8 round (15 November, evening)
Synthetic input to my own instances only (the compositor's inject module); tools in `runs/sf-term8` (not committed).
Activity Monitor's side is in SF-AA, "TERM8 round".

### Terminal ended at every minimise (the owner's report, via DESK6)
Crash report (`~/Library/Logs/DiagnosticReports/Terminal-651.crash`): SIGSEGV at 0x30 in `O2ContextCurrentGState` under
`-[NSImage drawInRect:fromRect:operation:fraction:]`, called by Terminal from the miniaturise notification that
`-[NSWindow _serverDidSetState:]` posts. Terminal draws its miniwindow's picture there with no current graphics context;
TERM7's 0133 read the context's alpha without checking for one (a regression of 13:12Z). Fixed in 0137: with no current
context an image or a string draws nothing, as on macOS (CoreGraphics logs an invalid context). The next run ended on
`-[NSString drawWithRect:options:attributes:]`, which this AppKit lacked: now there, with its 10.11 `context:` form and
`-boundingRectWithSize:options:attributes:context:`; NSAttributedString's `-drawWithRect:options:` no longer raises.
The miniwindow shows Terminal's title. Minimise/restore matrix (`runs/sf-term8/minitest.sh`, desk6 .next, restore by a
click on the miniwindow, the compositor's shell surface that appears): candidate c11/c12 and installed: Terminal alone
10/10 (Command-M and Window > Minimize alternating), with tabs 4/4, split 4/4, Preferences open 4/4; Activity Monitor
6/6, its inspector 4/4. Before (installed 96afb830): 1 of 1 ended (SIGSEGV), as DESK6 saw. Found on the way: with a
sheet open, Command-M miniaturises the sheet's own toplevel and leaves its window (macOS takes the window with it); not
fixed. The frame has no minimise button (Close and Maximize): its title-bar menu and the dock were not tried.

### VLC 3.0.24 shows its window (item 1)
With CODEX's v18 stage copied to `/private/var/db/sft8/vlc`, my AppKit candidate, my Foundation candidate and a
MediaPlayer test fixture in front: the first-run alert ("Check for album art and metadata?", answered No, Thanks by a
synthetic click), then the "movie.mov" window with VLC's menu strip, control bar (time advancing, volume) and the
Fullscreen Controls panel; playback ran (videotoolbox, auhal, vout "caopengllayer") until ended. The picture is black:
`CGLTexImageIOSurface2D error ... invalid numerical value` for every frame (CGL, CODEX-FX's). What was in the way, in
order: `+[NSColor currentControlTint]`; NSProxy without `-allowsWeakReference` (NSNotificationCenter keeps observers
weakly; libobjc forwarded the question into the proxy) - Foundation; `-[NSOutlineView setDropItem:dropChildIndex:]`
raised "not implemented"; a modal panel's wait had no window-server event source, so the first-run alert never
answered a click (it woke only when a timer fired: Terminal's alerts had one, VLC has none before -run); the shared
NSDocumentController kept unretained (a nib's instance, released: SIGSEGV in `_openUntitledFile:`); MediaPlayer
missing (weak import used behind @available: a NULL constant read); `-[NSArray initWithArray:copyItems:]`; CALayer
`needsDisplayOnBoundsChange`, `autoresizingMask`, `visibleRect`, `layoutSublayers`. Static scans
(`runs/sf-term8/selscan.py`, `unimpl.py`: selectors VLC's plugins send that nothing implements, or that AppKit implements
with NSUnimplementedMethod) found the rest of 0137 at once. Status items: this desktop has no status bar; an item keeps
what it is given and is shown nowhere (said once). Templates (NSActionTemplate and others) are AppKit's symbol drawings.

### Firefox (CODEX-FX's patches, Command-W, pinch, open)
- 0205 and 0206 not merged; CODEX-FX withdrew both. 0205: its fingerprint walk at each commit reads the layer context's
  root without holding it (the tree's own reads are safe: retained copies under the tree lock) and showed no gain.
  0206: Terminal and Activity Monitor never send the class conversions (`+frameRectForContentRect:styleMask:`; checked in
  their selector references and method lists), so it would change only Firefox, whose tab strip is right without it.
- Merged (numbers kept): 0210 + windowserver 0002 (`0210w`; finger scrolling in pixels, opt-in), 0212 (NSMenu
  appearance), 0214 (pasteboard provider), 0215 (CoreText copy keeps its graphics font), 0217 (font collection index),
  0219 (layer context releases its renderer, root and CGL resources; adapted: no 0208 here).
- Pinch: DESK6's compositor sends `zwp_pointer_gesture_pinch_v1` (inject `pinch`, trackpad ring v2). 0139/0139w:
  libWindowServer binds pointer gestures; AppKit makes NSEventTypeMagnify with `-magnification` and `-phase`. My probe
  window, inject pinch to 1.2 then 1.5: `-magnifyWithEvent:` four times, 0 / 0.1992 / 0.3008 / 0, began, changed,
  changed, ended. Synthetic; the physical two-finger pinch depends on the trackpad driver's ring.
- Command-W for a Mac application outside the application directories is the compositor's (its catalogue decides):
  `runs/m1-coord/requests-for-DESK6.md`.
- `/usr/bin/open` opens http(s) URLs with LaunchServices' handler (CODEX-FX's system-patches/0001, merged; CoreServices
  loaded only when a URL is opened).

### What Foundation's networking costs every process (item 4)
`runs/sf-term8/probe/fndcost.c` (a C program linked to libFoundation; exec to main from the kernel's start time,
footprint and resident size at main), 30 runs each, alternating, on the M1: the installed libFoundation 43a5fc1b (9.7 MB,
networking and hidden OpenSSL inside) against the one before it, d7fe3c48 (2.5 MB, from a prefix): exec to main
median/min/p90 21.36/20.95/22.36 ms against 21.30/21.12/22.44 ms; footprint 4176 against 3632 KB (+544 KB), resident
5712 against 5184 KB, internal (dirty) 4064 against 3536 KB (+528 KB). Under the ~1 ms / ~1 MB per process at which the
networking was to move into an image loaded on first use of NSURLSession: not split. (The +0.5 MB is per process: about
15 MB over the 30 Objective-C processes of a desktop session; OpenSSL's relocated data and Finch's classes.)

### Installed (20:2xZ, direct, the files of before kept as `<path>.pre-t8`)
AppKit 0c3fa810 (0137, 0139, FX 0210/0212/0214; before 96afb830), Onyx2D 2db9aa17 (0217; cd851066), CoreText 4413922a
(0215; 348df207), `QuartzCore.pd` 738ae322 (0138, FX 0219; 935c21d6), libWindowServer 691d0ec6 (0139w, FX 0210w;
6ee1bfd2), `/usr/bin/open` dc5a66bc (84e2f58d): the nix builds of the committed series, hashed on the M1 before anything
ran them. Gates before, on the same files from a prefix (c12): visual check identical for Terminal's window, tab, menu
and Preferences, Activity Monitor's views the same but for the figures; Activity Monitor 3 minutes with the layout cache
forced small (text right at 1 and 3 minutes); random input Activity Monitor 252 / Terminal 216 actions
(MallocScribble, cache forced small, sf-catch): alive, no report; Terminal's figures installed/c12 alternating:
launch 303-416 / 313-424 ms, echo 2-7 / 2-21 ms (21 once, the first key), `seq 1 300000` 0.99, 1.09 / 1.11, 1.02 s,
wheel 14-29 / 5-17 ms; minimise matrix and pinch as above. `sf_compat_push.sh check` 18/18 before and after. After the install: minimise matrix Terminal 10/10,
tabs 4/4, split 4/4, Preferences 4/4, Activity Monitor 6/6, inspector 3/4 (one Window-menu click of the test missed the
menu; nothing crashed); Activity Monitor's random-input pass alive, no report. Discarded: the owner was using the M1 at
20:15-20:25Z, when my Terminal random-input pass on the candidate, Terminal's figures and the visual check of the
installed files ran (he fought the injected pointer): those three are to be repeated with DESK7's virtual seat.
Revert: `ln -f <path>.pre-t8 <path>.back && mv -f <path>.back <path>` for the six paths.

### Foundation for VLC (core; installed 20:56Z by the guarded installer)
`terminal-app/patches/foundation-macos-applications.patch`: the NIB allocation hook (CODEX's), NSUserDefaults
`-addSuiteNamed:`/`-removeSuiteNamed:`, `-[NSCalendar ordinalityOfUnit:inUnit:forDate:]`, NSLocale
`+characterDirectionForLanguage:`/`+lineDirectionForLanguage:`, `+[NSThread sleepForTimeInterval:]`/`sleepUntilDate:`,
`-[NSURL getFileSystemRepresentation:maxLength:]`, `-[NSFileManager displayNameAtPath:]`, NSData's 10.6 base64 pair,
NSProxy `-allowsWeakReference`/`-retainWeakReference` (VLC's data-source proxy registered as an observer raised in
libobjc's weak store), `-[NSArray initWithArray:copyItems:]`. `sf-kvo-probe` checks each ("VLC additions 0 (wanted 0)").
QEMU rehearsal t8a and its second boot: libFoundation replaced, sf-kvo-probe ok, the only failed verdict sf-objc-probe on
a rehearsal root without PERF's libobjc (as in TERM6's t6f/t6g and TERM7's t7b). Guarded install (lifeline SSH open):
the probe failed with the file in use (status 134) and passed with the new one, programs status 0, installed
libFoundation a17bce22 (before 43a5fc1b, kept as `/usr/lib/libFoundation.dylib.old12`); `sf_compat_push.sh check` 18/18.
Revert: `macos-compat/sf-lib-install.sh` with `.old12` as the new file (core.lock, announced, lifeline open).

### Firefox's windowless process, cursors, reopen, FX-010, FX-004 (owner's direction: Firefox first)
Cocoa 0141w-0147, candidate c18, INSTALLED 00:09Z (16 November; direct, the file of before kept as `<path>.pre-c18`):
AppKit 835cdbce (c14's 18573adf kept), Onyx2D ace5dc3d (2db9aa17), `/usr/lib/libWindowServer.dylib` 739a1630
(691d0ec6); the nix builds of the series, which now reproduces every installed framework byte for byte. Gates before
(c18 from a prefix, owner idle; the first pass at 23:23Z was void, the desktop had frozen): visual check (Terminal
identical; Activity Monitor the same but for the figures; my first installed reference lost its Activity Monitor to a
dock menu left open and was taken again), 3 minutes of late views, random input Activity Monitor 84 and Terminal 108
actions (each stopped by my new guard when a desk surface appeared - a dock tile's name under the resting pointer;
the guard now lets that pass), Terminal's figures installed/c18 alternating: launch 214-398 / 352-431 ms, echo 5-11 /
3-9 ms, `seq 1 300000` 0.96-1.01 / 0.99-1.06 s, wheel 14-16 / 6-17 ms. After: visual check of the installed files
(as the candidate), minimise: every Command-M and frame-button round passed (Terminal 7 of 7, with tabs, split pane,
Preferences 3 of 3 each, Activity Monitor 4 of 4), the Window > Minimize rounds by clicks did not minimise (my click
on the menu item: not settled whether the item or my placement), the inspector's rounds 0 of 4 (the test's tile
pick, as before), random input Activity Monitor and Terminal 240 actions each, alive, no report;
`sf_compat_push.sh check` 18/18. Revert: `ln -f <path>.pre-c18 <path>.back && mv -f <path>.back <path>`.

- **A process left with no window** (OWNER-WINDOWLESS, CODEX-FX's reading of the owner's Firefox log: `xdg_wm_base#6:
  error 0: Surface already has a role`, then `Wayland dispatch stopped with error 100`, the process alive, sound
  playing). Two faults in libWindowServer. (1) A window shown again as another kind of role (a popup whose parent is
  gone comes back as a toplevel; a level change between a layer and an ordinary window) was given the new role on its
  old wl_surface; a surface keeps its role for life, so the compositor ended the connection. 0143w gives such a window
  a new wl_surface (its fractional-scale and viewport objects, blur, opaque and input regions made again), at every
  place a role is made, including the popup role made ahead (kWSWindowPrepareRole). (2) When the connection ended, the
  dispatch thread stopped and everything else went on (`connected` stayed 1): the application ended only if someone
  killed it. Now it says why (the protocol error and object, or the socket error) and ends with status 70, as a Mac
  application does when its window server goes; its app-menu socket file is removed. `PD_WS_SURVIVE_DISCONNECT=1`
  keeps the old behaviour for tests. Observed with my own probe (`runs/sf-term8/probe/rolechange.m`, no input): the
  installed files reproduce both (the error, then the process alive); c17: "gets a new surface", no error, shown; with
  the socket shut down under it the probe ends (status 70) and says so.
- **Reopen and quit from the desk** (0144): the app-menu socket the compositor already uses for the title-bar menu
  takes `reopen` (macOS's reopen event: the delegate's `-applicationShouldHandleReopen:hasVisibleWindows:`, unhide, a
  minimised window brought back, else the untitled window) and `quit` (`-terminate:`, with or without a window).
  Asked of DESK7 for the dock and the switcher (`runs/m1-coord/requests-for-DESK7.md`).
- **Cursors** (0141w, 0142, 0145w): libWindowServer asks DESK7's compositor for named shapes (`wp_cursor_shape_v1`, the
  map DESK7 gave) instead of uploading libwayland-cursor's images: the desk's own pointer set at the display's scale.
  Terminal's text area never asked for an I-beam: it makes its cursor-update tracking area with
  `NSTrackingInVisibleRect` and an empty rectangle, which this AppKit intersected with the bounds and dropped (0142:
  such an area follows the visible rectangle, as on macOS). Terminal then sets its own I-beam (Apple ships high-contrast
  I-beam TIFFs in Terminal.app and sets them with `-[NSCursor initWithImage:hotSpot:]`), which goes as a bitmap; AppKit
  sets it at every pointer motion, so the same image is no longer copied into a new shared-memory buffer each time
  (0145w). Observed (`PD_CURSOR_TRACE`, virtual-seat pointer): installed: Terminal only the arrow; c17: Terminal its
  I-beam image once, Activity Monitor DEFAULT then EW_RESIZE over the column divider.
- **FX-010** (nytimes.com's web-font text garbled: "Busness", "Mope to pead"): Onyx2D's glyph bitmap cache keyed the
  rendered glyphs by the FT_Face's address, size and glyph number, and was never told when a face was freed. Firefox
  makes and frees a face for each subset of a web font (nyt-franklin's unicode-range parts); a new subset's face made at
  a freed one's address was given the old subset's bitmaps for the same glyph numbers. 0146: the key also holds a
  number the face is given the first time it is seen (FreeType's client slot, `face->generic.data`, unused here), so a
  reused address is a new face. Found by reading; CODEX-FX's nytimes.com run is the proof (asked).
- **FX-004** (Firefox's crash reporter could not show its window: `+[NSTextField wrappingLabelWithString:]`
  unrecognised): 0147 adds the 10.12 constructors (NSTextField label/wrapping label/attributed label/text field;
  NSButton push button with title and/or image, check box, radio button).
- **Test prefixes and AppKit's resources**: a candidate's AppKit run from a test prefix sometimes had no bundle
  (`NSBundle bundleForClass:` gave the prefix directory: no key bindings, no check box or radio images; c17's visual
  check showed Terminal's Preferences without them). The kernel names a hard-linked file by whichever of its names was
  looked up last and dyld takes that name for the image; `stage.sh` had hard-linked the binary into the framework
  directories and kept it bare at the prefix's top. It now writes one file per framework (`Versions/C` a link to `A`):
  observed (`probe/bundlepath.m`) the bundle and `StandardKeyBindings.keyBindings` found. Earlier candidate runs may
  have run without AppKit's resources at times; the installed files never did (they are in place).
- **FX-001** (about:support "Display0 2561x1601@0Hz"): the system frameworks report the screen as 1707x1067 points at
  scale 1.5 (the compositor's integer logical size, 1706.67 rounded up) and the display mode as 1707x1067 at 60 Hz;
  1707 x 1.5 = 2560.5, Gecko's 2561. The panel's pixels (2560x1600) are not passed on. Not fixed yet; 0 Hz not
  reproduced with the system CoreGraphics.

### Performance pass (item 4; on the M1, my own instances, owner idle unless said)
Figures from runs judged by DESK7's real-input counters before and after (unchanged = the owner was idle).
- Activity Monitor, launch to its first commit (`m1-am-launch.py`, fresh home, 3 runs each, installed then c14):
  1588 (cold), 484, 477 ms and 663, 570, 484 ms; to the fifth commit (window, toolbar, the table's first fill) 866-1970
  and 865-1056 ms.
- Activity Monitor at its 5 s interval, no input, four instances side by side for 6 minutes (`m1-measure9.sh`): CPU 4.0
  to 5.1 % of one core (SF-AA's seventh round: 10.6-14.7 %, sixth 13 %); RSS +0.76 to +0.80 MB/min between 120 and
  210 s, then flat (95 MB installed, 89 MB with AppKit 0140).
- Column drag (`colres.sh`, TERM6's method: the divider right of "% CPU" 200 px right in 1 s and back, then 400 px in
  0.25 s; synthetic on the virtual seat; installed and c14 alternating, two each): motion -> frame median 35.3 / 33.9 ms
  installed, 33.9 / 34.1 ms c14 (2.0-2.1 frames; p95 52-60 ms); AppKit taking a position to its commit 21.1-21.8 ms;
  Activity Monitor CPU over the drags 21-22 %. Not at one frame: the step itself (cells of the column redrawn and laid
  out, the commit) takes longer than a frame. 0140's ordered objective costs nothing measurable here.
- Idle (CODEX-TB's owner-guarded 60 s sample, 21:14Z, kernel CIO29): machine 98.3 % idle, kernel 2.2 % of one CPU,
  the compositor 3.4 % (its shared-memory drop path 2.0 %).
- Terminal's figures (launch, echo, `seq 1 300000`, wheel) on the candidate were stopped when the owner came at 22:08Z;
  the figures of 20:18Z were discarded for the same reason: see the install section below for the repeat.

## TERM13 (17 November, ~06:10-06:50Z): the render thread installed at the owner's order
The owner ordered "Install them both" (CODEX-FX's damage-only path, installed 06:00Z, and TERM12's render thread) and
will judge by hand; only short safety checks, no frame-rate series. 0178 (QuartzCore render thread + transaction gate),
0178a (AppKit presenter), 0178w (libWindowServer present from that thread) are now in patched.nix's lists, unchanged
from TERM12. 0179 (painter's order) is NOT included: 0178 builds and behaved without it. 0180a/0180w stay held.
- Build: the committed series without the three reproduced the installed t11c byte for byte (AppKit 2e5a5266,
  libWindowServer c2cdfcf8, QuartzCore.pd 3c6d9530); with them: AppKit 6a2dd27a, libWindowServer 752999d3, QuartzCore.pd
  ab4f4daf. TERM12 had tested 0178 only together with 0179; these exact QuartzCore/libWindowServer files are new.
- Checks before installing (candidate prefix /private/var/db/sft13/c1; synthetic input on the virtual seat into my own
  windows only; own fresh profiles/homes; ~16 min of holds in all, one rerun included):
  Activity Monitor and Terminal with the candidate files and no other env: launch, paint (one capture each, looked at),
  Command-Q, EXIT=0, no exception, no crash report; control with the installed files the same. (First attempt: my
  Terminal had /bin/cat as shell, so Command-Q raised Terminal's own "terminate running processes" alert; my Activity
  Monitor stayed up 6.5 s after Command-Q and its capture came back empty - not reproduced in the repeat, cause not
  found.) Terminal random input (TERM9's fuzz, /bin/cat shell, MallocScribble=1, 140 s): 22 bursts / 264 actions, alive,
  no fault. `sf_compat_push.sh check` 18/18. Own Firefox (`-no-remote`, MOZ_CRASHREPORTER_DISABLE=1, the installed
  launcher's plan = SFFX_GPU_PRESENT/DAMAGE/IMAGE_CACHE=1 etc., plus SFFX_GPU_RENDER_THREAD=1, candidate prefix first):
  3 rounds of en.wikipedia.org/wiki/Firefox loaded and painted (captures looked at), wheel scroll, menu panel open/closed,
  new tab/close, second window/close, Command-W on the last window (windowless), Quit x3 (2 reopen + Command-Q, 1
  windowless desk Quit; parent gone in 2.5-3.5 s): 32/32 steps, 0 new crash reports, 0 log faults, family gone.
  sf-sample: the CARenderServer thread present, AppKit/libWindowServer/QuartzCore.pd loaded from the candidate prefix.
  The first Firefox run failed "Command-W on the last window" 3/3: a tool fault (the second window went at the key-down,
  its Command/W key-ups had no target, the virtual seat kept them held and dropped the next Command-W as "pressed again
  while held"); the tool now lets them go first; the rerun passed.
- Installed 06:34Z, direct: each file written beside its target, SHA-256 checked there before anything ran it, the file
  of before kept as `<path>.pre-t13` (= t11c), renamed into place. Paths: /System/Library/Frameworks/AppKit.framework/
  Versions/A/AppKit, /usr/lib/libWindowServer.dylib, /System/Library/Frameworks/QuartzCore.framework/Versions/A/
  QuartzCore.pd. After: my own Activity Monitor and Terminal on the installed files launch/paint/quit (EXIT=0), compat
  18/18. Nothing restarted: the owner's running Firefox, Terminal and Activity Monitor keep the old files until he
  restarts them; no kernel, no compositor relaunch.
- Without SFFX_GPU_RENDER_THREAD=1 the new code does nothing (checked: Activity Monitor/Terminal above).
- Launcher (CODEX-FX, at TERM13's request, 06:38:44Z): the installed `firefox-sf` now sets SFFX_GPU_RENDER_THREAD=1 with
  SFFX_GPU_PRESENT/DAMAGE/IMAGE_CACHE=1 (launcher SHA-256 412689a2..., manifest 9b13046c...; read back by TERM13 from the
  launcher's own plan). CODEX-FX's own-profile smoke passed (page, scroll, window, tab, Command-W on the last window,
  windowless Quit; 0 reports). Off switch: `python3 /usr/local/lib/siliconforest/firefox/160-system1/maintenance-render-thread-t13-v1/install-damage-path.py --render-thread off --apply`
  (the launcher/manifest of before are saved beside it). The owner's Firefox picks the switch up when he restarts it.
- Revert the three files: `ln -f <path>.pre-t13 <path>.back && mv -f <path>.back <path>` for each path above (programs
  started afterwards get t11c; switch the launcher off first, or the env is set with nothing to read it).
- Not done: the 12-run frame-rate series (deliberately not run); TERM12's lifecycle items maximise/restore and full
  screen (not in this smoke); video; any real (physical) input: all input here was synthetic.
- Tools: runs/sf-term13 (not committed): `app13.sh`, `fuzz13.sh` (TERM9's with my prefix and window lists instead of
  picture dumps), `m1/life13.py` (TERM12's life12 trimmed to the owner's list, real page), `install13.sh`.

## TERM12 round (16 November, afternoon): Firefox frames off the main thread, video tearing, position:fixed, CSD
Nothing installed this round: the M1 still runs t11c (AppKit 2e5a5266, libWindowServer c2cdfcf8, QuartzCore.pd 3c6d9530);
the committed series still builds exactly those. The new patches are in `integrations/puredarwin/cocoa/patches` but HELD
(listed in patched.nix's header, not in its lists). Tools: `integrations/puredarwin/firefox/perf/tools/term12/` (copies
of runs/sf-term12/tools). Own Firefox only (`-no-remote`, copy of a 93 MB warm profile, deleted after each run),
synthetic input on the virtual seat into my own windows only.

### Per-frame profile (sf-sample of the parent and GPU process, damage path + CGL 0303 + CoreVideo 0172, 8 s at ~3.4 ms)
- Warm profile: parent main thread busy 25 / 53 / 65 % of wall (anim / wheel scroll / 1080p60 video); our frame block
  (`-[CALayerContext _scheduleRender]` block on the main queue) 22 / 44 / 62 %: export glFinish (drmSyncobjWait) 18-20 %,
  WS sfGPUCompose+glFinish 2-11 %, CARenderer 1.5 / 7 / 25 % (video: `uploadPlane` CPU copy of the NV12 planes 20 %).
  Gecko's own main-thread work 3-10 %. GPU process light (Renderer 7-14 %).
- Fresh profile (TERM11's method): main thread 90-98 % busy, 64-72 % of it Gecko's first-run JS; frames wait behind it.
  Most of TERM11's 50-55 shown/s came from fresh profiles (CODEX-FX's warm-profile control: ~59.5 on the old path).

### 0178 / 0178a / 0178w: frames on a render thread (opt-in `SFFX_GPU_RENDER_THREAD=1`, with SFFX_GPU_PRESENT=1)
- After a main-thread GPU present AppKit gives QuartzCore a presenter (geometry snapshot + window-server geometry
  serial); every committed transaction then renders, exports, registers and presents on the "CARenderServer" thread.
  The main thread does a frame when a layer needs display, AppKit geometry changed (setFrame:/setLayer:/invalidate), or
  the window server refuses (`_windowServerSFFXPresentLayers`: serial moved by a new backing store, role, state/level/
  style change or any xdg configure). One process-wide frame lock; main-thread frames of other contexts that arrive
  during a render-thread frame are re-queued after it instead of blocking. Windows off the screen are not rendered;
  a popup context is asked for a render by other windows' commits only when its own surfaces changed.
- Transaction gate (CATransaction.m): renders and explicit transactions exclude each other; a render waits at most
  20 ms and then does NOT render (fail closed; CODEX-FX's literal-host contract and my host test pass, host only).
  Implicit (AppKit main-thread) changes are not gated (memory safety as before 0178: locked swaps, retained reads).
- Measured (own Firefox, synthetic): main thread out of the frame path (sf-sample: no CA/GL frames on main in anim and
  video); render-thread work p50 1.9-3.5 ms; it presents 60/s (ws presents: render-thread=300 per 5 s, flush=0).
  Presented (desk WMPACING, steady 10 s windows): r16 (full gate) anim 55.5-59.0, scroll 59.3-59.6 (2 steady windows
  x 3 runs), video 55.8-59.5; 0 of 9 runs >= 59.5 in both windows. The desk shows fewer than we commit: commits
  59.6-60.0/s, replaced_before_shown 0.4-10/s, commit->flip p50 22-31 ms. Delaying our present by 0/4/8 ms after
  Firefox's commit (r19, 2 runs each) changed nothing; a phase lock (render half a refresh away from the commits) was
  worse (removed). NOT 59.5 x 12: the remaining loss is in when the desk takes commits (replaced before shown), not in
  the parent main thread. Lifecycle x5 (r13): 85 steps, no crash report, no log fault, quit 5/5; "restore after
  maximise" fails 4/5, the INSTALLED t11c fails it too (2/4 runs): pre-existing.
- Tearing (item 2): a per-frame column record (PD_CA_DUMP_STRIP) on a page of 18 looping muted videos with grey
  borders, wheel scroll: the old main-thread path showed a video picture shifted ~8 rows against its own border in 10
  of 733 moving frames (3 runs, page still loading); render thread + gate: 0 of ~1200 moving frames (7 runs, settled
  and loading, conditions not identical). Also found: Firefox's title changes bumped the serial (fixed: title-only no).

### 0179: painter's order, contents clamped to edge (FXPERF2's position:fixed defect)
CARenderer drew each sublayer "a step nearer than its parent" with the depth test on, so a later sibling was hidden
where an earlier sibling's deeper-nested layers had drawn. FXPERF2's pages: the fixed magenta marker is MISSING with the
old order (still.html, nested.html; present on scrolled.html) and PRESENT with 0179 on all three (one capture each,
looked at). Contents now use GL_CLAMP_TO_EDGE (GL_REPEAT pulled the opposite edge into linear samples at fractional
positions). PD_CA_OLD_ORDER=1 restores the old behaviour for A/B.

### 0180a / 0180w (FX-002, tabs in the title bar): started, not working yet
A window with a full-size content view, transparent title bar and hidden title (Firefox with inTitlebar=1) asks the
desk for client-side decorations at run time; no menu strip in it (menus on right/Control-click of the buttons' row;
DESK asked to offer them from the dock tile). First capture (r18, own Firefox, inTitlebar=1): no desk title bar, no
menu strip, Firefox's tab strip at the window top (two chrome rows as on macOS) - but the three buttons are NOT shown
(the WS overlay or Gecko's `_closeButtonOrigin` placement still to debug). Installed t11c with inTitlebar=1 still has
the desk title bar + menu strip over the tab strip.

### Not done
Tab switch (FX-030) not measured (tool tabs12.py ready); NV12 GPU planes: CODEX's private QC caller (5d24e79c3) not yet
adopted, my inert version held as `term12/held-nv12-planes-on-gpu.patch`; item 5 (image clipboard, Terminal/AM
momentum, touch-to-stop, FX-027, COLR) untouched.

### Next steps (in order)
1. Desk pacing: with presents at 60/s from the render thread, find why the desk replaces 1-10/s (commit->flip 22-31 ms;
   needs the desk's latch timing per commit) - a compositor question.
2. Gate 0178/0178a/0178w+0179 for install: lifecycle x5, visual (done once, r13: same), AM/Terminal random input, compat,
   tearing record x12, position:fixed x3 pages x3; then add to patched.nix and install with `.pre-t12`; launcher env
   SFFX_GPU_RENDER_THREAD=1 (+SFFX_GPU_DAMAGE=1 SFFX_GPU_IMAGE_CACHE=1, CGL 0303) is CODEX-FX's.
3. 0180a: draw the buttons (check `_windowServerSFFXSetOverlay` is reached and the base pixels under the buttons).
4. Tab switch: `tabs12.py` installed vs candidate, then a Gecko profile of the switch.
Commands: edit trees runs/sf-term12/{ak,ws,qc}/b (a = series, m = after 0178a/0178w); `tools/mkp.sh`/`mkp2.sh` make the
patches; `nix-build patched.nix -A appkit|windowserver|quartzcore` (with the held patches added to the lists);
`tools/stage.sh <name> ak=.. ws=.. qc=.. cv=.. gl=..` stages a prefix on the M1; `tools/runm1.py <min> "<title>"
"pace12.py <name> <prefix> anim|scroll|video <runs> KEY=VAL,.."` (also prof12/life12/vid12/cap12/tabs12);
`paceset.py <tag> <prefix> 12 <env>`; `vscheck.py`/`stripcheck.py` for the strip records.

## TERM11 round (16 November, day): Firefox's menu panel, clipboard, Quit, display link
Tools in `runs/sf-term11` (not committed): `tools/t11_m1.py` (TERM10's launcher helper, my area /private/var/db/sft11),
`tools/pop11.py` (menu panel x N with Marionette panel state, hover, History, New Tab, doorhangers, permission prompt),
`tools/clip11.py` (two own Firefox, Command-C/V both ways, HTML), `tools/quit11.py` (Command-W, then the desk's app
Quit to the Firefox parent), `tools/pace11.py` (WMPACING shown/s, PD_CV_TRACE ticks, PD_WS_TIMING), `stage.sh`
(prefix with AppKit Versions/A and C, libWindowServer, QuartzCore.pd, CoreVideo). Synthetic input only, on the
virtual seat, into windows that appeared after my own launch; own fresh profiles, deleted after each run.

### FX-022: Firefox's menu panel (causes found by reading Gecko 160's widget/cocoa against our AppKit)
- `-orderWindow:relativeTo:` posted NSWindowWillMoveNotification around every order; Gecko's `-windowWillMove:` rolls
  popups up, and the panel is shown by `-addChildWindow:ordered:` (PopupLevel::Parent): it closed 26-30 ms after it
  opened (CODEX-FX's measurement). 0163: no move notifications on order.
- The popup grab moved the keyboard to the popup surface and libWindowServer told AppKit the browser window lost key
  (Gecko rolls up in `-windowDidResignKey:`). 0163w: our own popup taking the keyboard is no loss of key (a leave is told
  only if no enter on a window of ours follows within 40 ms); key events for a window that cannot be key go to the key
  window (Escape and arrows reach Firefox's popup manager, as on macOS).
- Gecko lowers a closing popup to level 8 and paints it after hiding it; a flush of the hidden window made
  libWindowServer map it again (TERM10's dark toplevel). 0165: a window off the screen sends no flush; 0163w: frameless
  child windows and frameless former popups are popups at any level.
- The panel then stayed open but was invisible: the popup's content view had a layer whose tree did not contain
  Firefox's ChildView layers (read back empty, 0/439936 pixels). 0170: a view with a layer that moves under another view
  puts its layer into the superview's layer; a removed view's layer leaves it; orphans are put back before a render.
- No hover: the pointer entering the panel brought no move. 0173w: pointer enter is a move there.
- Result in my own Firefox (candidate, synthetic input): 20/20 rounds open at the button, stay, painted, the item under
  the pointer highlights, Escape closes, window alive, 0 compositor protocol errors; History subview opens; New Tab acts;
  downloads, extensions, site permissions, tracking protection and bookmark panels and the geolocation prompt open at
  their anchors and close (captures looked at).
- Installed t11a 11:47Z, REVERTED 12:05Z: with it Firefox crashed when a window or panel closed (SIGSEGV in
  `-[CALayerContext _scheduleRender]`'s block): a view could get a second layer context replacing the first without
  invalidating it. 0175: a view keeps its context.

### Clipboard (DESK10's 0 bytes)
Another client's paste request was answered on Firefox's main thread from the board as it was then; the installed
files answered 0 bytes (reproduced, cl7a). Three causes, three fixes: 0168 answers from the data offered, on a thread
of its own (lazily provided data still through the main thread); 0176 data written after its type was offered goes
into that at once (Firefox's offer can go out before its data); 0177 within a second of a write, another client's
selection does not replace the board before the write's offer is out (the desk's clipboard keeper re-offers the old
copy: the offer then carried the other client's types and no data, a lone BOM on the other side). Result, two own
Firefox instances, synthetic Command-C/V: text A->B, B->A and HTML (bold/italic kept) 3/3 runs (cl12a-c); installed
t11c re-test below. Images: not tested.

### Windowless Quit (DESK9/CODEX-FX)
Gecko replaces `-terminate:` by a notification only and quits itself from `-applicationShouldTerminate:`; the desk's
Quit called `-terminate:`. 0164/0169: as macOS's quit Apple event, the delegate is asked first, once (AppKit's own
`-terminate:` does not ask it again). Command-W, then the desk's Quit to the Firefox parent (`app <pid> quit`): natural
exit 2.6-3.3 s, family gone, 3/3 candidate and 3/3 installed t11b; t10s7: answered ok, alive after 12 s (2/2).

### Trackpad momentum (owner: a hard flick coasted ~7 screens)
0166w: release speed capped at 6000 points/s, decay 0.95 per 16.7 ms by elapsed time. Synthetic touchswipe 50 mm into
my Firefox: 150 mm/s 2113 CSS px (old 2063); 440 mm/s 4123 px 2/2 (old 9273 px 2/2). Firefox scrolls by pixels with a
coast (no line steps). Touch-to-stop needs a contact signal from the driver/desk (asked of DESK10: a zero finger frame
at touch-down; the client already ends the coast at any finger frame).

### Presented frame rate (FXPERF2's bistable ~30/s)
Firefox's display link runs in its GPU process (PD_CV_TRACE=<dir> writes every tick to <dir>/cvtick-<pid>.txt). The
installed 0149 link waits for a pending frame's flip (up to two refreshes) and then ticks every second refresh. 0172:
one tick per refresh on the display's phase, the thread under THREAD_TIME_CONSTRAINT_POLICY: 58.8-59.8 ticks/s in 6/6
runs (CSS animation, wheel scroll, 1080p60; p10/p50/p90 16.6/16.7/16.7 ms), no half-rate run in 12; presented
50-55 frames/s: Firefox produces fewer frames than ticks (parent main-thread cost). The link Firefox loads is CODEX-FX's
video-shared CoreVideo (built from `corevideo-displaylink.nix`, which applies 0172): asked to rebuild and select it.
A first 0172 that re-phased on flip reports skipped refreshes (34 ticks/s): discarded. Damage path still opt-in.

### Installs (direct, the file of before kept as `<path>.pre-t11` = t10s7, hashed on the M1 before anything ran it)
- t11a 11:47Z (AppKit 671aec54, libWindowServer c2cdfcf8, QuartzCore.pd 3c6d9530 with FXPERF2's 0306): REVERTED 12:05Z
  (Firefox crashed when a window or panel closed; t11a kept as `<path>.t11a`).
- t11b ~12:50Z: AppKit 84fc8d2b (+0175). Gates before: visual = installed, AM 252 / Terminal 228 actions alive, compat
  18/18; after: the same. Installed: menu 20/20, all doorhangers and the permission prompt, 0 protocol errors.
- t11c ~13:35Z: AppKit 2e5a5266 (+0176/0177 clipboard); libWindowServer and QuartzCore.pd as t11b. Gates before: menu
  6/6, visual = installed, AM 252 / Terminal 228 alive, compat 18/18.
Revert: `ln -f <path>.pre-t11 <path>.back && mv -f <path>.back <path>` for AppKit, /usr/lib/libWindowServer.dylib,
QuartzCore.pd.

### Not done in this round
Reddit video-scroll line artefacts (item 3; CODEX withdrew the blank-video claim: capture artefact); FXPERF2's missing
position:fixed marker on pages with rounded/rotated boxes (filed; CARenderer clip/stencil suspected, not examined);
damage path default-on (held by the coordinator until 59.5+ presented in 12/12); Terminal/Activity Monitor momentum
not measured; FX-002 client-side decorations, FX-027 panel placement, COLR emoji.

## TERM10 round (16 November, early morning): Firefox's stale tiles, its menu panel, trackpad scrolling
Tools in `runs/sf-term10` (not committed): `tools/t10_m1.py` (FXLEAD's launcher helper, my own area
/private/var/db/sft10), `tools/ab.py` (sites + five Page Down), `tools/popt.py`, `tools/popstress.py`, `tools/scrollt.py`,
`tools/emojit*.py`; TERM9's gates (visgate/fuzz/compat) through `vis10.sh`, `gate10.sh`. Synthetic input only, on the
virtual seat, to my own windows; a window counts as mine only if it appeared after my own launch (one run at 04:43Z
took another Firefox window by its title and sent it 3 Shift + 5 Page Down: logged; the rule above since).

### FX-011/012: stale or foreign tiles (cause found, fixed, installed)
Cause (QuartzCore, 0157): CARenderer gave layers their texture names in a pass before it drew, while Firefox's
compositor thread changes the tree at the same time; a layer added between the two (a new tile, or one taken out and
put back) was drawn with texture name 0: it bound none, so its IOSurface was bound into the texture of the layer drawn
before it, and that layer showed this other surface until its own contents changed - a static chrome tile kept a page
tile or an uninitialised surface (FX-012's stripes) for good. With `PD_CA_DIAG=1` (each frame: layers met without a
name, layers whose texture holds another surface than their own) and `PD_CA_OLD=1` (the old naming) on my own Firefox:
old naming wiki 69 / GitHub 59 frames with a wrong layer, captures wrong; 0157: 2-15 unnamed layers met per run, 0 wrong
frames, in 12 of 12 runs (wiki, BBC, Reddit, GitHub code x 3; captures looked at). 0157 also: the render-scheduled
flag as a full-barrier atomic, texture names swapped under the tree lock, nested transactions inherit their outer
values, an implicit transaction is committed on its own thread. Installed 05:30Z: QuartzCore.pd 432fa011
(`.pre-t10` = c19's 95e0c621). Gates before (with libWindowServer 3170eacc in the prefix) and after: visual check looked at
(data only differs), random input alive (AM 180 / Terminal 168 actions after), compat 18/18. Revert:
`ln -f <path>.pre-t10 <path>.back && mv -f <path>.back <path>`.

### FX-022: Firefox's menu panel (≡)
Found on the way, each in my own Firefox with PD_WS_TRACE (synthetic clicks at the ≡ button, Escape after each):
- The desk reads popup and layer-surface buffers as shared memory only (`popups.rs`/`layers.rs` `import_shm`); the
  panel's window has GPU layers, so libWindowServer attached linux-dmabuf buffers: `WMPOPUP refused buffer: SHM access
  failed: NotManaged`. 0158w: a window that is or will be a popup/layer surface presents through shm; 0158 (AppKit) asks
  `_windowServerSFFXTakesGPULayers` and reads such a window's layers back without copying a GPU frame first.
- `-[NSWindow addChildWindow:ordered:]` made the child key (`makeKeyAndOrderFront:`). Firefox attaches the panel as a
  child of the browser window right after showing it, so the browser window resigned key and Firefox rolled the panel
  up at once (CODEX-FX's "showing, hiding before shown"); the hidden panel window was then flushed at level 8, made a
  toplevel, focused by the desk and unmapped again, and the next click died on `wrong configure serial` (seen here at
  06:0xZ with the installed files and with 0158w). 0162: a child is ordered above its visible parent and not made key;
  -removeChildWindow: only detaches (macOS). 0158w also: a menu-level window finds a parent in the active or any
  mapped window of the application before it is made a toplevel, and drops a toplevel role when hidden.
- Also learnt: my first traces clicked 32 px too high (the tab strip's "List all tabs" button, also a panel with the
  same show-then-hide); the ≡ button is 128 px below the window content's top at 150 %.

### Trackpad scrolling ("I have to scroll a lot")
The trackpad reached the desk as wheel steps (the trackpad child's libinput emulation: one step per 3.6 mm of
two-finger travel) and libWindowServer made a step one line: 19 points in Firefox, 5.3 points per mm at any speed.
DESK10's desk10-001 (from the next compositor start) sends it as axis source "finger" in points with the desk's
acceleration and scroll speed, and axis_stop at lift (its contract: requests-for-TERM5.md 06:05Z). 0159w (rewritten
to that contract; a first version that turned wheel steps into points is withdrawn): finger frames are precise
events for every application (before: Firefox only), began/changed/ended, then a momentum phase from the release
speed (last 100 ms of moving frames, 3+ frames, >300 pt/s; 0.95 a refresh; a finger frame or a button ends it); mouse
wheels stay lines. 0159: NSEvent -momentumPhase. 0161: Cocoa scroll views move by whole points (fractional offsets drew
Activity Monitor's rows over each other in a visual check). Measured in my own Firefox with the inject's finger frames
(30 frames 8 ms apart of 6 / 15 / 30 points, then `scroll stop`): installed 180 / 450 / 900 CSS px (the frames' sum, no
momentum); candidate 283 / 791 / 1545 px. Not felt by a hand yet; the wheel-step path the trackpad uses until desk10
runs is unchanged (lines).

### Other items
- FX-024 (emoji boxes): not reproduced at 05:4xZ: emoji render as Noto Emoji/DejaVu outlines on my page and on FXQA's
  forms.html, with and without the launcher helper's FONTCONFIG_FILE; the CoreText probe (`runs/sf-term10/probe`) finds
  Noto Emoji, its cmap/glyf through CGFont, the same with c20's and the pre-c20 Onyx2D.
- Tab strip half under the menu strip: with tabs in the title bar Firefox makes its ChildView the whole window frame
  (960 pt in AppKit's 938 pt content view, the 22 pt strip over its top); CODEX-FX asked to default
  browser.tabs.inTitlebar=0 until FX-002 (client-side decorations) exists.
- FX-027: Escape cancels the modal open panel (TERM9's probe returned Cancel); AppKit centres it, the desk places it
  lower right: DESK9 asked to honour the app's position for panels.
- FX-003: SystemConfiguration rebuild with 0152 requested of the root owner (requests-for-CLAUDE.md).
- FX-029: 0160/0160a (unchanged layer properties render nothing; opt-in SFFX_GPU_SKIP_UNCHANGED reuses the frame for a
  bare commit and presents nothing); PD_CA_DIAG counts: on GitHub without damage 58 of 134 renders per 5 s had
  unchanged layers and seeds. FXPERF2's damage-only frames (0300, 0301 adapted, 0302w; opt-in SFFX_GPU_DAMAGE=1)
  adopted into the series; with t10s7 my own Firefox showed 0 frames with a foreign surface with and without it
  (wiki, GitHub; captures looked at).

### Candidates and gates (second set)
t10s7 = AppKit 7aa13533 (0158, 0159, 0160a, 0161, 0162, 0301), libWindowServer 821dc8c4 (0158w, 0159w, 0302w),
QuartzCore.pd 2c6d0b74 (0157, 0160, 0300). Gates before: visual check looked at (data only), Activity Monitor random input
228 actions alive, compat 18/18; the Terminal pass twice did no actions (the owner active; then an empty inject
request.part from my own stalled pass blocked all injects - removed). t10s6 (the same without FXPERF2's patches) passed
all: AM 240 / Terminal 240 actions alive, compat 18/18. Found on the way: the M1's root filesystem was full (921 MB
free, captures failing); my 47 test profiles (~3.5 GB) removed. At 07:4xZ the M1 went to Linux for CODEX-TB's window;
the install of t10s7 waited for SiliconForest (`runs/sf-term10/install-b.sh`).
Installed 08:08Z (kernel back, desk10-next-003 running since 07:04Z): AppKit 7aa13533 (`.pre-t10b` = c19's 56099510),
/usr/lib/libWindowServer.dylib 821dc8c4 (`.pre-t10b` = 7133efed), QuartzCore.pd 2c6d0b74 (`.pre-t10b` = 432fa011,
`.pre-t10` = 95e0c621). The Terminal gate before ran at 07:5xZ: 228 actions alive. After: visual check: Terminal's
menu and tab crops differ from the 05:59Z reference by |diff| 62.7 / 10.9, but the old files (prefix c19) on the same
compositor give the same picture (vi10b vs vo10: 0.00 / 0.01): the desk's new geometry, not these files (DESK10
told). FX-022 on desk10 with these files: 4 of 4 ≡ clicks, window kept (no protocol error), but the panel is shown as
a separate dark toplevel at the right of the screen (the level-8 re-show), not as a menu: NOT fixed.


## TERM9 round (16 November, night): Firefox on the system frameworks
Tools in `runs/sf-term9` (not committed; TERM8's gates with `target` lines for DESK8's bound keys). Synthetic input only,
on DESK8's virtual seat, to my own windows. Firefox checks use my own instance of the INSTALLED app (its launcher's
plan, own fresh profile under /private/var/db/sft8/ff), never the owner's.

### One set of frameworks for Firefox (item 1)
The installed Firefox loaded CODEX-FX's private AppKit/QuartzCore/libWindowServer/CoreText/Onyx2D. Its Firefox-only Cocoa
patches are now in the main series (numbers kept; regenerated after 0147): 0206 (class geometry counts the menu strip),
0208/0213/0216/0219q (QuartzCore GPU frame export, lookups, reuse, its own 0219), 0209 (AppKit registers GPU layers; the
presenter is looked up once and never called without SFFX_GPU_PRESENT=1), 0209w (WindowServer whole-window GPU
presenter, `sffx_gpu_present.h`), and the IOSurface transport as 0230-0233 (`iosurface.nix`: per-session broker,
dma-buf storage, NV12 planes, FXLEAD's lookup fix) with 0151 (libdrm dlopen()ed on the first GPU surface: the private
copy linked @rpath/libdrm, which in the system file would have stopped every program that loads IOSurface). Every
GPU piece acts only with the launcher's SFFX_* variables. CODEX-FX switched the installed launcher to the system
AppKit/Onyx2D/CoreText/QuartzCore/libWindowServer at 01:04Z (its parent and GPU image lists checked); the launcher's
plan now has no private copy of those five (DYLD paths: IOSurface `surface-lookup-v1`, AVFoundation, gl, compat stubs,
sandbox/io interposers remain; CODEX-FX's prepared switch drops IOSurface and gl next).

### Frame pacing (item 2)
0148w: libWindowServer asks, for every frame it commits, the flip that shows it: wp_presentation feedback for a GPU
(dma-buf) frame, a wl_surface frame callback for the others (DESK8's desk answers wp_presentation only for dma-buf
commits); the latest flip in host time, the measured refresh and the commits in flight are the display clock
(`_windowServerDisplayClock`, `_windowServerDisplayClockWait`; at most 32 questions open, written off after 1 s).
0149 (CoreVideo, CODEX's pd-displaylink build with only PD's CVDisplayLink.m changed: `cocoa/corevideo-displaylink.nix`):
the display link ticks right after the flip that showed the app's last frame (inNow = flip, inOutputTime = + refresh),
waits for a frame in flight up to two refreshes, else ticks on the display's phase; PD_CV_TIMER=1 = the old timer.
My probe (`probe/pacing.m`, an SHM window redrawn from the link, 6 s, no input): installed (timer) 59.8 ticks/s, tick
interval p95 19.8 ms, no flips known; c19 58.7 flips/s reported, p95 16.7 ms, but 70 ticks/s: a flip the timer had
already predicted ticked again - fixed (CoreVideo f62ffb61; not re-measured before the install). Firefox with c19:
the desk's WMPACING shows its dma-buf commits (example.com 50 commits/49 shown, about:support 56/55, idle pages); the
60 fps scroll/video acceptance is CODEX-FX's/DESK8's measurement on these files.
FX-001: about:support's "@0Hz" is Firefox's own on macOS (XUL imports no refresh-rate API; its vsync rate comes from
CVDisplayLinkGetNominalOutputVideoRefreshPeriod, 60 Hz here). 2561x1601 = 1707 pt x 1.5: the desk's logical output
size is an integer (asked of DESK8 whether 1706 would do); not changed.

### Installed c19 (01:57Z, direct, the file of before kept as `<path>.pre-t9`)
AppKit 56099510, QuartzCore.pd 95e0c621, /usr/lib/libWindowServer.dylib 7133efed, CoreVideo f62ffb61, IOSurface
323e14c5, new /usr/libexec/iosurfaced d84411d1 (= the Firefox runtime's own), new /System/Library/Fonts/NotoThai (Noto
Sans Thai regular/bold, Noto Serif Thai; OFL 1.1, licence beside them; `cocoa/fonts-thai.nix`). Also in it: 0150
(FX-023: NSEvent timestamps are seconds since start-up, mach_absolute_time, not since 2001) and FXLEAD's 0153 (FX-036:
-[CALayer display] asks a view that wants -updateLayer for it; Firefox ends its commit suspension there: the stale
first "New Tab" picture). Gates before: visual check installed vs c19 (looked at: same but the figures), Activity
Monitor 3 minutes with the layout cache forced small, random input Activity Monitor 240 / Terminal 216 actions
(MallocScribble, cache forced small): alive, no report; compat 18/18. After: visual check of the installed files
(looked at). Firefox (my own, installed app, 02:1xZ): my local page, example.com and about:support painted (captures
looked at), the Thai line renders, FXWSPRESENT transport=dmabuf 240 frames 0 failures, 261 presentation feedbacks.
Not shown yet: FX-022 (a Terms-of-Use modal of the fresh profile kept the menu from opening; repeat with c20).
Revert: `ln -f <path>.pre-t9 <path>.back && mv -f <path>.back <path>`; iosurfaced and NotoThai: remove.
Found on the way: with the c18 system files (no presenter) Firefox's blank page was not the glyph cache (0146): my
installed Firefox with the Onyx2D before 0146 showed the same stale picture.

### c20 (FX-011/012 and font memory)
FXLEAD's second QuartzCore patch as 0155 (an IOSurface that is a layer's contents is marked in use, as the window server
does on macOS; -dealloc releases the last contents): FXLEAD's A/B on the installed c19, Wikipedia/BBC/GitHub after five
Page Downs, stale/foreign tiles 3/3 -> correct 3/3. FXPERF's 0154 (Onyx2D maps an installed font file once per process:
Firefox's parent held ten private 30 MB copies of the CJK collection; footprint 921 -> 634 MB). QuartzCore.pd d2ecc6eb
(byte-identical to FXLEAD's build), Onyx2D 8dabe746.

Installed 03:11Z: Onyx2D 8dabe746 only (0154; `.pre-c20` kept = c18's ace5dc3d). FXLEAD withdrew 0155 at 02:21Z (its
repeat showed FX-011/012 unchanged with it; Firefox's parent counts the surface use itself): out of the series, not
installed; QuartzCore.pd stays c19's 95e0c621. Gates before: visual check (looked at; an empty Disk table in the
candidate's Activity Monitor was data timing: the installed capture after has it full), 3 minutes of late views, random
input Activity Monitor 240 / Terminal 216 actions alive; after: visual check of the installed files (looked at; a 3 px
shift of Terminal's window between the runs, the new compositor's placement), random input Activity Monitor 240 /
Terminal 228 actions alive, no report; `sf_compat_push.sh check` 18/18.
Not done: my second Firefox check (FX-022 with the Terms modal off, PD_CV_TIMER A/B, the CPU path, about:support's
Graphics, the keydown timestamp, a view-tree dump for the tab strip that sits half under the menu strip): after the
03:4xZ boot my SSH-started Firefox could not start any child (`child bootstrap_look_up failed: 0x44e`), so it had no
window; reported to CODEX-TB. Script ready: `runs/sf-term9/verify20.sh`.

### FX-029 (FXPERF): presents paced by frame callbacks, built for FXPERF's A/B
0156w (opt-in PD_WS_PACE_PRESENTS=1, off by default): a window presents at most once per wl_surface frame callback; a
flush while its last commit is undrawn marks the present pending and the callback presents the newest backing store
(a callback missing 100 ms holds nothing back). libWindowServer d0cea40f, staged in /private/var/db/sft8/c21; not
installed. Next on the same path: damage (render, compose and commit only the changed rectangle) - QuartzCore and
WindowServer, with CGL's EGLImage cache and fence (CODEX-FX) and the desk's damage use (DESK9).

### FX-003 (a crash report at every Firefox exit): cause, fix not built
Breakpad symbols: nsNetworkLinkService::Shutdown() calls CFRunLoopRemoveSource(mCFRunLoop = NULL): Init() gave up when
our SystemConfiguration's SCNetworkReachabilitySetCallback returned FALSE (PureDarwin's pd_sc_reachability.c answers
FALSE to SetCallback/Schedule/Unschedule), before it had its run loop. macOS returns TRUE. `cocoa/patches/0152` (TRUE;
no notification ever fires). Not built: the installed SystemConfiguration is the tip root's and the tip's Security does
not build here; for the root/rebase owner. Not a CoreFoundation fault.

## Performance: optimised core libraries (15 November, night; PERF)
PureDarwin builds every libSystem component and dyld with `CMAKE_BUILD_TYPE=Debug` and no `-O` of its own (CMake's
Debug flags are `-g`): -O0 code, each local stored to the stack and loaded again. Compile database of the default
recipe (`ninja -t compdb`) and the disassembly agree:

| Component | Before | Now (installed) |
|---|---|---|
| libmalloc, libc (stdio, stdlib, locale, string), libplatform, pthread, libdispatch, libxpc, corecrypto/CommonCrypto (`-DDEBUG`), libresolv, syslog, libsystem_trace: all inside `/usr/lib/libSystem.B.dylib` | -O0 -g, assertions on (no NDEBUG), `-fno-stack-protector` in libmalloc/libsystem_kernel | -O2 -g -fno-strict-aliasing, otherwise the same flags (build type still Debug, so -DDEBUG and the assertions are as before) |
| libplatform's string functions (arm64 uses the generic C, not assembly) | -O0; memset went through memset_pattern4, which called memmove for every 4 bytes | -O2 with -fno-builtin; memset/bzero/memset_pattern4/8/16 with 8-byte stores |
| libsystem_kernel | -O0 -g -momit-leaf-frame-pointer (mostly .s syscall stubs) | -O2, same flags |
| `/usr/lib/dyld` | -O0 -g NDEBUG -fptrauth-calls | -O2 -g; libobjc's forced load follows DYLD_LIBRARY_PATH |
| `/usr/lib/system/libdyld.dylib` | -O0 -g NDEBUG | unchanged (not replaced tonight) |
| libc++, libc++abi, libunwind | -Os NDEBUG | unchanged |
| `/usr/lib/libobjc.A.dylib` (PureDarwin's) | -Os -DNDEBUG, no -g; getenv("PD_POOL_TRACE") at every autorelease and every pool-pop release | -Os as before; PD_POOL_TRACE read once at _objc_init |
| libCoreFoundation | -O2 since COCOA4 | unchanged |

Apple builds these projects Release (-Os); that is from Apple's published projects, not from a file in this tree.
Frame pointers stay (arm64 Darwin keeps them; sf-prof walks them), symbols and the debug map stay (`-g`).

Recipes: `libsystem-default/build-libsystem-opt.nix` (the default recipe's source and patches, plus
`patches/string-functions-no-builtin.patch`, `patches/platform-memset-wide-stores.patch`,
`patches/dyld-libobjc-search-paths.patch`); `terminal-app/build-libsystem.nix` gives it (SF_LIBSYSTEM=debug: the
build of before); `terminal-app/build-libobjc.nix` (`patches/objc-pool-trace-read-once.patch`, in the tree;
SF_LIBOBJC=old leaves it out). The RAM root and the QEMU images still stage the default -O0 recipe (`libsystem-default/DEFAULT.txt`).

What could only work at -O0, and what was done about it:
- the C string functions (memmove, memset, memset_pattern*, strlen, ... in libplatform and libc's `string/`) could be
  compiled into calls to themselves once loops are recognised as library idioms: `-fno-builtin` for those sources.
  Checked on the binary: no new call cycles between functions (a script over both disassemblies), libSystem's exports
  and imports identical (2679 / 71).
- type punning in the FreeBSD libc, libxpc and PureDarwin's additions: `-fno-strict-aliasing` for the whole build,
  as for CoreFoundation.
- the PAC-lowering assumptions in dyld (`dyld-arm64e-keys-off.patch` signs a volatile probe value): unaffected;
  the kernel lowers whatever PAC forms -O2 emits.
- not changed, deliberately: NDEBUG/DEBUG (removing assertions is a separate step), libdyld.dylib (another core file,
  little in hot paths), libobjc's -Os (Apple's choice; objc_msgSend is assembly).
- libmalloc's configuration (`src/platform.h`): PureDarwin disables the nano allocator on arm64 (CONFIG_NANOZONE only
  for x86_64; nanov2 is a stub, `pd_nanov2_fallback.c`) and the medium allocator on arm64; tiny/small magazines
  follow the CPU count (8 on the M1). Not changed tonight: nano v2 needs Apple's OS-variant machinery and nano v1 a
  fixed region (0x600000000000) inside this kernel's user address space, neither established here.

PureDarwin's dyld loads `/usr/lib/libobjc.A.dylib` into every program itself with DYLD_LIBRARY_PATH switched off, so
no libobjc could be tried from a test directory (and the guarded installer could not prove one): the optimised dyld
lets that load follow DYLD_LIBRARY_PATH (`sf-dyld-probe` proves it).

### Rehearsals and installs
| Step | Result |
|---|---|
| QEMU pf1: libSystem 5d88cf7d (-O2 alone), dyld kept | FAILED, failed=1: the one failing check was sf-dyld-probe, which I had made every rehearsal require while this one kept the old dyld (45 other verdicts ok). Not installed |
| QEMU pf2 | the guest never got its command (sat at the prompt 10 min); stopped, nothing judged |
| QEMU pf2b: libSystem a715a1e2 (-O2 + memset) and dyld be18e963 | replaced as on the M1, COMPAT_VERDICT failed=0, QDDESK_PASS; fresh boot pf2b-2: failed=0, QDDESK_PASS |
| M1, 01:57Z: libSystem a715a1e2 | SFLIB 3 installed (ba8013eb is `/usr/lib/libSystem.B.dylib.old6`); new SSH connection ok; `sf_compat_push.sh check` 18/18; sf-claude-check as before |
| M1, 02:03Z: dyld be18e963 | SFDYLD 3 installed (472fd9a3 is `/usr/lib/dyld.old2`); new SSH ok; check 18/18; sf-claude-check as before; Terminal (Preferences twice, a new tab running zsh and brew) and Activity Monitor open (`runs/perf/shots/ia-sheet.png`) |
| QEMU pf3: libobjc 2eca1460 on the root pf2b left | FAILED, failed=1: that root carried the guest's one-shot marker (`/etc/compat-second-boot`), so nothing was replaced and sf-objc-probe met the old libobjc; my setup. Second boot stopped |
| QEMU pf3b: the same on a copy of that root without the marker (debugfs) | libobjc replaced through DYLD_LIBRARY_PATH, failed=0, QDDESK_PASS; fresh boot pf3b-2: failed=0, QDDESK_PASS |
| M1, 02:59Z: libobjc 2eca1460 | SFLIB 3 installed (PureDarwin's 6fe8dd67 is `/usr/lib/libobjc.A.dylib.old`); new SSH ok; check 18/18 before and after; sf-claude-check as before; window checks again (`runs/perf/shots/ib-sheet.png`) |

Revert: `ln /usr/lib/dyld.old2 /usr/lib/dyld.back && mv -f /usr/lib/dyld.back /usr/lib/dyld`, then
`ln /usr/lib/libSystem.B.dylib.old6 /usr/lib/libSystem.B.dylib.back && mv -f /usr/lib/libSystem.B.dylib.back /usr/lib/libSystem.B.dylib`
(dyld first: the old dyld runs either libSystem); libobjc: `ln /usr/lib/libobjc.A.dylib.old /usr/lib/libobjc.A.dylib.back &&
mv -f /usr/lib/libobjc.A.dylib.back /usr/lib/libobjc.A.dylib`. Test files: `/private/var/db/sfperf` (remove).

### Measured on the M1 (before = PureDarwin's -O0 files as installed until 01:57Z)
| Measure (M1, synthetic input where any) | Before | After (libSystem + dyld installed) | + new libobjc (test prefix) |
|---|---|---|---|
| Activity Monitor CPU at the 5 s refresh, 10 min, no input, with Terminal x2 beside it | 9.69 % (60 s windows 8.8-10.2) | 3.57 % (3.4-3.7) | 3.46 % (3.3-3.5) |
| Activity Monitor RSS over the same 10 min | 75.3 -> 75.3 MB | 65.0 -> 67.3 MB | 64.7 -> 67.2 MB |
| Terminal CPU, idle, same run | 0.81 % (0.7-0.9) | 0.27 % | 0.27 % |
| Terminal RSS, same run | 59.8 -> 60.4 MB | 54.7 -> 55.3 MB | 52.4 -> 52.9 MB |
| Launch, exec to first WindowServer commit: Terminal / Activity Monitor | 563-679 ms (6) / 1004-1231 ms (3) | 307-350 ms (6) / 499-513 ms (3) | 323-353 ms (3) / 481-525 ms (3) |
| RSS at that first commit: Terminal / Activity Monitor | 59.8-59.9 / 65.5-65.6 MB | 52.2-54.8 / 59.7 MB | 52.2-52.3 / 59.7-59.8 MB |
| `seq 1 300000` typed into Terminal (zsh `time`) | 2.83-2.86 s (3) | 1.08-1.10 s (3) | - |
| Typing echo, key to commit (`sf-latency.py inject`) | 4-8 ms (6) | 2-4 ms (6) | - |
| `zsh -i -c exit` / `brew --version` (median of 5) | 259 / 198 ms | 190 / 89 ms | - |
| `python3 -c pass` / a JSON + sort script / `node -e 0` | 108 / 433 / 410 ms | 66 / 301 / 154 ms | - |
| `ls -lR /usr` / `seq 1 300000 \| wc -l` / `seq 1 200000 \| sort -r` | 1248 / 506 / 706 ms | 142 / 137 / 387 ms | - |
| start of /bin/echo (200 runs x 3) | 13.9-15.3 ms (old dyld, new libSystem) | 8.8-10.7 ms | - |
| RSS of a trivial Objective-C program (`sf-rss`) | 3456 KB | 2992 KB | 3008 KB |

Micro-benchmarks (`probe/sf-bench.c`, best of 3 x 5 rounds, ns per operation; run-to-run spread under 10 %):

| | Before | After | + new libobjc |
|---|---|---|---|
| malloc+free 16 B / 256 B / 4 KB / 64 KB | 401 / 833 / 380 / 1566 | 72 / 119 / 73 / 957 | same |
| 1000 mixed small blocks, per block / calloc 128 B / realloc 16 B -> 1 KB | 2477 / 1273 / 5868 | 326 / 78 / 764 | same |
| pthread_mutex lock+unlock / os_unfair_lock | 156 / 57 | 25 / 21 | same |
| memcpy 4 KB / memcpy 1 MB / memset 4 KB / strlen 256 B | 10760 / 3190259 / 22710 / 159 | 140 / 68205 / 126 / 36 | same |
| snprintf / getenv (absent) | 4438 / 396 | 365 / 82 | same |
| dispatch_sync / dispatch_async round trip / dispatch_once | 207 / 21804 / 0.5 | 25 / 18318 / 0.5 | same |
| objc_msgSend / retain+release / NSObject new+release | 5.4 / 28.1 / 604 | 5.4 / 28.1 / 102 | same |
| autorelease pool with 10 objects, push to pop | 7833 | 2015 | 401 |

Before = PureDarwin's -O0 libSystem and dyld while they were installed (23:55Z-01:52Z), after = the installed optimised
files (02:05Z-02:28Z); the 10-minute rows run AM, AM, Terminal, Terminal side by side with no input. The "+ new
libobjc" column was measured from a test prefix before its install. Activity Monitor's RSS was flat before and grew
2.3 MB in the 10 minutes after (both instances alike): not attributed. retain+release (28 ns) and dispatch's thread
round trip did not move: not looked into.
Two later 10-minute runs with all three files installed (03:06Z, 03:17Z) are not comparable with the rows above: by
then TERM5's AppKit/libWindowServer/Onyx2D (02:46Z) and DESK4's compositor 003 (03:09Z, relaunched inside the first
run) were new, and a click reached Activity Monitor in the second (it ended: SIGTRAP in CFArrayGetValueAtIndex under
-[NSTableView mouseDown:], report on the M1). Activity Monitor used 4.8 % and 6.2 % there (60 s windows 3.2-8.0), and
its RSS jumped once by about 17 MB (at the compositor relaunch in the first run, after 30-60 s in the second) and then
stayed flat. Activity Monitor crash reports tonight: 01:20Z (before these installs), 02:34Z (objc_msgSend under the
constraint layout), 03:15Z (SIGBUS in Onyx2D's span writer, another agent's instance), 03:18Z (the one above): all in
AppKit/Onyx2D paths, not attributed; written up for TERM5.

Tools: `probe/sf-bench.c` (micro-benchmarks, `sf-bench [rounds]`), `runs/perf/perf-launch.py` / `perf-cli.py` /
`measure.sh` (not committed: exec to first commit, command-line programs, 10-minute RSS/CPU), sf-prof.
What is left in Activity Monitor and Terminal is in AppKit/Foundation/WindowServer and went to TERM5
(`runs/m1-coord/requests-for-TERM5.md`, "PERF"): the constraint solver and text drawing at every refresh (AM's main
thread, 93 % waiting, sf-prof), `-[NSMutableIndexSet shiftIndexesStartingAtIndex:by:]` and `-[NSMenu update]` under a
flood. Measured from a test prefix (DYLD_LIBRARY_PATH), Activity Monitor's RSS was 5-11 MB higher with the new
libSystem (heap in use identical); installed, it is 8-10 MB lower. The rise came only with the test prefix; its
cause was not established.

## Owner's hands-on round, Terminal (night of 14/15 November; TERM5)
The owner used Terminal and Activity Monitor by hand on the panel at 150 % and reported; each finding was reproduced
here with synthetic input through the inject module, measured in the window's own buffer (inject `dump`, and
`PD_WS_DUMP_DIR` for the exact client buffer), fixed in cocoa 0120..0122 and checked on the installed files
(captures `runs/sf-term5/shots`, not committed).

| Finding (owner's words, shortened) | Cause | Fix | Seen on the M1 |
|---|---|---|---|
| Thin lines between text rows, also not over Activity Monitor | (1) Onyx2D rounded every clip out to whole pixels; a dirty rect of whole pixels that went to points and back ended 1e-13 past its last row, so the clip took one more row, which AppKit's clear of a translucent window made transparent and Terminal's fill of the same rect did not reach: a fully transparent pixel row, the desk showing through hard. (2) `-[NSView centerScanRect:]` aligned to whole *points*, and a backing store is whole pixels (719 px for 479 pt: 1.5010 px/pt), so Terminal's per-row selection and ANSI fills ended half way through a pixel that the next row covered again | Onyx2D 0122: a clip edge within 1/1024 px of a boundary is on it. AppKit 0120: `centerScanRect:` and `backingAlignedRect:options:` (NSView, NSWindow) align to the backing store's own pixels per point; dirty rects likewise | seam rows found by `runs/sf-term5/seams.py` in Terminal's buffer, 150 %: before 2 after `seq 1 2000` (row 35 at alpha about 0.45, row 711) and one per row boundary inside a selection; after 0 in fresh, seq, scroll back, font +/-, selection, edge resize. 100 % and 200 % (`PD_WS_SCALE`, client buffer): 0 before and after. Crops `seam-*-before-after.png` |
| Opaque by default, as on macOS | Terminal's default profile here is Clear Dark (alpha 0.95). Terminal draws its profiles opaque when `-[NSWorkspace accessibilityDisplayShouldReduceTransparency]` answers YES (it reads it in `-[TTView initWithFrame:]`; `alphaForBackground` is then 1), as macOS does with Reduce Transparency | AppKit 0120: with no `reduceTransparency` in the user's `com.apple.universalaccess`, transparency is reduced while the compositor cannot blur behind a window (`_windowServerCanBlurBehind`, WindowServer 0121: ext-background-effect-v1 offered or not). `defaults write com.apple.universalaccess reduceTransparency -bool false` gives translucency back. Opaque toplevels set their opaque region | a brand-new HOME (`CFFIXED_USER_HOME`): Clear Dark's colours at alpha 255 (99.3 % of the buffer, the rest its outline), `wl_surface.set_opaque_region` sent (`WAYLAND_DEBUG`); with reduceTransparency NO: alpha 242 as before |
| Profiles > Text: black bar over "Background" | NSPathControl answered its cell's size, NSCell's 10000 x 10000, as its intrinsic size: the box round the background image's path was laid out 10007 points wide to the left (`PD_LAYOUT_TRACE`) | 0120: no intrinsic width, the height of its control size | the row shows the well, "Color & Effects" and "Image:" (`pi1-text`) |
| "Parts weren't readable" in Preferences at 150 % | every pane and Profiles tab captured (`pi1-*`, `pi2-*`): explanatory multi-line labels showed one line (Profiles > Tab, Window) | 0120: a wrapping label longer than its width answers the height of its lines at that width; the help button draws its '?' (it was an empty square) | not captured with the final build. Also found: a brand-new user's first Preferences shows an empty profile list until Terminal is started again (the profiles are imported into the preferences file; the table hears nothing): not fixed |
| Colour well -> colour panel -> opacity | Terminal's background well sets the colour panel's accessory view (its Color & Effects controls): `-[NSColorPanel accessoryView] is not implemented` raised and no panel appeared | 0120: accessory view under the panel's controls; a colour well toggles once per click (a press on its border toggled it twice) | **still wrong**: no exception any more, but the panel does not appear from the well (Command-Shift-C shows it, with the opacity slider); the opacity -> profile -> window path end to end was not walked |
| The mouse offset from the highlighted menu item | not reproduced at 150 % with absolute or relative synthetic motion: the drawn arrow's tip is 2 to 3 px up-left of the pointer (its outline) over Terminal, over a menu, over the desk; highlight under the pointer for strip menus (also those the compositor slid up), submenus and the desk's title-bar menu. What was found: moving down Terminal's Shell menu built the New Window and New Tab submenus synchronously (110 ms each), the highlight trailing the pointer meanwhile | 0120: motion coalesced to the newest; a submenu opens after the pointer rests 60 ms on its item | drawn arrow: its dark tip on the pointer (0,0) over Terminal, after relative motion and over a menu (the 2-3 px are its white outline); the desk's scale file says 180 = 150 % in 1/120 units (unchanged since 13 November); at 125 % (scale file 150, compositor relaunched) the strip's and the menu's text are the same height (12 px) and the highlight follows the pointer but for one boundary row (the backing store's 1.251 px/pt against the pointer's 1.25: at most 1 px); at 175 % (210) and 200 % (240) the same (strip 17/menu 18 px, 20/20 px; highlight under the pointer but where the row's highlight is inset from its rect; dark tip within 1 px); 150 % (180) restored. `PD_WS_TIMING`, one inject line per frame down and up the Shell menu: motion -> commit p95 16.3 -> 13.0 ms, max 118.6 -> 43.4; motion -> release (the frame that shows it) median 28.0 -> 22.8 ms, p95 50.9 -> 30.4 |
| Activity Monitor: a click on another row opened the inspector | click counts had no distance or window test and a 1.0 s interval | 0120: 0.5 s (or `com.apple.mouse.doubleClickThreshold`), same window, within 4 points; NSTableView sends its double action only when both clicks are on one row | two clicks on two rows 700 ms apart: selection moves, no inspector |
| Closing the inspector ended Activity Monitor | SIGSEGV (sf-catch): `-[NSMenu update]` -> `targetForAction:` -> the key window's freed delegate; a window ordered out stayed key and main | 0120: a window ordered out gives up key and main to the frontmost window that can take them | Command-W on the inspector: Activity Monitor lives |
| Activity Monitor ended when its Energy tab was clicked | SIGSEGV in a tab switch (1 in 50 shuffled tab clicks; the crash report: `-[NSTabView selectTabViewItem:]` -> `-_pdSolveConstraints` -> `-_pdLayoutTreeFitting:apply:` messaging a freed view): a view taken out of the hierarchy left the constraints that named it in the views above | 0120: as on macOS, a view taken out takes with it the constraints above that refer to it or to a view inside it | 100 shuffled clicks on the five tabs with a row selected, and the inspector opened and closed: Activity Monitor lives, no new crash report |
| Terminal tabs: the bell drawn over the title | the tab's `accessoryView` (Terminal's bell and activity marks) was never placed by the tab bar | 0120: a slot at the tab's right edge, the title centred between the close box and it; one-line strings whose style truncates (head, middle, tail) are drawn with an ellipsis (the typesetter laid these modes out as clipping: the centred title's ends were drawn over each other, the garble the owner saw beside the bell) | 8 tabs: "ro…sh" in every tab, the bell in the first tab's slot while another is selected, gone when it is selected (`tc8-*-bar.png`); 16 tabs: titles and bell give way (tabs of 52 pt), no overlap. Not done: overflow (Show All Tabs), a wider title when there is no accessory |

Incidents (in `runs/m1-coord/log.txt`): twice my after-install check ended Activity Monitor processes that were
not mine (its own-pid filter used `comm -`, which toybox's comm does not read; fixed with `grep -vxF`); the desk
ran at 125 % from 02:10Z to about 02:36Z between my scale tests (PERF's side-by-side run fell in it).

Crash reports: AppKit 0120 installs a handler for SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE and SIGTRAP that writes
`~/Library/Logs/DiagnosticReports/<name>-<pid>.crash` (signal, address, the faulting thread's frames named by image
and offset) and raises the signal again; exceptions an event raises go to standard error as before, which `open` sends
to `~/Library/Logs/<name>.log`. `PD_NO_CRASH_REPORT=1` turns it off.

Tools (`runs/sf-term5`, not committed): `seams.py` (rows of a buffer whose colour is not the background),
`t1.sh` (Terminal with a fresh HOME through the seam cases), `menulat.sh`/`latan.py` (menu motion to frame),
`tip.sh`/`tipan.py` (drawn pointer against the pointer), `amtest.sh`/`amtabs.sh` (Activity Monitor rows, inspector,
tabs), `tcheck.sh` (the after-install checks; it ends only its own processes).

## Seventh round (14 November, night; COCOA4): the owner's findings, leaks attributed
The owner used Terminal by hand on the M1 (20:38Z to 20:55Z) and reported; seen and checked here with synthetic input
through the inject module (captures `runs/sf-cocoa4/shots`, not committed):
- **Terminal > Settings did not open a second time** (Command-W, then Command-,: nothing). `-[NSWindow close]`
  destroyed the window-server window even of a window kept after closing (releasedWhenClosed NO), so the next
  `-makeKeyAndOrderFront:` had no surface. Fixed (cocoa 0116): the window-server window goes with the object.
  `p3-prefs2`, `c4k-2prefs-again`.
- **The pointer vanished over Terminal**: `+[NSCursor setHiddenUntilMouseMoves:YES]` (Terminal, at each key typed)
  hid it and nothing showed it again. Now +hide/+unhide are counted and libWindowServer shows a pointer hidden until
  it moves at its next motion or entry, on its input thread (0116, 0117; `PD_CURSOR_TRACE=1`). `p3-moved`.
- **Profiles > colour wells opened nothing** (no NSColorPanel nib): the panel is built in code, with an opacity slider
  when the application does not ignore alpha (Terminal's profile colours). `c4k-5colors`. Not yet walked: changing a
  profile's background opacity and seeing the window become translucent (the compositor composites ARGB with alpha).
- Also on the way: NSPathCell called `-[NSURL initWithScheme:host:path:]` and `-[NSFileManager displayNameAtPath:]`,
  which this Foundation lacked (the first is added in core round c4b, the cell no longer needs either).
- **Marks drawn pure blue and a red line around the window** (owner; DESK3 found the red edge in Terminal's own
  buffer, `dump`): one root cause. AppKit's HSB conversions (`NSColor.subproj/conversions.h`) took `float *` and
  NSColor_CGColor passed `CGFloat *`: each wrote 4 bytes into an 8-byte variable, so `-getHue:saturation:brightness:
  alpha:` answered denormals and `+colorWithCalibratedHue:...` built colours from stack garbage. Terminal derives its
  mark colour and its window's edge stroke from the profile colours through those calls. Fixed (0116); seen in the
  window's own buffer: edges (25,29,38) next to the background (24,28,37) where they were (189,28,37); a marked
  prompt line's bracket faint grey-blue (70,82,109) where it was (7,7,250) (`c4redline-7`, `c4mark-7`).
- **The desk's title-bar menu** (DESK3's socket `$XDG_RUNTIME_DIR/app-menu/<pid>`): AppKit answers `menu 1` (the
  main menu validated, 196 lines for Terminal with key equivalents and disabled flags), `invoke <id>` (Preferences
  opened through it) and `menubar show|hide` (the strip hidden and shown again on every window). Checked over the
  socket from SSH; the compositor's own menu was not opened here.
- **Translucency**: Terminal's default profile here is Clear Dark, whose background has alpha 0.95 (242): what is
  behind shows through, sharp because the desk has no blur (macOS blurs behind a translucent profile). Opaque profiles
  draw opaque; the opacity slider of the new colour panel changes it.
- Leaks (sf-heap, `probe/sf-heap.c`): the window-server thread's autoreleased objects (0116), and libFoundation's
  shells and +1 returns (core round c4b; SF-AA). Terminal idle: +0.33 MB/min before, +0.1 MB/min with c4b's files.
- Core round (QEMU, then the guarded installer): `c4a` (CoreFoundation -O2 alone) left libFoundation out of its stage,
  so the image's older libFoundation met today's Foundation wrapper (failures unrelated to CF; libCoreFoundation -O2
  replaced and ls/sh/node/claude/python3/bash/git/vim/nvim/tmux ran with it); `c4b` failed on its own probes
  (CFAutorelease's owned-class test missed NSArray-bridged objects; NSDate had no keyed coding for the NSDateInterval
  check); fixed and checked on the M1 from a test prefix; `c4c`/`c4c-2` failed=0, QDDESK_PASS; installed (SF-AA).
  `sf-cf-o2-probe` and `sf-cf-autorelease-probe` are the installer's proofs of a new libCoreFoundation (CORE list and
  probe_of in `tools/sf_terminal.sh`).
- Installed (direct, the files of before as `.pre-c4`): AppKit 335c15a4, libWindowServer 2e251bb1, Onyx2D e5497009,
  CoreGraphics b5a1f9b4. Revert: `ln -f <path>.pre-c4 <path>.back && mv -f <path>.back <path>`.

## Sixth round (14 November, evening; COCOA3): the leak every program had, toolbar, scrollers, symbols
Seen on the M1 (synthetic input through the inject module while DESK2's test compositor ran; captures
`runs/sf-am3/shots/c3-t-*`, `c3-ts-*`, not committed):
- libFoundation's convenience constructors leaked every object (PureDarwin CoreFoundation's `CFAutorelease` never
  autoreleases a CF-owned object). Fixed in libFoundation (`PDAutoreleaseCF.h`), core round QEMU `c3b`/`c3b-2`
  (failed=0, QDDESK_PASS both), guarded install (libFoundation e77c9824, `.old6`; Foundation wrapper a660d750,
  `.old3`). Terminal ended at launch with it until cocoa 0108 (NSMenuItem's getters retained and autoreleased:
  Terminal keeps a removed menu item's key equivalent, which macOS keeps as a never-freed tagged pointer).
  Terminal with the new libraries: launch, 300,000 lines twice, Preferences, a new tab, the Inspector: alive
  (`c3-t-n2`, `n3`, `n4`). `sf_compat_push.sh check` after the installs: 18 of 18, `failed=0`.
- The Preferences toolbar shows its icons (Finch's symbol glyphs, cocoa 0111: General, Profiles, Encodings;
  Window Groups' symbol is none of them and stays blank) and the selected pane in a soft rounded rectangle (0113).
- Overlay scrollers (0114) for every application: no track, a thin knob shown when the user scrolls, faded a
  second later; the legacy look with `AppleShowScrollBars=Always` in the defaults or `PD_SCROLLERS=legacy`.
  Checked in Activity Monitor's table (`c3-sd-*`); in Terminal's scrollback the knob was not captured (the window
  sat partly off the screen and the inject compositor was then taken down at the end of DESK2's night).
- Finch's constraint solver in place of ours was measured with `PD_SOLVE_TIME=1` (cocoa 0112): Terminal's
  Preferences, 206 whole-tree solves, 1574 ms with ours; with Finch's as a throwaway solver, 104 solves had taken
  10075 ms after 9 s. Not taken; it would pay off only with an incremental driver (keep the solver between layouts,
  change constants), which is not written.
- Revert: SF-AA, "Installed on the M1's disk, sixth round".

## Fifth round (14 November, afternoon): Activity Monitor's changes merged, Terminal checked
Activity Monitor's cocoa patches are in the main series now (0100..0107, `cocoa/patched.nix`;
docs/roadmap/issues/SF-AA-activity-monitor.md). The merge attempt of the night had been taken back
because Terminal's Preferences General pane and Inspector Info pane overlapped with them. The cause:
with 0104's NIBArchive booleans read the right way round, those views are laid out by their
constraints, and three inputs to the solver were wrong. 0106 fixes them: a nib's standard-space
constraints (`NSSymbolicConstant` "NSSpace"; 246 of the 692 constraints of `TTAppPreferences.nib`)
were 0 points; archived hugging and compression priorities were not read; `NSMatrix` reported
10000 by 10000 as its intrinsic size. The Inspector's table header also lost its last column's title
under an empty corner.
- Seen on the M1 with the installed Terminal after the installs (fresh boot, synthetic input):
  Preferences General (`runs/sf-am3/shots/t3-prefs.png`, `t5-prefs.png`) and Inspector Info
  (`t4-info.png`, `t5-info.png`) are laid out as in `merge-before-*`, with the radio buttons and the
  checkbox in the accent colour and the Command field's placeholder. Tabs (`t4-tabs`), the View menu
  (`t4-view`) and full screen in and out (`t4-fs`, `t4-fsback`) also look right. The window comes back
  larger after full screen, as in the fourth round's `fsOff`.
- `sf_compat_push.sh check`: 18 of 18, `failed=0`, before and after the core installs
  (`runs/sf-am3/regress/`).
- Core files, rehearsed in QEMU `am1`/`am1-2` (failed=0, QDDESK_PASS both) and installed by the
  guarded installer: libSystem ba8013eb (`memorystatus_get_level`; `.old5` is the one of before),
  the CoreFoundation and Foundation wrappers with Activity Monitor's additions (6f280f66, d25ed0b1;
  `.old2`), and a new IOKit wrapper at `IOKit.framework/Versions/A/IOKit` (b5a239e7; the link of
  before is `IOKit.old`). IOKit's path is now one of the files the guarded installer handles
  (`sf_terminal.sh`, `sf_userland_push.sh`), with its probe. libFoundation bed36b5d: key-value
  observing remembers `+keyPathsForValuesAffectingValueForKey:` per class and key (rehearsed `am2`,
  `am2-2`; the one of before, 193b520a, is `.old5`; the probe's fifth check is the symbol
  `PDFoundationKVOAffectingKeysCache`). Found while writing that check, not fixed: a dependent key
  declared with `+keyPathsForValuesAffecting<Key>` is not heard when its affecting key changes through a
  plain setter, with this build and the one of before alike. `sf_compat_push.sh check` 18 of 18 after it.
- Replaced directly, with the file of before kept beside it: AppKit (`.pre-am` is TERM's 527784f2),
  CoreGraphics (`.pre-am`), the CoreServices wrapper (`.pre-am`). Details and how to revert are in
  SF-AA, "Installed on the M1's disk".
- Foundation-am replaces NSNumberFormatter's decimal and percent styles for every program that loads
  the Foundation wrapper, Terminal included; nothing in Terminal's checks changed with it.

## Fourth round (night of 14 November): performance, text size and keys, full screen
Everything here was done on the M1 booted from its disk. The input was synthetic (the compositor's
inject module); nobody looked at the panel. Pictures are in `runs/sf-terminal/shots/` (not committed).

What a person at the M1 can now do that they could not before:
- Scroll with the wheel at 60 frames a second (synthetic wheel events; the trackpad's scrolling was not tried). Menus open in 64 to 96 ms. Resizing the window by its border
  follows the pointer (about 25 ms from each compositor configure to the frame of that size).
- Change the text size: View > Bigger, Smaller and Default Font Size (Command-+, Command-=, Command--,
  Command-0). The window grows and shrinks with the text and keeps its 120x30 grid. Command-0 beeps
  when the size is already the default, as on macOS.
- Full screen: View > Enter Full Screen and Control-Command-F. The window covers the whole display
  in a Space of its own, with its menu strip. Exit Full Screen (the same key or menu item) brings it
  back (`fs1`..`fs3`, `w-Enter-Full-Screen-Ctrl-Cmd-F`).
- Use the panels: Shell > Export Text As (Command-S) and Open (Command-O). Print (Command-P) says
  that there are no printers and saves a PDF. New Remote Connection opens its window (all four in
  `t4-panels`).
- Use the Window menu: Minimize (Command-M) puts the window on the compositor's tile and a click on
  the tile brings it back (`w-Minimize-Cmd-M`, `min2`). Cycle Through Windows (Command-`) raises the next
  window. Tabs can be dragged to another place in the bar or out of the window into a window of
  their own (`tdrag0`..`tdrag2`).
- Use the Edit menu: Clear to Start (Command-K), Clear Screen, Select All, Find (`fd*`) and Use
  Selection for Find (Command-E) work.
- Open the inspector (Command-I): it lists the running processes by name ("login -pf root",
  "-zsh", "sleep 300", with their colours: `ins3`). Command-W closes it. Edit Title (Command-Shift-I)
  selects its title field (`t4-inspector-cmdw`: open, then closed by Command-W).
- See the other menu items: Terminal > Services shows "No Services Apply" (`svc1`). Secure
  Keyboard Entry toggles its check mark (`sk2`). New Command and New Remote Connection open their
  windows. Help says that no help is available instead of raising.
- Hear the bell: the sound goes through the sound stream's coreaudiod, as a short tone, because no
  macOS sound is copied. The audio server opened the speakers and mixed one client. Nobody listened.
- `open -a Terminal <folder>` opens the shell in that folder, not in its parent (installed
  libFoundation `193b520a…`, rehearsed in `ty11`).

### Performance (M1, `tools/sf-latency.py`, synthetic input)
"Before" is the stack installed at the start of the night (Ctrl-C, idle and memory: the libraries of
before in the test prefix `tpre`). "After" is the stack installed at the end. Each row is 2 to 6
measurements.

| Measure | Target | Before | After | Met? |
|---|---|---|---|---|
| Menu, press to the popup's first frame | < 100 ms | 122 to 164 ms | 64 to 96 ms | yes. The popup was drawn twice, and the window's menu strip was laid out again on every frame (at 150% the content's top edge falls into a pixel row of the strip); each key equivalent validated every menu |
| Launch, exec to the first frame | about 0.85 s, nib decoding and strip layout | 827 to 850 ms (2255 ms the first after a boot) | 818 to 933 ms | no. The profile: nib loading about 213 ms (key-value coding `setValue:forKey:` 63 ms, property-list parsing 33 ms, autorelease pool 45 ms), NSApplication's start 58 ms (a Wayland round trip 27 ms, the localization table 25 ms). |
| New tab (Command-T), to the new tab's first frame | near one frame | 169 to 171 ms | 137 to 175 ms | no. A new tab starts a shell on a pty and lays out a whole view. The rest of the time was not attributed |
| Tab switch (Command-Shift-[ / ]), first frame | near one frame | 58 to 62 ms | 44 to 54 ms (one 101 ms) | partly. It was drawn twice. Now 3 frames per switch (what each holds was not attributed) |
| Wheel, 60 steps one per compositor frame | 60 Hz | a frame every 31 to 33 ms, the last 400 ms behind | 60 frames for 60 steps, 14 ms median, 17 ms p90 | yes. The causes: the strip relayout; a CTLine and a CTFont made for each row of each frame (CoreText now keeps them); `-valueForKey:` formatting nine names per call (Foundation keeps the answer per class and key); rectangle outlines stroked through the edge rasterizer (Onyx2D covers axis-aligned rectangles and outlines directly); a 2 MB winding buffer calloc'd per context. In a tab with nothing to scroll: 31 frames for 60 steps |
| Resize by dragging the border, configure to the frame of that size | smooth | median 44 to 129 ms | median 24 to 26 ms, p90 30 to 39 (fresh window); 25 to 42 / 114 with the long scrollback `yes` and `find` leave | yes for a fresh window. Before, every configure was drawn in turn, at the old buffer's size limits |
| Maximize (frame button), click to the first full-size frame | - | 234 ms (restore 216) | 215 ms (restore 185); configure to frame 47 ms (26) | the client's part is 26 to 47 ms; the rest is before the configure arrives (compositor) |
| Typing echo, key to frame | keep 7 to 8 ms | 9 to 11 ms | 7 to 11 ms | kept |
| Output, 40 lines to the pty, to the frame / on screen | - | 63 to 73 / 74 to 84 ms | 49 to 69 / 62 to 83 ms | - |
| Control-C under `yes` / `find /`, to the process gone | at once | 86 / 115 ms (old libraries) | 61 / 39 to 43 ms (fresh window); 126 to 129 / 41 to 188 ms with the scrollback `yes` leaves | yes in a fresh window. A long scrollback slows everything that reflows or redraws |
| Selection drag, 30 pointer steps | - | not measured | 28 frames, 17 ms median gap | - |
| Flood, `cat` of 1.3 MB | - | 73 to 699 ms (noisy) | 297 to 880 ms (noisy) | not established either way |
| Idle CPU with a blinking cursor, 20 s | - | 0.21 s (old libraries) | 0.13 s (dev) to 0.32 s (installed, just after launch); RSS about 63 MB | - |
| Memory growth, 300,000 lines of output | - | +92 MB (old libraries) | +81 MB | - |

### Menus and key equivalents (Terminal's main menu, walked on the M1)
Each item was chosen by its key equivalent through the inject module, or by the pointer where it
has none. "Works" means the result was seen in a capture of the screen. "Commit only" means Terminal drew a new
frame and the content was not inspected. Not walked: no result either way.

| Menu | Works | Commit only | Unclear or wrong | Nothing | Not walked |
|---|---|---|---|---|---|
| Terminal | Preferences (1.2 s); Secure Keyboard Entry (the check mark; the private call it makes does nothing here, and on Wayland no other client receives the keys anyway); Services ("No Services Apply"); Quit | - | Hide Terminal (hides every window; nothing on the desktop brings a hidden application back); Hide Others (nothing to hide) | - | About Terminal; Show All |
| Shell | New Window; New Tab; New Command; New Remote Connection; Open; Close Window / Close Tab; Export Text As; Show Inspector; Edit Title; Print (no printers, Save as PDF) | Reset; Hard Reset | - | - | New Window / New Tab with Same Command; Use Settings as Default; Export Settings; Export Selected Text As; Edit Background Color (needs the colour panel); Print Selection |
| Edit | Copy; Paste; Select All; Clear to Start; Clear Screen; Find; Find Next; Find Previous; Hide Find Bar; Use Selection for Find; Copy Plain Text (beeps without a selection, as on macOS) | Clear Scrollback; Mark; Jump to Previous Mark; Jump to Next Mark | Paste Escaped Text, Insert Bookmark, Select Between Marks, Undo, Jump to Selection, Use Option as Meta Key (its HUD appears; the effect on keys not checked): no visible change | Show Colors (`NSColorPanel.nib` is missing) | Redo; Cut; Copy Without Background Color; Style for Copy; Paste Selection; Paste Escaped Selection; the Marks, Bookmarks and Navigate items other than those named; Clear to Previous Mark / Bookmark; Fill Screen; Find's Select All items; Num Lock |
| View | Show / Hide Tab Bar; Split Pane; Close Split Pane; Bigger; Smaller; Default Font Size; Enter / Exit Full Screen (menu and Control-Command-F) | Scroll to Top; Scroll to Bottom; Page Up; Page Down; Line Up; Line Down | Allow Mouse Reporting (no visible change) | - | Show Marks; Show / Hide Alternate Screen |
| Window | Minimize; Cycle Through Windows; Show Previous / Next Tab; Move Tab to New Window and Merge All Windows (third round); Zoom by the frame's button | - | - | Show All Tabs | Zoom (menu item); Open Window Group; Save Windows as Group; Return to Default Size; Bring All to Front |
| Help | Terminal Help (the alert that help is not available) | - | Open man Page for Selection (no result recorded) | - | Search man Page Index |
| Keys outside menus | typing; Return; Escape; Control-C; arrows | Page Up, Home, End | - | - | - |

Counts: 40 work, 12 drew a frame without being inspected, 10 unclear, 2 do nothing (Show Colors,
Show All Tabs). The rest were not walked: about 44 items, mostly alternates and the Marks,
Bookmarks and Navigate submenus.

Split Pane followed by Close Split Pane makes the window shorter each time, by 14 points per pair
(719, 710, 689 ... 599 pixels over three pairs). `sf-trace` shows Terminal's own geometry doing it:
`-[TTTabController splitPane:]` gives each pane 225 points (14 rows of 15 points), sets the window
to fit 2 x 14 rows, and Close sets it to fit the 29 rows that remain. Whether macOS, with its
line height, comes out the same was not checked. Show / Hide Tab Bar returns to the old height.

### Full screen
AppKit asks the compositor through `xdg_toplevel.set_fullscreen` / `unset_fullscreen` (AppKit
0052) and follows the compositor's state. The window's full-screen mask is AppKit's and is not sent to the
server as a style. The menu item reads Enter or Exit Full Screen, and the delegate gets the
will/did calls. The compositor's side was DESK's: client full-screen and maximize requests,
`xdg_activation_v1`, Command chords passed to Mac applications, and full-screen windows covering the
dock (`answers-from-DESK.md`, compositor patches 9 to 11). Open, the compositor's: after Exit Full Screen a second configure 120
to 145 ms later gives the window the work area's size (1629x1029 points) instead of its old frame
(seen again at the end with Control-Command-F).

### Left-overs
| Item | State |
|---|---|
| `open -a Terminal <folder>` in the parent | fixed. Terminal's `-openFolder:inNewTab:` keeps a URL that ends in "/" and otherwise takes the parent. Foundation's `+[NSURL fileURLWithPath:]` now ends a directory's URL in "/", as on macOS. The same check in `-initFileURLWithPath:` left the Preferences window empty, so that method is unchanged (the caller that depends on it was not found) |
| Use Selection for Find (Command-E) took one character | not a fault: the synthetic double-click's timing. With a proper double-click it takes the word (`findme123`) |
| Tab bar colour in dark profiles | as on macOS: Terminal sets no appearance on its windows (only on the Option-as-Meta HUD), so the bar follows the system appearance |
| Bell | audible through coreaudiod (AudioToolbox wrapper: System Sound Services, a short tone as the alert sound). Not heard by a person |
| Tab dragging | done: reorder within the bar, and drag out (more than 40 points away) to a new window |
| Services | the menu shows "No Services Apply"; there are no services |
| Secure Keyboard Entry | the item toggles; nothing to secure on Wayland (only the focused client receives keys; the test compositor's inject module is the exception) |
| Session restore | a relaunch restored a window with its working directory (it shows the `cd` it ran) |
| Marks | Mark and Jump to Previous / Next Mark draw; the marks themselves were not inspected |
| Profile thumbnails grey | not fixed. AppKit 0057 makes `-getRectsBeingDrawn:` answer the whole view outside a display; the thumbnails are still grey frames. Traced (`sf-trace`): `-[TTView thumbnailImageWithProfile:size:]` locks focus on a new image, concatenates its scale (0.16), and `-drawRect:` runs there over the whole 850x457 view. It then draws that image into a second image (`-drawAtPoint:fromRect:operation:fraction:` inside the second image's focus). Suspected, not established: an image whose only representation is a cached window copied into another image's focus gives nothing |
| Symbol images, `Assets.car` | not done. Finch's CoreUI reader is a whole framework, which is not adopted. Terminal's catalogue holds only its icon (read from the `.icns`) and colours; the toolbar's images are system SF Symbols |
| Font panel ("at least does not break") | it does not break, and it does not open: Preferences > Profiles > Text > Change… highlights and nothing follows (no `NSFontPanel.nib` installed; see the remaining-work table) |
| Scroller look, colour emoji (CBDT), window proxy icon, drag and drop, Show All Tabs, colour panel | not done |

### Faults found in the fourth round (in the system, none in Terminal)
| Symptom | Cause | Fix |
|---|---|---|
| Wheel at 30 Hz, menus 120 to 160 ms, tab switch 60 ms | the strip relaid out on every frame; popups and tabs drawn twice; a CTLine/CTFont per row per frame; `-valueForKey:` formatting names; rectangles through the edge rasterizer; a key equivalent validated every menu | AppKit 0051, 0052; CoreText 0053; Onyx2D 0054; Foundation (key-value coding cache) |
| A resize drag lagged hundreds of ms | every configure was drawn in turn; the size limits were committed on the old buffer, and the compositor clamped the next configure to them | AppKit 0052 (a configure overtaken by a later one is skipped); WindowServer 0058 (limits ride on the next frame) |
| Bigger / Smaller gave the wrong window size after the line cache | `-[NSFont isEqual:]` ignores the point size, so a line laid out at the old size was reused | CoreText 0053 compares the size (found in this round's own change) |
| Command-= did nothing | the item's key is "+" | AppKit 0052: Command-= is Command-+ |
| Every AppKit menu became a layer-shell overlay | upstream's layer-shell path, live once the compositor offered layer shell | WindowServer 0058: layer shell only with `PD_WS_LAYER_SHELL=1` |
| Export, Open, Print did nothing; Help raised | no panels without a display server's own; no print path; `NSHelpManager` unimplemented | AppKit 0055, 0056 |
| Command-` and the Window menu did not raise a window; Minimize lost the window | no `xdg_activation_v1` use; minimise was an unmap | WindowServer 0058, AppKit 0052 |
| Inspector raised exceptions; then listed `<TTProcessInfo: ...>` | `NSTableColumn -isHidden`, a cell copying a non-copyable value, font bindings, a non-colour foreground; value transformers named by class were never instantiated and a table column's binder ignored transformers | AppKit 0059 |
| Shift alone (and so Command-W) opened the inspector's pull-down | a button's empty key equivalent matched an event without characters | AppKit 0059 |
| `open -a Terminal <folder>` opened the parent | `+[NSURL fileURLWithPath:]` never ended a directory's URL in "/" | Foundation |

Patches: `cocoa/patches/0051`..`0059` (descriptions in `cocoa/patched.nix`),
`terminal-app/patches/foundation-macos-applications.patch`, `terminal-app/compat/AudioToolbox.c`.
Nothing was taken from Finch in this round.

### Remaining work (after the fourth round)
| Item | What is known | Estimate |
|---|---|---|
| Launch 0.8 to 0.9 s | nib decoding (key-value coding setters, property lists, autorelease) and NSApplication's start; the profile's figures are in the performance table | 1 day for a setter cache and the start's round trip |
| New tab 140 to 175 ms, tab switch 45 to 55 ms | three frames per switch; not attributed further | 1 day |
| Colour panel and Font panel (Show Colors, Edit Background Color, Preferences' colour wells and its font Change… button) | Show Colors does nothing and Change… only highlights. AppKit's own panels load from Cocotron's compiled nibs (`en.lproj/NSColorPanel.nib`, `NSFontPanel.nib`, `NSColorPicker.subproj/en.lproj/*.nib`, MIT, in the source), which upstream's recipe does not install. Laid into a test prefix's AppKit resources: Show Colors opened the Colors panel (its wheel drawn black), and Terminal then ended without a message when Preferences' Profiles pane opened (crash not diagnosed). Not installed | 1 day |
| Profile thumbnails grey | the drawing reaches the first image; the copy into the second is the suspect | 1 day |
| Show All Tabs | no tab overview in AppKit | 2 days |
| Exit Full Screen size | the compositor's second configure (work area) | compositor (`requests-for-DESK.md`, TERM 5) |
| Minimised-window tiles outlive their windows | two tiles stayed after their Terminal processes exited | compositor (TERM 5) |
| Hide Terminal | nothing on the desktop unhides an application | compositor / dock |
| Scroller look, colour emoji, proxy icon, drag and drop, symbol images | as before | as before |
| `-initFileURLWithPath:` of a directory | macOS ends it in "/"; doing so here broke the Preferences window | half a day to find the caller |
| Activity Monitor's patches into the shared stack (`requests-for-TERM.md`, "From ACTMON, 2") | tried. The cocoa patches 0100..0105 build over 0059, but with them Terminal's Preferences General pane and Inspector Info pane are laid out wrongly (`shots/merge-am-*` against `merge-before-*`). Likely cause: the NIBArchive booleans read the right way round hand those views to Auto Layout. Not merged. The wrappers, the IOKit wrapper and libSystem's `memorystatus_get_level` need the core pipeline: not done (`answers-from-TERM.md`, "To ACTMON, 2") | 1 day (constraint solver) + a core round |

## The binary (scanner, `integrations/puredarwin/homebrew/sf_gapscan.py --app`)
The scanner now reads universal files (`--arch arm64e`), the import table of chained fixups, the
chains themselves (authenticated binds and rebases), Objective-C class and category method lists and
`__objc_selrefs`, and takes an application bundle that is not in the root (`--app <dir>=<logical path>`;
`--under <tree>` lays a tree under the root). Table of 13 November against the M1's disk root as it was:
`integrations/puredarwin/homebrew/gapscan/terminal-before.md`.

| | |
|---|---|
| Slices | x86_64, arm64e (ptrauth ABI byte 0x80); chained fixups, pointer format 12 (`DYLD_CHAINED_PTR_ARM64E_USERLAND24`) |
| Fixups | 11068 rebases (688 authenticated), 2438 binds (1902 authenticated); 442 imports bound authenticated (IA 360, DA 84, all address-diversified) |
| Imports | 733 from 37 libraries (16 weak Swift overlays, none of which it imports a symbol from) |
| Missing on the M1's disk, 13 November | 214 symbols; libraries absent: libswiftCore (83), ColorSync (11), DataDetectorsCore (4), ApplicationServices (2), CoreAnalytics (2), libScreenReader (0); in libraries that are there: AppKit 37, Foundation 16, CoreFoundation 14, libSystem 13, CoreGraphics 11, AudioToolbox 6, Carbon 6, CoreServices 5, QuartzCore 2, CoreText 2 |
| Objective-C | 144 classes imported, 16 not defined anywhere; 3271 selectors sent, 307 implemented by no image of the closure (a lower bound of what is missing: a selector some class has may be missing on the class it is sent to) |

## arm64e: decision
**Pointer authentication stays off, for every process, as upstream PureDarwin already has it; arm64 and
arm64e images share a process.** Not a scheme of ours: upstream's kernel (in our tip, so in the installed
kernel `093b61f1…`) keeps the PAC keys off (`machdep.ptrauth_enabled: 0`), rewrites PAC instructions to
their plain forms in every page that is faulted in executable (`pd_pac_lower_page`, `osfmk/vm/vm_fault.c`;
a private copy for file mappings so the file keeps its signed page) and emulates what cannot be rewritten
(`LDRAA/LDRAB`, `PACGA`: `osfmk/arm64/sleh.c`); upstream's dyld is an arm64 program compiled with
`-fptrauth-calls`, so its signing of authenticated fixups is itself lowered to nothing. Real PAC would need
an arm64e build of the whole userland (libSystem, libobjc, libc++, every framework) and a kernel with
`ptrauth_calls`: not what upstream builds, and nothing an application can observe is gained by it.

Measured on the M1 (13 November, the test copy in `/private/tmp/sfy`, nothing installed):
- The kernel execs the arm64e slice of the universal file and starts dyld in it.
- dyld then refused every library: `arm64 dylibs cannot be loaded into arm64e processes`. dyld decides
  that from the apple string `ptrauth_disabled=1`, which only a kernel built with `ptrauth_calls` sends,
  or from the main program being plain arm64.
- Fix (`libsystem-default/patches/dyld-arm64e-keys-off.patch`): dyld signs a probe value at start; where
  signing leaves it unchanged nothing in the process can sign or authenticate, and the keys count as
  disabled. Then arm64 libraries load into an arm64e program, arm64e slices and thin arm64e libraries load
  into any program (a program prefers slices of its own kind), and the "platform binaries only" rule for
  arm64e does not apply. On a kernel that really signs the probe changes and dyld behaves as before.
  arm64 programs are not affected: for them dyld already took the keys as disabled.
- With that dyld (a test copy of the binary naming `/tmp/sfy/dyld` as its dynamic linker; 13 bytes, the
  same length) the refusal is gone and dyld goes on to the first absent library.

## Faults that were in the way, in the order met (each is a fix in the system, none in Terminal)
| # | Symptom | Cause | Fix |
|---|---|---|---|
| 1 | dyld: `arm64 dylibs cannot be loaded into arm64e processes` | dyld takes the PAC keys as enabled in an arm64e program | `libsystem-default/patches/dyld-arm64e-keys-off.patch` (above) |
| 2 | 214 imports missing | section "Load surface" | |
| 3 | dyld unloads a wrapper's re-exported library, then "image not found" | dyld keeps one image per install name; a wrapper carried the name of the library it re-exports | wrappers are linked under `<path>.sfcompat` (`compat/build.sh`) |
| 4 | SIGBUS in libobjc while dyld tells it the images are mapped | current linkers put `__objc_protorefs`, `__objc_superrefs` into the read-only `__DATA_CONST` segment; dyld 852 has made it read-only by then, Apple's current dyld opens it for the runtime | `libsystem-default/patches/dyld-objc-writes-data-const.patch` |
| 5 | SIGSEGV in `swift_getObjectType`, first Swift call of `main` | upstream's arm64 Swift runtime carries Apple's arm64e isa mask (`0x00007ffffffffff8`); this libobjc is a plain arm64 build with objc4's other packed isa (`0x0000000ffffffff8`, magic bits above). Any Swift on arm64 here, not an arm64e matter | `build-swift-runtime.nix` |
| 6 | SIGSEGV formatting `%@` in nib loading | AppKit called `-stringByAppendingFormat:`, which its Foundation headers do not declare: compiled as a fixed-argument call, the method read its argument from the stack | AppKit patch 0021 |
| 7 | "Unable to load main nib file", every keyed-archive nib nil | PD's `NSKeyedUnarchiver` read only PD's own archive format, not Apple's (`$archiver`, `$top`, `$objects`, UIDs). AppKit's own nibs are NIBArchives, read by AppKit | Foundation patch (below) |
| 8 | SIGSEGV in `-[NSWindow _updateWSState]` | `strncpy` from a nil window title | 0021 |
| 9 | one missing method ended the process | objc4's `-doesNotRecognizeSelector:` ends the process; on macOS it is an exception the event loop reports. Then: the exception could not pass Foundation's forwarding trampoline (no unwind information) | Foundation patch; AppKit 0021 reports exceptions from run-loop callouts too |
| 10 | `-[NSArray boolValue]` from a binding | key paths with `@count` were not understood (no collection operators) | Foundation patch |
| 11 | SIGSEGV in `CFStringGetLength` from Terminal's `-drawRect:` | `[[NSAttributedString alloc] init]` gave the 16-byte shell `+alloc` makes, read as a CoreFoundation object | Foundation patch |
| 12 | Terminal's own assertion in every `-drawRect:` (`TTView.m:2494`) | `-[NSString cStringUsingEncoding:]` returned the string's internal pointer or NULL; Terminal asks for UTF-16 | Foundation patch |

## Load surface (what was added so that all 733 imports resolve)
Table before: `homebrew/gapscan/terminal-before.md` (214 missing). After: `terminal-after.md` (0).

| Library | Missing before | How | Real or minimal |
|---|---|---|---|
| libswiftCore and overlays | 83 (absent) | `build-swift-runtime.nix`: upstream PureDarwin's build of Swift 6.3.3 from swift.org (Apache-2.0 with runtime exception) and the open 5.4.3/5.9 overlays, at the tip, with the isa mask corrected | real |
| Foundation (Swift half) | 5 of 16 | `swift/SFBridging.swift` in the overlay: Dictionary and Int, Bool bridging | real (element by element) |
| Foundation (Objective-C) | 11 | wrapper `compat/Foundation.m` (re-exports libFoundation and libswiftFoundation, upstream's framework-compat layout): `NSFileTypeForHFSTypeCode`, `_NSDictionaryOfVariableBindings`, `NSScriptCommand`, `NSScriptClassDescription`, the object specifiers | real as data objects; nothing delivers Apple Events |
| CoreFoundation | 14 | wrapper `compat/CoreFoundation.m` (re-exports libCoreFoundation and libFoundation): `CFStringTokenizer*` on ICU's break iterators, three NSURL keys; `_CFWebServicesCopyProviderInfo` | tokenizer real (words, sentences, lines); web services: no provider |
| libSystem | 13 | `libsystem-default/patches/macos26-sdk-imports.patch`: `malloc_type_*` (the type descriptor is not used), `_os_feature_enabled_impl` (no flag files: off), `qtn_proc_*` (object real, applying answers ENOTSUP), `os_variant_is_basesystem`, `strnvis` | real except quarantine |
| AppKit | 37 | patches 0019 (tip's NSStackView, NSAccessibilityElement), 0020 (`NSMacOS26Additions`: 19 constants, accessibility helpers, gesture recognizers that keep their configuration, NSTextFinder without a find bar, NSWindowTab, private classes NSScrollerImpPair, NSFunctionRow, NSFindIndicator, NSInputManager that answer nothing and say so once, and about 60 methods) | mixed; each says in its comment what it does not do |
| CoreGraphics | 11 | in the framework (cocoa patch 0025, `CGPrivateAdditions.m`): font smoothing getters answer what Onyx2D does (grey anti-aliasing, no LCD; not what was last set), CGS/CPS window-server calls answer "not implemented" | minimal |
| CoreText | 2 | in the framework (0023): `CTFontDrawGlyphsWithAdvances` on `CTFontDrawGlyphs`, `CTFontShouldAntiAlias` | real: every glyph Terminal draws goes through the first |
| ColorSync | 11 (absent) | `compat/ColorSync.c`: profile by name, no transform (NULL) | minimal |
| Carbon | 6 | wrapper: no Carbon events, no dead-key state (xkb composes) | minimal |
| AudioToolbox | 6 | wrapper: `AudioServices*` answer errors | minimal: silent bell |
| CoreServices | 5 | wrapper: `CreateTextToUnicodeInfo`, `ConvertFromTextToUnicode` on CFString's converters, `kUTTypeSymLink` | real |
| QuartzCore | 2 | wrapper: `kCATransition`, `kCATransitionFade` | constants |
| DataDetectorsCore, CoreAnalytics, libScreenReader | 4, 2, 0 (absent) | `compat/*.c` | by design: no detector, no analytics, no screen reader |
| ApplicationServices | 2 (absent) | the Cocoa stream's build (with ImageIO, which it re-exports), from `cocoa/build-firefox-root.nix` | SF-U's |

Wrappers (`build-compat.nix`, built against recipes, not against a copy of a running root): a wrapper
stands at the framework's path and re-exports the real library, which stays beside it as `<name>.pd`
(QuartzCore, Carbon, CoreServices, AudioToolbox). CoreFoundation and Foundation need no `.pd`: at
those two paths the root has links to `/usr/lib/lib*.dylib`, and the wrappers re-export those
libraries (Foundation's also the Swift overlay, whose names Apple's programs bind to Foundation; on
macOS the collection classes live in CoreFoundation, so that wrapper re-exports libFoundation too).
This is upstream's own framework-compat layout. Folded into the frameworks' own sources so far:
CoreGraphics and CoreText (above). Still wrappers because their frameworks come from four different
recipes: QuartzCore (two constants), Carbon, CoreServices, AudioToolbox.

Foundation (`build-foundation.nix`, `patches/foundation-macos-applications.patch`, against the tip):
a reader for Apple's keyed archives in `NSKeyedUnarchiver` (class substitution, delegate callbacks,
`-replaceObject:withObject:`, value classes built from Apple's keys, objects known before their
`-initWithCoder:` runs and never retained through it), catchable unrecognized-selector exceptions,
unwind information for `NSForwardingTrampoline`, KVC collection operators, `-mutableArrayValueForKey:`,
key-value observing with key paths, dependent key paths and notifying setters (the setter of the
class is replaced by a thunk that brackets the original with will/did, for all instances; arm64
only; no per-instance subclass, so `object_getClass` of an observed object is its class),
localization tables (`.loctable`), `-[NSAttributedString init]`, a converting
`-cStringUsingEncoding:`, about 30 methods, and two diagnostics (`NSUnrecognizedSelectorBacktrace=1`,
`NSExceptionRaiseBacktrace=1`). Upstream's `PD_OBJC_UNRECOGNIZED=continue` lists many missing
methods in one run.

Reused from SF-U (nothing copied): the whole AppKit stack with patches 0001 to 0018, its NIBArchive
reader, its WindowServer with compositor frames and fractional scale (the M1 compositor answered with
server-side frames at 150%), its layout solver, popups and key equivalents, ApplicationServices and
ImageIO, the patchwork trees (`runs/cocoa/patchwork`; a frozen `d/` = patches 0001 to 0018 was added,
patches 0019 on are `d` to `b`). Reused from the macOS-compatibility stream (issue 45): its guarded
library installer, its QEMU rehearsal and its check, with this job's stage (`SF_COMPAT_STAGE`).
Changed in SF-U's stack by this job: its document has the list ("what the Terminal.app stream changed").

## The system side as recipes
`terminal-app/build-macos-apps-root.nix` is one tree rooted at `/`: everything a root needs beyond
its base so that Terminal runs, every file from a recipe. `sf_terminal.sh stage` tries that tree
from a scratch prefix; `install-stage` splits it for installing.

| Part | Recipe | In the root's own recipe? |
|---|---|---|
| `/usr/lib/dyld`, `/usr/lib/libSystem.B.dylib` | `libsystem-default/build-libsystem.nix` | yes: the default libSystem recipe now has the two dyld patches and the macOS 26 SDK imports (every RAM root and image built from it gets them) |
| `/usr/lib/libFoundation.dylib` | `terminal-app/build-foundation.nix` (upstream's Foundation at the tip + one patch) | laid over the base by the tree; upstream's Foundation recipe is not ours to change |
| `/usr/lib/swift/*` | `terminal-app/build-swift-runtime.nix` | new in the tree |
| AppKit, CoreText, CoreGraphics, Onyx2D, libWindowServer | `cocoa/patched.nix` (patches 0001 to 0027) | yes: the Cocoa stream's root recipes (`build-desktop-root.nix`, `build-firefox-root.nix`) take all five from `patched.nix`, and `macos-compat/build-appkit.nix` is a selection of that root. That stream's own checks were not re-run on 0019 to 0027 |
| the wrappers (CoreFoundation, Foundation, QuartzCore, Carbon, CoreServices, AudioToolbox), ColorSync, DataDetectorsCore, CoreAnalytics, libScreenReader | `terminal-app/build-compat.nix` | new; links against recipes (libSystem, libobjc, CoreFoundation, Foundation, ICU, the Swift runtime, the Cocoa root, `macos-compat/build-coreservices.nix`, the sound stream's AudioToolbox), not against a copy of a running root |
| ApplicationServices, ImageIO | `cocoa/build-firefox-root.nix` | the Cocoa stream's |
| `/usr/bin/open` | `terminal-app/system/open.c` (`open --listen <pipe>`: the session's listener for the desktop menu's Applications rows) | new |
| `/usr/bin/login` | `terminal-app/system/login.c` (`login -pf <user>`, `-pfl <user> <program> ...`: only pre-authenticated logins; no utmpx, no "Last login") | new |
| the installer's probes (`/usr/share/sf-test/sf-*-probe`) | `terminal-app/probe/` | new |

`macos-compat/build-compat-root.nix` ("what a SiliconForest root gets for macOS programs") lays the
tree over its AppKit and Perl parts. The storage stream's tools lay only paths the base lacks
(`sf_userland_push.sh push sys`), so the files that replace the base's go on as a part of their own:
**`m1-storage/tools/sf_userland_push.sh push apps`** runs `sf_terminal.sh install new`, `install core`
and `install direct` in that order on the running root (and `verify apps` compares every file
there with the tree). The five files every program loads are only sent when their SHA-256 is in
`terminal-app/core-rehearsed.sha256`, the record of the QEMU rehearsals that passed
(`SF_USERLAND_DRY=1 ... push apps` checks that and prints what it would run). A fresh root also
gets root's shell (`m1-storage/userland/sf-userland-install.sh`: zsh where `/bin/zsh` is there),
the session's listener (`m1-storage/system/sf-session`) and, once it is installed as the desktop,
the compositor with popups and the Applications menu (`terminal-app/compositor/README.md`: built
from patches against the local repository, not by a recipe yet).

## Installing
Five files are loaded by every program: dyld, libSystem, libFoundation, and the two framework paths
`CoreFoundation.framework/Versions/A/CoreFoundation` and `Foundation.framework/Versions/C/Foundation`
(links to `/usr/lib` on the root; the wrappers replace the links, and programs built on macOS name
exactly those paths). They go through:
1. `sf_terminal.sh qemu <name>` then `qemu <name> second`: the M1's root image in QEMU
   (`macos-compat/tools/sf_compat_qemu.sh` with this job's stage), the replacements done there as on
   the M1, the macOS-compatibility check, then a fresh boot of the root that leaves.
2. On the M1, `sf_terminal.sh install core`: for each library `macos-compat/sf-lib-install.sh`
   (the new file written beside the installed one, hashed there before anything loads it, the probe
   and ls, sh, node, claude, python3, bash, git, vim, nvim, tmux run with it through `DYLD_*`, the file
   in use kept as `.old`, renamed, the same programs as new processes; a failed confirmation renames
   back). An installed path that is a link is kept as a link.
3. `/usr/lib/dyld` last, by `guest/sf-dyld-install.py`: no `DYLD_*` variable reaches the linker,
   so programs are run as copies that name the new file (`/usr/lib/dyl2`, the same length) as their
   linker; the swap and its way back are `link(2)`/`rename(2)` inside one Python process, so a linker
   that does not start programs is undone without starting one.
Everything else (`install new`, `install direct`): `guest/sf-apps-install.sh` hashes each file on
the target, keeps an existing file (`<path>.pd` where the new file is its wrapper, `<path>.old-sfy`
otherwise) and records what it did in `/private/var/db/sf-macos-apps.installed`.

### What was run (13 and 14 November)
| Step | Result | Where |
|---|---|---|
| QEMU, the M1's root image with macos-compat's additions | libSystem, libFoundation, both wrappers, then dyld replaced as on the M1; macos-compat check 18 of 18; `QDDESK_PASS`, `COMPAT_VERDICT failed=0` | `runs/m1-storage/qemu-dr/ty4/serial.log` |
| QEMU, a fresh boot of the root that run left | the five probes and node, claude, python3, bash, git, vim, nvim, tmux as new processes, the check again: `QDDESK_PASS`, 33 verdicts ok, `failed=0` | `runs/m1-storage/qemu-dr/ty4-2/serial.log` |
| (earlier rehearsals) | `ty1`: a root without macos-compat's additions (its check failed for that reason; the five replacements passed); `ty2`, `ty3`: stopped, the stage was rebuilt | same directory |
| M1: `install new`, `install app` | 35 files and 11 links the system lacked; the bundle, 129 files equal | `runs/sf-terminal/m1-install/` |
| M1: `install core` | libSystem (the file of before is `.old3`), libFoundation (`.old`), CoreFoundation and Foundation wrappers (the links kept as `.old`), dyld (`.old`): each `SFLIB 3 installed` / `SFDYLD 3 installed` with the ten programs at the statuses they had | `runs/sf-terminal/m1-install/*.txt` |
| M1: `install direct` | AppKit, CoreText, CoreGraphics, Onyx2D, libWindowServer replaced (kept as `.old-sfy`), QuartzCore, Carbon, CoreServices, AudioToolbox wrapped (the library kept as `.pd`), `/usr/bin/open` new | `direct.txt` |
| M1: regression, before and after | `macos-compat/tools/sf_compat_push.sh check`: 18 of 18 ok both times; `sf-claude-check.sh`: the same statuses and the same two TUI screens | `runs/sf-terminal/regress/` |
| M1: restart, then `open -a Terminal` | the unmodified bundle runs, commands typed into it run | `shots/v1.png`, `w1.png` |
| 14 November. QEMU `ty6`, `ty6-2` | the second libFoundation (`ad2e75a9…`) with the unchanged other four: replaced as on the M1, `COMPAT_VERDICT failed=0`, `QDDESK_PASS`; fresh boot of the result: 38 verdicts ok, `failed=0`, `QDDESK_PASS` | `runs/m1-storage/qemu-dr/ty6/serial.log`, `ty6-2/serial.log`; `terminal-app/core-rehearsed.sha256` |
| M1: `sf_userland_push.sh push apps` | libFoundation replaced (`SFLIB 3 installed`, the one of before is `.old2`; its probe ends with status 139 on the first build and 0 on the second); the other four and dyld `already installed`; 80 direct files as they were | `runs/sf-terminal/m1-install/push-apps-2.txt` |
| M1: `sf_terminal.sh revert direct`, then `push apps` again | 56 entries reverted (47 new files and links removed, 5 replaced and 4 wrapped files renamed back: Terminal then stops at the first missing framework); installed again: 36 new files, 11 links, 5 replaced, 4 wrapped | `revert-direct.txt`, `push-apps-3.txt` |
| M1: `verify apps` | 80 of 81 files equal, one differs: `/usr/lib/libSystem.B.dylib` in 4 bytes (below) | `push-apps-3.txt` |
| M1: regression | `sf_compat_push.sh check`: 18 of 18 ok, `failed=0` | `runs/sf-terminal/regress/compat-check-1114.txt` |
| M1: the compositor | `/usr/bin/chonkstep-darwin-desktop` is the build with popups and Applications and without the inject module (`9115579a…`; the one of before, `721574af…`, is `.old-daily`); `.next` is the same source with the inject module (`810a1569…`) | `runs/m1-desktop-010/wm-darwin-apps-keep/` |
| 14 November, later. M1: regression after this round's installs | `sf_compat_push.sh check`: 18 of 18 ok, `failed=0` | `runs/sf-terminal/regress/compat-check-1114b.txt` |
| QEMU: the Cocoa stream's checks on patches 0001 to 0039 with `-O2` | `hello --scale 2 --theme graphite`, `frame --scale 2`, `popups --scale 2`, `apps --scale 1.5`: pass. `apps --scale 1.5 --theme amber-phosphor`: FAILED (the located operator-key box took in orange pixels at the Calculator's title, so the click landed below the keys; explained and fixed in the third round, below) | `runs/sf-terminal/regress/cocoa-*-1114b.txt` |
| M1: restart (test build, one marked boot) | from the desktop menu: Applications > Terminal; a command; Shell > New Window > Pro; Command-W twice ends the program | `shots/fin1`..`fin6` |
| M1: restart (installed build, no marks) | the session runs `/usr/bin/chonkstep-darwin-desktop` and the listener; no inject directory; a bundle path written into the session's pipe starts Terminal with zsh on a pty (the menu row itself was exercised in the test build of the same source). No capture of the screen exists for this boot: the installed build cannot make one | process list over SSH |
| 14 November, third round. QEMU `ty7`, `ty7-2` | libSystem `02f56b7e…` (feature flags) with the other four unchanged and the rehearsed dyld `472fd9a3…` (the recipe's rebuild of dyld differs only in its debug map: `sf_terminal.sh install-stage` with `SF_DYLD`): replaced as on the M1, `COMPAT_VERDICT failed=0`, `QDDESK_PASS`; fresh boot: 33 verdicts ok, `failed=0`, `QDDESK_PASS` | `runs/m1-storage/qemu-dr/ty7/serial.log`, `ty7-2/serial.log` |
| M1: `push apps`, then `install core` | the first push said libSystem "already installed" (its probe passed with the old library); the probe now also reads the flag: `install core` replaced libSystem (`SFLIB 3 installed`, the one of before is `.old4`; the ten programs at the statuses they had; tmux `-V` exits 1 with either library). `install new`: the fonts and the feature flag plist (the terminfo database was laid first by hand: `/private/var/db/sf-terminfo.installed`) | `runs/sf-terminal/m1-install/push-apps-y2.txt`, `core-y2.txt`, `new-y2.txt` |
| M1: AppKit, Onyx2D, libWindowServer, `/usr/bin/open`, the desktop terminal | replaced directly by `runs/sf-terminal/y2/put.sh` (the files of before are `<path>.pre-y2`); each the nix build of the patches committed (AppKit `e610800f…`, equal to the dev build) | - |
| M1: regression | `sf_compat_push.sh check`: 18 of 18 ok, `failed=0` | `runs/sf-terminal/regress/compat-check-y2.txt` |
| QEMU: the Cocoa stream's checks | `hello --scale 2 --theme graphite`, `frame --scale 2`, `popups --scale 2` (AppKit up to 0049), `apps --scale 1.5`, `apps --scale 1.5 --theme amber-phosphor` (up to 0050): all pass. The amber-phosphor failure of before was the check's: the compositor draws window titles in amber there, a few anti-aliased edge pixels of the Calculator's title fell within the tolerance of the operator keys' orange and the located box took in the title bar (1.5 px/pt measured as 3.83). The Cocoa stream recorded apps passing at 100% nextstep-classic, 150% teal-blueprint and 200% graphite, never at amber-phosphor. `cocoa_run.py`'s locate now leaves out rows and columns with only a few matching pixels: 1.52 px/pt | `runs/sf-terminal/regress/cocoa-*-y2.txt`, `cocoa-y2-summary.txt` |
| Night of 14 November, fourth round. QEMU `ty8` | libFoundation with the key-value coding cache: FAILED (`COMPAT_VERDICT failed=2`): its probe's 200 ms timing bound is too slow under TCG. The probe now finds the cache by the symbol `PDFoundationKeyValueCodingCache` | `runs/m1-storage/qemu-dr/ty8/serial.log` |
| QEMU `ty9`, `ty9-2` | libFoundation `5fdf0875…` with the other four unchanged: replaced as on the M1, `COMPAT_VERDICT failed=0`, `QDDESK_PASS`; fresh boot: `failed=0`, `QDDESK_PASS` | `qemu-dr/ty9/`, `ty9-2/`; `core-rehearsed.sha256` |
| M1: `install core libFoundation.dylib` | `SFLIB 3 installed`; the one of before is `.old3` | commit `1c191fd87` |
| M1: AppKit, CoreText, Onyx2D, libWindowServer, AudioToolbox wrapper | replaced directly by `runs/sf-terminal/t4/put.sh` (the files of before are `<path>.pre-t4`), each the nix build of the committed patches; AppKit and libWindowServer twice more as patches were added (last: AppKit `527784f2…`, equal to the dev build that was tested) | `runs/m1-coord/log.txt` |
| QEMU `ty10` | libFoundation that also asked the file system in `-initFileURLWithPath:`: first pass `failed=0`, `QDDESK_PASS`; stopped in the second pass, because on the M1 (test prefix) this build left Terminal's Preferences window empty. Not installed | `qemu-dr/ty10/` |
| QEMU `ty11`, `ty11-2` | libFoundation `193b520a…` (`+[NSURL fileURLWithPath:]` of a directory ends in "/"; the probe checks it) with the other four unchanged: replaced as on the M1, `COMPAT_VERDICT failed=0`, `QDDESK_PASS`; fresh boot: `failed=0`, `QDDESK_PASS` | `qemu-dr/ty11/`, `ty11-2/`; `core-rehearsed.sha256` |
| M1: `install core libFoundation.dylib` | `SFLIB 3 installed`, the one of before (ty9's) is `.old4`; the probe 1 with the library in use (it lacks the fourth build), 0 with the new one; node, claude, python3, bash, git, vim, nvim 0 with both, tmux `-V` 1 with both. Then `open -a Terminal <folder>` (as `-NSOpen`) gave the folder, and Preferences drew | `runs/sf-terminal/m1-install/core-t4.txt` |
| M1: regression | `sf_compat_push.sh check`: 18 of 18 ok, `failed=0` | `runs/sf-terminal/regress/compat-check-t4.txt` |
| QEMU: the Cocoa stream's checks (AppKit up to 0059) | `hello --scale 2 --theme graphite`, `frame --scale 2`, `popups --scale 2`, `apps --scale 1.5 --theme amber-phosphor`: pass. `apps --scale 1.5`: FAILED the first time (the typed command's `COCOATERM_OK` did not arrive within 120 s; the keys reached the window; two other QEMU guests ran at the same time), passed when run again alone | `runs/sf-terminal/regress/cocoa-*-t4.txt`, `cocoa-t4-summary.txt`, `cocoa-apps_scale_1.5-t4-rerun.txt` |

The dyld and libSystem that were rehearsed and installed are what
`libsystem-default/build-libsystem.nix` builds (sha256 `472fd9a3…` and `dfc854f1…`); libFoundation
`ad2e75a9…` (13 November: `ac8a048b…`), the wrappers `525419ad…` and `c4d12d0d…`. Not replaced:
libobjc, libCoreFoundation (unchanged) and `/usr/lib/system/libdyld.dylib`: the recipe's and the
installed one have the same code and data. Their 570 differing bytes are the modification times of
31 object files in the debugging symbol table (`N_OSO` entries: two builds of the same source, 12
and 14 November) and the code signature's hashes of the 15 pages that hold them. Nothing to install.

**The installed libSystem reads back with 4 bytes changed.** `/usr/lib/libSystem.B.dylib` on the M1
was verified as `dfc854f1…` before it was installed; read now it is `e5267447…`: four instruction
words in one page of `__text` (file offsets 0x27c000, 0x27c0a8, 0x27c210, 0x27c31c) are `nop`
(`d503201f`) where the file has `xpaclri` (`d50320ff`). That is what upstream's kernel does to
pointer-authentication instructions in pages it maps executable (section "arm64e"), here visible
through `read(2)`. Whether the page on the disk changed or only the cached page was not established
(it needs the raw block; the kernel is not this job's). Consequences: a hash of a library in use
is not a reliable check on this kernel, and `verify apps` names this file each time.

## On the M1's disk and how to revert
- The five core files: `ln <path>.old <path>.back && mv -f <path>.back <path>` each (libSystem's is
  `.old3`; libFoundation's `.old2` is the first patched build, `.old` the base's; for the two
  framework paths the kept file is a link: `ln -s "$(readlink <path>.old)"
  <path>.back && mv -f <path>.back <path>`). dyld first if all are to go: the old dyld starts every
  program with any of the libraries.
- Everything `install new` and `install direct` put there: `sf_terminal.sh revert direct`
  (`/private/var/db/sf-macos-apps.installed` is the list; new files are removed, `.old-sfy` and
  `.pd` files renamed back).
- `/Applications/Utilities/Terminal.app`: remove it. `/var/root/Library/Preferences/com.apple.Terminal.plist`
  and `/var/root/Library/Logs/Terminal.log` are what running it leaves.
- `/usr/bin/login`: remove it (it was put there before the installer's list existed and is not on
  it; Terminal then starts `/bin/sh` again).
- Root's shell: `/etc/passwd.old-sfy` and `/etc/master.passwd.old-sfy` are the files of before
  (`/bin/sh`).
- The session script: `/usr/libexec/sf-session.before-apps` is the one without the listener,
  `sf-session.old` the one of 13 November morning.
- The compositor: `ln /usr/bin/chonkstep-darwin-desktop.old-daily /usr/bin/chonkstep-darwin-desktop.back
  && mv -f /usr/bin/chonkstep-darwin-desktop.back /usr/bin/chonkstep-darwin-desktop` brings back the
  build of 12 November (no popups: then Mac applications have no menus, and no Applications row).
- The test compositor is `/usr/bin/chonkstep-darwin-desktop.next`. It runs only in the one boot that
  finds `/private/var/db/desktop-next-try`, and takes requests only when `/private/var/db/desktop-inject`
  exists. Both marks were removed on 14 November: the M1 runs the installed build, which has no
  inject module. To test again: `touch` both marks as root and restart (the first is renamed to
  `desktop-next-tried` when it is used).
- From the first day, from the Cocoa stream's root: `/System/Library/Fonts/{Inter,SourceCodePro,URW}`,
  `/etc/fonts/**`, `/share/fontconfig/**`, `/System/Library/Themes/Default.plist`,
  `/var/cache/fontconfig` (`runs/sf-terminal/m1-data-files.txt`, 129 paths). Fonts are open:
  Inter 4.1 (SIL OFL 1.1), URW base35 (AGPL-3.0 with the font exception), Source Code Pro (SIL OFL
  1.1). Nothing from macOS but Terminal.app itself; the font Terminal draws with is inside its bundle
  and is read from there (`PDApplicationFonts=NO` in the environment or its defaults turns that off).
- 14 November, later (this round): AppKit, CoreText, Onyx2D, CoreGraphics and libWindowServer of
  before are `<path>.pre-perf` (hard links; `ln -f <path>.pre-perf <path>.back && mv -f <path>.back
  <path>` each), the base's are still `.old-sfy`; `/usr/bin/login.before-fork` is the login that
  exec'd the shell; the compositors of before are `/usr/bin/chonkstep-darwin-desktop.before-cmdq`
  and `.next.before-cmdq`. `/var/cache/fontconfig` holds the cache of Terminal's font directory
  (written at its first launch). Test copies: `/private/var/db/sfy/{devO,wsdev*,pre}` (remove).
- `/private/var/db/sfy` (the scratch prefix, `/private/tmp/sfy` links to it after `ln -sfn`): remove.
- 14 November, third round: AppKit, Onyx2D, libWindowServer, `/usr/bin/open` and
  `/usr/bin/chonk-terminal-darwin` of before are `<path>.pre-y2` (`ln -f <path>.pre-y2 <path>.back &&
  mv -f <path>.back <path>` each); libSystem of before is `/usr/lib/libSystem.B.dylib.old4` (as for
  the core files above); the terminfo files laid by hand are listed in
  `/private/var/db/sf-terminfo.installed` (remove each); the fonts and the feature flag plist are
  on `/private/var/db/sf-macos-apps.installed` (`sf_terminal.sh revert direct`). Terminal's
  preferences file holds what the tests changed (window frames; CursorBlink was set and unset).
- Fourth round (night of 14 November): AppKit, CoreText, Onyx2D, libWindowServer and the AudioToolbox
  wrapper of before are `<path>.pre-t4` (hard links: `ln -f <path>.pre-t4 <path>.back && mv -f
  <path>.back <path>` each; AppKit `/System/Library/Frameworks/AppKit.framework/Versions/A/AppKit`,
  CoreText and Onyx2D likewise under their frameworks, `/usr/lib/libWindowServer.dylib`,
  `/System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox`). Installed now:
  AppKit `527784f2…`, CoreText `348df207…`, Onyx2D `872a34b6…`, libWindowServer `84eb8bf4…`,
  AudioToolbox `ac4f1f0a…`. libFoundation (as for the core files above): installed now `193b520a…`
  (ty11); ty9's (the key-value coding cache alone) is `/usr/lib/libFoundation.dylib.old4`; the
  build of before the cache is `.old3`. Test prefixes `/private/var/db/sfy/{tdev,tpre,tdevnf}`
  (remove). Terminal's preferences file holds what the tests changed (window frames, a restored window, the Secure
  Keyboard Entry default set and unset). `/private/tmp/oaf` is a test folder.

## Launching
From the desktop menu (right click on the desktop, or Command-Space): **Applications > Terminal**.
The submenu has one row per bundle in `/Applications` and `/Applications/Utilities`
(`compositor/wm-darwin-applications.patch`). The compositor starts no program: it writes the
bundle's path into the pipe `$XDG_RUNTIME_DIR/open-requests`, and the session's listener
(`open --listen`, started by `sf-session` with the desktop's environment) opens it; without a
listener the row says that the session takes no requests. Or type `open -a Terminal` /
`open /Applications/Utilities/Terminal.app` in a terminal or over SSH (`/usr/bin/open`: the bundle's
executable in a session of its own, output in `~/Library/Logs/<name>.log`). Each `open` starts a
new instance: there are no Apple Events to hand a request to a running one.

## Tools
- `tools/sf_terminal.sh`: stage, push, run, shot, scan (scratch prefix); install-stage, qemu,
  install, launch, revert.
- `tools/sf_inject.sh`, `tools/sf_keys.py`: typed text, keys, chords, pointer and captures of the
  screen through the compositor's inject module (`compositor/wm-darwin-inject.patch`, against the
  source of the compositor the disk has; built with `chonkstep-darwin/tools/build_desktop_binaries.sh`).
  `m1-storage/system/sf-session` runs `/usr/bin/chonkstep-darwin-desktop.next` for one boot when
  `/private/var/db/desktop-next-try` exists and gives the compositor its inject directory when
  `/private/var/db/desktop-inject` exists.
- The compositor with the inject module is a test build: `compositor/README.md` says how it is
  built, how it is kept out of daily use, and how to run it for one boot. Captures of the screen
  need it.
- `probe/sf-kvo-probe.m`: the installer's proof of a new libFoundation; extend it when the library
  gains something the installed build lacks, or the installer will say "already installed".
- `probe/sf-trace.c` (+ `sf-trace-thunks.S`): the methods of chosen classes as a program calls them,
  with arguments, results, objects named, and times (`DYLD_INSERT_LIBRARIES`, `SFTRACE=...`). How the
  causes above were found.
- `tools/sf_methods.py`: where an application's methods are and what each calls (reads the binary).
- `tools/sf-catch.c`: runs a program and reports a Mach exception from outside.
- `probe/sf-nibprobe.m`: decodes a compiled nib as NSNib does and names what stands in the way.
- `homebrew/sf_gapscan.py --app`.
- `probe/sf-prof.c` (`libsfprof.dylib`, built by `stage`): a sampling profiler of the main thread
  (`DYLD_INSERT_LIBRARIES`, `touch /tmp/sfprof.on` to start, `rm` to stop and write `/tmp/sfprof.txt`;
  `SFPROF_FOCUS`, `SFPROF_PROGRAM`). How every cause of slowness above was found.
- `tools/sf-latency.py` (on the target): `launch`, `output N`, `inject REQUEST`: ms from exec, from a
  write to the pty, from an injected request to the WindowServer's commits (`PD_WS_TIMING=1`).
  `trace REQUEST [settle]`: every frame of each surface after a request (count, sizes, gaps) and
  the time from each compositor configure to the frame of that size (drags, wheel, floods);
  `ctrlc PROGRAM`: Control-C while PROGRAM runs, ms until it is gone. `sf-prof` also writes each
  sample's whole stack (`/tmp/sfprof.txt.stacks`) for inclusive times.

## Commands
```sh
T=integrations/puredarwin/terminal-app/tools/sf_terminal.sh
$T stage && $T scan && $T push          # the tree, tried from /private/tmp/sfy (nothing installed)
$T install-stage                        # runs/sf-terminal/install
$T qemu NAME && $T qemu NAME second     # the rehearsal and the fresh boot of its result (25 + 20 minutes)
$T install new; $T install app; $T install core; $T install direct
integrations/puredarwin/m1-storage/tools/sf_userland_push.sh push apps   # the three installs, core files only as rehearsed; verify apps
$T revert direct                        # undo install new and install direct (then push apps again)
$T launch 20                            # or on the target: open -a Terminal
I=integrations/puredarwin/terminal-app/tools/sf_inject.sh
$I 'text:echo hello\n' key:up chord:ctrl+c wait:500 capture:name     # runs/sf-terminal/shots/name.png
```
Pending: the M1's own Terminal (macOS 27.0), which needs macOS running to fetch.
