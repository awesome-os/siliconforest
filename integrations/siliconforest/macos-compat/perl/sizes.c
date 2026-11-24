/* SPDX-License-Identifier: MIT */
/* The type sizes hints-darwin gives perl-cross, asserted with the compiler that builds Perl. */
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define SIZE(type, n) _Static_assert(sizeof(type) == (n), #type)
SIZE(char, 1); SIZE(short, 2); SIZE(int, 4); SIZE(long, 8); SIZE(long long, 8);
SIZE(double, 8); SIZE(long double, 8); SIZE(void *, 8);
SIZE(size_t, 8); SIZE(fpos_t, 8); SIZE(off_t, 8); SIZE(uid_t, 4); SIZE(gid_t, 4); SIZE(time_t, 8);
SIZE(((struct stat *)0)->st_ino, 8);
_Static_assert((uid_t)-1 > 0 && (gid_t)-1 > 0 && (ino_t)-1 > 0, "uid_t, gid_t, ino_t are unsigned");
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "little endian");
