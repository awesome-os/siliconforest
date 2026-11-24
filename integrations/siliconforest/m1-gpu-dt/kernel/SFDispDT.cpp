// SPDX-License-Identifier: MIT
/*
 * SFDispDT.cpp - the display nodes (src/dispdt.c) built from the live device tree and, on
 * request, published in gIODTPlane next to the GPU's. Called by SFGpuDT::start().
 *
 *   sfdispdt=0  (default) nothing
 *   sfdispdt=1  build and log a summary
 *   sfdispdt=2  the same, then publish: iommu@23130c000, iommu@231304000, mbox@231c08000,
 *               power-management@23b738000, clock-disp0, dcp@231c00000 (with piodma and
 *               panel), display-subsystem under the device-tree root; the five reserved
 *               regions under reserved-memory; framebuffer@<base> under chosen.
 *               Needs sfgpudt_stringprops=1 (the of layer must be told which properties
 *               are text), as the GPU nodes do.
 *
 * dcp@231c00000 and display-subsystem are published as platform devices so that a driver
 * kext can match them; nothing here starts a driver or touches display hardware. The only
 * hardware-derived inputs are the device tree and the boot-args' framebuffer description.
 */
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOService.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/boot.h>

extern "C" {
#include "dispdt.h"
}

#define LOG(fmt, ...) IOLog("SFGpuDT: dispdt: " fmt "\n", ##__VA_ARGS__)

namespace {

struct AdtCtx {
    IORegistryEntry *held[10];
    const char *path[10];
    unsigned n;
};

const void *adtGetprop(void *ctx, const char *nodePath, const char *name, uint32_t *len)
{
    AdtCtx *c = (AdtCtx *)ctx;
    IORegistryEntry *e = NULL;

    for (unsigned i = 0; i < c->n; i++)
        if (strcmp(c->path[i], nodePath) == 0)
            e = c->held[i];
    if (e == NULL) {
        if (c->n >= 10)
            return NULL;
        e = IORegistryEntry::fromPath(nodePath, gIODTPlane);
        if (e == NULL)
            return NULL;
        c->path[c->n] = nodePath; /* the library passes string literals */
        c->held[c->n++] = e;
    }
    OSData *d = OSDynamicCast(OSData, e->getProperty(name));
    if (d == NULL)
        return NULL;
    *len = d->getLength();
    return d->getBytesNoCopy();
}

/* "dcp@231c00000" -> name "dcp", location "231c00000": sfos_dt_name() joins them again */
void nameEntry(IORegistryEntry *e, const char *full)
{
    char name[48];
    const char *at = strchr(full, '@');

    if (at == NULL || (size_t)(at - full) >= sizeof(name)) {
        e->setName(full);
        return;
    }
    memcpy(name, full, at - full);
    name[at - full] = 0;
    e->setName(name);
    e->setLocation(at + 1, gIODTPlane);
}

bool publish(const struct dispdt_tree *tree)
{
    IORegistryEntry *made[DISPDT_NODE_COUNT] = {};
    bool reused[DISPDT_NODE_COUNT] = {};
    IORegistryEntry *root = IORegistryEntry::fromPath("/", gIODTPlane);
    bool ok = root != NULL;

    for (int id = 0; ok && id < DISPDT_NODE_COUNT; id++) {
        const struct dispdt_node *n = &tree->nodes[id];
        IORegistryEntry *e = NULL;

        if (!n->present)
            continue;
        if (n->existing) {
            /* reserved-memory (the GPU's publish may have made it) and chosen: attach below what is there */
            char path[64];
            snprintf(path, sizeof(path), "/%s", n->name);
            e = IORegistryEntry::fromPath(path, gIODTPlane);
            if (e != NULL) {
                made[id] = e;
                reused[id] = true;
                continue;
            }
        }
        if (n->device) {
            /* a driver binds to it: a service whose name matching looks at "compatible" */
            IOPlatformDevice *dev = OSTypeAlloc(IOPlatformDevice);
            ok = dev != NULL && dev->init();
            e = dev;
        } else {
            e = OSTypeAlloc(IORegistryEntry);
            ok = e != NULL && e->init();
        }
        if (!ok) {
            OSSafeReleaseNULL(e);
            break;
        }
        made[id] = e;
        nameEntry(e, n->name);

        OSData *strings = OSData::withCapacity(64);
        for (uint32_t i = 0; i < tree->nprops; i++) {
            const struct dispdt_prop *p = &tree->props[i];
            uint8_t bytes[DISPDT_MAX_VALUE];

            if (p->node != id)
                continue;
            dispdt_prop_to_iodt(p, bytes);
            OSData *d = OSData::withBytes(bytes, p->len);
            if (d == NULL) {
                ok = false;
                break;
            }
            e->setProperty(p->name, d);
            d->release();
            if (p->type == GPUDT_T_STRINGS && strings != NULL)
                strings->appendBytes(p->name, (unsigned int)strlen(p->name) + 1);
        }
        /* which properties are text, so the of layer need not guess from the bytes */
        if (ok && strings != NULL && strings->getLength() != 0)
            e->setProperty("pd,string-props", strings);
        OSSafeReleaseNULL(strings);
    }

    /* parents before children: the ids are ordered that way */
    for (int id = 0; ok && id < DISPDT_NODE_COUNT; id++) {
        if (made[id] == NULL || reused[id])
            continue;
        int parent = tree->nodes[id].parent;
        ok = made[id]->attachToParent(parent < 0 ? root : made[parent], gIODTPlane);
    }
    for (int id = 0; ok && id < DISPDT_NODE_COUNT; id++) {
        if (made[id] == NULL || !tree->nodes[id].device)
            continue;
        IOService *svc = OSDynamicCast(IOService, made[id]);
        ok = svc != NULL && svc->attach(IOService::getPlatform());
        if (ok)
            svc->registerService();
    }
    if (!ok) {
        for (int id = DISPDT_NODE_COUNT - 1; id >= 0; id--)
            if (made[id] != NULL && !reused[id])
                made[id]->detachAll(gIODTPlane);
    }
    for (int id = 0; id < DISPDT_NODE_COUNT; id++)
        OSSafeReleaseNULL(made[id]);
    OSSafeReleaseNULL(root);
    return ok;
}

} // namespace

