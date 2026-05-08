#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

bool audio_init(void);
void audio_start(void);
void audio_stop(void);
void audio_cleanup(void);

#endif
