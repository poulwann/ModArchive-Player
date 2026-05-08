#include "audio.h"
#include "browser.h"
#include "cache.h"
#include "display.h"
#include "http.h"
#include "player.h"
#include "shader.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

typedef enum {
    APP_BROWSER,
    APP_DOWNLOADING,
    APP_PLAYING,
    APP_SEARCH_INPUT,
    APP_QUIT,
} app_state_t;

static char g_search_buf[128] = {0};
static int g_search_len = 0;

static atomic_size_t g_dl_downloaded = 0;
static atomic_size_t g_dl_total = 0;
static atomic_int g_dl_complete = 0;
static atomic_int g_dl_error = 0;
static char g_dl_filename[128] = {0};

static void download_progress(size_t downloaded, size_t total, void *user_data) {
    (void)user_data;
    atomic_store(&g_dl_downloaded, downloaded);
    atomic_store(&g_dl_total, total);
}

static bool download_and_play(const module_entry_t *entry) {
    char path_buf[512];

    if (cache_get_path(entry->module_id, entry->filename, path_buf, sizeof(path_buf))) {
        return player_load_file(path_buf);
    }

    char url[512];
    browser_get_download_url(entry, url, sizeof(url));

    atomic_store(&g_dl_downloaded, 0);
    atomic_store(&g_dl_total, 0);
    atomic_store(&g_dl_complete, 0);
    atomic_store(&g_dl_error, 0);
    strncpy(g_dl_filename, entry->filename, sizeof(g_dl_filename) - 1);

    http_response_t response = {0};
    if (!http_download_memory(url, &response, download_progress, NULL)) {
        atomic_store(&g_dl_error, 1);
        return false;
    }

    atomic_store(&g_dl_complete, 1);

    cache_store(entry->module_id, entry->filename,
                response.data, response.size, path_buf, sizeof(path_buf));

    bool ok = player_load_memory(response.data, response.size);
    free(response.data);
    return ok;
}

static void update_cache_status(browser_state_t *browser) {
    for (int i = 0; i < browser->module_count; i++) {
        browser->modules[i].cached = cache_has(browser->modules[i].module_id);
    }
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] [file.mod]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --chart=TYPE   Start with chart (tophits, topscore, featured, favourites)\n");
    fprintf(stderr, "  --help         Show this help\n");
    fprintf(stderr, "\nIf no file is given, opens the ModArchive browser.\n");
}

