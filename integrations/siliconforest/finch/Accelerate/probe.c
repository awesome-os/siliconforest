/* SPDX-License-Identifier: MIT */
/* Independent known-vector checks of actual staged numeric shared libraries. */
#define SF_COMPONENT_vDSP
#define SF_COMPONENT_vMisc
#define SF_COMPONENT_LAPACK
#include <Accelerate/Accelerate.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
void cblas_dgemm(int,int,int,int,int,int,double,const double *,int,const double *,int,double,double *,int);
float cblas_sdot(int,const float *,int,const float *,int);
double sdot_(const int *,const float *,const int *,const float *,const int *);
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"failed: %s\n",#x);return 1; } }while(0)
static int near(double a,double b) { return fabs(a-b)<1e-6; }
int main(void) {
    float a[]={1,2,3,4}, b[]={5,6,7,8}, out[4]={0};
    vDSP_vadd(a,1,b,1,out,1,4);
    CHECK(out[0]==6 && out[3]==12);
    float sum=0; vDSP_dotpr(a,1,b,1,&sum,4); CHECK(sum==70);
    float re[]={1,0,0,0}, im[]={0,0,0,0}; DSPSplitComplex z={re,im};
    FFTSetup fft=vDSP_create_fftsetup(2,kFFTRadix2);CHECK(fft);
    vDSP_fft_zip(fft,&z,1,2,FFT_FORWARD);
    for(int i=0;i<4;i++)CHECK(near(re[i],1)&&near(im[i],0));
    vDSP_fft_zip(fft,&z,1,2,FFT_INVERSE);CHECK(near(re[0],4)&&near(re[1],0));
    vDSP_destroy_fftsetup(fft);
    double coef[]={1,0,0,0,0}; float delay[4]={0};
    vDSP_biquad_Setup biquad=vDSP_biquad_CreateSetup(coef,1);CHECK(biquad);
    vDSP_biquad(biquad,delay,a,1,out,1,4);CHECK(out[0]==1 && out[3]==4);
    vDSP_biquad_DestroySetup(biquad);
    float sq[]={1,4,9,16}; int n=4;vvsqrtf(out,sq,&n);
    CHECK(out[0]==1&&out[1]==2&&out[2]==3&&out[3]==4);
    double x[]={1,2,3,4},y[]={5,6,7,8},c[4]={0};
    cblas_dgemm(101,111,111,2,2,2,1,x,2,y,2,0,c,2);
    CHECK(c[0]==19&&c[1]==22&&c[2]==43&&c[3]==50);
    typedef void (*gemm64)(int,int,int,long,long,long,double,const double *,long,const double *,long,double,double *,long);
    gemm64 wide=(gemm64)dlsym(RTLD_DEFAULT,"cblas_dgemm$NEWLAPACK$ILP64");CHECK(wide);
    wide(101,111,111,2,2,2,1,x,2,y,2,0,c,2);CHECK(c[0]==19&&c[3]==50);
    CHECK(cblas_sdot(4,a,1,b,1)==70);
    int one=1;CHECK(sdot_(&n,a,&one,b,&one)==70);
    typedef float (*dotnew)(const int *,const float *,const int *,const float *,const int *);
    dotnew modern=(dotnew)dlsym(RTLD_DEFAULT,"sdot$NEWLAPACK");CHECK(modern&&modern(&n,a,&one,b,&one)==70);
    int two=2, info=0, pivots[2], workn=64; double matrix[]={1,3,2,4}, work[64];
    dgetrf_(&two,&two,matrix,&two,pivots,&info);CHECK(info==0);
    dgetri_(&two,matrix,&two,pivots,work,&workn,&info);
    CHECK(info==0&&near(matrix[0],-2)&&near(matrix[1],1.5)&&near(matrix[2],1)&&near(matrix[3],-.5));
    puts("Accelerate bootstrap: PASS (vDSP arithmetic/FFT/biquad, vector sqrt, BLAS32/64 and legacy/new dot ABI, LAPACK LU/inverse)");
    return 0;
}
