#include "display.h"
#include "shader.h"
#include "font.h"

#include <GL/glew.h>
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define WINDOW_TITLE   "mod_player"
#define WINDOW_WIDTH   1024
#define WINDOW_HEIGHT  768
#define BASE_FONT_SCALE 2
#define MAX_BATCH_QUADS 4096

typedef struct { uint8_t r, g, b, a; } color_t;

static const color_t COL_BG         = { 0x1a, 0x1a, 0x2e, 0xff };
static const color_t COL_TEXT       = { 0xcc, 0xcc, 0xcc, 0xff };
static const color_t COL_BRIGHT     = { 0xff, 0xff, 0xff, 0xff };
static const color_t COL_DIM        = { 0x66, 0x66, 0x88, 0xff };
static const color_t COL_HIGHLIGHT  = { 0x00, 0x88, 0xff, 0xff };
static const color_t COL_SELECTED   = { 0x20, 0x30, 0x50, 0xff };
static const color_t COL_NOTE       = { 0x44, 0xdd, 0x88, 0xff };
static const color_t COL_INST       = { 0xff, 0xaa, 0x33, 0xff };
static const color_t COL_EFFECT     = { 0xdd, 0x44, 0x88, 0xff };
static const color_t COL_VU_LOW     = { 0x22, 0xaa, 0x44, 0xff };
static const color_t COL_VU_MID     = { 0xcc, 0xcc, 0x22, 0xff };
static const color_t COL_VU_HIGH    = { 0xdd, 0x33, 0x33, 0xff };
static const color_t COL_CACHED     = { 0x44, 0xdd, 0x88, 0xff };
static const color_t COL_ROW_HIGHLIGHT = { 0x28, 0x38, 0x58, 0xff };
static const color_t COL_HEADER_BG  = { 0x10, 0x10, 0x22, 0xff };
static const color_t COL_PROGRESS   = { 0x00, 0x88, 0xff, 0xff };

static SDL_Window *g_window = NULL;
static SDL_GLContext g_gl_context = NULL;
static display_layout_t g_layout = {0};

static GLuint g_batch_vao = 0;
static GLuint g_batch_vbo = 0;
static GLuint g_batch_program = 0;
static GLint g_batch_loc_projection = -1;
static GLint g_batch_loc_texture = -1;
static GLint g_batch_loc_use_texture = -1;

static GLuint g_font_texture = 0;
static int g_font_tex_width = 0;
static int g_font_tex_height = 0;

typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
} batch_vertex_t;

static batch_vertex_t g_batch_vertices[MAX_BATCH_QUADS * 6];
static int g_batch_count = 0;
static bool g_batch_textured = false;

static int g_win_w = WINDOW_WIDTH;
static int g_win_h = WINDOW_HEIGHT;

static int g_char_w = FONT_CHAR_WIDTH * BASE_FONT_SCALE;
static int g_char_h = FONT_CHAR_HEIGHT * BASE_FONT_SCALE;
static float g_ui_scale = 1.0f;

static inline int px(int base_pixels) {
    return (int)(base_pixels * g_ui_scale + 0.5f);
}

static const char *batch_vertex_src =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aTexCoord;\n"
    "layout(location = 2) in vec4 aColor;\n"
    "uniform mat4 projection;\n"
    "out vec2 TexCoord;\n"
    "out vec4 Color;\n"
    "void main() {\n"
    "    gl_Position = projection * vec4(aPos, 0.0, 1.0);\n"
    "    TexCoord = aTexCoord;\n"
    "    Color = aColor;\n"
    "}\n";

static const char *batch_fragment_src =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "uniform int use_texture;\n"
    "in vec2 TexCoord;\n"
    "in vec4 Color;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    if (use_texture != 0) {\n"
    "        float alpha = texture(tex, TexCoord).r;\n"
    "        FragColor = vec4(Color.rgb, Color.a * alpha);\n"
    "    } else {\n"
    "        FragColor = Color;\n"
    "    }\n"
    "}\n";

