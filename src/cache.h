#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

bool cache_init(void);
bool cache_has(uint32_t module_id);

bool cache_get_path(uint32_t module_id, const char *filename,
                    char *path_buf, size_t buf_size);

bool cache_store(uint32_t module_id, const char *filename,
                 const void *data, size_t data_size,
                 char *path_buf, size_t buf_size);

bool cache_store_file(uint32_t module_id, const char *filename,
                      const char *src_path,
                      char *path_buf, size_t buf_size);

const char *cache_get_dir(void);

typedef struct {
    uint32_t module_id;
    char filename[128];
} cache_entry_t;

int cache_list(cache_entry_t *entries, int max_entries);

#endif
