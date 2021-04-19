#include "ext.h"

#include "ext-lua.h"

#include <SDL.h>
#include <assert.h>
#include <string.h>

#include "ext/ext-yoomp.h"
#include "ext/ext-mercenary.h"

static ext_state* states[2];
static ext_state* current_state = NULL;

void ext_init()
{
	ext_lua_init();

	states[0] = ext_register_yoomp();
	assert(states[0]);
	states[1] = ext_register_mercenary();
	assert(states[1]);

	current_state = states[0];
	printf("csci=%p", current_state->add_to_config);
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
		return current_state->code_injection(pc, op);
	}

	return op;

#if 0
	static UI_tMenuItem menu_array[] = {
		UI_MENU_CHECK(0, "Enable sound:"),
		UI_MENU_CHECK(1, "Enable sound:"),
//		UI_MENU_SUBMENU_SUFFIX(1, "Frequency:", freq_string),
		UI_MENU_END
	};

//	SetItemChecked(menu_array, 0, 1);

	int option = 0;
	UI_driver->fInit();
	option = UI_driver->fSelect("Sound Settings", 0, option, menu_array, NULL);
#endif

	return mercenary_code_injections(pc, op);
}

ext_state* ext_state_alloc(void)
{
	ext_state *s = malloc(sizeof(ext_state));
	assert(s);
	memset(s, 0, sizeof(*s));

	return s;
}
