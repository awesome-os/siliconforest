/* Original helpers. Numeric facts are cited in PROVENANCE.json.
 * No upstream function is copied. No encode, send, or allocation.
 */
#include "m1tp_enum.h"

static uint16_t load_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((unsigned)p[1] << 8));
}

int m1tp_claim_request(uint8_t *used, uint8_t report, uint8_t subdevice)
{
    uint8_t bit;
    if (!used || (*used & ~31u)) return 0;
    if (report == 0x01 && subdevice == 0xd0) bit = M1TP_BIT_INFO;
    else if (report == 0x02 && subdevice == 0x01) bit = M1TP_BIT_IFACE1;
    else if (report == 0x10 && subdevice == 0x01) bit = M1TP_BIT_DESC1;
    else if (report == 0x02 && subdevice == 0x02) bit = M1TP_BIT_IFACE2;
    else if (report == 0x10 && subdevice == 0x02) bit = M1TP_BIT_DESC2;
    else return 0;
    if (*used & bit) return 0;
    *used = (uint8_t)(*used | bit);
    return 1;
}

int m1tp_interface_info(const uint8_t *payload, size_t n,
                        struct m1tp_interface_info *out)
{
    uint16_t input, output, control;
    if (!payload || !out) return M1TP_ERR_ARGUMENT;
    if (n < 16u || n > 2038u) return M1TP_ERR_LENGTH;
    if (payload[1] != 2u) return M1TP_ERR_INTERFACE;
    input = load_u16(payload + 6);
    if (input < 1u || input > 2038u) return M1TP_ERR_INPUT;
    output = load_u16(payload + 8);
    control = load_u16(payload + 10);
    out->max_input = input;
    out->max_output = output;
    out->max_control = control;
    out->interface = 2u;
    out->country = payload[5];
    return M1TP_OK;
}

int m1tp_copy_descriptor(uint8_t *dst, size_t cap, uint32_t *length,
                         const uint8_t *src, size_t n)
{
    size_t i;
    if (!dst || !length || !src) return M1TP_ERR_ARGUMENT;
    if (n < 1u || n > 512u) return M1TP_ERR_LENGTH;
    if (n > cap) return M1TP_ERR_LENGTH;
    for (i = 0; i < n; i++) dst[i] = src[i];
    *length = (uint32_t)n;
    return M1TP_OK;
}
