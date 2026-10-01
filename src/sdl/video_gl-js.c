/*
 * sdl/video_gl-js.c - OpenGL bindings for JavaScript extensions
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* The global "gl" object mirrors the legacy OpenGL calls the emulator loads
   in video_gl.c (see struct glapi in video_gl-common.h), minus the "gl"
   prefix: gl.Enable(gl.BLEND). Constants drop the "GL_" prefix, like WebGL.
   gl.createTexture()/gl.loadTextureRGBA() give a Texture whose "pixels"
   Uint8Array is the texture memory itself; gl.drawTriangles() draws flat
   vertex arrays in one call. */

#include "sdl/video_gl-js.h"
#include "sdl/video_gl-common.h"
#include "sdl/video_gl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------ texture helpers ------------------------------ */

#define Z_VALUE_2D -2.0f   /* depth used for flat drawing */

typedef struct gl_texture {
	int width;
	int height;
	size_t num_bytes;       /* width * height * 4 (RGBA) */
	unsigned char *data;
	GLuint gl_id;
} gl_texture;

static gl_texture gl_texture_new(int width, int height)
{
	gl_texture t;
	t.width = width;
	t.height = height;
	t.num_bytes = (size_t) width * height * 4;
	t.data = malloc(t.num_bytes);
	if (t.data == NULL) {
		fprintf(stderr, "out of memory for a %dx%d texture\n", width, height);
		exit(1);
	}
	gl.GenTextures(1, &t.gl_id);
	return t;
}

/* Reads raw RGBA bytes; the caller checked that the file exists. */
static gl_texture gl_texture_load_rgba(const char *fname, int width, int height)
{
	gl_texture t = gl_texture_new(width, height);
	FILE *f = fopen(fname, "rb");
	size_t got = f != NULL ? fread(t.data, 1, t.num_bytes, f) : 0;
	if (f != NULL)
		fclose(f);
	if (got != t.num_bytes)
		printf("Texture %s: read %zu of %zu bytes\n", fname, got, t.num_bytes);
	else
		printf("Texture %s loaded, id=%u\n", fname, t.gl_id);
	return t;
}

/* Uploads the pixels to OpenGL. */
static void gl_texture_finalize(const gl_texture *t)
{
	GLint filtering = SDL_VIDEO_GL_filtering ? GL_LINEAR : GL_NEAREST;
	gl.BindTexture(GL_TEXTURE_2D, t->gl_id);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->data);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
}

