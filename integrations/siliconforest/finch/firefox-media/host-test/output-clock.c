/* SPDX-License-Identifier: MIT */
#include "../output-clock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 assert(sf_audio_callback_host(1200,1,200,0,500,99)==1000);
 assert(sf_audio_callback_host(1200,1,200,2,500,99)==2000);
 assert(sf_audio_callback_host(1200,0,200,0,500,99)==99);
 assert(sf_audio_callback_host(0,1,200,0,500,99)==99);
 assert(sf_audio_callback_host(1200,1,200,-1,500,99)==99);
 assert(sf_audio_callback_host(1200,1,200,NAN,500,99)==99);
 assert(sf_audio_callback_host(UINT64_MAX,1,0,2,500,99)==99);
 assert(sf_audio_callback_host(1200,1,200,1,24000000.0/44100.0,99)==1544);
 // Past-but-valid timestamps remain past; cubeb's saturating lead then has a
 // fixed-property floor. Do not falsely claim exact startup correction.
 assert(sf_audio_callback_host(1100,1,200,0,500,999)==900);
 puts("OUTPUT_CLOCK_HOST PASS arithmetic/startup/44k1/overflow/invalid; NOT hardware or acoustic proof");
}
