/* SPDX-License-Identifier: MIT */
/* Own declarations from public API descriptions and Linux-built native probes. */
#pragma once
#include <stddef.h>
typedef enum { QUADRATURE_INTEGRATE_QNG=0, QUADRATURE_INTEGRATE_QAG=1,
               QUADRATURE_INTEGRATE_QAGS=2 } quadrature_integrator;
typedef enum { QUADRATURE_SUCCESS=0, QUADRATURE_ERROR=-1, QUADRATURE_INVALID_ARG_ERROR=-2,
               QUADRATURE_ALLOC_ERROR=-3, QUADRATURE_INTERNAL_ERROR=-99,
               QUADRATURE_INTEGRATE_MAX_EVAL_ERROR=-101,
               QUADRATURE_INTEGRATE_BAD_BEHAVIOUR_ERROR=-102 } quadrature_status;
typedef void (*quadrature_function_array)(void *,size_t,const double *,double *);
typedef struct { quadrature_function_array fun; void *fun_arg; } quadrature_integrate_function;
typedef struct { quadrature_integrator integrator; double abs_tolerance,rel_tolerance;
                 size_t qag_points_per_interval,max_intervals; } quadrature_integrate_options;
#define QUADRATURE_INTEGRATE_QAG_WORKSPACE_PER_INTERVAL 32
/* Conservative workspace budget, two native-observed 76-byte QAGS slots.
 * This is our header's budget; no claim to have read Apple's macro value. */
#define QUADRATURE_INTEGRATE_QAGS_WORKSPACE_PER_INTERVAL 152
