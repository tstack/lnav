
#ifndef __COSMOPOLITAN__
#include <stdint.h>
#endif

#define SHA_256_HASH_SIZE 32

#ifdef __cplusplus
extern "C" {
#endif

void
calc_sha_256(uint8_t hash[SHA_256_HASH_SIZE], const void *input, size_t len);

#ifdef __cplusplus
};
#endif
