/* SPDX-License-Identifier: MIT */
/* Two already completed packed RGBA/BGRA one-pixel marker rows. No GL calls.
 * Rows span sample_left88..1816 in the original1920-wide video, scaled with it.
 * This decodes an image's identity, never claims that it was shown on a panel. */
#ifndef SF_MEDIA_FRAME_ID_DECODE_H
#define SF_MEDIA_FRAME_ID_DECODE_H
static inline int sf_media_frame_id_rgba(const unsigned char *a,const unsigned char *b,unsigned pixels)
{
    static const unsigned prefix[4]={1,1,0,1},suffix[4]={0,0,1,0};
    if(!a||!b||pixels<54||pixels>65536)return -1;
    unsigned bits[18];
    for(unsigned column=0;column<18;column++){
        unsigned x=(2*column+1)*pixels/36,values[2];
        for(unsigned row=0;row<2;row++){
            const unsigned char *p=row?b:a;unsigned sum=0;
            for(int d=-1;d<=1;d++)for(unsigned c=0;c<3;c++)sum+=p[4*(x+d)+c];
            unsigned level=sum/9;if(level>=80&&level<=180)return -1;
            values[row]=level>180;
        }
        if(values[0]==values[1])return -1;
        bits[column]=values[0];
    }
    for(unsigned i=0;i<4;i++)if(bits[i]!=prefix[i]||bits[i+14]!=suffix[i])return -1;
    unsigned gray=0,frame=0;for(unsigned i=0;i<10;i++)gray|=bits[i+4]<<i;
    while(gray){frame^=gray;gray>>=1;}
    return frame<900?(int)frame:-1;
}
#endif
