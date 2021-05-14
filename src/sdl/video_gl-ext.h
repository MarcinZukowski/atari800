#ifndef SDL_VIDEO_GL_EXT_H
#define SDL_VIDEO_GL_EXT_H

#include <stdlib.h>

struct lua_State;

typedef unsigned int GLuint;

typedef struct gl_texture {
	int width;
	int height;
	int num_pixels;
	size_t num_bytes;

	unsigned char *data;

	GLuint gl_id;
} gl_texture;

gl_texture gl_texture_new(int width, int height);
gl_texture gl_texture_load_rgba(const char* fname, int width, int height);
void gl_texture_finalize(gl_texture *t);
#define Z_VALUE_2D -2.0f  // value for drawing textures in 2d
void gl_texture_draw(gl_texture *t,
        float tex_l, float tex_r, float tex_t, float tex_b,
        float scr_l, float scr_r, float scr_t, float scr_b,
		float z);

struct gl_obj;
struct gl_obj* gl_obj_load(const char *path);
void gl_obj_render(struct gl_obj *o);
void gl_obj_render_colorized(struct gl_obj *o, float multR, float multG, float multB);

void gl_lua_ext_init(struct lua_State *L);

#endif  /* SDL_VIDEO_GL_EXT_H */
