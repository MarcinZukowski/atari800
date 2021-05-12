#include "ext.h"

#include "ext-lua.h"

#include <SDL.h>

#include <assert.h>
#include <string.h>

#include "ext/ext-altreal.h"
#include "ext/ext-bjl.h"
#include "ext/ext-mercenary.h"
#include "ext/ext-river-raid.h"
#include "ext/ext-yoomp.h"
#include "ext/ext-zybex.h"

#include "antic.h"
#include "cpu.h"
#include "memory.h"
#include "monitor.h"
#include "ui.h"

#define NUM_STATES 6
static ext_state* states[NUM_STATES];
static ext_state* current_state = NULL;

#define RAM_SIZE (64*1024)

static byte code_injection_map[RAM_SIZE];
static int code_injection_map_set = 0;

static int inside_menu = 0;

static int faking_cpu = 0;

static void set_current_state(ext_state *state)
{
	current_state = state;

	printf("Set current state to: %s\n", current_state ? current_state->name : "-none-");

	if (!state) {
		return;
	}

	// Initialize code injections
	int i = 0;
	memset(code_injection_map, 0, RAM_SIZE);
	code_injection_map_set = 0;
	if (current_state->injection_list) {
		for (i = 0; current_state->injection_list[i] >= 0; i++) {
			code_injection_map[current_state->injection_list[i]] = 1;
		}
		code_injection_map_set = 1;
	}

	printf("State set, %d code injections\n", i);
}

void ext_init()
{
	ext_lua_init();

	states[0] = ext_register_yoomp();
	assert(states[0]);
	states[1] = ext_register_mercenary();
	assert(states[1]);
	states[2] = ext_register_zybex();
	assert(states[2]);
	states[3] = ext_register_altreal();
	assert(states[3]);
	states[4] = ext_register_bjl();
	assert(states[4]);
	states[5] = ext_register_river_raid();
	assert(states[5]);

//	set_current_state(states[1]);
	if (current_state) {
		current_state->initialize();
	}
}

static void ext_choose_ext()
{
	if (inside_menu) {
		return;
	}

	// Choose extension
	if (!current_state) {
		for (int i = 0; i < NUM_STATES; i++) {
			if (states[i]->initialize()) {
				set_current_state(states[i]);
				break;
			}
		}
	}

	static UI_tMenuItem menu_array_template[] = {
		UI_MENU_ACTION(100, "Found extension:"),
		// We'll include here the additional entries
		UI_MENU_ACTION(101, "EXIT"),
		UI_MENU_END
	};

	#define MAX_CONFIG_ITEMS 10
	UI_tMenuItem *menu_array = alloca(sizeof(UI_tMenuItem) * MAX_CONFIG_ITEMS);

	int option = 0;
	UI_driver->fInit();
	inside_menu = 1;
	for (;;) {
		// Prepare menu including the extension's menu
		UI_tMenuItem* ext_config = (current_state && current_state->get_config) ? current_state->get_config() : NULL;

		menu_array[0] = menu_array_template[0];
		menu_array[0].suffix = current_state ? current_state->name : "-UNKNOWN-";

		int idx = 1;
		if (ext_config) {
			for (int i = 0; ext_config[i].flags != UI_ITEM_END; i++, idx++) {
				menu_array[idx] = ext_config[i];
			}
		}

		// Add EXIT
		menu_array[idx++] = menu_array_template[1];
		menu_array[idx] = menu_array_template[2];

		// Run the menu
		option = UI_driver->fSelect("Extensions", 0, option, menu_array, NULL);
		if (option < 0 || option == 101) {
			break;
		}
		if (current_state && current_state->handle_config) {
			current_state->handle_config(option);
		}
	}
	inside_menu = 0;
}

void ext_frame(void)
{
	if (inside_menu) {
		return;
	}
	const Uint8 *state = SDL_GetKeyState(NULL);
	if (!state[SDLK_TAB]) {
		return;
	}
	ext_choose_ext();
}

void ext_pre_gl_frame(void)
{
	if (inside_menu) {
		return;
	}
	if (current_state && current_state->pre_gl_frame) {
		current_state->pre_gl_frame();
	}
}

void ext_post_gl_frame(void)
{
	if (inside_menu) {
		return;
	}
	if (current_state && current_state->post_gl_frame) {
		current_state->post_gl_frame();
	}
}

int ext_handle_code_injection(int pc, int op)
{
	if (faking_cpu) {
		return op;
	}
	if (current_state && current_state->code_injection) {
		if (code_injection_map_set && !code_injection_map[pc]) {
			// No need to call
			return op;
		}
		return current_state->code_injection(pc, op);
	}

	return op;
}

ext_state* ext_state_alloc(void)
{
	ext_state *s = malloc(sizeof(ext_state));
	assert(s);
	memset(s, 0, sizeof(*s));

	return s;
}

/* =========================================== FPS ===========================  */
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

	snprintf(fps_buf, 20, "FRAMES: %d ", fps_last_frames);
	return fps_buf;
}

/* =========================================== FAKE CPU =========================== */

static int prev_CPU_IRQ;
static int prev_ANTIC_wsync_halt;
static int prev_ANTIC_cur_screen_pos;
static int prev_ANTIC_xpos;
static int prev_ANTIC_xpos_limit;
static int prev_ANTIC_delayed_wsync;
static FILE *prev_MONITOR_trace_file;


static void ext_fakecpu_init()
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

static void ext_fakecpu_do_one()
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

static void ext_fakecpu_finish()
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

static int ext_fakecpu_until(int end_pc, int end_op, int after)
{
	// Save state
	ext_fakecpu_init();

	// Re-execute this instruction
	CPU_regPC--;

	do {
		ext_fakecpu_do_one();
		if (end_pc && CPU_regPC == end_pc) {
			break;
		}
		if (end_op && MEMORY_mem[CPU_regPC] == end_op) {
			break;
		}
	} while (TRUE);

	if (after) {
		ext_fakecpu_do_one();
	}

	ext_fakecpu_finish();
	return OP_NOP;
}

int ext_fakecpu_until_pc(int end_pc)
{
	return ext_fakecpu_until(end_pc, /*end_op=*/ 0, /*after=*/ 0);
}

int ext_fakecpu_until_op(int end_op)
{
	return ext_fakecpu_until(/*end_pc=*/ 0, end_op, /*afger=*/ 0);
}

int ext_fakecpu_until_after_op(int end_op)
{
	return ext_fakecpu_until(/*end_pc=*/ 0, end_op, /*after=*/ 1);
}