/* Draws the texture on a quad: texture coordinates, then screen coordinates. */
static void gl_texture_draw(const gl_texture *t,
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

/* ------------------------------ argument helpers ------------------------------ */

#define ARG_I(n, var) int32_t var; if (JS_ToInt32(ctx, &var, argv[n])) return JS_EXCEPTION
#define ARG_F(n, var) double var##_d; float var; if (JS_ToFloat64(ctx, &var##_d, argv[n])) return JS_EXCEPTION; var = (float) var##_d
#define ARG_D(n, var) double var; if (JS_ToFloat64(ctx, &var, argv[n])) return JS_EXCEPTION
#define NEED(n) if (argc < (n)) return JS_ThrowTypeError(ctx, "%s expects %d arguments", __func__ + 6, (n))

/* Reads up to max floats out of a JS array into out; returns the count. */
static int read_floats(JSContext *ctx, JSValueConst arr, float *out, int max)
{
	JSValue lenv;
	int32_t len, i;
	if (!JS_IsArray(ctx, arr))
		return -1;
	lenv = JS_GetPropertyStr(ctx, arr, "length");
	if (JS_ToInt32(ctx, &len, lenv)) {
		JS_FreeValue(ctx, lenv);
		return -1;
	}
	JS_FreeValue(ctx, lenv);
	if (len > max)
		len = max;
	for (i = 0; i < len; i++) {
		JSValue v = JS_GetPropertyUint32(ctx, arr, i);
		double d;
		int rc = JS_ToFloat64(ctx, &d, v);
		JS_FreeValue(ctx, v);
		if (rc)
			return -1;
		out[i] = (float) d;
	}
	return len;
}

/* ------------------------------ gl.* functions ------------------------------ */

static JSValue js_gl_Enable(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(1); ARG_I(0, cap); gl.Enable(cap); return JS_UNDEFINED;
}
static JSValue js_gl_Disable(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(1); ARG_I(0, cap); gl.Disable(cap); return JS_UNDEFINED;
}
static JSValue js_gl_Begin(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(1); ARG_I(0, mode); gl.Begin(mode); return JS_UNDEFINED;
}
static JSValue js_gl_End(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	gl.End(); return JS_UNDEFINED;
}
static JSValue js_gl_MatrixMode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(1); ARG_I(0, mode); gl.MatrixMode(mode); return JS_UNDEFINED;
}
static JSValue js_gl_PushMatrix(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	gl.PushMatrix(); return JS_UNDEFINED;
}
static JSValue js_gl_PopMatrix(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	gl.PopMatrix(); return JS_UNDEFINED;
}
static JSValue js_gl_LoadIdentity(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	gl.LoadIdentity(); return JS_UNDEFINED;
}
static JSValue js_gl_PushAttrib(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(1); ARG_I(0, mask); gl.PushAttrib(mask); return JS_UNDEFINED;
}
static JSValue js_gl_PopAttrib(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	gl.PopAttrib(); return JS_UNDEFINED;
}
static JSValue js_gl_Clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(1); ARG_I(0, mask); gl.Clear(mask); return JS_UNDEFINED;
}
static JSValue js_gl_ClearColor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(4); ARG_F(0, r); ARG_F(1, g); ARG_F(2, b); ARG_F(3, a); gl.ClearColor(r, g, b, a); return JS_UNDEFINED;
}
static JSValue js_gl_Color4f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(4); ARG_F(0, r); ARG_F(1, g); ARG_F(2, b); ARG_F(3, a); gl.Color4f(r, g, b, a); return JS_UNDEFINED;
}
static JSValue js_gl_TexCoord2f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(2); ARG_F(0, s); ARG_F(1, t); gl.TexCoord2f(s, t); return JS_UNDEFINED;
}
static JSValue js_gl_Vertex3f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(3); ARG_F(0, x); ARG_F(1, y); ARG_F(2, z); gl.Vertex3f(x, y, z); return JS_UNDEFINED;
}
static JSValue js_gl_Normal3f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(3); ARG_F(0, x); ARG_F(1, y); ARG_F(2, z); gl.Normal3f(x, y, z); return JS_UNDEFINED;
}
static JSValue js_gl_Translatef(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(3); ARG_F(0, x); ARG_F(1, y); ARG_F(2, z); gl.Translatef(x, y, z); return JS_UNDEFINED;
}
static JSValue js_gl_Scalef(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(3); ARG_F(0, x); ARG_F(1, y); ARG_F(2, z); gl.Scalef(x, y, z); return JS_UNDEFINED;
}
static JSValue js_gl_Rotatef(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(4); ARG_F(0, a); ARG_F(1, x); ARG_F(2, y); ARG_F(3, z); gl.Rotatef(a, x, y, z); return JS_UNDEFINED;
}
static JSValue js_gl_BlendFunc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(2); ARG_I(0, s); ARG_I(1, d); gl.BlendFunc(s, d); return JS_UNDEFINED;
}
static JSValue js_gl_BindTexture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(2); ARG_I(0, target); ARG_I(1, id); gl.BindTexture(target, id); return JS_UNDEFINED;
}
static JSValue js_gl_TexParameteri(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(3); ARG_I(0, target); ARG_I(1, pname); ARG_I(2, param); gl.TexParameteri(target, pname, param); return JS_UNDEFINED;
}
static JSValue js_gl_PolygonMode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(2); ARG_I(0, face); ARG_I(1, mode); gl.PolygonMode(face, mode); return JS_UNDEFINED;
}
static JSValue js_gl_LineWidth(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(1); ARG_F(0, w); gl.LineWidth(w); return JS_UNDEFINED;
}
static JSValue js_gl_Viewport(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(4); ARG_I(0, x); ARG_I(1, y); ARG_I(2, w); ARG_I(3, h); gl.Viewport(x, y, w, h); return JS_UNDEFINED;
}
static JSValue js_gl_Scissor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(4); ARG_I(0, x); ARG_I(1, y); ARG_I(2, w); ARG_I(3, h); gl.Scissor(x, y, w, h); return JS_UNDEFINED;
}
static JSValue js_gl_Ortho(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(6); ARG_D(0, l); ARG_D(1, r); ARG_D(2, b); ARG_D(3, t); ARG_D(4, n); ARG_D(5, f);
	gl.Ortho(l, r, b, t, n, f); return JS_UNDEFINED;
}
static JSValue js_gl_Frustum(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(6); ARG_D(0, l); ARG_D(1, r); ARG_D(2, b); ARG_D(3, t); ARG_D(4, n); ARG_D(5, f);
	gl.Frustum(l, r, b, t, n, f); return JS_UNDEFINED;
}
static JSValue js_gl_Fogf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	NEED(2); ARG_I(0, pname); ARG_F(1, param); gl.Fogf(pname, param); return JS_UNDEFINED;
}
/* gl.Fogfv(pname, [values]) */
static JSValue js_gl_Fogfv(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float vals[4] = {0, 0, 0, 0};
	NEED(2); ARG_I(0, pname);
	if (read_floats(ctx, argv[1], vals, 4) < 0)
		return JS_ThrowTypeError(ctx, "Fogfv expects an array of numbers");
	gl.Fogfv(pname, vals);
	return JS_UNDEFINED;
}
/* gl.Lightfv(light, pname, [values]) */
static JSValue js_gl_Lightfv(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float vals[4] = {0, 0, 0, 0};
	NEED(3); ARG_I(0, light); ARG_I(1, pname);
	if (read_floats(ctx, argv[2], vals, 4) < 0)
		return JS_ThrowTypeError(ctx, "Lightfv expects an array of numbers");
	gl.Lightfv(light, pname, vals);
	return JS_UNDEFINED;
}
/* gl.GetIntegerv(pname) -> array; 4 values for VIEWPORT and SCISSOR_BOX, 1 otherwise */
static JSValue js_gl_GetIntegerv(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	GLint vals[4] = {0, 0, 0, 0};
	JSValue arr;
	int n, i;
	NEED(1); ARG_I(0, pname);
	n = (pname == GL_VIEWPORT || pname == GL_SCISSOR_BOX) ? 4 : 1;
	gl.GetIntegerv(pname, vals);
	arr = JS_NewArray(ctx);
	for (i = 0; i < n; i++)
		JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, vals[i]));
	return arr;
}

