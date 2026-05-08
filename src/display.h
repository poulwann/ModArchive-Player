#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include "browser.h"
#include "player.h"

typedef enum {
    VIEW_BROWSER,
    VIEW_DOWNLOADING,
    VIEW_PLAYER,
} view_state_t;

typedef struct {
    const char *filename;
    size_t downloaded;
    size_t total;
    bool complete;
    bool error;
} download_progress_t;

typedef struct {
    int playlist_y_start;
    int playlist_y_end;
    int playlist_row_h;
    int playlist_list_y;
    int playlist_start;
    int playlist_max_visible;
    int browser_list_y_start;
    int browser_list_y_end;
    int browser_row_h;
    int browser_start;
    int browser_max_visible;
    bool browser_artist_row;
    int browser_artist_row_y;
} display_layout_t;

bool display_init(void);
void display_cleanup(void);
void display_render_browser(const browser_state_t *browser);
void display_render_download(const download_progress_t *progress);
void display_render_player(const player_info_t *info, const browser_state_t *browser);
void display_render_search_input(const char *query);
void display_present(void);
void display_clear(void);
void display_get_size(int *w, int *h);
const display_layout_t *display_get_layout(void);

#endif
