/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_REGISTRY_H
#define SF_TB_REGISTRY_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SF_TB_REGISTRY_CAPACITY 256
struct sf_tb_registry_ops {
    void *ctx;
    void (*lock)(void *);
    void (*unlock)(void *);
    // Atomic, nonblocking reference operations. retain runs under lock and
    // cannot reenter. release runs outside the lock and may destroy/reenter.
    bool (*retain)(void *,void *device);
    void (*release)(void *,void *device);
};
struct sf_tb_member { void *device,*parent,*bus,*device_class;uint64_t serial; };
struct sf_tb_registry {
    struct sf_tb_registry_ops ops;
    struct sf_tb_member members[SF_TB_REGISTRY_CAPACITY];
    uint64_t serial;
    bool initialized;
};
struct sf_tb_registry_ref { void *device,*parent; };
enum sf_tb_match { SF_TB_MATCH_BUS,SF_TB_MATCH_PARENT,SF_TB_MATCH_CLASS };
struct sf_tb_registry_iter {
    enum sf_tb_match match;
    void *key;
    uint64_t watermark,cursor;
    bool reverse;
};
/* Metadata/reference ownership only; no probe, DMA, power or hardware ACK.
 * Zero-init then init once before concurrent use. Adds own device and parent
 * references. Iterators pin each returned device, skip removed records and
 * exclude registrations newer than begin's watermark. Call put for every
 * returned reference; begin/next retain nothing after a completed iteration.
 * A returned reference also pins the parent until after the device is put. */
int sf_tb_registry_init(struct sf_tb_registry *,const struct sf_tb_registry_ops *);
int sf_tb_registry_add(struct sf_tb_registry *,void *device,void *parent,void *bus,void *device_class);
int sf_tb_registry_remove(struct sf_tb_registry *,void *device);
int sf_tb_registry_begin(struct sf_tb_registry *,struct sf_tb_registry_iter *,enum sf_tb_match,void *key,void *start,bool reverse);
int sf_tb_registry_next(struct sf_tb_registry *,struct sf_tb_registry_iter *,struct sf_tb_registry_ref *);
void sf_tb_registry_put(struct sf_tb_registry *,struct sf_tb_registry_ref *);
#ifdef __cplusplus
}
#endif
#endif
