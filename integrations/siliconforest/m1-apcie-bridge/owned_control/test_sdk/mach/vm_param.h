#ifndef BCP_TEST_VM_PARAM_H
#define BCP_TEST_VM_PARAM_H
#include <cstdint>
extern uint32_t test_page_shift;
extern uint64_t page_size;
#define PAGE_SHIFT test_page_shift
#define PAGE_SIZE (UINT64_C(1)<<test_page_shift)
#endif
