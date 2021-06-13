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

#include "ext-lua.h"
#include "ext.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <lua/lua.h>
#include <lua/lualib.h>
#include <lua/lauxlib.h>

#include "akey.h"
#include "memory.h"
#include "antic.h"
#include "cpu.h"
#include "ui.h"

#include "sdl/video_gl.h"

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

static int ext_lua_eval(const char* str)
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

int ext_lua_run_str(const char *str)
{
	printf("\nRUNNING: %s\n", str);
	return ext_lua_eval(str);
}

static int ext_lua_run_file(const char *fname)
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
	res = ext_lua_eval(str);
	printf("%s: File evaluated, exit code: %d\n", __FUNCTION__, res);
	return res;
}

static int ext_lua_antic_dlist(lua_State* L) {
    lua_pushnumber(L, ANTIC_dlist);
    return 1;
}

// Lua-specific extension state
typedef struct {
	const char* name;

	// All Lua extensions for now use the simple memory fingerprint mechanism
	int enable_check_address;
	byte *enable_check_fingerprint;
	int enable_check_fingerprint_size;

	// Compatible with ext_state::injection_list, ends with -1 if present
	int *code_injection_list;
	int code_injection_function;
} ext_lua_state;

static int ext_lua_shared_initialize(ext_state *state)
{
	ext_lua_state *els = (ext_lua_state*)state->internal_state;
	EXT_ASSERT_NOT_NULL(els);

	if (memcmp(MEMORY_mem + els->enable_check_address,
			els->enable_check_fingerprint,
			els->enable_check_fingerprint_size)) {
		// No match
		return 0;
	}

	// Match
	return 1;
}

static int ext_lua_shared_code_injection(ext_state *state, int pc, int op)
{
	ext_lua_state *els = (ext_lua_state*)state->internal_state;
	EXT_ASSERT_NOT_NULL(els);

	EXT_ASSERT_GT(els->code_injection_function, 0);

	// Call the provided extension's function
	lua_geti(L, LUA_REGISTRYINDEX, els->code_injection_function);
	lua_pushnumber(L, pc);
	lua_pushnumber(L, op);

	if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
		EXT_ERROR("Failed calling lua function: %s", lua_tostring(L, -1));
	}

	int ret = luaL_checkinteger(L, -1);
	lua_pop(L, -1);

	return ret;
}

// Lua wrapper over ext_fakecpu_until_op
static int ext_lua_fakecpu_until_op(lua_State *L)
{
	int op = luaL_checkinteger(L, 1);
	int ret = ext_fakecpu_until_op(op);
	lua_pushinteger(L, ret);
	return 1;
}

// Function called by Lua scripts
static int ext_lua_register(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	ext_lua_state *els = malloc(sizeof(ext_lua_state));
	EXT_ASSERT_NOT_NULL(els);
	memset(els, 0, sizeof(*els));

	lua_getfield(L, 1, "NAME");
	els->name = luaL_checkstring(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, 1, "ENABLE_CHECK_ADDRESS");
	els->enable_check_address = luaL_checkinteger(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, 1, "ENABLE_CHECK_FINGERPRINT");
	luaL_checktype(L, -1, LUA_TTABLE);
	int sz = lua_rawlen(L, -1);
	EXT_ASSERT_GT(sz, 0);
	els->enable_check_fingerprint_size = sz;
	els->enable_check_fingerprint = malloc(sz);
	EXT_ASSERT_NOT_NULL(els->enable_check_fingerprint);
	for (int i = 0; i < sz; i++) {
		lua_rawgeti(L, -1, i + 1);  // push next value on stack
		int val = luaL_checkinteger(L, -1);  // Note, doesn't pop from stack
		EXT_ASSERT_BETWEEN(val, 0x00, 0xFF);
		els->enable_check_fingerprint[i] = val;
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	lua_getfield(L, 1, "CODE_INJECTION_LIST");
	if (!lua_isnil(L, -1)) {
		luaL_checktype(L, -1, LUA_TTABLE);
		int sz = lua_rawlen(L, -1);
		EXT_ASSERT_GT(sz, 0);
		els->code_injection_list = malloc((sz + 1) * sizeof(*els->code_injection_list));
		EXT_ASSERT_NOT_NULL(els->code_injection_list);
		for (int i = 0; i < sz; i++) {
			lua_rawgeti(L, -1, i + 1);
			int val = luaL_checkinteger(L, -1);
			EXT_ASSERT_BETWEEN(val, 0x0000, 0xFFFF);
			els->code_injection_list[i] = val;
			lua_pop(L, 1);
		}
		els->code_injection_list[sz] = -1;
	}
	lua_pop(L, 1);

	lua_getfield(L, 1, "CODE_INJECTION_FUNCTION");
	if (!lua_isnil(L, -1)) {
		luaL_checktype(L, -1, LUA_TFUNCTION);
		els->code_injection_function = luaL_ref(L, LUA_REGISTRYINDEX);
	} else {
		lua_pop(L, 1);
	}

	if (els->code_injection_list && !els->code_injection_function) {
		EXT_ERROR0("Can't have CODE_INJECTION_LIST without CODE_INJECTION_FUNCTION");
	}
	if (!els->code_injection_list && els->code_injection_function) {
		EXT_ERROR0("Can't have CODE_INJECTION_FUNCTION without CODE_INJECTION_LIST");
	}

	// Verify we're still sane
	luaL_checktype(L, 1, LUA_TTABLE);

	// Now, register the extension
	ext_state *state = ext_state_alloc();
	state->name = els->name;
	state->internal_state = els;
	state->initialize = ext_lua_shared_initialize;
	state->injection_list = els->code_injection_list;
	if (els->code_injection_function) {
		state->code_injection = ext_lua_shared_code_injection;
	}

	ext_register_ext(state);

	return 0;
}

void ext_lua_init()
{
    printf("Initializing Lua\n");
    L = luaL_newstate();
	assert(L);
	luaL_openlibs(L);

	gl_lua_ext_init(L);

	create_barray_type(L);

	lua_register(L, "a8_memory", get_a8_memory);
	lua_register(L, "antic_dlist", ext_lua_antic_dlist);
	lua_register(L, "ext_register", ext_lua_register);
	lua_register(L, "ext_fakecpu_until_op", ext_lua_fakecpu_until_op);

#define push_integer_value(_id) \
	lua_pushinteger(L, _id); \
	lua_setglobal(L, #_id);

	push_integer_value(OP_RTS);
	push_integer_value(OP_NOP);

//	if(ext_lua_run_file("data/ext/yoomp/script.lua")) {
	if(ext_lua_run_file("data/ext/bjl/init.lua")) {
		printf("exiting\n");
		exit(1);
	};

	char* code [] = {
		"print('Hello, World')",
		"a8mem = a8_memory()",
	};

	for (int c = 0; c < sizeof(code) / sizeof(*code); c++) {
		int res;
		// Here we load the string and use lua_pcall for run the code
		if (ext_lua_run_str(code[c])) {
			printf("exiting\n");
			exit(1);
		}
	}
}

struct lua_State *lua_ext_get_state(void)
{
	return L;
}
