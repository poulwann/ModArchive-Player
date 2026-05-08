#include "shader.h"

#include <GL/glew.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TUNING_SHARP              0.0f
#define TUNING_PERSISTENCE_R      0.3f
#define TUNING_PERSISTENCE_G      0.525f
#define TUNING_PERSISTENCE_B      0.42f
#define TUNING_BLEED              0.2f
#define TUNING_ARTIFACTS          0.5f
#define TUNING_OVERSCAN           1.1f
#define TUNING_BARREL            0.115f
#define TUNING_MASK_BRIGHTNESS    0.45f
#define TUNING_MASK_OPACITY       0.5f
#define TUNING_SATUR              0.5f
#define TUNING_BLOOM_DOWN_SPREAD  0.10f
#define TUNING_BLOOM_UP_SPREAD    0.10f
#define TUNING_BLOOM_INTENSITY    0.25f
#define TUNING_BLOOM_POWER        0.5f

#define BLOOM_DIVISOR 8

static const char *quad_vertex_src =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aTexCoord;\n"
    "out vec2 TexCoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "    TexCoord = aTexCoord;\n"
    "}\n";

static const char *composite_fragment_src =
    "#version 330 core\n"
    "uniform sampler2D curFrame;\n"
    "uniform sampler2D prevFrame;\n"
    "uniform sampler2D artifactTex;\n"
    "uniform vec2 rcpScrWidth;\n"
    "uniform vec2 rcpScrHeight;\n"
    "uniform float tuning_sharp;\n"
    "uniform vec4 tuning_persistence;\n"
    "uniform float tuning_bleed;\n"
    "uniform float tuning_artifacts;\n"
    "uniform float ntscLerp;\n"
    "in vec2 TexCoord;\n"
    "out vec4 FragColor;\n"
    "\n"
    "float Brightness(vec4 c) {\n"
    "    return dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 artifact1 = texture(artifactTex, TexCoord);\n"
    "    vec4 artifact2 = texture(artifactTex, TexCoord + rcpScrHeight);\n"
    "    vec4 artifact = mix(artifact1, artifact2, ntscLerp);\n"
    "\n"
    "    vec2 leftUV = TexCoord - rcpScrWidth;\n"
    "    vec2 rightUV = TexCoord + rcpScrWidth;\n"
    "\n"
    "    vec4 curLeft = texture(curFrame, leftUV);\n"
    "    vec4 curLocal = texture(curFrame, TexCoord);\n"
    "    vec4 curRight = texture(curFrame, rightUV);\n"
    "\n"
    "    vec4 tunedNTSC = artifact * tuning_artifacts;\n"
    "\n"
    "    vec4 prevLeft = texture(prevFrame, leftUV);\n"
    "    vec4 prevLocal = texture(prevFrame, TexCoord);\n"
    "    vec4 prevRight = texture(prevFrame, rightUV);\n"
    "\n"
    "    curLocal = clamp(curLocal +\n"
    "        (((curLeft - curLocal) + (curRight - curLocal)) * tunedNTSC), 0.0, 1.0);\n"
    "\n"
    "    float curBrt = Brightness(curLocal);\n"
    "    float offset = 0.0;\n"
    "    float sharpWeights[3] = float[](1.0, -0.3162277, 0.1);\n"
    "\n"
    "    for (int i = 0; i < 3; ++i) {\n"
    "        vec2 stepSize = vec2(1.0 / 256.0, 0.0) * float(i + 1);\n"
    "        vec4 neighborLeft = texture(curFrame, TexCoord - stepSize);\n"
    "        vec4 neighborRight = texture(curFrame, TexCoord + stepSize);\n"
    "        float nBrtL = Brightness(neighborLeft);\n"
    "        float nBrtR = Brightness(neighborRight);\n"
    "        offset += ((curBrt - nBrtL) + (curBrt - nBrtR)) * sharpWeights[i];\n"
    "    }\n"
    "\n"
    "    curLocal = clamp(curLocal + (offset * tuning_sharp *\n"
    "        mix(vec4(1.0), artifact, tuning_artifacts)), 0.0, 1.0);\n"
    "\n"
    "    vec4 blended_prev = tuning_persistence *\n"
    "        (1.0 / (1.0 + 2.0 * tuning_bleed)) *\n"
    "        (prevLocal + (prevLeft + prevRight) * tuning_bleed);\n"
    "    curLocal = clamp(max(curLocal, blended_prev), 0.0, 1.0);\n"
    "\n"
    "    FragColor = curLocal;\n"
    "}\n";

