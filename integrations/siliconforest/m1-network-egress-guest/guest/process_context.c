#include "process_context.h"
#include <string.h>
static int shell(const char *name) {
 return !strcmp(name,"sh") || !strcmp(name,"zsh") ||
        !strncmp(name,"darwin-desktop-test",16);
}
int eg_process_context(const struct eg_process *p,size_t n,int32_t self,int32_t parent) {
 if(!p || !n || n>64 || self<2 || parent<1)return 0;
 unsigned char allowed[64]={0};int32_t current=self;int found_init=0;
 for(size_t i=0;i<n;i++) {
  if(p[i].pid<0 || p[i].parent<0 || p[i].euid || !memchr(p[i].name,0,sizeof(p[i].name)))return 0;
  for(size_t j=0;j<i;j++)if(p[j].pid==p[i].pid)return 0;
 }
 for(unsigned depth=0;depth<8;depth++) {
  size_t i=0;for(;i<n;i++)if(p[i].pid==current)break;
  if(i==n || allowed[i])return 0;
  allowed[i]=1;
  if(current==1) {
   if(strcmp(p[i].name,"launchd") || p[i].parent)return 0;
   found_init=1;break;
  }
  if(current==self) {if(p[i].parent!=parent)return 0;}
  else if(!shell(p[i].name))return 0;
  current=p[i].parent;if(current<1)return 0;
 }
 if(!found_init)return 0;
 for(size_t i=0;i<n;i++)if(!allowed[i] &&
    (p[i].pid!=0 || p[i].parent || strcmp(p[i].name,"kernel_task")))return 0;
 return 1;
}
