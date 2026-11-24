/* SPDX-License-Identifier: MIT */
#ifndef SF_TB_CM_HELPERS_H
#define SF_TB_CM_HELPERS_H
#include <linux/random.h>
#include <linux/property.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/unaligned.h>
#include <linux/idr.h>
#include "sf_tb_ida_range.h"
/* Repair only this private CM's inclusive range ABI. Never pass end==start
 * to the native exclusive-end backend (fixed PCI HopID8 is a real caller). */
static inline int sf_tb_ida_alloc_range(struct ida *ida,unsigned int minimum,
                                      unsigned int maximum,gfp_t flags)
{
 uint32_t end=0;int ret=sf_tb_ida_exclusive_end(minimum,maximum,&end);
 return ret?ret:ida_simple_get(ida,minimum,end,flags);
}
#define ida_alloc_range(ida,minimum,maximum,flags) sf_tb_ida_alloc_range(ida,minimum,maximum,flags)
#define ida_alloc_max(ida,maximum,flags) sf_tb_ida_alloc_range(ida,0,maximum,flags)
/* Typed copies preserve the native host byte order without unaligned loads. */
#ifndef get_unaligned
#define get_unaligned(p) ({ __typeof__(*(p)) v;memcpy((void *)&v,(p),sizeof(v));v; })
#define put_unaligned(value,p) do { __typeof__(*(p)) v=(value);memcpy((p),&v,sizeof(v)); } while(0)
#endif
#ifndef get_random_u32
static inline u32 sf_tb_random_u32(void) { u32 v;get_random_bytes(&v,sizeof(v));return v; }
#define get_random_u32 sf_tb_random_u32
#endif
/* The function-like completion macro otherwise also expands cm_ops->complete(). */
#undef complete
static inline void complete(struct completion *c) { linux_complete_common(c,0); }
#undef alloc_ordered_workqueue
static inline struct workqueue_struct *sf_tb_ordered_workqueue(unsigned flags,const char *fmt,...) __attribute__((format(printf,2,3)));
static inline struct workqueue_struct *sf_tb_ordered_workqueue(unsigned flags,const char *fmt,...)
{
 char name[64];va_list args;va_start(args,fmt);int n=vsnprintf(name,sizeof(name),fmt,args);va_end(args);
 if(n<0 || n>=sizeof(name))return NULL;
 /* Both native taskqueue implementations copy name before returning. */
 return flags&WQ_HIGHPRI?linux_create_workqueue_highpri(name,1):linux_create_workqueue_common(name,1);
}
#define alloc_ordered_workqueue(fmt,flags,...) sf_tb_ordered_workqueue((flags),(fmt),##__VA_ARGS__)
static inline int device_property_count_u8(struct device *dev,const char *name)
{ return fwnode_property_read_u8_array(dev_fwnode(dev),name,NULL,0); }
static inline int device_property_read_u8_array(struct device *dev,const char *name,u8 *value,size_t count)
{ return fwnode_property_read_u8_array(dev_fwnode(dev),name,value,count); }
static inline int device_property_read_u8(struct device *dev,const char *name,u8 *value)
{ return device_property_read_u8_array(dev,name,value,1); }
#ifndef SET_RUNTIME_PM_OPS
#define SET_RUNTIME_PM_OPS(s,r,i) .runtime_suspend=(s),.runtime_resume=(r),.runtime_idle=(i),
#endif
#ifndef PCI_EXT_CAP_ID_VNDR
#define PCI_EXT_CAP_ID_VNDR 0x0b
#endif
/* Preserve constant-time comparison's optimizer barrier. */
#ifndef OPTIMIZER_HIDE_VAR
#define OPTIMIZER_HIDE_VAR(v) __asm__ __volatile__("" : "+r" (v))
#endif
static inline void sf_tb_put_be32(u32 value,void *p)
{ __be32 wire=cpu_to_be32(value);memcpy(p,&wire,sizeof(wire)); }
#define put_unaligned_be32 sf_tb_put_be32
static inline int hex_to_bin(unsigned char c) { return _h2b(c); }
static inline bool device_match_fwnode(struct device *dev,const struct fwnode_handle *fw)
{ return fw && dev_fwnode(dev)==fw; }
static inline int fwnode_property_read_u8(const struct fwnode_handle *fw,const char *name,u8 *value)
{ return fwnode_property_read_u8_array(fw,name,value,1); }
static inline struct fwnode_handle *fwnode_find_reference(const struct fwnode_handle *fw,const char *name,unsigned index)
{
 struct fwnode_reference_args args={0};
 int err=fwnode_property_get_reference_args(fw,name,NULL,0,index,&args);
 return err?ERR_PTR(err):args.fwnode;
}
DEFINE_FREE(fwnode_handle,struct fwnode_handle *,if(!IS_ERR_OR_NULL(_T)) fwnode_handle_put(_T))
static inline int device_match_name(struct device *dev,const void *name)
{ return dev && name && dev_name(dev) && !strcmp(dev_name(dev),name); }
static inline int device_match_of_node(struct device *dev,const void *node)
{ return dev && node && dev->of_node==node; }
/* Component aggregate binding needs real transactional devres groups. These
 * required native APIs deliberately have no successful no-op implementation. */
void *devres_open_group(struct device *,void *,gfp_t);
void devres_close_group(struct device *,void *);
int devres_release_group(struct device *,void *);
void devres_remove_group(struct device *,void *);
/* Repair the private SDK macro's single-argument size_mul call. */
#undef kmalloc_objs
#define kmalloc_objs(p,n,...) kmalloc_array((n),sizeof(p),default_gfp(__VA_ARGS__))
#endif
