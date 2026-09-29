/*
 * ext-js.c - JavaScript (QuickJS) scripting for Atari800 extensions
 *
 * Copyright (C) 2021-2026 Atari800 development team (see DOC/CREDITS)
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

/* Scripts see two globals, "a8" (emulator access, extension registration)
   and "gl" (OpenGL, see sdl/video_gl-js.c), plus console.log and the QuickJS
   "std" and "os" modules. Each data/ext/<name>/init.js is evaluated as an
   ES module at startup and is expected to call a8.register(). */

#include "ext-js.h"
#include "ext.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <quickjs/quickjs.h>
#include <quickjs/quickjs-libc.h>

#include "antic.h"
#include "colours.h"
#include "cpu.h"
#include "gtia.h"
#include "memory.h"
#include "ui.h"
#include "ui_basic.h"

#include "sdl/sfx.h"
#include "sdl/video_gl-js.h"

static JSRuntime *rt = NULL;
static JSContext *ctx = NULL;

/* Prints the pending exception (with its stack trace) and exits. Script
   errors are programming errors in a hack, so we do not try to carry on. */
static void js_fatal(const char *where)
{
	printf("JavaScript error in %s:\n", where);
	fflush(stdout);
	js_std_dump_error(ctx);
	exit(2);
}

/* Runs pending promise jobs (there normally are none). */
static void run_jobs(void)
{
	JSContext *c;
	while (JS_ExecutePendingJob(rt, &c) > 0)
		;
}

static char *dup_cstring(JSValueConst v, const char *what)
{
	const char *s = JS_ToCString(ctx, v);
	char *r;
	if (s == NULL)
		js_fatal(what);
	r = strdup(s);
	JS_FreeCString(ctx, s);
	EXT_ASSERT_NOT_NULL(r);
	return r;
}

static int get_int_prop(JSValueConst obj, const char *name, int *out)
{
	JSValue v = JS_GetPropertyStr(ctx, obj, name);
	int rc;
	if (JS_IsException(v))
		return -1;
	if (JS_IsUndefined(v)) {
		JS_FreeValue(ctx, v);
		return 1;
	}
	rc = JS_ToInt32(ctx, out, v);
	JS_FreeValue(ctx, v);
	return rc;
}

static int get_array_length(JSValueConst arr)
{
	int len = -1;
	if (get_int_prop(arr, "length", &len) != 0)
		return -1;
	return len;
}

/* ============================== a8.* functions ============================== */

static JSValue js_a8_fakeCpuUntilPc(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int32_t pc;
	if (JS_ToInt32(c, &pc, argv[0]))
		return JS_EXCEPTION;
	return JS_NewInt32(c, ext_fakecpu_until_pc(pc));
}

static JSValue js_a8_fakeCpuUntilOp(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int32_t op;
	if (JS_ToInt32(c, &op, argv[0]))
		return JS_EXCEPTION;
	return JS_NewInt32(c, ext_fakecpu_until_op(op));
}

static JSValue js_a8_fakeCpuUntilAfterOp(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int32_t op;
	if (JS_ToInt32(c, &op, argv[0]))
		return JS_EXCEPTION;
	return JS_NewInt32(c, ext_fakecpu_until_after_op(op));
}

/* a8.peek(addr) / a8.poke(addr, value): hardware-aware memory access (bank
   switching, ROM, I/O registers), unlike the raw a8.mem array. */
static JSValue js_a8_peek(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int32_t addr;
	if (JS_ToInt32(c, &addr, argv[0]))
		return JS_EXCEPTION;
	if (addr < 0 || addr > 0xffff)
		return JS_ThrowRangeError(c, "peek: address %d out of range", addr);
	return JS_NewInt32(c, MEMORY_GetByte(addr));
}

