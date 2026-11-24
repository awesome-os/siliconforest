/* SPDX-License-Identifier: MIT */
/* Own metadata utilities from runtime comparison of source-owned inputs. No
 * decoder, asset object, extension registration or Apple implementation copied. */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreGraphics/CGAffineTransform.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
extern const CFStringRef kCMFormatDescriptionExtension_ColorPrimaries;
extern const CFStringRef kCMFormatDescriptionExtension_TransferFunction;
extern const CFStringRef kCMFormatDescriptionExtension_YCbCrMatrix;
int32_t FigGetRotationAngleAndFlipsFromCGAffineTransform(CGAffineTransform t,Boolean *firstFlip,Boolean *secondFlip){
 int32_t angle=0;Boolean first=false,second=false;
 if(fabs(t.b)<1e-6&&fabs(t.c)<1e-6){
  if(t.a<0&&t.d<0)angle=180;
  else if(t.a<0&&t.d>0)second=true;
  else if(t.a>0&&t.d<0)first=true;
 }else if(fabs(t.a)<1e-6&&fabs(t.d)<1e-6){
  if(t.b>0&&t.c<0)angle=90;
  else if(t.b<0&&t.c>0)angle=270;
  else if(t.b>0&&t.c>0){angle=90;second=true;}
  else if(t.b<0&&t.c<0){angle=270;second=true;}
 }
 if(firstFlip)*firstFlip=first;if(secondFlip)*secondFlip=second;return angle;
}
CFStringRef MTCopyLocalizedStringForVideoDynamicRange(uint32_t value){
 static const char *names[]={"Unknown","SDR","HLG","Dolby Vision 8.4","HDR10","Dolby Vision 5","Dolby Vision 10","Dolby Vision 10.4","Dolby Vision 8.1"};
 return value<sizeof(names)/sizeof(names[0])?CFStringCreateWithCString(NULL,names[value],kCFStringEncodingUTF8):NULL;
}
struct code {const char *name;unsigned value;};
static unsigned code(CFTypeRef value,const struct code *table,size_t count){
 if(!value||CFGetTypeID(value)!=CFStringGetTypeID())return 2;
 for(size_t i=0;i<count;i++){
  CFStringRef key=CFStringCreateWithCString(NULL,table[i].name,kCFStringEncodingUTF8);
  Boolean equal=key&&CFEqual(value,key);if(key)CFRelease(key);if(equal)return table[i].value;
 }
 return 2;
}
CFStringRef MTCopyStringForColorInfo(uint32_t type,CMFormatDescriptionRef format){
 if(type!=0x76696465||!format||CMFormatDescriptionGetMediaType(format)!=0x76696465)return NULL;
 CFTypeRef p=CMFormatDescriptionGetExtension(format,kCMFormatDescriptionExtension_ColorPrimaries),
           t=CMFormatDescriptionGetExtension(format,kCMFormatDescriptionExtension_TransferFunction),
           m=CMFormatDescriptionGetExtension(format,kCMFormatDescriptionExtension_YCbCrMatrix);
 if(!p&&!t&&!m)return NULL;
 static const struct code primaries[]={{"ITU_R_709_2",1},{"SMPTE_C",6},{"SMPTE_240M_1995",7},{"ITU_R_2020",9}};
 static const struct code transfer[]={{"ITU_R_709_2",1},{"SMPTE_240M_1995",7},{"Linear",8},{"sRGB",13},{"SMPTE_ST_2084_PQ",16},{"ITU_R_2100_HLG",18}};
 static const struct code matrix[]={{"ITU_R_709_2",1},{"ITU_R_601_4",6},{"SMPTE_240M_1995",7},{"ITU_R_2020",9}};
 unsigned a=code(p,primaries,sizeof(primaries)/sizeof(primaries[0])),b=code(t,transfer,sizeof(transfer)/sizeof(transfer[0])),c=code(m,matrix,sizeof(matrix)/sizeof(matrix[0]));
 const char *label="";
 if(a==1&&b==1&&c==1)label="HD ";
 else if(a==6&&b==1&&c==6)label="SD ";
 else if(a==9&&b==16&&c==9)label="BT.2020 PQ ";
 else if(a==9&&b==18&&c==9)label="BT.2020 HLG ";
 char text[128];snprintf(text,sizeof(text),"%s(%u-%u-%u)",label,a,b,c);
 return CFStringCreateWithCString(NULL,text,kCFStringEncodingUTF8);
}