/* Returns the data of a Float32Array (any 4-byte typed array) without copying,
   or NULL with an exception pending. The array keeps the buffer alive. */
static const float *get_float_array(JSContext *ctx, JSValueConst v, size_t *count)
{
	size_t offset = 0, length = 0, elem = 0, size = 0;
	JSValue buf = JS_GetTypedArrayBuffer(ctx, v, &offset, &length, &elem);
	uint8_t *data;
	if (JS_IsException(buf))
		return NULL;
	data = JS_GetArrayBuffer(ctx, &size, buf);
	JS_FreeValue(ctx, buf);
	if (data == NULL || elem != 4) {
		JS_ThrowTypeError(ctx, "expected a Float32Array");
		return NULL;
	}
	*count = length / 4;
	return (const float *) (data + offset);
}

/* gl.drawTriangles(positions, normals) - draws GL_TRIANGLES from flat arrays
   of x, y, z per vertex; normals may be omitted. One call per mesh instead of
   one per vertex. */
static JSValue js_gl_drawTriangles(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const float *pos, *nrm = NULL;
	size_t npos = 0, nnrm = 0, i;
	NEED(1);
	pos = get_float_array(ctx, argv[0], &npos);
	if (pos == NULL)
		return JS_EXCEPTION;
	if (npos % 9 != 0)
		return JS_ThrowRangeError(ctx, "drawTriangles: positions must hold 3 vertices per triangle");
	if (argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
		nrm = get_float_array(ctx, argv[1], &nnrm);
		if (nrm == NULL)
			return JS_EXCEPTION;
		if (nnrm != npos)
			return JS_ThrowRangeError(ctx, "drawTriangles: normals and positions differ in length");
	}
	gl.Begin(GL_TRIANGLES);
	for (i = 0; i < npos; i += 3) {
		if (nrm != NULL)
			gl.Normal3f(nrm[i], nrm[i + 1], nrm[i + 2]);
		gl.Vertex3f(pos[i], pos[i + 1], pos[i + 2]);
	}
	gl.End();
	return JS_UNDEFINED;
}

