#!/bin/bash
# SPDX-License-Identifier: MIT
# Regenerates ./0100-*.patch .. (the Activity Monitor patches, docs/roadmap/issues/SF-AA-activity-monitor.md)
# from the patchwork trees in runs/sf-actmon/pw (not committed):
#   base/     = runs/cocoa/patchwork/g + the committed patches 0040..0059 (as committed on 14 November)
#   am-0105/  = base/ + 0100..0105 (frozen)
#   am-0106/  = am-0105/ + 0106 (frozen)
#   am-0107/  = am-0106/ + 0107 (frozen)
#   am-0109/  = am-0107/ + 0108, 0109 (frozen; COCOA3, evening of 14 November)
#   am-0115/  = am-0109/ + 0110..0115 (frozen; COCOA3)
#   am-0119/  = am-0115/ + 0116..0119 (frozen; COCOA4, night of 14 November)
#   am-0122/  = am-0119/ + 0120..0122 (frozen; TERM5, night of 14/15 November)
#   am-0123/  = am-0122/ + 0123, 0124 (frozen; TERM6, night of 15 November)
#   am-0126/  = am-0123/ + 0125, 0126 (frozen; TERM6)
#   am-0127/  = am-0126/ + 0127 (frozen; TERM6)
#   am-0128/  = am-0127/ + 0128 (frozen; TERM6)
#   am-0129/  = am-0128/ + 0129 (frozen; TERM6)
#   am-0130/  = am-0129/ + 0130 (frozen; TERM6; 0130 diffs from am-0128: am-0129 changed NSView.m too)
#   am-0131/  = am-0130/ + 0131 (frozen; TERM6)
#   am-0132/  = am-0131/ + 0132 (frozen; TERM6)
#   am-0207/  = am-0132/ + the Firefox patches 0200, 0201, 0202, 0203, 0204, 0207 (CODEX-FX's, applied as they are;
#               QuartzCore and Onyx2D/O2Font_freetype.m added from the pin; TERM7)
#   am-0135/  = am-0207/ + 0133..0135 (frozen; TERM7)
#   am/       = am-0135/ + 0136 on (the current tree; TERM7)
# Each patch diffs two trees for its files; a file belongs to one patch of a set. They apply after 0059
# (../patched.nix).
cd /home/chrisk/src/darwin-lab/runs/sf-actmon/pw; P=/home/chrisk/src/darwin-lab/integrations/siliconforest/cocoa/patches
mk(){ from=$1 to=$2 out=$3; shift 3; : > $P/$out; for f in "$@"; do diff -uN --label a/$f --label b/$f $from/$f $to/$f >> $P/$out; done; echo "$out $(wc -l < $P/$out)"; }
A=src/Frameworks/AppKit
mk base am-0105 0100-appkit-activity-monitor-names.patch $A/NSActivityMonitorAdditions.m
mk base am-0105 0101-appkit-view-based-tables-hidden-columns.patch $A/NSTableViewViewBased.m $A/NSTableView.m $A/NSOutlineView.m $A/NSTableColumn.h $A/NSTableColumn.m $A/NSTableHeaderView.m $A/NSLayoutConstraint.m
mk base am-0105 0103-appkit-layer-backed-views-without-egl.patch $A/NSView.m
mk base am-0105 0104-appkit-found-with-activity-monitor.patch $A/nib.subproj/NSNibArchiveUnarchiver.m $A/nib.subproj/NSCustomView.m $A/NSCell.m $A/NSControl.m $A/NSKeyValueBinding/NSBinder.m $A/NSKeyValueBinding/NSKVOBinder.m $A/NSKeyValueBinding/NSCachingBinder.m $A/NSKeyValueBinding/NSMultipleValueBinder.m $A/NSWindowMenuStrip.m
mk base am-0105 0105-appkit-asset-catalog-colors-from-finch.patch $A/SFCUIPrivate.h $A/SFCUIStore.m $A/SFCUIRendition.m
mk base am-0105 0102-coregraphics-activity-monitor-cgs.patch src/Frameworks/CoreGraphics/CGActivityMonitorAdditions.m
( diff -rq base am-0105 ) | sed 's/^/differs from base: /'
# 0106: am-0105 -> am-0106
mk am-0105 am-0106 0106-appkit-nib-layout-standard-space-priorities.patch $A/NSLayoutConstraint.m $A/NSView.m $A/NSViewLayout.m $A/NSScrollView.m
( diff -rq am-0105 am-0106 ) | sed 's/^/differs from am-0105: /'
# 0107: am-0106 -> am-0107
mk am-0106 am-0107 0107-appkit-activity-monitor-look-and-cost.patch $A/NSSymbolImages.m $A/NSImage.m $A/nib.subproj/NSCustomResource.m $A/NSImageCell.m \
  $A/NSMenu.subproj/NSMenuView.m $A/NSTableHeaderView.m $A/NSTableView.m $A/NSTableViewViewBased.m $A/NSOutlineView.m \
  $A/NSToolbar.subproj/NSToolbarItemView.m $A/NSToolbar.subproj/NSToolbarView.m $A/NSColor.subproj/NSColor_catalog.h \
  $A/NSColor.subproj/NSColor_catalog.m $A/NSView.m $A/NSPopUpButtonCell.m
