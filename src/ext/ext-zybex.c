#include "ext-zybex.h"

#include <assert.h>

#include <SDL.h>

#include "antic.h"
#include "cpu.h"
#include "memory.h"
#include "ui.h"
#include "ui_basic.h"
#include "screen.h"

static int config_display_fps = 1;

static int init(void)
{
	// Some memory fingerprint from 0x3000
	byte fingerprint_3000[] = {0x18, 0x69, 0x14, 0xA8, 0xC0, 0x50};

	if (memcmp(MEMORY_mem + 0x3000, fingerprint_3000, sizeof(fingerprint_3000))) {
		// No match
		return 0;
	}

	// Match
	return 1;
}

static void refresh_config(struct UI_tMenuItem *menu)
{
	menu[1].suffix = config_display_fps ? "ON" : "OFF";
}

static void add_to_config(struct UI_tMenuItem *menu)
{
	static UI_tMenuItem configs[] = {
		UI_MENU_ACTION(1, "Display FPS:"),
		UI_MENU_END
	};
	for (int i = 0; i  < sizeof(configs) / sizeof(*configs); i++) {
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
	}
	refresh_config(menu);
}

static void pre_gl_frame()
{
	if (!config_display_fps) {
		return;
	}

	const char *fps_str = ext_fps_str(ANTIC_dlist);
	Print(0x9f, 0x90, fps_str, 0, -1, 20);

	int x, y;
	int ymin = 0;
	int ymax = Screen_HEIGHT;
	int xmin = 0;
	int xmax = Screen_WIDTH;
	UBYTE *scr = Screen_atari;

	for (y = ymin; y < ymax; y++) {
		for (x = xmin; x < xmax; x++) {
			int idx = Screen_WIDTH * y + x;
			if (scr[idx] == 0) {
				scr[idx] = x + y;
			}
		}
	}

}


ext_state* ext_register_zybex(void)
{
	ext_state *s = ext_state_alloc();
	static char* name  = "ZYBEX HACK by ERU";
	s->initialize = init;
	s->name = name;
	s->code_injection = NULL;
	s->pre_gl_frame = pre_gl_frame;

	s->add_to_config = add_to_config;
	s->handle_config = handle_config;

	return s;
}
