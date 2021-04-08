/*
 * lua-ext.c - Lua extension framework for Atari800
 *
 * Copyright (C) 2021 Atari800 development team (see DOC/CREDITS)
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

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <lua/lua.h>
#include <lua/lualib.h>
#include <lua/lauxlib.h>

// #define TINYOBJ_LOADER_C_IMPLEMENTATION
char *dynamic_fgets(char **buf, size_t *size, FILE *file);  //  pacify compiler warnings
#include "tinyobj_loader_c.h"

#include "akey.h"
#include "memory.h"

#ifndef WITH_LUA_EXT
#error "WITH_LUA_EXT is expected"
#endif

typedef unsigned char byte;

// FROM: https://stackoverflow.com/questions/11689135/share-array-between-lua-and-c

#define LUA_BARRAY "barray"
// metatable method for handling "array[index]"
static int barray_index (lua_State* L) {
	printf("barray_index\n");
    byte** parray = luaL_checkudata(L, 1, LUA_BARRAY);
    int index = luaL_checkinteger(L, 2);
    lua_pushnumber(L, (*parray)[index]);
    return 1;
}

// metatable method for handle "array[index] = value"
static int barray_newindex (lua_State* L) {
	printf("barray_newindex\n");
    byte** parray = luaL_checkudata(L, 1, LUA_BARRAY);
    int index = luaL_checkinteger(L, 2);
    int value = luaL_checkinteger(L, 3);
    (*parray)[index] = value;
    return 0;
}

static int barray_size(lua_State* L) {
	printf("barray_size\n");
    byte** parray = luaL_checkudata(L, 1, LUA_BARRAY);
    lua_pushnumber(L, 7);
    return 1;
}

static int barray_new(lua_State *L) {
	size_t size = luaL_checkinteger(L, 1);
	byte *ptr = malloc(size);
	printf("barray_new: %zd %p\n", size, ptr);
    byte** parray = lua_newuserdata(L, sizeof(byte**));
    *parray = ptr;
	luaL_setmetatable(L, LUA_BARRAY);
//    luaL_getmetatable(L, LUA_BARRAY);
//	luaL_setmetatable(L, -2);
	return 1;
}

static int barray_gc(lua_State *L) {
	byte *ptr = *(byte**)(lua_touserdata(L, 1));
	printf("barray_gc: %p\n", ptr);
	free(ptr);
	return 0;
}

// create a metatable for our array type
static void create_barray_type(lua_State* L) {
   static const struct luaL_Reg barray[] = {
//      { "__index",  barray_index  },
      { "__newindex",  barray_newindex  },
      { "size",  barray_size  },
	  { "get", barray_index },
	  { "set", barray_newindex },
      NULL, NULL
   };
   luaL_newmetatable(L, "barray");
   luaL_setfuncs(L, barray, 0);
   // Some magic from https://stackoverflow.com/questions/38162702/how-to-register-c-nested-class-to-lua
   lua_pushvalue(L, -1);
   lua_setfield(L, -2, "__index");
}

// Based on: https://gist.github.com/Youka/2a6e69584672f7cb0331
static void create_barray_type2(lua_State* L) {
	lua_register(L, LUA_BARRAY, barray_new);
	luaL_newmetatable(L, LUA_BARRAY);
	lua_pushcfunction(L, barray_gc); lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, barray_size); lua_setfield(L, -2, "size");
//	lua_pushcfunction(L, barray_index); lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, barray_index); lua_setfield(L, -2, "get");
	lua_pushcfunction(L, barray_newindex); lua_setfield(L, -2, "__newindex");
	lua_pushcfunction(L, barray_newindex); lua_setfield(L, -2, "set");
//	lua_pop(L, 1);
//  lua_setmetatable(L, -2);
}


// expose an array to lua, by storing it in a userdata with the array metatable
static int expose_barray(lua_State* L, byte *barray) {
   byte** parray = lua_newuserdata(L, sizeof(byte**));
   *parray = barray;
   luaL_getmetatable(L, "barray");
   lua_setmetatable(L, -2);
   printf("ok!\n");
   return 1;
}

static int get_a8_memory (lua_State* L) {
	printf("get_a8_memory\n");
    return expose_barray(L, MEMORY_mem );
}

void lua_ext_init()
{
    printf("Initializing Lua\n");
    lua_State *L = luaL_newstate();
	assert(L);
	luaL_openlibs(L);

	create_barray_type(L);

    lua_register(L, "a8_memory", get_a8_memory);

	char* code [] = {
		"print('Hello, World')",
/*
		"print(barray(3))",
		"xx = barray(4); print(xx);",
		"print(xx);",
		"foo = barray(5)",
		"print(foo)",
		"print(foo[0])",
*/
		"a8mem = a8_memory()",
		"print(a8mem[2])",
		"print(a8mem:get(2))",
		"a8mem[2] = 1",
		"print(a8mem:get(2))",
		"print(a8mem)",
		"print(a8mem[2.0])",
		"print(a8mem:size())",
	};

	for (int c = 0; c < sizeof(code) / sizeof(*code); c++) {
		int res;
		// Here we load the string and use lua_pcall for run the code
		printf("\nRUNNING: %s\n", code[c]);
		if (res = luaL_loadstring(L, code[c]) == LUA_OK) {
			if (res = lua_pcall(L, 0, 0, 0) == LUA_OK) {
				// If it was executed successfuly we
				// remove the code from the stack
				lua_pop(L, lua_gettop(L));
			} else {
				const char* errmsg = lua_tostring(L, -1);
				printf("ERROR: %s\n", errmsg);
			}
		} else {
			const char* errmsg = lua_tostring(L, -1);
			printf("ERROR: %s\n", errmsg);
		}
	}
}

