/* Original host fake-file wiring; no hardware implementation. */
#include "readonly_accessor.h"

static int observer_read32(void *context, unsigned region, uint32_t offset, uint32_t *value)
{
    struct apr_observer *wire = context;
    unsigned port_region;
    if (!wire || !value || !apo_read_allowed(region, offset)) return APB_ARGUMENT;
    switch (region) {
    case DLS_PMGR: port_region = APB_PMGR; break;
    case DLS_RC:   port_region = APB_RC; break;
    case DLS_PHY:  port_region = APB_PHY; break;
    case DLS_PORT: port_region = APB_PORT0; break;
    default: return APB_ARGUMENT;
    }
    if (!wire->port.read32) return APB_ARGUMENT;
    return wire->port.read32(wire->port.context, port_region, offset, value);
}

int apr_bind_observer_fake(struct apr_observer *wire, const struct apb_io *fake,
                           struct dls_io *observer_io)
{
    int result;
    if (!wire || !fake || !observer_io || fake == &wire->port || fake == &wire->checked.fake)
        return APB_ARGUMENT;
    result = apr_bind_fake(&wire->checked, fake, &wire->port);
    if (result) return result;
    observer_io->context = wire;
    observer_io->read32 = observer_read32;
    return APB_OK;
}

int apo_observe_fake_accessor(const struct dls_board *board, const struct apb_io *fake,
                              struct apo_snapshot *out)
{
    struct apr_observer wire = {0};
    struct dls_io io;
    if (apr_bind_observer_fake(&wire, fake, &io)) return apo_observe(board, NULL, out);
    return apo_observe(board, &io, out);
}
