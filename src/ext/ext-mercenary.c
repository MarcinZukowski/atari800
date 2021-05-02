#include "ext-mercenary.h"

#include <assert.h>

#include <SDL.h>
#include "sdl/video_gl-common.h"
#include "sdl/video_gl-ext.h"

#include "cpu.h"
#include "gtia.h"
#include "colours.h"
#include "memory.h"
#include "ui.h"
#include "ui_basic.h"

static int config_display_fps = 1;
static int config_accelerate = 1;
static int config_control_flips = 1;
#define CONFIG_LINE_MODE_A8 0
#define CONFIG_LINE_MODE_GL 1
#define CONFIG_LINE_MODE_BOTH 2
#define CONFIG_LINE_MODE_COUNT 3
static int config_line_mode = CONFIG_LINE_MODE_GL;
static char* config_line_modes[CONFIG_LINE_MODE_COUNT] = {
	"Atari native",
	"OpenGL",
	"Both"
};

// Info about a single drawn line
typedef struct drawn_line {
	int startX, startY;
	int endX, endY;
	int color;
} drawn_line;

#define MAX_LINES 100
// Holds info about all lines drawn in one frame
typedef struct drawing_state {
	drawn_line drawn_lines[MAX_LINES];
	int drawn_lines_count;
} drawing_state;

// Two states, we'll alternate between them as the screen changes
static drawing_state state1;
static drawing_state state2;
static drawing_state *shown_state = &state1;
static drawing_state *prepared_state = &state2;
static int shown_dl;

// A change in 0x2805 is a new frame
static int current_dl_byte() {
	return MEMORY_dGetByte(0x2805);
}