static const char *screen_fragment_src =
    "#version 330 core\n"
    "uniform sampler2D compFrame;\n"
    "uniform sampler2D shadowMask;\n"
    "uniform float tuning_overscan;\n"
    "uniform float tuning_barrel;\n"
    "uniform float tuning_mask_brightness;\n"
    "uniform float tuning_mask_opacity;\n"
    "uniform float tuning_satur;\n"
    "uniform vec2 maskScale;\n"
    "in vec2 TexCoord;\n"
    "out vec4 FragColor;\n"
    "\n"
    "void main() {\n"
    "    vec2 uv = TexCoord;\n"
    "\n"
    "    vec2 scanUV = uv * maskScale;\n"
    "    vec3 scanTex = texture(shadowMask, scanUV).rgb;\n"
    "    scanTex += tuning_mask_brightness;\n"
    "    scanTex = mix(vec3(1.0), scanTex, tuning_mask_opacity);\n"
    "\n"
    "    vec2 overscanUV = (uv * tuning_overscan) - ((tuning_overscan - 1.0) * 0.5);\n"
    "\n"
    "    vec2 centered = overscanUV - vec2(0.5);\n"
    "    float rsq = dot(centered, centered);\n"
    "    overscanUV = centered + (centered * (tuning_barrel * rsq)) + vec2(0.5);\n"
    "\n"
    "    vec3 compTex = texture(compFrame, overscanUV).rgb;\n"
    "\n"
    "    vec4 emissive = vec4(compTex * scanTex, 1.0);\n"
    "\n"
    "    float desat = dot(emissive.rgb, vec3(0.299, 0.587, 0.114));\n"
    "    emissive.rgb = mix(vec3(desat), emissive.rgb, tuning_satur);\n"
    "\n"
    "    FragColor = emissive;\n"
    "}\n";

static const char *post_fragment_src =
    "#version 330 core\n"
    "uniform sampler2D inputTex;\n"
    "uniform vec2 bloomScale;\n"
    "in vec2 TexCoord;\n"
    "out vec4 FragColor;\n"
    "\n"
    "void main() {\n"
    "    vec2 poisson[7] = vec2[](\n"
    "        vec2( 0.000000,  0.000000),\n"
    "        vec2( 0.000000,  1.000000),\n"
    "        vec2( 0.000000, -1.000000),\n"
    "        vec2(-0.866025,  0.500000),\n"
    "        vec2(-0.866025, -0.500000),\n"
    "        vec2( 0.866025,  0.500000),\n"
    "        vec2( 0.866025, -0.500000)\n"
    "    );\n"
    "\n"
    "    vec4 bloom = vec4(0.0);\n"
    "    for (int i = 0; i < 7; ++i) {\n"
    "        bloom += texture(inputTex, TexCoord + poisson[i] * bloomScale);\n"
    "    }\n"
    "    bloom /= 7.0;\n"
    "    FragColor = bloom;\n"
    "}\n";

static const char *present_fragment_src =
    "#version 330 core\n"
    "uniform sampler2D preBloomTex;\n"
    "uniform sampler2D upsampledTex;\n"
    "uniform float bloomScalar;\n"
    "uniform float bloomPower;\n"
    "in vec2 TexCoord;\n"
    "out vec4 FragColor;\n"
    "\n"
    "vec4 ColorPow(vec4 c, float p) {\n"
    "    float luma = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
    "    if (luma < 0.001) return c;\n"
    "    vec3 color = c.rgb / luma;\n"
    "    float powLuma = pow(luma, p);\n"
    "    return vec4(color * powLuma, c.a);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 preBloom = texture(preBloomTex, TexCoord);\n"
    "    vec4 blurred = texture(upsampledTex, TexCoord);\n"
    "    FragColor = preBloom + ColorPow(blurred, bloomPower) * bloomScalar;\n"
    "}\n";

