/* SPDX-License-Identifier: MIT */
/* Public void opt-in API. No extension registry exists; report that limitation
 * rather than claim readers were installed or fabricate assets. */
#include <dispatch/dispatch.h>
#include <stdio.h>
void MTRegisterProfessionalVideoWorkflowFormatReaders(void){
 static dispatch_once_t once;
 dispatch_once(&once,^{fputs("MediaToolbox: professional MediaExtension readers unavailable (no registry)\n",stderr);});
}
