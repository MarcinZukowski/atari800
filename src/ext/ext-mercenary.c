#include "ext-mercenary.h"

#include <assert.h>

#include "memory.h"
#include "cpu.h"

int mercenary_code_injections(int pc, int op)
{
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
		return OP_NOP;;
	}
	return op;
}

ext_state* ext_register_mercenary(void)
{
	ext_state *s = ext_state_alloc();
	s->code_injection = mercenary_code_injections;
	s->render_frame = NULL;

	return s;
}