( diff -rq am-0106 am-0107 ) | sed 's/^/differs from am-0106: /'
# 0108, 0109: am-0107 -> am-0109
mk am-0107 am-0109 0108-appkit-menu-item-getters-autoreleased.patch $A/NSMenu.subproj/NSMenuItem.m
mk am-0107 am-0109 0109-coregraphics-imageio-thumbnail-metadata.patch src/Frameworks/CoreGraphics/CGImageMetadata.h src/Frameworks/CoreGraphics/CGImageMetadata.m
( diff -rq am-0107 am-0109 ) | sed 's/^/differs from am-0107: /'
# 0110, 0111 on: am-0109 -> am (the current tree)
mk am-0109 am-0115 0110-appkit-collection-view-from-finch.patch $A/NSCollectionView/NSCollectionView.h $A/NSCollectionView/NSCollectionView.m \
  $A/NSCollectionView/NSCollectionViewItem.h $A/NSCollectionView/NSCollectionViewItem.m $A/NSCollectionView/NSCollectionViewLayout.h \
  $A/NSCollectionView/NSCollectionViewLayout.m $A/NSActivityMonitorAdditions.m $A/NSViewController.h $A/NSViewController.m
mk am-0109 am-0115 0111-appkit-catalog-images-symbols-from-finch.patch $A/SFCUIPrivate.h $A/SFCUIRendition.m $A/SFCUICodecs.h \
  $A/SFCUIDeepmap.m $A/SFCUIImage.m $A/NSImage.m $A/SFSymbolGlyphs.m $A/NSSymbolImages.m $A/NSImageSymbolConfiguration.m
mk am-0109 am-0115 0112-appkit-solve-time-trace.patch $A/NSLayoutConstraint.m
mk am-0109 am-0115 0113-appkit-toolbar-buttons-macos26.patch $A/NSButtonCell.m $A/NSToolbar.subproj/NSToolbarView.m \
  $A/NSToolbar.subproj/NSToolbarItemView.m
mk am-0109 am-0115 0114-appkit-overlay-scrollers.patch $A/NSScroller.m $A/NSScrollView.m $A/NSScrollView.h $A/NSView.m \
  $A/NSMacOS26Additions.m $A/NSModernAppKit.m
mk am-0109 am-0115 0115-appkit-cgevent-autoreleased.patch $A/NSEvent.subproj/NSEvent_Modern.m
( diff -rq am-0109 am-0115 ) | sed "s/^/differs from am-0109: /"
# 0116 on (COCOA4, night of 14 November): am-0115 -> am
mk am-0115 am-0119 0116-appkit-owner-findings-windows-cursor-colors.patch $A/NSWindow.m $A/NSCursor.m $A/NSApplication.m $A/NSPathCell.m \
  $A/NSModernAppKit.m $A/NSColorPanel.m $A/NSSymbolImages.m $A/NSWindowMenuStrip.m $A/NSWindow-Private.h $A/NSColor.subproj/conversions.h
