/*
 * web/web.c - the platform layer of the web build: the emulator as a
 * WebAssembly module driven by a page (web/host.js). The page calls
 * web_frame() once per emulated frame, sets the input beforehand, and reads
 * the screen, the palette and the sound straight out of the module's memory.
 * The game extensions run in the page as well, as the same scripts the
 * native build runs in QuickJS: the functions at the end are what their
 * `a8` object is made of.
 *
 * Copyright (C) 2026 Atari800 development team (see DOC/CREDITS)
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

#include "config.h"

#include <emscripten.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "afile.h"
#include "akey.h"
#include "antic.h"
#include "atari.h"
#include "cartridge.h"
#include "colours.h"
#include "cpu.h"
#include "devices.h"
#include "ext.h"
#include "gtia.h"
#include "input.h"
#include "log.h"
#include "memory.h"
#include "monitor.h"
#include "platform.h"
#include "pokey.h"
#include "screen.h"
#include "sound.h"
#include "statesav.h"
#include "ui.h"
#include "util.h"

/* ------------------------------ input ------------------------------ */

static int key_code = AKEY_NONE;
static int joy[4] = { 0, 0, 0, 0 };    /* bits 0-3: up, down, left, right */
static int trig[4] = { 0, 0, 0, 0 };

/* What the page holds down this frame: an Atari key code (akey.h; AKEY_NONE
   for none), shift, the console keys (bit 0 Start, 1 Select, 2 Option, set
   when pressed) and the two joysticks. */
EMSCRIPTEN_KEEPALIVE void web_input(int code, int shift, int consol, int joy0, int trig0, int joy1, int trig1)
{
	key_code = code;
	INPUT_key_shift = shift;
	INPUT_key_consol = INPUT_CONSOL_NONE & ~(consol & 7);
	joy[0] = joy0 & 15; trig[0] = trig0;
	joy[1] = joy1 & 15; trig[1] = trig1;
}

int PLATFORM_Keyboard(void)
{
	return key_code;
}

int PLATFORM_PORT(int num)
{
	if (num == 0)
		return (joy[0] | (joy[1] << 4)) ^ 0xff;
	if (num == 1)
		return (joy[2] | (joy[3] << 4)) ^ 0xff;
	return 0xff;
}

int PLATFORM_TRIG(int num)
{
	return num >= 0 && num < 4 && trig[num] ? 0 : 1;
}

/* ------------------------------ sound ------------------------------ */

/* One buffer per emulated frame, which the page takes after web_frame().
   The frame rate is not a whole number of samples (882.4 at 44100 Hz in
   PAL), so now and then a frame is a sample shorter. */
static UBYTE *sound_array = NULL;
static unsigned int sound_bytes = 0;
static unsigned int sound_buffer_size = 0;
static double sample_diff;
static double sample_residual;

int PLATFORM_SoundSetup(Sound_setup_t *setup)
{
	double refresh_rate = Atari800_tv_mode == Atari800_TV_PAL ? Atari800_FPS_PAL : Atari800_FPS_NTSC;
	double samples_per_frame = setup->freq / refresh_rate;
	setup->buffer_frames = (unsigned int) ceil(samples_per_frame);
	sound_buffer_size = setup->buffer_frames * setup->sample_size * setup->channels;
	if (sound_buffer_size == 0)
		return FALSE;
	free(sound_array);
	sound_array = (UBYTE *) Util_malloc(sound_buffer_size);
	sample_diff = (double) setup->buffer_frames - samples_per_frame;
	sample_residual = 0;
	return TRUE;
}

void PLATFORM_SoundExit(void)
{
	free(sound_array);
	sound_array = NULL;
}

void PLATFORM_SoundPause(void)
{
}

void PLATFORM_SoundContinue(void)
{
}

unsigned int PLATFORM_SoundAvailable(void)
{
	unsigned int size = sound_buffer_size;
	sample_residual += sample_diff;
	if (sample_residual > 1.0) {
		sample_residual -= 1.0;
		size -= Sound_out.sample_size * Sound_out.channels;
	}
	sound_bytes = 0;
	return size;
}

void PLATFORM_SoundWrite(UBYTE const *buffer, unsigned int size)
{
	memcpy(sound_array, buffer, size);
	sound_bytes = size;
}

EMSCRIPTEN_KEEPALIVE UBYTE *web_sound(void) { return sound_array; }
EMSCRIPTEN_KEEPALIVE int web_sound_bytes(void) { return (int) sound_bytes; }
EMSCRIPTEN_KEEPALIVE int web_sound_rate(void) { return (int) Sound_out.freq; }
EMSCRIPTEN_KEEPALIVE int web_sound_channels(void) { return (int) Sound_out.channels; }
EMSCRIPTEN_KEEPALIVE int web_sound_sample_size(void) { return Sound_out.sample_size; }

