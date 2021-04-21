#include "ext-mercenary.h"

#include <assert.h>

#include <SDL.h>

#include "cpu.h"
#include "memory.h"
#include "ui.h"
#include "ui_basic.h"

static int config_display_fps = 1;
static int config_control_to_accelerate = 1;
static int config_accel_lines = 0;
static int config_accel_background = 0;

static int mercenary_code_injections(int pc, int op)
{
	int accel = 0;
	if (config_control_to_accelerate) {
		const Uint8 *state = SDL_GetKeyState(NULL);
		if (state[SDLK_LCTRL] || state[SDLK_RCTRL]) {
			accel = 1;
		}
	}

	if (accel || config_accel_lines) {
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

				while (1) {
					byte curX = Xmajor ? majorCur : minorCur;
					byte curY = Xmajor ? minorCur : majorCur;

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
					MEMORY_PutByte(adr, newByte);

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

				MEMORY_PutByte(0x06, fracCur);
				CPU_regX = Xmajor ? majorCur : minorCur;
				CPU_regY = Xmajor ? minorCur : majorCur;

				return OP_RTS;
			}
		}
	}


	// pixel drawing
	if (pc == 0x538a) {
		// implement me
		return op;
	}

	if (accel || config_accel_background) {
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
			return OP_NOP;;
		}
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

	// Match
	return 1;
}

static void mercenary_refresh_config(struct UI_tMenuItem *menu)
{
	menu[1].suffix = config_display_fps ? "ON" : "OFF";
	menu[2].suffix = config_control_to_accelerate ? "ON" : "OFF";
	menu[3].suffix = config_accel_lines ? "ON" : "OFF";
	menu[4].suffix = config_accel_background ? "ON" : "OFF";
}

static void mercenary_add_to_config(struct UI_tMenuItem *menu)
{
	static UI_tMenuItem configs[] = {
		UI_MENU_ACTION(1, "Display FPS:"),
		UI_MENU_ACTION(2, "CONTROL to accelerate:"),
		UI_MENU_ACTION(3, "Accelerate lines:"),
		UI_MENU_ACTION(4, "Accelerate background:"),
		UI_MENU_END
	};
	for (int i = 0; i  < 5; i++) {
		menu[1 + i] = configs[i];
	}
	mercenary_refresh_config(menu);
}

static void mercenary_handle_config(struct UI_tMenuItem *menu, int option)
{
	switch (option) {
		case 1:
			config_display_fps ^= 1;
			break;
		case 2:
			config_control_to_accelerate ^= 1;
			break;
		case 3:
			config_accel_lines ^= 1;
			break;
		case 4:
			config_accel_background ^= 1;
			break;
	}
	mercenary_refresh_config(menu);
}

static int last_dl_value = 0;
static int frames = 0;
static int last_frames = 0;

static void mercenary_pre_gl_frame()
{
	if (!config_display_fps) {
		return;
	}

	// Display FPS (well, really vblanks-per-frame)
	// A change in 0x2805 is a new frame
	char buf[20];
	frames++;
	int dl_value = MEMORY_dGetByte(0x2805);
	if (dl_value != last_dl_value) {
		last_frames = frames;
		frames = 0;
		last_dl_value = dl_value;
	}

	snprintf(buf, 20, "FRAMES: %d ", last_frames);

	Print(0x9f, 0x90, buf, 0, -1, 20);
}

ext_state* ext_register_mercenary(void)
{
	ext_state *s = ext_state_alloc();
	static char* name  = "MERCENARY HACK by ERU";
	s->initialize = mercenary_init;
	s->name = name;
	s->code_injection = mercenary_code_injections;
	s->pre_gl_frame = mercenary_pre_gl_frame;

	s->add_to_config = mercenary_add_to_config;
	s->handle_config = mercenary_handle_config;

	return s;
}
