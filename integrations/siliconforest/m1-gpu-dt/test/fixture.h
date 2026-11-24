/* SPDX-License-Identifier: MIT */
#ifndef M1_GPU_DT_FIXTURE_H
#define M1_GPU_DT_FIXTURE_H

struct fixture_prop {
    const char *path;
    const char *name;
    const unsigned char *value;
    unsigned len;
};

extern const struct fixture_prop fixture_props[];
extern const unsigned fixture_nprops;

#endif
