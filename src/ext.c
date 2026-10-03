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
#include "log.h"
#include "monitor.h"
#include "ui.h"
#include "util.h"

#define MAX_EXTENSIONS 64
static ext_extension *extensions[MAX_EXTENSIONS];
static int num_extensions = 0;

/* The active extension, if any */
static ext_extension *current = NULL;

/* An extension stays active only while its fingerprint is in memory: when
   the program moves on to something else (or is replaced), its hooks and
   code injections must stop. A program may hide the fingerprint for a
   moment (a bank switched out), hence the grace. The extension the user
   chose comes back by itself when its program does. */
#define GONE_FRAMES 50
static ext_extension *chosen = NULL;
static int gone = 0;

static int inside_menu = 0;
static int alt_held = 0;    /* extensions off while held */
static int ctrl_held = 0;   /* acceleration off while held */

/* A8_EXT_SELECT=<part of an extension name> activates that extension as soon
   as its fingerprint matches, without going through the TAB menu. For
   developing and testing extensions. */
static const char *preselect_name = NULL;

char ext_dir[FILENAME_MAX] = "data/ext";

int ext_initialise(int *argc, char *argv[])
{
	int i, j;
	for (i = j = 1; i < *argc; i++) {
		if (strcmp(argv[i], "-ext-dir") == 0) {
			if (i + 1 >= *argc) {
				Log_print("Missing argument for '%s'", argv[i]);
				return FALSE;
			}
			Util_strlcpy(ext_dir, argv[++i], sizeof(ext_dir));
		}
		else {
			if (strcmp(argv[i], "-help") == 0) {
				Log_print("\t-ext-dir <path>  The directory of the game extensions (default: data/ext)");
			}
			argv[j++] = argv[i];
		}
	}
	*argc = j;
	return TRUE;
}

int ext_read_config(char *option, char *parameters)
{
	if (strcmp(option, "EXT_DIR") != 0)
		return FALSE;
	Util_strlcpy(ext_dir, parameters, sizeof(ext_dir));
	return TRUE;
}

void ext_write_config(FILE *fp)
{
	fprintf(fp, "EXT_DIR=%s\n", ext_dir);
}

int ext_acceleration_disabled(void)
{
	return alt_held || ctrl_held;
}

int ext_is_active(void)
{
	return current != NULL;
}

void ext_register(ext_extension *ext)
{
	EXT_ASSERT_NOT_NULL(ext);
	EXT_ASSERT_LT(num_extensions, MAX_EXTENSIONS);
	printf("Registering extension: %s\n", ext->name);
	extensions[num_extensions++] = ext;
}

static int fingerprint_present(const ext_extension *ext)
{
	return memcmp(MEMORY_mem + ext->fp_address, ext->fp_bytes, ext->fp_size) == 0;
}

/* Does the program in memory match the extension? Runs onActivate() if so. */
static int detect(ext_extension *ext)
{
	if (!fingerprint_present(ext))
		return 0;
	if (ext_js_has_hook(ext, EXT_HOOK_ACTIVATE))
		ext_js_call_hook(ext, EXT_HOOK_ACTIVATE);
	return 1;
}

static void activate(ext_extension *ext)
{
	int i = 0;

	current = chosen = ext;
	gone = 0;
	printf("Active extension: %s\n", ext->name);

	if (ext->injection_list != NULL)
		while (ext->injection_list[i] >= 0)
			i++;
	ext_set_code_injections(ext->injection_list, i);
	printf("%d code injection address(es)\n", i);
}

static void deactivate(void)
{
	printf("%s: the program is gone\n", current->name);
	ext_set_code_injections(NULL, 0);
	current = NULL;
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

	if (current != NULL) {
		if (fingerprint_present(current))
			gone = 0;
		else if (++gone >= GONE_FRAMES)
			deactivate();
	}
	else if (chosen != NULL) {
		if (detect(chosen))
			activate(chosen);
	}
	else if (preselect_name != NULL) {
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
	if (ext_cpu_faking || alt_held || current == NULL)
		return op;
	if (!ext_code_injection_wanted(pc))
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
