Whole Finch SecurityInterface sources compile against our declarations and open
PD/AppKit headers. Link stops on NSCache and NSTableCellView classes in the base
build; the M1's Cocoa patch supplies the latter, but NSCache is absent from its
Foundation exports. It also needs the staged Finch SecurityFoundation/Security
slice. Not installed; no panels, windows or authorization UI were exercised.
