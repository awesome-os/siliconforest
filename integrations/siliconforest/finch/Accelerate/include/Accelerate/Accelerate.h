/* SPDX-License-Identifier: MIT */
/* Own declarations from Finch implementations; no Apple SDK metadata. */
#pragma once
#if !defined(SF_ACCELERATE_BUILD) && !defined(SF_COMPONENT_vDSP) && !defined(SF_COMPONENT_vMisc) && !defined(SF_COMPONENT_BLAS) && !defined(SF_COMPONENT_LAPACK) && !defined(SF_COMPONENT_vImage) && !defined(SF_COMPONENT_Quadrature) && !defined(SF_COMPONENT_LinearAlgebra) && !defined(SF_COMPONENT_SparseBLAS) && !defined(SF_COMPONENT_Sparse)
#define SF_COMPONENT_vDSP
#define SF_COMPONENT_vMisc
#define SF_COMPONENT_BLAS
#define SF_COMPONENT_LAPACK
#define SF_COMPONENT_vImage
#define SF_COMPONENT_Quadrature
#define SF_COMPONENT_LinearAlgebra
#define SF_COMPONENT_SparseBLAS
#define SF_COMPONENT_Sparse
#endif

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <Accelerate/DSP.h>
#include <Accelerate/Misc.h>
#if defined(SF_COMPONENT_LAPACK) || defined(SF_COMPONENT_BLAS)
typedef int __LAPACK_int;
#endif
#ifdef SF_COMPONENT_LinearAlgebra
#include <vecLib/LinearAlgebra/LinearAlgebra.h>
#endif
#ifdef SF_COMPONENT_vImage
#include <Accelerate/Image.h>
#endif
#ifdef SF_COMPONENT_Quadrature
#include <Accelerate/Quadrature.h>
#endif
#ifdef SF_COMPONENT_SparseBLAS
#include <vecLib/Sparse/BLAS.h>
#endif
#ifdef SF_COMPONENT_Sparse
#include <Accelerate/Sparse.h>
#endif
#ifndef SF_ACCELERATE_PREPROCESS
#ifdef SF_COMPONENT_BLAS
#include <Accelerate/BLAS-public.h>
#endif
#ifdef SF_COMPONENT_vDSP
#include <Accelerate/vDSP-generated.h>
#endif
#ifdef SF_COMPONENT_vMisc
#include <Accelerate/vMisc-generated.h>
#endif
#ifdef SF_COMPONENT_LAPACK
#include <Accelerate/LAPACK-generated.h>
#endif
#ifdef SF_COMPONENT_vImage
#include <Accelerate/vImage-generated.h>
#endif
#ifdef SF_COMPONENT_Quadrature
#include <Accelerate/Quadrature-generated.h>
#endif
#ifdef SF_COMPONENT_Sparse
#include <Accelerate/Sparse-generated.h>
#endif
#endif
