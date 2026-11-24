/* SPDX-License-Identifier: MIT */
/* Built on Linux; read public runtime behaviour on the controller Mac.
 * No SDK headers, GUI, files, network, or changes to the machine. */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef void *object;
#define LOAD(R,N,A) R (*N) A; *(void **)&N=dlsym(h,#N); if(!N){fprintf(stderr,"missing: %s\n",#N);return 1;}
int main(void) {
    void *h=dlopen("/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libLinearAlgebra.dylib",RTLD_NOW|RTLD_LOCAL);
    if(!h){fprintf(stderr,"%s\n",dlerror());return 1;}
    LOAD(object,la_matrix_from_double_buffer,(const double *,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long));
    LOAD(object,la_matrix_from_float_buffer,(const float *,unsigned long,unsigned long,unsigned long,unsigned long,unsigned long));
    LOAD(object,la_sum,(object,object));
    LOAD(object,la_solve,(object,object));
    LOAD(object,la_matrix_slice,(object,long,long,long,long,unsigned long,unsigned long));
    LOAD(object,la_identity_matrix,(unsigned long,unsigned int,unsigned long));
    LOAD(long,la_matrix_to_double_buffer,(double *,unsigned long,object));
    LOAD(long,la_matrix_to_float_buffer,(float *,unsigned long,object));
    LOAD(long,la_status,(object));
    LOAD(double,la_norm_as_double,(object,unsigned long));
    void (*la_release)(object);
    *(void **)&la_release=dlsym(h,"la_release");
    if(!la_release)*(void **)&la_release=dlsym(RTLD_DEFAULT,"os_release");
    if(!la_release){fprintf(stderr,"missing object release\n");return 1;}
    double data[]={3,4,0,0}, out[4]={0};float fdata[]={3,4},fout[4]={0};
    object a=la_matrix_from_double_buffer(data,2,1,1,0,0);
    object b=la_matrix_from_double_buffer(data,1,1,1,0,0);
    object f=la_matrix_from_float_buffer(fdata,2,1,1,0,0);
    if(!a||!b||!f)return 1;
    printf("valid status=%ld\n",la_status(a));
    for(unsigned long n=0;n<6;n++)printf("norm %lu = %.17g\n",n,la_norm_as_double(a,n));
    object mismatch=la_sum(a,b),precision=la_sum(a,f);
    object invalid=la_matrix_from_double_buffer(data,2,1,0,0,0);
    object slice=la_matrix_slice(a,4,0,1,1,1,1);
    printf("dimension=%ld precision=%ld invalid=%ld slice=%ld\n",la_status(mismatch),la_status(precision),la_status(invalid),la_status(slice));
    /* Test single-bit scalar encodings without taking values from an SDK. */
    for(unsigned i=0;i<21;i++){
        unsigned int type=i ? 1U<<(i-1) : 0;
        object id=la_identity_matrix(2,type,0);
        printf("scalar %u status=%ld double=%ld float=%ld\n",type,la_status(id),la_matrix_to_double_buffer(out,2,id),la_matrix_to_float_buffer(fout,2,id));
        la_release(id);
    }
    double zeros[4]={0};object singular=la_matrix_from_double_buffer(zeros,2,2,2,0,0);
    object solution=la_solve(singular,a);long conversion=la_matrix_to_double_buffer(out,1,solution);
    printf("singular conversion=%ld status=%ld\n",conversion,la_status(solution));
    object all[]={a,b,f,mismatch,precision,invalid,slice,singular,solution};
    for(unsigned i=0;i<sizeof(all)/sizeof(*all);i++)la_release(all[i]);
    return 0;
}