#if 0

static void xx_load_file(void *ctx, const char * filename, const int is_mtl, const char *obj_filename, char ** buffer, size_t * len)
{
    long string_size = 0, read_size = 0;
    FILE * handler = fopen(filename, "r");

    if (handler) {
        fseek(handler, 0, SEEK_END);
        string_size = ftell(handler);
        rewind(handler);
        *buffer = (char *) malloc(sizeof(char) * (string_size + 1));
        read_size = fread(*buffer, sizeof(char), (size_t) string_size, handler);
        (*buffer)[string_size] = '\0';
        if (string_size != read_size) {
            free(buffer);
            *buffer = NULL;
        }
        fclose(handler);
    }

    *len = read_size;
}

tinyobj_shape_t * xx_shape = NULL;
tinyobj_material_t * xx_material = NULL;
tinyobj_attrib_t xx_attrib;

static void xx_load_obj()
{

    size_t num_shapes;
    size_t num_materials;

    tinyobj_attrib_init(&xx_attrib);

	chdir("data/ext/yoomp");

//	const char* filename = "beach-ball.obj";
	const char* filename = "ball-yoomp.obj";
//	const char* filename = "ball-amiga.obj";
//	const char* filename = "ball-amiga-2.obj";

	printf("Loading obj: %s\n", filename);

    int result = tinyobj_parse_obj(&xx_attrib, &xx_shape, &num_shapes, &xx_material, &num_materials, filename, xx_load_file, NULL, TINYOBJ_FLAG_TRIANGULATE);

	chdir("../../..");

	assert(result == TINYOBJ_SUCCESS);

	printf("%zd shapes, %zd materials\n", num_shapes, num_materials);
	printf("shape: %s %d %d\n", xx_shape->name, xx_shape->length, xx_shape->face_offset);
	printf("attribs: #v:%d #n:%d #tc:%d #f:%d #fnv: %d\n",
	    xx_attrib.num_vertices, xx_attrib.num_normals, xx_attrib.num_texcoords, xx_attrib.num_faces, xx_attrib.num_face_num_verts);
}

static void xx_v3(int idx)
{
	int v_idx = xx_attrib.faces[idx].v_idx;
	gl.Normal3f(xx_attrib.normals[3 * v_idx], xx_attrib.normals[3 * v_idx + 1], xx_attrib.normals[3 * v_idx + 2]);
	gl.Vertex3f(xx_attrib.vertices[3 * v_idx], xx_attrib.vertices[3 * v_idx + 1], xx_attrib.vertices[3 * v_idx + 2]);
}

static float xx_last = 0;