/* ------------------------------ the platform ------------------------------ */

int PLATFORM_Initialise(int *argc, char *argv[])
{
	return Sound_Initialise(argc, argv);
}

int PLATFORM_Exit(int run_monitor)
{
	Log_flushlog();
	return FALSE;   /* there is no monitor to come back from */
}

void PLATFORM_DisplayScreen(void)
{
	/* the page draws the screen itself, from web_screen() */
}

/* There is no built-in user interface: the page has its own. A cartridge
   image without a header is taken for the standard type of its size. */
int UI_SelectCartType(int k)
{
	return k == 8 ? CARTRIDGE_STD_8 : k == 16 ? CARTRIDGE_STD_16 : CARTRIDGE_NONE;
}
int UI_Initialise(int *argc, char *argv[]) { return TRUE; }
void UI_Run(void) { }
int UI_is_active = FALSE;
int UI_alt_function = -1;
int UI_current_function = -1;
char UI_atari_files_dir[UI_MAX_DIRECTORIES][FILENAME_MAX];
char UI_saved_files_dir[UI_MAX_DIRECTORIES][FILENAME_MAX];
int UI_n_atari_files_dir = 0;
int UI_n_saved_files_dir = 0;
int UI_show_hidden_files = FALSE;

/* The emulator is set up by main(), which returns; the module stays loaded
   and the page drives it from then on. */
int main(int argc, char **argv)
{
	if (!Atari800_Initialise(&argc, argv)) {
		printf("atari800: initialisation failed\n");
		return 3;
	}
	return 0;
}

/* One emulated frame: Atari800_Frame() without the user interface and
   without waiting for the next frame's time, which is the page's job. */
EMSCRIPTEN_KEEPALIVE void web_frame(void)
{
	INPUT_key_code = PLATFORM_Keyboard();
	switch (INPUT_key_code) {
	case AKEY_COLDSTART:
		Atari800_Coldstart();
		break;
	case AKEY_WARMSTART:
		Atari800_Warmstart();
		break;
	default:
		break;
	}
	Devices_Frame();
	INPUT_Frame();
	GTIA_Frame();
	ANTIC_Frame(TRUE);
	POKEY_Frame();
	Sound_Update();
	Atari800_nframes++;
}

EMSCRIPTEN_KEEPALIVE UBYTE *web_screen(void) { return (UBYTE *) Screen_atari; }
EMSCRIPTEN_KEEPALIVE int *web_colours(void) { return Colours_table; }
EMSCRIPTEN_KEEPALIVE int web_is_pal(void) { return Atari800_tv_mode == Atari800_TV_PAL; }

/* Loads a saved state, or boots a disk, program or cartridge image, from
   the module's file system (the page writes the file there first). */
EMSCRIPTEN_KEEPALIVE int web_load_state(const char *path)
{
	return StateSav_ReadAtariState(path, "rb");
}

EMSCRIPTEN_KEEPALIVE int web_open_file(const char *path)
{
	int r = AFILE_OpenFile(path, TRUE, 1, FALSE);
	if (r == AFILE_ERROR)
		return FALSE;
	if ((r & 0xff) == AFILE_ROM && (r >> 8) > 0) {
		/* a cartridge image without a header: its type goes by its size */
		int type = UI_SelectCartType(r >> 8);
		if (type == CARTRIDGE_NONE)
			return FALSE;
		CARTRIDGE_SetTypeAutoReboot(&CARTRIDGE_main, type);
	}
	return TRUE;
}

/* ------------------------------ for the extensions ------------------------------ */

/* The page's handler for an address a script asked for: returns the opcode
   to run in place of op (see ext.h) */
EM_JS(int, web_js_code_injection, (int pc, int op), {
	return Module.onCodeInjection ? Module.onCodeInjection(pc, op) : op;
});

int ext_handle_code_injection(int pc, int op)
{
	if (ext_cpu_faking || !ext_code_injection_wanted(pc))
		return op;
	return web_js_code_injection(pc, op);
}

EMSCRIPTEN_KEEPALIVE UBYTE *web_mem(void) { return MEMORY_mem; }
EMSCRIPTEN_KEEPALIVE UBYTE *web_xe_bank(int n) { return MEMORY_XEBank(n); }
EMSCRIPTEN_KEEPALIVE int web_peek(int addr) { return MEMORY_GetByte(addr & 0xffff); }
EMSCRIPTEN_KEEPALIVE void web_poke(int addr, int value) { MEMORY_PutByte(addr & 0xffff, (UBYTE) value); }

