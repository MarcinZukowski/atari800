#include "ext.h"

#include "ext-lua.h"

#include <SDL.h>
#include <assert.h>
#include <string.h>

#include "ext/ext-yoomp.h"
#include "ext/ext-mercenary.h"

#include "ui.h"

#define NUM_STATES 2
static ext_state* states[NUM_STATES];
static ext_state* current_state = NULL;

#define RAM_SIZE (64*1024)

static byte code_injection_map[RAM_SIZE];
static int code_injection_map_set = 0;

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

	printf("State set, %d code injections", i);
}

void ext_init()
{
	ext_lua_init();

	states[0] = ext_register_yoomp();
	assert(states[0]);
	states[1] = ext_register_mercenary();
	assert(states[1]);

//	set_state(states[0]);
}

static void ext_choose_ext()
{
	// Choose extension
	if (!current_state) {
		for (int i = 0; i < NUM_STATES; i++) {
			if (states[i]->initialize()) {
				set_current_state(states[i]);
				break;
			}
		}
	}

	static UI_tMenuItem menu_array[] = {
		UI_MENU_ACTION(0, "Found extension:"),
//		UI_MENU_SUBMENU_SUFFIX(1, "Frequency:", freq_string),
		UI_MENU_END
	};

	FindMenuItem(menu_array, 0)->suffix = current_state ? current_state->name : "-UNKNOWN-";

	int option = 0;
	UI_driver->fInit();
	option = UI_driver->fSelect("Extensions", 0, option, menu_array, NULL);
}

void ext_frame(void)
{
	const Uint8 *state = SDL_GetKeyState(NULL);
	if (!state[SDLK_RETURN]) {
		return;
	}
	ext_choose_ext();
}

void ext_gl_frame(void)
{
	if (current_state && current_state->render_frame) {
		current_state->render_frame();
	}
}

int ext_handle_code_injection(int pc, int op)
{
	const Uint8 *state = SDL_GetKeyState(NULL);
	if (!state[SDLK_LSHIFT]) {
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

#if 0
#endif
}

ext_state* ext_state_alloc(void)
{
	ext_state *s = malloc(sizeof(ext_state));
	assert(s);
	memset(s, 0, sizeof(*s));

	return s;
}
