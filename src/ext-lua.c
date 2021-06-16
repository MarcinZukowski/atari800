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
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <lua/lua.h>
#include <lua/lualib.h>
#include <lua/lauxlib.h>

#include "akey.h"
#include "memory.h"
#include "antic.h"
#include "cpu.h"
#include "ui.h"
#include "ui_basic.h"

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
	// Do nothing here, as the exposed memory is not managed by us
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
   return 1;
}

static int get_a8_memory (lua_State* L) {
    return expose_barray(L, MEMORY_mem );
}

static lua_State *L = NULL;

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

static int ext_lua_antic_hscrol(lua_State* L) {
    lua_pushnumber(L, ANTIC_HSCROL);
    return 1;
}

/** *********************** SCRIPTING SUPPORT ****************** */

// Lua-specific extension state
typedef struct {
	const char* label;
	const char** options;
	int option_count;
	int current;
	// Handle to the Lua menu table
	int handle;
} ext_lua_menu_item;

typedef struct {
	// Handle to the registered lua table
	int self;

	// Extension name
	const char* name;

	// All Lua extensions for now use the simple memory fingerprint mechanism
	int enable_check_address;
	byte *enable_check_fingerprint;
	int enable_check_fingerprint_size;

	// Compatible with ext_state::injection_list, ends with -1 if present
	int *code_injection_list;
	int code_injection_function;

	// Handles to various functions
	int pre_gl_frame;
	int post_gl_frame;

	// Menu
	ext_lua_menu_item *menu_items;
	int menu_item_count;
	UI_tMenuItem *ui_menu_items;

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

	// Call the provided extension's function
	EXT_ASSERT_GT(els->code_injection_function, 0);
	lua_geti(L, LUA_REGISTRYINDEX, els->code_injection_function);
	// Push "self" parameter
	lua_geti(L, LUA_REGISTRYINDEX, els->self);
	// Push other params
	lua_pushnumber(L, pc);
	lua_pushnumber(L, op);
	if (lua_pcall(L, 3, 1, 0) != LUA_OK) {
		EXT_ERROR("Failed calling lua function: %s", lua_tostring(L, -1));
	}

	int ret = luaL_checkinteger(L, -1);
	lua_pop(L, -1);

	return ret;
}

static void ext_lua_refresh_config(ext_state *state)
{
	ext_lua_state *els = (ext_lua_state*)state->internal_state;
	EXT_ASSERT_NOT_NULL(els);
	EXT_ASSERT_GT(els->menu_item_count, 0);

	for (int i = 0; i < els->menu_item_count; i++) {
		ext_lua_menu_item *item = els->menu_items + i;
		int current = item->current;
		// Update UI
		els->ui_menu_items[i].suffix = item->options[current];
		// Update LUA state
		lua_geti(L, LUA_REGISTRYINDEX, item->handle);
		luaL_checktype(L, -1, LUA_TTABLE);
		lua_pushstring(L, "CURRENT");
		lua_pushnumber(L, current);
		lua_settable(L, -3);
		lua_pop(L, 1);
	}
}

static struct UI_tMenuItem* ext_lua_shared_get_config(ext_state *state)
{
	ext_lua_state *els = (ext_lua_state*)state->internal_state;
	EXT_ASSERT_NOT_NULL(els);
	EXT_ASSERT_GT(els->menu_item_count, 0);

	ext_lua_refresh_config(state);

	return els->ui_menu_items;
}

static void ext_lua_shared_handle_config(ext_state *state, int option)
{
	ext_lua_state *els = (ext_lua_state*)state->internal_state;
	EXT_ASSERT_NOT_NULL(els);
	EXT_ASSERT_GT(els->menu_item_count, 0);

	EXT_ASSERT_BETWEEN(option, 0, els->menu_item_count - 1);
	ext_lua_menu_item *item = els->menu_items + option;
	item->current = (item->current + 1) % item->option_count;

	ext_lua_refresh_config(state);
}


static void ext_lua_shared_pre_gl_frame(ext_state *state)
{
	ext_lua_state *els = (ext_lua_state*)state->internal_state;
	EXT_ASSERT_NOT_NULL(els);

	// Call the provided extension's function
	EXT_ASSERT_GT(els->pre_gl_frame, 0);
	lua_geti(L, LUA_REGISTRYINDEX, els->pre_gl_frame);
	// Push "self" parameter
	lua_geti(L, LUA_REGISTRYINDEX, els->self);
	// Call
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		EXT_ERROR("Failed calling lua function: %s", lua_tostring(L, -1));
	}
}

