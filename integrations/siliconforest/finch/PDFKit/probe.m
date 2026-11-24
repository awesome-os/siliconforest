// SPDX-License-Identifier: MIT
// File-only PDFKit probe. Dynamic framework/class lookup also permits a Linux
// cross build against PD, where PDFKit is not installed. No views or printing.
#import <PDFKit/PDFKit.h>
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    @autoreleasepool {
        if (argc != 2) return 2;
        void *framework = dlopen("/System/Library/Frameworks/PDFKit.framework/Versions/A/PDFKit", RTLD_NOW | RTLD_LOCAL);
        if (!framework) { fprintf(stderr, "PDFKit load: %s\n", dlerror()); return 3; }
        Class documentClass = NSClassFromString(@"PDFDocument");
        if (!documentClass) return 4;
        NSData *data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]];
        PDFDocument *document = [[documentClass alloc] initWithData:data];
        if (!document || document.pageCount != 1) return 5;
        PDFPage *page = [document pageAtIndex:0];
        NSRect box = [page boundsForBox:kPDFDisplayBoxMediaBox];
        NSString *text = [page.string stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        NSArray *matches = [document findString:@"PDF" withOptions:0];
        // The key is a documented public value; no link to a dummy PDFKit stub.
        NSString *title = [document.documentAttributes objectForKey:@"Title"];
        printf("document pages=%lu version=%ld.%ld encrypted=%d locked=%d title=%s\n", (unsigned long)document.pageCount, (long)document.majorVersion, (long)document.minorVersion, document.isEncrypted, document.isLocked, title.UTF8String);
        printf("page media=%.0f,%.0f,%.0f,%.0f rotation=%ld text=%s matches=%lu\n",box.origin.x,box.origin.y,box.size.width,box.size.height,(long)page.rotation,text.UTF8String,(unsigned long)matches.count);
        BOOL okay = box.origin.x == 0 && box.origin.y == 0 && box.size.width == 72 && box.size.height == 72 &&
            !document.isEncrypted && !document.isLocked && [title isEqual:@"Finch fixture"] && [text isEqual:@"Finch PDF fixture"] && matches.count == 1;
        if (matches.count) printf("selection=%s pages=%lu\n",[[matches objectAtIndex:0] string].UTF8String,(unsigned long)[[[matches objectAtIndex:0] pages] count]);
        [document release];
        return okay ? 0 : 6;
    }
}
