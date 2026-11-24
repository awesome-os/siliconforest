/* Original host-only checked callback path. See register_accessor.h. */
#include "register_accessor.h"

#define APR_BOUND 0x41505231u
#define APR_BCM_BOUND 0x41505232u

static int window_ok(const struct apr_accessor *a, unsigned region, uint32_t offset, uint32_t width)
{
    uint32_t size = apl_region_size(region);
    if (a->bound == APR_BCM_BOUND) {
        if (region == APR_BCM_CONFIG) size = APR_BCM_CONFIG_BYTES;
        else if (region == APR_BCM_BAR0) size = APR_BCM_BAR0_BYTES;
        else if (region == APR_BCM_BAR2) size = APR_BCM_BAR2_BYTES;
    }
    return size >= width && !(offset & (width - 1u)) && offset <= size - width;
}

static int ready(const struct apr_accessor *a)
{
    return a && (a->bound == APR_BOUND || a->bound == APR_BCM_BOUND);
}

static int read32(void *context, unsigned region, uint32_t offset, uint32_t *value)
{
    struct apr_accessor *a = context;
    uint32_t temporary = 0;
    if (!ready(a) || !value || !window_ok(a, region, offset, 4)) return APB_ARGUMENT;
    if (region == APB_FUSE && offset != 0x84 && offset != 0x418 && offset != 0x41c)
        return APB_ARGUMENT;
    if (!a->fake.read32 || a->fake.read32(a->fake.context, region, offset, &temporary))
        return APB_IO;
    *value = temporary;
    return APB_OK;
}

static int write32(void *context, unsigned region, uint32_t offset, uint32_t value)
{
    struct apr_accessor *a = context;
    if (!ready(a) || !window_ok(a, region, offset, 4)) return APB_ARGUMENT;
    /* No fake shared-write opt-in exists in this accessor. */
    if (apl_region_shared(region)) return APB_GATED;
    if (region == APR_BCM_CONFIG && offset != 0x80 && offset != 0x98) return APB_GATED;
    if (region == APB_ECAM0 && offset == 0xa0) return APB_ARGUMENT;
    if (!a->fake.write32 || a->fake.write32(a->fake.context, region, offset, value))
        return APB_IO;
    return APB_OK;
}

static int read16(void *context, unsigned region, uint32_t offset, uint16_t *value)
{
    struct apr_accessor *a = context;
    uint16_t temporary = 0;
    if (!ready(a) || !value || region != APB_ECAM0 || offset != 0xa0)
        return APB_ARGUMENT;
    if (!a->fake.read16 || a->fake.read16(a->fake.context, region, offset, &temporary))
        return APB_IO;
    *value = temporary;
    return APB_OK;
}

static int write16(void *context, unsigned region, uint32_t offset, uint16_t value)
{
    struct apr_accessor *a = context;
    if (!ready(a) || region != APB_ECAM0 || offset != 0xa0) return APB_ARGUMENT;
    if (!a->fake.write16 || a->fake.write16(a->fake.context, region, offset, value))
        return APB_IO;
    return APB_OK;
}

static void delay_us(void *context, uint32_t microseconds)
{
    struct apr_accessor *a = context;
    if (ready(a) && a->fake.delay_us) a->fake.delay_us(a->fake.context, microseconds);
}

static int bind_fake(struct apr_accessor *a, const struct apb_io *fake, struct apb_io *io, uint32_t bound)
{
    struct apb_io next;
    if (!a || !fake || !io || !fake->read32 || fake == io || fake == &a->fake || io == &a->fake ||
        fake->read32 == read32 || fake->write32 == write32 ||
        fake->read16 == read16 || fake->write16 == write16 || fake->delay_us == delay_us)
        return APB_ARGUMENT;
    next.context = a;
    next.read32 = read32; next.write32 = write32;
    next.read16 = read16; next.write16 = write16; next.delay_us = delay_us;
    a->fake = *fake;
    a->bound = bound;
    *io = next;
    return APB_OK;
}

int apr_bind_fake(struct apr_accessor *a, const struct apb_io *fake, struct apb_io *io)
{
    return bind_fake(a, fake, io, APR_BOUND);
}

int apr_bind_bcm_fake(struct apr_accessor *a, const struct apb_io *fake, struct apb_io *io)
{
    return bind_fake(a, fake, io, APR_BCM_BOUND);
}

int apr_bcm_io_bound(const struct apb_io *io)
{
    if (!io || io->read32 != read32 || io->write32 != write32 || !io->context) return 0;
    return ((const struct apr_accessor *)io->context)->bound == APR_BCM_BOUND;
}