static void ext_lua_shared_post_gl_frame(ext_state *state)
{
	ext_lua_state *els = (ext_lua_state*)state->internal_state;
	EXT_ASSERT_NOT_NULL(els);

	// Call the provided extension's function
	EXT_ASSERT_GT(els->post_gl_frame, 0);
	lua_geti(L, LUA_REGISTRYINDEX, els->post_gl_frame);
	// Push "self" parameter
	lua_geti(L, LUA_REGISTRYINDEX, els->self);
	// Call
	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		EXT_ERROR("Failed calling lua function: %s", lua_tostring(L, -1));
	}
}



// Lua wrapper over ext_fakecpu_until_op
static int ext_lua_fakecpu_until_op(lua_State *L)
{
	int op = luaL_checkinteger(L, 1);
	int ret = ext_fakecpu_until_op(op);
	lua_pushinteger(L, ret);
	return 1;
}

// Lua wrapper over ext_fakecpu_until_pc
static int ext_lua_fakecpu_until_pc(lua_State *L)
{
	int op = luaL_checkinteger(L, 1);
	int ret = ext_fakecpu_until_pc(op);
	lua_pushinteger(L, ret);
	return 1;
}

static int ext_lua_print_fps(lua_State *L)
{
	int value = luaL_checkinteger(L, 1);
	int color1 = luaL_checkinteger(L, 2);
	int color2 = luaL_checkinteger(L, 3);
	int posx = luaL_checkinteger(L, 4);
	int posy = luaL_checkinteger(L, 5);

	const char *fps_str = ext_fps_str(value);
	Print(color1, color2, fps_str, posx, posy, 20);

	return 0;
}

static int ext_lua_acceleration_disabled(lua_State *L)
{
	lua_pushboolean(L, ext_acceleration_disabled());
	return 1;
}

static int hlp_lua_tablesize(lua_State *L, int idx)
{
	int sz = 0;
	lua_pushnil(L);
	while (lua_next(L, idx - 1)) {
		sz++;
		lua_pop(L, 1);
	}
	return sz;
}