static void xx_render_ball()
{
	gl.Disable(GL_TEXTURE_2D);

	gl.MatrixMode(GL_MODELVIEW);
	gl.PushMatrix();
	gl.LoadIdentity();

/*
	gl.Enable(GL_LIGHTING);
	gl.Enable(GL_LIGHT0);
	gl.Enable(GL_COLOR_MATERIAL);
	GLfloat light_pos[] = {0, 0.2, 2.5, 0};
	gl.Lightfv(GL_LIGHT0, GL_POSITION, light_pos);
*/
	int last_matid = -1;

	tinyobj_attrib_t *a = &xx_attrib;

	xx_last++;

	const int EQU_BALL_X = 0x0030;
	const int EQU_BALL_VX = 0x0031;
	const int EQU_BALL_VY = 0x0032;

	float ball_angle = MEMORY_mem[EQU_BALL_X];
	float ball_vx = MEMORY_mem[EQU_BALL_VX];
	float ball_vy = MEMORY_mem[EQU_BALL_VY];

	gl.Translatef(
		(ball_vx - 128 + 4) / 84,
		- (ball_vy - 112 - 8) / 120,
		0);
	gl.Scalef(0.05, 0.07, 0.07);
	gl.Rotatef(ball_angle / 256.0 * 360.0, 0, 0, 1);
	gl.Rotatef(11 * xx_last, 1, 0, 0);
	gl.Rotatef(90, 0, 0, 1);

	for (int f = 0; f < a->num_face_num_verts; f++) {
		assert(a->face_num_verts[f] == 3);
		int matid = a->material_ids[f];
		if (f == 0 || matid != last_matid) {
			if (f) {
				gl.End();
			}
			last_matid = matid;
			tinyobj_material_t mat = xx_material[matid];
			gl.Color4f(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1);
			gl.Begin(GL_TRIANGLES);
		}
		xx_v3(3 * f + 0);
		xx_v3(3 * f + 1);
		xx_v3(3 * f + 2);
	}
	gl.End();

	gl.Rotatef(30, 1, 1, 1);

	gl.PopMatrix();

	gl.Enable(GL_TEXTURE_2D);
	gl.Color4f(1, 1, 1, 1);
	gl.Disable(GL_LIGHTING);
	gl.Disable(GL_LIGHT0);
}


typedef struct xx_texture {
	int width;
	int height;
	int pixels;
	size_t bytes;

	unsigned char *data;

	GLuint id;
} xx_texture;

static xx_texture xx_background;

static xx_texture xx_texture_load_rgba(const char* fname, int width, int height)
{
	xx_texture xt;
	FILE *f;
	size_t res;

	xt.width = width;
	xt.height = height;
	xt.pixels = width * height;
	xt.bytes = xt.pixels * 4;

	xt.data = malloc(xt.bytes);

	printf("Loading: %s\n", fname);
	f = fopen(fname, "rb");
	assert(f);
	res = fread(xt.data, 1, xt.bytes, f);
	assert(res == xt.bytes);
	fclose(f);

	gl.GenTextures(1, &xt.id);

	printf("Texture %s loaded, id=%d\n", fname, xt.id);

	return xt;
}

static void xx_texture_finalize(xx_texture *xt)
{
	gl.BindTexture(GL_TEXTURE_2D, xt->id);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, xt->width, xt->height, /*border=*/0,
		GL_RGBA, GL_UNSIGNED_BYTE, xt->data);

	GLint filtering = SDL_VIDEO_GL_filtering ? GL_LINEAR : GL_NEAREST;
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
}

void xx_init()
{
	gl.Enable(GL_BLEND);

	xx_background = xx_texture_load_rgba("data/ext/yoomp/rof.rgba", 476, 476);

	xx_texture *xt = &xx_background;
	float xc = xt->width / 2.0f + 7;
	float yc = xt->height / 2.0f;
	float rad = 120.0;
	float dark = 0.8 * rad;

	// Fix alphas
	for (int y = 0; y < xt->width; y++) {
		for (int x = 0; x < xt->width; x++) {
			int idx = y * xt->width + x;
			float r = sqrt((x - xc) * (x - xc) + (y - yc) * (y - yc));
			int alpha = 0;
			if (r <= dark) {
				alpha = 255 - 255.0 * r / dark;
			} else if (r <= rad) {
				alpha = 0;
			} else {
				alpha = 255;
			}
			xt->data[4 * idx + 3] = alpha;
		}
	}

	xx_texture_finalize(&xx_background);

	xx_load_obj();
}

void xx_draw()
{
	float L = -0.77;
	float R = -L;
	float T = 0.9;
	float B = -0.75;

	float TL = 0.18;
	float TR = 0.84;
	float TT = 0.76;
	float TB = 0.25;

	const Uint8 *state = SDL_GetKeyState(NULL);
	if (!state[SDLK_RETURN]) {
		return;
	}

	gl.BindTexture(GL_TEXTURE_2D, xx_background.id);

	gl.Disable(GL_DEPTH_TEST);
	gl.Color4f(1.0f, 1.0f, 1.0f, 1.0f);
	gl.Enable(GL_BLEND);
	gl.BlendFunc(
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA
	);

	gl.Begin(GL_QUADS);
	gl.TexCoord2f(TL, TB);
	gl.Vertex3f(L, B, -2.0f);
	gl.TexCoord2f(TR, TB);
	gl.Vertex3f(R, B, -2.0f);
	gl.TexCoord2f(TR, TT);
	gl.Vertex3f(R, T, -2.0f);
	gl.TexCoord2f(TL, TT);
	gl.Vertex3f(L, T, -2.0f);
	gl.End();

	gl.Disable(GL_BLEND);

	xx_render_ball();
}
#endif