static JSValue js_a8_poke(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int32_t addr, value;
	UBYTE b;
	if (argc < 2)
		return JS_ThrowTypeError(c, "poke(addr, value) needs 2 arguments");
	if (JS_ToInt32(c, &addr, argv[0]) || JS_ToInt32(c, &value, argv[1]))
		return JS_EXCEPTION;
	if (addr < 0 || addr > 0xffff)
		return JS_ThrowRangeError(c, "poke: address %d out of range", addr);
	b = (UBYTE) (value & 0xff);
	MEMORY_PutByte(addr, b);
	return JS_UNDEFINED;
}

/* a8.cpu.a/x/y/s/p/pc: the 6502 registers, read/write. They are current
   inside onCodeInjection(), where changes take effect on return. */
enum { CPU_A, CPU_X, CPU_Y, CPU_S, CPU_P, CPU_PC };

static JSValue js_a8_cpu_get(JSContext *c, JSValueConst this_val, int magic)
{
	switch (magic) {
	case CPU_A: return JS_NewInt32(c, CPU_regA);
	case CPU_X: return JS_NewInt32(c, CPU_regX);
	case CPU_Y: return JS_NewInt32(c, CPU_regY);
	case CPU_S: return JS_NewInt32(c, CPU_regS);
	case CPU_P: return JS_NewInt32(c, CPU_regP);
	case CPU_PC: return JS_NewInt32(c, CPU_regPC);
	}
	return JS_UNDEFINED;
}

