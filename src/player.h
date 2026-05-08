#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLAYER_MAX_CHANNELS 64
#define PLAYER_SAMPLE_RATE  48000

typedef enum {
    PLAYER_STOPPED,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
} player_status_t;

typedef struct {
    float vu_left;
    float vu_right;
} channel_vu_t;

typedef struct {
    uint8_t note;
    uint8_t instrument;
    uint8_t volume;
    uint8_t effect;
    uint8_t effect_param;
} pattern_cell_t;

typedef struct {
    player_status_t status;
    int num_channels;
    int current_order;
    int current_row;
    int current_pattern;
    int num_orders;
    int num_patterns;
    int speed;
    int tempo;
    double position_seconds;
    double duration_seconds;
    char title[128];
    char format_type[16];
    channel_vu_t channels[PLAYER_MAX_CHANNELS];
} player_info_t;

bool player_load_file(const char *filepath);
bool player_load_memory(const void *data, size_t size);
void player_unload(void);
void player_play(void);
void player_pause(void);
void player_stop(void);
void player_toggle_pause(void);
void player_next_order(void);
void player_prev_order(void);
void player_seek(double seconds);
size_t player_read_stereo(float *buffer, size_t frames);
void player_get_info(player_info_t *info);
bool player_get_pattern_row(int pattern, int row, pattern_cell_t *cells, int max_channels);
bool player_is_loaded(void);

#endif
