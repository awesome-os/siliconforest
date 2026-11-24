/* SPDX-License-Identifier: MIT */
#include "pdsf.h"
#include "nvme.h"
#include "string.h"
#include "utils.h"

/*
 * The installed stage 2. Its stage 1 can only start an image that fits in its own footprint
 * (m1n1 chainload.c refuses a larger one: "image span overlaps the live heap"), so the
 * configuration and the collection are not behind this image: they are the file
 * sf/kernel.bin, next to sf/boot.bin on the boot partition, a record list exactly as it is
 * appended to the bench's image. It is read here into m1n1's heap (read-only: m1n1's storage
 * code has no write), the storage controller is stopped again, and the records are handled
 * as appended ones are: same checksums, same root rule, and any failure ends in sf_stop.
 */
int rust_load_image(const char *spec, void **image, size_t *size);

#define fail(...)                                                                                  \
    do {                                                                                           \
        printf("PDSF: " __VA_ARGS__);                                                              \
        return -1;                                                                                 \
    } while (0)

int pdsf_boot_file(const char *partuuid)
{
    static char spec[64];
    void *image = NULL;
    size_t size = 0;
    int ret;

    if (strlen(partuuid) != 36)
        fail("stage 1 named no boot partition\n");
    snprintf(spec, sizeof(spec), "%s;" PDSF_KERNEL_FILE, partuuid);
    printf("PDSF: reading " PDSF_KERNEL_FILE " from the boot partition\n");
    if (!nvme_init())
        fail("the storage controller did not start\n");
    ret = rust_load_image(spec, &image, &size);
    /* stopped before anything else: the kernel's driver starts the controller itself */
    if (!nvme_shutdown())
        fail("the storage controller did not stop\n");
    if (ret || !image || !size)
        fail("could not read " PDSF_KERNEL_FILE "\n");
    ret = pdsf_boot_records(image, size, 0);
    if (ret == 1)
        fail(PDSF_KERNEL_FILE " is not a record list\n");
    return ret;
}
