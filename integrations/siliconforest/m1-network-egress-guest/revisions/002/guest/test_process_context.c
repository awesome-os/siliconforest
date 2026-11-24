#include "process_context.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error assertions required
#endif
int main(void) {
 struct eg_process base[6]={{0,0,0,"kernel_task"},{1,0,0,"launchd"},
   {6,1,0,"zsh"},{7,6,0,"darwin-desktop-t"},{9,7,0,"darwin-net-egres"}};
 struct eg_process p[6];memcpy(p,base,sizeof(p));
 assert(eg_process_context(p,5,9,7));
 p[5]=(struct eg_process){10,1,0,"sh"};assert(!eg_process_context(p,6,9,7));
 p[5]=(struct eg_process){10,6,0,"zsh"};assert(!eg_process_context(p,6,9,7));
 memcpy(p,base,sizeof(p));p[3].parent=9;assert(!eg_process_context(p,5,9,7));
 memcpy(p,base,sizeof(p));p[4].parent=6;assert(!eg_process_context(p,5,9,7));
 memcpy(p,base,sizeof(p));p[1].euid=1;assert(!eg_process_context(p,5,9,7));
 memcpy(p,base,sizeof(p));strcpy(p[1].name,"sh");assert(!eg_process_context(p,5,9,7));
 memcpy(p,base,sizeof(p));p[2].pid=1;assert(!eg_process_context(p,5,9,7));
 memcpy(p,base,sizeof(p));memset(p[3].name,'x',sizeof(p[3].name));assert(!eg_process_context(p,5,9,7));
 memcpy(p,base,sizeof(p));strcpy(p[0].name,"root-helper");assert(!eg_process_context(p,5,9,7));
 memcpy(p,base,sizeof(p));strcpy(p[3].name,"route");assert(!eg_process_context(p,5,9,7));
 assert(!eg_process_context(p,0,9,7));assert(!eg_process_context(p,65,9,7));
 struct eg_process actual[7]={{0,0,0,"kernel_task"},{1,0,0,"launchd"},
   {2,1,0,"launchctl"},{3,2,0,"zsh"},{7,3,0,"zsh"},{8,7,0,"darwin-net-egres"}};
 assert(eg_process_context(actual,6,8,7));
 actual[6]=(struct eg_process){10,1,0,"launchctl"};assert(!eg_process_context(actual,7,8,7));
 actual[2].parent=3;assert(!eg_process_context(actual,6,8,7));actual[2].parent=1;
 actual[2].euid=1;assert(!eg_process_context(actual,6,8,7));actual[2].euid=0;
 strcpy(actual[2].name,"launchctl-extra");assert(!eg_process_context(actual,6,8,7));
 strcpy(actual[2].name,"launchctl");actual[3].parent=1;
 assert(!eg_process_context(actual,6,8,7));
 struct eg_process indirect[6]={{0,0,0,"kernel_task"},{1,0,0,"launchd"},
   {2,1,0,"zsh"},{3,2,0,"launchctl"},{7,3,0,"zsh"},{8,7,0,"darwin-net-egres"}};
 assert(!eg_process_context(indirect,6,8,7));
 puts("exact process ancestry: unrelated root shells/cycles/UID/identity/shape negatives PASS");
}
