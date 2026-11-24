/* SPDX-License-Identifier: MIT */
/* Native/port API dispatch observations, independent small nonsingular systems. */
#include <Accelerate/Accelerate.h>
#include <stdio.h>
#include <math.h>
static unsigned errors;
static void error(const char *text){(void)text;errors++;}
static unsigned applies;
static void diagonal(void *memory,enum CBLAS_TRANSPOSE transpose,DenseMatrix_Double input,DenseMatrix_Double output){
 (void)transpose;double *d=memory;applies++;
 for(int col=0;col<input.columnCount;col++)for(int row=0;row<input.rowCount;row++)output.data[col*output.columnStride+row]=input.data[col*input.columnStride+row]/d[row];
}
int main(void){
 setvbuf(stdout,NULL,_IOLBF,0);
 for(unsigned symmetric=0;symmetric<2;symmetric++){
  long starts[]={0,2,4};int rows[]={0,1,0,1};double values[]={4,1,1,3};
  SparseAttributes_t a={0};a.kind=symmetric?SparseSymmetric:0;a.triangle=SparseLowerTriangle;
  long lowerStarts[]={0,2,3};int lowerRows[]={0,1,1};double lowerValues[]={4,1,3};
  SparseMatrix_Double matrix={{2,2,symmetric?lowerStarts:starts,symmetric?lowerRows:rows,a,1},symmetric?lowerValues:values};
  for(int type=0;type<=3;type++){

   SparseOpaquePreconditioner_Double p={0};
   double diag[]={4,3};
   if(type==1)p=(SparseOpaquePreconditioner_Double){SparsePreconditionerUser,diag,diagonal};
   if(type>=2)p=SparseCreatePreconditioner(type,matrix);
   printf("PREC symmetric=%u requested=%d returned=%d apply=%d memory=%d\n",symmetric,type,p.type,p.apply!=NULL,p.mem!=NULL);
   for(int variant=0;variant<=2;variant++){
    double b[]={1,2},x[]={0,0};errors=0;
    SparseIterativeMethod method=SparseGMRES((SparseGMRESOptions){.reportError=error,.variant=variant,.maxIterations=20});
    int status=0;
    if(type!=1){
     status=SparseSolve(method,matrix,(DenseVector_Double){2,b},(DenseVector_Double){2,x},type);
     printf("SOLVE symmetric=%u type=%d variant=%d status=%d errors=%u x=%.8f,%.8f\n",symmetric,type,variant,status,errors,x[0],x[1]);
    }
    if(type!=0){
     x[0]=x[1]=0;errors=0;applies=0;
     status=SparseSolve(method,matrix,(DenseVector_Double){2,b},(DenseVector_Double){2,x},p);
     printf("OPAQUE symmetric=%u type=%d variant=%d status=%d errors=%u x=%.8f,%.8f\n",symmetric,type,variant,status,errors,x[0],x[1]);
    }
   }
   if(type>=2)SparseCleanup(p);
  }
 }
 return 0;
}
