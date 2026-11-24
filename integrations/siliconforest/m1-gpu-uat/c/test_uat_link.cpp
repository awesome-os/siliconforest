/* Host C++ caller of the separately C-compiled scalar codec. License pending.
 * Include directly: an external extern-C wrapper would hide a broken header.
 * Uses fields shared with frozen C001 so the old-header control must compile
 * and then fail at linkage, rather than fail because of the field rename.
 */
#include <uat_t8103.h>
#include <cstddef>

static_assert(sizeof(struct uat_decoded) == 48, "decoded layout");
static_assert(offsetof(struct uat_decoded, pa) == 8, "PA base offset");
static_assert(offsetof(struct uat_decoded, reserved0) == 47, "tail offset");

int main()
{
    struct uat_flags flags = {};
    struct uat_decoded decoded = {};
    struct uat_block_facts facts = {};
    const uint64_t page_base = 0x18004000ull;
    const uint64_t sentinel = 0xa5a5a5a5a5a5a5a5ull;
    uint64_t word = 0;
    uint64_t pa = sentinel;
    uint32_t offset = 0;

    if (uat_pa_bits() != 40u || uat_page_size() != 16384u) return 1;
    flags.af = 1;
    if (uat_encode_pte(3u, UAT_KIND_PAGE, page_base, &flags, &word) != UAT_OK) return 2;
    if (word != (page_base | 0x403ull)) return 3;
    if (uat_decode(3u, word, &decoded) != UAT_OK) return 4;
    if (decoded.kind != UAT_KIND_PAGE || decoded.pa != page_base || decoded.af != 1u) return 5;
    if (uat_page_offset(0x02004123ull, &offset) != UAT_OK || offset != 0x123u) return 6;
    if (decoded.pa != page_base) return 7;
    if (uat_refuse_block(2u, 0x02001000ull, 0x04000000ull, &pa, &facts) != UAT_REFUSED)
        return 8;
    if (pa != sentinel || facts.raw_output != 0x04000000ull ||
        facts.span != (1ull << 25) || facts.virtual_offset != 0x1000ull) return 9;
    return 0;
}
