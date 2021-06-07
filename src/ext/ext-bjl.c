#include "ext-bjl.h"

#include <assert.h>

#include "antic.h"
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
	if (config_accelerate == ACC_NONE || ext_acceleration_disabled()) {
		return op;
	}

	if (pc == 0xb51c)  {
		return ext_fakecpu_until_op(OP_RTS);
	}

	if (config_accelerate == ACC_LOW) {
		return op;
	}

	if (pc  == 0x9da7 || pc == 0xaf32)  {
		return ext_fakecpu_until_op(OP_RTS);
	}

	return op;
}

static int init(void)
{
	// Some memory fingerprint from 0x29A4
	byte fingerprint_41fc[] = {0x6A, 0x61, 0x67, 0x67, 0x69};

	if (memcmp(MEMORY_mem + 0x41fc, fingerprint_41fc, sizeof(fingerprint_41fc))) {
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

	const char *fps_str = ext_fps_str(ANTIC_dlist);
	Print(0x9f, 0x90, fps_str, 0, -2, 20);
}

ext_state* ext_register_bjl(void)
{
	ext_state *s = ext_state_alloc();
	static char* name  = "Behind Jaggi Lines HACK by ERU";
	s->initialize = init;
	s->name = name;
	s->code_injection = code_injections;
	s->pre_gl_frame = pre_gl_frame;

	s->get_config = get_config;
	s->handle_config = handle_config;

	return s;
}
