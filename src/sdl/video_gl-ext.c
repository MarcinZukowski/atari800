#include "sdl/video_gl-ext.h"
#include "sdl/video_gl-common.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* gl_texture code ******************************************************************* */

gl_texture gl_texture_new(int width, int height)
{
	gl_texture t;

	t.width = width;
	t.height = height;
	t.num_pixels = width * height;
	t.num_bytes = t.num_pixels * 4;

	t.data = malloc(t.num_bytes);
	assert(t.data);

	gl.GenTextures(1, &t.gl_id);

	return t;
}

gl_texture gl_texture_load_rgba(const char* fname, int width, int height)
{
	FILE *f;
	size_t res;

	gl_texture t = gl_texture_new(width, height);

	printf("Loading: %s (%d x %d)\n", fname, width, height);
	f = fopen(fname, "rb");
	assert(f);
	res = fread(t.data, 1, t.num_bytes, f);
	assert(res == t.num_bytes);
	fclose(f);

	printf("Texture %s loaded, id=%d\n", fname, t.gl_id);

	return t;
}

void gl_texture_finalize(gl_texture *t)
{
	gl.BindTexture(GL_TEXTURE_2D, t->gl_id);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, /*border=*/0,
		GL_RGBA, GL_UNSIGNED_BYTE, t->data);

	GLint filtering = SDL_VIDEO_GL_filtering ? GL_LINEAR : GL_NEAREST;
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
}

void gl_texture_draw(gl_texture *t,
		float tex_l, float tex_r, float tex_t, float tex_b,
		float scr_l, float scr_r, float scr_t, float scr_b,
		float z)
{
	gl.BindTexture(GL_TEXTURE_2D, t->gl_id);

	gl.Begin(GL_QUADS);
	gl.TexCoord2f(tex_l, tex_b);
	gl.Vertex3f(scr_l, scr_b, z);
	gl.TexCoord2f(tex_r, tex_b);
	gl.Vertex3f(scr_r, scr_b, z);
	gl.TexCoord2f(tex_r, tex_t);
	gl.Vertex3f(scr_r, scr_t, z);
	gl.TexCoord2f(tex_l, tex_t);
	gl.Vertex3f(scr_l, scr_t, z);
	gl.End();
}
