#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include <stdbool.h>

bool http_init(void);
void http_cleanup(void);

typedef struct {
    char *data;
    size_t size;
} http_response_t;

typedef void (*http_progress_fn)(size_t downloaded, size_t total, void *user_data);

bool http_fetch_text(const char *url, http_response_t *response);

bool http_download_file(const char *url, const char *filepath,
                        http_progress_fn progress, void *user_data);

bool http_download_memory(const char *url, http_response_t *response,
                          http_progress_fn progress, void *user_data);

#endif
