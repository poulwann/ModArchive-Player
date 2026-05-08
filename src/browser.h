#ifndef BROWSER_H
#define BROWSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BROWSER_MAX_TITLE    128
#define BROWSER_MAX_FILENAME 128
#define BROWSER_MAX_MODULES  40
#define BROWSER_MODULES_PER_PAGE 40
#define BROWSER_MAX_ARTIST_NAME 128

typedef enum {
    CHART_TOP_DOWNLOADS,
    CHART_TOP_SCORE,
    CHART_FEATURED,
    CHART_TOP_FAVOURITES,
    CHART_COUNT
} chart_type_t;

typedef struct {
    int rank;
    uint32_t module_id;
    char title[BROWSER_MAX_TITLE];
    char filename[BROWSER_MAX_FILENAME];
    uint32_t download_count;
    bool cached;
} module_entry_t;

typedef struct {
    uint32_t artist_id;
    char artist_name[BROWSER_MAX_ARTIST_NAME];
    module_entry_t artist_modules[BROWSER_MAX_MODULES];
    int artist_module_count;
    bool artist_found;
} artist_result_t;

typedef struct {
    chart_type_t current_chart;
    int current_page;
    int total_pages;
    module_entry_t modules[BROWSER_MAX_MODULES];
    int module_count;
    int selected_index;
    int hover_index;
    bool hover_active;
    int playing_index;
    bool loading;
    bool load_error;
    artist_result_t artist;
} browser_state_t;

void browser_init(browser_state_t *state);
bool browser_fetch_chart(browser_state_t *state, chart_type_t chart, int page);
bool browser_search(browser_state_t *state, const char *query, int page);
bool browser_search_artist(artist_result_t *result, const char *query);
bool browser_fetch_artist_modules(artist_result_t *result);
void browser_get_download_url(const module_entry_t *entry, char *url_buf, size_t buf_size);
const char *browser_chart_name(chart_type_t chart);

#endif
