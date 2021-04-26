#include "ext-altreal.h"

#include <assert.h>

#include "memory.h"
#include "ui.h"
#include "ui_basic.h"

static int config_display_fps = 1;
static int config_accelerate = 1;

// Seems calling here is once per frame
static int calls_7856 = 0;

static int code_injections(const int pc, int op)
{
	if (pc == 0x7856) { // Moving into font memory?
		calls_7856++;
	}

	if (!config_accelerate) {
		return op;
	}

	if (1 && pc == 0x90) {  // Drawing?
		return ext_fakecpu_until_pc(0x00D5);
	}

	if (1 && pc == 0x4A69) { // ??
		return ext_fakecpu_until_pc(0x4A82);
	}

	if (1 && pc == 0x3884) {  // ??
		return ext_fakecpu_until_pc(0x38CE);
	}

	if (1 && pc == 0x7858) { // Moving into font memory?
		return ext_fakecpu_until_pc(0x7887);
	}

	if (1 && pc == 0x7A1F) { // ??
		return ext_fakecpu_until_pc(0x7A36);
	}

	if (1 && pc == 0x7F1B) { // ??
		return ext_fakecpu_until_pc(0x7F4A);
	}

	return op;
}

static int init(void)
{
	// Some memory fingerprint from 0x29A4
	byte fingerprint_29B6[] = {0x44, 0x75, 0x6E, 0x67, 0x65, 0x6F, 0x6E};

	if (memcmp(MEMORY_mem + 0x29B6, fingerprint_29B6, sizeof(fingerprint_29B6))) {
		// No match
		return 0;
	}

	// Match
	return 1;
}

static void refresh_config(struct UI_tMenuItem *menu)
{
	menu[1].suffix = config_display_fps ? "ON" : "OFF";
	menu[2].suffix = config_accelerate ? "ON" : "OFF";
}

static void add_to_config(struct UI_tMenuItem *menu)
{
	static UI_tMenuItem configs[] = {
		UI_MENU_ACTION(1, "Display FPS:"),
		UI_MENU_ACTION(2, "Accelerate:"),
		UI_MENU_END
	};
	for (int i = 0; i  < sizeof(configs)/sizeof(*configs); i++) {
		menu[1 + i] = configs[i];
	}
	refresh_config(menu);
}

static void handle_config(struct UI_tMenuItem *menu, int option)
{
	switch (option) {
		case 1:
			config_display_fps ^= 1;
			break;
		case 2:
			config_accelerate ^= 1;
			break;
	}
	refresh_config(menu);
}

static void pre_gl_frame()
{
	if (!config_display_fps) {
		return;
	}

	const char *fps_str = ext_fps_str(calls_7856);
	Print(0x9f, 0x90, fps_str, 0, -1, 20);
}

ext_state* ext_register_altreal(void)
{
	ext_state *s = ext_state_alloc();
	static char* name  = "Alt.Real. HACK by ERU";
	s->initialize = init;
	s->name = name;
	s->code_injection = code_injections;
	s->pre_gl_frame = pre_gl_frame;

	s->add_to_config = add_to_config;
	s->handle_config = handle_config;

	return s;
}