enum { CPU_A, CPU_X, CPU_Y, CPU_S, CPU_P, CPU_PC };

EMSCRIPTEN_KEEPALIVE int web_cpu_get(int reg)
{
	switch (reg) {
	case CPU_A: return CPU_regA;
	case CPU_X: return CPU_regX;
	case CPU_Y: return CPU_regY;
	case CPU_S: return CPU_regS;
	case CPU_P: return CPU_regP;
	default: return CPU_regPC;
	}
}

EMSCRIPTEN_KEEPALIVE void web_cpu_set(int reg, int value)
{
	switch (reg) {
	case CPU_A: CPU_regA = (UBYTE) value; break;
	case CPU_X: CPU_regX = (UBYTE) value; break;
	case CPU_Y: CPU_regY = (UBYTE) value; break;
	case CPU_S: CPU_regS = (UBYTE) value; break;
	case CPU_P: CPU_regP = (UBYTE) value; break;
	default: CPU_regPC = (UWORD) value; break;
	}
}

/* The ANTIC and GTIA registers a script can read, in the order of web/api.js */
EMSCRIPTEN_KEEPALIVE int web_reg(int reg)
{
	switch (reg) {
	case 0: return ANTIC_dlist;
	case 1: return ANTIC_HSCROL;
	case 2: return ANTIC_VSCROL;
	case 3: return ANTIC_CHBASE;
	case 4: return ANTIC_PMBASE;
	case 5: return ANTIC_DMACTL;
	case 6: return GTIA_COLBK;
	case 7: return GTIA_COLPF0;
	case 8: return GTIA_COLPF1;
	case 9: return GTIA_COLPF2;
	case 10: return GTIA_COLPF3;
	case 11: return GTIA_COLPM0;
	case 12: return GTIA_COLPM1;
	case 13: return GTIA_COLPM2;
	case 14: return GTIA_COLPM3;
	case 15: return GTIA_HPOSP0;
	case 16: return GTIA_HPOSP1;
	case 17: return GTIA_HPOSP2;
	case 18: return GTIA_HPOSP3;
	case 19: return GTIA_SIZEP0;
	case 20: return GTIA_SIZEP1;
	case 21: return GTIA_SIZEP2;
	case 22: return GTIA_SIZEP3;
	case 23: return GTIA_GRAFP0;
	case 24: return GTIA_GRAFP1;
	case 25: return GTIA_GRAFP2;
	case 26: return GTIA_GRAFP3;
	case 27: return GTIA_PRIOR;
	case 28: return GTIA_GRACTL;
	case 29: return GTIA_HPOSM0;
	case 30: return GTIA_HPOSM1;
	case 31: return GTIA_HPOSM2;
	case 32: return GTIA_HPOSM3;
	case 33: return GTIA_SIZEM;
	case 34: return GTIA_GRAFM;
	default: return 0;
	}
}

EMSCRIPTEN_KEEPALIVE int web_fakecpu_until_pc(int pc, int max_insns) { return ext_fakecpu_until_pc(pc, max_insns); }
EMSCRIPTEN_KEEPALIVE int web_fakecpu_until_op(int op, int max_insns) { return ext_fakecpu_until_op(op, max_insns); }
EMSCRIPTEN_KEEPALIVE int web_fakecpu_until_after_op(int op, int max_insns) { return ext_fakecpu_until_after_op(op, max_insns); }
EMSCRIPTEN_KEEPALIVE int web_fakecpu_while_in(int lo, int hi, int max_insns) { return ext_fakecpu_while_in(lo, hi, max_insns); }
EMSCRIPTEN_KEEPALIVE void web_set_code_injections(const int *addresses, int count) { ext_set_code_injections(addresses, count); }

/* The profile: per address, how often it ran (what = 0) or the cycles spent (1) */
EMSCRIPTEN_KEEPALIVE void web_profile(int what, double *out)
{
	int i;
	for (i = 0; i < 0x10000; i++)
		out[i] = (double) (what ? MONITOR_coverage[i].cycles : MONITOR_coverage[i].count);
}

EMSCRIPTEN_KEEPALIVE void web_profile_reset(void)
{
	memset(MONITOR_coverage, 0, sizeof(MONITOR_coverage));
	MONITOR_coverage_insns = 0;
	MONITOR_coverage_cycles = 0;
}
