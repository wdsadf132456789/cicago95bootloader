#ifndef STAGE3_HASH_H
#define STAGE3_HASH_H

#include <stdint.h>

uint32_t hash_crc32(const uint8_t *data, uint32_t len);

#endif