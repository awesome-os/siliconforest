/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define WORKERS 8
#define ITERATIONS 100
static CGFontRef font;
static uint64_t expected[WORKERS][ITERATIONS];
static unsigned mismatches[WORKERS];
/* Darwin does not supply pthread barriers: use a condition variable. */
static pthread_mutex_t startLock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t startCondition=PTHREAD_COND_INITIALIZER;
static int ready,go;
static uint64_t draw(unsigned worker,unsigned iteration) {
    unsigned char pixels[256*64*4]={0};
    CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    CGContextRef context=CGBitmapContextCreate(pixels,256,64,8,256*4,space,kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    if(!context) return 0;
    CGContextSetFont(context,font);CGContextSetFontSize(context,12+worker*2+iteration%4);
    CGContextSetRGBFillColor(context,1,1,1,1);
    CGGlyph glyphs[8];for(unsigned g=0;g<8;g++)glyphs[g]=36+(iteration*11+g)%1500;
    CGContextShowGlyphsAtPoint(context,4,20,glyphs,8);
    CGContextRelease(context);
    int ink=0;for(unsigned i=0;i<sizeof(pixels);i++)ink|=pixels[i];if(!ink)return 0;
    uint64_t hash=1469598103934665603ULL;for(unsigned i=0;i<sizeof(pixels);i++)hash=(hash^pixels[i])*1099511628211ULL;
    return hash;
}
static void *worker(void *arg) {
    @autoreleasepool {
    unsigned n=(unsigned)(uintptr_t)arg;
    pthread_mutex_lock(&startLock);ready++;pthread_cond_broadcast(&startCondition);
    while(!go)pthread_cond_wait(&startCondition,&startLock);pthread_mutex_unlock(&startLock);
    for(unsigned i=0;i<ITERATIONS;i++)if(draw(n,i)!=expected[n][i])mismatches[n]++;
    } return NULL;
}
int main(void) { @autoreleasepool {
    CTFontRef ct=CTFontCreateWithName(CFSTR("Inter"),18,NULL);
    font=CTFontCopyGraphicsFont(ct,NULL);if(!font)return 2;
    struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
    for(unsigned n=0;n<WORKERS;n++)for(unsigned i=0;i<ITERATIONS;i++){expected[n][i]=draw(n,i);if(!expected[n][i])return 2;}
    clock_gettime(CLOCK_MONOTONIC,&a);
    printf("FXFONT sequential_references=%d ready\n",WORKERS*ITERATIONS);fflush(stdout);
    pthread_t threads[WORKERS];for(unsigned n=0;n<WORKERS;n++)pthread_create(&threads[n],NULL,worker,(void*)(uintptr_t)n);
    pthread_mutex_lock(&startLock);while(ready<WORKERS)pthread_cond_wait(&startCondition,&startLock);go=1;pthread_cond_broadcast(&startCondition);pthread_mutex_unlock(&startLock);
    unsigned errors=0;for(unsigned n=0;n<WORKERS;n++){pthread_join(threads[n],NULL);errors+=mismatches[n];printf("FXFONT worker=%u expected=%llx mismatches=%u\n",n,(unsigned long long)expected[n][0],mismatches[n]);}
    clock_gettime(CLOCK_MONOTONIC,&b);printf("FXFONT renders=%d mismatches=%u seconds=%.6f\n",WORKERS*ITERATIONS,errors,(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)*1e-9);
    CGFontRelease(font);CFRelease(ct);return errors?1:0;
} }
