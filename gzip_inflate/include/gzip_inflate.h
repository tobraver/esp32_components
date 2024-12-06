#ifndef __GZIP_INFLATE_H__
#define __GZIP_INFLATE_H__

#include "stdio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gzip_inflate(uint8_t *in, int inlen, uint8_t *out, int *outlen);

#ifdef __cplusplus
}
#endif
#endif // __GZIP_INFLATE_H__
