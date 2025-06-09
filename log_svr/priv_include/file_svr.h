#ifndef __FILE_SVR_H__
#define __FILE_SVR_H__

#include "stdio.h"
#include "esp_err.h"

// file path max number
#define FILE_SVR_PATH_MAX_NUM    10

typedef struct {
    uint32_t cur_index;
    uint64_t m_modify_time[FILE_SVR_PATH_MAX_NUM];
    FILE*    m_fp;
} file_svr_desc_t;

esp_err_t file_svr_get_desc(file_svr_desc_t* desc);
esp_err_t file_svr_set_desc(file_svr_desc_t* desc);
esp_err_t file_svr_open(file_svr_desc_t* desc, char* mode);
esp_err_t file_svr_read(file_svr_desc_t* desc, uint8_t* buff, size_t* len);
esp_err_t file_svr_write(file_svr_desc_t* desc, uint8_t* buff, size_t len);
size_t file_svr_size(file_svr_desc_t* desc);
esp_err_t file_svr_close(file_svr_desc_t* desc);
esp_err_t file_svr_begin(file_svr_desc_t* desc);
esp_err_t file_svr_next(file_svr_desc_t* desc);
esp_err_t file_svr_update(file_svr_desc_t* desc, uint32_t index);


#endif // __FILE_SVR_H__
