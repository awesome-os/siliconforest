/* SPDX-License-Identifier: MIT */
#include <sfavd/sfavd_import_validate.h>
#include <sfavd/sfavd_import_pages.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(struct sfavd_import)==528,"fixed ABI1 extension layout");
static int compare(const void *a,const void *b){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return (x>y)-(x<y);}
static struct sfavd_import valid(void) {
 struct sfavd_import r={.ctx=3,.count=2};
 r.buffers[0]=(struct sfavd_import_buffer){0x100000000,0x8000};
 r.buffers[1]=(struct sfavd_import_buffer){0x100008000,0x8000};return r;
}
int main(void) {
 struct sfavd_import r=valid();assert(!sfavd_import_validate(&r,2,0x7001));
 assert(sfavd_import_validate(NULL,2,1)==EINVAL);
 assert(sfavd_import_validate(&r,1,1)==EINVAL);
 assert(sfavd_import_validate(&r,33,1)==EINVAL);
 assert(sfavd_import_validate(&r,2,0)==EINVAL);
 assert(sfavd_import_validate(&r,2,SFAVD_IMPORT_MAX_BYTES+1)==EINVAL);
 r.flags=1;assert(sfavd_import_validate(&r,2,1)==EINVAL);r=valid();
 r.reserved=1;assert(sfavd_import_validate(&r,2,1)==EINVAL);r=valid();
 r.buffers[1].address+=1;assert(sfavd_import_validate(&r,2,1)==EINVAL);r=valid();
 r.buffers[1].address=r.buffers[0].address+0x4000;assert(sfavd_import_validate(&r,2,1)==EINVAL);r=valid();
 r.buffers[1].size=0x4000;assert(sfavd_import_validate(&r,2,0x4001)==EINVAL);r=valid();
 r.buffers[1].address=UINT64_MAX-0x3fff;assert(sfavd_import_validate(&r,2,1)==EINVAL);r=valid();
 r.buffers[1].address=SFAVD_IMPORT_USER_LIMIT-0x4000;assert(sfavd_import_validate(&r,2,1)==EINVAL);r=valid();
 r.buffers[2]=r.buffers[0];assert(sfavd_import_validate(&r,2,1)==EINVAL);
 /* Exhaust all relative placements around an accepted boundary. Accepted
  * cases must have non-overlapping, aligned, in-range intervals. */
 for (int offset=-16;offset<=16;offset++) {
  r=valid();r.buffers[1].address=r.buffers[0].address+(int64_t)offset*0x4000;
  int accepted=!sfavd_import_validate(&r,2,0x8000);
  assert(accepted==(offset<=-2||offset>=2));
 }
 uint32_t seed=1;
 for(unsigned test=0;test<10000;test++) {
  uint64_t pages[32],reference[32];size_t n=test%32+1;
  for(size_t i=0;i<n;i++){seed=seed*1664525+1013904223;pages[i]=(uint64_t)(seed%64+1)*16384;}
  memcpy(reference,pages,n*sizeof(*pages));qsort(reference,n,sizeof(*pages),compare);
  int unique=1;for(size_t i=1;i<n;i++)if(reference[i]==reference[i-1])unique=0;
  assert(sfavd_pages_unique(pages,n)==unique);
  assert(!memcmp(pages,reference,n*sizeof(*pages)));
  for(unsigned i=0;i<66;i++) {
   int found=0;for(size_t j=0;j<n;j++)if(pages[j]==(uint64_t)i*16384)found=1;
   assert(sfavd_pages_contains(pages,n,(uint64_t)i*16384)==found);
  }
 }
 puts("SFAVD optional-import admission/ABI/overlap boundaries PASS; no hardware");
}
