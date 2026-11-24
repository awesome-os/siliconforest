/* SPDX-License-Identifier: MIT */
/* Own arm64 declarations for the types used by Finch vDSP. */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef unsigned long vDSP_Length;
typedef long vDSP_Stride;
typedef struct { float real, imag; } DSPComplex;
typedef struct { double real, imag; } DSPDoubleComplex;
typedef struct { float *realp, *imagp; } DSPSplitComplex;
typedef struct { double *realp, *imagp; } DSPDoubleSplitComplex;
typedef int FFTDirection, FFTRadix, vDSP_DFT_Direction, vDSP_DCT_Type, vDSP_DFT_RealtoComplex;
enum { FFT_FORWARD=1, FFT_INVERSE=-1, kFFTRadix2=0, kFFTRadix3=1, kFFTRadix5=2,
 vDSP_DFT_FORWARD=1, vDSP_DFT_INVERSE=-1, vDSP_DCT_II=2, vDSP_DCT_III=3, vDSP_DCT_IV=4,
 vDSP_DFT_Interleaved_ComplextoComplex=0, vDSP_DFT_Interleaved_RealtoComplex=1,
 vDSP_HANN_NORM=1, vDSP_HALF_WINDOW=2 };
typedef struct OpaqueFFTSetup *FFTSetup;
typedef struct OpaqueFFTSetupD *FFTSetupD;
typedef struct vDSP_biquad_SetupStruct *vDSP_biquad_Setup;
typedef struct vDSP_biquad_SetupStructD *vDSP_biquad_SetupD;
typedef struct vDSP_biquadm_SetupStruct *vDSP_biquadm_Setup;
typedef struct vDSP_biquadm_SetupStructD *vDSP_biquadm_SetupD;
typedef struct vDSP_DFT_SetupStruct *vDSP_DFT_Setup;
typedef struct vDSP_DFT_SetupStructD *vDSP_DFT_SetupD;
typedef struct OpaqueDFTInterleaved *vDSP_DFT_Interleaved_Setup;
typedef struct OpaqueDFTInterleavedD *vDSP_DFT_Interleaved_SetupD;
typedef struct { uint8_t bytes[3]; } vDSP_int24, vDSP_uint24;
