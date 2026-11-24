/* SPDX-License-Identifier: MIT */
/* Own Sparse declarations using public structure/width facts; no SDK input. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <complex.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <vecLib/Sparse/BLAS.h>
#include <Accelerate/Sparse-constants.h>
typedef unsigned SparseKind_t;
typedef uint8_t SparseTriangle_t,SparseFactorization_t,SparseOrder_t,SparseScaling_t,SparseSubfactor_t,SparseGMRESVariant_t;
typedef uint32_t SparseControl_t;
typedef int SparseStatus_t,SparsePreconditioner_t,SparseIterativeStatus_t,SparseLSMRConvergenceTest_t;
typedef struct { unsigned transpose:1,triangle:1,kind:2,_reserved:11,_allocatedBySparse:1; } SparseAttributes_t;
typedef struct { unsigned transpose:1,triangle:1,kind:3,conjugate_transpose:1,_reserved:9,_allocatedBySparse:1; } SparseAttributesComplex_t;
_Static_assert(sizeof(SparseAttributes_t)==4,"Sparse attribute ABI");
_Static_assert(sizeof(SparseAttributesComplex_t)==4,"Sparse complex attribute ABI");
typedef struct { int rowCount,columnCount; long *columnStarts; int *rowIndices; SparseAttributes_t attributes; uint8_t blockSize; } SparseMatrixStructure;
typedef struct { int rowCount,columnCount; long *columnStarts; int *rowIndices; SparseAttributesComplex_t attributes; uint8_t blockSize; } SparseMatrixStructureComplex;
typedef struct {
    SparseControl_t control; SparseOrder_t orderMethod; int *order,*ignoreRowsAndColumns;
    void *(*malloc)(size_t); void (*free)(void *); void (*reportError)(const char *);
} SparseSymbolicFactorOptions;
typedef struct { SparseControl_t control; SparseScaling_t scalingMethod; void *scaling; double pivotTolerance,zeroTolerance; } SparseNumericFactorOptions;
typedef struct {
    SparseStatus_t status; int rowCount,columnCount; SparseAttributes_t attributes;
    uint8_t blockSize; SparseFactorization_t type; void *factorization;
    size_t workspaceSize_Float,workspaceSize_Double,factorSize_Float,factorSize_Double;
} SparseOpaqueSymbolicFactorization;
#define SF_SPARSE_TYPES(S,T,A,ST) \
 typedef struct { ST structure; T *data; } SparseMatrix_##S; \
 typedef struct { int count; T *data; } DenseVector_##S; \
 typedef struct { int rowCount,columnCount,columnStride; A attributes; T *data; } DenseMatrix_##S; \
 typedef struct { SparseStatus_t status; A attributes; SparseOpaqueSymbolicFactorization symbolicFactorization; bool userFactorStorage; void *numericFactorization; size_t solveWorkspaceRequiredStatic,solveWorkspaceRequiredPerRHS; } SparseOpaqueFactorization_##S; \
 typedef struct { A attributes; SparseSubfactor_t contents; SparseOpaqueFactorization_##S factor; size_t workspaceRequiredStatic,workspaceRequiredPerRHS; } SparseOpaqueSubfactor_##S; \
 typedef struct { SparsePreconditioner_t type; void *mem; void (*apply)(void *,enum CBLAS_TRANSPOSE,DenseMatrix_##S,DenseMatrix_##S); } SparseOpaquePreconditioner_##S;
SF_SPARSE_TYPES(Float,float,SparseAttributes_t,SparseMatrixStructure)
SF_SPARSE_TYPES(Double,double,SparseAttributes_t,SparseMatrixStructure)
SF_SPARSE_TYPES(Complex_Float,float _Complex,SparseAttributesComplex_t,SparseMatrixStructureComplex)
SF_SPARSE_TYPES(Complex_Double,double _Complex,SparseAttributesComplex_t,SparseMatrixStructureComplex)
#undef SF_SPARSE_TYPES
typedef struct { void (*reportError)(const char *); } _SparseIterativeMethodBaseOptions;
typedef struct { void (*reportError)(const char *); int maxIterations; double atol,rtol; void (*reportStatus)(const char *); } SparseCGOptions;
typedef struct { void (*reportError)(const char *); SparseGMRESVariant_t variant; int nvec,maxIterations; double atol,rtol; void (*reportStatus)(const char *); } SparseGMRESOptions;
typedef struct { void (*reportError)(const char *); double lambda; int nvec; SparseLSMRConvergenceTest_t convergenceTest; double atol,rtol,btol,conditionLimit; int maxIterations; void (*reportStatus)(const char *); } SparseLSMROptions;
typedef struct { int method; union { _SparseIterativeMethodBaseOptions base; SparseCGOptions cg; SparseGMRESOptions gmres; SparseLSMROptions lsmr; char padding[256]; } options; } SparseIterativeMethod;
#ifndef SF_ACCELERATE_PREPROCESS
#include <Accelerate/Sparse-generated.h>
#ifndef SPARSE_SOLVE_HEADER
#include <vecLib/Sparse/Solve.h>
#endif
#endif
