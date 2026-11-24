/* SPDX-License-Identifier: MIT */
/* Own public data declarations. Carbon packing verified by Linux-built M5 probe. */
#include <CoreServices/FileManager.h>
#include <stdint.h>
#pragma pack(push,2)
typedef struct {uint16_t high;uint32_t low;uint16_t fraction;} SFUTCDate;
typedef struct {
 uint16_t nodeFlags;int16_t volume;uint32_t parentDirID,nodeID;
 uint8_t sharingFlags,userPrivileges,reserved[2];
 SFUTCDate created,contentModified,attributeModified,accessed,backedUp;
 uint32_t permissions[4];uint8_t finderInfo[16],extendedFinderInfo[16];
 uint64_t dataLogicalSize,dataPhysicalSize,resourceLogicalSize,resourcePhysicalSize;
 uint32_t valence,textEncoding;
} SFCatalogInfo;
typedef struct {
 int16_t version;int32_t attributes;void *localHandle;
 int32_t serverAddress,volumeGrade;int16_t foreignPrivileges;
 int32_t extendedAttributes;const char *deviceID;uint32_t maximumNameLength;
} SFVolumeParms;
#pragma pack(pop)
_Static_assert(sizeof(SFCatalogInfo)==144,"catalog ABI");
_Static_assert(__builtin_offsetof(SFVolumeParms,deviceID)==28,"volume ABI, native reference");
_Static_assert(sizeof(SFVolumeParms)==40,"volume ABI");
