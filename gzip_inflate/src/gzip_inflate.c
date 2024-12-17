#include "gzip_inflate.h"

#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "stdbool.h"
#include "esp_idf_version.h"
#if (ESP_IDF_VERSION_MAJOR == 4) && (ESP_IDF_VERSION_MINOR < 3)
#include "esp32/rom/miniz.h"
#else
#include "rom/miniz.h"
#endif
#include "esp_err.h"
#include "esp_log.h"

// Add the API missing in miniz of ROM code
// Original code can be gotten from: https://github.com/richgel999/miniz/blob/master/miniz.c
#ifdef MINIZ_NO_ZLIB_APIS

// Flush values. For typical usage you only need MZ_NO_FLUSH and MZ_FINISH. The other values are for advanced use (refer
// to the zlib docs).
enum { MZ_NO_FLUSH = 0, MZ_PARTIAL_FLUSH = 1, MZ_SYNC_FLUSH = 2, MZ_FULL_FLUSH = 3, MZ_FINISH = 4, MZ_BLOCK = 5 };

// Return status codes. MZ_PARAM_ERROR is non-standard.
enum {
    MZ_OK = 0,
    MZ_STREAM_END = 1,
    MZ_NEED_DICT = 2,
    MZ_ERRNO = -1,
    MZ_STREAM_ERROR = -2,
    MZ_DATA_ERROR = -3,
    MZ_MEM_ERROR = -4,
    MZ_BUF_ERROR = -5,
    MZ_VERSION_ERROR = -6,
    MZ_PARAM_ERROR = -10000
};

// Compression levels: 0-9 are the standard zlib-style levels, 10 is best possible compression (not zlib compatible, and
// may be very slow), MZ_DEFAULT_COMPRESSION=MZ_DEFAULT_LEVEL.
enum {
    MZ_NO_COMPRESSION = 0,
    MZ_BEST_SPEED = 1,
    MZ_BEST_COMPRESSION = 9,
    MZ_UBER_COMPRESSION = 10,
    MZ_DEFAULT_LEVEL = 6,
    MZ_DEFAULT_COMPRESSION = -1
};

// Window bits
#define MZ_DEFAULT_WINDOW_BITS 15
#define MZ_MIN(a, b)           (a > b ? b : a)

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

typedef struct {
    tinfl_decompressor m_decomp;
    mz_uint            m_dict_ofs, m_dict_avail, m_first_call, m_has_flushed;
    int                m_window_bits;
    mz_uint8           m_dict[TINFL_LZ_DICT_SIZE];
    tinfl_status       m_last_status;
} inflate_state;

static const char *mz_error(int err)
{
    static struct
    {
        int m_err;
        const char *m_pDesc;
    } s_error_descs[] =
        {
          { MZ_OK, "" }, { MZ_STREAM_END, "stream end" }, { MZ_NEED_DICT, "need dictionary" }, { MZ_ERRNO, "file error" }, { MZ_STREAM_ERROR, "stream error" }, { MZ_DATA_ERROR, "data error" }, { MZ_MEM_ERROR, "out of memory" }, { MZ_BUF_ERROR, "buf error" }, { MZ_VERSION_ERROR, "version error" }, { MZ_PARAM_ERROR, "parameter error" }
        };
    mz_uint i;
    for (i = 0; i < sizeof(s_error_descs) / sizeof(s_error_descs[0]); ++i)
        if (s_error_descs[i].m_err == err)
            return s_error_descs[i].m_pDesc;
    return "";
}

static int mz_inflateInit2(mz_streamp pStream, int window_bits)
{
    inflate_state *pDecomp;
    if (!pStream)
        return MZ_STREAM_ERROR;
    if ((window_bits != MZ_DEFAULT_WINDOW_BITS) && (-window_bits != MZ_DEFAULT_WINDOW_BITS))
        return MZ_PARAM_ERROR;

    pStream->data_type = 0;
    pStream->adler = 0;
    pStream->crc32 = 0;
    pStream->msg = NULL;
    pStream->total_in = 0;
    pStream->total_out = 0;

    pDecomp = (inflate_state *) calloc(1, sizeof(inflate_state));
    if (!pDecomp)
        return MZ_MEM_ERROR;

    pStream->state = (struct mz_internal_state *) pDecomp;

    tinfl_init(&pDecomp->m_decomp);
    pDecomp->m_dict_ofs = 0;
    pDecomp->m_dict_avail = 0;
    pDecomp->m_last_status = TINFL_STATUS_NEEDS_MORE_INPUT;
    pDecomp->m_first_call = 1;
    pDecomp->m_has_flushed = 0;
    pDecomp->m_window_bits = window_bits;

    return MZ_OK;
}

