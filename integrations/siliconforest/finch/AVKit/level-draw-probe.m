/* SPDX-License-Identifier: MIT */
/* Our bitmap-only meter test; no window, input, capture or output device. */
#import <AVKit/AVKit.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int draw(AVAudioLevelIndicatorView *view,CGContextRef bitmap,double value,unsigned char *bytes,size_t count){
 memset(bytes,0,count);view.doubleValue=value;[view drawRect:view.bounds];
 unsigned nonzero=0;for(size_t n=0;n<count;n++)nonzero+=bytes[n]!=0;return nonzero!=0;
}
int main(void){@autoreleasepool{
 enum{width=240,height=9,count=width*height*4};unsigned char bytes[count],quiet[count],half[count];
 CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();CGContextRef bitmap=CGBitmapContextCreate(bytes,width,height,8,width*4,color,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);CGColorSpaceRelease(color);
 if(!bitmap)return 1;
 AVAudioLevelIndicatorView *view=[[AVAudioLevelIndicatorView alloc] initWithFrame:NSMakeRect(0,0,width,height)];
 NSGraphicsContext *graphics=[NSGraphicsContext graphicsContextWithGraphicsPort:bitmap flipped:NO];
 [NSGraphicsContext saveGraphicsState];[NSGraphicsContext setCurrentContext:graphics];
 int good=draw(view,bitmap,0,bytes,count);memcpy(quiet,bytes,count);
 good&=draw(view,bitmap,.5,bytes,count);memcpy(half,bytes,count);
 good&=draw(view,bitmap,1,bytes,count);
 unsigned middle=0,full=0;for(size_t n=0;n<count;n++){middle+=quiet[n]!=half[n];full+=half[n]!=bytes[n];}
 good&=middle>0&&full>0&&view.doubleValue==1;
 [NSGraphicsContext restoreGraphicsState];CGContextRelease(bitmap);[view release];
 printf("LEVEL_DRAW quiet-to-half=%u half-to-full=%u values-stored=1 no-window=1\n",middle,full);
 return good?0:1;
}}
