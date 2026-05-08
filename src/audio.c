#include "audio.h"
#include "player.h"
#include <SDL.h>
#include <stdio.h>

#define AUDIO_BUFFER_SIZE 1024

static SDL_AudioDeviceID g_device = 0;

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;

    int frames = len / (int)(sizeof(float) * 2);
    float *buffer = (float *)stream;

    size_t read = player_read_stereo(buffer, (size_t)frames);

    if ((int)read < frames) {
        size_t offset = read * 2;
        size_t remaining = ((size_t)frames - read) * 2;
        for (size_t i = 0; i < remaining; i++) {
            buffer[offset + i] = 0.0f;
        }
    }
}

bool audio_init(void) {
    SDL_AudioSpec desired = {0};
    SDL_AudioSpec obtained = {0};

    desired.freq = PLAYER_SAMPLE_RATE;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = AUDIO_BUFFER_SIZE;
    desired.callback = audio_callback;
    desired.userdata = NULL;

    g_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained,
                                   SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (g_device == 0) {
        fprintf(stderr, "audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return false;
    }

    if (obtained.format != AUDIO_F32SYS) {
        fprintf(stderr, "audio: could not get float32 format\n");
        SDL_CloseAudioDevice(g_device);
        g_device = 0;
        return false;
    }

    return true;
}

void audio_start(void) {
    if (g_device) {
        SDL_PauseAudioDevice(g_device, 0);
    }
}

void audio_stop(void) {
    if (g_device) {
        SDL_PauseAudioDevice(g_device, 1);
    }
}

void audio_cleanup(void) {
    if (g_device) {
        SDL_CloseAudioDevice(g_device);
        g_device = 0;
    }
}
