/* SPDX-License-Identifier: MIT */
/* HOST TEST ONLY: compatible-string fixtures, no device discovery or MMIO. */
#ifndef SF_TB_TEST_OF_H
#define SF_TB_TEST_OF_H
#include <linux/types.h>
#include <string.h>
struct property { const char *name; void *value; int length; struct property *next; };
struct device_node { struct property *properties; };
struct of_device_id { char compatible[128]; };
static inline const struct of_device_id *of_match_node(const struct of_device_id *ids, const struct device_node *root)
{
    if (!root) return NULL;
    for (struct property *p=root->properties;p;p=p->next) {
        if (strcmp(p->name,"compatible")) continue;
        for (const struct of_device_id *id=ids;id->compatible[0];++id)
            for (int off=0;off<p->length;) {
                size_t len=strnlen((const char*)p->value+off,p->length-off);
                if (len>=(size_t)(p->length-off)) return NULL;
                if (!strcmp((const char*)p->value+off,id->compatible)) return id;
                off+=len+1;
            }
    }
    return NULL;
}
#endif