static GLuint compile_shader_src(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "display: shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool init_batch_renderer(void) {
    GLuint vert = compile_shader_src(GL_VERTEX_SHADER, batch_vertex_src);
    if (!vert) return false;
    GLuint frag = compile_shader_src(GL_FRAGMENT_SHADER, batch_fragment_src);
    if (!frag) { glDeleteShader(vert); return false; }

    g_batch_program = glCreateProgram();
    glAttachShader(g_batch_program, vert);
    glAttachShader(g_batch_program, frag);
    glLinkProgram(g_batch_program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint success;
    glGetProgramiv(g_batch_program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(g_batch_program, sizeof(log), NULL, log);
        fprintf(stderr, "display: batch shader link error: %s\n", log);
        return false;
    }

    g_batch_loc_projection = glGetUniformLocation(g_batch_program, "projection");
    g_batch_loc_texture = glGetUniformLocation(g_batch_program, "tex");
    g_batch_loc_use_texture = glGetUniformLocation(g_batch_program, "use_texture");

    glGenVertexArrays(1, &g_batch_vao);
    glGenBuffers(1, &g_batch_vbo);

    glBindVertexArray(g_batch_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_batch_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_batch_vertices), NULL, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(batch_vertex_t), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(batch_vertex_t),
                          (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(batch_vertex_t),
                          (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    return true;
}

static bool create_font_texture(void) {
    int num_chars = FONT_LAST_CHAR - FONT_FIRST_CHAR + 1;
    g_font_tex_width = FONT_CHAR_WIDTH * num_chars;
    g_font_tex_height = FONT_CHAR_HEIGHT;

    uint8_t *pixels = calloc(g_font_tex_width * g_font_tex_height, 1);
    if (!pixels) return false;

    for (int ch = 0; ch < num_chars; ch++) {
        const uint8_t *glyph = &font_data[ch * FONT_CHAR_HEIGHT];
        int x_offset = ch * FONT_CHAR_WIDTH;

        for (int row = 0; row < FONT_CHAR_HEIGHT; row++) {
            uint8_t byte = glyph[row];
            for (int col = 0; col < FONT_CHAR_WIDTH; col++) {
                bool pixel = (byte >> (7 - col)) & 1;
                pixels[row * g_font_tex_width + x_offset + col] = pixel ? 255 : 0;
            }
        }
    }

    glGenTextures(1, &g_font_texture);
    glBindTexture(GL_TEXTURE_2D, g_font_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, g_font_tex_width, g_font_tex_height, 0,
                 GL_RED, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(pixels);
    return true;
}

static void flush_batch(void) {
    if (g_batch_count == 0) return;

    glUseProgram(g_batch_program);

    float proj[16] = {0};
    proj[0] = 2.0f / (float)g_win_w;
    proj[5] = -2.0f / (float)g_win_h;
    proj[10] = -1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(g_batch_loc_projection, 1, GL_FALSE, proj);

    glUniform1i(g_batch_loc_texture, 0);
    glUniform1i(g_batch_loc_use_texture, g_batch_textured ? 1 : 0);

    if (g_batch_textured) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_font_texture);
    }

    glBindVertexArray(g_batch_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_batch_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, g_batch_count * sizeof(batch_vertex_t), g_batch_vertices);
    glDrawArrays(GL_TRIANGLES, 0, g_batch_count);
    glBindVertexArray(0);

    g_batch_count = 0;
}

static void ensure_batch_mode(bool textured) {
    if (g_batch_count > 0 && g_batch_textured != textured) {
        flush_batch();
    }
    g_batch_textured = textured;
}

static void batch_quad(float x, float y, float w, float h, color_t color) {
    ensure_batch_mode(false);
    if (g_batch_count + 6 > MAX_BATCH_QUADS * 6) flush_batch();

    float r = color.r / 255.0f;
    float g = color.g / 255.0f;
    float b = color.b / 255.0f;
    float a = color.a / 255.0f;

    batch_vertex_t *v = &g_batch_vertices[g_batch_count];
    v[0] = (batch_vertex_t){ x,     y,     0, 0, r, g, b, a };
    v[1] = (batch_vertex_t){ x + w, y,     0, 0, r, g, b, a };
    v[2] = (batch_vertex_t){ x + w, y + h, 0, 0, r, g, b, a };
    v[3] = (batch_vertex_t){ x,     y,     0, 0, r, g, b, a };
    v[4] = (batch_vertex_t){ x + w, y + h, 0, 0, r, g, b, a };
    v[5] = (batch_vertex_t){ x,     y + h, 0, 0, r, g, b, a };
    g_batch_count += 6;
}

static void batch_glyph(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1, color_t color) {
    ensure_batch_mode(true);
    if (g_batch_count + 6 > MAX_BATCH_QUADS * 6) flush_batch();

    float r = color.r / 255.0f;
    float g = color.g / 255.0f;
    float b = color.b / 255.0f;
    float a = color.a / 255.0f;

    batch_vertex_t *v = &g_batch_vertices[g_batch_count];
    v[0] = (batch_vertex_t){ x,     y,     u0, v0, r, g, b, a };
    v[1] = (batch_vertex_t){ x + w, y,     u1, v0, r, g, b, a };
    v[2] = (batch_vertex_t){ x + w, y + h, u1, v1, r, g, b, a };
    v[3] = (batch_vertex_t){ x,     y,     u0, v0, r, g, b, a };
    v[4] = (batch_vertex_t){ x + w, y + h, u1, v1, r, g, b, a };
    v[5] = (batch_vertex_t){ x,     y + h, u0, v1, r, g, b, a };
    g_batch_count += 6;
}

static void draw_rect(int x, int y, int w, int h, color_t color) {
    batch_quad((float)x, (float)y, (float)w, (float)h, color);
}

static void draw_char(int x, int y, char ch, color_t color) {
    if (ch < FONT_FIRST_CHAR || ch > FONT_LAST_CHAR) ch = ' ';

    int index = ch - FONT_FIRST_CHAR;
    float u0 = (float)(index * FONT_CHAR_WIDTH) / (float)g_font_tex_width;
    float u1 = (float)((index + 1) * FONT_CHAR_WIDTH) / (float)g_font_tex_width;
    float v0 = 0.0f;
    float v1 = 1.0f;

    batch_glyph((float)x, (float)y, (float)g_char_w, (float)g_char_h,
                u0, v0, u1, v1, color);
}

static void draw_text(int x, int y, const char *text, color_t color) {
    while (*text) {
        draw_char(x, y, *text, color);
        x += g_char_w;
        text++;
    }
}

static void draw_text_n(int x, int y, const char *text, int max_chars, color_t color) {
    int i = 0;
    while (*text && i < max_chars) {
        draw_char(x, y, *text, color);
        x += g_char_w;
        text++;
        i++;
    }
}

bool display_init(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow(
        WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL
    );
    if (!g_window) {
        fprintf(stderr, "display: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    g_gl_context = SDL_GL_CreateContext(g_window);
    if (!g_gl_context) {
        fprintf(stderr, "display: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        return false;
    }

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "display: glewInit failed: %s\n", glewGetErrorString(err));
        SDL_GL_DeleteContext(g_gl_context);
        SDL_DestroyWindow(g_window);
        g_gl_context = NULL;
        g_window = NULL;
        return false;
    }

    SDL_GL_SetSwapInterval(1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    if (!init_batch_renderer()) {
        fprintf(stderr, "display: failed to init batch renderer\n");
        SDL_GL_DeleteContext(g_gl_context);
        SDL_DestroyWindow(g_window);
        g_gl_context = NULL;
        g_window = NULL;
        return false;
    }

    if (!create_font_texture()) {
        fprintf(stderr, "display: failed to create font texture\n");
        SDL_GL_DeleteContext(g_gl_context);
        SDL_DestroyWindow(g_window);
        g_gl_context = NULL;
        g_window = NULL;
        return false;
    }

    if (!shader_init()) {
        fprintf(stderr, "display: failed to init CRT shader\n");
        SDL_GL_DeleteContext(g_gl_context);
        SDL_DestroyWindow(g_window);
        g_gl_context = NULL;
        g_window = NULL;
        return false;
    }

    return true;
}

void display_cleanup(void) {
    shader_cleanup();
    if (g_font_texture) { glDeleteTextures(1, &g_font_texture); g_font_texture = 0; }
    if (g_batch_vbo) { glDeleteBuffers(1, &g_batch_vbo); g_batch_vbo = 0; }
    if (g_batch_vao) { glDeleteVertexArrays(1, &g_batch_vao); g_batch_vao = 0; }
    if (g_batch_program) { glDeleteProgram(g_batch_program); g_batch_program = 0; }
    if (g_gl_context) { SDL_GL_DeleteContext(g_gl_context); g_gl_context = NULL; }
    if (g_window) { SDL_DestroyWindow(g_window); g_window = NULL; }
}

void display_clear(void) {
    SDL_GL_GetDrawableSize(g_window, &g_win_w, &g_win_h);

    float scale_x = (float)g_win_w / (float)WINDOW_WIDTH;
    float scale_y = (float)g_win_h / (float)WINDOW_HEIGHT;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    g_ui_scale = scale;
    int font_scale = (int)(BASE_FONT_SCALE * scale + 0.5f);
    if (font_scale < 1) font_scale = 1;
    g_char_w = FONT_CHAR_WIDTH * font_scale;
    g_char_h = FONT_CHAR_HEIGHT * font_scale;

    shader_begin_scene(g_win_w, g_win_h);

    glClearColor(COL_BG.r / 255.0f, COL_BG.g / 255.0f,
                 COL_BG.b / 255.0f, COL_BG.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void display_present(void) {
    flush_batch();

    float time = (float)SDL_GetTicks() / 1000.0f;
    shader_end_scene(time, g_win_w, g_win_h);

    SDL_GL_SwapWindow(g_window);
}

void display_get_size(int *w, int *h) {
    *w = g_win_w;
    *h = g_win_h;
}

const display_layout_t *display_get_layout(void) {
    return &g_layout;
}

void display_render_browser(const browser_state_t *browser) {
    int win_w, win_h;
    display_get_size(&win_w, &win_h);

    int margin = g_char_w;
    int header_h = g_char_h * 3;
    int row_h = g_char_h + px(4);

    draw_rect(0, 0, win_w, header_h, COL_HEADER_BG);

    char title[128];
    snprintf(title, sizeof(title), "ModArchive - %s",
             browser_chart_name(browser->current_chart));
    draw_text(margin, g_char_h / 2, title, COL_BRIGHT);

    char page_info[64];
    snprintf(page_info, sizeof(page_info), "Page %d/%d  [Tab: switch chart]  [PgUp/PgDn: page]",
             browser->current_page, browser->total_pages);
    draw_text(margin, g_char_h * 2 - px(4), page_info, COL_DIM);

    if (browser->loading) {
        draw_text(margin, header_h + g_char_h * 2, "Loading...", COL_HIGHLIGHT);
        return;
    }

    if (browser->load_error) {
        draw_text(margin, header_h + g_char_h * 2, "Error loading chart data!", COL_VU_HIGH);
        return;
    }

    int y = header_h + px(4);

    g_layout.browser_artist_row = false;
    if (browser->artist.artist_found && browser->artist.artist_module_count > 0) {
        g_layout.browser_artist_row = true;
        g_layout.browser_artist_row_y = y;
        if (browser->selected_index == -1) {
            draw_rect(0, y - px(2), win_w, row_h, COL_SELECTED);
        }
        char artist_line[256];
        snprintf(artist_line, sizeof(artist_line), "Artist: %s (%d modules)  [Enter: show]  [A: download all]",
                 browser->artist.artist_name, browser->artist.artist_module_count);
        draw_text(margin, y, artist_line,
                  browser->selected_index == -1 ? COL_HIGHLIGHT : COL_CACHED);
        y += row_h;
    }

    int col_rank = margin;
    int col_title = col_rank + g_char_w * 5;
    int col_filename = col_title + g_char_w * 30;
    int col_downloads = col_filename + g_char_w * 24;

    draw_text(col_rank, y, "#", COL_DIM);
    draw_text(col_title, y, "Title", COL_DIM);
    draw_text(col_filename, y, "Filename", COL_DIM);
    draw_text(col_downloads, y, "Downloads", COL_DIM);
    y += row_h + px(2);

    int footer_h = g_char_h * 2 + px(10);
    int max_visible = (win_h - y - footer_h) / row_h;
    if (max_visible < 1) max_visible = 1;
    int start = 0;
    if (browser->selected_index >= max_visible) {
        start = browser->selected_index - max_visible + 1;
    }
    if (start < 0) start = 0;

    g_layout.browser_list_y_start = y;
    g_layout.browser_list_y_end = win_h - footer_h;
    g_layout.browser_row_h = row_h;
    g_layout.browser_start = start;
    g_layout.browser_max_visible = max_visible;

    for (int i = start; i < browser->module_count && (i - start) < max_visible; i++) {
        const module_entry_t *mod = &browser->modules[i];
        int row_y = y + (i - start) * row_h;

        if (i == browser->selected_index) {
            draw_rect(0, row_y - px(2), win_w, row_h, COL_SELECTED);
        }

        char rank_str[8];
        snprintf(rank_str, sizeof(rank_str), "%3d", mod->rank);
        draw_text(col_rank, row_y, rank_str, i == browser->selected_index ? COL_BRIGHT : COL_TEXT);

        if (mod->cached) {
            draw_char(col_rank + g_char_w * 4, row_y, '*', COL_CACHED);
        }

        color_t title_col = i == browser->selected_index ? COL_HIGHLIGHT : COL_TEXT;
        draw_text_n(col_title, row_y, mod->title, 28, title_col);

        draw_text_n(col_filename, row_y, mod->filename, 22, COL_DIM);

        char dl_str[32];
        if (mod->download_count > 0) {
            snprintf(dl_str, sizeof(dl_str), "%u", mod->download_count);
        } else {
            dl_str[0] = '\0';
        }
        draw_text(col_downloads, row_y, dl_str, COL_DIM);
    }

    int footer_y = win_h - g_char_h * 2 - px(6);
    char footer_buf[256];
    snprintf(footer_buf, sizeof(footer_buf),
             "[Enter: play]  [F1: search]  [F2: cached]  [Q: quit]  [F3: CRT %s]",
             shader_is_enabled() ? "on" : "off");
    draw_text(margin, footer_y, footer_buf, COL_DIM);
    draw_text(margin, footer_y + g_char_h + px(2),
              "Mouse: [Click: play]  [Scroll: navigate]", COL_DIM);
}

void display_render_download(const download_progress_t *progress) {
    int win_w, win_h;
    display_get_size(&win_w, &win_h);

    int center_y = win_h / 2;
    int margin = g_char_w * 2;

    draw_text(margin, center_y - g_char_h * 2, "Downloading...", COL_BRIGHT);

    if (progress->filename) {
        draw_text(margin, center_y - g_char_h, progress->filename, COL_TEXT);
    }

    int bar_x = margin;
    int bar_y = center_y + g_char_h;
    int bar_w = win_w - margin * 2;
    int bar_h = g_char_h;

    draw_rect(bar_x, bar_y, bar_w, bar_h, COL_DIM);

    if (progress->total > 0) {
        float pct = (float)progress->downloaded / (float)progress->total;
        if (pct > 1.0f) pct = 1.0f;
        int fill_w = (int)(pct * (float)bar_w);
        draw_rect(bar_x, bar_y, fill_w, bar_h, COL_PROGRESS);

        char pct_str[32];
        snprintf(pct_str, sizeof(pct_str), "%d%% (%zu / %zu bytes)",
                 (int)(pct * 100.0f), progress->downloaded, progress->total);
        draw_text(margin, bar_y + bar_h + px(8), pct_str, COL_TEXT);
    } else {
        char dl_str[32];
        snprintf(dl_str, sizeof(dl_str), "%zu bytes downloaded", progress->downloaded);
        draw_text(margin, bar_y + bar_h + px(8), dl_str, COL_TEXT);
    }

    if (progress->error) {
        draw_text(margin, bar_y + bar_h + g_char_h * 2, "Download failed! Press any key to return.", COL_VU_HIGH);
    }
}

void display_render_search_input(const char *query) {
    int win_w, win_h;
    display_get_size(&win_w, &win_h);

    int center_y = win_h / 2;
    int margin = g_char_w * 2;

    draw_text(margin, center_y - g_char_h * 2, "Search ModArchive:", COL_BRIGHT);

    char input_display[140];
    snprintf(input_display, sizeof(input_display), "> %s_", query);
    draw_text(margin, center_y, input_display, COL_HIGHLIGHT);

    draw_text(margin, center_y + g_char_h * 2, "[Enter: search]  [Esc: cancel]", COL_DIM);
}

static const char *note_names[] = {
    "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
};

static void format_note(uint8_t note, char *buf) {
    if (note == 0) {
        strcpy(buf, "...");
    } else if (note == 0xFF) {
        strcpy(buf, "===");
    } else if (note == 0xFE) {
        strcpy(buf, "^^^");
    } else {
        int n = (note - 1) % 12;
        int octave = (note - 1) / 12;
        snprintf(buf, 4, "%s%d", note_names[n], octave);
    }
}

void display_render_player(const player_info_t *info, const browser_state_t *browser) {
    int win_w, win_h;
    display_get_size(&win_w, &win_h);

    int margin = g_char_w;

    int header_h = g_char_h * 4;
    draw_rect(0, 0, win_w, header_h, COL_HEADER_BG);

    char header[256];
    snprintf(header, sizeof(header), "%s  [%s]",
             info->title[0] ? info->title : "(untitled)",
             info->format_type[0] ? info->format_type : "?");
    draw_text(margin, px(4), header, COL_BRIGHT);

    char pos_str[128];
    snprintf(pos_str, sizeof(pos_str),
             "Ord: %02d/%02d  Pat: %02d  Row: %02d  Spd: %d  BPM: %d  Ch: %d",
             info->current_order, info->num_orders,
             info->current_pattern, info->current_row,
             info->speed, info->tempo, info->num_channels);
    draw_text(margin, g_char_h + px(8), pos_str, COL_TEXT);

    int mins = (int)info->position_seconds / 60;
    int secs = (int)info->position_seconds % 60;
    int dur_mins = (int)info->duration_seconds / 60;
    int dur_secs = (int)info->duration_seconds % 60;
    char time_str[64];
    snprintf(time_str, sizeof(time_str), "Time: %d:%02d / %d:%02d  %s",
             mins, secs, dur_mins, dur_secs,
             info->status == PLAYER_PAUSED ? "[PAUSED]" :
             info->status == PLAYER_STOPPED ? "[STOPPED]" : "");
    draw_text(margin, g_char_h * 2 + px(12), time_str,
              info->status == PLAYER_PAUSED ? COL_VU_MID : COL_TEXT);

    int footer_h = g_char_h * 2 + px(10);
    int content_y = header_h + px(4);
    int content_h = win_h - header_h - footer_h;
    int pattern_h = (int)(content_h * 0.40f);
    int vu_h = (int)(content_h * 0.15f);
    if (vu_h < g_char_h * 2) vu_h = g_char_h * 2;
    if (vu_h > px(120)) vu_h = px(120);
    int playlist_h = content_h - pattern_h - vu_h;

    int vu_y = content_y + pattern_h + px(4);
    int playlist_y = vu_y + vu_h + px(4);

    g_layout.playlist_y_start = playlist_y;
    g_layout.playlist_y_end = win_h - footer_h;

    int cell_width = g_char_w * 10;
    int max_display_channels = (win_w - margin * 2) / cell_width;
    int display_channels = info->num_channels;
    if (display_channels > max_display_channels) display_channels = max_display_channels;

    int pat_row_h = g_char_h + px(2);
    int rows_visible = pattern_h / pat_row_h;
    int center_row = rows_visible / 2;

    for (int vis_row = 0; vis_row < rows_visible; vis_row++) {
        int row = info->current_row - center_row + vis_row;
        int y = content_y + vis_row * pat_row_h;

        bool is_current = (vis_row == center_row);
        if (is_current) {
            draw_rect(0, y - px(1), win_w, pat_row_h, COL_ROW_HIGHLIGHT);
        }

        if (row >= 0) {
            char row_str[16];
            snprintf(row_str, sizeof(row_str), "%02X", row);
            draw_text(margin, y, row_str, is_current ? COL_BRIGHT : COL_DIM);
        }

        if (row < 0) continue;

        pattern_cell_t cells[PLAYER_MAX_CHANNELS];
        if (!player_get_pattern_row(info->current_pattern, row, cells, display_channels)) {
            continue;
        }

        for (int ch = 0; ch < display_channels; ch++) {
            int x = margin + g_char_w * 4 + ch * cell_width;
            pattern_cell_t *cell = &cells[ch];

            char note_buf[4];
            format_note(cell->note, note_buf);
            color_t note_col = cell->note > 0 && cell->note < 0xFE ?
                               COL_NOTE : COL_DIM;
            draw_text(x, y, note_buf, cell->note == 0 ? COL_DIM : note_col);

            if (cell->instrument > 0) {
                char inst_buf[4];
                snprintf(inst_buf, sizeof(inst_buf), "%02X", cell->instrument);
                draw_text(x + g_char_w * 4, y, inst_buf, COL_INST);
            } else {
                draw_text(x + g_char_w * 4, y, "..", COL_DIM);
            }

            if (cell->effect > 0 || cell->effect_param > 0) {
                char fx_buf[8];
                snprintf(fx_buf, sizeof(fx_buf), "%X%02X",
                         cell->effect, cell->effect_param);
                draw_text(x + g_char_w * 7, y, fx_buf, COL_EFFECT);
            } else {
                draw_text(x + g_char_w * 7, y, "...", COL_DIM);
            }
        }
    }

    if (vu_h > g_char_h) {
        int vu_bar_width = (win_w - margin * 2) / (display_channels > 0 ? display_channels : 1);
        if (vu_bar_width > px(40)) vu_bar_width = px(40);
        int vu_bar_height = vu_h - g_char_h;
        if (vu_bar_height > px(100)) vu_bar_height = px(100);

        for (int ch = 0; ch < display_channels; ch++) {
            int x = margin + ch * vu_bar_width;
            float vu = (info->channels[ch].vu_left + info->channels[ch].vu_right) * 0.5f;
            if (vu > 1.0f) vu = 1.0f;

            int fill_h = (int)(vu * (float)vu_bar_height);

            draw_rect(x + px(2), vu_y, vu_bar_width - px(4), vu_bar_height, COL_DIM);

            if (fill_h > 0) {
                color_t vu_col = COL_VU_LOW;
                if (vu > 0.7f) vu_col = COL_VU_HIGH;
                else if (vu > 0.4f) vu_col = COL_VU_MID;

                draw_rect(x + px(2), vu_y + vu_bar_height - fill_h,
                          vu_bar_width - px(4), fill_h, vu_col);
            }

            char ch_str[16];
            snprintf(ch_str, sizeof(ch_str), "%02d", ch + 1);
            draw_text(x + px(2), vu_y + vu_bar_height + px(2), ch_str, COL_DIM);
        }
    }

    if (browser && browser->module_count > 0 && playlist_h > g_char_h * 2) {
        draw_rect(margin, playlist_y - px(2), win_w - margin * 2, px(1), COL_DIM);

        draw_text(margin, playlist_y, "Playlist:", COL_HIGHLIGHT);
        int list_y = playlist_y + g_char_h + px(4);
        int pl_row_h = g_char_h + px(2);
        int max_visible = (g_layout.playlist_y_end - list_y) / pl_row_h;
        if (max_visible < 1) max_visible = 1;

        int start = 0;
        if (browser->selected_index >= max_visible) {
            start = browser->selected_index - max_visible + 1;
        }

        g_layout.playlist_list_y = list_y;
        g_layout.playlist_row_h = pl_row_h;
        g_layout.playlist_start = start;
        g_layout.playlist_max_visible = max_visible;

        for (int i = start; i < browser->module_count && (i - start) < max_visible; i++) {
            const module_entry_t *mod = &browser->modules[i];
            int row_y_pos = list_y + (i - start) * pl_row_h;

            if (i == browser->selected_index) {
                draw_rect(0, row_y_pos - px(1), win_w, pl_row_h, COL_SELECTED);
            }

            char indicator = ' ';
            if (i == browser->playing_index) {
                indicator = '>';
            }
            draw_char(margin, row_y_pos, indicator,
                      i == browser->playing_index ? COL_VU_LOW : COL_DIM);

            char num_str[16];
            snprintf(num_str, sizeof(num_str), "%02d", i + 1);
            draw_text(margin + g_char_w * 2, row_y_pos, num_str, COL_DIM);

            color_t title_col = COL_TEXT;
            if (i == browser->playing_index) title_col = COL_VU_LOW;
            else if (i == browser->selected_index) title_col = COL_HIGHLIGHT;
            draw_text_n(margin + g_char_w * 5, row_y_pos, mod->title, 40, title_col);

            if (mod->cached) {
                draw_char(margin + g_char_w * 46, row_y_pos, '*', COL_CACHED);
            }
        }
    }

    int footer_y = win_h - g_char_h * 2 - px(6);
    char footer_buf[256];
    snprintf(footer_buf, sizeof(footer_buf),
             "[Space: pause]  [Esc: browser]  [F1: search]  [F2: cached]  [F3: CRT %s]  [Q: quit]",
             shader_is_enabled() ? "on" : "off");
    draw_text(margin, footer_y, footer_buf, COL_DIM);
    draw_text(margin, footer_y + g_char_h + px(2),
              "Mouse: [Click: play]  [Scroll: navigate]  [Back: browser]", COL_DIM);
}
