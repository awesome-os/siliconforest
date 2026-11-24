#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Our public Sparse glue over Finch's actual typed engine; no SDK body input.

Public signatures/structure facts come from Apple API documentation. Options,
allocation, ownership and dispatch below are authored here. The generated header
is also compiled by Finch's overloads.c into out-of-line C++-mangled entry points.
"""
from pathlib import Path
import sys
out=['/* SPDX-License-Identifier: MIT */','/* Generated from our sparse-overloads.py, never SDK inline bodies. */','#ifndef SF_ACCELERATE_PREPROCESS']
def emit(ret,name,args,body,overload=True):
    attribute='__attribute__((overloadable)) ' if overload else ''
    out.append('#ifdef SF_ACCELERATE_BUILD')
    out.append('static inline '+attribute+f'{ret} {name}({args}) {{\n    '+body+'\n}')
    if not name.startswith('sf_sparse_'):
        out.append('#else\n'+attribute+f'{ret} {name}({args});')
    out.append('#endif')
emit('void','sf_sparse_error','const char *message','fputs(message,stderr);',False)
emit('SparseSymbolicFactorOptions','sf_sparse_options','SparseSymbolicFactorOptions options','if(!options.malloc) options.malloc=malloc; if(!options.free) options.free=free; if(!options.reportError) options.reportError=sf_sparse_error; return options;',False)
for n,member,kind in [('SparseConjugateGradient','cg',0),('SparseGMRES','gmres',1),('SparseLSMR','lsmr',2)]:
    opt={'cg':'SparseCGOptions','gmres':'SparseGMRESOptions','lsmr':'SparseLSMROptions'}[member]
    emit('SparseIterativeMethod',n,opt+' options',f'SparseIterativeMethod result={{0}}; result.method={kind}; result.options.{member}=options; return result;')
    emit('SparseIterativeMethod',n,'void',f'return {n}(({opt}){{0}});')
emit('SparseOpaqueSymbolicFactorization','SparseRetain','SparseOpaqueSymbolicFactorization factor','_SparseRetainSymbolic(&factor); return factor;')
emit('void','SparseCleanup','SparseOpaqueSymbolicFactorization factor','_SparseDestroyOpaqueSymbolic(&factor);')
emit('SparseOpaqueSymbolicFactorization','SparseFactor','SparseFactorization_t type, SparseMatrixStructure matrix, SparseSymbolicFactorOptions options','options=sf_sparse_options(options); if(type>=80) return _SparseSymbolicFactorLU(type,&matrix,&options); if(type>=40) return _SparseSymbolicFactorQR(type,&matrix,&options); return _SparseSymbolicFactorSymmetric(type,&matrix,&options);')
emit('SparseOpaqueSymbolicFactorization','SparseFactor','SparseFactorization_t type, SparseMatrixStructure matrix','return SparseFactor(type,matrix,(SparseSymbolicFactorOptions){0});')
for S,T,AT,ST,OLD in [('Float','float','SparseAttributes_t','SparseMatrixStructure','float'),('Double','double','SparseAttributes_t','SparseMatrixStructure','double'),('Complex_Float','float _Complex','SparseAttributesComplex_t','SparseMatrixStructureComplex','float_complex'),('Complex_Double','double _Complex','SparseAttributesComplex_t','SparseMatrixStructureComplex','double_complex')]:
    A='SparseMatrix_'+S; D='DenseMatrix_'+S; V='DenseVector_'+S; F='SparseOpaqueFactorization_'+S; U='SparseOpaqueSubfactor_'+S; P='SparseOpaquePreconditioner_'+S
    emit(D,'sf_sparse_dense',V+' v',f'return ({D}){{v.count,1,v.count,{{0}},v.data}};')
    params=f'int m,int n,long count,uint8_t blockSize,{AT} attributes,const int *rows,const int *cols,const {T} *data'
    emit(A,'SparseConvertFromCoordinate',params+',void *storage,void *workspace',f'return _SparseConvertFromCoordinate_{S}(m,n,count,blockSize,attributes,rows,cols,data,(char *)storage,(int *)workspace);')
    emit(A,'SparseConvertFromCoordinate',params,f'''if(m<0 || n<0 || count<0 || !blockSize) abort();
    size_t blocks=(size_t)count, blockElements=(size_t)blockSize*blockSize;
    if(blocks>(SIZE_MAX-48-((size_t)n+1)*sizeof(long))/(sizeof(int)+blockElements*sizeof({T}))) abort();
    size_t bytes=48+((size_t)n+1)*sizeof(long)+blocks*(sizeof(int)+blockElements*sizeof({T}));
    char *storage=malloc(bytes); int *workspace=malloc((size_t)(m ? m : 1)*sizeof(int));
    if(!storage || !workspace) abort(); attributes._allocatedBySparse=1;
    {A} result=_SparseConvertFromCoordinate_{S}(m,n,count,blockSize,attributes,rows,cols,data,storage,workspace);
    free(workspace); return result;''')
    emit(A,'SparseConvertFromOpaque','sparse_matrix_'+OLD+' matrix',f'return _SparseConvertFromOpaque_{S}(matrix);')
    emit('void','SparseCleanup',A+' matrix','if(matrix.structure.attributes._allocatedBySparse) free(matrix.structure.columnStarts);')
    for typ,attr in [(A,'structure.attributes'),(D,'attributes'),(F,'attributes'),(U,'attributes')]:
        emit(typ,'SparseGetTranspose',typ+' matrix',f'matrix.{attr}.transpose ^= 1; return matrix;')
        if S.startswith('Complex_'):
            emit(typ,'SparseGetConjugateTranspose',typ+' matrix',f'matrix.{attr}.transpose ^= 1; matrix.{attr}.conjugate_transpose ^= 1; return matrix;')
    for typ in [D,V]:
        xm='sf_sparse_dense(x)' if typ==V else 'x';ym='sf_sparse_dense(y)' if typ==V else 'y'
        for name,acc in [('SparseMultiply','false'),('SparseMultiplyAdd','true')]:
            emit('void',name,f'{T} alpha,{A} matrix,{typ} x,{typ} y',f'_SparseSpMV_{S}(alpha,matrix,{xm},{acc},{ym});')
            emit('void',name,f'{A} matrix,{typ} x,{typ} y',f'_SparseSpMV_{S}(1,matrix,{xm},{acc},{ym});')
    emit(F,'SparseRetain',F+' factor',f'_SparseRetainNumeric_{S}(&factor); return factor;')
    emit('void','SparseCleanup',F+' factor',f'_SparseDestroyOpaqueNumeric_{S}(&factor);')
    emit('void','SparseCleanup',P+' preconditioner',f'_SparseReleaseOpaquePreconditioner_{S}(&preconditioner);')
    emit(P,'SparseCreatePreconditioner',f'SparsePreconditioner_t type,{A} matrix',f'return _SparseCreatePreconditioner_{S}(type,&matrix);')
    emit(F,'SparseFactor',f'SparseFactorization_t type,{A} matrix,SparseSymbolicFactorOptions sfoptions,SparseNumericFactorOptions nfoptions',f'''sfoptions=sf_sparse_options(sfoptions); if(type>=80) return _SparseFactorLU_{S}(type,&matrix,&sfoptions,&nfoptions);
    if(type>=40) return _SparseFactorQR_{S}(type,&matrix,&sfoptions,&nfoptions);
    return _SparseFactorSymmetric_{S}(type,&matrix,&sfoptions,&nfoptions);''')
    emit(F,'SparseFactor',f'SparseFactorization_t type,{A} matrix',f'return SparseFactor(type,matrix,(SparseSymbolicFactorOptions){{0}},(SparseNumericFactorOptions){{0}});')
    emit(F,'SparseFactor',f'SparseOpaqueSymbolicFactorization symbolic,{A} matrix,SparseNumericFactorOptions options',f'''if(symbolic.type>=80) return _SparseNumericFactorLU_{S}(&symbolic,&matrix,&options,NULL,NULL);
    if(symbolic.type>=40) return _SparseNumericFactorQR_{S}(&symbolic,&matrix,&options,NULL,NULL);
    return _SparseNumericFactorSymmetric_{S}(&symbolic,&matrix,&options,NULL,NULL);''')
    emit(F,'SparseFactor',f'SparseOpaqueSymbolicFactorization symbolic,{A} matrix','return SparseFactor(symbolic,matrix,(SparseNumericFactorOptions){0});')
    emit('void','SparseRefactor',f'{A} matrix,{F} factor,SparseNumericFactorOptions options',f'''if(factor.symbolicFactorization.type>=80) _SparseRefactorLU_{S}(&matrix,&factor,&options,NULL);
    else if(factor.symbolicFactorization.type>=40) _SparseRefactorQR_{S}(&matrix,&factor,&options,NULL);
    else _SparseRefactorSymmetric_{S}(&matrix,&factor,&options,NULL);''')
    emit('void','SparseRefactor',f'{A} matrix,{F} factor','SparseRefactor(matrix,factor,(SparseNumericFactorOptions){0});')
    emit(U,'SparseCreateSubfactor',f'SparseSubfactor_t contents,{F} factor',f'{U} sub={{0}}; sub.factor=SparseRetain(factor); sub.contents=contents; _SparseGetWorkspaceRequired_{S}(contents,factor,&sub.workspaceRequiredStatic,&sub.workspaceRequiredPerRHS); return sub;')
    emit(U,'SparseRetain',U+' subfactor','subfactor.factor=SparseRetain(subfactor.factor); return subfactor;')
    emit('void','SparseCleanup',U+' subfactor','SparseCleanup(subfactor.factor);')
    out.append(f'int __attribute__((overloadable)) SparseGetInertia({F},int *,int *,int *);')
    for typ in [D,V]:
        pre=f'{D} b0=sf_sparse_dense(b),x0=sf_sparse_dense(x);' if typ==V else f'{D} b0=b,x0=x;'
        for factorType,private in [(F,'_SparseSolveOpaque_'),(U,'_SparseSolveSubfactor_')]:
            emit('void','SparseSolve',f'{factorType} factor,{typ} b,{typ} x,void *workspace',pre+f' {private}{S}(&factor,&b0,&x0,workspace);')
            emit('void','SparseSolve',f'{factorType} factor,{typ} b,{typ} x','SparseSolve(factor,b,x,NULL);')
            preIn=f'{D} x0=sf_sparse_dense(x);' if typ==V else f'{D} x0=x;'
            emit('void','SparseSolve',f'{factorType} factor,{typ} x',preIn+f' {private}{S}(&factor,NULL,&x0,NULL);')
        emit('void','SparseMultiply',f'{U} factor,{typ} x,{typ} y',pre.replace('b0=sf_sparse_dense(b)','b0=sf_sparse_dense(y)').replace('b0=b','b0=y')+f' _SparseMultiplySubfactor_{S}(&factor,&x0,&b0,NULL);')
    # Block operators use dense matrices internally; vector overloads adapt the block too.
    for typ in [D,V]:
        block=f'void (^operation)(bool,enum CBLAS_TRANSPOSE,{typ},{typ})'
        if typ==V:
            adapt=f'void (^op)(bool,enum CBLAS_TRANSPOSE,{D},{D})=^(bool acc,enum CBLAS_TRANSPOSE trans,{D} x1,{D} y1){{ operation(acc,trans,({V}){{x1.rowCount,x1.data}},({V}){{y1.rowCount,y1.data}}); }};'
            pre=f'{D} b0=sf_sparse_dense(b),x0=sf_sparse_dense(x); '+adapt
        else:pre=f'{D} b0=b,x0=x; void (^op)(bool,enum CBLAS_TRANSPOSE,{D},{D})=operation;'
        emit('SparseIterativeStatus_t','SparseSolve',f'SparseIterativeMethod method,{block},{typ} b,{typ} x,{P} preconditioner',pre+f''' if(method.method==0) return _SparseCGSolve_{S}(&method.options.cg,&x0,&b0,op,&preconditioner);
    if(method.method==1) return _SparseGMRESSolve_{S}(&method.options.gmres,&x0,&b0,op,&preconditioner);
    if(method.method==2) return _SparseLSMRSolve_{S}(&method.options.lsmr,&x0,&b0,op,&preconditioner);
    return SparseIterativeParameterError;''')
        emit('SparseIterativeStatus_t','SparseSolve',f'SparseIterativeMethod method,{block},{typ} b,{typ} x',f'return SparseSolve(method,operation,b,x,({P}){{0}});')
        # Adapt a sparse matrix to the exact operator callback expected by the iterative engine.
        tchange='matrix.structure.attributes.conjugate_transpose ^= trans==CblasConjTrans;' if S.startswith('Complex_') else ''
        dmX='sf_sparse_dense(x1)' if typ==V else 'x1';dmY='sf_sparse_dense(y1)' if typ==V else 'y1'
        opbody=f'{A} matrix=a; matrix.structure.attributes.transpose ^= trans!=CblasNoTrans; {tchange} _SparseSpMV_{S}(1,matrix,{dmX},acc,{dmY});'
        operation=f'^ (bool acc,enum CBLAS_TRANSPOSE trans,{typ} x1,{typ} y1){{ {opbody} }}'
        emit('SparseIterativeStatus_t','SparseSolve',f'SparseIterativeMethod method,{A} a,{typ} b,{typ} x,{P} preconditioner',f'return SparseSolve(method,{operation},b,x,preconditioner);')
        emit('SparseIterativeStatus_t','SparseSolve',f'SparseIterativeMethod method,{A} a,{typ} b,{typ} x,SparsePreconditioner_t type',f'{P} p=_SparseCreatePreconditioner_{S}(type,&a); SparseIterativeStatus_t result=SparseSolve(method,{operation},b,x,p); _SparseReleaseOpaquePreconditioner_{S}(&p); return result;')
        emit('SparseIterativeStatus_t','SparseSolve',f'SparseIterativeMethod method,{A} a,{typ} b,{typ} x','return SparseSolve(method,a,b,x,SparsePreconditionerNone);')
    block=f'void (^operation)(bool,enum CBLAS_TRANSPOSE,{D},{D})'
    emit('void','SparseIterate',f'SparseIterativeMethod method,int iteration,const bool *converged,void *state,{block},{D} b,{D} r,{D} x,{P} preconditioner',f'''if(method.method==0) _SparseCGIterate_{S}(&method.options.cg,iteration,(char *)state,converged,&x,&b,&r,&preconditioner,operation);
    else if(method.method==1) _SparseGMRESIterate_{S}(&method.options.gmres,iteration,(char *)state,converged,&x,&b,&r,&preconditioner,operation);
    else if(method.method==2) _SparseLSMRIterate_{S}(&method.options.lsmr,iteration,(char *)state,converged,&x,&b,&r,&preconditioner,operation);''')
    emit('void','SparseIterate',f'SparseIterativeMethod method,int iteration,const bool *converged,void *state,{block},{D} b,{D} r,{D} x',f'SparseIterate(method,iteration,converged,state,operation,b,r,x,({P}){{0}});')
    emit('size_t','SparseGetStateSize_'+S,'SparseIterativeMethod method,bool preconditioner,int m,int n,int nrhs',f'return _SparseGetIterativeStateSize_{S}(&method,preconditioner,m,n,nrhs);')
out.append('#endif')
Path(sys.argv[1]).write_text('\n'.join(out)+'\n')
