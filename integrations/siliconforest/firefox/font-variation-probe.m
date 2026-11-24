/* SPDX-License-Identifier: MIT
 * Compare pixel output, metrics, shaping and numeric variation metadata through
 * direct CG, CT descriptors/copies, data fonts and Firefox's CG-name-key route.
 * This finite CPU bitmap control does not qualify browser presentation.
 */
#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
static uint64_t paint(CTFontRef font,unsigned *ink){
 unsigned char pixels[256*64*4]={0};*ink=0;
 CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();CGContextRef context=CGBitmapContextCreate(pixels,256,64,8,1024,color,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);CGColorSpaceRelease(color);if(!context)return 0;
 UniChar chars[]={'W','e','b','f','o','n','t'};CGGlyph glyphs[7]={0};CGPoint points[7];CTFontGetGlyphsForCharacters(font,chars,glyphs,7);for(int i=0;i<7;i++)points[i]=CGPointMake(6+i*32,30);
 CGContextSetRGBFillColor(context,0,0,0,1);CTFontDrawGlyphs(font,glyphs,points,7,context);CGContextRelease(context);
 uint64_t hash=UINT64_C(14695981039346656037);for(unsigned i=0;i<sizeof(pixels);i++)hash=(hash^pixels[i])*UINT64_C(1099511628211);for(unsigned i=0;i<256*64;i++)*ink+=pixels[i*4+3]!=0;return hash;
}
static CTFontDescriptorRef descriptor(CTFontDescriptorRef base,CFDictionaryRef variations){
 const void *key=kCTFontVariationAttribute;CFDictionaryRef attrs=CFDictionaryCreate(NULL,&key,(const void **)&variations,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 CTFontDescriptorRef result=base?CTFontDescriptorCreateCopyWithAttributes(base,attrs):CTFontDescriptorCreateWithAttributes(attrs);CFRelease(attrs);return result;
}
static uint64_t cpu_ns(void){struct timespec t={0};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)!=0)return 0;return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static int benchmark(CGFontRef graphics){
 int32_t tag=0x77676874;double weight=900;CFNumberRef key=CFNumberCreate(NULL,kCFNumberSInt32Type,&tag),value=CFNumberCreate(NULL,kCFNumberDoubleType,&weight);const void *k=key,*v=value;
 CFDictionaryRef variation=CFDictionaryCreate(NULL,&k,&v,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);CGFontRef varied=CGFontCreateCopyWithVariations(graphics,variation);CFRelease(variation);CFRelease(key);CFRelease(value);
 CFDictionaryRef names=CGFontCopyVariations(varied);CTFontRef seed=CTFontCreateWithGraphicsFont(varied,30,NULL,NULL);CTFontDescriptorRef original=CTFontCopyFontDescriptor(seed),wanted=descriptor(original,names);CFRelease(original);CFRelease(seed);CFRelease(names);
 int failures=0;
 for(int repeat=0;repeat<5;repeat++){
  double checksum=0;uint64_t before=cpu_ns();
  for(int i=0;i<100;i++){
   CTFontRef fromCG=CTFontCreateWithGraphicsFont(varied,30,NULL,NULL),font=CTFontCreateCopyWithAttributes(fromCG,0,NULL,wanted);
   const void *key=kCTFontAttributeName,*value=font;CFDictionaryRef attrs=CFDictionaryCreate(NULL,&key,&value,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);CFAttributedStringRef text=CFAttributedStringCreate(NULL,CFSTR("W"),attrs);CTLineRef line=CTLineCreateWithAttributedString(text);checksum+=CTLineGetTypographicBounds(line,NULL,NULL,NULL);CFRelease(line);CFRelease(text);CFRelease(attrs);CFRelease(font);CFRelease(fromCG);
  }
  uint64_t after=cpu_ns();int pass=before&&after>before&&checksum>0;failures+=!pass;
  printf("FXFONTBENCH repeat=%d iterations=100 thread_cpu_ns=%llu ns_per_font=%.1f checksum=%.6f pass=%d\n",repeat,(unsigned long long)(after-before),(after-before)/100.0,checksum,pass);
 }
 CFRelease(wanted);CGFontRelease(varied);return failures;
}
int main(int argc,char **argv){@autoreleasepool{
 int bench=argc==3&&strcmp(argv[1],"--bench")==0;const char *path=argc==2?argv[1]:(bench?argv[2]:NULL);if(!path)return 2;NSData *bytes=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path]];if(!bytes)return 2;
 CGDataProviderRef provider=CGDataProviderCreateWithCFData((CFDataRef)bytes);CGFontRef graphics=CGFontCreateWithDataProvider(provider);CGDataProviderRelease(provider);if(!graphics)return 2;
 CTFontRef base=CTFontCreateWithGraphicsFont(graphics,30,NULL,NULL);CFArrayRef axes=CTFontCopyVariationAxes(base);printf("FXFONTAXES count=%ld\n",axes?(long)CFArrayGetCount(axes):0);if(!axes||CFArrayGetCount(axes)==0)return 1;
 double low=0,high=0;int32_t weightTag=0x77676874;
 for(CFIndex i=0;i<CFArrayGetCount(axes);i++){
  CFDictionaryRef axis=CFArrayGetValueAtIndex(axes,i);int32_t tag=0;CFNumberGetValue(CFDictionaryGetValue(axis,kCTFontVariationAxisIdentifierKey),kCFNumberSInt32Type,&tag);
  if(tag==weightTag){CFNumberGetValue(CFDictionaryGetValue(axis,kCTFontVariationAxisMinimumValueKey),kCFNumberDoubleType,&low);CFNumberGetValue(CFDictionaryGetValue(axis,kCTFontVariationAxisMaximumValueKey),kCFNumberDoubleType,&high);}
 }
 if(!(high>low))return 1;
 uint64_t hashes[2][5]={0};double advances[2][5]={0};int failures=0;unsigned baseInk=0;uint64_t baseBefore=paint(base,&baseInk);
 for(int weight=0;weight<2;weight++){
  double value=weight?900:100,expected=fmax(low,fmin(high,value));CFNumberRef key=CFNumberCreate(NULL,kCFNumberSInt32Type,&weightTag),number=CFNumberCreate(NULL,kCFNumberDoubleType,&value);const void *k=key,*v=number;
  CFDictionaryRef variations=CFDictionaryCreate(NULL,&k,&v,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);CFRelease(number);CTFontDescriptorRef desc=descriptor(NULL,variations);
  for(int mode=0;mode<5;mode++){
   CGFontRef variant=(mode==1||mode==4)?CGFontCreateCopyWithVariations(graphics,variations):CGFontRetain(graphics);CTFontRef font=NULL;
   if(mode==2)font=CTFontCreateCopyWithAttributes(base,30,NULL,desc);
   else if(mode==3){CTFontDescriptorRef memory=CTFontManagerCreateFontDescriptorFromData((CFDataRef)bytes),withVariation=descriptor(memory,variations);font=CTFontCreateWithFontDescriptor(withVariation,30,NULL);CFRelease(withVariation);CFRelease(memory);}
   else if(mode==4){CTFontRef fromCG=CTFontCreateWithGraphicsFont(variant,30,NULL,NULL);CFDictionaryRef names=CGFontCopyVariations(variant);CTFontDescriptorRef original=CTFontCopyFontDescriptor(fromCG),copied=descriptor(original,names);font=CTFontCreateCopyWithAttributes(fromCG,0,NULL,copied);CFRelease(copied);CFRelease(original);CFRelease(names);CFRelease(fromCG);}
   else font=CTFontCreateWithGraphicsFont(variant,30,NULL,mode==1?NULL:desc);
   unsigned ink=0;hashes[weight][mode]=paint(font,&ink);failures+=!ink;
   UniChar character='W';CGGlyph glyph=0;CGSize advance=CGSizeZero;CTFontGetGlyphsForCharacters(font,&character,&glyph,1);CTFontGetAdvancesForGlyphs(font,kCTFontOrientationHorizontal,&glyph,&advance,1);advances[weight][mode]=advance.width;
   const void *attribute=kCTFontAttributeName,*fontValue=font;CFDictionaryRef textAttrs=CFDictionaryCreate(NULL,&attribute,&fontValue,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);CFAttributedStringRef text=CFAttributedStringCreate(NULL,CFSTR("W"),textAttrs);CTLineRef line=CTLineCreateWithAttributedString(text);double shaped=CTLineGetTypographicBounds(line,NULL,NULL,NULL);long glyphCount=CTLineGetGlyphCount(line);CFRelease(line);CFRelease(text);CFRelease(textAttrs);
   CFDictionaryRef actual=CTFontCopyVariation(font);double actualWeight=-1;CFNumberRef actualValue=actual?CFDictionaryGetValue(actual,key):NULL;int metadata=actualValue&&CFGetTypeID(actualValue)==CFNumberGetTypeID()&&CFNumberGetValue(actualValue,kCFNumberDoubleType,&actualWeight)&&fabs(actualWeight-expected)<.001;
   CFIndex count=actual?CFDictionaryGetCount(actual):0;const void *keys[count?count:1],*values[count?count:1];unsigned nonnumeric=0;if(actual){CFDictionaryGetKeysAndValues(actual,keys,values);for(CFIndex i=0;i<count;i++)nonnumeric+=CFGetTypeID(keys[i])!=CFNumberGetTypeID();CFRelease(actual);}
   int shaping=glyphCount==1&&fabs(shaped-advance.width)<.05;failures+=!metadata||nonnumeric||!shaping;
   const char *routes[]={"CT-descriptor","explicit-CG","CT-copy-descriptor","CT-memory-descriptor","Firefox-CG-name-copy"};
   printf("FXFONTVAR weight=%g route=%s ink=%u advance=%.12f shaped=%.12f glyphs=%ld metadata_weight=%.4f nonnumeric=%u metadata=%d shaping=%d hash=%016llx\n",value,routes[mode],ink,advance.width,shaped,glyphCount,actualWeight,nonnumeric,metadata,shaping,(unsigned long long)hashes[weight][mode]);CFRelease(font);CGFontRelease(variant);
  }
  CFRelease(key);CFRelease(desc);CFRelease(variations);
 }
 if(bench)failures+=benchmark(graphics);
 for(int weight=0;weight<2;weight++)for(int mode=0;mode<5;mode++)failures+=hashes[weight][mode]!=hashes[weight][1]||advances[weight][mode]!=advances[weight][1];
 unsigned afterInk=0;uint64_t baseAfter=paint(base,&afterInk);int pass=!failures&&hashes[0][1]!=hashes[1][1]&&baseBefore==baseAfter;printf("FXFONTVAR base_unchanged=%d pass=%d\n",baseBefore==baseAfter,pass);CFRelease(axes);CFRelease(base);CGFontRelease(graphics);return pass?0:1;
}}
