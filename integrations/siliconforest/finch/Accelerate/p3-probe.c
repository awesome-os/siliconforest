/* SPDX-License-Identifier: MIT */
/* Named versus explicit-profile conversion, no display or image files. */
#include <Accelerate/Accelerate.h>
#include <ColorSync/ColorSync.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
/* Own declaration from the public API and pinned provider signature. */
CFDataRef ColorSyncProfileCopyData(ColorSyncProfileRef,CFErrorRef *);
static const unsigned char input[][4]={
 {0,0,0,255},{255,255,255,255},{255,0,0,255},{0,255,0,255},{0,0,255,255},
 {128,128,128,255},{46,46,46,255},{64,128,192,255},{192,64,128,255},{20,200,80,255}
};
static void need(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
static void convert(const char *name,CGColorSpaceRef from,CGColorSpaceRef to,int floating){
 enum{N=sizeof(input)/sizeof(input[0])};float fin[N][4],fout[N][4];unsigned char bout[N][4];
 for(unsigned i=0;i<N;i++)for(unsigned k=0;k<4;k++)fin[i][k]=input[i][k]/255.0f;
 memset(fout,0,sizeof fout);memset(bout,0,sizeof bout);
 vImage_CGImageFormat a={0},b={0};a.bitsPerComponent=b.bitsPerComponent=floating?32:8;
 a.bitsPerPixel=b.bitsPerPixel=floating?128:32;a.colorSpace=from;b.colorSpace=to;
 a.bitmapInfo=b.bitmapInfo=kCGImageAlphaLast|(floating?kCGBitmapFloatComponents|kCGBitmapByteOrder32Little:0);
 a.renderingIntent=b.renderingIntent=kCGRenderingIntentRelativeColorimetric;
 vImage_Error error=0;vImageConverterRef c=vImageConverter_CreateWithCGImageFormat(&a,&b,NULL,0,&error);
 need(c&&error==0,"create converter");
 vImage_Buffer src={floating?(void*)fin:(void*)input,1,N,floating?sizeof fin:sizeof input};
 vImage_Buffer dst={floating?(void*)fout:(void*)bout,1,N,floating?sizeof fout:sizeof bout};
 error=vImageConvert_AnyToAny(c,&src,&dst,NULL,0);need(error==0,"convert pixels");
 for(unsigned i=0;i<N;i++){
  printf("PIX %s %s %u",name,floating?"F32":"U8",i);
  for(unsigned k=0;k<4;k++)if(floating){need(isfinite(fout[i][k]),"finite output");printf(" %.9g",fout[i][k]);}else printf(" %u",bout[i][k]);
  putchar('\n');
 }
 vImageConverter_Release(c);
}
int main(void){setvbuf(stdout,NULL,_IOLBF,0);
 CGColorSpaceRef srgb=CGColorSpaceCreateWithName(kCGColorSpaceSRGB),p3=CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
 need(srgb&&p3,"named spaces");
 CFDataRef nativeICC=CGColorSpaceCopyICCData(p3);
 printf("PROFILE named-P3 icc=%d name=%d\n",nativeICC!=NULL,CFEqual(CGColorSpaceGetName(p3),kCGColorSpaceDisplayP3));
 if(nativeICC)CFRelease(nativeICC);
 ColorSyncProfileRef profile=ColorSyncProfileCreateWithName(kColorSyncDisplayP3Profile);need(profile!=NULL,"real P3 profile");
 CFDataRef bytes=ColorSyncProfileCopyData(profile,NULL);need(bytes!=NULL,"real P3 profile bytes");
 CGColorSpaceRef icc=CGColorSpaceCreateWithICCData(bytes);need(icc!=NULL,"explicit P3 space");
 const vImageRGBPrimaries primaries={.red_x=.68,.red_y=.32,.green_x=.265,.green_y=.69,.blue_x=.15,.blue_y=.06,.white_x=.3127,.white_y=.3290};
 const vImageTransferFunction transfer={1.055,1,0,-.055,1.0/2.4,.0031308,12.92,0};
 vImage_Error error=0;
 CGColorSpaceRef generated=vImageCreateRGBColorSpaceWithPrimariesAndTransferFunction(&primaries,&transfer,kCGRenderingIntentRelativeColorimetric,0,&error);
 need(generated&&error==0,"P3 generated from public colorimetry");
 for(int f=0;f<2;f++){
  convert("identity",srgb,srgb,f);convert("named-P3",srgb,p3,f);
  convert("ICC-P3",srgb,icc,f);convert("generated-P3",srgb,generated,f);
 }
 CGColorSpaceRelease(generated);CGColorSpaceRelease(icc);CFRelease(bytes);CFRelease(profile);CGColorSpaceRelease(p3);CGColorSpaceRelease(srgb);
 puts("PASS: named/ICC/generated P3 conversion observations");return 0;
}
