/* SPDX-License-Identifier: MIT
 * Register only the bundled OFL font in each Firefox process. No user font
 * directory, fontconfig configuration or system registry is changed.
 */
#include <CoreText/CoreText.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#ifndef SFFX_FONT_PREFIX
#error SFFX_FONT_PREFIX must name this artifact's private directory
#endif
__attribute__((constructor)) static void registerFonts(void) {
    const char *path=SFFX_FONT_PREFIX "/NotoSansDevanagari-Regular.ttf";
    CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,
        (const UInt8 *)path,strlen(path),false);
    CFErrorRef error=NULL;
    bool ok=url && CTFontManagerRegisterFontsForURL(url,kCTFontManagerScopeProcess,&error);
    // AppKit and CoreText own separate process fontconfig objects. Register
    // the same application font in the graphics-font resolver too.
    void *(*config)(void)=dlsym(RTLD_DEFAULT,"O2FontSharedFontConfig");
    int (*add)(void *,const unsigned char *)=dlsym(RTLD_DEFAULT,"FcConfigAppFontAddFile");
    int graphics=config && add && add(config(),(const unsigned char *)path);
    fprintf(stderr,"FXFONTS pid=%d process_registration=%d graphics_registration=%d\n",getpid(),ok,graphics);
    if(error)CFRelease(error);
    if(url)CFRelease(url);
}
