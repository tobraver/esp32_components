#include "gzip_deflate.h"
#include "esp_log.h"

const static char* TAG = "gzip";

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
#define MZ_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MZ_MIN(a, b) (((a) < (b)) ? (a) : (b))

static int mz_deflateInit2(mz_streamp pStream, int level, int method, int window_bits, int mem_level, int strategy);
static int mz_deflate(mz_streamp pStream, int flush);
static int mz_deflateEnd(mz_streamp pStream);

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

static const mz_uint s_tdefl_num_probes[11] = { 0, 1, 6, 32, 16, 32, 128, 256, 512, 768, 1500 };

/* level may actually range from [0,10] (10 is a "hidden" max level, where we want a bit more compression and it's fine if throughput to fall off a cliff on some files). */
mz_uint tdefl_create_comp_flags_from_zip_params(int level, int window_bits, int strategy)
{
    mz_uint comp_flags = s_tdefl_num_probes[(level >= 0) ? MZ_MIN(10, level) : MZ_DEFAULT_LEVEL] | ((level <= 3) ? TDEFL_GREEDY_PARSING_FLAG : 0);
    if (window_bits > 0)
        comp_flags |= TDEFL_WRITE_ZLIB_HEADER;

    if (!level)
        comp_flags |= TDEFL_FORCE_ALL_RAW_BLOCKS;
    else if (strategy == MZ_FILTERED)
        comp_flags |= TDEFL_FILTER_MATCHES;
    else if (strategy == MZ_HUFFMAN_ONLY)
        comp_flags &= ~TDEFL_MAX_PROBES_MASK;
    else if (strategy == MZ_FIXED)
        comp_flags |= TDEFL_FORCE_ALL_STATIC_BLOCKS;
    else if (strategy == MZ_RLE)
        comp_flags |= TDEFL_RLE_MATCHES;

    return comp_flags;
}

static int mz_deflateInit2(mz_streamp pStream, int level, int method, int window_bits, int mem_level, int strategy)
{
    tdefl_compressor *pComp;
    mz_uint comp_flags = TDEFL_COMPUTE_ADLER32 | tdefl_create_comp_flags_from_zip_params(level, window_bits, strategy);

    if (!pStream)
        return MZ_STREAM_ERROR;
    if ((method != MZ_DEFLATED) || ((mem_level < 1) || (mem_level > 9)) || ((window_bits != MZ_DEFAULT_WINDOW_BITS) && (-window_bits != MZ_DEFAULT_WINDOW_BITS)))
        return MZ_PARAM_ERROR;

    pStream->data_type = 0;
    pStream->adler = MZ_ADLER32_INIT;
    pStream->msg = NULL;
    pStream->total_in = 0;
    pStream->total_out = 0;

    pComp = (tdefl_compressor *) calloc(1, sizeof(tdefl_compressor));
    if (!pComp)
        return MZ_MEM_ERROR;

    pStream->state = (struct mz_internal_state *)pComp;

    if (tdefl_init(pComp, NULL, NULL, comp_flags) != TDEFL_STATUS_OKAY)
    {
        mz_deflateEnd(pStream);
        return MZ_PARAM_ERROR;
    }

    return MZ_OK;
}

