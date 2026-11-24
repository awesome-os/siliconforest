/* SPDX-License-Identifier: MIT */
/* Whole upstream CLI with fake CoreGraphics/CF providers. No display changes. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
typedef unsigned CGDirectDisplayID;
typedef long CFIndex;
typedef int CGError;
struct Mode { size_t width,height; double hz; int32_t id; };
typedef struct Mode *CGDisplayModeRef;
typedef struct Mode *CFArrayRef;
enum { kCGErrorSuccess=0 };
static struct Mode modes[]={{800,600,60,1},{800,600,75,2},{1920,1080,60,3}};
static struct Mode *current=&modes[0];
static unsigned set_calls;static bool refuse,no_modes;
static CGDirectDisplayID CGMainDisplayID(void){return 1;}
static CFArrayRef CGDisplayCopyAllDisplayModes(CGDirectDisplayID d,void *options){assert(d==1&&!options);return no_modes?NULL:modes;}
static CFIndex CFArrayGetCount(CFArrayRef a){assert(a==modes);return 3;}
static const void *CFArrayGetValueAtIndex(CFArrayRef a,CFIndex i){assert(a==modes&&i>=0&&i<3);return a+i;}
static CGDisplayModeRef CGDisplayCopyDisplayMode(CGDirectDisplayID d){assert(d==1);return current;}
static int32_t CGDisplayModeGetIODisplayModeID(CGDisplayModeRef m){return m->id;}
static size_t CGDisplayModeGetWidth(CGDisplayModeRef m){return m->width;}
static size_t CGDisplayModeGetHeight(CGDisplayModeRef m){return m->height;}
static double CGDisplayModeGetRefreshRate(CGDisplayModeRef m){return m->hz;}
static void CGDisplayModeRelease(CGDisplayModeRef m){assert(m>=modes&&m<modes+3);}
static void CFRelease(CFArrayRef a){assert(a==modes);}
static CGError CGDisplaySetDisplayMode(CGDirectDisplayID d,CGDisplayModeRef m,void *options){assert(d==1&&!options);set_calls++;if(refuse)return 7;current=m;return 0;}
static unsigned fake_sleep(unsigned seconds){assert(seconds==1);return 0;}
#define sleep fake_sleep
#define main sfres_main
@APP@
#undef main
static int invoke(const char *arg,const char *second){char *argv[]={(char *)"sfres",(char *)arg,(char *)second,NULL};return sfres_main(second?3:arg?2:1,argv);}
int main(void){
 assert(!invoke(NULL,NULL)&&!set_calls);assert(!invoke("list",NULL)&&!set_calls);
 assert(!invoke("800x600",NULL)&&current->hz==75);
 assert(!invoke("800x600@60",NULL)&&current->hz==60);
 assert(!invoke("id","3")&&current->width==1920);
 unsigned before=set_calls;assert(invoke("999x999",NULL)==1&&set_calls==before);
 assert(invoke("nonsense",NULL)==2&&set_calls==before);
 assert(invoke("id","99")==1&&set_calls==before);
 refuse=true;assert(invoke("800x600",NULL)==1);refuse=false;
 no_modes=true;before=set_calls;assert(invoke(NULL,NULL)==1&&set_calls==before);
 puts("PASS whole sfres CLI with fake providers: list, highest/explicit refresh, mode ID, unavailable mode, refusal and no display");
 puts("LIMIT no real CG mode inventory, compositor handoff, hardware modeset or external display proof");
}