static int mercenary_code_injections(int pc, int op)
{
	int accel = config_accelerate;
	if (config_control_flips) {
		const Uint8 *state = SDL_GetKeyState(NULL);
		if (state[SDLK_LCTRL] || state[SDLK_RCTRL]) {
			accel ^= 1;
		}
	}

	if (!accel) {
		return op;
	}

	if (pc == 0x4dd8 || pc == 0x4fde || pc == 0x4e59 || pc == 0x4e12 || pc == 0x342c) {
		return ext_fakecpu_until_after_op(OP_RTS);
	}

	// line drawing - 8 different modes
	int lines[8][6] = {
		/*   PC  Xmajor  majorDelta, minorLimit, minorDelta, fracNeg */
		{0x5230,   TRUE,          1,       0x98,          1,   FALSE},  /* X right major, Y down minor, ADC delta */
		{0x525d,  FALSE,          1,       0xA0,          1,   FALSE},  /* Y down major, X rigth minor, ADC delta */
		{0x528A,  FALSE,          1,       0xFF,         -1,   FALSE},  /* Y down major, X left minor, ADC delta */
		{0x52b7,   TRUE,         -1,       0x98,          1,    TRUE},  /* X left major, Y down minor, SBC delta */
		{0x52E4,   TRUE,         -1,       0xFF,         -1,    TRUE},  /* X left major, Y up minor, SBC  delta */
		{0x5311,  FALSE,         -1,       0xFF,         -1,   FALSE},  /* Y up major, X left minor, ADC delta */
		{0x533e,  FALSE,         -1,       0xA0,          1,    TRUE},  /* Y up major, X right minor, SBC delta */
		{0x536b,   TRUE,          1,       0xFF,         -1,    TRUE},  /* X right major, Y up minor, ADC delta */
	};

	for (int l = 0; l < 8; l++) {
		if (pc == lines[l][0]) {
			int screen = MEMORY_dGetByte(0x23) * 0x0100 + 0x0010;

			byte X = CPU_regX;
			byte Y = CPU_regY;
			byte Xlimit = MEMORY_dGetByte(0x6a);
			byte Ylimit = MEMORY_dGetByte(0x6b);

			byte Xmajor = lines[l][1];
			byte majorCur = Xmajor ? X : Y;
			byte majorLimit = Xmajor ? Xlimit : Ylimit;
			byte majorDelta = lines[l][2];
			byte minorCur = Xmajor ? Y : X;
			byte minorLimit = lines[l][3];
			byte minorDelta = lines[l][4];

			byte fracCur = MEMORY_dGetByte(0x06);
			byte fracDelta = MEMORY_dGetByte(0x64);
			byte fracNeg = lines[l][5];

			int colorAnd = MEMORY_GetByte(0x5243) == 0x3D;

			byte curX, curY;
			byte startX = 0xFF, startY = 0xFF;

			while (1) {
				curX = Xmajor ? majorCur : minorCur;
				curY = Xmajor ? minorCur : majorCur;

				if (startX == 0xFF) {
					startX = curX;
					startY = curY;
				}

					MEMORY_PutByte(0x04, curY);

				int adr = screen + 40 * curY + (curX / 4);
				int mask = 0x03 << 2 * (3 - curX % 4);
				int oldByte = MEMORY_GetByte(adr);
				int newByte;
				if (colorAnd) {
					newByte = oldByte & (~mask);
				} else {
					// Seems we're only ORing odd bits here, this prevents lines from getting onto the sky
					newByte = oldByte | (mask & 0x55);
				}
				// Draw pixel, if needed
				if (config_line_mode != CONFIG_LINE_MODE_GL) {
					MEMORY_PutByte(adr, newByte);
				}

				if (majorCur == majorLimit) {
					break;
				}
				majorCur += majorDelta;
				byte fracNew = fracCur;
				if (fracNeg) {
					fracNew -= fracDelta;
					if (fracNew > fracCur) {
						minorCur += minorDelta;
					}
				} else {
					fracNew += fracDelta;
					if (fracNew < fracCur) {
						minorCur += minorDelta;
					}
				}
				fracCur = fracNew;
				if (minorCur == minorLimit) {
					break;
				}
			};

			assert(prepared_state->drawn_lines_count < MAX_LINES);
			prepared_state->drawn_lines[prepared_state->drawn_lines_count++] =
					(drawn_line){startX, startY, curX, curY, colorAnd};

			MEMORY_PutByte(0x06, fracCur);
			CPU_regX = Xmajor ? majorCur : minorCur;
			CPU_regY = Xmajor ? minorCur : majorCur;

			return OP_RTS;
		}
	}


	// pixel drawing
	if (pc == 0x538a) {
		// implement me
		return op;
	}

	// line fill: 1-color
	if (pc == 0x586f) {
		// X - how many lines
		// A - value
		// [00] - address
		int cx, cy, maxy, val;
		int dst = MEMORY_dGetWord(0x00);
		maxy = CPU_regX;
		val = CPU_regA;
		for (cy = 0; cy < maxy; cy++) {
			dst = MEMORY_dGetWord(0x00);
			for (cx = 0; cx < 40; cx++) {
				MEMORY_PutByte(dst + cx, val);
			}
			MEMORY_PutByte(0x18, MEMORY_GetByte(0x18) - 1);
			MEMORY_dPutWord(0x00, dst + 40);
		}
		// Fake we didn't do anything
		return OP_RTS;
	}

	// fill 2-color
	if (pc == 0x570e) {
		int dst = MEMORY_dGetWord(0x00);
		int change = MEMORY_dGetByte(0x7);
		change = (159 - CPU_regA);
		int c1 = MEMORY_dGetWord(0x80);
		int c2 = MEMORY_dGetWord(0x81);
		int x;
		int c1bytes = change / 4;
		int c2bytes = 40 - c1bytes;
		assert(c2bytes >= 0);
		for (x = 0; x < c1bytes; x++) {
			MEMORY_PutByte(dst + x, c1);
		}
		int mask = 0xFF >> (2 * (change & 3));
		int val = (c2 & mask) | (c1 & ~mask);
		MEMORY_PutByte(dst + c1bytes, val);
		for (x = 1; x < c2bytes; x++) {
			MEMORY_PutByte(dst + c1bytes + x, c2);
		}
		// Jump ahead of this line, as we don't implement that logic
		CPU_regPC = 0x5823;
		return OP_NOP;
	}

	return op;
}

