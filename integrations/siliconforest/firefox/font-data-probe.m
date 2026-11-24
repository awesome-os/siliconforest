/* SPDX-License-Identifier: MIT
 * Reproduce Firefox NativeFontResourceMac's descriptor-from-data path while
 * filesystem writes are denied. A native diagnostic, never a sandbox bypass.
 */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#include <sandbox.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned paint(CTFontRef font){
 if(!font)return 0;
 unsigned char pixels[256*64*4]={0};
 CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
 CGContextRef context=CGBitmapContextCreate(pixels,256,64,8,256*4,space,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
 CGColorSpaceRelease(space);if(!context)return 0;
 UniChar chars[]={ 'W','e','b',' ','f','o','n','t' };CGGlyph glyphs[8]={0};CGPoint positions[8];
 CTFontGetGlyphsForCharacters(font,chars,glyphs,8);
 for(int i=0;i<8;i++)positions[i]=CGPointMake(5+i*28,30);
 CGContextSetRGBFillColor(context,0,0,0,1);CTFontDrawGlyphs(font,glyphs,positions,8,context);CGContextRelease(context);
 unsigned painted=0;for(unsigned i=0;i<256*64;i++)painted+=pixels[i*4+3]!=0;
 return painted;
}
int main(int argc,char **argv){@autoreleasepool{
 if(argc<2||argc>3)return 2;
 NSData *bytes=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]];if(!bytes||![bytes length])return 2;
 CFDataRef input=CFDataCreate(NULL,[bytes bytes],[bytes length]);if(!input)return 2;
 if(argc==3){char *error=NULL;int status=sandbox_init("(version 1)(allow default)(deny file-write* (subpath \"/tmp\"))(deny file-write* (subpath \"/private/tmp\"))",0,&error);
  printf("FXFONTDATA sandbox_status=%d error=%s\n",status,error?error:"none");sandbox_free_error(error);if(status)return 2;}
 CGDataProviderRef provider=CGDataProviderCreateWithCFData(input);
 CGFontRef graphics=CGFontCreateWithDataProvider(provider);CGDataProviderRelease(provider);
 CTFontRef direct=graphics?CTFontCreateWithGraphicsFont(graphics,30,NULL,NULL):NULL;
 errno=0;CTFontDescriptorRef descriptor=CTFontManagerCreateFontDescriptorFromData(input);int descriptor_errno=errno;
 CTFontRef from=descriptor?CTFontCreateWithFontDescriptor(descriptor,30,NULL):NULL;
 CGFontRef roundtrip=from?CTFontCopyGraphicsFont(from,NULL):NULL;
 unsigned direct_pixels=paint(direct),descriptor_pixels=paint(from);
 CFArrayRef all=CTFontManagerCreateFontDescriptorsFromData(input);CFIndex faces=all?CFArrayGetCount(all):0;int face_failures=0;
 for(CFIndex i=0;i<faces;i++){
  CTFontRef face=CTFontCreateWithFontDescriptor(CFArrayGetValueAtIndex(all,i),30,NULL);
  CGFontRef cg=face?CTFontCopyGraphicsFont(face,NULL):NULL;CFStringRef ps=cg?CGFontCopyPostScriptName(cg):NULL;char name[256]="null";
  if(ps){CFStringGetCString(ps,name,sizeof(name),kCFStringEncodingUTF8);CFRelease(ps);}
  unsigned pixels=paint(face);printf("FXFONTDATA_FACE index=%ld name=%s glyphs=%zu pixels=%u pass=%d\n",(long)i,name,cg?CGFontGetNumberOfGlyphs(cg):0,pixels,face&&cg&&pixels);
  face_failures+=!face||!cg||!pixels;if(cg)CGFontRelease(cg);if(face)CFRelease(face);
 }
 if(all)CFRelease(all);printf("FXFONTDATA_COLLECTION faces=%ld failures=%d\n",(long)faces,face_failures);
 char name[256]="null";CFStringRef ps=roundtrip?CGFontCopyPostScriptName(roundtrip):NULL;
 if(ps){CFStringGetCString(ps,name,sizeof(name),kCFStringEncodingUTF8);CFRelease(ps);}
 printf("FXFONTDATA bytes=%lu direct_graphics=%d direct_ct=%d direct_pixels=%u descriptor=%d descriptor_errno=%d descriptor_ct=%d roundtrip_graphics=%d descriptor_pixels=%u name=%s pass=%d\n",(unsigned long)[bytes length],graphics!=NULL,direct!=NULL,direct_pixels,descriptor!=NULL,descriptor_errno,from!=NULL,roundtrip!=NULL,descriptor_pixels,name,graphics&&direct&&direct_pixels&&descriptor&&from&&roundtrip&&descriptor_pixels);
 if(roundtrip)CGFontRelease(roundtrip);if(from)CFRelease(from);if(descriptor)CFRelease(descriptor);if(direct)CFRelease(direct);if(graphics)CGFontRelease(graphics);
 CFRelease(input);
 return direct_pixels&&descriptor_pixels&&faces&&!face_failures?0:1;
}}