mk am-0115 am-0119 0117-windowserver-cursor-shown-when-the-pointer-moves.patch src/Frameworks/WindowServer/rpc_wayland.c
mk am-0115 am-0119 0118-coregraphics-icc-and-indexed-color-spaces.patch src/Frameworks/CoreGraphics/CGColorSpace.h src/Frameworks/CoreGraphics/CGColorSpace.m
mk am-0115 am-0119 0119-onyx2d-shading-path-released.patch src/Frameworks/Onyx2D/O2Context_builtin.m
( diff -rq am-0115 am-0119 ) | sed 's/^/differs from am-0115: /'
# 0120..0122 (TERM5, night of 14 November): am-0119 -> am-0122
mk am-0119 am-0122 0120-appkit-device-pixels-opaque-default-menu-tracking.patch $A/NSView.m $A/NSView.h $A/NSWindow.m $A/NSWindow.h $A/NSPathControl.m $A/NSImage.m $A/NSColorPanel.m $A/NSViewLayout.m $A/NSTableView.m $A/NSApplication.m $A/NSDisplay.m $A/NSWindowTabbing.m $A/NSButtonCell.m $A/NSStringDrawing.m $A/NSColorWell.m $A/NSLayoutConstraint.m \
  $A/NSWorkspaceOpenConfiguration.m $A/NSMenu.subproj/NSMenuView.m
mk am-0119 am-0122 0121-windowserver-opaque-region-blur-query-test-scale.patch src/Frameworks/WindowServer/rpc_wayland.c \
  src/Frameworks/WindowServer/rpc.h src/Frameworks/WindowServer/message.h
mk am-0119 am-0122 0122-onyx2d-clip-edges-on-pixels.patch src/Frameworks/Onyx2D/O2Context_builtin.m
( diff -rq am-0119 am-0122 ) | sed "s/^/differs from am-0119: /"
# 0123, 0124 (TERM6, night of 15 November): am-0122 -> am-0123
mk am-0122 am-0123 0123-appkit-menus-fast-crashes-layers-without-egl.patch $A/NSMenu.subproj/NSMenuView.m $A/NSMenu.subproj/NSMenu.m $A/NSColor.subproj/NSColor.m \
  $A/NSWindow.m $A/NSView.m $A/NSApplication.m $A/NSTableView.m $A/NSProgressIndicator.m $A/NSColorWell.m $A/NSColorPanel.m
mk am-0122 am-0123 0124-windowserver-popup-role-ahead-idle-frames.patch src/Frameworks/WindowServer/rpc_wayland.c src/Frameworks/WindowServer/message.h
( diff -rq am-0122 am-0123 ) | sed 's/^/differs from am-0122: /'
# 0125, 0126 (TERM6): am-0123 -> am-0126
mk am-0123 am-0126 0125-appkit-menu-panel-seams-translucency-choice-split-view.patch $A/NSMenu.subproj/NSMenuView.m $A/NSColor.subproj/NSColor.m \
  $A/NSColorPanel.m $A/NSWorkspaceOpenConfiguration.m $A/NSStringDrawer.m $A/NSSplitView.m
mk am-0123 am-0126 0126-windowserver-popup-configure-wait-timing.patch src/Frameworks/WindowServer/rpc_wayland.c
( diff -rq am-0123 am-0126 ) | sed 's/^/differs from am-0123: /'
# 0127 (TERM6): am-0126 -> am-0127
mk am-0126 am-0127 0127-appkit-float-value-abi-colour-well-click-opaque-menus.patch $A/NSCell.h $A/NSCell.m $A/NSControl.h $A/NSControl.m \
  $A/NSActionCell.m $A/NSScroller.h $A/NSScroller.m $A/NSColorWell.m $A/NSSliderCell.m $A/NSMenu.subproj/NSMenuView.m \
  $A/NSAlert.m $A/NSButton.m $A/NSTextFinder.m $A/NSView.m
( diff -rq am-0126 am-0127 ) | sed 's/^/differs from am-0126: /'
# 0128 (TERM6): am-0127 -> am-0128
mk am-0127 am-0128 0128-appkit-table-header-sort-indicator-slot.patch $A/NSTableHeaderCell.m $A/NSTableHeaderView.m
( diff -rq am-0127 am-0128 ) | sed 's/^/differs from am-0127: /'
# 0129 (TERM6): am-0128 -> am-0129
mk am-0128 am-0129 0129-windowserver-frames-kept-by-default.patch src/Frameworks/WindowServer/rpc_wayland.c
( diff -rq am-0128 am-0129 ) | sed 's/^/differs from am-0128: /'
# 0130 (TERM6): am-0128 -> am-0130 for AppKit (am-0129's NSView.m trace with it)
mk am-0128 am-0130 0130-appkit-column-drag-scroll-copy-split-panes.patch $A/NSView.m $A/NSLayoutConstraint.m $A/NSTableView.m \
  $A/NSTableViewViewBased.m $A/NSTableHeaderView.m $A/NSSplitView.m $A/NSStringDrawer.h $A/NSStringDrawer.m $A/NSWindow.m \
  $A/NSClipView.m $A/NSScroller.m $A/NSGradient.m
