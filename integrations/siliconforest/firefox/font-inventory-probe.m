/* SPDX-License-Identifier: MIT
 * Observe family aliases and language cascade coverage through public CoreText.
 * A nonempty cascade is inventory, not proof of the browser's shaped output.
 */
#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
int main(void) {@autoreleasepool {
 const char *families[]={"Helvetica Neue","Helvetica","Arial","Georgia","Times New Roman","-apple-system","system-ui","SF Pro","sans-serif","serif","monospace"};
 int missing=0;
 for(unsigned i=0;i<sizeof(families)/sizeof(*families);i++){
  CTFontRef font=CTFontCreateWithName((CFStringRef)[NSString stringWithUTF8String:families[i]],18,NULL);
  CFStringRef name=font?CTFontCopyPostScriptName(font):NULL;UniChar chars[]={'A','b','9'};CGGlyph glyphs[3]={0};
  int ok=font&&CTFontGetGlyphsForCharacters(font,chars,glyphs,3)&&glyphs[0]&&glyphs[1]&&glyphs[2];missing+=!ok;
  printf("FXFONTALIAS requested=%s actual=%s glyphs=%u,%u,%u pass=%d\n",families[i],name?[(NSString *)name UTF8String]:"null",glyphs[0],glyphs[1],glyphs[2],ok);
  if(name)CFRelease(name);if(font)CFRelease(font);
 }
 CTFontRef base=CTFontCreateWithName(CFSTR("system-ui"),18,NULL);
 const char *languages[]={"en","el","ar","hi","zh-Hans","ja","ko","th"};UniChar samples[]={0x0041,0x03b1,0x0627,0x0915,0x4e2d,0x3042,0xac00,0x0e20};
 for(unsigned i=0;i<sizeof(samples)/sizeof(*samples);i++){
  CFStringRef language=(CFStringRef)[NSString stringWithUTF8String:languages[i]];CFArrayRef requested=CFArrayCreate(NULL,(const void **)&language,1,&kCFTypeArrayCallBacks);
  CFArrayRef cascade=CTFontCopyDefaultCascadeListForLanguages(base,requested);unsigned covered=0;
  for(CFIndex n=0;cascade&&n<CFArrayGetCount(cascade);n++){
   CTFontRef font=CTFontCreateWithFontDescriptor((CTFontDescriptorRef)CFArrayGetValueAtIndex(cascade,n),18,NULL);CGGlyph glyph=0;
   covered+=font&&CTFontGetGlyphsForCharacters(font,&samples[i],&glyph,1)&&glyph!=0;
   if(font)CFRelease(font);
  }
  printf("FXFONTCASCADE language=%s sample=U+%04X descriptors=%ld covering=%u\n",languages[i],samples[i],cascade?(long)CFArrayGetCount(cascade):0,covered);
  if(cascade)CFRelease(cascade);CFRelease(requested);
 }
 if(base)CFRelease(base);return missing?1:0;
}}
