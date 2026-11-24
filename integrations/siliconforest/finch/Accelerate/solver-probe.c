/* SPDX-License-Identifier: MIT */
/* Independent invariants for the actual sparse factor/iterative engines. */
#include <Accelerate/Accelerate.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"failed: %s\n",#x);return 1;} }while(0)
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    int row[]={0,1,1},col[]={0,0,1}; double data[]={4,1,3};
    SparseAttributes_t attributes={0};attributes.kind=SparseSymmetric;attributes.triangle=SparseLowerTriangle;
    SparseMatrix_Double a=SparseConvertFromCoordinate(2,2,3,1,attributes,row,col,data);
    CHECK(a.structure.attributes._allocatedBySparse && a.structure.columnStarts[2]==3);
    double x[]={1,2},y[]={0,0};
    SparseMultiply(a,(DenseVector_Double){2,x},(DenseVector_Double){2,y});
    CHECK(y[0]==6 && y[1]==7);
    const SparseFactorization_t methods[]={SparseFactorizationCholesky,SparseFactorizationLDLTTPP,SparseFactorizationQR,SparseFactorizationLU};
    int gr[]={0,1,0,1},gc[]={0,0,1,1}; double gv[]={4,1,1,3};
    SparseMatrix_Double general=SparseConvertFromCoordinate(2,2,4,1,(SparseAttributes_t){0},gr,gc,gv);
    double b[]={1,2};
    for(unsigned i=0;i<sizeof(methods)/sizeof(*methods);i++) {
        SparseOpaqueFactorization_Double f=SparseFactor(methods[i],methods[i]>=80 ? general : a);
        CHECK(f.status==SparseStatusOK);
        double result[2]={0};
        SparseSolve(f,(DenseVector_Double){2,b},(DenseVector_Double){2,result});
        CHECK(fabs(result[0]-1.0/11)<1e-10 && fabs(result[1]-7.0/11)<1e-10);
        SparseCleanup(f);
    }
    double result[2]={0};
    SparseIterativeStatus_t status=SparseSolve(SparseConjugateGradient(),a,(DenseVector_Double){2,b},(DenseVector_Double){2,result});
    CHECK(status==SparseIterativeConverged && fabs(result[0]-1.0/11)<1e-8 && fabs(result[1]-7.0/11)<1e-8);
    SparseCleanup(general);
    SparseCleanup(a);
    puts("PASS Sparse coordinate/multiply, Cholesky/LDLT/QR/LU, CG, ownership");
    return 0;
}
