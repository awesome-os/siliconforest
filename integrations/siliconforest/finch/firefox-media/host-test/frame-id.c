/* SPDX-License-Identifier: MIT */
#include "../frame-id-decode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    unsigned char a[1728*4],b[1728*4];
    const unsigned prefix[4]={1,1,0,1},suffix[4]={0,0,1,0};
    for(unsigned scale=1;scale<=8;scale++)for(unsigned frame=0;frame<900;frame++){
        unsigned pixels=1728/scale,gray=frame^(frame>>1);
        for(unsigned x=0;x<pixels;x++){
            unsigned col=x*18/pixels;
            unsigned bit=col<4?prefix[col]:col>=14?suffix[col-14]:(gray>>(col-4))&1;
            for(unsigned c=0;c<4;c++){a[4*x+c]=bit?224:32;b[4*x+c]=bit?32:224;}
        }
        assert(sf_media_frame_id_rgba(a,b,pixels)==(int)frame);
    }
    memcpy(b,a,sizeof(b));assert(sf_media_frame_id_rgba(a,b,1728)==-1);
    memset(a,128,sizeof(a));assert(sf_media_frame_id_rgba(a,b,1728)==-1);
    assert(sf_media_frame_id_rgba(a,b,1)==-1&&sf_media_frame_id_rgba(NULL,b,1728)==-1);
    puts("FRAME_ID_HOST PASS 7200scaledIDs/mixed-rows/ambiguous/bounds; no target presentation claim");
}