static int mz_deflate(mz_streamp pStream, int flush)
{
    size_t in_bytes, out_bytes;
    mz_ulong orig_total_in, orig_total_out;
    int mz_status = MZ_OK;

    if ((!pStream) || (!pStream->state) || (flush < 0) || (flush > MZ_FINISH) || (!pStream->next_out))
        return MZ_STREAM_ERROR;
    if (!pStream->avail_out)
        return MZ_BUF_ERROR;

    if (flush == MZ_PARTIAL_FLUSH)
        flush = MZ_SYNC_FLUSH;

    if (((tdefl_compressor *)pStream->state)->m_prev_return_status == TDEFL_STATUS_DONE)
        return (flush == MZ_FINISH) ? MZ_STREAM_END : MZ_BUF_ERROR;

    orig_total_in = pStream->total_in;
    orig_total_out = pStream->total_out;
    for (;;)
    {
        tdefl_status defl_status;
        in_bytes = pStream->avail_in;
        out_bytes = pStream->avail_out;

        defl_status = tdefl_compress((tdefl_compressor *)pStream->state, pStream->next_in, &in_bytes, pStream->next_out, &out_bytes, (tdefl_flush)flush);
        pStream->next_in += (mz_uint)in_bytes;
        pStream->avail_in -= (mz_uint)in_bytes;
        pStream->total_in += (mz_uint)in_bytes;
        pStream->adler = tdefl_get_adler32((tdefl_compressor *)pStream->state);

        pStream->next_out += (mz_uint)out_bytes;
        pStream->avail_out -= (mz_uint)out_bytes;
        pStream->total_out += (mz_uint)out_bytes;

        if (defl_status < 0)
        {
            mz_status = MZ_STREAM_ERROR;
            break;
        }
        else if (defl_status == TDEFL_STATUS_DONE)
        {
            mz_status = MZ_STREAM_END;
            break;
        }
        else if (!pStream->avail_out)
            break;
        else if ((!pStream->avail_in) && (flush != MZ_FINISH))
        {
            if ((flush) || (pStream->total_in != orig_total_in) || (pStream->total_out != orig_total_out))
                break;
            return MZ_BUF_ERROR; /* Can't make forward progress without some input.*/
        }
    }
    return mz_status;
}

static int mz_deflateEnd(mz_streamp pStream)
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

typedef enum {
    GZIP_OS_FATFileSystem = 0x00,
    GZIP_OS_Amiga = 0x01,
    GZIP_OS_VMS = 0x02,
    GZIP_OS_Unix = 0x03,
    GZIP_OS_VM_CMS = 0x04,
    GZIP_OS_AtariTOS = 0x05,
    GZIP_OS_HPFSFileSystem = 0x06,
    GZIP_OS_Macintosh = 0x07,
    GZIP_OS_ZSystem = 0x08,
    GZIP_OS_CP_M = 0x09,
    GZIP_OS_TOPS_20 = 0x0A,
    GZIP_OS_NTFSFileSystem = 0x0B,
    GZIP_OS_QDOS = 0x0C,
    GZIP_OS_AcordRISCOS = 0x0D,
    GZIP_OS_Unknown = 0xFF
} gzip_os_t;

