#include "ext-altreal.h"

#include <assert.h>

#include "memory.h"
#include "ui.h"
#include "ui_basic.h"

static int config_display_fps = 1;
#define ACC_NONE 0
#define ACC_LOW 1
#define ACC_HIGH  2
#define ACC_COUNT 3
static int config_accelerate = ACC_LOW;

// Seems calling here is once per frame
static int calls_7856 = 0;

static int code_injections(const int pc, int op)
{
	if (pc == 0x7856) { // Moving into font memory?
		calls_7856++;
	}

	if (config_accelerate == ACC_NONE || ext_acceleration_disabled()) {
		return op;
	}

	if (pc == 0x90) {  // Drawing?
		return ext_fakecpu_until_pc(0x00D5);
	}

	if (config_accelerate == ACC_LOW) {
		return op;
	}

	if (pc == 0x4A69) { // ??
		return ext_fakecpu_until_pc(0x4A82);
	}

	if (pc == 0x3884) {  // ??
		return ext_fakecpu_until_pc(0x38CE);
	}

	if (pc == 0x7858) { // Moving into font memory?
		return ext_fakecpu_until_pc(0x7887);
	}

	if (pc == 0x7A1F) { // ??
		return ext_fakecpu_until_pc(0x7A36);
	}

	if (pc == 0x7F1B) { // ??
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

static UI_tMenuItem menu[] = {
	UI_MENU_ACTION(0, "Display FPS:"),
	UI_MENU_ACTION(1, "Acceleration:"),
	UI_MENU_END
};

static void refresh_config()
{
	const char* config_accelerate_strings[ACC_COUNT] = { "OFF", "LOW", "HIGH" };
	menu[0].suffix = config_display_fps ? "ON" : "OFF";
	menu[1].suffix = config_accelerate_strings[config_accelerate];
}

static struct UI_tMenuItem* get_config()
{
	refresh_config();
	return menu;
}

static void handle_config(int option)
{
	switch (option) {
		case 0:
			config_display_fps ^= 1;
			break;
		case 1:
			config_accelerate = (config_accelerate + 1) % ACC_COUNT;
			break;
	}
	refresh_config();
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

	s->get_config = get_config;
	s->handle_config = handle_config;

	return s;
}
