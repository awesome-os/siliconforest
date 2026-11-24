/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "capture-pool.h"
int main(void) {
 assert(sf_sfavd_capture_pool(12,32,NULL)==32);
 assert(sf_sfavd_capture_pool(12,32,"")==32);
 assert(sf_sfavd_capture_pool(12,32,"20")==20);
 assert(sf_sfavd_capture_pool(12,32,"12")==12);
 assert(sf_sfavd_capture_pool(12,32,"11")==32);
 assert(sf_sfavd_capture_pool(12,32,"33")==32);
 const char *bad[]={"0","-1","+20","20x"," 20","20 ","4294967296","99999999999999999999999999999999"};
 for(unsigned n=0;n<sizeof(bad)/sizeof(bad[0]);n++)assert(sf_sfavd_capture_pool(12,32,bad[n])==32);
 assert(sf_sfavd_capture_pool(1,UINT_MAX,"4294967295")==UINT_MAX);
 assert(sf_sfavd_capture_pool(1,UINT_MAX,"4294967296")==UINT_MAX);
 puts("capture pool input/codec-bound/overflow controls PASS; no device claim");
}
