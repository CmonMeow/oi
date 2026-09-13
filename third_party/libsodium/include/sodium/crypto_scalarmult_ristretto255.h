
#ifndef crypto_scalarmult_ristretto255_H
#define crypto_scalarmult_ristretto255_H

#include <stddef.h>

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_scalarmult_ristretto255_BYTES 32U
SODIUM_EXPORT
size_t crypto_scalarmult_ristretto255_bytes(void);

#define crypto_scalarmult_ristretto255_SCALARBYTES 32U
SODIUM_EXPORT
size_t crypto_scalarmult_ristretto255_scalarbytes(void);









SODIUM_EXPORT
int crypto_scalarmult_ristretto255(unsigned char *q, const unsigned char *n,
                                   const unsigned char *p)
            __attribute__ ((warn_unused_result)) __attribute__ ((nonnull));

SODIUM_EXPORT
int crypto_scalarmult_ristretto255_base(unsigned char *q,
                                        const unsigned char *n)
            __attribute__ ((nonnull));

#ifdef __cplusplus
}
#endif

#endif
