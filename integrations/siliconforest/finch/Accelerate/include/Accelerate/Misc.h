/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
typedef float vFloat __attribute__((ext_vector_type(4)));
typedef int8_t vSInt8 __attribute__((ext_vector_type(16)));
typedef int16_t vSInt16 __attribute__((ext_vector_type(8)));
typedef int32_t vSInt32 __attribute__((ext_vector_type(4)));
typedef uint8_t vUInt8 __attribute__((ext_vector_type(16)));
typedef uint16_t vUInt16 __attribute__((ext_vector_type(8)));
typedef uint32_t vUInt32 __attribute__((ext_vector_type(4)));
typedef union { uint32_t words[8]; vUInt32 vectors[2]; } vU256;
typedef union { uint32_t words[16]; vUInt32 vectors[4]; } vU512;
typedef union { uint32_t words[32]; vUInt32 vectors[8]; } vU1024;
typedef vU256 vS256;
typedef vU512 vS512;
typedef vU1024 vS1024;
typedef union { uint32_t words[4]; uint32_t v[4]; vUInt32 vector; } vU128;
typedef vU128 vS128;
typedef float _Complex __float_complex_t;
typedef double _Complex __double_complex_t;
