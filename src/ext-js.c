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

/* Scripts see two globals, "a8" (emulator access) and "gl" (OpenGL, see
   sdl/video_gl-js.c), plus console.log and the QuickJS "std" and "os"
   modules. Each data/ext/<name>/init.js is evaluated as an ES module at
   startup; its default export is the extension object, described in
   data/ext/README.md. */

#include "ext-js.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <quickjs/quickjs-libc.h>

#include "antic.h"
#include "colours.h"
#include "cpu.h"
#include "gtia.h"
#include "memory.h"
#include "ui_basic.h"

#include "sdl/sfx.h"
#include "sdl/video_gl-js.h"

static JSRuntime *rt = NULL;
static JSContext *ctx = NULL;

/* Prints the pending exception with its stack trace and exits. Script errors
   are programming errors in a hack, so we do not try to carry on. */
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

/* 0 = read, 1 = undefined, -1 = exception */
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

/* ============================== the a8 global ============================== */

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

/* a8.peek(addr) / a8.poke(addr, value): memory access that honours bank
   switching, ROM and I/O registers, unlike the raw a8.mem array. */
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

/* a8.cpu.a/x/y/s/p/pc: the 6502 registers. Current inside onCodeInjection(),
   where changes take effect on return. */
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

/* a8.antic.* and a8.gtia.* getters */
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

/* ---- Sound class: a8.loadSound(path) -> { play() } ---- */

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

static const JSCFunctionListEntry js_a8_funcs[] = {
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
	JSValue buf = JS_NewArrayBuffer(ctx, (uint8_t *) ptr, bytes, NULL, NULL, 0);
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

static void install_globals(void)
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

/* ============================== calling into an extension ============================== */

static const char *hook_names[EXT_HOOK_COUNT] = {
	"onActivate", "onPreGlFrame", "onPostGlFrame", "onCodeInjection"
};

void ext_js_call_hook(ext_extension *ext, enum ext_hook hook)
{
	JSValue ret = JS_Call(ctx, ext->hooks[hook], ext->self, 0, NULL);
	if (JS_IsException(ret))
		js_fatal(hook_names[hook]);
	JS_FreeValue(ctx, ret);
	run_jobs();
}

int ext_js_call_code_injection(ext_extension *ext, int pc, int op)
{
	JSValue argv[2];
	JSValue ret;
	int32_t result;

	argv[0] = JS_NewInt32(ctx, pc);
	argv[1] = JS_NewInt32(ctx, op);
	ret = JS_Call(ctx, ext->hooks[EXT_HOOK_CODE_INJECTION], ext->self, 2, argv);
	if (JS_IsException(ret))
		js_fatal("onCodeInjection");
	if (JS_ToInt32(ctx, &result, ret))
		js_fatal("onCodeInjection (the return value must be an opcode)");
	JS_FreeValue(ctx, ret);
	return result & 0xff;
}

/* Reads menu[key].current and, if wanted, the text of that option. Returns
   the number of options, or -1 with an exception pending. */
static int menu_item_read(ext_extension *ext, int i, int *current, char **text)
{
	JSValue item, options, opt;
	int count;

	item = JS_GetPropertyStr(ctx, ext->menu, ext->menu_keys[i]);
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
		JS_ThrowTypeError(ctx, "menu item %s: options must be a non-empty array", ext->menu_keys[i]);
		return -1;
	}
	if (*current < 0 || *current >= count) {
		JS_FreeValue(ctx, options);
		JS_ThrowRangeError(ctx, "menu item %s: current=%d out of range", ext->menu_keys[i], *current);
		return -1;
	}
	if (text != NULL) {
		opt = JS_GetPropertyUint32(ctx, options, *current);
		*text = dup_cstring(opt, "menu option");
		JS_FreeValue(ctx, opt);
	}
	JS_FreeValue(ctx, options);
	return count;
}

UI_tMenuItem *ext_js_menu_items(ext_extension *ext)
{
	int i;
	for (i = 0; i < ext->menu_count; i++) {
		int current;
		char *text = NULL;
		if (menu_item_read(ext, i, &current, &text) < 0)
			js_fatal("reading the menu");
		free(ext->menu_suffixes[i]);
		ext->menu_suffixes[i] = text;
		ext->ui_items[i].suffix = text;
	}
	return ext->ui_items;
}