// Function called by Lua scripts
static int ext_lua_register(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	ext_lua_state *els = malloc(sizeof(ext_lua_state));
	EXT_ASSERT_NOT_NULL(els);
	memset(els, 0, sizeof(*els));

	lua_pushvalue(L, -1);
	els->self = luaL_ref(L, LUA_REGISTRYINDEX);

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

	lua_getfield(L, 1, "PRE_GL_FRAME");
	if (!lua_isnil(L, -1)) {
		luaL_checktype(L, -1, LUA_TFUNCTION);
		els->pre_gl_frame = luaL_ref(L, LUA_REGISTRYINDEX);
	} else {
		lua_pop(L, 1);
	}

	lua_getfield(L, 1, "POST_GL_FRAME");
	if (!lua_isnil(L, -1)) {
		luaL_checktype(L, -1, LUA_TFUNCTION);
		els->post_gl_frame = luaL_ref(L, LUA_REGISTRYINDEX);
	} else {
		lua_pop(L, 1);
	}

	lua_getfield(L, 1, "MENU");
	if (!lua_isnil(L, -1)) {
		luaL_checktype(L, -1, LUA_TTABLE);
		int sz = hlp_lua_tablesize(L, -1);
		EXT_ASSERT_GT(sz, 0);
		ext_lua_menu_item *items = malloc(sz * sizeof(*items));
		els->menu_items = items;
		EXT_ASSERT_NOT_NULL(items);
		els->menu_item_count = sz;

		// Iterate over menu items
		lua_pushnil(L);
		int idx = 0;
		while (lua_next(L, -2)) {
			ext_lua_menu_item *item = items + idx++;
			memset(item, 0, sizeof(*item));
			EXT_ASSERT_EQ(lua_type(L, -1), LUA_TTABLE);
			EXT_ASSERT_EQ(lua_type(L, -2), LUA_TSTRING);

			// Parse one menu item from table at -1

			// Save the handle to that item
			lua_pushvalue(L, -1);
			item->handle = luaL_ref(L, LUA_REGISTRYINDEX);

			// LABEL
			lua_getfield(L, -1, "LABEL");
			item->label = luaL_checkstring(L, -1);
			lua_pop(L, 1);  // pop label

			// OPTIONS
			lua_getfield(L, -1, "OPTIONS");
			luaL_checktype(L, -1, LUA_TTABLE);
			item->option_count = lua_rawlen(L, -1);
			item->options = malloc(sizeof(*item->options) * item->option_count);
			for (int i = 0; i < item->option_count; i++) {
				lua_rawgeti(L, -1, i + 1);
				item->options[i] = luaL_checkstring(L, -1);
				lua_pop(L, 1);  // pop current option
			}
			lua_pop(L, 1);  // pop options table

			// CURRENT
			lua_getfield(L, -1, "CURRENT");
			if (lua_isnil(L, -1)) {
				lua_pop(L, 1);
				item->current = 0;
			} else {
				item->current = luaL_checkinteger(L, -1);
				lua_pop(L, 1);  // pop current
			}

			lua_pop(L, 1);  // pop table
		}
		// Create a structure for UI handler
		els->ui_menu_items = malloc(sizeof(*els->ui_menu_items) * (els->menu_item_count + 1));
		for (int i = 0; i < els->menu_item_count; i++) {
			els->ui_menu_items[i] = (UI_tMenuItem) UI_MENU_ACTION(i, els->menu_items[i].label);
		}
		els->ui_menu_items[els->menu_item_count] = 	(UI_tMenuItem) UI_MENU_END;
	} else {
		lua_pop(L, 1);
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
	if (els->pre_gl_frame) {
		state->pre_gl_frame = ext_lua_shared_pre_gl_frame;
	}
	if (els->post_gl_frame) {
		state->post_gl_frame = ext_lua_shared_post_gl_frame;
	}
	if (els->menu_item_count > 0) {
		state->get_config = ext_lua_shared_get_config;
		state->handle_config = ext_lua_shared_handle_config;
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

	// Register OpenGL extensions
	gl_lua_ext_init(L);

	// Register various a8-specific types
	create_barray_type(L);

	lua_register(L, "a8_memory", get_a8_memory);
	lua_register(L, "antic_dlist", ext_lua_antic_dlist);
	lua_register(L, "antic_hscrol", ext_lua_antic_hscrol);
	lua_register(L, "ext_register", ext_lua_register);
	lua_register(L, "ext_fakecpu_until_op", ext_lua_fakecpu_until_op);
	lua_register(L, "ext_fakecpu_until_pc", ext_lua_fakecpu_until_pc);
	lua_register(L, "ext_print_fps", ext_lua_print_fps);
	lua_register(L, "ext_acceleration_disabled", ext_lua_acceleration_disabled);

#define push_integer_value(_id) \
	lua_pushinteger(L, _id); \
	lua_setglobal(L, #_id);

	push_integer_value(OP_RTS);
	push_integer_value(OP_NOP);

	// Register common.lua
	if (ext_lua_run_file("data/ext/common.lua")) {
		printf("exiting\n");
		exit(1);
	};

	// Register all files matching: data/ext/*/init.lua
	const char *dirname = "data/ext";
	DIR *dir = opendir(dirname);
	EXT_ASSERT_NOT_NULL(dir);

	struct dirent *dp;
#define BUFSIZE 1000
	char buf[BUFSIZE];
	while ((dp = readdir(dir)) != NULL) {
		// Check if it's a directory
		struct stat stbuf;
		snprintf(buf, BUFSIZE, "%s/%s", dirname, dp->d_name);
		if (stat(buf, &stbuf) == -1) {
			EXT_ERROR("stat(%s) failed", buf);
		}
		if ((stbuf.st_mode & S_IFDIR) == 0) {
			// Skip files
			continue;
		}
		// See if init.lua exists
		snprintf(buf, BUFSIZE, "%s/%s/init.lua", dirname, dp->d_name);
		if (stat(buf, &stbuf) == -1) {
			// File doesn't exist
			continue;
		}
		// Found init.lua, try to load
		if (ext_lua_run_file(buf)) {
			printf("exiting\n");
			exit(1);
		};
	}


#if 0  // some old code
	if(ext_lua_run_file("data/ext/yoomp/script.lua")) {
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
#endif
}

struct lua_State *lua_ext_get_state(void)
{
	return L;
}