( diff -rq am-0129 am-0130 ) | sed 's/^/differs from am-0129: /'
# 0131 (TERM6): am-0130 -> am-0131
mk am-0130 am-0131 0131-appkit-typeset-cache-generation-kept.patch $A/NSStringDrawer.m
( diff -rq am-0130 am-0131 ) | sed 's/^/differs from am-0130: /'
# 0132 (TERM6): am-0131 -> am-0132
mk am-0131 am-0132 0132-appkit-inspector-tab-content-window-fits-single-line-values.patch $A/NSLayoutConstraint.m $A/NSWindow.m \
  $A/NSTabView.m $A/NSViewLayout.m $A/NSControl.m $A/NSCell.h $A/NSCell.m $A/NSApplication.m
( diff -rq am-0131 am-0132 ) | sed 's/^/differs from am-0131: /'
# 0133-0135 (TERM7): am-0207 -> am (the Firefox patches 0200-0207 are CODEX-FX's files, not generated here)
Q=src/Frameworks/QuartzCore
mk am-0207 am 0133-appkit-hosted-layers-cpu-class-named-nibs-arranged-subviews-names.patch $A/NSView.m $A/NSViewController.m \
  $A/NSSplitView.h $A/NSSplitView.m $A/NSSystemNamesAdditions.m $A/NSTextView.subproj/NSTypesetter_concrete.m $A/NSImage.m $A/NSActivityMonitorAdditions.m
mk am-0207 am 0134-quartzcore-layer-display-opengl-layer.patch $Q/CALayer.m $Q/CALayerContext.m $Q/CAOpenGLLayer.h $Q/CAOpenGLLayer.m $Q/CoreAnimation.h
mk am-0207 am 0135-coregraphics-display-fade-opengl-acceleration.patch src/Frameworks/CoreGraphics/CGDirectDisplay_siliconforest.m
( diff -rq am-0207 am ) | sed 's/^/differs from am-0207: /'
# 0136 (TERM7): am-0135 -> am
mk am-0135 am 0136-appkit-wrapped-label-keeps-its-last-line.patch $A/NSTextFieldCell.m
( diff -rq am-0135 am ) | sed 's/^/differs from am-0135: /'
# 0137 (TERM8) and CODEX-FX's 0210, 0210w, 0212, 0214, 0215, 0217, 0219 merged (0219 adapted: the main series has no
# 0208, whose GPU frame export it also released; 0210w is CODEX-FX's windowserver-patches/0002 at this tree's paths).
#   am-0136/  = am-0135/ + 0136 (frozen; Onyx2D/O2Font_freetype.h added from the pin)
#   am-0210/  = am-0136/ + CODEX-FX's patches above (frozen; TERM8)
#   am/       = am-0210/ + 0137 on (the current tree; TERM8)
O=src/Frameworks/Onyx2D
mk am-0136 am-0210 0210-precise-scroll-deltas-and-gesture-phase.patch $A/NSEvent.subproj/NSEvent_mouse.h $A/NSEvent.subproj/NSEvent_mouse.m $A/NSApplication.m $A/NSView.m
mk am-0136 am-0210 0210w-windowserver-finger-pixels-and-scroll-phase.patch src/Frameworks/WindowServer/rpc_wayland.c
mk am-0136 am-0210 0212-menu-appearance-and-popup-inheritance.patch $A/NSAppearance.m $A/NSMenu.subproj/NSMenu.h $A/NSMenu.subproj/NSMenuView.m
mk am-0136 am-0210 0214-pasteboard-item-provider-receives-destination-board.patch $A/NSPasteboardItem.m
mk am-0136 am-0210 0215-coretext-copy-keeps-graphics-font-for-rendering-attributes.patch src/Frameworks/CoreText/CTFont.m
mk am-0136 am-0210 0217-graphics-font-keeps-fontconfig-collection-face-index.patch $O/O2Font_freetype.h $O/O2Font_freetype.m $O/O2Font_ttf.m
mk am-0136 am-0210 0219-layer-context-releases-owned-renderer-and-cgl-resources.patch $Q/CALayerContext.m
( diff -rq am-0136 am-0210 ) | sed 's/^/differs from am-0136: /'
mk am-0210 am-0137 0137-appkit-vlc-interface-modal-events-minimise-crash.patch $A/NSColor.subproj/NSColor.h $A/NSColor.subproj/NSColor.m \
  $A/NSCell.h $A/NSImage.m $A/NSStatusItem.m $A/NSSegmentedControl/NSSegmentedCell.m $A/NSVLCAdditions.m $A/NSOutlineView.m \
  $A/NSSliderCell.m $A/NSClipView.m $A/NSBox.m $A/NSButtonCell.m $A/NSAnimation/NSAnimation.m $A/NSBrowserCell.m $A/NSApplication.m \
  $A/NSDocumentController.m $A/NSStringDrawing.h $A/NSStringDrawing.m