static const char *passthrough_fragment_src =
    "#version 330 core\n"
    "uniform sampler2D scene;\n"
    "in vec2 TexCoord;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = texture(scene, TexCoord);\n"
    "}\n";

static bool g_enabled = true;
static bool g_even_frame = true;

static GLuint g_prog_composite = 0;
static GLuint g_prog_screen = 0;
static GLuint g_prog_post = 0;
static GLuint g_prog_present = 0;
static GLuint g_prog_passthrough = 0;

static GLuint g_quad_vao = 0;
static GLuint g_quad_vbo = 0;

typedef struct {
    GLuint fbo;
    GLuint texture;
    int width;
    int height;
} render_target_t;

static render_target_t g_rt_scene = {0};
static render_target_t g_rt_composite_even = {0};
static render_target_t g_rt_composite_odd = {0};
static render_target_t g_rt_screen = {0};
static render_target_t g_rt_downsample = {0};
static render_target_t g_rt_upsample = {0};

static GLuint g_tex_shadow_mask = 0;
static GLuint g_tex_artifacts = 0;

static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "shader: compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(GLuint vert, GLuint frag) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "shader: link error: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static GLuint build_program(const char *frag_src) {
    GLuint vert = compile_shader(GL_VERTEX_SHADER, quad_vertex_src);
    if (!vert) return 0;

    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    if (!frag) {
        glDeleteShader(vert);
        return 0;
    }

    GLuint prog = link_program(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

static bool create_render_target(render_target_t *rt, int width, int height) {
    if (rt->fbo && rt->width == width && rt->height == height) {
        return true;
    }

    if (rt->texture) glDeleteTextures(1, &rt->texture);
    if (rt->fbo) glDeleteFramebuffers(1, &rt->fbo);

    rt->width = width;
    rt->height = height;

    glGenTextures(1, &rt->texture);
    glBindTexture(GL_TEXTURE_2D, rt->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &rt->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           rt->texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "shader: FBO incomplete (%dx%d)\n", width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

static void destroy_render_target(render_target_t *rt) {
    if (rt->texture) { glDeleteTextures(1, &rt->texture); rt->texture = 0; }
    if (rt->fbo) { glDeleteFramebuffers(1, &rt->fbo); rt->fbo = 0; }
    rt->width = 0;
    rt->height = 0;
}

static void ensure_render_targets(int width, int height) {
    create_render_target(&g_rt_scene, width, height);
    create_render_target(&g_rt_composite_even, width, height);
    create_render_target(&g_rt_composite_odd, width, height);
    create_render_target(&g_rt_screen, width, height);

    int ds_w = width / BLOOM_DIVISOR;
    int ds_h = height / BLOOM_DIVISOR;
    if (ds_w < 1) ds_w = 1;
    if (ds_h < 1) ds_h = 1;
    create_render_target(&g_rt_downsample, ds_w, ds_h);
    create_render_target(&g_rt_upsample, width, height);
}

static void create_quad(void) {
    float quad_vertices[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
    };

    glGenVertexArrays(1, &g_quad_vao);
    glGenBuffers(1, &g_quad_vbo);

    glBindVertexArray(g_quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

static void draw_fullscreen_quad(void) {
    glBindVertexArray(g_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

static void generate_shadow_mask(void) {
    const int w = 64;
    const int h = 32;
    uint8_t *pixels = calloc(w * h * 3, 1);
    if (!pixels) return;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int col = (x / 2) % 3;
            int idx = (y * w + x) * 3;
            pixels[idx + 0] = (col == 0) ? 255 : 20;
            pixels[idx + 1] = (col == 1) ? 255 : 20;
            pixels[idx + 2] = (col == 2) ? 255 : 20;
        }
    }

    glGenTextures(1, &g_tex_shadow_mask);
    glBindTexture(GL_TEXTURE_2D, g_tex_shadow_mask);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    free(pixels);
}

static void generate_artifacts_texture(void) {
    const int w = 256;
    const int h = 224;
    uint8_t *pixels = calloc(w * h * 3, 1);
    if (!pixels) return;

    const float PI = 3.14159265358979f;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float phase = (float)(x + y) * (2.0f * PI / 3.0f);
            float r = 0.5f + 0.5f * cosf(phase);
            float g = 0.5f + 0.5f * cosf(phase + 2.094395f);
            float b = 0.5f + 0.5f * cosf(phase + 4.188790f);

            int idx = (y * w + x) * 3;
            pixels[idx + 0] = (uint8_t)(r * 255.0f);
            pixels[idx + 1] = (uint8_t)(g * 255.0f);
            pixels[idx + 2] = (uint8_t)(b * 255.0f);
        }
    }

    glGenTextures(1, &g_tex_artifacts);
    glBindTexture(GL_TEXTURE_2D, g_tex_artifacts);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    free(pixels);
}

bool shader_init(void) {
    g_prog_composite = build_program(composite_fragment_src);
    if (!g_prog_composite) return false;

    g_prog_screen = build_program(screen_fragment_src);
    if (!g_prog_screen) return false;

    g_prog_post = build_program(post_fragment_src);
    if (!g_prog_post) return false;

    g_prog_present = build_program(present_fragment_src);
    if (!g_prog_present) return false;

    g_prog_passthrough = build_program(passthrough_fragment_src);
    if (!g_prog_passthrough) return false;

    create_quad();
    generate_shadow_mask();
    generate_artifacts_texture();

    return true;
}

void shader_cleanup(void) {
    if (g_prog_composite) { glDeleteProgram(g_prog_composite); g_prog_composite = 0; }
    if (g_prog_screen) { glDeleteProgram(g_prog_screen); g_prog_screen = 0; }
    if (g_prog_post) { glDeleteProgram(g_prog_post); g_prog_post = 0; }
    if (g_prog_present) { glDeleteProgram(g_prog_present); g_prog_present = 0; }
    if (g_prog_passthrough) { glDeleteProgram(g_prog_passthrough); g_prog_passthrough = 0; }

    if (g_quad_vbo) { glDeleteBuffers(1, &g_quad_vbo); g_quad_vbo = 0; }
    if (g_quad_vao) { glDeleteVertexArrays(1, &g_quad_vao); g_quad_vao = 0; }

    destroy_render_target(&g_rt_scene);
    destroy_render_target(&g_rt_composite_even);
    destroy_render_target(&g_rt_composite_odd);
    destroy_render_target(&g_rt_screen);
    destroy_render_target(&g_rt_downsample);
    destroy_render_target(&g_rt_upsample);

    if (g_tex_shadow_mask) { glDeleteTextures(1, &g_tex_shadow_mask); g_tex_shadow_mask = 0; }
    if (g_tex_artifacts) { glDeleteTextures(1, &g_tex_artifacts); g_tex_artifacts = 0; }
}

void shader_set_enabled(bool enabled) {
    g_enabled = enabled;
}

bool shader_is_enabled(void) {
    return g_enabled;
}

void shader_begin_scene(int width, int height) {
    ensure_render_targets(width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, g_rt_scene.fbo);
    glViewport(0, 0, width, height);
}

void shader_end_scene(float time, int width, int height) {
    (void)time;

    ensure_render_targets(width, height);

    if (!g_enabled) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(g_prog_passthrough);
        glUniform1i(glGetUniformLocation(g_prog_passthrough, "scene"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_rt_scene.texture);
        draw_fullscreen_quad();
        glUseProgram(0);
        return;
    }

    render_target_t *current_composite = g_even_frame ? &g_rt_composite_even : &g_rt_composite_odd;
    render_target_t *prev_composite = g_even_frame ? &g_rt_composite_odd : &g_rt_composite_even;

    {
        glBindFramebuffer(GL_FRAMEBUFFER, current_composite->fbo);
        glViewport(0, 0, current_composite->width, current_composite->height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(g_prog_composite);

        glUniform1i(glGetUniformLocation(g_prog_composite, "curFrame"), 0);
        glUniform1i(glGetUniformLocation(g_prog_composite, "prevFrame"), 1);
        glUniform1i(glGetUniformLocation(g_prog_composite, "artifactTex"), 2);

        glUniform2f(glGetUniformLocation(g_prog_composite, "rcpScrWidth"),
                    1.0f / (float)width, 0.0f);
        glUniform2f(glGetUniformLocation(g_prog_composite, "rcpScrHeight"),
                    0.0f, 1.0f / (float)height);

        glUniform1f(glGetUniformLocation(g_prog_composite, "tuning_sharp"), TUNING_SHARP);
        glUniform4f(glGetUniformLocation(g_prog_composite, "tuning_persistence"),
                    TUNING_PERSISTENCE_R, TUNING_PERSISTENCE_G, TUNING_PERSISTENCE_B, 0.0f);
        glUniform1f(glGetUniformLocation(g_prog_composite, "tuning_bleed"), TUNING_BLEED);
        glUniform1f(glGetUniformLocation(g_prog_composite, "tuning_artifacts"), TUNING_ARTIFACTS);
        glUniform1f(glGetUniformLocation(g_prog_composite, "ntscLerp"), g_even_frame ? 0.0f : 1.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_rt_scene.texture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, prev_composite->texture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, g_tex_artifacts);

        draw_fullscreen_quad();
    }

    {
        glBindFramebuffer(GL_FRAMEBUFFER, g_rt_screen.fbo);
        glViewport(0, 0, g_rt_screen.width, g_rt_screen.height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(g_prog_screen);

        glUniform1i(glGetUniformLocation(g_prog_screen, "compFrame"), 0);
        glUniform1i(glGetUniformLocation(g_prog_screen, "shadowMask"), 1);

        glUniform1f(glGetUniformLocation(g_prog_screen, "tuning_overscan"), TUNING_OVERSCAN);
        glUniform1f(glGetUniformLocation(g_prog_screen, "tuning_barrel"), TUNING_BARREL);
        glUniform1f(glGetUniformLocation(g_prog_screen, "tuning_mask_brightness"), TUNING_MASK_BRIGHTNESS);
        glUniform1f(glGetUniformLocation(g_prog_screen, "tuning_mask_opacity"), TUNING_MASK_OPACITY);
        glUniform1f(glGetUniformLocation(g_prog_screen, "tuning_satur"), TUNING_SATUR);

        float mask_scale_x = (float)width * 0.5f / 64.0f;
        float mask_scale_y = (float)height / 32.0f;
        glUniform2f(glGetUniformLocation(g_prog_screen, "maskScale"), mask_scale_x, mask_scale_y);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, current_composite->texture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_tex_shadow_mask);

        draw_fullscreen_quad();
    }

    {
        glBindFramebuffer(GL_FRAMEBUFFER, g_rt_downsample.fbo);
        glViewport(0, 0, g_rt_downsample.width, g_rt_downsample.height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(g_prog_post);

        glUniform1i(glGetUniformLocation(g_prog_post, "inputTex"), 0);

        float inv_aspect = (float)height / (float)width;
        glUniform2f(glGetUniformLocation(g_prog_post, "bloomScale"),
                    inv_aspect * TUNING_BLOOM_DOWN_SPREAD, TUNING_BLOOM_DOWN_SPREAD);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_rt_screen.texture);

        draw_fullscreen_quad();
    }

    {
        glBindFramebuffer(GL_FRAMEBUFFER, g_rt_upsample.fbo);
        glViewport(0, 0, g_rt_upsample.width, g_rt_upsample.height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(g_prog_post);

        glUniform1i(glGetUniformLocation(g_prog_post, "inputTex"), 0);

        float inv_aspect = (float)height / (float)width;
        glUniform2f(glGetUniformLocation(g_prog_post, "bloomScale"),
                    TUNING_BLOOM_UP_SPREAD, inv_aspect * TUNING_BLOOM_UP_SPREAD);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_rt_downsample.texture);

        draw_fullscreen_quad();
    }

    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(g_prog_present);

        glUniform1i(glGetUniformLocation(g_prog_present, "preBloomTex"), 0);
        glUniform1i(glGetUniformLocation(g_prog_present, "upsampledTex"), 1);
        glUniform1f(glGetUniformLocation(g_prog_present, "bloomScalar"), TUNING_BLOOM_INTENSITY);
        glUniform1f(glGetUniformLocation(g_prog_present, "bloomPower"), TUNING_BLOOM_POWER);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_rt_screen.texture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_rt_upsample.texture);

        draw_fullscreen_quad();
    }

    glUseProgram(0);

    g_even_frame = !g_even_frame;
}
