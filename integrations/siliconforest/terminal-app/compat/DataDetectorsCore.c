// SPDX-License-Identifier: MIT
// DataDetectorsCore.framework (private on macOS; Terminal.app imports four functions): the
// scanner that finds addresses, dates and links in text. This system has no detector: a
// scanner cannot be created, and the calls on one find nothing.
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
CFTypeRef DDScannerCreate(int type, CFOptionFlags options, CFErrorRef *error) { if (error) *error = NULL; return NULL; }
bool DDScannerScanStringWithRange(CFTypeRef scanner, CFStringRef string, CFRange range) { return false; }
CFArrayRef DDScannerCopyResultsWithOptions(CFTypeRef scanner, CFOptionFlags options) { return NULL; }
CFRange DDResultGetRange(CFTypeRef result) { return CFRangeMake(kCFNotFound, 0); }
