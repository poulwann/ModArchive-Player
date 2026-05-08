#ifndef SHADER_H
#define SHADER_H

#include <stdbool.h>
#include <stdint.h>

bool shader_init(void);
void shader_cleanup(void);
void shader_begin_scene(int width, int height);
void shader_end_scene(float time, int width, int height);
void shader_set_enabled(bool enabled);
bool shader_is_enabled(void);

#endif