static int mercenary_init(void)
{
	// Some memory fingerprint from 0x4000
	byte fingerprint_4000[] = {0xA6, 0x65, 0xBC, 0x57, 0x6B};

	if (memcmp(MEMORY_mem + 0x4000, fingerprint_4000, sizeof(fingerprint_4000))) {
		// No match
		return 0;
	}

	state1.drawn_lines_count = 0;
	state2.drawn_lines_count = 0;

	// Match
	return 1;
}

static UI_tMenuItem menu[] = {
	UI_MENU_ACTION(0, "Display FPS:"),
	UI_MENU_ACTION(1, "Accelerate:"),
	UI_MENU_ACTION(2, "CONTROL flips acceleration:"),
	UI_MENU_ACTION(3, "Line drawing mode:"),
	UI_MENU_END
};

static void refresh_config()
{
	menu[0].suffix = config_display_fps ? "ON" : "OFF";
	menu[1].suffix = config_accelerate ? "ON" : "OFF";
	menu[2].suffix = config_control_flips ? "ON" : "OFF";
	menu[3].suffix = config_line_modes[config_line_mode];
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
			config_accelerate ^= 1;
			break;
		case 2:
			config_control_flips ^= 1;
			break;
		case 3:
			config_line_mode = (config_line_mode + 1) % CONFIG_LINE_MODE_COUNT;
			break;
	}
	refresh_config();
}

static void mercenary_pre_gl_frame()
{
	if (!config_display_fps) {
		return;
	}

	const char *fps_str = ext_fps_str(current_dl_byte());
	Print(0x9f, 0x90, fps_str, 0, -1, 20);
}

static float adjustX(int x)
{
	float div = 336 / 2 / 2;
	return ((x - 80) / div) ;
}
static float adjustY(int y)
{
	return -1 * (2  * (y /239.0f) - 1) - 0.2;
}

static void mercenary_post_gl_frame()
{
	int dl = current_dl_byte();
	if (dl != shown_dl) {
		// DL change, flip prepared/shown states
		drawing_state *prev = shown_state;
		shown_state = prepared_state;
		prepared_state = prev;
		prev->drawn_lines_count = 0;
		shown_dl = dl;
	}

	if (config_line_mode == CONFIG_LINE_MODE_A8) {
		return;
	}

	// Draw lines with GL

	// Remember state
	gl.PushAttrib(GL_ENABLE_BIT);

	gl.Disable(GL_TEXTURE_2D);
	gl.Disable(GL_BLEND);

	gl.LineWidth(4);
	for (int i = 0 ; i < shown_state->drawn_lines_count; i++) {
		drawn_line *line = &shown_state->drawn_lines[i];
		// Current drawn color is in 0xA1 and 0xA4, or so it seems
		int color = line->color ? MEMORY_mem[0xA1] : MEMORY_mem[0xA4];
		gl.Color4f(Colours_GetR(color) / 255.0f, Colours_GetG(color) / 255.0f, Colours_GetB(color) / 255.0f, 1);
		float startX = adjustX(line->startX);
		float endX = adjustX(line->endX);
		float startY = adjustY(line->startY);
		float endY = adjustY(line->endY);
		gl.Begin(GL_LINES);
		gl.Vertex3f(startX, startY, -2.0);
		gl.Vertex3f(endX, endY, -2.0);
		gl.End();
	}

	// Restore state
	gl.PopAttrib();
	gl.Color4f(1.0f, 1.0f, 1.0f, 1.0f);
}

ext_state* ext_register_mercenary(void)
{
	ext_state *s = ext_state_alloc();
	static char* name  = "MERCENARY HACK by ERU";
	s->initialize = mercenary_init;
	s->name = name;
	s->code_injection = mercenary_code_injections;
	s->pre_gl_frame = mercenary_pre_gl_frame;
	s->post_gl_frame = mercenary_post_gl_frame;

	s->get_config = get_config;
	s->handle_config = handle_config;

	mercenary_init();

	return s;
}
