#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Private patch of the reviewed CGL observer: no per-frame filesystem writes."""
from pathlib import Path
import sys

p=Path(sys.argv[1]);s=p.read_text()
def replace(old,new):
 global s
 if s.count(old)!=1:raise ValueError('Exact reviewed CGL observer anchor required: '+old[:70])
 s=s.replace(old,new)
replace('static double sffx_frame_previous_emit_ms;',r'''#include <errno.h>
static char sffx_frame_log[1200*1024];
static size_t sffx_frame_log_used;
static unsigned sffx_frame_log_records,sffx_frame_log_overflow,sffx_frame_preview,sffx_frame_log_finished;
static double sffx_frame_format_sum,sffx_frame_format_max,sffx_frame_hook_sum,sffx_frame_hook_max;
static void sffx_frame_configure(void);
__attribute__((visibility("default"))) int SFFXFrameIDFlushLog(void){
    pthread_once(&sffx_frame_once,sffx_frame_configure);
    if(!sffx_frame_enabled)return 3;
    if(pthread_mutex_trylock(&sffx_frame_lock))return 1;
    if(sffx_frame_log_finished){pthread_mutex_unlock(&sffx_frame_lock);return 2;}
    sffx_frame_log_finished=1;
    double before=sf_cgl_now();
    ssize_t written=write(STDERR_FILENO,sffx_frame_log,sffx_frame_log_used);
    int error=written<0?errno:0;
    double after=sf_cgl_now();
    char summary[512];
    int size=snprintf(summary,sizeof(summary),
        "FXFRAMEID_BUFFER explicit_flush=1 pid=%d records=%u bytes=%zu write_return=%zd write_errno=%d write_ms=%.6f overflow=%u format_sum_ms=%.6f format_max_ms=%.6f hook_sum_ms=%.6f hook_max_ms=%.6f\n",
        getpid(),sffx_frame_log_records,sffx_frame_log_used,written,error,after-before,
        sffx_frame_log_overflow,sffx_frame_format_sum,sffx_frame_format_max,
        sffx_frame_hook_sum,sffx_frame_hook_max);
    if(size>0&&(size_t)size<sizeof(summary))(void)write(STDERR_FILENO,summary,(size_t)size);
    int status=written==(ssize_t)sffx_frame_log_used&&!error&&!sffx_frame_log_overflow?0:4;
    pthread_mutex_unlock(&sffx_frame_lock);return status;
}''')
replace('    pthread_mutex_lock(&sffx_frame_lock);\n    double before=sf_cgl_now();',
        '    pthread_mutex_lock(&sffx_frame_lock);\n    if(sffx_frame_log_finished){pthread_mutex_unlock(&sffx_frame_lock);return;}\n    double before=sf_cgl_now();')
replace('    char line[1024];','    double format_started=sf_cgl_now();\n    char line[1024];')
replace(' previous_emit_ms=%.6f readback_bytes=', ' log_buffered=1 readback_bytes=')
replace('before,after,sffx_frame_previous_emit_ms,pixels*8','before,after,pixels*8')
replace('if(size<0||(size_t)size>=sizeof(line)-used)goto frame_emit_done;',
        'if(size<0||(size_t)size>=sizeof(line)-used){sffx_frame_log_overflow++;goto frame_emit_done;}')
start=s.index('    if(used&&used<sizeof(line)){')
end=s.index('frame_emit_done:',start)
s=s[:start]+r'''    if(used&&used+1<sizeof(line)){
        line[used++]='\n';line[used]='\0';
        if(used>sizeof(sffx_frame_log)-sffx_frame_log_used){
            sffx_frame_log_overflow++;goto frame_emit_done;
        }
        memcpy(sffx_frame_log+sffx_frame_log_used,line,used);
        sffx_frame_log_used+=used;sffx_frame_log_records++;
        double format_ms=sf_cgl_now()-format_started,hook_ms=sf_cgl_now()-before;
        sffx_frame_format_sum+=format_ms;sffx_frame_hook_sum+=hook_ms;
        if(format_ms>sffx_frame_format_max)sffx_frame_format_max=format_ms;
        if(hook_ms>sffx_frame_hook_max)sffx_frame_hook_max=hook_ms;
        // One frame-zero admission receipt is required BEFORE playback. Its
        // distinct prefix prevents duplication in the final ordered ID trace.
        if(frame==0&&!sffx_frame_preview){
            sffx_frame_preview=1;
            char preview[1048];
            int n=snprintf(preview,sizeof(preview),"FXFRAMEID_PREVIEW %s",line+strlen("FXFRAMEID "));
            if(n>0&&(size_t)n<sizeof(preview))(void)write(STDERR_FILENO,preview,(size_t)n);
        }
    }else sffx_frame_log_overflow++;
''' +s[end:]
p.write_text(s)
