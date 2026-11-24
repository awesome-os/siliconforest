#ifndef BCP_TEST_IOLOCKS_H
#define BCP_TEST_IOLOCKS_H
#include <cstdint>
struct lck_mtx_t {uint64_t words[2];};
struct IOLock;
extern "C" lck_mtx_t *IOLockGetMachLock(IOLock *);
#endif
