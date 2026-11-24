// SPDX-License-Identifier: MIT
// Accelerate.framework: the single-precision vDSP vector routines Firefox's on-device
// inference library imports (libmozinference: llama.cpp's use of Accelerate), as plain loops
// with vDSP's argument order and stride semantics. Nothing else of Accelerate is here.
#include <stddef.h>
#include <math.h>

typedef unsigned long vDSP_Length;
typedef long vDSP_Stride;

#define EXPORT __attribute__((visibility("default")))

// C[n] = A[n] + B[n]
EXPORT void vDSP_vadd(const float *A, vDSP_Stride IA, const float *B, vDSP_Stride IB, float *C, vDSP_Stride IC, vDSP_Length N)
{
    for (vDSP_Length n = 0; n < N; n++)
        C[n * IC] = A[n * IA] + B[n * IB];
}

// C[n] = A[n] - B[n]; vDSP names the subtrahend first
EXPORT void vDSP_vsub(const float *B, vDSP_Stride IB, const float *A, vDSP_Stride IA, float *C, vDSP_Stride IC, vDSP_Length N)
{
    for (vDSP_Length n = 0; n < N; n++)
        C[n * IC] = A[n * IA] - B[n * IB];
}

// C[n] = A[n] * B[n]
EXPORT void vDSP_vmul(const float *A, vDSP_Stride IA, const float *B, vDSP_Stride IB, float *C, vDSP_Stride IC, vDSP_Length N)
{
    for (vDSP_Length n = 0; n < N; n++)
        C[n * IC] = A[n * IA] * B[n * IB];
}

// C[n] = A[n] / B[n]; vDSP names the divisor first
EXPORT void vDSP_vdiv(const float *B, vDSP_Stride IB, const float *A, vDSP_Stride IA, float *C, vDSP_Stride IC, vDSP_Length N)
{
    for (vDSP_Length n = 0; n < N; n++)
        C[n * IC] = A[n * IA] / B[n * IB];
}

// C[n] = A[n] + *B
EXPORT void vDSP_vsadd(const float *A, vDSP_Stride IA, const float *B, float *C, vDSP_Stride IC, vDSP_Length N)
{
    for (vDSP_Length n = 0; n < N; n++)
        C[n * IC] = A[n * IA] + *B;
}

// C[n] = A[n] * *B
EXPORT void vDSP_vsmul(const float *A, vDSP_Stride IA, const float *B, float *C, vDSP_Stride IC, vDSP_Length N)
{
    for (vDSP_Length n = 0; n < N; n++)
        C[n * IC] = A[n * IA] * *B;
}

// D[n] = A[n] * *B + *C
EXPORT void vDSP_vsmsa(const float *A, vDSP_Stride IA, const float *B, const float *C, float *D, vDSP_Stride ID, vDSP_Length N)
{
    for (vDSP_Length n = 0; n < N; n++)
        D[n * ID] = A[n * IA] * *B + *C;
}

// *C = sum of A
EXPORT void vDSP_sve(const float *A, vDSP_Stride IA, float *C, vDSP_Length N)
{
    float sum = 0;
    for (vDSP_Length n = 0; n < N; n++)
        sum += A[n * IA];
    *C = sum;
}

// *C = maximum of A, -infinity for an empty vector
EXPORT void vDSP_maxv(const float *A, vDSP_Stride IA, float *C, vDSP_Length N)
{
    float max = -INFINITY;
    for (vDSP_Length n = 0; n < N; n++)
        if (A[n * IA] > max)
            max = A[n * IA];
    *C = max;
}

// *C = mean of the squares of A, NaN for an empty vector as 0/0 is
EXPORT void vDSP_measqv(const float *A, vDSP_Stride IA, float *C, vDSP_Length N)
{
    float sum = 0;
    for (vDSP_Length n = 0; n < N; n++)
        sum += A[n * IA] * A[n * IA];
    *C = sum / (float)N;
}
