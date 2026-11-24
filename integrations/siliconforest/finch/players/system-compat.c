/* SPDX-License-Identifier: MIT */
/* VLC's legacy errno table, backed by the running PD strerror implementation.
 * Prefix-only facade re-exports the actual installed libSystem; no core swap. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
const char *sys_errlist[ELAST+1];
int sys_nerr=ELAST+1;
__attribute__((constructor)) static void make_error_table(void){
    for(int n=0;n<=ELAST;n++){
        const char *text=strerror(n);
        char *copy=text?strdup(text):NULL;
        sys_errlist[n]=copy?copy:"Unknown error";
    }
}
