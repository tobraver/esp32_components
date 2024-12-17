#ifndef __GZIP_DEFLATE_H__
#define __GZIP_DEFLATE_H__

#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "esp_idf_version.h"
#if (ESP_IDF_VERSION_MAJOR == 4) && (ESP_IDF_VERSION_MINOR < 3)
#include "esp32/rom/miniz.h"
#else
#include "rom/miniz.h"
#endif
#include "esp_err.h"

#ifdef MINIZ_NO_ZLIB_APIS

struct mz_internal_state;

// Compression/decompression stream struct.
typedef struct mz_stream_s {
    const unsigned char      *next_in;   // pointer to next byte to read
    unsigned int              avail_in;  // number of bytes available at next_in
    mz_ulong                  total_in;  // total number of bytes consumed so far

    unsigned char            *next_out;  // pointer to next byte to write
    unsigned int              avail_out; // number of bytes that can be written to next_out
    mz_ulong                  total_out; // total number of bytes produced so far

    char                     *msg;       // error msg (unused)
    struct mz_internal_state *state;     // internal state, allocated by zalloc/zfree

    int                       data_type; // data_type (unused)
    mz_ulong                  adler;     // adler32 of the source or uncompressed data
    mz_ulong                  crc32;     // crc32 of the source or uncompressed data
} mz_stream;

typedef mz_stream *mz_streamp;

#endif // MINIZ_NO_ZLIB_APIS

#define GZIP_DEFLATE_LEVEL      MZ_DEFAULT_LEVEL // -1 ~ 10
#define GZIP_DEFLATE_BUFF_SIZE  1024

typedef esp_err_t(*gzip_stream_out_t)(uint8_t* data, uint32_t len, void* user_ctx);

typedef struct {
    uint8_t id1;
    uint8_t id2;
    uint8_t compression;
    uint8_t flags;
    uint8_t mtime[4];
    uint8_t extra_flags;
    uint8_t os;
} gzip_header_t;

typedef struct {
    gzip_header_t header;
    uint32_t      crc32;
    uint32_t      issize;
    uint32_t      zipsize;
    mz_stream     stream;
    uint8_t       buffer[GZIP_DEFLATE_BUFF_SIZE];
    gzip_stream_out_t stream_out;
    void*         user_ctx;
} gzip_deflate_t;

typedef gzip_deflate_t* gzip_deflate_handle_t;

#if __cplusplus
extern "C" {
#endif

gzip_deflate_handle_t gzip_deflate_create(gzip_stream_out_t stream_out, void* user_ctx);
esp_err_t gzip_deflate_destroy(gzip_deflate_handle_t handle);
esp_err_t gzip_deflate_write(gzip_deflate_handle_t handle, uint8_t* data, uint32_t len, int is_finish);
esp_err_t gzip_deflate(uint8_t *in, int inlen, uint8_t *out, int *outlen);

#if __cplusplus
}
#endif
#endif // !__GZIP_DEFLATE_H__
