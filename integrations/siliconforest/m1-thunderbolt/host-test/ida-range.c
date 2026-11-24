/* SPDX-License-Identifier: MIT */
#include "sf_tb_ida_range.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint32_t end;
    assert(!sf_tb_ida_exclusive_end(8,8,&end) && end==9);
    assert(!sf_tb_ida_exclusive_end(0,0,&end) && end==1);
    assert(!sf_tb_ida_exclusive_end(INT_MAX,INT_MAX,&end) && end==0);
    assert(!sf_tb_ida_exclusive_end(INT_MAX,UINT32_MAX,&end) && end==0);
    assert(!sf_tb_ida_exclusive_end(0,UINT32_MAX,&end) && end==0);
    end=123;
    assert(sf_tb_ida_exclusive_end(9,8,&end)==-ENOSPC && end==123);
    assert(sf_tb_ida_exclusive_end((uint32_t)INT_MAX+1,UINT32_MAX,&end)==-ENOSPC && end==123);
    assert(sf_tb_ida_exclusive_end(0,0,NULL)==-EINVAL);
    unsigned cases=0;
    for(uint32_t lo=0;lo<256;++lo)for(uint32_t hi=lo;hi<512;++hi){
        assert(!sf_tb_ida_exclusive_end(lo,hi,&end));
        /* The backend accepts [start,end). Both endpoints must fit, and
         * the very next ID must be excluded, including singleton ranges. */
        assert(lo<end && hi<end && hi+1==end);++cases;
    }
    printf("IDA range ABI host checks: %u ranges, fixed HopID8, singleton0, INT_MAX/clamp/invalid/output preservation passed; no native allocator or hardware execution\n",cases);
}
