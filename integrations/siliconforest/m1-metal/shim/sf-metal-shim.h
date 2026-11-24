// SPDX-License-Identifier: MIT
// Included ahead of every darling-metal file here (ours, SiliconForest m1-metal): names it expects
// from Apple's headers that SiliconForest's do not define.
#include <stdint.h>
#include <CoreFoundation/CoreFoundation.h>
// a darling-metal header spells uint8_t this way (NS_ENUM(uint8, MTLTextureSwizzle))
typedef uint8_t uint8;