void ext_js_menu_cycle(ext_extension *ext, int option)
{
	int current, count;
	JSValue item;

	if (option < 0 || option >= ext->menu_count)
		return;
	count = menu_item_read(ext, option, &current, NULL);
	if (count < 0)
		js_fatal("reading the menu");
	current = (current + 1) % count;

	item = JS_GetPropertyStr(ctx, ext->menu, ext->menu_keys[option]);
	if (JS_IsException(item) || JS_SetPropertyStr(ctx, item, "current", JS_NewInt32(ctx, current)) < 0)
		js_fatal("updating the menu");
	JS_FreeValue(ctx, item);
}

/* ============================== loading extensions ============================== */

static void bad_extension(const char *path, const char *fmt, ...)
{
	va_list ap;
	printf("%s: ", path);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	exit(1);
}

/* Optional function property; JS_UNDEFINED when absent. */
static JSValue get_hook(JSValueConst obj, const char *name, const char *path)
{
	JSValue v = JS_GetPropertyStr(ctx, obj, name);
	if (JS_IsException(v))
		js_fatal(path);
	if (JS_IsUndefined(v) || JS_IsNull(v)) {
		JS_FreeValue(ctx, v);
		return JS_UNDEFINED;
	}
	if (!JS_IsFunction(ctx, v))
		bad_extension(path, "%s must be a function", name);
	return v;
}

/* Reads an array of integers in [lo, hi] into a -1 terminated list. */
static int read_int_array(JSValueConst arr, const char *what, int lo, int hi, int **out, const char *path)
{
	int len, i;
	int *vals;

	if (!JS_IsArray(ctx, arr) || (len = get_array_length(arr)) <= 0)
		bad_extension(path, "%s must be a non-empty array", what);
	vals = malloc(sizeof(int) * (len + 1));
	EXT_ASSERT_NOT_NULL(vals);
	for (i = 0; i < len; i++) {
		JSValue v = JS_GetPropertyUint32(ctx, arr, i);
		int32_t n;
		if (JS_ToInt32(ctx, &n, v))
			js_fatal(path);
		JS_FreeValue(ctx, v);
		if (n < lo || n > hi)
			bad_extension(path, "%s[%d]=%d is out of range", what, i, n);
		vals[i] = n;
	}
	vals[len] = -1;
	*out = vals;
	return len;
}

/* Builds an extension from a module's default export and registers it.
   Takes ownership of obj. */