static int mz_inflate(mz_streamp pStream, int flush)
{
    inflate_state *pState;
    mz_uint n, first_call, decomp_flags = TINFL_FLAG_COMPUTE_ADLER32;
    size_t in_bytes, out_bytes, orig_avail_in;
    tinfl_status status;

    if ((!pStream) || (!pStream->state))
        return MZ_STREAM_ERROR;
    if (flush == MZ_PARTIAL_FLUSH)
        flush = MZ_SYNC_FLUSH;
    if ((flush) && (flush != MZ_SYNC_FLUSH) && (flush != MZ_FINISH))
        return MZ_STREAM_ERROR;

    pState = (inflate_state *) pStream->state;
    if (pState->m_window_bits > 0)
        decomp_flags |= TINFL_FLAG_PARSE_ZLIB_HEADER;
    orig_avail_in = pStream->avail_in;

    first_call = pState->m_first_call;
    pState->m_first_call = 0;
    if (pState->m_last_status < 0)
        return MZ_DATA_ERROR;

    if (pState->m_has_flushed && (flush != MZ_FINISH))
        return MZ_STREAM_ERROR;
    pState->m_has_flushed |= (flush == MZ_FINISH);

    if ((flush == MZ_FINISH) && (first_call)) {
        // MZ_FINISH on the first call implies that the input and output buffers are large enough to hold the entire
        // compressed/decompressed file.
        decomp_flags |= TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;
        in_bytes = pStream->avail_in;
        out_bytes = pStream->avail_out;
        status = tinfl_decompress(&pState->m_decomp, pStream->next_in, &in_bytes, pStream->next_out, pStream->next_out,
                                  &out_bytes, decomp_flags);
        pState->m_last_status = status;
        pStream->next_in += (mz_uint) in_bytes;
        pStream->avail_in -= (mz_uint) in_bytes;
        pStream->total_in += (mz_uint) in_bytes;
        pStream->adler = tinfl_get_adler32(&pState->m_decomp);
        pStream->next_out += (mz_uint) out_bytes;
        pStream->avail_out -= (mz_uint) out_bytes;
        pStream->total_out += (mz_uint) out_bytes;

        if (status < 0)
            return MZ_DATA_ERROR;
        else if (status != TINFL_STATUS_DONE) {
            pState->m_last_status = TINFL_STATUS_FAILED;
            return MZ_BUF_ERROR;
        }
        return MZ_STREAM_END;
    }
    // flush != MZ_FINISH then we must assume there's more input.
    if (flush != MZ_FINISH)
        decomp_flags |= TINFL_FLAG_HAS_MORE_INPUT;

    if (pState->m_dict_avail) {
        n = MZ_MIN(pState->m_dict_avail, pStream->avail_out);
        memcpy(pStream->next_out, pState->m_dict + pState->m_dict_ofs, n);
        pStream->next_out += n;
        pStream->avail_out -= n;
        pStream->total_out += n;
        pState->m_dict_avail -= n;
        pState->m_dict_ofs = (pState->m_dict_ofs + n) & (TINFL_LZ_DICT_SIZE - 1);
        return ((pState->m_last_status == TINFL_STATUS_DONE) && (!pState->m_dict_avail)) ? MZ_STREAM_END : MZ_OK;
    }

    for (;;) {
        in_bytes = pStream->avail_in;
        out_bytes = TINFL_LZ_DICT_SIZE - pState->m_dict_ofs;

        status = tinfl_decompress(&pState->m_decomp, pStream->next_in, &in_bytes, pState->m_dict,
                                  pState->m_dict + pState->m_dict_ofs, &out_bytes, decomp_flags);
        pState->m_last_status = status;

        pStream->next_in += (mz_uint) in_bytes;
        pStream->avail_in -= (mz_uint) in_bytes;
        pStream->total_in += (mz_uint) in_bytes;
        pStream->adler = tinfl_get_adler32(&pState->m_decomp);

        pState->m_dict_avail = (mz_uint) out_bytes;

        n = MZ_MIN(pState->m_dict_avail, pStream->avail_out);
        memcpy(pStream->next_out, pState->m_dict + pState->m_dict_ofs, n);
        pStream->next_out += n;
        pStream->avail_out -= n;
        pStream->total_out += n;
        pState->m_dict_avail -= n;
        pState->m_dict_ofs = (pState->m_dict_ofs + n) & (TINFL_LZ_DICT_SIZE - 1);

        if (status < 0)
            return MZ_DATA_ERROR; // Stream is corrupted (there could be some uncompressed data left in the output
                                  // dictionary - oh well).
        else if ((status == TINFL_STATUS_NEEDS_MORE_INPUT) && (!orig_avail_in))
            return MZ_BUF_ERROR; // Signal caller that we can't make forward progress without supplying more input or by
                                 // setting flush to MZ_FINISH.
        else if (flush == MZ_FINISH) {
            // The output buffer MUST be large to hold the remaining uncompressed data when flush==MZ_FINISH.
            if (status == TINFL_STATUS_DONE)
                return pState->m_dict_avail ? MZ_BUF_ERROR : MZ_STREAM_END;
            // status here must be TINFL_STATUS_HAS_MORE_OUTPUT, which means there's at least 1 more byte on the way. If
            // there's no more room left in the output buffer then something is wrong.
            else if (!pStream->avail_out)
                return MZ_BUF_ERROR;
        } else if ((status == TINFL_STATUS_DONE) || (!pStream->avail_in) || (!pStream->avail_out) ||
                   (pState->m_dict_avail))
            break;
    }

    return ((status == TINFL_STATUS_DONE) && (!pState->m_dict_avail)) ? MZ_STREAM_END : MZ_OK;
}

