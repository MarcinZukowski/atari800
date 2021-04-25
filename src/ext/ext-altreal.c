#include "ext-altreal.h"

#include <assert.h>

#include "cpu.h"
#include "antic.h"
#include "memory.h"
#include "monitor.h"
#include "ui.h"
#include "ui_basic.h"

static int config_display_fps = 1;
static int config_accelerate = 1;

static int faking_cpu = 0;

int prev_CPU_IRQ;
int prev_ANTIC_wsync_halt;
int prev_ANTIC_cur_screen_pos;
int prev_ANTIC_xpos;
int prev_ANTIC_xpos_limit;
int prev_ANTIC_delayed_wsync;
FILE *prev_MONITOR_trace_file;

static void fakecpu_init()
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

static void fakecpu_do_one()
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

static void fakecpu_finish()
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

static int fakecpu_until(int end_pc)
{
	// Save state
	fakecpu_init();

	// Re-execute this instruction
	CPU_regPC--;

	do {
		fakecpu_do_one();
	} while (CPU_regPC != end_pc);
	fakecpu_finish();
	return OP_NOP;
}

static int last_dl_value = 0;
static int frames = 0;
static int last_frames = 0;
// Seems calling here is once per frame
static int calls_7856 = 0;

static int code_injections(const int pc, int op)
{
	if (faking_cpu) {
//		printf("Faking, return\n");
		return op;
	}

	if (pc == 0x7856) { // Moving into font memory?
		calls_7856++;
	}

	if (!config_accelerate) {
		return op;
	}

	if (1 && pc == 0x90) {  // Drawing?
		return fakecpu_until(0x00D5);
	}

	if (1 && pc == 0x4A69) { // ??
		return fakecpu_until(0x4A82);
	}

	if (1 && pc == 0x3884) {  // ??
		return fakecpu_until(0x38CE);
	}

	if (1 && pc == 0x7858) { // Moving into font memory?
		return fakecpu_until(0x7887);
	}

	if (1 && pc == 0x7A1F) { // ??
		return fakecpu_until(0x7A36);
	}

	if (1 && pc == 0x7F1B) { // ??
		return fakecpu_until(0x7F4A);
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

	// Display FPS (well, really vblanks-per-frame)
	// A change in 0x2805 is a new frame
	char buf[20];
	frames++;
	int dl_value = calls_7856;
	if (dl_value != last_dl_value) {
		last_frames = frames;
		frames = 0;
		last_dl_value = dl_value;
	}

	snprintf(buf, 20, "FRAMES: %d ", last_frames);

	Print(0x9f, 0x90, buf, 0, -1, 20);
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