static void pixels_free(JSRuntime *rt, void *opaque, void *ptr)
{
	free(ptr);
}

/* gl.readPixels(x, y, width, height) -> Uint8Array of RGBA bytes, rows from
   the bottom up, in window pixels. Meant for testing scripts. */
static JSValue js_gl_readPixels(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	static void (APIENTRY *readPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *) = NULL;
	uint8_t *buf;
	size_t size;
	JSValue ab, arr, targv[3];
	NEED(4); ARG_I(0, x); ARG_I(1, y); ARG_I(2, w); ARG_I(3, h);
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
		return JS_ThrowRangeError(ctx, "readPixels: bad size");
	if (readPixels == NULL) {
		readPixels = (void (APIENTRY *)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *)) SDL_GL_GetProcAddress("glReadPixels");
		if (readPixels == NULL)
			return JS_ThrowReferenceError(ctx, "readPixels: glReadPixels not available");
	}
	size = (size_t) w * h * 4;
	buf = malloc(size);
	if (buf == NULL)
		return JS_ThrowOutOfMemory(ctx);
	readPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
	ab = JS_NewArrayBuffer(ctx, buf, size, pixels_free, NULL, 0);
	targv[0] = ab;
	targv[1] = JS_NewInt32(ctx, 0);
	targv[2] = JS_NewInt32(ctx, (int32_t) size);
	arr = JS_NewTypedArray(ctx, 3, targv, JS_TYPED_ARRAY_UINT8);
	JS_FreeValue(ctx, ab);
	return arr;
}

/* ------------------------------ Texture class ------------------------------ */

static JSClassID js_texture_class_id;

/* The pixel buffer is owned by the Uint8Array we hand to scripts, so it stays
   valid for as long as anything references it; only the struct is ours. */
static void texture_finalizer(JSRuntime *rt, JSValue val)
{
	gl_texture *t = JS_GetOpaque(val, js_texture_class_id);
	free(t);
}


static const JSClassDef js_texture_class = { "Texture", texture_finalizer };

static JSValue wrap_texture(JSContext *ctx, gl_texture src)
{
	gl_texture *t = malloc(sizeof(gl_texture));
	JSValue obj, buf, argv[3], pixels;
	if (t == NULL)
		return JS_ThrowOutOfMemory(ctx);
	*t = src;
	obj = JS_NewObjectClass(ctx, js_texture_class_id);
	if (JS_IsException(obj)) {
		free(t);
		return obj;
	}
	JS_SetOpaque(obj, t);
	buf = JS_NewArrayBuffer(ctx, t->data, t->num_bytes, pixels_free, NULL, 0);
	argv[0] = buf;
	argv[1] = JS_NewInt32(ctx, 0);
	argv[2] = JS_NewInt32(ctx, (int32_t) t->num_bytes);
	pixels = JS_NewTypedArray(ctx, 3, argv, JS_TYPED_ARRAY_UINT8);
	JS_FreeValue(ctx, buf);
	JS_DefinePropertyValueStr(ctx, obj, "pixels", pixels, JS_PROP_ENUMERABLE);
	return obj;
}

