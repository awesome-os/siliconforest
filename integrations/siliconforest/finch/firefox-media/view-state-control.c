/* SPDX-License-Identifier: MIT */
#include <dlfcn.h>
#include <stdio.h>
int main(void){
    int absent=dlsym(RTLD_DEFAULT,"NSApp")==NULL;
    printf("SFVIEW_CONTROL no-AppKit-loaded=%d\n",absent);
    return absent?0:1;
}