int main(int argc, char *argv[]) {
    const char *file_arg = NULL;
    chart_type_t initial_chart = CHART_TOP_DOWNLOADS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strncmp(argv[i], "--chart=", 8) == 0) {
            const char *chart_str = argv[i] + 8;
            if (strcmp(chart_str, "tophits") == 0) initial_chart = CHART_TOP_DOWNLOADS;
            else if (strcmp(chart_str, "topscore") == 0) initial_chart = CHART_TOP_SCORE;
            else if (strcmp(chart_str, "featured") == 0) initial_chart = CHART_FEATURED;
            else if (strcmp(chart_str, "favourites") == 0) initial_chart = CHART_TOP_FAVOURITES;
            else {
                fprintf(stderr, "Unknown chart type: %s\n", chart_str);
                return 1;
            }
        } else if (argv[i][0] != '-') {
            file_arg = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (!http_init()) {
        fprintf(stderr, "Failed to initialize HTTP\n");
        SDL_Quit();
        return 1;
    }

    if (!cache_init()) {
        fprintf(stderr, "Failed to initialize cache\n");
        http_cleanup();
        SDL_Quit();
        return 1;
    }

    if (!display_init()) {
        fprintf(stderr, "Failed to initialize display\n");
        http_cleanup();
        SDL_Quit();
        return 1;
    }

    if (!audio_init()) {
        fprintf(stderr, "Failed to initialize audio\n");
        display_cleanup();
        http_cleanup();
        SDL_Quit();
        return 1;
    }

    app_state_t app_state = APP_BROWSER;
    browser_state_t browser;
    browser_init(&browser);
    browser.playing_index = -1;

    if (file_arg) {
        if (player_load_file(file_arg)) {
            player_play();
            audio_start();
            app_state = APP_PLAYING;
        } else {
            fprintf(stderr, "Failed to load: %s\n", file_arg);
            app_state = APP_QUIT;
        }
    } else {
        browser_fetch_chart(&browser, initial_chart, 1);
        update_cache_status(&browser);
    }

    while (app_state != APP_QUIT) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                app_state = APP_QUIT;
                break;
            }

            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode key = event.key.keysym.sym;

                if (key == SDLK_F3) {
                    shader_set_enabled(!shader_is_enabled());
                }

                if (key == SDLK_F1 && app_state != APP_SEARCH_INPUT) {
                    if (app_state == APP_PLAYING) {
                        audio_stop();
                        player_stop();
                        player_unload();
                        browser.playing_index = -1;
                    }
                    g_search_buf[0] = '\0';
                    g_search_len = 0;
                    SDL_StartTextInput();
                    app_state = APP_SEARCH_INPUT;
                }

                if (key == SDLK_F2) {
                    if (app_state == APP_SEARCH_INPUT) {
                        SDL_StopTextInput();
                    }
                    if (app_state == APP_PLAYING) {
                        audio_stop();
                        player_stop();
                        player_unload();
                        browser.playing_index = -1;
                    }
                    cache_entry_t cache_entries[BROWSER_MAX_MODULES];
                    int n = cache_list(cache_entries, BROWSER_MAX_MODULES);
                    browser.module_count = n;
                    browser.selected_index = 0;
                    browser.current_page = 1;
                    browser.total_pages = 1;
                    for (int ci = 0; ci < n; ci++) {
                        memset(&browser.modules[ci], 0, sizeof(module_entry_t));
                        browser.modules[ci].module_id = cache_entries[ci].module_id;
                        strncpy(browser.modules[ci].filename, cache_entries[ci].filename,
                                BROWSER_MAX_FILENAME - 1);
                        strncpy(browser.modules[ci].title, cache_entries[ci].filename,
                                BROWSER_MAX_TITLE - 1);
                        browser.modules[ci].rank = ci + 1;
                        browser.modules[ci].cached = true;
                    }
                    app_state = APP_BROWSER;
                }

                switch (app_state) {
                case APP_BROWSER:
                    switch (key) {
                    case SDLK_q:
                        app_state = APP_QUIT;
                        break;
                    case SDLK_c:
                        if (event.key.keysym.mod & KMOD_CTRL)
                            app_state = APP_QUIT;
                        break;
                    case SDLK_UP:
                        if (browser.selected_index > 0)
                            browser.selected_index--;
                        else if (browser.selected_index == 0 &&
                                 browser.artist.artist_found &&
                                 browser.artist.artist_module_count > 0)
                            browser.selected_index = -1;
                        break;
                    case SDLK_DOWN:
                        if (browser.selected_index == -1)
                            browser.selected_index = 0;
                        else if (browser.selected_index < browser.module_count - 1)
                            browser.selected_index++;
                        break;
                    case SDLK_PAGEUP:
                        if (browser.current_page > 1) {
                            browser_fetch_chart(&browser, browser.current_chart,
                                               browser.current_page - 1);
                            update_cache_status(&browser);
                            browser.selected_index = 0;
                        }
                        break;
                    case SDLK_PAGEDOWN:
                        if (browser.current_page < browser.total_pages) {
                            browser_fetch_chart(&browser, browser.current_chart,
                                               browser.current_page + 1);
                            update_cache_status(&browser);
                            browser.selected_index = 0;
                        }
                        break;
                    case SDLK_TAB: {
                        int next = ((int)browser.current_chart + 1) % CHART_COUNT;
                        browser_fetch_chart(&browser, (chart_type_t)next, 1);
                        update_cache_status(&browser);
                        browser.selected_index = 0;
                        break;
                    }
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (browser.selected_index == -1 &&
                            browser.artist.artist_found &&
                            browser.artist.artist_module_count > 0) {
                            browser.module_count = browser.artist.artist_module_count;
                            memcpy(browser.modules, browser.artist.artist_modules,
                                   sizeof(module_entry_t) * browser.artist.artist_module_count);
                            browser.selected_index = 0;
                            update_cache_status(&browser);
                            memset(&browser.artist, 0, sizeof(browser.artist));
                        } else if (browser.module_count > 0 &&
                                   browser.selected_index >= 0 &&
                                   browser.selected_index < browser.module_count) {
                            const module_entry_t *entry = &browser.modules[browser.selected_index];
                            char path_buf[512];
                            if (cache_get_path(entry->module_id, entry->filename,
                                               path_buf, sizeof(path_buf))) {
                                if (player_load_file(path_buf)) {
                                    player_play();
                                    audio_start();
                                    browser.playing_index = browser.selected_index;
                                    app_state = APP_PLAYING;
                                }
                            } else {
                                app_state = APP_DOWNLOADING;
                            }
                        }
                        break;
                    case SDLK_HOME:
                        browser.selected_index = 0;
                        break;
                    case SDLK_END:
                        browser.selected_index = browser.module_count - 1;
                        break;
                    case SDLK_a:
                        if (browser.artist.artist_found && browser.artist.artist_module_count > 0) {
                            browser.module_count = browser.artist.artist_module_count;
                            memcpy(browser.modules, browser.artist.artist_modules,
                                   sizeof(module_entry_t) * browser.artist.artist_module_count);
                            browser.selected_index = 0;
                            memset(&browser.artist, 0, sizeof(browser.artist));
                        }
                        if (browser.module_count > 0) {
                            for (int di = 0; di < browser.module_count; di++) {
                                module_entry_t *entry = &browser.modules[di];
                                if (!cache_has(entry->module_id)) {
                                    char url[512];
                                    browser_get_download_url(entry, url, sizeof(url));
                                    http_response_t resp = {0};
                                    if (http_download_memory(url, &resp, NULL, NULL)) {
                                        char path_buf[512];
                                        cache_store(entry->module_id, entry->filename,
                                                    resp.data, resp.size, path_buf, sizeof(path_buf));
                                        free(resp.data);
                                    }
                                }
                            }
                            update_cache_status(&browser);
                        }
                        break;
                    default:
                        break;
                    }
                    break;

                case APP_SEARCH_INPUT:
                    if (key == SDLK_ESCAPE) {
                        SDL_StopTextInput();
                        app_state = APP_BROWSER;
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        SDL_StopTextInput();
                        if (g_search_len > 0) {
                            browser_search(&browser, g_search_buf, 1);
                            update_cache_status(&browser);
                            browser.selected_index = 0;
                            memset(&browser.artist, 0, sizeof(browser.artist));
                            if (browser_search_artist(&browser.artist, g_search_buf)) {
                                browser_fetch_artist_modules(&browser.artist);
                            }
                        }
                        app_state = APP_BROWSER;
                    } else if (key == SDLK_BACKSPACE) {
                        if (g_search_len > 0) {
                            g_search_len--;
                            g_search_buf[g_search_len] = '\0';
                        }
                    }
                    break;

                case APP_DOWNLOADING:
                    if (key == SDLK_ESCAPE) {
                        app_state = APP_BROWSER;
                    } else if (key == SDLK_q) {
                        app_state = APP_QUIT;
                    } else if (key == SDLK_c && (event.key.keysym.mod & KMOD_CTRL)) {
                        app_state = APP_QUIT;
                    }
                    break;

                case APP_PLAYING:
                    switch (key) {
                    case SDLK_q:
                        app_state = APP_QUIT;
                        break;
                    case SDLK_c:
                        if (event.key.keysym.mod & KMOD_CTRL)
                            app_state = APP_QUIT;
                        break;
                    case SDLK_ESCAPE:
                    case SDLK_BACKSPACE:
                        audio_stop();
                        player_stop();
                        player_unload();
                        browser.playing_index = -1;
                        app_state = APP_BROWSER;
                        break;
                    case SDLK_SPACE:
                        player_toggle_pause();
                        break;
                    case SDLK_PLUS:
                    case SDLK_EQUALS:
                    case SDLK_KP_PLUS:
                        player_next_order();
                        break;
                    case SDLK_MINUS:
                    case SDLK_KP_MINUS:
                        player_prev_order();
                        break;
                    case SDLK_LEFT:
                        player_seek(-5.0);
                        break;
                    case SDLK_RIGHT:
                        player_seek(5.0);
                        break;
                    case SDLK_UP:
                        if (browser.selected_index > 0)
                            browser.selected_index--;
                        break;
                    case SDLK_DOWN:
                        if (browser.selected_index < browser.module_count - 1)
                            browser.selected_index++;
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (browser.module_count > 0 &&
                            browser.selected_index < browser.module_count &&
                            browser.selected_index != browser.playing_index) {
                            audio_stop();
                            player_stop();
                            player_unload();

                            const module_entry_t *entry = &browser.modules[browser.selected_index];
                            char path_buf[512];
                            if (cache_get_path(entry->module_id, entry->filename,
                                               path_buf, sizeof(path_buf))) {
                                if (player_load_file(path_buf)) {
                                    player_play();
                                    audio_start();
                                    browser.playing_index = browser.selected_index;
                                }
                            } else {
                                app_state = APP_DOWNLOADING;
                            }
                        }
                        break;
                    default:
                        break;
                    }
                    break;

                default:
                    break;
                }
            }

            if (event.type == SDL_TEXTINPUT && app_state == APP_SEARCH_INPUT) {
                size_t input_len = strlen(event.text.text);
                if (g_search_len + (int)input_len < (int)sizeof(g_search_buf) - 1) {
                    memcpy(&g_search_buf[g_search_len], event.text.text, input_len);
                    g_search_len += (int)input_len;
                    g_search_buf[g_search_len] = '\0';
                }
            }

            if (event.type == SDL_MOUSEWHEEL) {
                int mouse_y;
                SDL_GetMouseState(NULL, &mouse_y);
                const display_layout_t *layout = display_get_layout();
                int scroll_amount = event.wheel.y;

                if (app_state == APP_BROWSER) {
                    int scroll_top = layout->browser_artist_row ?
                        layout->browser_artist_row_y : layout->browser_list_y_start;
                    if (mouse_y >= scroll_top &&
                        mouse_y <= layout->browser_list_y_end) {
                        browser.selected_index -= scroll_amount;
                        int min_idx = (browser.artist.artist_found &&
                                       browser.artist.artist_module_count > 0) ? -1 : 0;
                        if (browser.selected_index < min_idx)
                            browser.selected_index = min_idx;
                        if (browser.selected_index >= browser.module_count)
                            browser.selected_index = browser.module_count - 1;
                        browser.hover_active = false;
                    }
                } else if (app_state == APP_PLAYING) {
                    if (mouse_y >= layout->playlist_y_start &&
                        mouse_y <= layout->playlist_y_end) {
                        browser.selected_index -= scroll_amount;
                        if (browser.selected_index < 0)
                            browser.selected_index = 0;
                        if (browser.selected_index >= browser.module_count)
                            browser.selected_index = browser.module_count - 1;
                        browser.hover_active = false;
                    }
                }
            }

            if (event.type == SDL_MOUSEMOTION) {
                const display_layout_t *layout = display_get_layout();
                int my = event.motion.y;
                browser.hover_active = true;

                if (app_state == APP_BROWSER) {
                    if (layout->browser_artist_row &&
                        my >= layout->browser_artist_row_y &&
                        my < layout->browser_artist_row_y + layout->browser_row_h) {
                        browser.hover_index = -1;
                        browser.selected_index = -1;
                    } else if (my >= layout->browser_list_y_start &&
                        my < layout->browser_list_y_end &&
                        layout->browser_row_h > 0) {
                        int row = (my - layout->browser_list_y_start) / layout->browser_row_h;
                        int idx = row + layout->browser_start;
                        if (idx >= 0 && idx < browser.module_count &&
                            row < layout->browser_max_visible) {
                            browser.hover_index = idx;
                            browser.selected_index = idx;
                        } else {
                            browser.hover_index = -1;
                        }
                    } else {
                        browser.hover_index = -1;
                    }
                } else if (app_state == APP_PLAYING) {
                    if (my >= layout->playlist_list_y &&
                        my < layout->playlist_y_end &&
                        layout->playlist_row_h > 0) {
                        int row = (my - layout->playlist_list_y) / layout->playlist_row_h;
                        int idx = row + layout->playlist_start;
                        if (idx >= 0 && idx < browser.module_count &&
                            row < layout->playlist_max_visible) {
                            browser.hover_index = idx;
                            browser.selected_index = idx;
                        } else {
                            browser.hover_index = -1;
                        }
                    } else {
                        browser.hover_index = -1;
                    }
                }
            }

            if (event.type == SDL_MOUSEBUTTONDOWN &&
                (event.button.button == SDL_BUTTON_LEFT ||
                 event.button.button == SDL_BUTTON_MIDDLE)) {
                if (app_state == APP_BROWSER && browser.selected_index == -1 &&
                    browser.artist.artist_found && browser.artist.artist_module_count > 0) {
                    const display_layout_t *layout = display_get_layout();
                    int my = event.button.y;
                    if (layout->browser_artist_row &&
                        my >= layout->browser_artist_row_y &&
                        my < layout->browser_artist_row_y + layout->browser_row_h) {
                        browser.module_count = browser.artist.artist_module_count;
                        memcpy(browser.modules, browser.artist.artist_modules,
                               sizeof(module_entry_t) * browser.artist.artist_module_count);
                        browser.selected_index = 0;
                        update_cache_status(&browser);
                        memset(&browser.artist, 0, sizeof(browser.artist));
                    }
                } else if (browser.module_count > 0 && browser.selected_index >= 0 &&
                    browser.selected_index < browser.module_count) {
                    if (app_state == APP_BROWSER) {
                        const display_layout_t *layout = display_get_layout();
                        int my = event.button.y;
                        if (my >= layout->browser_list_y_start &&
                            my < layout->browser_list_y_end) {
                            const module_entry_t *entry = &browser.modules[browser.selected_index];
                            char path_buf[512];
                            if (cache_get_path(entry->module_id, entry->filename,
                                               path_buf, sizeof(path_buf))) {
                                if (player_load_file(path_buf)) {
                                    player_play();
                                    audio_start();
                                    browser.playing_index = browser.selected_index;
                                    app_state = APP_PLAYING;
                                }
                            } else {
                                app_state = APP_DOWNLOADING;
                            }
                        }
                    } else if (app_state == APP_PLAYING) {
                        const display_layout_t *layout = display_get_layout();
                        int my = event.button.y;
                        if (my >= layout->playlist_list_y &&
                            my < layout->playlist_y_end &&
                            browser.selected_index != browser.playing_index) {
                            audio_stop();
                            player_stop();
                            player_unload();

                            const module_entry_t *entry = &browser.modules[browser.selected_index];
                            char path_buf[512];
                            if (cache_get_path(entry->module_id, entry->filename,
                                               path_buf, sizeof(path_buf))) {
                                if (player_load_file(path_buf)) {
                                    player_play();
                                    audio_start();
                                    browser.playing_index = browser.selected_index;
                                }
                            } else {
                                app_state = APP_DOWNLOADING;
                            }
                        }
                    }
                }
            }

            if (event.type == SDL_MOUSEBUTTONDOWN &&
                event.button.button == SDL_BUTTON_X1) {
                if (app_state == APP_PLAYING) {
                    audio_stop();
                    player_stop();
                    player_unload();
                    browser.playing_index = -1;
                    app_state = APP_BROWSER;
                }
            }
        }

        if (app_state == APP_DOWNLOADING) {
            const module_entry_t *entry = &browser.modules[browser.selected_index];
            if (download_and_play(entry)) {
                player_play();
                audio_start();
                browser.playing_index = browser.selected_index;
                app_state = APP_PLAYING;
                update_cache_status(&browser);
            } else {
                app_state = APP_BROWSER;
            }
        }

        if (app_state == APP_PLAYING) {
            player_info_t info;
            player_get_info(&info);
            if (info.status == PLAYER_STOPPED && player_is_loaded()) {
                audio_stop();
                player_unload();

                if (browser.module_count > 0) {
                    browser.selected_index = (browser.selected_index + 1) % browser.module_count;
                    const module_entry_t *next_entry = &browser.modules[browser.selected_index];

                    char path_buf[512];
                    if (cache_get_path(next_entry->module_id, next_entry->filename,
                                       path_buf, sizeof(path_buf))) {
                        if (player_load_file(path_buf)) {
                            player_play();
                            audio_start();
                            browser.playing_index = browser.selected_index;
                        } else {
                            browser.playing_index = -1;
                            app_state = APP_BROWSER;
                        }
                    } else {
                        app_state = APP_DOWNLOADING;
                    }
                } else {
                    browser.playing_index = -1;
                    app_state = APP_BROWSER;
                }
            }
        }

        display_clear();

        switch (app_state) {
        case APP_BROWSER:
            display_render_browser(&browser);
            break;
        case APP_SEARCH_INPUT:
            display_render_search_input(g_search_buf);
            break;
        case APP_DOWNLOADING: {
            download_progress_t prog = {
                .filename = g_dl_filename,
                .downloaded = atomic_load(&g_dl_downloaded),
                .total = atomic_load(&g_dl_total),
                .complete = atomic_load(&g_dl_complete) != 0,
                .error = atomic_load(&g_dl_error) != 0,
            };
            display_render_download(&prog);
            break;
        }
        case APP_PLAYING: {
            player_info_t info;
            player_get_info(&info);
            display_render_player(&info, &browser);
            break;
        }
        case APP_QUIT:
            break;
        }

        display_present();
    }

    audio_stop();
    audio_cleanup();
    player_unload();
    display_cleanup();
    http_cleanup();
    SDL_Quit();

    return 0;
}
