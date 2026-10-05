/*
 * ext-cpu.c - the part of the extension plumbing that only touches the CPU:
 * which addresses a script wants to be called at, and the "fake CPU" that
 * runs 6502 code in no emulated time. Shared by the native host (ext.c with
 * QuickJS) and the web build (web/web.c, where the scripts run in the page).
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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "antic.h"
#include "cpu.h"
#include "memory.h"
#include "monitor.h"

/* Which addresses the active extension wants onCodeInjection for */
#define RAM_SIZE 0x10000
static UBYTE code_injection_map[RAM_SIZE];
static int code_injection_map_set = 0;

int ext_cpu_faking = 0;

int ext_code_injection_wanted(int pc)
{
	return code_injection_map_set && code_injection_map[pc & 0xffff];
}

/* Replaces the code injection addresses */
void ext_set_code_injections(const int *addresses, int count)
{
	int i;
	memset(code_injection_map, 0, RAM_SIZE);
	for (i = 0; i < count; i++)
		if (addresses[i] >= 0 && addresses[i] < RAM_SIZE)
			code_injection_map[addresses[i]] = 1;
	code_injection_map_set = count > 0;
}

/* ======================================== FAKE CPU ======================================== */

/* Runs instructions with ANTIC stopped and interrupts deferred (the CPU does
   not service IRQs while ext_cpu_faking is set, and ANTIC raises no NMI
   since no time passes), so that they take no emulated time and leave
   everything but the CPU state untouched. The IRQ line itself is left to
   the hardware: code run this way may acknowledge or enable an interrupt. */

static int prev_ANTIC_wsync_halt;
static int prev_ANTIC_cur_screen_pos;
static int prev_ANTIC_xpos;
static int prev_ANTIC_xpos_limit;
static int prev_ANTIC_delayed_wsync;
#ifdef MONITOR_TRACE
static FILE *prev_MONITOR_trace_file;
#endif

static void fakecpu_begin(void)
{
	assert(!ext_cpu_faking);
	ext_cpu_faking = 1;
	prev_ANTIC_wsync_halt = ANTIC_wsync_halt;
	prev_ANTIC_cur_screen_pos = ANTIC_cur_screen_pos;
	prev_ANTIC_xpos = ANTIC_xpos;
	prev_ANTIC_xpos_limit = ANTIC_xpos_limit;
	prev_ANTIC_delayed_wsync = ANTIC_delayed_wsync;
#ifdef MONITOR_TRACE
	prev_MONITOR_trace_file = MONITOR_trace_file;
#endif
}

static void fakecpu_step(void)
{
	assert(ext_cpu_faking);
	ANTIC_wsync_halt = 0;
	ANTIC_cur_screen_pos = ANTIC_NOT_DRAWING;
	ANTIC_xpos = 0;
	ANTIC_xpos_limit = 1;
#ifdef MONITOR_TRACE
	MONITOR_trace_file = NULL;
#endif
	CPU_GO(1);
}

static void fakecpu_end(void)
{
	ANTIC_wsync_halt = prev_ANTIC_wsync_halt;
	ANTIC_cur_screen_pos = prev_ANTIC_cur_screen_pos;
	ANTIC_xpos = prev_ANTIC_xpos;
	ANTIC_xpos_limit = prev_ANTIC_xpos_limit;
	ANTIC_delayed_wsync = prev_ANTIC_delayed_wsync;
#ifdef MONITOR_TRACE
	MONITOR_trace_file = prev_MONITOR_trace_file;
#endif
	assert(ext_cpu_faking);
	ext_cpu_faking = 0;
}

/* A run to an address or opcode the code never reaches would go on for ever:
   a game that loaded other code over a hooked routine (a shop in Alternate
   Reality) runs that code through the hook one day. After this many
   instructions the run is given up with a warning, and the CPU goes on from
   where it got to, in real time. 1,000,000 is about a second of 6502 time;
   the loops the extensions skip take thousands. */
#define FAKECPU_DEFAULT_MAX_INSNS 1000000

/* The warning once per hooked address, so that a hook met every frame does
   not flood the output */
static int gave_up_at[16];
static int gave_up_count = 0;

static void fakecpu_gave_up(int start_pc, int end_pc, int end_op, int n)
{
	int i;
	for (i = 0; i < gave_up_count; i++)
		if (gave_up_at[i] == start_pc)
			return;
	if (gave_up_count < (int) (sizeof(gave_up_at) / sizeof(gave_up_at[0])))
		gave_up_at[gave_up_count++] = start_pc;
	if (end_pc)
		printf("ext: the fake CPU run from $%04X to $%04X gave up at $%04X after %d instructions: "
		       "the code there is not what the hook expects. The game goes on in real time.\n", start_pc, end_pc, CPU_regPC, n);
	else
		printf("ext: the fake CPU run from $%04X to opcode $%02X gave up at $%04X after %d instructions: "
		       "the code there is not what the hook expects. The game goes on in real time.\n", start_pc, end_op, CPU_regPC, n);
	fflush(stdout);
}

static int fakecpu_until(int end_pc, int end_op, int after, int max_insns)
{
	int n = 0, start_pc = CPU_regPC - 1;
	if (max_insns <= 0)
		max_insns = FAKECPU_DEFAULT_MAX_INSNS;
	fakecpu_begin();
	CPU_regPC--;   /* re-execute the current instruction */
	for (;;) {
		fakecpu_step();
		n++;
		if (end_pc && CPU_regPC == end_pc)
			break;
		if (end_op && MEMORY_mem[CPU_regPC] == end_op)
			break;
		if (n >= max_insns) {
			fakecpu_end();
			fakecpu_gave_up(start_pc, end_pc, end_op, n);
			return OP_NOP;
		}
	}
	if (after)
		fakecpu_step();
	fakecpu_end();
	return OP_NOP;
}

/* Runs the instruction at PC and the following ones in no emulated time for
   as long as the PC stays within lo..hi, but at most max_insns of them: a
   loop waiting for something that cannot change meanwhile (a counter the
   interrupts advance, VCOUNT) would otherwise never end. Returns how many
   instructions ran, negative when the budget ran out. */
int ext_fakecpu_while_in(int lo, int hi, int max_insns)
{
	int n = 0;
	fakecpu_begin();
	CPU_regPC--;   /* re-execute the current instruction */
	for (;;) {
		fakecpu_step();
		n++;
		if (CPU_regPC < lo || CPU_regPC > hi)
			break;
		if (n >= max_insns) {
			n = -n;
			break;
		}
	}
	fakecpu_end();
	return n;
}

int ext_fakecpu_until_pc(int end_pc, int max_insns)
{
	return fakecpu_until(end_pc, 0, 0, max_insns);
}

int ext_fakecpu_until_op(int end_op, int max_insns)
{
	return fakecpu_until(0, end_op, 0, max_insns);
}

int ext_fakecpu_until_after_op(int end_op, int max_insns)
{
	return fakecpu_until(0, end_op, 1, max_insns);
}
