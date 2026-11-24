/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual decoder plus independent floating point geometry, host only. */
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "m1tp_mt.c"
#include "sf_tp_gesture_ring.h"

static struct m1tp_mt_state state;
static struct m1tp_mt_out output;
static uint64_t tick;
static void reset(void)
{
    struct m1tp_mt_config config = {0};config.thumb = M1TP_MT_OFF;
    m1tp_mt_init(&state,&config);tick = 1000000;
}
static void feed(unsigned n, int x, int y, int cx, int cy, unsigned button)
{
    uint8_t bytes[M1TP_MT_MAX_REPORT_BYTES];struct m1tp_mt_frame frame;
    int16_t xy[3][2]={{(int16_t)(cx-x),(int16_t)(cy+y)},
                       {(int16_t)(cx+x),(int16_t)(cy-y)}, {(int16_t)cx,(int16_t)(cy+500)}};
    tick += 8000;
    size_t size=m1tp_mt_build(bytes,sizeof(bytes),(uint8_t)button,(uint16_t)(tick/1000),n,
                               (const int16_t (*)[2])xy,NULL);
    assert(size && m1tp_mt_parse(bytes,size,&frame)==M1TP_MT_KIND_TOUCH);
    (void)m1tp_mt_step(&state,&frame,tick,&output);
}
static double distance(void)
{
    const struct m1tp_mt_slot *a=&state.slot[state.pinch_a],*b=&state.slot[state.pinch_b];
    return hypot((b->x-a->x)*1000.0/state.cfg.res_x,(b->y-a->y)*1000.0/state.cfg.res_y);
}
int main(void)
{
    assert(sizeof(struct sf_tp_swipe_ring)==776 && sizeof(struct sf_tp_swipe_event)==24);
    assert(offsetof(struct sf_tp_swipe_event,ms)==12 && offsetof(struct sf_tp_swipe_event,scale_q16)==16 &&
           offsetof(struct sf_tp_swipe_event,angle_cdeg)==20);
    /* Every quadrant and the +/-180 branch: no kernel floating point. */
    for(int i=-1800;i<=1800;++i){
        double a=i*acos(-1.0)/1800.0;
        int64_t x=llround(cos(a)*100000),y=llround(sin(a)*100000);
        double ref=atan2((double)y,(double)x)*18000.0/acos(-1.0);
        assert(fabs(pinch_angle(x,y)-ref)<2.0);
    }
    for(int sign=-1;sign<=1;sign+=2){
        reset();unsigned begins=0,updates=0,ends=0;double initial=0;uint32_t final_scale=0;
        int radius=sign>0?1000:3000;
        for(unsigned i=0;i<4;++i)feed(2,radius,0,0,3000,0);
        for(unsigned i=0;i<40;++i){
            radius+=sign*40;feed(2,radius,0,0,3000,0);
            assert(!output.dx && !output.dy && !output.wheel && !output.buttons);
            if(output.swipe==M1TP_MT_SWIPE_BEGIN){++begins;initial=distance();
                assert(output.gesture_kind==1 && output.scale_q16==65536 && !output.angle_cdeg);}
            if(output.swipe==M1TP_MT_SWIPE_UPDATE){++updates;
                assert(output.gesture_kind==1 && !output.angle_cdeg);
                double expected=distance()/initial*65536;
                assert(fabs(output.scale_q16-expected)<20);final_scale=output.scale_q16;}
        }
        assert(begins==1 && updates>20 && (sign>0?final_scale>65536:final_scale<65536));
        /* A brief lifted contact freezes geometry; return continues this gesture. */
        for(unsigned i=0;i<4;++i){feed(1,radius,0,0,3000,0);assert(!output.swipe);}
        feed(2,radius,0,0,3000,0);assert(state.pinch_active && output.swipe!=M1TP_MT_SWIPE_BEGIN);
        for(unsigned i=0;i<3;++i){feed(0,0,0,0,3000,0);if(output.swipe==M1TP_MT_SWIPE_END){
            ++ends;assert(output.gesture_kind==1 && output.scale_q16==final_scale);}}
        assert(ends==1 && !state.pinch_active);
    }
    /* Rotation both directions, including wrap. Compare each emitted delta to
     * the actual parsed contacts rather than to the input's rounded geometry. */
    for(int sign=-1;sign<=1;sign+=2){
        reset();unsigned begins=0,updates=0;double previous=0;int total=0;
        for(unsigned i=0;i<4;++i)feed(2,-2000,100,0,3000,0);
        for(unsigned i=0;i<120;++i){
            double radians=(177.0+sign*(double)i)*acos(-1.0)/180.0;
            feed(2,(int)llround(cos(radians)*2000),(int)llround(sin(radians)*2000),0,3000,0);
            assert(!output.dx && !output.dy && !output.wheel);
            if(state.pinch_active){
                const struct m1tp_mt_slot *a=&state.slot[state.pinch_a],*b=&state.slot[state.pinch_b];
                double angle=atan2((b->y-a->y)*1000.0/state.cfg.res_y,
                                   (b->x-a->x)*1000.0/state.cfg.res_x)*18000/acos(-1.0);
                if(output.swipe==M1TP_MT_SWIPE_BEGIN)++begins;
                if(output.swipe==M1TP_MT_SWIPE_UPDATE){double delta=angle-previous;
                    if(delta>18000)delta-=36000;else if(delta < -18000)delta+=36000;
                    assert(fabs(output.angle_cdeg-delta)<3);++updates;total+=output.angle_cdeg;}
                previous=angle;
            }
        }
        assert(begins==1 && updates>80 && sign*total>8000);
        /* A click changes gesture ownership: cancellation, never scroll. */
        feed(2,(int)llround(cos((177.0+sign*119.0)*acos(-1.0)/180.0)*2000),
             (int)llround(sin((177.0+sign*119.0)*acos(-1.0)/180.0)*2000),0,3000,1);
        assert(output.swipe==M1TP_MT_GESTURE_CANCEL && output.gesture_kind==1 && !output.wheel);
    }
    /* Centre translation is a pinch update, and one-finger lift ends after
     * the100ms count debounce. Third-finger substitution cancels once. */
    for(unsigned third=0;third<2;++third){
        reset();for(unsigned i=0;i<4;++i)feed(2,1000,0,0,3000,0);
        for(unsigned i=0;i<30;++i)feed(2,1000+(int)i*40,0,0,3000,0);
        assert(state.pinch_active);
        unsigned moved=0;
        for(unsigned i=1;i<=12;++i){feed(2,2160,0,(int)i*40,3000,0);
            if(output.swipe==M1TP_MT_SWIPE_UPDATE && output.swipe_dx>0)++moved;
            assert(!output.dx && !output.dy && !output.wheel);}
        assert(moved>=8);
        unsigned finished=0;
        for(unsigned i=0;i<17;++i){feed(third?3:1,2160,0,480,3000,0);
            if(output.gesture_kind==1 && output.swipe){
                assert(output.swipe==(third?M1TP_MT_GESTURE_CANCEL:M1TP_MT_SWIPE_END));++finished;}}
        assert(finished==1 && !state.pinch_active);
    }
    /* Non-pinch two-finger translation remains scroll with no pinch events. */
    reset();for(unsigned i=0;i<4;++i)feed(2,1000,0,0,3000,0);
    for(unsigned i=0;i<40;++i){feed(2,1000,0,0,3000+(int)i*40,0);assert(!output.swipe && !state.pinch_active);}
    /* Guard degenerate geometry and a protocol fallback without losing CANCEL. */
    reset();state.gesture=M1TP_MT_G_PINCH;state.finger_count=2;
    state.pinch_active=1;state.pinch_a=0;state.pinch_b=1;state.pinch_scale_q16=65536;
    struct m1tp_mt_frame basic={0};basic.kind=M1TP_MT_KIND_BASIC;
    (void)m1tp_mt_step(&state,&basic,tick+8000,&output);
    assert(output.swipe==M1TP_MT_GESTURE_CANCEL && !state.pinch_active);
    state.pinch_active=1;basic.kind=M1TP_MT_KIND_RESET;
    (void)m1tp_mt_step(&state,&basic,tick+16000,&output);
    assert(output.swipe==M1TP_MT_GESTURE_CANCEL && !state.pinch_active && state.gesture==M1TP_MT_G_NONE);
    puts("K14 actual decoder: 3601 angle oracles, pinch out/in, rotation/wrap, debounce/end/cancel, scroll exclusion and776-byte ABI PASS; host only");
}
