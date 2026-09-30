/*
 * ext.c - game extensions: registry, activation, hooks and helpers
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

#include "ext.h"
#include "ext-js.h"

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "antic.h"
#include "cpu.h"
#include "memory.h"
#include "monitor.h"
#include "ui.h"

#define MAX_EXTENSIONS 64
static ext_extension *extensions[MAX_EXTENSIONS];
static int num_extensions = 0;

/* The active extension, if any */
static ext_extension *current = NULL;

/* Which addresses the active extension wants onCodeInjection for */
#define RAM_SIZE 0x10000
static byte code_injection_map[RAM_SIZE];
static int code_injection_map_set = 0;

static int inside_menu = 0;
static int faking_cpu = 0;
static int alt_held = 0;    /* extensions off while held */
static int ctrl_held = 0;   /* acceleration off while held */

/* A8_EXT_SELECT=<part of an extension name> activates that extension as soon
   as its fingerprint matches, without going through the TAB menu. For
   developing and testing extensions. */
static const char *preselect_name = NULL;

int ext_acceleration_disabled(void)
{
	return alt_held || ctrl_held;
}

void ext_register(ext_extension *ext)
{
	EXT_ASSERT_NOT_NULL(ext);
	EXT_ASSERT_LT(num_extensions, MAX_EXTENSIONS);
	printf("Registering extension: %s\n", ext->name);
	extensions[num_extensions++] = ext;
}

/* Does the program in memory match the extension? Runs onActivate() if so. */
static int detect(ext_extension *ext)
{
	if (memcmp(MEMORY_mem + ext->fp_address, ext->fp_bytes, ext->fp_size) != 0)
		return 0;
	if (ext_js_has_hook(ext, EXT_HOOK_ACTIVATE))
		ext_js_call_hook(ext, EXT_HOOK_ACTIVATE);
	return 1;
}

static void activate(ext_extension *ext)
{
	int i = 0;

	current = ext;
	printf("Active extension: %s\n", ext->name);

	memset(code_injection_map, 0, RAM_SIZE);
	code_injection_map_set = 0;
	if (ext->injection_list != NULL) {
		for (i = 0; ext->injection_list[i] >= 0; i++)
			code_injection_map[ext->injection_list[i]] = 1;
		code_injection_map_set = 1;
	}
	printf("%d code injection address(es)\n", i);
}

void ext_init(void)
{
	preselect_name = getenv("A8_EXT_SELECT");
	if (preselect_name != NULL)
		printf("A8_EXT_SELECT: will activate the first extension matching '%s' once its fingerprint matches\n", preselect_name);

	ext_js_init();
}

/* The TAB menu: finds the extension for the running program, then shows its
   options until the user leaves. */
static void menu(void)
{
	int i;
	UI_tMenuItem header = UI_MENU_ACTION(100, "Found extension:");
	UI_tMenuItem exit_item = UI_MENU_ACTION(101, "EXIT");
	UI_tMenuItem end = UI_MENU_END;
	UI_tMenuItem items[16];
	int option = 0;

	if (current == NULL) {
		for (i = 0; i < num_extensions; i++) {
			if (detect(extensions[i])) {
				activate(extensions[i]);
				break;
			}
		}
	}

	UI_driver->fInit();
	inside_menu = 1;
	for (;;) {
		int idx = 0;
		UI_tMenuItem *ext_items = (current != NULL && current->menu_count > 0) ? ext_js_menu_items(current) : NULL;

		items[idx] = header;
		items[idx++].suffix = current != NULL ? current->name : "-UNKNOWN-";
		if (ext_items != NULL) {
			for (i = 0; ext_items[i].flags != UI_ITEM_END && idx < 14; i++)
				items[idx++] = ext_items[i];
		}
		items[idx++] = exit_item;
		items[idx] = end;

		option = UI_driver->fSelect("Extensions", 0, option, items, NULL);
		if (option < 0 || option == 101)
			break;
		if (current != NULL)
			ext_js_menu_cycle(current, option);
	}
	inside_menu = 0;

	/* Wait until no key is pressed, so that leaving the menu does not reach the Atari */
	for (;;) {
		const Uint8 *keys;
		SDL_PumpEvents();
		keys = SDL_GetKeyState(NULL);
		if (!(keys[SDLK_ESCAPE] || keys[SDLK_RETURN] || keys[SDLK_TAB] || keys[SDLK_SPACE]))
			break;
	}
}

/* Called once per Atari frame from the SDL main loop */
void ext_frame(void)
{
	const Uint8 *keys = SDL_GetKeyState(NULL);
	alt_held = keys[SDLK_LALT] || keys[SDLK_RALT];
	ctrl_held = keys[SDLK_LCTRL] || keys[SDLK_RCTRL];

	if (inside_menu || alt_held)
		return;

	if (preselect_name != NULL && current == NULL) {
		int i;
		for (i = 0; i < num_extensions; i++) {
			if (strstr(extensions[i]->name, preselect_name) != NULL && detect(extensions[i])) {
				printf("A8_EXT_SELECT: activating %s\n", extensions[i]->name);
				activate(extensions[i]);
				break;
			}
		}
	}

	if (keys[SDLK_TAB])
		menu();

	/* The per-frame hook that does not need OpenGL: for headless runs too */
	if (current != NULL && ext_js_has_hook(current, EXT_HOOK_FRAME))
		ext_js_call_hook(current, EXT_HOOK_FRAME);
}

