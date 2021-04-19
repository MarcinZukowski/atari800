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

gl_texture gl_texture_load_rgba(const char* fname, int width, int height);
void gl_texture_finalize(gl_texture *t);

struct gl_obj;
struct gl_obj* gl_obj_load(const char *path);
void gl_obj_render(struct gl_obj *o);

void gl_lua_ext_init(struct lua_State *L);

void xx_init(void);
void xx_draw(void);

#endif  /* SDL_VIDEO_GL_EXT_H */
