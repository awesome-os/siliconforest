/* Original read-only tiny MIG consumer; no device open or physical access. */
#include <PDIOKitLib.h>
#include <mach/mach.h>
#include <CommonCrypto/CommonDigest.h>
#include <stdio.h>
#include <string.h>
#include "device_user.h"

int main(int argc,char **argv)
{
    mach_port_t master=MACH_PORT_NULL,entry=MACH_PORT_NULL;
    io_string_t path="IOService:/IOResources";
    io_name_t key="PDGPUPreflightV1";
    uint8_t raw[64],digest[CC_SHA256_DIGEST_LENGTH];
    mach_msg_type_number_t count=sizeof(raw);
    if (argc!=3||strcmp(argv[1],"--nonce")||strlen(argv[2])!=48) return 2;
    for (unsigned i=0;i<48;i++)
        if (!((argv[2][i]>='0'&&argv[2][i]<='9')||(argv[2][i]>='a'&&argv[2][i]<='f'))) return 2;
    kern_return_t kr=IOMasterPort(MACH_PORT_NULL,&master);
    if (kr!=KERN_SUCCESS||master==MACH_PORT_NULL) return 1;
    kr=io_registry_entry_from_path(master,path,&entry);
    if (kr!=KERN_SUCCESS||entry==MACH_PORT_NULL) return 1;
    kr=io_registry_entry_get_property_bytes(entry,key,(char *)raw,&count);
    kern_return_t release=IOObjectRelease(entry);
    if (kr!=KERN_SUCCESS||count!=sizeof(raw)||release!=KERN_SUCCESS) return 1;
    if (!CC_SHA256(raw,sizeof(raw),digest)) return 1;
    printf("DL25_GPU_PREFLIGHT_BEGIN_%s bytes=64\n",argv[2]);
    printf("DL25_GPU_PREFLIGHT_HEX_%s index=0 data=",argv[2]);
    for (unsigned i=0;i<sizeof(raw);i++) printf("%02x",raw[i]);
    printf("\nDL25_GPU_PREFLIGHT_END_%s sha256=",argv[2]);
    for (unsigned i=0;i<sizeof(digest);i++) printf("%02x",digest[i]);
    printf("\n");
    return fflush(stdout)||ferror(stdout)?1:0;
}