/* Returns true when the nodes were published. fwVersion: gpudt_tree.fw_version. */
bool sf_dispdt_run(const uint32_t *fwVersion, uint32_t fwVersionLen, bool ofHasStringProps)
{
    int mode = 0;
    bool published = false;

    PE_parse_boot_argn("sfdispdt", &mode, sizeof(mode));
    if (mode <= 0)
        return false;

    struct dispdt_tree *tree = (struct dispdt_tree *)IOMallocZero(sizeof(*tree));
    if (tree == NULL)
        return false;
    AdtCtx ctx = {};
    struct gpudt_adt_ops ops = {&ctx, adtGetprop};
    struct dispdt_config cfg = {};
    boot_args *args = (boot_args *)PE_state.bootArgs;

    cfg.chip_id = GPUDT_CHIP_T8103;
    cfg.phandle_base = DISPDT_DARWIN_PHANDLE_BASE;
    for (uint32_t i = 0; i < fwVersionLen && i < 4; i++)
        cfg.fw_version[i] = fwVersion[i];
    cfg.fw_version_len = fwVersionLen;
    cfg.fb_base = args->Video.v_baseAddr;
    cfg.fb_width = (uint32_t)args->Video.v_width;
    cfg.fb_height = (uint32_t)args->Video.v_height;
    cfg.fb_stride = (uint32_t)args->Video.v_rowBytes;
    cfg.fb_depth = (uint32_t)args->Video.v_depth;

    int ret = dispdt_build_t8103(&ops, &cfg, tree);
    for (uint32_t i = 0; i < tree->ndiags; i++)
        LOG("note %u %s (0x%llx, 0x%llx)", tree->diags[i].code, tree->diags[i].text ? tree->diags[i].text : "",
            (unsigned long long)tree->diags[i].a, (unsigned long long)tree->diags[i].b);
    if (ret != DISPDT_OK) {
        LOG("the display tree was refused: %d", ret);
    } else {
        unsigned nodes = 0;
        for (int id = 0; id < DISPDT_NODE_COUNT; id++)
            nodes += tree->nodes[id].present ? 1 : 0;
        LOG("%u nodes, %u properties; dcp firmware %u.%u.%u compat %u.%u.%u", nodes, (unsigned)tree->nprops,
            (unsigned)cfg.fw_version[0], (unsigned)cfg.fw_version[1], (unsigned)cfg.fw_version[2], (unsigned)tree->fw_compat[0],
            (unsigned)tree->fw_compat[1], (unsigned)tree->fw_compat[2]);
        LOG("carve-outs: dcp_data 0x%llx+0x%llx region57 0x%llx+0x%llx region94 0x%llx+0x%llx region95 0x%llx+0x%llx framebuffer 0x%llx+0x%llx",
            (unsigned long long)tree->region_phys[0], (unsigned long long)tree->region_size[0],
            (unsigned long long)tree->region_phys[1], (unsigned long long)tree->region_size[1],
            (unsigned long long)tree->region_phys[2], (unsigned long long)tree->region_size[2],
            (unsigned long long)tree->region_phys[3], (unsigned long long)tree->region_size[3],
            (unsigned long long)tree->region_phys[4], (unsigned long long)tree->region_size[4]);
        /* v_depth carries flag bits above the depth (0x1001e in boot dc12): the low byte is the depth, as m1n1 takes it */
        LOG("boot framebuffer 0x%llx %ux%u stride %u depth %u (v_depth 0x%x)", (unsigned long long)cfg.fb_base,
            (unsigned)cfg.fb_width, (unsigned)cfg.fb_height, (unsigned)cfg.fb_stride, (unsigned)(cfg.fb_depth & 0xff),
            (unsigned)cfg.fb_depth);
        if (mode >= 2 && ofHasStringProps) {
            published = publish(tree);
            LOG("%s", published ? "published" : "NOT published");
        } else {
            LOG("not publishing (sfdispdt=%d%s)", mode, ofHasStringProps ? "" : ", needs sfgpudt_stringprops=1");
        }
    }
    for (unsigned i = 0; i < ctx.n; i++)
        ctx.held[i]->release();
    IOFree(tree, sizeof(*tree));
    return published;
}