static void register_extension(JSValue obj, const char *path)
{
	ext_extension *ext;
	JSValue v, fp, bytes;
	int *fp_vals = NULL;
	int i;

	if (!JS_IsObject(obj))
		bad_extension(path, "the default export must be the extension object");

	ext = calloc(1, sizeof(ext_extension));
	EXT_ASSERT_NOT_NULL(ext);
	ext->self = obj;
	ext->menu = JS_UNDEFINED;
	for (i = 0; i < EXT_HOOK_COUNT; i++)
		ext->hooks[i] = JS_UNDEFINED;

	/* name */
	v = JS_GetPropertyStr(ctx, obj, "name");
	if (!JS_IsString(v))
		bad_extension(path, "name must be a string");
	ext->name = dup_cstring(v, "name");
	JS_FreeValue(ctx, v);

	/* fingerprint: { address, bytes } */
	fp = JS_GetPropertyStr(ctx, obj, "fingerprint");
	if (!JS_IsObject(fp))
		bad_extension(path, "fingerprint must be { address, bytes }");
	if (get_int_prop(fp, "address", &ext->fp_address) != 0 || ext->fp_address < 0 || ext->fp_address > 0xffff)
		bad_extension(path, "fingerprint.address must be a 16-bit address");
	bytes = JS_GetPropertyStr(ctx, fp, "bytes");
	ext->fp_size = read_int_array(bytes, "fingerprint.bytes", 0, 0xff, &fp_vals, path);
	JS_FreeValue(ctx, bytes);
	JS_FreeValue(ctx, fp);
	if (ext->fp_address + ext->fp_size > 0x10000)
		bad_extension(path, "the fingerprint runs past the end of memory");
	ext->fp_bytes = malloc(ext->fp_size);
	EXT_ASSERT_NOT_NULL(ext->fp_bytes);
	for (i = 0; i < ext->fp_size; i++)
		ext->fp_bytes[i] = (byte) fp_vals[i];
	free(fp_vals);

	/* hooks */
	for (i = 0; i < EXT_HOOK_COUNT; i++)
		ext->hooks[i] = get_hook(obj, hook_names[i], path);

	/* codeInjections: [addresses], together with onCodeInjection */
	v = JS_GetPropertyStr(ctx, obj, "codeInjections");
	if (!JS_IsUndefined(v) && !JS_IsNull(v))
		read_int_array(v, "codeInjections", 0, 0xffff, &ext->injection_list, path);
	JS_FreeValue(ctx, v);
	if ((ext->injection_list != NULL) != ext_js_has_hook(ext, EXT_HOOK_CODE_INJECTION))
		bad_extension(path, "codeInjections and onCodeInjection go together");

	/* menu: { KEY: { label, options, current }, ... } */
	v = JS_GetPropertyStr(ctx, obj, "menu");
	if (JS_IsObject(v)) {
		JSPropertyEnum *tab;
		uint32_t len;
		if (JS_GetOwnPropertyNames(ctx, &tab, &len, v, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY))
			js_fatal(path);
		if (len == 0)
			bad_extension(path, "menu must have at least one item");
		ext->menu = v;
		ext->menu_count = (int) len;
		ext->menu_keys = calloc(len, sizeof(char *));
		ext->menu_labels = calloc(len, sizeof(char *));
		ext->menu_suffixes = calloc(len, sizeof(char *));
		ext->ui_items = calloc(len + 1, sizeof(UI_tMenuItem));
		EXT_ASSERT(ext->menu_keys && ext->menu_labels && ext->menu_suffixes && ext->ui_items, "%s", "out of memory");
		for (i = 0; i < (int) len; i++) {
			JSValue key = JS_AtomToString(ctx, tab[i].atom);
			JSValue item, label;
			int current;
			ext->menu_keys[i] = dup_cstring(key, "menu key");
			JS_FreeValue(ctx, key);
			item = JS_GetPropertyStr(ctx, v, ext->menu_keys[i]);
			if (!JS_IsObject(item))
				bad_extension(path, "menu.%s must be { label, options, current }", ext->menu_keys[i]);
			label = JS_GetPropertyStr(ctx, item, "label");
			if (!JS_IsString(label))
				bad_extension(path, "menu.%s.label must be a string", ext->menu_keys[i]);
			ext->menu_labels[i] = dup_cstring(label, "menu label");
			JS_FreeValue(ctx, label);
			JS_FreeValue(ctx, item);
			if (menu_item_read(ext, i, &current, NULL) < 0)
				js_fatal(path);
			ext->ui_items[i].flags = UI_ITEM_ACTION;
			ext->ui_items[i].retval = i;
			ext->ui_items[i].item = ext->menu_labels[i];
		}
		ext->ui_items[len].flags = UI_ITEM_END;
		JS_FreePropertyEnum(ctx, tab, len);
	}
	else {
		if (!JS_IsUndefined(v) && !JS_IsNull(v))
			bad_extension(path, "menu must be an object");
		JS_FreeValue(ctx, v);
	}

	ext_register(ext);
}

/* Evaluates one init.js as an ES module (the way qjs does) and registers
   its default export. */
static void load_module(const char *path)
{
	size_t len;
	uint8_t *buf;
	JSValue val, ns, def;
	JSModuleDef *m;

	printf("Loading extension: %s\n", path);
	buf = js_load_file(ctx, &len, path);
	if (buf == NULL)
		bad_extension(path, "cannot read the file");
	val = JS_Eval(ctx, (const char *) buf, len, path, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
	js_free(ctx, buf);
	if (JS_IsException(val))
		js_fatal(path);
	m = JS_VALUE_GET_PTR(val);
	js_module_set_import_meta(ctx, val, TRUE, FALSE);
	val = JS_EvalFunction(ctx, val);
	val = js_std_await(ctx, val);
	if (JS_IsException(val))
		js_fatal(path);
	JS_FreeValue(ctx, val);

	ns = JS_GetModuleNamespace(ctx, m);
	if (JS_IsException(ns))
		js_fatal(path);
	def = JS_GetPropertyStr(ctx, ns, "default");
	JS_FreeValue(ctx, ns);
	if (JS_IsException(def))
		js_fatal(path);
	register_extension(def, path);
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

	install_globals();

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
		load_module(buf);
	}
	closedir(dir);
}