/* The emulator's own UI (F1) paints into the Atari screen with the emulation
   paused, so the display list still looks like the game's: extensions must
   not draw over it */
void ext_pre_gl_frame(void)
{
	if (inside_menu || UI_is_active || alt_held || current == NULL)
		return;
	if (ext_js_has_hook(current, EXT_HOOK_PRE_GL_FRAME))
		ext_js_call_hook(current, EXT_HOOK_PRE_GL_FRAME);
}

void ext_post_gl_frame(void)
{
	if (inside_menu || UI_is_active || alt_held || current == NULL)
		return;
	if (ext_js_has_hook(current, EXT_HOOK_POST_GL_FRAME))
		ext_js_call_hook(current, EXT_HOOK_POST_GL_FRAME);
}

/* Called by the CPU before executing the instruction at pc */
int ext_handle_code_injection(int pc, int op)
{
	if (faking_cpu || alt_held || current == NULL)
		return op;
	if (!code_injection_map_set || !code_injection_map[pc])
		return op;
	return ext_js_call_code_injection(current, pc, op);
}

/* =========================================== FPS =========================================== */

static int fps_last_value = 0;
static int fps_frames = 0;
static int fps_last_frames = 0;
static char fps_buf[20];

char *ext_fps_str(int current_value)
{
	fps_frames++;
	if (current_value != fps_last_value) {
		fps_last_frames = fps_frames;
		fps_frames = 0;
		fps_last_value = current_value;
	}
	snprintf(fps_buf, sizeof(fps_buf), "FRAMES: %d ", fps_last_frames);
	return fps_buf;
}

/* ======================================== FAKE CPU ======================================== */

/* Runs instructions with interrupts and ANTIC stopped, so that they take no
   emulated time and leave everything but the CPU state untouched. */

static int prev_CPU_IRQ;
static int prev_ANTIC_wsync_halt;
static int prev_ANTIC_cur_screen_pos;
static int prev_ANTIC_xpos;
static int prev_ANTIC_xpos_limit;
static int prev_ANTIC_delayed_wsync;
static FILE *prev_MONITOR_trace_file;

static void fakecpu_begin(void)
{
	assert(!faking_cpu);
	faking_cpu = 1;
	prev_CPU_IRQ = CPU_IRQ;
	prev_ANTIC_wsync_halt = ANTIC_wsync_halt;
	prev_ANTIC_cur_screen_pos = ANTIC_cur_screen_pos;
	prev_ANTIC_xpos = ANTIC_xpos;
	prev_ANTIC_xpos_limit = ANTIC_xpos_limit;
	prev_ANTIC_delayed_wsync = ANTIC_delayed_wsync;
	prev_MONITOR_trace_file = MONITOR_trace_file;
}

static void fakecpu_step(void)
{
	assert(faking_cpu);
	CPU_IRQ = 0;
	ANTIC_wsync_halt = 0;
	ANTIC_cur_screen_pos = ANTIC_NOT_DRAWING;
	ANTIC_xpos = 0;
	ANTIC_xpos_limit = 1;
	MONITOR_trace_file = NULL;
	CPU_GO(1);
}

static void fakecpu_end(void)
{
	ANTIC_wsync_halt = prev_ANTIC_wsync_halt;
	CPU_IRQ = prev_CPU_IRQ;
	ANTIC_cur_screen_pos = prev_ANTIC_cur_screen_pos;
	ANTIC_xpos = prev_ANTIC_xpos;
	ANTIC_xpos_limit = prev_ANTIC_xpos_limit;
	ANTIC_delayed_wsync = prev_ANTIC_delayed_wsync;
	MONITOR_trace_file = prev_MONITOR_trace_file;
	assert(faking_cpu);
	faking_cpu = 0;
}

static int fakecpu_until(int end_pc, int end_op, int after)
{
	fakecpu_begin();
	CPU_regPC--;   /* re-execute the current instruction */
	for (;;) {
		fakecpu_step();
		if (end_pc && CPU_regPC == end_pc)
			break;
		if (end_op && MEMORY_mem[CPU_regPC] == end_op)
			break;
	}
	if (after)
		fakecpu_step();
	fakecpu_end();
	return OP_NOP;
}

int ext_fakecpu_until_pc(int end_pc)
{
	return fakecpu_until(end_pc, 0, 0);
}

int ext_fakecpu_until_op(int end_op)
{
	return fakecpu_until(0, end_op, 0);
}

int ext_fakecpu_until_after_op(int end_op)
{
	return fakecpu_until(0, end_op, 1);
}