mk am-0210 am-0137 0138-quartzcore-layer-autoresizing-visible-rect-display-on-resize.patch $Q/CALayer.h $Q/CALayer.m
( diff -rq am-0210 am-0137 ) | sed 's/^/differs from am-0210: /'
# 0139 (TERM8): am-0137 -> am (am-0137 = am-0210 + 0137, 0138; frozen)
mk am-0137 am-0139 0139-appkit-pinch-magnify.patch $A/NSApplication.m $A/NSEvent.subproj/NSEvent_mouse.h $A/NSEvent.subproj/NSEvent_mouse.m
mk am-0137 am-0139 0139w-windowserver-pinch-gestures.patch src/Frameworks/WindowServer/rpc_wayland.c
( diff -rq am-0137 am-0139 ) | sed 's/^/differs from am-0137: /'
# 0140 (TERM8): am-0139 -> am-0140 (am-0139 = am-0137 + 0139, 0139w; frozen)
mk am-0139 am-0140 0140-appkit-layout-priorities-in-order.patch $A/SFLayoutSolver.m
( diff -rq am-0139 am-0140 ) | sed 's/^/differs from am-0139: /'
# 0141w, 0142 (TERM8): am-0140 -> am-0142 (am-0140 = am-0139 + 0140; frozen)
mk am-0140 am-0142 0141w-windowserver-cursor-shapes.patch src/Frameworks/WindowServer/rpc_wayland.c
mk am-0140 am-0142 0142-appkit-tracking-area-in-visible-rect.patch $A/NSView.m
( diff -rq am-0140 am-0142 ) | sed 's/^/differs from am-0140: /'
# 0143w, 0144 (TERM8, Firefox's windowless process): am-0142 -> am-0144 (am-0142 = am-0140 + 0141w, 0142; frozen)
mk am-0142 am-0144 0143w-windowserver-new-surface-for-a-new-role-ends-with-compositor.patch src/Frameworks/WindowServer/rpc_wayland.c
mk am-0142 am-0144 0144-appkit-reopen-and-quit-from-the-desk.patch $A/NSApplication.m $A/NSWindowMenuStrip.m
( diff -rq am-0142 am-0144 ) | sed 's/^/differs from am-0142: /'
# 0145w (TERM8): am-0144 -> am (am-0144 = am-0142 + 0143w, 0144; frozen)
mk am-0144 am 0145w-windowserver-same-cursor-image-not-sent-again.patch src/Frameworks/WindowServer/rpc_wayland.c
( diff -rq am-0144 am ) | sed 's/^/differs from am-0144: /'
# 0146 (TERM8, FX-010): Onyx2D, the glyph cache keyed by face address and generation (am-0144 -> am, Onyx2D only)
mk am-0144 am 0146-onyx2d-glyph-cache-knows-a-reused-face.patch $O/O2Context_builtin.m
# 0147 (TERM8, FX-004): am-0146 -> am (am-0146 = am-0144 + 0145w, 0146; frozen)
mk am-0146 am 0147-appkit-label-and-button-constructors.patch $A/NSTextField.h $A/NSTextField.m $A/NSButton.h $A/NSButton.m
( diff -rq am-0146 am ) | sed 's/^/differs from am-0146: /'