static JSValue js_a8_cpu_set(JSContext *c, JSValueConst this_val, JSValueConst val, int magic)
{
	int32_t v;
	if (JS_ToInt32(c, &v, val))
		return JS_EXCEPTION;
	switch (magic) {
	case CPU_A: CPU_regA = (UBYTE) v; break;
	case CPU_X: CPU_regX = (UBYTE) v; break;
	case CPU_Y: CPU_regY = (UBYTE) v; break;
	case CPU_S: CPU_regS = (UBYTE) v; break;
	case CPU_P: CPU_regP = (UBYTE) v; break;
	case CPU_PC: CPU_regPC = (UWORD) v; break;
	}
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_a8_cpu_funcs[] = {
	JS_CGETSET_MAGIC_DEF("a", js_a8_cpu_get, js_a8_cpu_set, CPU_A),
	JS_CGETSET_MAGIC_DEF("x", js_a8_cpu_get, js_a8_cpu_set, CPU_X),
	JS_CGETSET_MAGIC_DEF("y", js_a8_cpu_get, js_a8_cpu_set, CPU_Y),
	JS_CGETSET_MAGIC_DEF("s", js_a8_cpu_get, js_a8_cpu_set, CPU_S),
	JS_CGETSET_MAGIC_DEF("p", js_a8_cpu_get, js_a8_cpu_set, CPU_P),
	JS_CGETSET_MAGIC_DEF("pc", js_a8_cpu_get, js_a8_cpu_set, CPU_PC),
};

static JSValue js_a8_printFps(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int32_t value, fg, bg, x, y;
	if (argc < 5)
		return JS_ThrowTypeError(c, "printFps(value, fg, bg, x, y) needs 5 arguments");
	if (JS_ToInt32(c, &value, argv[0]) || JS_ToInt32(c, &fg, argv[1]) || JS_ToInt32(c, &bg, argv[2])
	    || JS_ToInt32(c, &x, argv[3]) || JS_ToInt32(c, &y, argv[4]))
		return JS_EXCEPTION;
	Print(fg, bg, ext_fps_str(value), x, y, 20);
	return JS_UNDEFINED;
}

static JSValue js_a8_accelerationDisabled(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	return JS_NewBool(c, ext_acceleration_disabled());
}

/* a8.rgb(colour) -> [r, g, b] with components in 0..255 */
static JSValue js_a8_rgb(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	int32_t colour;
	JSValue arr;
	if (JS_ToInt32(c, &colour, argv[0]))
		return JS_EXCEPTION;
	colour &= 0xff;
	arr = JS_NewArray(c);
	JS_SetPropertyUint32(c, arr, 0, JS_NewInt32(c, Colours_GetR(colour)));
	JS_SetPropertyUint32(c, arr, 1, JS_NewInt32(c, Colours_GetG(colour)));
	JS_SetPropertyUint32(c, arr, 2, JS_NewInt32(c, Colours_GetB(colour)));
	return arr;
}

/* a8.antic.* and a8.gtia.* getters; magic selects the register. */
enum { REG_DLIST, REG_HSCROL, REG_COLBK, REG_COLPF0, REG_COLPF1, REG_COLPF2, REG_COLPF3,
       REG_COLPM0, REG_COLPM1, REG_COLPM2, REG_COLPM3 };

static JSValue js_a8_get_reg(JSContext *c, JSValueConst this_val, int magic)
{
	switch (magic) {
	case REG_DLIST: return JS_NewInt32(c, ANTIC_dlist);
	case REG_HSCROL: return JS_NewInt32(c, ANTIC_HSCROL);
	case REG_COLBK: return JS_NewInt32(c, GTIA_COLBK);
	case REG_COLPF0: return JS_NewInt32(c, GTIA_COLPF0);
	case REG_COLPF1: return JS_NewInt32(c, GTIA_COLPF1);
	case REG_COLPF2: return JS_NewInt32(c, GTIA_COLPF2);
	case REG_COLPF3: return JS_NewInt32(c, GTIA_COLPF3);
	case REG_COLPM0: return JS_NewInt32(c, GTIA_COLPM0);
	case REG_COLPM1: return JS_NewInt32(c, GTIA_COLPM1);
	case REG_COLPM2: return JS_NewInt32(c, GTIA_COLPM2);
	case REG_COLPM3: return JS_NewInt32(c, GTIA_COLPM3);
	}
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_a8_antic_funcs[] = {
	JS_CGETSET_MAGIC_DEF("dlist", js_a8_get_reg, NULL, REG_DLIST),
	JS_CGETSET_MAGIC_DEF("hscrol", js_a8_get_reg, NULL, REG_HSCROL),
};

static const JSCFunctionListEntry js_a8_gtia_funcs[] = {
	JS_CGETSET_MAGIC_DEF("colbk", js_a8_get_reg, NULL, REG_COLBK),
	JS_CGETSET_MAGIC_DEF("colpf0", js_a8_get_reg, NULL, REG_COLPF0),
	JS_CGETSET_MAGIC_DEF("colpf1", js_a8_get_reg, NULL, REG_COLPF1),
	JS_CGETSET_MAGIC_DEF("colpf2", js_a8_get_reg, NULL, REG_COLPF2),
	JS_CGETSET_MAGIC_DEF("colpf3", js_a8_get_reg, NULL, REG_COLPF3),
	JS_CGETSET_MAGIC_DEF("colpm0", js_a8_get_reg, NULL, REG_COLPM0),
	JS_CGETSET_MAGIC_DEF("colpm1", js_a8_get_reg, NULL, REG_COLPM1),
	JS_CGETSET_MAGIC_DEF("colpm2", js_a8_get_reg, NULL, REG_COLPM2),
	JS_CGETSET_MAGIC_DEF("colpm3", js_a8_get_reg, NULL, REG_COLPM3),
};

/* ============================== Sound class ============================== */

static JSClassID js_sound_class_id;

static JSValue js_sound_play(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	SDL_SFX_sample *s = JS_GetOpaque2(c, this_val, js_sound_class_id);
	if (s == NULL)
		return JS_EXCEPTION;
	SDL_SFX_Play(s);
	return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_sound_proto_funcs[] = {
	JS_CFUNC_DEF("play", 0, js_sound_play),
};

static const JSClassDef js_sound_class = {
	"Sound",
	/* samples live as long as the emulator; nothing to finalize */
};

static JSValue js_a8_loadSound(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	const char *path = JS_ToCString(c, argv[0]);
	SDL_SFX_sample *s;
	JSValue obj;
	if (path == NULL)
		return JS_EXCEPTION;
	s = SDL_SFX_Load(path);
	if (s == NULL) {
		JSValue e = JS_ThrowReferenceError(c, "cannot load sound %s", path);
		JS_FreeCString(c, path);
		return e;
	}
	JS_FreeCString(c, path);
	obj = JS_NewObjectClass(c, js_sound_class_id);
	JS_SetOpaque(obj, s);
	return obj;
}

/* ============================== Extensions ============================== */

typedef struct {
	JSValue self;              /* the object passed to a8.register() */
	char *name;

	/* Memory fingerprint used to detect the program */
	int fp_address;
	byte *fp_bytes;
	int fp_size;

	/* Compatible with ext_state::injection_list, -1 terminated, or NULL */
	int *injection_list;

	/* Cached hook functions, JS_UNDEFINED when the script has none */
	JSValue on_activate;
	JSValue on_pre_gl_frame;
	JSValue on_post_gl_frame;
	JSValue on_code_injection;

	/* Menu: the "menu" object, its keys in definition order, and the UI
	   items handed to ext.c (suffix strings are owned here). */
	JSValue menu;
	int menu_count;
	char **menu_keys;
	char **menu_labels;
	char **menu_suffixes;
	UI_tMenuItem *ui_items;
} ext_js_state;

static void call_hook(ext_js_state *s, JSValueConst fn, int argc, JSValueConst *argv, const char *what)
{
	JSValue ret = JS_Call(ctx, fn, s->self, argc, argv);
	if (JS_IsException(ret))
		js_fatal(what);
	JS_FreeValue(ctx, ret);
	run_jobs();
}

static int ext_js_initialize(ext_state *state)
{
	ext_js_state *s = (ext_js_state *) state->internal_state;

	if (memcmp(MEMORY_mem + s->fp_address, s->fp_bytes, s->fp_size) != 0)
		return 0;

	if (!JS_IsUndefined(s->on_activate))
		call_hook(s, s->on_activate, 0, NULL, "onActivate");
	return 1;
}

static void ext_js_pre_gl_frame(ext_state *state)
{
	ext_js_state *s = (ext_js_state *) state->internal_state;
	call_hook(s, s->on_pre_gl_frame, 0, NULL, "onPreGlFrame");
}

static void ext_js_post_gl_frame(ext_state *state)
{
	ext_js_state *s = (ext_js_state *) state->internal_state;
	call_hook(s, s->on_post_gl_frame, 0, NULL, "onPostGlFrame");
}

static int ext_js_code_injection(ext_state *state, int pc, int op)
{
	ext_js_state *s = (ext_js_state *) state->internal_state;
	JSValue argv[2];
	JSValue ret;
	int32_t result;

	argv[0] = JS_NewInt32(ctx, pc);
	argv[1] = JS_NewInt32(ctx, op);
	ret = JS_Call(ctx, s->on_code_injection, s->self, 2, argv);
	if (JS_IsException(ret))
		js_fatal("onCodeInjection");
	if (JS_ToInt32(ctx, &result, ret))
		js_fatal("onCodeInjection (return value must be an opcode)");
	JS_FreeValue(ctx, ret);
	return result & 0xff;
}

/* Reads menu[key].current and menu[key].options[current]; returns the
   options count, or -1 with a pending exception. */
static int menu_item_read(ext_js_state *s, int i, int *current, char **suffix)
{
	JSValue item, options, opt;
	int count;

	item = JS_GetPropertyStr(ctx, s->menu, s->menu_keys[i]);
	if (JS_IsException(item))
		return -1;
	if (get_int_prop(item, "current", current) != 0) {
		JS_FreeValue(ctx, item);
		return -1;
	}
	options = JS_GetPropertyStr(ctx, item, "options");
	JS_FreeValue(ctx, item);
	if (JS_IsException(options))
		return -1;
	count = get_array_length(options);
	if (count <= 0) {
		JS_FreeValue(ctx, options);
		JS_ThrowTypeError(ctx, "menu item %s: options must be a non-empty array", s->menu_keys[i]);
		return -1;
	}
	if (*current < 0 || *current >= count) {
		JS_FreeValue(ctx, options);
		JS_ThrowRangeError(ctx, "menu item %s: current=%d out of range", s->menu_keys[i], *current);
		return -1;
	}
	if (suffix != NULL) {
		opt = JS_GetPropertyUint32(ctx, options, *current);
		*suffix = dup_cstring(opt, "menu option");
		JS_FreeValue(ctx, opt);
	}
	JS_FreeValue(ctx, options);
	return count;
}

static struct UI_tMenuItem *ext_js_get_config(ext_state *state)
{
	ext_js_state *s = (ext_js_state *) state->internal_state;
	int i;

	for (i = 0; i < s->menu_count; i++) {
		int current;
		char *suffix = NULL;
		if (menu_item_read(s, i, &current, &suffix) < 0)
			js_fatal("reading menu");
		free(s->menu_suffixes[i]);
		s->menu_suffixes[i] = suffix;
		s->ui_items[i].suffix = suffix;
	}
	return s->ui_items;
}

static void ext_js_handle_config(ext_state *state, int option)
{
	ext_js_state *s = (ext_js_state *) state->internal_state;
	int current, count;
	JSValue item;

	if (option < 0 || option >= s->menu_count)
		return;
	count = menu_item_read(s, option, &current, NULL);
	if (count < 0)
		js_fatal("reading menu");
	current = (current + 1) % count;

	item = JS_GetPropertyStr(ctx, s->menu, s->menu_keys[option]);
	if (JS_IsException(item) || JS_SetPropertyStr(ctx, item, "current", JS_NewInt32(ctx, current)) < 0)
		js_fatal("updating menu");
	JS_FreeValue(ctx, item);
}

/* Fetches an optional function property; JS_UNDEFINED when absent. */
static JSValue get_hook(JSValueConst obj, const char *name)
{
	JSValue v = JS_GetPropertyStr(ctx, obj, name);
	if (JS_IsException(v))
		js_fatal("a8.register");
	if (JS_IsUndefined(v) || JS_IsNull(v)) {
		JS_FreeValue(ctx, v);
		return JS_UNDEFINED;
	}
	if (!JS_IsFunction(ctx, v)) {
		JS_ThrowTypeError(ctx, "a8.register: %s must be a function", name);
		js_fatal("a8.register");
	}
	return v;
}

/* Reads an array of integers in [lo, hi]; returns the count. */
static int read_int_array(JSValueConst arr, const char *what, int lo, int hi, int **out)
{
	int len, i;
	int *vals;

	if (!JS_IsArray(ctx, arr) || (len = get_array_length(arr)) <= 0) {
		JS_ThrowTypeError(ctx, "a8.register: %s must be a non-empty array", what);
		js_fatal("a8.register");
	}
	vals = malloc(sizeof(int) * (len + 1));
	EXT_ASSERT_NOT_NULL(vals);
	for (i = 0; i < len; i++) {
		JSValue v = JS_GetPropertyUint32(ctx, arr, i);
		int32_t n;
		if (JS_ToInt32(ctx, &n, v)) {
			JS_FreeValue(ctx, v);
			js_fatal("a8.register");
		}
		JS_FreeValue(ctx, v);
		if (n < lo || n > hi) {
			JS_ThrowRangeError(ctx, "a8.register: %s[%d]=%d out of range", what, i, n);
			js_fatal("a8.register");
		}
		vals[i] = n;
	}
	vals[len] = -1;
	*out = vals;
	return len;
}

/* a8.register({ name, fingerprint: {address, bytes}, codeInjections,
                 onCodeInjection, onActivate, onPreGlFrame, onPostGlFrame, menu }) */
static JSValue js_a8_register(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ext_js_state *s;
	ext_state *state;
	JSValue v, fp, bytes;
	int *fp_vals = NULL;
	int i;

	if (argc < 1 || !JS_IsObject(argv[0]))
		return JS_ThrowTypeError(c, "a8.register(extension) expects an object");

	s = malloc(sizeof(ext_js_state));
	EXT_ASSERT_NOT_NULL(s);
	memset(s, 0, sizeof(*s));
	s->self = JS_DupValue(c, argv[0]);
	s->menu = JS_UNDEFINED;

	/* name */
	v = JS_GetPropertyStr(c, s->self, "name");
	if (!JS_IsString(v))
		return JS_ThrowTypeError(c, "a8.register: name must be a string");
	s->name = dup_cstring(v, "name");
	JS_FreeValue(c, v);

	/* fingerprint: { address, bytes } */
	fp = JS_GetPropertyStr(c, s->self, "fingerprint");
	if (!JS_IsObject(fp))
		return JS_ThrowTypeError(c, "a8.register: fingerprint must be { address, bytes }");
	if (get_int_prop(fp, "address", &s->fp_address) != 0 || s->fp_address < 0 || s->fp_address > 0xffff)
		return JS_ThrowTypeError(c, "a8.register: fingerprint.address must be a 16-bit address");
	bytes = JS_GetPropertyStr(c, fp, "bytes");
	s->fp_size = read_int_array(bytes, "fingerprint.bytes", 0, 0xff, &fp_vals);
	JS_FreeValue(c, bytes);
	JS_FreeValue(c, fp);
	if (s->fp_address + s->fp_size > 0x10000)
		return JS_ThrowRangeError(c, "a8.register: fingerprint runs past the end of memory");
	s->fp_bytes = malloc(s->fp_size);
	EXT_ASSERT_NOT_NULL(s->fp_bytes);
	for (i = 0; i < s->fp_size; i++)
		s->fp_bytes[i] = (byte) fp_vals[i];
	free(fp_vals);

	/* hooks */
	s->on_activate = get_hook(s->self, "onActivate");
	s->on_pre_gl_frame = get_hook(s->self, "onPreGlFrame");
	s->on_post_gl_frame = get_hook(s->self, "onPostGlFrame");
	s->on_code_injection = get_hook(s->self, "onCodeInjection");

	/* codeInjections: [addresses] */
	v = JS_GetPropertyStr(c, s->self, "codeInjections");
	if (!JS_IsUndefined(v) && !JS_IsNull(v))
		read_int_array(v, "codeInjections", 0, 0xffff, &s->injection_list);
	JS_FreeValue(c, v);
	if ((s->injection_list != NULL) != !JS_IsUndefined(s->on_code_injection))
		return JS_ThrowTypeError(c, "a8.register: codeInjections and onCodeInjection go together");

	/* menu: { KEY: { label, options, current }, ... } */
	v = JS_GetPropertyStr(c, s->self, "menu");
	if (JS_IsObject(v)) {
		JSPropertyEnum *tab;
		uint32_t len;
		if (JS_GetOwnPropertyNames(c, &tab, &len, v, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY))
			return JS_EXCEPTION;
		if (len == 0)
			return JS_ThrowTypeError(c, "a8.register: menu must have at least one item");
		s->menu = v;
		s->menu_count = (int) len;
		s->menu_keys = calloc(len, sizeof(char *));
		s->menu_labels = calloc(len, sizeof(char *));
		s->menu_suffixes = calloc(len, sizeof(char *));
		s->ui_items = calloc(len + 1, sizeof(UI_tMenuItem));
		EXT_ASSERT(s->menu_keys && s->menu_labels && s->menu_suffixes && s->ui_items, "%s", "out of memory");
		for (i = 0; i < (int) len; i++) {
			JSValue key = JS_AtomToString(c, tab[i].atom);
			JSValue item, label;
			int current;
			s->menu_keys[i] = dup_cstring(key, "menu key");
			JS_FreeValue(c, key);
			item = JS_GetPropertyStr(c, v, s->menu_keys[i]);
			if (!JS_IsObject(item))
				return JS_ThrowTypeError(c, "a8.register: menu.%s must be { label, options, current }", s->menu_keys[i]);
			label = JS_GetPropertyStr(c, item, "label");
			if (!JS_IsString(label))
				return JS_ThrowTypeError(c, "a8.register: menu.%s.label must be a string", s->menu_keys[i]);
			s->menu_labels[i] = dup_cstring(label, "menu label");
			JS_FreeValue(c, label);
			JS_FreeValue(c, item);
			if (menu_item_read(s, i, &current, NULL) < 0)
				return JS_EXCEPTION;
			s->ui_items[i].flags = UI_ITEM_ACTION;
			s->ui_items[i].retval = i;
			s->ui_items[i].item = s->menu_labels[i];
		}
		s->ui_items[len].flags = UI_ITEM_END;
		JS_FreePropertyEnum(c, tab, len);
	}
	else {
		if (!JS_IsUndefined(v) && !JS_IsNull(v))
			return JS_ThrowTypeError(c, "a8.register: menu must be an object");
		JS_FreeValue(c, v);
	}

	/* Hand it over to the extension framework */
	state = ext_state_alloc();
	state->name = s->name;
	state->internal_state = s;
	state->initialize = ext_js_initialize;
	state->injection_list = s->injection_list;
	if (s->injection_list != NULL)
		state->code_injection = ext_js_code_injection;
	if (!JS_IsUndefined(s->on_pre_gl_frame))
		state->pre_gl_frame = ext_js_pre_gl_frame;
	if (!JS_IsUndefined(s->on_post_gl_frame))
		state->post_gl_frame = ext_js_post_gl_frame;
	if (s->menu_count > 0) {
		state->get_config = ext_js_get_config;
		state->handle_config = ext_js_handle_config;
	}
	ext_register_ext(state);

	return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_a8_funcs[] = {
	JS_CFUNC_DEF("register", 1, js_a8_register),
	JS_CFUNC_DEF("fakeCpuUntilPc", 1, js_a8_fakeCpuUntilPc),
	JS_CFUNC_DEF("fakeCpuUntilOp", 1, js_a8_fakeCpuUntilOp),
	JS_CFUNC_DEF("fakeCpuUntilAfterOp", 1, js_a8_fakeCpuUntilAfterOp),
	JS_CFUNC_DEF("peek", 1, js_a8_peek),
	JS_CFUNC_DEF("poke", 2, js_a8_poke),
	JS_CFUNC_DEF("printFps", 5, js_a8_printFps),
	JS_CFUNC_DEF("accelerationDisabled", 0, js_a8_accelerationDisabled),
	JS_CFUNC_DEF("rgb", 1, js_a8_rgb),
	JS_CFUNC_DEF("loadSound", 1, js_a8_loadSound),
	JS_PROP_INT32_DEF("OP_RTS", OP_RTS, JS_PROP_ENUMERABLE),
	JS_PROP_INT32_DEF("OP_NOP", OP_NOP, JS_PROP_ENUMERABLE),
	JS_OBJECT_DEF("antic", js_a8_antic_funcs, 2, JS_PROP_ENUMERABLE),
	JS_OBJECT_DEF("gtia", js_a8_gtia_funcs, 9, JS_PROP_ENUMERABLE),
	JS_OBJECT_DEF("cpu", js_a8_cpu_funcs, 6, JS_PROP_ENUMERABLE),
};

/* Wraps existing C memory in a typed array without copying. The memory is
   static, so the ArrayBuffer gets no free function. */
static JSValue new_external_typed_array(void *ptr, size_t bytes, size_t count, JSTypedArrayEnum type)
{
	JSValue buf = JS_NewArrayBuffer(ctx, (uint8_t *) ptr, bytes, NULL, NULL, FALSE);
	JSValue argv[3];
	JSValue arr;
	argv[0] = buf;
	argv[1] = JS_NewInt32(ctx, 0);
	argv[2] = JS_NewInt32(ctx, (int32_t) count);
	arr = JS_NewTypedArray(ctx, 3, argv, type);
	JS_FreeValue(ctx, buf);
	if (JS_IsException(arr))
		js_fatal("creating typed array");
	return arr;
}

static void install_a8(void)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue a8 = JS_NewObject(ctx);
	JSValue proto;

	JS_SetPropertyFunctionList(ctx, a8, js_a8_funcs, sizeof(js_a8_funcs) / sizeof(js_a8_funcs[0]));

	/* a8.mem: the 64 KB of Atari memory, read/write, zero copy */
	JS_SetPropertyStr(ctx, a8, "mem",
		new_external_typed_array(MEMORY_mem, 0x10000, 0x10000, JS_TYPED_ARRAY_UINT8));
	/* a8.palette: Colours_table, 0x00RRGGBB per Atari colour, zero copy */
	JS_SetPropertyStr(ctx, a8, "palette",
		new_external_typed_array(Colours_table, sizeof(Colours_table), 256, JS_TYPED_ARRAY_INT32));

	JS_NewClassID(&js_sound_class_id);
	JS_NewClass(rt, js_sound_class_id, &js_sound_class);
	proto = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, proto, js_sound_proto_funcs, 1);
	JS_SetClassProto(ctx, js_sound_class_id, proto);

	JS_SetPropertyStr(ctx, global, "a8", a8);
	SDL_VIDEO_GL_JS_Init(ctx, global);
	JS_FreeValue(ctx, global);
}

/* Evaluates one init.js as an ES module, the same way qjs does. */
static void run_module(const char *path)
{
	size_t len;
	uint8_t *buf;
	JSValue val;

	printf("Loading JavaScript extension: %s\n", path);
	buf = js_load_file(ctx, &len, path);
	if (buf == NULL) {
		printf("Cannot read %s\n", path);
		exit(1);
	}
	val = JS_Eval(ctx, (const char *) buf, len, path, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
	js_free(ctx, buf);
	if (JS_IsException(val))
		js_fatal(path);
	js_module_set_import_meta(ctx, val, TRUE, FALSE);
	val = JS_EvalFunction(ctx, val);
	val = js_std_await(ctx, val);
	if (JS_IsException(val))
		js_fatal(path);
	JS_FreeValue(ctx, val);
}

void ext_js_init(void)
{
	const char *dirname = "data/ext";
	DIR *dir;
	struct dirent *dp;
	char buf[1024];

	printf("Initializing JavaScript (QuickJS)\n");
	rt = JS_NewRuntime();
	EXT_ASSERT_NOT_NULL(rt);
	js_std_init_handlers(rt);
	/* file-based ES module loader from quickjs-libc; it resolves relative
	   imports against the importing module's path */
	JS_SetModuleLoaderFunc2(rt, NULL, js_module_loader, js_module_check_attributes, NULL);
	ctx = JS_NewContext(rt);
	EXT_ASSERT_NOT_NULL(ctx);
	js_std_add_helpers(ctx, 0, NULL);   /* console.log, print */
	js_init_module_std(ctx, "std");
	js_init_module_os(ctx, "os");

	install_a8();

	/* Load every data/ext/<name>/init.js */
	dir = opendir(dirname);
	EXT_ASSERT_NOT_NULL(dir);
	while ((dp = readdir(dir)) != NULL) {
		struct stat stbuf;
		snprintf(buf, sizeof(buf), "%s/%s", dirname, dp->d_name);
		if (stat(buf, &stbuf) == -1 || (stbuf.st_mode & S_IFDIR) == 0)
			continue;
		snprintf(buf, sizeof(buf), "%s/%s/init.js", dirname, dp->d_name);
		if (stat(buf, &stbuf) == -1)
			continue;
		run_module(buf);
	}
	closedir(dir);
}
