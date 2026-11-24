/* SPDX-License-Identifier: MIT */
/* Linux-built runtime measurement of public Quadrature semantics, no SDK. */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stddef.h>
typedef void (*callback)(void *,size_t,const double *,double *);
typedef struct { callback fun; void *fun_arg; } function;
typedef struct { int integrator; double abs_tolerance,rel_tolerance; size_t qag_points_per_interval,max_intervals; } options;
static int calls;
static void integrand(void *arg,size_t count,const double *x,double *y)
{
    calls+=(int)count;
    for(size_t i=0;i<count;i++)y[i]=arg ? 1/sqrt(x[i]) : exp(x[i]);
}
int main(void)
{
    void *handle=dlopen("/System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/vecLib.framework/Versions/A/libQuadrature.dylib",RTLD_NOW|RTLD_LOCAL);
    if(!handle){fprintf(stderr,"%s\n",dlerror());return 1;}
    double (*integrate)(const function *,double,double,const options *,int *,double *,size_t,void *);
    *(void **)&integrate=dlsym(handle,"quadrature_integrate");
    if(!integrate)return 1;
    function f={integrand,NULL};
    for(int it=0;it<5;it++) {
        options o={it,1e-10,1e-10,21,50};int status;double err;
        double result=integrate(&f,0,1,&o,&status,&err,0,NULL);
        printf("integrator %d result=%.17g status=%d\n",it,result,status);
    }
    union { long double alignment; unsigned char data[8192]; } workspace;
    for(int it=1;it<=2;it++) {
        options o={it,1e-14,0,15,100};int last_status=999,last_calls=-1;
        f.fun_arg=&calls;
        for(size_t bytes=1;bytes<=512;bytes++) {
            calls=0;int status;
            double result=integrate(&f,0,1,&o,&status,NULL,bytes,workspace.data);
            if(status!=last_status || calls!=last_calls) {
                printf("workspace integrator=%d bytes=%zu status=%d calls=%d result=%.12g\n",it,bytes,status,calls,result);
                last_status=status;last_calls=calls;
            }
        }
    }
    return 0;
}
