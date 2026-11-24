/* SPDX-License-Identifier: MIT */
#ifndef SFAVD_IMPORT_PAGES_H
#define SFAVD_IMPORT_PAGES_H
#include <stddef.h>
#include <stdint.h>
static inline void sfavd_pages_sift(uint64_t *p,size_t start,size_t n) {
 size_t root=start;
 while(root<n/2) {
  size_t child=root*2+1;
  if(child+1<n&&p[child]<p[child+1])child++;
  if(p[root]>=p[child])break;
  uint64_t value=p[root];p[root]=p[child];p[child]=value;root=child;
 }
}
/* Sort physical pages without kernel allocation/recursion; reject aliases. */
static inline int sfavd_pages_unique(uint64_t *p,size_t n) {
 if(!p||!n)return 0;
 for(size_t i=n/2;i>0;i--)sfavd_pages_sift(p,i-1,n);
 for(size_t end=n;end>1;end--) {
  uint64_t value=p[0];p[0]=p[end-1];p[end-1]=value;
  sfavd_pages_sift(p,0,end-1);
 }
 for(size_t i=0;i<n;i++)if(!p[i]||(p[i]&16383)||(i&&p[i]==p[i-1]))return 0;
 return 1;
}
static inline int sfavd_pages_contains(const uint64_t *p,size_t n,uint64_t value) {
 size_t lo=0,hi=n;
 while(lo<hi) {size_t mid=lo+(hi-lo)/2;if(p[mid]<value)lo=mid+1;else hi=mid;}
 return lo<n&&p[lo]==value;
}
#endif