gzip_deflate_handle_t gzip_deflate_create(gzip_stream_out_t stream_out, void* user_ctx)
{
    gzip_deflate_handle_t handle = (gzip_deflate_handle_t)malloc(sizeof(gzip_deflate_t));
    if(handle == NULL) {
        ESP_LOGE(TAG, "gzip deflate create malloc failed.");
        goto create_failed;
    }
    handle->header.id1 = 0x1F;
    handle->header.id2 = 0x8B;
    handle->header.compression = MZ_DEFLATED;
    handle->header.flags = 0;
    handle->header.mtime[0] = 0x00;
    handle->header.mtime[1] = 0x00;
    handle->header.mtime[2] = 0x00;
    handle->header.mtime[3] = 0x00;
    handle->header.extra_flags = 0;
    handle->header.os = GZIP_OS_Unknown;
    handle->crc32 = MZ_CRC32_INIT;
    handle->issize = 0;
    handle->zipsize = sizeof(handle->header) + sizeof(handle->crc32) + sizeof(handle->issize);

    memset(&handle->stream, 0, sizeof(handle->stream));
    if(mz_deflateInit2(&handle->stream, GZIP_DEFLATE_LEVEL, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != MZ_OK) {
        ESP_LOGE(TAG, "gzip deflate miniz stream init failed.");
        goto create_failed;
    }

    handle->user_ctx = user_ctx;
    handle->stream_out = stream_out;
    if(handle->stream_out) {
        if(handle->stream_out((uint8_t*)&handle->header, sizeof(handle->header), handle->user_ctx) != ESP_OK) {
            ESP_LOGE(TAG, "gzip deflate stream out failed.");
            goto create_failed;
        }
    }
    ESP_LOGI(TAG, "gzip deflate create success.");
    return handle;
create_failed:
    if(handle) {
        mz_deflateEnd(&handle->stream);
        free(handle);
        handle = NULL;
    }
    return handle;
}

esp_err_t gzip_deflate_destroy(gzip_deflate_handle_t handle)
{
    if(handle) {
        mz_deflateEnd(&handle->stream);
        free(handle);
    }
    ESP_LOGI(TAG, "gzip deflate destory success.");
    return ESP_OK;
}

esp_err_t gzip_deflate_write(gzip_deflate_handle_t handle, uint8_t* data, uint32_t len, int is_finish)
{
    int flush = is_finish ? MZ_FINISH : MZ_SYNC_FLUSH;
    uint32_t deflate_size = 0;
    if(handle == NULL) {
        ESP_LOGE(TAG, "gzip deflate handle is null.");
        return ESP_FAIL;
    }
    if(!is_finish && (data == NULL || len == 0)) {
        ESP_LOGE(TAG, "gzip deflate data is null or len is 0.");
        return ESP_FAIL;
    }

    handle->stream.next_in = data;
    handle->stream.avail_in = len;

    if(data && len) {
        handle->crc32 = mz_crc32(handle->crc32, data, len);
        handle->issize += len;
    }

    do {
        handle->stream.next_out = handle->buffer;
        handle->stream.avail_out = GZIP_DEFLATE_BUFF_SIZE;
        if(mz_deflate(&handle->stream, flush) < 0) {
            ESP_LOGE(TAG, "gzip deflate failed.");
            return ESP_FAIL;
        }
        deflate_size = GZIP_DEFLATE_BUFF_SIZE - handle->stream.avail_out;
        handle->zipsize += deflate_size;
        if(handle->stream_out) {
            if(handle->stream_out(handle->buffer, deflate_size, handle->user_ctx) != ESP_OK) {
                ESP_LOGE(TAG, "gzip deflate stream out failed.");
                return ESP_FAIL;
            }
        }
    } while(handle->stream.avail_out == 0);

    if(is_finish && handle->stream_out) {
        if(handle->stream_out((uint8_t*)&handle->crc32, sizeof(handle->crc32), handle->user_ctx) != ESP_OK) {
            ESP_LOGE(TAG, "gzip deflate stream out failed.");
            return ESP_FAIL;
        }
        if(handle->stream_out((uint8_t*)&handle->issize, sizeof(handle->issize), handle->user_ctx) != ESP_OK) {
            ESP_LOGE(TAG, "gzip deflate stream out failed.");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t gzip_deflate(uint8_t *in, int inlen, uint8_t *out, int *outlen)
{
    esp_err_t ret = ESP_FAIL;
    gzip_header_t header = { 0 };
    uint32_t crc32 = MZ_CRC32_INIT, issize = 0;
    uint32_t fmt_size = sizeof(gzip_header_t) + sizeof(crc32) + sizeof(issize);
    if(!in || !inlen || !out || !outlen || (*outlen <= fmt_size)) {
        ESP_LOGE(TAG, "gzip deflate param error");
        return ret;
    }
    mz_stream stream = { 0 };
    int status = mz_deflateInit2(&stream, GZIP_DEFLATE_LEVEL, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY);
    if(status != MZ_OK) {
        ESP_LOGE(TAG, "gzip deflate init error, reason: %s", mz_error(status));
        return ret;
    }
    stream.next_in = in;
    stream.avail_in = inlen;
    stream.next_out = out + sizeof(header);
    stream.avail_out = *outlen - fmt_size;
    status = mz_deflate(&stream, MZ_FINISH);
    if(status != MZ_STREAM_END) {
        ESP_LOGE(TAG, "gzip deflate error, reason: %s", mz_error(status));
    } else {
        ret = ESP_OK;
        header.id1 = 0x1F;
        header.id2 = 0x8B;
        header.compression = MZ_DEFLATED;
        header.flags = 0;
        header.mtime[0] = 0x00;
        header.mtime[1] = 0x00;
        header.mtime[2] = 0x00;
        header.mtime[3] = 0x00;
        header.extra_flags = 0;
        header.os = GZIP_OS_Unknown;
        crc32 = mz_crc32(crc32, in, inlen);
        issize = inlen;
        memcpy(out, &header, sizeof(header));
        memcpy(out + sizeof(header) + stream.total_out, &crc32, sizeof(crc32));
        memcpy(out + sizeof(header) + stream.total_out + sizeof(crc32), &issize, sizeof(issize));
        *outlen = stream.total_out + fmt_size;
    }
    mz_deflateEnd(&stream);
    ESP_LOGI(TAG, "gzip deflate finish");
    return ret;
}