static int mz_inflateEnd(mz_streamp pStream)
{
    if (!pStream)
        return MZ_STREAM_ERROR;
    if (pStream->state) {
        free(pStream->state);
        pStream->state = NULL;
    }
    return MZ_OK;
}

#endif // MINIZ_NO_ZLIB_APIS

static const char* TAG = "GZIP_INFLATE";
#define GZIP_HEADER_SIZE (10)

typedef struct {
    int              head_flag;
    int              extra_len;
    int              head_filled;
    mz_stream        stream;
} gzip_inflate_t;

static bool _gzip_verify_header(gzip_inflate_t *gzip, uint8_t *data, int size)
{
    if (size < GZIP_HEADER_SIZE) {
        return false;
    }
    if (data[0] != 0x1F || data[1] != 0x8B || data[2] != 0x8) {
        return false;
    }
    gzip->head_flag = data[3];
    return true;
}

static int _gzip_skip_header(gzip_inflate_t *gzip, uint8_t *data, int size)
{
    if (gzip->head_flag == 0) {
        return 0;
    }
    int org_size = size;
    if (gzip->head_flag & 4) {
        // 2 bytes extra len
        if (gzip->head_filled > 2) {
            return -1;
        }
        if (gzip->head_filled + size >= 2) {
            int used = 2 - gzip->head_filled;
            if (used) {
                size -= used;
                while (used--) {
                    gzip->extra_len = gzip->extra_len + (*data << (gzip->head_filled * 8));
                    gzip->head_filled++;
                    data++;
                }
            }
            if (size >= gzip->extra_len) {
                size -= gzip->extra_len;
                data += gzip->extra_len;
                gzip->extra_len = 0;
                gzip->head_flag &= ~4;
                gzip->head_filled = 0;
            } else {
                gzip->extra_len -= size;
                return org_size;
            }
        } else {
            for (int i = 0; i < size; i++) {
                gzip->extra_len = gzip->extra_len + (*data << (gzip->head_filled * 8));
                gzip->head_filled++;
                data++;
            }
            gzip->head_filled += size;
            return org_size;
        }
    }
    if (gzip->head_flag & 8) {
        // name
        while (size) {
            size--;
            data++;
            if (*(data - 1) == '\0') {
                gzip->head_flag &= ~8;
                break;
            }
        }
        if (size == 0) {
            return org_size;
        }
    }
    if (gzip->head_flag & 0x10) {
        // comment
        while (size) {
            size--;
            data++;
            if (*(data - 1) == '\0') {
                gzip->head_flag &= ~0x10;
                break;
            }
        }
        if (size == 0) {
            return org_size;
        }
    }
    if (gzip->head_flag & 0x2) {
        // CRC16
        if (gzip->head_filled > 2) {
            return -1;
        }
        if (gzip->head_filled + size >= 2) {
            int used = 2 - gzip->head_filled;
            size -= used;
            data += used;
            gzip->head_flag &= ~0x2;
        } else {
            gzip->head_filled += size;
            return org_size;
        }
    }
    return org_size - size;
}

esp_err_t gzip_inflate(uint8_t *in, int inlen, uint8_t *out, int *outlen)
{
    esp_err_t ret = ESP_FAIL;
    gzip_inflate_t gzip = { 0 };
    if (!in || (inlen < GZIP_HEADER_SIZE) || !out || !outlen || (*outlen <= 0)) {
        ESP_LOGE(TAG, "gzip inflate params error");
        return ret;
    }
    if(!_gzip_verify_header(&gzip, in, inlen)) {
        ESP_LOGE(TAG, "gzip inflate header error");
        return ret;
    }
    int skip_size = _gzip_skip_header(&gzip, in + GZIP_HEADER_SIZE, inlen - GZIP_HEADER_SIZE);
    int header_size = GZIP_HEADER_SIZE + skip_size;
    if(header_size >= inlen) {
        ESP_LOGE(TAG, "gzip inflate header size error");
        return ret;
    }
    int status = mz_inflateInit2(&gzip.stream, -MZ_DEFAULT_WINDOW_BITS);
    if(status != MZ_OK) {
        ESP_LOGE(TAG, "gzip inflate init error, reason: %s", mz_error(status));
        return ret;
    }
    gzip.stream.next_in = (uint8_t *)in + header_size;
    gzip.stream.avail_in = inlen - header_size;
    gzip.stream.next_out = out;
    gzip.stream.avail_out = *outlen;
    status = mz_inflate(&gzip.stream, MZ_FINISH);
    if(status != MZ_STREAM_END) {
        ESP_LOGE(TAG, "gzip inflate error, reason: %s", mz_error(status));
    } else {
        ret = ESP_OK;
        *outlen = gzip.stream.total_out;
        ESP_LOGI(TAG, "gzip inflate success");
    }
    mz_inflateEnd(&gzip.stream);
    ESP_LOGI(TAG, "gzip inflate finish");
    return ret;
}

