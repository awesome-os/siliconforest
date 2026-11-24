/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <time.h>
static void name(CFStringRef text,char output[256]){
 if(!text||!CFStringGetCString(text,output,256,kCFStringEncodingUTF8))snprintf(output,256,"?");
}
int main(int argc,char **argv){@autoreleasepool{
 const char *texts[]={"A","Ε","日","द","ا","🙂","🌲","🚀"};
 unsigned codes[]={0x41,0x395,0x65e5,0x926,0x627,0x1f642,0x1f332,0x1f680};int mismatches=0,missing=0;
 CTFontRef base=CTFontCreateWithName(CFSTR("sans-serif"),28,NULL);if(!base)return 2;
 for(int i=0;i<8;i++){
  CFStringRef text=CFStringCreateWithCString(NULL,texts[i],kCFStringEncodingUTF8);CFIndex count=CFStringGetLength(text);
  CTFontRef fallback=CTFontCreateForString(base,text,CFRangeMake(0,count));if(!fallback)return 2;
  CGFontRef graphics=CTFontCopyGraphicsFont(fallback,NULL);if(!graphics)return 2;
  UniChar chars[2]={0};CGGlyph glyphs[2]={0},roundtripGlyphs[2]={0};CFStringGetCharacters(text,CFRangeMake(0,count),chars);
  int found=CTFontGetGlyphsForCharacters(fallback,chars,glyphs,count);
  CTFontRef roundtrip=CTFontCreateWithGraphicsFont(graphics,28,NULL,NULL);
  CTFontGetGlyphsForCharacters(roundtrip,chars,roundtripGlyphs,count);
  FT_Face face=[(id)graphics respondsToSelector:@selector(face)]?(FT_Face)[(id)graphics performSelector:@selector(face)]:NULL;
  unsigned expected=face?FT_Get_Char_Index(face,codes[i]):0;
  CFStringRef ctName=CTFontCopyPostScriptName(fallback),cgName=CGFontCopyPostScriptName(graphics);char a[256],b[256];name(ctName,a);name(cgName,b);
  CGFontRef named=CGFontCreateWithFontName(ctName);CFStringRef namedName=named?CGFontCopyPostScriptName(named):NULL;
  char namedText[256];name(namedName,namedText);
  printf("FXFONTNAMED U+%04X requested=%s actual=%s exact=%d\n",codes[i],a,namedText,namedName&&CFEqual(ctName,namedName));
  if(namedName)CFRelease(namedName);if(named)CGFontRelease(named);
  CTFontDescriptorRef descriptor=CTFontCopyFontDescriptor(roundtrip);
  CTFontRef copied=CTFontCreateCopyWithAttributes(roundtrip,28,NULL,descriptor);
  CFStringRef copiedName=copied?CTFontCopyPostScriptName(copied):NULL;char copiedText[256];name(copiedName,copiedText);
  printf("FXFONTCOPY U+%04X requested=%s actual=%s exact=%d\n",codes[i],a,copiedText,copiedName&&CFEqual(ctName,copiedName));
  if(copiedName)CFRelease(copiedName);if(copied)CFRelease(copied);CFRelease(descriptor);
  int same=face&&glyphs[0]==expected&&roundtripGlyphs[0]==expected;
  printf("FXFONTCOVER U+%04X CT=%s CG=%s found=%d glyph=%u roundtrip=%u graphicsFT=%u same=%d base_equal=%d\n",codes[i],a,b,found,glyphs[0],roundtripGlyphs[0],expected,same,CFEqual(base,fallback));
  mismatches+=!same;missing+=!found;
  if(ctName)CFRelease(ctName);if(cgName)CFRelease(cgName);CFRelease(roundtrip);CGFontRelease(graphics);CFRelease(fallback);CFRelease(text);
 }
 CFRelease(base);printf("FXFONTCOVER mismatches=%d missing=%d\n",mismatches,missing);
 if(argc==2){
  NSData *bytes=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]];
  CGDataProviderRef provider=CGDataProviderCreateWithCFData((CFDataRef)bytes);
  CGFontRef graphics=provider?CGFontCreateWithDataProvider(provider):NULL;
  CTFontRef memory=graphics?CTFontCreateWithGraphicsFont(graphics,28,NULL,NULL):NULL;
  if(!memory)return 2;
  CFStringRef original=CTFontCopyPostScriptName(memory);
  CTFontDescriptorRef descriptor=CTFontCopyFontDescriptor(memory);
  CGAffineTransform transform=CGAffineTransformMakeScale(1.25,.75);
  for(int trial=0;trial<3;trial++){
   struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
   for(int i=0;i<256;i++){
    CTFontRef copied=CTFontCreateCopyWithAttributes(memory,41,&transform,descriptor);
    if(copied)CFRelease(copied);
   }
   clock_gettime(CLOCK_MONOTONIC,&b);
   printf("FXFONTCOPYBENCH trial=%d copies=256 wall_seconds=%.9f\n",trial,(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9);
  }
  for(int mode=0;mode<2;mode++){
   CTFontRef copy=CTFontCreateCopyWithAttributes(memory,41,&transform,mode?descriptor:NULL);
   CFStringRef copied=copy?CTFontCopyPostScriptName(copy):NULL;char a[256],b[256];name(original,a);name(copied,b);
   CGAffineTransform actual=copy?CTFontGetMatrix(copy):CGAffineTransformIdentity;
   UniChar character=0x0926;CGGlyph before=0,after=0;CTFontGetGlyphsForCharacters(memory,&character,&before,1);
   if(copy)CTFontGetGlyphsForCharacters(copy,&character,&after,1);
   int pass=copy&&copied&&CFEqual(original,copied)&&before&&before==after&&CTFontGetSize(copy)==41&&actual.a==1.25&&actual.d==.75;
   printf("FXFONTMEMORY descriptor=%d original=%s copied=%s before=%u after=%u size_matrix=%d pass=%d\n",mode,a,b,before,after,copy&&CTFontGetSize(copy)==41&&actual.a==1.25&&actual.d==.75,pass);
   mismatches+=!pass;if(copied)CFRelease(copied);if(copy)CFRelease(copy);
  }
  CFRelease(descriptor);CFRelease(original);CFRelease(memory);CGFontRelease(graphics);CGDataProviderRelease(provider);
 }
 return mismatches?1:0;
}}
