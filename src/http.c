#include "http.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define USER_AGENT "mod_player/0.1 (Linux; github.com/mod_player)"

static bool g_initialized = false;

bool http_init(void) {
    if (g_initialized) return true;
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        fprintf(stderr, "http: curl_global_init failed: %s\n", curl_easy_strerror(res));
        return false;
    }
    g_initialized = true;
    return true;
}

void http_cleanup(void) {
    if (g_initialized) {
        curl_global_cleanup();
        g_initialized = false;
    }
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    http_response_t *resp = (http_response_t *)userp;

    if (nmemb > 0 && size > SIZE_MAX / nmemb) return 0;
    size_t real_size = size * nmemb;
    if (real_size > SIZE_MAX - resp->size - 1) return 0;

    char *ptr = realloc(resp->data, resp->size + real_size + 1);
    if (!ptr) {
        fprintf(stderr, "http: out of memory\n");
        return 0;
    }

    resp->data = ptr;
    memcpy(&resp->data[resp->size], contents, real_size);
    resp->size += real_size;
    resp->data[resp->size] = '\0';
    return real_size;
}

typedef struct {
    http_progress_fn fn;
    void *user_data;
} progress_ctx_t;

static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    progress_ctx_t *ctx = (progress_ctx_t *)clientp;
    if (ctx->fn) {
        ctx->fn((size_t)dlnow, (size_t)dltotal, ctx->user_data);
    }
    return 0;
}

bool http_fetch_text(const char *url, http_response_t *response) {
    if (!g_initialized || !url || !response) return false;

    response->data = NULL;
    response->size = 0;

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "http: fetch failed for %s: %s\n", url, curl_easy_strerror(res));
        free(response->data);
        response->data = NULL;
        response->size = 0;
        return false;
    }

    return true;
}

bool http_download_file(const char *url, const char *filepath,
                        http_progress_fn progress_fn, void *user_data) {
    if (!g_initialized || !url || !filepath) return false;

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        fprintf(stderr, "http: cannot open file for writing: %s\n", filepath);
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        return false;
    }

    progress_ctx_t pctx = { .fn = progress_fn, .user_data = user_data };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    if (progress_fn) {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pctx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) {
        fprintf(stderr, "http: download failed for %s: %s\n", url, curl_easy_strerror(res));
        remove(filepath);
        return false;
    }

    return true;
}

bool http_download_memory(const char *url, http_response_t *response,
                          http_progress_fn progress_fn, void *user_data) {
    if (!g_initialized || !url || !response) return false;

    response->data = NULL;
    response->size = 0;

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    progress_ctx_t pctx = { .fn = progress_fn, .user_data = user_data };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    if (progress_fn) {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pctx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "http: download failed for %s: %s\n", url, curl_easy_strerror(res));
        free(response->data);
        response->data = NULL;
        response->size = 0;
        return false;
    }

    return true;
}
