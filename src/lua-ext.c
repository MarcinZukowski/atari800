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

#include "sdl/video_gl.h"

typedef unsigned char byte;

// FROM: https://stackoverflow.com/questions/11689135/share-array-between-lua-and-c

#define LUA_BARRAY "barray"
// metatable method for handling "array[index]"
static int barray_index (lua_State* L) {
    byte** parray = luaL_checkudata(L, 1, LUA_BARRAY);
    int index = luaL_checkinteger(L, 2);
    lua_pushnumber(L, (*parray)[index]);
    return 1;
}

// metatable method for handle "array[index] = value"
static int barray_newindex (lua_State* L) {
    byte** parray = luaL_checkudata(L, 1, LUA_BARRAY);
    int index = luaL_checkinteger(L, 2);
    int value = luaL_checkinteger(L, 3);
    (*parray)[index] = value;
    return 0;
}

static int barray_size(lua_State* L) {
    byte** parray = luaL_checkudata(L, 1, LUA_BARRAY);
    lua_pushnumber(L, 7);
    return 1;
}

static int barray_new(lua_State *L) {
	size_t size = luaL_checkinteger(L, 1);
	byte *ptr = malloc(size);
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
	exit(1);
	return 0;
}

// create a metatable for our array type
static void create_barray_type(lua_State* L) {
   static const struct luaL_Reg barray[] = {
//      { "__index",  barray_index  },    // This doesn't work, unfortunately
      { "__newindex",  barray_newindex  },
      { "__gc",  barray_gc  },
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

lua_State *L = NULL;

static int lext_eval(const char* str)
{
	int res;
	if ((res = luaL_loadstring(L, str)) == LUA_OK) {
		if ((res = lua_pcall(L, 0, 0, 0)) == LUA_OK) {
			// If it was executed successfuly we
			// remove the code from the stack
			lua_pop(L, lua_gettop(L));
		} else {
			const char* errmsg = lua_tostring(L, -1);
			printf("ERROR(call): %s\n", errmsg);
			return 1;
		}
		return 0;
	} else {
		const char* errmsg = lua_tostring(L, -1);
		printf("ERROR(run): %s\n", errmsg);
		return 1;
	}
}

static int lext_run_str(const char *str)
{
	printf("\nRUNNING: %s\n", str);
	return lext_eval(str);
}

static int lext_run_file(const char *fname)
{
	FILE *f;
	int res;
	size_t fsize;

	printf("\n%s: opening %s\n", __FUNCTION__, fname);
	f = fopen(fname, "rb");
	assert(f);

	fseek(f, 0, SEEK_END);
	fsize = ftell(f);
	rewind(f);

	char *str = alloca(fsize+1);

	res = fread(str, 1, fsize, f);
	assert(res == fsize);
	str[fsize] = '\0';
	fclose(f);

	printf("%s: Running from file (%zd bytes)\n", __FUNCTION__, fsize);
	res = lext_eval(str);
	printf("%s: File evaluated, exit code: %d\n", __FUNCTION__, res);
	return res;
}

void lua_ext_init()
{
    printf("Initializing Lua\n");
    L = luaL_newstate();
	assert(L);
	luaL_openlibs(L);


	gl_lua_ext_init(L);

	create_barray_type(L);
    lua_register(L, "a8_memory", get_a8_memory);

	if(lext_run_file("data/ext/yoomp/script.lua")) {
		printf("exiting\n");
		exit(1);
	};

	char* code [] = {
		"print('Hello, World')",
		"a8mem = a8_memory()",
		"yoomp_initialize()",
	};

	for (int c = 0; c < sizeof(code) / sizeof(*code); c++) {
		int res;
		// Here we load the string and use lua_pcall for run the code
		if (lext_run_str(code[c])) {
			printf("exiting\n");
			exit(1);
		}
	}
}

void lua_draw_background()
{
	if (!L) {
		printf("%s: Lua not ready, waiting\n", __FUNCTION__);
		return;
	}

	lext_run_str("yoomp_render_frame()");
}
