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

static int inside_menu = 0;

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
		UI_MENU_ACTION(0, "Found extension:"),
//		UI_MENU_SUBMENU_SUFFIX(1, "Frequency:", freq_string),
		UI_MENU_END
	};

	#define MAX_CONFIG_ITEMS 10
	UI_tMenuItem *menu_array = malloc(sizeof(UI_tMenuItem) * MAX_CONFIG_ITEMS);

	menu_array[0] = menu_array_template[0];
	menu_array[1] = menu_array_template[1];

	menu_array[0].suffix = current_state ? current_state->name : "-UNKNOWN-";

	if (current_state && current_state->add_to_config) {
		current_state->add_to_config(menu_array);
	}

	int option = 0;
	UI_driver->fInit();
	inside_menu = 1;
	for (;;) {
		option = UI_driver->fSelect("Extensions", 0, option, menu_array, NULL);
		printf("Option: %d\n", option);
		if (option < 0) {
			break;
		}
		if (current_state && current_state->handle_config) {
			current_state->handle_config(menu_array, option);
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

void ext_gl_frame(void)
{
	if (inside_menu) {
		return;
	}
	if (current_state && current_state->render_frame) {
		current_state->render_frame();
	}
}

int ext_handle_code_injection(int pc, int op)
{
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
