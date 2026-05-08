#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#define CACHE_SUBDIR "mod_player/modules"

static char g_cache_dir[512] = {0};
static bool g_initialized = false;

static bool ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *p = tmp + 1;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
        p++;
    }
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool cache_init(void) {
    if (g_initialized) return true;

    const char *cache_home = getenv("XDG_CACHE_HOME");
    if (cache_home && cache_home[0]) {
        snprintf(g_cache_dir, sizeof(g_cache_dir), "%s/%s", cache_home, CACHE_SUBDIR);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "cache: cannot determine home directory\n");
            return false;
        }
        snprintf(g_cache_dir, sizeof(g_cache_dir), "%s/.cache/%s", home, CACHE_SUBDIR);
    }

    if (!ensure_directory(g_cache_dir)) {
        fprintf(stderr, "cache: cannot create directory: %s\n", g_cache_dir);
        return false;
    }

    g_initialized = true;
    return true;
}

const char *cache_get_dir(void) {
    return g_cache_dir;
}

static void build_cache_path(uint32_t module_id, const char *filename,
                             char *path_buf, size_t buf_size) {
    snprintf(path_buf, buf_size, "%s/%u_%s", g_cache_dir, module_id, filename);
}

bool cache_has(uint32_t module_id) {
    if (!g_initialized) return false;

    DIR *dir = opendir(g_cache_dir);
    if (!dir) return false;

    char prefix[32];
    snprintf(prefix, sizeof(prefix), "%u_", module_id);
    size_t prefix_len = strlen(prefix);

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, prefix, prefix_len) == 0) {
            closedir(dir);
            return true;
        }
    }
    closedir(dir);
    return false;
}

bool cache_get_path(uint32_t module_id, const char *filename,
                    char *path_buf, size_t buf_size) {
    if (!g_initialized) return false;

    if (filename && filename[0]) {
        build_cache_path(module_id, filename, path_buf, buf_size);
        struct stat st;
        if (stat(path_buf, &st) == 0 && S_ISREG(st.st_mode)) {
            return true;
        }
    }

    DIR *dir = opendir(g_cache_dir);
    if (!dir) return false;

    char prefix[32];
    snprintf(prefix, sizeof(prefix), "%u_", module_id);
    size_t prefix_len = strlen(prefix);

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, prefix, prefix_len) == 0) {
            snprintf(path_buf, buf_size, "%s/%s", g_cache_dir, ent->d_name);
            closedir(dir);
            return true;
        }
    }
    closedir(dir);
    return false;
}

bool cache_store(uint32_t module_id, const char *filename,
                 const void *data, size_t data_size,
                 char *path_buf, size_t buf_size) {
    if (!g_initialized || !data || data_size == 0) return false;

    build_cache_path(module_id, filename, path_buf, buf_size);

    FILE *fp = fopen(path_buf, "wb");
    if (!fp) {
        fprintf(stderr, "cache: cannot write file: %s\n", path_buf);
        return false;
    }

    size_t written = fwrite(data, 1, data_size, fp);
    fclose(fp);

    if (written != data_size) {
        fprintf(stderr, "cache: incomplete write to: %s\n", path_buf);
        remove(path_buf);
        return false;
    }

    return true;
}

int cache_list(cache_entry_t *entries, int max_entries) {
    if (!g_initialized || !entries || max_entries <= 0) return 0;

    DIR *dir = opendir(g_cache_dir);
    if (!dir) return 0;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max_entries) {
        if (ent->d_name[0] == '.') continue;
        char *underscore = strchr(ent->d_name, '_');
        if (!underscore) continue;

        entries[count].module_id = (uint32_t)strtoul(ent->d_name, NULL, 10);
        if (entries[count].module_id == 0) continue;

        const char *fname = underscore + 1;
        strncpy(entries[count].filename, fname, sizeof(entries[count].filename) - 1);
        entries[count].filename[sizeof(entries[count].filename) - 1] = '\0';
        count++;
    }
    closedir(dir);
    return count;
}

bool cache_store_file(uint32_t module_id, const char *filename,
                      const char *src_path,
                      char *path_buf, size_t buf_size) {
    if (!g_initialized || !src_path) return false;

    build_cache_path(module_id, filename, path_buf, buf_size);

    if (rename(src_path, path_buf) == 0) {
        return true;
    }

    FILE *src = fopen(src_path, "rb");
    if (!src) return false;

    FILE *dst = fopen(path_buf, "wb");
    if (!dst) {
        fclose(src);
        return false;
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(src);
            fclose(dst);
            remove(path_buf);
            return false;
        }
    }

    fclose(src);
    fclose(dst);
    remove(src_path);
    return true;
}
