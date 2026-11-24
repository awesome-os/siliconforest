/* SPDX-License-Identifier: MIT */
/* Independent real/complex SparseBLAS vectors and lifecycle checks. */
#include <Accelerate/Accelerate.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"failed: %s\n",#x);return 1;} }while(0)
int main(void) {
    sparse_matrix_double a=sparse_matrix_create_double(2,2);CHECK(a);
    CHECK(sparse_set_matrix_property(a,SPARSE_UPPER_TRIANGULAR)==SPARSE_SUCCESS);
    CHECK(sparse_insert_entry_double(a,2,0,0)==SPARSE_SUCCESS);
    CHECK(sparse_insert_entry_double(a,3,0,1)==SPARSE_SUCCESS);
    CHECK(sparse_insert_entry_double(a,4,1,1)==SPARSE_SUCCESS);
    CHECK(sparse_commit(a)==SPARSE_SUCCESS);
    CHECK(sparse_get_matrix_number_of_rows(a)==2 && sparse_get_matrix_number_of_columns(a)==2);
    CHECK(sparse_get_matrix_nonzero_count(a)==3);
    double x[]={1,2},y[2]={0};
    CHECK(sparse_matrix_vector_product_dense_double(CblasNoTrans,1,a,x,1,y,1)==SPARSE_SUCCESS);
    CHECK(y[0]==8 && y[1]==8);
    CHECK(sparse_operator_norm_double(a,SPARSE_NORM_ONE)==7);
    CHECK(sparse_operator_norm_double(a,SPARSE_NORM_INF)==5);
    CHECK(sparse_vector_triangular_solve_dense_double(CblasNoTrans,1,a,y,1)==SPARSE_SUCCESS);
    CHECK(fabs(y[0]-1)<1e-12 && fabs(y[1]-2)<1e-12);
    CHECK(sparse_matrix_destroy(a)==SPARSE_SUCCESS);
    double _Complex c[]={1+2*I,3-I},d[]={2-I,4+I},z[2]={0};
    sparse_matrix_double_complex b=sparse_matrix_create_double_complex(2,2);CHECK(b);
    CHECK(sparse_insert_entry_double_complex(b,c[0],0,0)==SPARSE_SUCCESS);
    CHECK(sparse_insert_entry_double_complex(b,c[1],1,1)==SPARSE_SUCCESS);
    CHECK(sparse_commit(b)==SPARSE_SUCCESS);
    CHECK(sparse_matrix_vector_product_dense_double_complex(CblasNoTrans,1,b,d,1,z,1)==SPARSE_SUCCESS);
    CHECK(cabs(z[0]-c[0]*d[0])<1e-12 && cabs(z[1]-c[1]*d[1])<1e-12);
    /* Sparse BLAS adds into the output vector. Isolate this product. */
    z[0]=z[1]=0;
    CHECK(sparse_matrix_vector_product_dense_double_complex(CblasConjTrans,1,b,d,1,z,1)==SPARSE_SUCCESS);
    CHECK(cabs(z[0]-conj(c[0])*d[0])<1e-12 && cabs(z[1]-conj(c[1])*d[1])<1e-12);
    CHECK(sparse_matrix_destroy(b)==SPARSE_SUCCESS);
    puts("PASS SparseBLAS real/complex product, norms, triangular solve, lifecycle");
    return 0;
}
