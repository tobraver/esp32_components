#ifndef __MEM_H__
#define __MEM_H__

#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"

typedef struct
{
    char* part_name;
    char* namespace;
} prefs_t;

bool prefs_init(prefs_t hprefs);

bool prefs_erase_partition(prefs_t hprefs);
bool prefs_erase_namespace(prefs_t hprefs);
bool prefs_erase_key(prefs_t hprefs, char* key);

bool prefs_write_u32(prefs_t hprefs, char* key, uint32_t value);
bool prefs_read_u32(prefs_t hprefs, char* key, uint32_t* value);

bool prefs_write_u64(prefs_t hprefs, char* key, uint64_t value);
bool prefs_read_u64(prefs_t hprefs, char* key, uint64_t* value);

bool prefs_write_block(prefs_t hprefs, char* key, void* buff, uint32_t size);
bool prefs_read_block(prefs_t hprefs, char* key, void* buff, uint32_t size);


#endif // !__MEM_H__