/* gl.createTexture(width, height) -> Texture with zeroed RGBA pixels */
static JSValue js_gl_createTexture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	gl_texture t;
	NEED(2); ARG_I(0, w); ARG_I(1, h);
	if (w <= 0 || h <= 0)
		return JS_ThrowRangeError(ctx, "createTexture: size must be positive");
	t = gl_texture_new(w, h);
	memset(t.data, 0, t.num_bytes);
	return wrap_texture(ctx, t);
}

/* gl.loadTextureRGBA(path, width, height) -> Texture (call finalize() before use) */
static JSValue js_gl_loadTextureRGBA(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *path;
	gl_texture t;
	FILE *f;
	NEED(3); ARG_I(1, w); ARG_I(2, h);
	if (w <= 0 || h <= 0)
		return JS_ThrowRangeError(ctx, "loadTextureRGBA: size must be positive");
	path = JS_ToCString(ctx, argv[0]);
	if (path == NULL)
		return JS_EXCEPTION;
	/* check first, gl_texture_load_rgba() asserts on a missing file */
	f = fopen(path, "rb");
	if (f == NULL) {
		JSValue e = JS_ThrowReferenceError(ctx, "loadTextureRGBA: cannot open %s", path);
		JS_FreeCString(ctx, path);
		return e;
	}
	fclose(f);
	t = gl_texture_load_rgba(path, w, h);
	JS_FreeCString(ctx, path);
	return wrap_texture(ctx, t);
}

static JSValue js_texture_finalize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	gl_texture *t = JS_GetOpaque2(ctx, this_val, js_texture_class_id);
	if (t == NULL)
		return JS_EXCEPTION;
	gl_texture_finalize(t);
	return JS_UNDEFINED;
}

/* texture.draw(texL, texR, texT, texB, scrL, scrR, scrT, scrB, z = -2) */
static JSValue js_texture_draw(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	gl_texture *t = JS_GetOpaque2(ctx, this_val, js_texture_class_id);
	float z = Z_VALUE_2D;
	if (t == NULL)
		return JS_EXCEPTION;
	if (argc < 8)
		return JS_ThrowTypeError(ctx, "draw expects 8 or 9 arguments");
	{
		ARG_F(0, tl); ARG_F(1, tr); ARG_F(2, tt); ARG_F(3, tb);
		ARG_F(4, sl); ARG_F(5, sr); ARG_F(6, st); ARG_F(7, sb);
		if (argc > 8) {
			ARG_F(8, zz);
			z = zz;
		}
		gl_texture_draw(t, tl, tr, tt, tb, sl, sr, st, sb, z);
	}
	return JS_UNDEFINED;
}

/* gl.drawScreen(x0, y0, x1, y1, left, right, top, bottom, z = -2): draws that
   region of the emulated screen (pixels of the displayed area, y down) onto
   the rectangle given in GL coordinates; for extensions that rearrange the
   screen */
static JSValue js_gl_drawScreen(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	float z = Z_VALUE_2D;
	if (argc < 8)
		return JS_ThrowTypeError(ctx, "drawScreen expects 8 or 9 arguments");
	{
		ARG_F(0, x0); ARG_F(1, y0); ARG_F(2, x1); ARG_F(3, y1);
		ARG_F(4, l); ARG_F(5, r); ARG_F(6, t); ARG_F(7, b);
		if (argc > 8) {
			ARG_F(8, zz);
			z = zz;
		}
		SDL_VIDEO_GL_DrawScreenRegion(x0, y0, x1, y1, l, r, t, b, z);
	}
	return JS_UNDEFINED;
}

/* gl.screenSize() -> [width, height]: the displayed area in pixels */
static JSValue js_gl_screenSize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int w, h;
	JSValue a = JS_NewArray(ctx);
	SDL_VIDEO_GL_ScreenSize(&w, &h);
	JS_SetPropertyUint32(ctx, a, 0, JS_NewInt32(ctx, w));
	JS_SetPropertyUint32(ctx, a, 1, JS_NewInt32(ctx, h));
	return a;
}

enum { TEX_WIDTH, TEX_HEIGHT, TEX_ID };

