#ifndef MK_ZLIB_GZIP_H
#define MK_ZLIB_GZIP_H

#include <stdint.h>

int mk_zlib_is_gzip_package(const uint8_t *buf, uint32_t len);
int mk_zlib_decompress_gzip(const uint8_t *in_buf, uint32_t in_len,
                            uint8_t *out_buf, uint32_t out_buf_len,
                            uint32_t *consumed_len, uint32_t *out_len);

#endif
