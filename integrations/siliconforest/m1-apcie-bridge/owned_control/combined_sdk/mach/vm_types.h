/* TEST ONLY types; production uses the exact private KDK preincludes. */
#ifndef BOC_COMBINED_TEST_VM_TYPES_H
#define BOC_COMBINED_TEST_VM_TYPES_H
#include <cstdint>
struct test_pmap { uint64_t identity; };
using pmap_t=test_pmap *;
using ppnum_t=uint32_t;
using addr64_t=uint64_t;
#endif
