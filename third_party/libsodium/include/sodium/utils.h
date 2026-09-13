
#ifndef sodium_utils_H
#define sodium_utils_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SODIUM_C99
# if defined(__cplusplus) || !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#  define SODIUM_C99(X)
# else
#  define SODIUM_C99(X) X
# endif
#endif

SODIUM_EXPORT
void sodium_memzero(void *const pnt, const size_t len);

SODIUM_EXPORT
void sodium_stackzero(const size_t len);







SODIUM_EXPORT
int sodium_memcmp(const void *const b1_, const void *const b2_, size_t len)
    __attribute__((warn_unused_result));







SODIUM_EXPORT
int sodium_compare(const unsigned char *b1_, const unsigned char *b2_, size_t len)
    __attribute__((warn_unused_result));

SODIUM_EXPORT
int sodium_is_zero(const unsigned char *n, const size_t nlen);

SODIUM_EXPORT
void sodium_increment(unsigned char *n, const size_t nlen);

SODIUM_EXPORT
void sodium_add(unsigned char *a, const unsigned char *b, const size_t len);

SODIUM_EXPORT
void sodium_sub(unsigned char *a, const unsigned char *b, const size_t len);

SODIUM_EXPORT
char *sodium_bin2hex(char *const hex, const size_t hex_maxlen, const unsigned char *const bin,
                     const size_t bin_len) __attribute__((nonnull(1)));

SODIUM_EXPORT
int sodium_hex2bin(unsigned char *const bin, const size_t bin_maxlen, const char *const hex,
                   const size_t hex_len, const char *const ignore, size_t *const bin_len,
                   const char **const hex_end) __attribute__((nonnull(1)));

#define sodium_base64_VARIANT_ORIGINAL            1
#define sodium_base64_VARIANT_ORIGINAL_NO_PADDING 3
#define sodium_base64_VARIANT_URLSAFE             5
#define sodium_base64_VARIANT_URLSAFE_NO_PADDING  7





#define sodium_base64_ENCODED_LEN(BIN_LEN, VARIANT)                            \
  ((BIN_LEN) / 3U > (SIZE_MAX - 5) / 4U                                        \
       ? (size_t)SIZE_MAX                                                      \
       : (((BIN_LEN) / 3U) * 4U +                                              \
          ((((BIN_LEN) - ((BIN_LEN) / 3U) * 3U) |                              \
            (((BIN_LEN) - ((BIN_LEN) / 3U) * 3U) >> 1)) &                      \
           1U) *                                                               \
              (4U - ((0U - (((VARIANT) & 2U) >> 1)) &                          \
                     (3U - ((BIN_LEN) - ((BIN_LEN) / 3U) * 3U)))) +            \
          1U))

SODIUM_EXPORT
size_t sodium_base64_encoded_len(const size_t bin_len, const int variant);

SODIUM_EXPORT
char *sodium_bin2base64(char *const b64, const size_t b64_maxlen, const unsigned char *const bin,
                        const size_t bin_len, const int variant) __attribute__((nonnull(1)));

SODIUM_EXPORT
int sodium_base642bin(unsigned char *const bin, const size_t bin_maxlen, const char *const b64,
                      const size_t b64_len, const char *const ignore, size_t *const bin_len,
                      const char **const b64_end, const int variant) __attribute__((nonnull(1)));

SODIUM_EXPORT
int sodium_ip2bin(unsigned char bin[16], const char *ip, size_t ip_len_)
    __attribute__((warn_unused_result)) __attribute__((nonnull));

SODIUM_EXPORT
char *sodium_bin2ip(char *ip, size_t ip_maxlen, const unsigned char bin[16])
    __attribute__((nonnull));

SODIUM_EXPORT
int sodium_mlock(void *const addr, const size_t len) __attribute__((nonnull));

SODIUM_EXPORT
int sodium_munlock(void *const addr, const size_t len) __attribute__((nonnull));



































SODIUM_EXPORT
void *sodium_malloc(const size_t size) __attribute__((malloc));

SODIUM_EXPORT
void *sodium_allocarray(size_t count, size_t size) __attribute__((malloc));

SODIUM_EXPORT
void sodium_free(void *ptr);

SODIUM_EXPORT
int sodium_mprotect_noaccess(void *ptr) __attribute__((nonnull));

SODIUM_EXPORT
int sodium_mprotect_readonly(void *ptr) __attribute__((nonnull));

SODIUM_EXPORT
int sodium_mprotect_readwrite(void *ptr) __attribute__((nonnull));

SODIUM_EXPORT
int sodium_pad(size_t *padded_buflen_p, unsigned char *buf, size_t unpadded_buflen,
               size_t blocksize, size_t max_buflen) __attribute__((nonnull(2)));

SODIUM_EXPORT
int sodium_unpad(size_t *unpadded_buflen_p, const unsigned char *buf, size_t padded_buflen,
                 size_t blocksize) __attribute__((nonnull(2)));



int _sodium_alloc_init(void);

#ifdef __cplusplus
}
#endif

#endif
