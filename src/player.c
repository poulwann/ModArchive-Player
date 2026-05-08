#include "player.h"
#include <libopenmpt/libopenmpt.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

static openmpt_module *g_module = NULL;
static atomic_int g_status = PLAYER_STOPPED;
static player_info_t g_info = {0};

static void openmpt_log(const char *message, void *userdata) {
    (void)userdata;
    fprintf(stderr, "openmpt: %s\n", message);
}

bool player_load_file(const char *filepath) {
    player_unload();

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "player: cannot open file: %s\n", filepath);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        fclose(fp);
        return false;
    }

    void *data = malloc((size_t)size);
    if (!data) {
        fclose(fp);
        return false;
    }

    if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return false;
    }
    fclose(fp);

    bool ok = player_load_memory(data, (size_t)size);
    free(data);
    return ok;
}

bool player_load_memory(const void *data, size_t size) {
    player_unload();

    g_module = openmpt_module_create_from_memory2(
        data, size,
        openmpt_log, NULL,
        NULL, NULL,
        NULL, NULL,
        NULL
    );

    if (!g_module) {
        fprintf(stderr, "player: failed to load module\n");
        return false;
    }

    const char *title = openmpt_module_get_metadata(g_module, "title");
    if (title) {
        strncpy(g_info.title, title, sizeof(g_info.title) - 1);
        openmpt_free_string(title);
    }

    const char *type = openmpt_module_get_metadata(g_module, "type");
    if (type) {
        strncpy(g_info.format_type, type, sizeof(g_info.format_type) - 1);
        openmpt_free_string(type);
    }

    g_info.num_channels = openmpt_module_get_num_channels(g_module);
    g_info.num_orders = openmpt_module_get_num_orders(g_module);
    g_info.num_patterns = openmpt_module_get_num_patterns(g_module);
    g_info.duration_seconds = openmpt_module_get_duration_seconds(g_module);

    atomic_store(&g_status, PLAYER_STOPPED);
    return true;
}

void player_unload(void) {
    atomic_store(&g_status, PLAYER_STOPPED);
    if (g_module) {
        openmpt_module_destroy(g_module);
        g_module = NULL;
    }
    memset(&g_info, 0, sizeof(g_info));
}

void player_play(void) {
    if (g_module) {
        atomic_store(&g_status, PLAYER_PLAYING);
    }
}

void player_pause(void) {
    if (g_module) {
        atomic_store(&g_status, PLAYER_PAUSED);
    }
}

void player_stop(void) {
    if (g_module) {
        atomic_store(&g_status, PLAYER_STOPPED);
        openmpt_module_set_position_seconds(g_module, 0.0);
    }
}

void player_toggle_pause(void) {
    if (!g_module) return;
    int status = atomic_load(&g_status);
    if (status == PLAYER_PLAYING) {
        atomic_store(&g_status, PLAYER_PAUSED);
    } else if (status == PLAYER_PAUSED) {
        atomic_store(&g_status, PLAYER_PLAYING);
    } else {
        atomic_store(&g_status, PLAYER_PLAYING);
    }
}

void player_next_order(void) {
    if (!g_module) return;
    int order = openmpt_module_get_current_order(g_module);
    int num = openmpt_module_get_num_orders(g_module);
    if (order < num - 1) {
        openmpt_module_set_position_order_row(g_module, order + 1, 0);
    }
}

void player_prev_order(void) {
    if (!g_module) return;
    int order = openmpt_module_get_current_order(g_module);
    if (order > 0) {
        openmpt_module_set_position_order_row(g_module, order - 1, 0);
    }
}

void player_seek(double seconds) {
    if (!g_module) return;
    double pos = openmpt_module_get_position_seconds(g_module);
    double new_pos = pos + seconds;
    if (new_pos < 0.0) new_pos = 0.0;
    openmpt_module_set_position_seconds(g_module, new_pos);
}

size_t player_read_stereo(float *buffer, size_t frames) {
    if (!g_module) return 0;

    int status = atomic_load(&g_status);
    if (status != PLAYER_PLAYING) {
        memset(buffer, 0, frames * 2 * sizeof(float));
        return 0;
    }

    size_t read = openmpt_module_read_interleaved_float_stereo(
        g_module, PLAYER_SAMPLE_RATE, frames, buffer);

    if (read == 0) {
        atomic_store(&g_status, PLAYER_STOPPED);
    }

    return read;
}

void player_get_info(player_info_t *info) {
    if (!g_module) {
        memset(info, 0, sizeof(*info));
        return;
    }

    info->status = (player_status_t)atomic_load(&g_status);
    info->num_channels = g_info.num_channels;
    info->num_orders = g_info.num_orders;
    info->num_patterns = g_info.num_patterns;
    info->duration_seconds = g_info.duration_seconds;
    memcpy(info->title, g_info.title, sizeof(info->title));
    memcpy(info->format_type, g_info.format_type, sizeof(info->format_type));

    info->current_order = openmpt_module_get_current_order(g_module);
    info->current_row = openmpt_module_get_current_row(g_module);
    info->current_pattern = openmpt_module_get_current_pattern(g_module);
    info->speed = openmpt_module_get_current_speed(g_module);
    info->tempo = (int)openmpt_module_get_current_tempo2(g_module);
    info->position_seconds = openmpt_module_get_position_seconds(g_module);

    int channels = info->num_channels;
    if (channels > PLAYER_MAX_CHANNELS) channels = PLAYER_MAX_CHANNELS;
    for (int i = 0; i < channels; i++) {
        info->channels[i].vu_left = openmpt_module_get_current_channel_vu_left(g_module, i);
        info->channels[i].vu_right = openmpt_module_get_current_channel_vu_right(g_module, i);
    }
}

bool player_get_pattern_row(int pattern, int row, pattern_cell_t *cells, int max_channels) {
    if (!g_module || !cells) return false;

    int num_rows = openmpt_module_get_pattern_num_rows(g_module, pattern);
    if (row < 0 || row >= num_rows) return false;

    int channels = openmpt_module_get_num_channels(g_module);
    if (channels > max_channels) channels = max_channels;

    for (int ch = 0; ch < channels; ch++) {
        cells[ch].note = openmpt_module_get_pattern_row_channel_command(
            g_module, pattern, row, ch, OPENMPT_MODULE_COMMAND_NOTE);

        cells[ch].instrument = openmpt_module_get_pattern_row_channel_command(
            g_module, pattern, row, ch, OPENMPT_MODULE_COMMAND_INSTRUMENT);

        cells[ch].volume = openmpt_module_get_pattern_row_channel_command(
            g_module, pattern, row, ch, OPENMPT_MODULE_COMMAND_VOLUMEEFFECT);

        cells[ch].effect = openmpt_module_get_pattern_row_channel_command(
            g_module, pattern, row, ch, OPENMPT_MODULE_COMMAND_EFFECT);

        cells[ch].effect_param = openmpt_module_get_pattern_row_channel_command(
            g_module, pattern, row, ch, OPENMPT_MODULE_COMMAND_PARAMETER);
    }

    return true;
}

bool player_is_loaded(void) {
    return g_module != NULL;
}