static JSValue js_texture_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	gl_texture *t = JS_GetOpaque2(ctx, this_val, js_texture_class_id);
	if (t == NULL)
		return JS_EXCEPTION;
	switch (magic) {
	case TEX_WIDTH: return JS_NewInt32(ctx, t->width);
	case TEX_HEIGHT: return JS_NewInt32(ctx, t->height);
	case TEX_ID: return JS_NewInt32(ctx, (int32_t) t->gl_id);
	}
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_texture_proto_funcs[] = {
	JS_CFUNC_DEF("finalize", 0, js_texture_finalize),
	JS_CFUNC_DEF("draw", 9, js_texture_draw),
	JS_CGETSET_MAGIC_DEF("width", js_texture_get, NULL, TEX_WIDTH),
	JS_CGETSET_MAGIC_DEF("height", js_texture_get, NULL, TEX_HEIGHT),
	JS_CGETSET_MAGIC_DEF("id", js_texture_get, NULL, TEX_ID),
};

/* ------------------------------ the gl object ------------------------------ */

#define C(name) JS_PROP_INT32_DEF(#name, GL_##name, JS_PROP_ENUMERABLE)

static const JSCFunctionListEntry js_gl_funcs[] = {
	JS_CFUNC_DEF("Enable", 1, js_gl_Enable),
	JS_CFUNC_DEF("Disable", 1, js_gl_Disable),
	JS_CFUNC_DEF("Begin", 1, js_gl_Begin),
	JS_CFUNC_DEF("End", 0, js_gl_End),
	JS_CFUNC_DEF("MatrixMode", 1, js_gl_MatrixMode),
	JS_CFUNC_DEF("PushMatrix", 0, js_gl_PushMatrix),
	JS_CFUNC_DEF("PopMatrix", 0, js_gl_PopMatrix),
	JS_CFUNC_DEF("LoadIdentity", 0, js_gl_LoadIdentity),
	JS_CFUNC_DEF("PushAttrib", 1, js_gl_PushAttrib),
	JS_CFUNC_DEF("PopAttrib", 0, js_gl_PopAttrib),
	JS_CFUNC_DEF("Clear", 1, js_gl_Clear),
	JS_CFUNC_DEF("ClearColor", 4, js_gl_ClearColor),
	JS_CFUNC_DEF("Color4f", 4, js_gl_Color4f),
	JS_CFUNC_DEF("TexCoord2f", 2, js_gl_TexCoord2f),
	JS_CFUNC_DEF("Vertex3f", 3, js_gl_Vertex3f),
	JS_CFUNC_DEF("Normal3f", 3, js_gl_Normal3f),
	JS_CFUNC_DEF("Translatef", 3, js_gl_Translatef),
	JS_CFUNC_DEF("Scalef", 3, js_gl_Scalef),
	JS_CFUNC_DEF("Rotatef", 4, js_gl_Rotatef),
	JS_CFUNC_DEF("BlendFunc", 2, js_gl_BlendFunc),
	JS_CFUNC_DEF("BindTexture", 2, js_gl_BindTexture),
	JS_CFUNC_DEF("TexParameteri", 3, js_gl_TexParameteri),
	JS_CFUNC_DEF("PolygonMode", 2, js_gl_PolygonMode),
	JS_CFUNC_DEF("LineWidth", 1, js_gl_LineWidth),
	JS_CFUNC_DEF("Viewport", 4, js_gl_Viewport),
	JS_CFUNC_DEF("Scissor", 4, js_gl_Scissor),
	JS_CFUNC_DEF("Ortho", 6, js_gl_Ortho),
	JS_CFUNC_DEF("Frustum", 6, js_gl_Frustum),
	JS_CFUNC_DEF("Fogf", 2, js_gl_Fogf),
	JS_CFUNC_DEF("Fogfv", 2, js_gl_Fogfv),
	JS_CFUNC_DEF("Lightfv", 3, js_gl_Lightfv),
	JS_CFUNC_DEF("GetIntegerv", 1, js_gl_GetIntegerv),
	JS_CFUNC_DEF("createTexture", 2, js_gl_createTexture),
	JS_CFUNC_DEF("drawScreen", 9, js_gl_drawScreen),
	JS_CFUNC_DEF("screenSize", 0, js_gl_screenSize),
	JS_CFUNC_DEF("loadTextureRGBA", 3, js_gl_loadTextureRGBA),
	JS_CFUNC_DEF("drawTriangles", 2, js_gl_drawTriangles),
	JS_CFUNC_DEF("readPixels", 4, js_gl_readPixels),

	/* capabilities */
	C(BLEND), C(DEPTH_TEST), C(LIGHTING), C(LIGHT0), C(LIGHT1), C(FOG), C(SCISSOR_TEST),
	C(TEXTURE_2D), C(CULL_FACE), C(LINE_SMOOTH), C(COLOR_MATERIAL), C(NORMALIZE), C(MULTISAMPLE),
	/* matrices */
	C(MODELVIEW), C(PROJECTION), C(TEXTURE),
	/* queries */
	C(VIEWPORT), C(SCISSOR_BOX), C(SAMPLES),
	/* clear bits, attrib bits */
	C(COLOR_BUFFER_BIT), C(DEPTH_BUFFER_BIT), C(ALL_ATTRIB_BITS), C(ENABLE_BIT), C(LIGHTING_BIT),
	C(CURRENT_BIT), C(LINE_BIT), C(POLYGON_BIT), C(TEXTURE_BIT), C(TRANSFORM_BIT), C(VIEWPORT_BIT), C(SCISSOR_BIT),
	/* primitives */
	C(POINTS), C(LINES), C(LINE_LOOP), C(LINE_STRIP), C(TRIANGLES), C(TRIANGLE_STRIP),
	C(TRIANGLE_FAN), C(QUADS), C(QUAD_STRIP), C(POLYGON),
	/* blending */
	C(ZERO), C(ONE), C(SRC_COLOR), C(ONE_MINUS_SRC_COLOR), C(DST_COLOR), C(ONE_MINUS_DST_COLOR),
	C(SRC_ALPHA), C(ONE_MINUS_SRC_ALPHA), C(DST_ALPHA), C(ONE_MINUS_DST_ALPHA),
	/* textures */
	C(TEXTURE_WRAP_S), C(TEXTURE_WRAP_T), C(TEXTURE_MIN_FILTER), C(TEXTURE_MAG_FILTER),
	C(REPEAT), C(CLAMP), C(CLAMP_TO_EDGE), C(CLAMP_TO_BORDER), C(NEAREST), C(LINEAR),
	C(NEAREST_MIPMAP_NEAREST), C(LINEAR_MIPMAP_NEAREST), C(NEAREST_MIPMAP_LINEAR), C(LINEAR_MIPMAP_LINEAR),
	C(GENERATE_MIPMAP), C(TRUE), C(FALSE),
	/* fog */
	C(FOG_MODE), C(FOG_START), C(FOG_END), C(FOG_DENSITY), C(FOG_COLOR), C(EXP), C(EXP2),
	/* polygon mode, lights */
	C(FRONT), C(BACK), C(FRONT_AND_BACK), C(POINT), C(LINE), C(FILL),
	C(POSITION), C(AMBIENT), C(DIFFUSE), C(SPECULAR),
};

#undef C

void SDL_VIDEO_GL_JS_Init(JSContext *ctx, JSValueConst global)
{
	JSRuntime *rt = JS_GetRuntime(ctx);
	JSValue glo, proto;

	JS_NewClassID(&js_texture_class_id);
	JS_NewClass(rt, js_texture_class_id, &js_texture_class);
	proto = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, proto, js_texture_proto_funcs,
		sizeof(js_texture_proto_funcs) / sizeof(js_texture_proto_funcs[0]));
	JS_SetClassProto(ctx, js_texture_class_id, proto);

	glo = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, glo, js_gl_funcs, sizeof(js_gl_funcs) / sizeof(js_gl_funcs[0]));
	JS_SetPropertyStr(ctx, global, "gl", glo);
}
