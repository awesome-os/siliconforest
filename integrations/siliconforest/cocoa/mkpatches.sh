#!/bin/bash
# SPDX-License-Identifier: MIT
# Regenerates ./patches from the patchwork trees under runs/cocoa/patchwork (not committed;
# docs/roadmap/issues/SF-U-cocoa.md says how to rebuild them): a/ the pin, frozen trees after each
# set of patches (c/ 0015, d/ 0018, e/ 0027, f/ 0033, g/ 0039), b/ now. Edit b/, then run this; each
# function diffs one frozen tree against the next (the newest set: g/ against b/), and within one
# set a file belongs to one patch. Every file of a patch must be in every tree (copy the pin's
# version into the frozen trees when a set starts to touch a file the trees lacked).
#   bash integrations/siliconforest/cocoa/mkpatches.sh
cd /home/chrisk/src/darwin-lab/runs/cocoa/patchwork; P=/home/chrisk/src/darwin-lab/integrations/siliconforest/cocoa/patches
# a/ = the pin, c/ = after patches 0001-0015 (the first agent's work, frozen), b/ = now.
# mkpatch: a -> c (do not edit c/). mknew: c -> b, the patches from 0016 on; a file belongs to one of them.
mkpatch(){ out=$1; shift; : > $P/$out; for f in "$@"; do diff -uN --label a/$f --label b/$f a/$f c/$f >> $P/$out; done; echo "$out $(wc -l < $P/$out)"; }
mknew(){ out=$1; shift; : > $P/$out; for f in "$@"; do diff -uN --label a/$f --label b/$f c/$f d/$f >> $P/$out; done; echo "$out $(wc -l < $P/$out)"; }
# d/ = after patches 0001-0018 (the Cocoa stream as parked, frozen). mkterm: d -> b, the Terminal.app stream's patches (0019 on).
mkterm(){ out=$1; shift; : > $P/$out; for f in "$@"; do diff -uN --label a/$f --label b/$f d/$f e/$f >> $P/$out; done; echo "$out $(wc -l < $P/$out)"; }
# e/ = after patches 0001-0027 (frozen). mkty: e -> b, the Terminal.app stream's second set (0028 on).
mkty(){ out=$1; shift; : > $P/$out; for f in "$@"; do diff -uN --label a/$f --label b/$f e/$f f/$f >> $P/$out; done; echo "$out $(wc -l < $P/$out)"; }
A=src/Frameworks/AppKit
mkpatch 0001-tip-appkit-font-weights-symbol-configuration.patch $A/NSFontDescriptor.h $A/NSFontDescriptor.m $A/NSImageSymbolConfiguration.h $A/NSImageSymbolConfiguration.m $A/AppKit.h
mkpatch 0002-appkit-sharing-service-picker.patch $A/NSSharingService.h $A/NSSharingService.m
mkpatch 0003-authenticationservices-web-session-browser-side.patch src/Frameworks/AuthenticationServices/AuthenticationServices.m
mkpatch 0004-windowserver-server-decorations-and-scale.patch src/Frameworks/WindowServer/message.h src/Frameworks/WindowServer/rpc.h src/Frameworks/WindowServer/rpc_wayland.c
mkpatch 0005-appkit-server-decorations-and-scale.patch $A/NSWindow.h $A/NSWindow.m $A/NSWindow-Private.h $A/NSApplication.m $A/NSThemeFrame.m $A/NSScreen.m $A/NSDisplay.m $A/NSView.h $A/NSView.m
mkpatch 0006-onyx2d-glyphs-at-device-scale.patch src/Frameworks/Onyx2D/O2Context_builtin.m
mkpatch 0009-security-secitemcopymatching-defined-result.patch src/Libraries/Security/apple/OSX/sec/Security/SecItem.c
mkpatch 0011-appkit-layout-api.patch $A/NSViewLayout.h $A/NSViewLayout.m $A/NSLayoutConstraint.m $A/NSModernAppKit.h $A/NSModernAppKit.m $A/SFLayoutSolver.h $A/SFLayoutSolver.m
mkpatch 0012-appkit-controls-macos26.patch $A/NSButtonCell.m $A/NSPopUpButtonCell.m $A/NSTextFieldCell.m $A/NSSliderCell.m $A/NSSlider.m $A/NSProgressIndicator.m $A/NSSegmentedControl/NSSegmentedCell.m $A/NSInterfacePart/SFControlDrawing.m $A/NSStringDrawer.m
mkpatch 0013-onyx2d-png-straight-alpha-byte-order.patch src/Frameworks/Onyx2D/O2Encoder_PNG.m
mkpatch 0015-appkit-popup-view-array.patch $A/NSPopUpView.m $A/NSInterfacePart/NSGraphicsStyle.m
# every file that differs between c/ and b/ must be named in a mknew line below
mknew 0016-appkit-event-queue-discard.patch $A/NSDisplay.m
mknew 0017-appkit-popups.patch $A/NSPopUpView.m $A/NSMenu.subproj/NSMenuView.h $A/NSMenu.subproj/NSMenuView.m $A/NSMenu.subproj/NSMenu.m $A/NSPopUpButtonCell.m $A/NSComboBoxCell.m $A/NSToolTipWindow.m $A/NSWindow.h $A/NSWindow.m $A/NSApplication.m
mknew 0018-windowserver-popup-placement.patch src/Frameworks/WindowServer/message.h src/Frameworks/WindowServer/rpc.h src/Frameworks/WindowServer/rpc_wayland.c
# Terminal.app stream (docs/roadmap/issues/SF-Y-terminal-app.md)
mkterm 0019-tip-appkit-stackview-accessibilityelement.patch $A/NSStackView.h $A/NSStackView.m $A/NSAccessibilityElement.h $A/NSAccessibilityElement.m
mkterm 0020-appkit-macos26-additions.patch $A/NSMacOS26Additions.h $A/NSMacOS26Additions.m $A/AppKit.h
mkterm 0021-appkit-fixes-found-with-terminal.patch $A/nib.subproj/NSNibLoading.m $A/nib.subproj/NSNib.m $A/nib.subproj/NSWindowTemplate.m $A/NSApplication.m $A/NSFontPanel.m $A/NSPathControl.m $A/NSViewController.m $A/NSWindow.m
mkterm 0022-windowserver-presented-frame-dump.patch src/Frameworks/WindowServer/rpc_wayland.c
mkterm 0023-coretext-fixed-pitch-families-private-draw.patch src/Frameworks/CoreText/CTRegistry.m src/Frameworks/CoreText/KTFont.m src/Frameworks/CoreText/CTFont.m
mkterm 0024-appkit-fonts-bindings-found-with-terminal.patch $A/NSFont.m $A/NSController/NSArrayController.m $A/NSKeyValueBinding/NSKVOBinder.m $A/NSKeyValueBinding/NSBinder.m
mkterm 0025-coregraphics-private-additions.patch src/Frameworks/CoreGraphics/CGPrivateAdditions.m
mkterm 0026-onyx2d-copy-bits.patch src/Frameworks/Onyx2D/O2Context_builtin.m
mkterm 0027-appkit-layout-once-per-resize.patch $A/NSView.m $A/NSLayoutConstraint.m
# 0028 on: mkty lines
mkty 0028-appkit-window-menu-strip.patch $A/NSWindowMenuStrip.m $A/NSMenu.subproj/NSMenuView.h $A/NSMenu.subproj/NSMenuView.m $A/NSThemeFrame.m $A/NSWindow.m $A/NSWindow-Private.h $A/NSApplication.m
mkty 0029-appkit-fixes-found-with-terminal-2.patch $A/NSTabView.m $A/NSFoundWithTerminalAdditions.m $A/NSToolbar.subproj/NSToolbar.m $A/NSToolbar.subproj/NSToolbarView.m $A/NSWorkspace.m $A/NSDisplay.m
mkty 0030-windowserver-app-id-without-bundle-identifier.patch src/Frameworks/WindowServer/rpc_wayland.c
mkty 0031-appkit-popup-button-content-bindings.patch $A/NSPopUpButton.m
mkty 0032-appkit-measured-sizes-remembered.patch $A/NSStringDrawer.h $A/NSStringDrawer.m
mkty 0033-appkit-scroller-look-macos26.patch $A/NSScroller.m $A/NSInterfacePart/SFControlDrawing.m
( cd /home/chrisk/src/darwin-lab/runs/cocoa/patchwork; diff -rq e b ) | sed 's/^/differs from e: /'
# f/ = after patches 0001-0033 (frozen). mkperf: f -> g, the Terminal.app stream's third set (0034 to 0039).
mkperf(){ out=$1; shift; : > $P/$out; for f in "$@"; do diff -uN --label a/$f --label b/$f f/$f g/$f >> $P/$out; done; echo "$out $(wc -l < $P/$out)"; }
# g/ = after patches 0001-0039 (frozen). mkg: g -> h, the Terminal.app stream's fourth set (0040 to 0050).
mkg(){ out=$1; shift; : > $P/$out; for f in "$@"; do diff -uN --label a/$f --label b/$f g/$f h/$f >> $P/$out; done; echo "$out $(wc -l < $P/$out)"; }
mkperf 0034-coretext-graphics-font-cache-font-directories.patch src/Frameworks/CoreText/KTFont.m src/Frameworks/CoreText/CTRegistry.m src/Frameworks/CoreText/CTInternal.h src/Frameworks/CoreText/CTFontManager.m src/Frameworks/CoreText/CTFont.m
mkperf 0035-appkit-glyph-advances-font-directory-menu-archive.patch $A/NSApplication.m $A/KTFont_FT.m $A/O2Font_FT.m $A/NSMutableAttributedString.m
mkperf 0036-windowserver-present-on-release-tab-toplevel.patch src/Frameworks/WindowServer/rpc_wayland.c src/Frameworks/WindowServer/message.h
mkperf 0037-onyx2d-font-by-name-once.patch src/Frameworks/Onyx2D/O2Font_ttf.m
mkperf 0038-appkit-found-with-terminal-3.patch $A/NSFoundWithTerminalAdditions.m $A/NSFontFamily.m
mkperf 0039-appkit-window-tabs.patch $A/NSWindowTabbing.m $A/NSWindow.m $A/NSThemeFrame.m $A/NSMacOS26Additions.m
( cd /home/chrisk/src/darwin-lab/runs/cocoa/patchwork; diff -rq f g ) | sed 's/^/differs from f: /'
mkg 0040-appkit-dirty-rects-as-drawn.patch $A/NSView.m
mkg 0041-appkit-windows-opaque-untitled-sheets.patch $A/NSWindow.m $A/NSThemeFrame.m
mkg 0042-tip-onyx2d-icns-png-entries.patch src/Frameworks/Onyx2D/O2ImageSource_ICNS.m
mkg 0043-windowserver-sheet-parent.patch src/Frameworks/WindowServer/message.h src/Frameworks/WindowServer/rpc_wayland.c
mkg 0044-appkit-alerts-macos-layout.patch $A/NSAlert.m $A/NSPanel.m $A/NSApplication.m $A/NSImage.m
mkg 0045-appkit-word-wrap-at-fragment-start.patch $A/NSTextView.subproj/NSTypesetter_concrete.m
mkg 0046-appkit-nib-floats-labels-menu-glyphs.patch $A/nib.subproj/NSNibArchiveUnarchiver.m $A/NSStringDrawer.m $A/NSMenu.subproj/NSMenuView.m
mkg 0047-appkit-bindings-write-back.patch $A/NSControl.m $A/NSController/NSControllerSelectionProxy.m
mkg 0048-appkit-find-bar.patch $A/NSTextFinder.m $A/NSMacOS26Additions.m $A/NSMacOS26Additions.h $A/NSResponder.m
mkg 0049-appkit-font-lookups-remembered.patch $A/O2Font_FT.m $A/NSMutableAttributedString.m
mkg 0050-appkit-tab-view-macos26.patch $A/NSInterfacePart/NSGraphicsStyle.m
( cd /home/chrisk/src/darwin-lab/runs/cocoa/patchwork; diff -rq g h ) | sed 's/^/differs from g: /'
# h/ = after patches 0001-0050 (frozen, 15 November). mkh: h -> b, the Terminal.app stream's fifth set (0051 on).
mkh(){ out=$1; shift; : > $P/$out; for f in "$@"; do diff -uN --label a/$f --label b/$f h/$f b/$f >> $P/$out; done; echo "$out $(wc -l < $P/$out)"; }
mkh 0051-appkit-draw-once.patch $A/NSWindowMenuStrip.m $A/NSMenu.subproj/NSMenuView.m $A/NSWindowTabbing.m
mkh 0052-appkit-full-screen-key-equivalents.patch $A/NSWindow.m $A/NSMenu.subproj/NSMenu.m $A/NSApplication.m
mkh 0053-coretext-lines-and-fonts-remembered.patch src/Frameworks/CoreText/CTLine.m src/Frameworks/CoreText/CTTypesetter.m
mkh 0054-onyx2d-rectangles-without-edges.patch src/Frameworks/Onyx2D/O2Context_builtin.m
mkh 0055-appkit-save-open-panels.patch $A/SFFilePanel.m $A/NSSavePanel.m $A/NSOpenPanel.m
mkh 0056-appkit-print-to-pdf-help.patch $A/NSDisplay.m $A/NSPrintOperation.m $A/NSHelpManager.m
mkh 0057-appkit-rects-outside-display.patch $A/NSView.m
mkh 0058-windowserver-activation-configure-timing.patch src/Frameworks/WindowServer/rpc_wayland.c
mkh 0059-appkit-found-walking-the-menus.patch $A/NSFoundWithTerminalAdditions.m $A/NSCell.m $A/NSAttributedString.m $A/NSKeyValueBinding/NSBinder.m $A/NSKeyValueBinding/NSMultipleValueBinder.m $A/NSButton.m
( cd /home/chrisk/src/darwin-lab/runs/cocoa/patchwork; diff -rq h b ) | sed 's/^/differs from h: /'
