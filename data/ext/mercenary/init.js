// Mercenary: accelerated drawing (the game's line and fill routines are
// re-implemented here and the 6502 code is skipped) and the lines drawn again
// with OpenGL, as thin lines or as pixel-exact polygons. A port of the former
// C extension (src/ext/ext-mercenary.c); the addresses come from mercenary.md.
import { rgb, word } from "../common.js";

const MODE_A8 = 0, MODE_GL = 1, MODE_BOTH = 2;                       // line drawing mode
const TYPE_LINE = 0, TYPE_POLYGON_LINE = 1, TYPE_POLYGON_FILL = 2;   // GL line type

// Expensive routines that can simply be skipped (run on the fake CPU)
const SKIPPED_ROUTINES = [0x4DD8, 0x4FDE, 0x4E59, 0x4E12, 0x342C];

// The eight line drawing routines, one per direction:
// [pc, xMajor, majorDelta, minorLimit, minorDelta, fracNeg]
const LINE_ROUTINES = [
	[0x5230, true,   1, 0x98,  1, false],   // X right major, Y down minor, ADC delta
	[0x525D, false,  1, 0xA0,  1, false],   // Y down major, X right minor, ADC delta
	[0x528A, false,  1, 0xFF, -1, false],   // Y down major, X left minor, ADC delta
	[0x52B7, true,  -1, 0x98,  1, true],    // X left major, Y down minor, SBC delta
	[0x52E4, true,  -1, 0xFF, -1, true],    // X left major, Y up minor, SBC delta
	[0x5311, false, -1, 0xFF, -1, false],   // Y up major, X left minor, ADC delta
	[0x533E, false, -1, 0xA0,  1, true],    // Y up major, X right minor, SBC delta
	[0x536B, true,   1, 0xFF, -1, true],    // X right major, Y up minor, ADC delta
];
const FILL_ONE_COLOUR = 0x586F;
const FILL_TWO_COLOURS = 0x570E;

const mem = a8.mem;

// Lines drawn during one frame, kept in two sets: the one being shown and
// the one being prepared. They swap when the display list byte at $2805
// changes, which marks a new frame. With acceleration on, the game renders
// several of its frames per display frame, so allow for plenty of lines.
const MAX_LINES = 1000;
let shownLines = [];
let preparedLines = [];
let shownDl = -1;

// Mercenary pixel coordinates to GL coordinates. The Atari screen is 336x240
// in GL terms; the picture is 160 double-width pixels wide, starting 24
// lines down.
const halfPixelX = 2 / 336 / 2 * 2;
const halfPixelY = 2 / 240 / 2;
const adjustX = (x) => (x - 80) / (336 / 2 / 2) + halfPixelX;
const adjustY = (y) => -((y + 24 - 120) / (240 / 2) + halfPixelY);

/* ------------------------------ the game's drawing routines ------------------------------ */

// Re-implements one of the eight Bresenham-style line routines. X and Y hold
// the start, $6A/$6B the limits, $06 the fraction and $64 its delta. Remembers
// the line for the OpenGL pass and, if drawPixels, also plots it like the game.
function drawLine([, xMajor, majorDelta, minorLimit, minorDelta, fracNeg], drawPixels) {
	const screen = mem[0x23] * 0x0100 + 0x0010;
	const X = a8.cpu.x, Y = a8.cpu.y;
	const xLimit = mem[0x6A], yLimit = mem[0x6B];

	let majorCur = xMajor ? X : Y;
	const majorLimit = xMajor ? xLimit : yLimit;
	let minorCur = xMajor ? Y : X;

	let fracCur = mem[0x06];
	const fracDelta = mem[0x64];

	// The code at $53B7 patches the line routine to either OR or AND pixels
	const colourAnd = mem[0x5243] === 0x3D;

	let curX, curY;
	let startX = -1, startY = -1;
	for (;;) {
		curX = xMajor ? majorCur : minorCur;
		curY = xMajor ? minorCur : majorCur;
		if (startX < 0) {
			startX = curX;
			startY = curY;
		}

		mem[0x04] = curY;

		const adr = screen + 40 * curY + (curX >> 2);
		const mask = 0x03 << (2 * (3 - (curX & 3)));
		const oldByte = a8.peek(adr);
		// ORing only the odd bits keeps the lines off the sky
		const newByte = colourAnd ? oldByte & ~mask : oldByte | (mask & 0x55);
		if (drawPixels)
			a8.poke(adr, newByte);

		if (majorCur === majorLimit)
			break;
		majorCur = (majorCur + majorDelta) & 0xFF;

		// 8-bit fraction arithmetic: a wrap-around means a step on the minor axis
		let fracNew;
		if (fracNeg) {
			fracNew = (fracCur - fracDelta) & 0xFF;
			if (fracNew > fracCur)
				minorCur = (minorCur + minorDelta) & 0xFF;
		}
		else {
			fracNew = (fracCur + fracDelta) & 0xFF;
			if (fracNew < fracCur)
				minorCur = (minorCur + minorDelta) & 0xFF;
		}
		fracCur = fracNew;
		if (minorCur === minorLimit)
			break;
	}

	if (preparedLines.length < MAX_LINES)
		preparedLines.push({ startX, startY, endX: curX, endY: curY, colourAnd });

	// Leave the registers as the game's routine would
	mem[0x06] = fracCur;
	a8.cpu.x = xMajor ? majorCur : minorCur;
	a8.cpu.y = xMajor ? minorCur : majorCur;
	return a8.OP_RTS;
}

// $586F: fills X lines of 40 bytes with A, starting at ($00); $18 counts down
function fillOneColour() {
	const count = a8.cpu.x, value = a8.cpu.a;
	for (let cy = 0; cy < count; cy++) {
		const dst = word(0x00);
		for (let cx = 0; cx < 40; cx++)
			a8.poke(dst + cx, value);
		mem[0x18] = mem[0x18] - 1;   // wraps like the 6502 does
		const next = dst + 40;
		mem[0x00] = next;            // low byte
		mem[0x01] = next >> 8;       // high byte
	}
	return a8.OP_RTS;
}

// $570E: fills one line with two colours, ($80) left of the switch point
// 159 - A and ($81) right of it, then skips the game's own loop body
function fillTwoColours() {
	const dst = word(0x00);
	const change = 159 - a8.cpu.a;
	const c1 = word(0x80), c2 = word(0x81);   // only the low bytes get stored
	const c1bytes = Math.trunc(change / 4);
	const c2bytes = 40 - c1bytes;
	for (let x = 0; x < c1bytes; x++)
		a8.poke(dst + x, c1);
	const mask = 0xFF >> (2 * (change & 3));
	a8.poke(dst + c1bytes, (c2 & mask) | (c1 & ~mask));
	for (let x = 1; x < c2bytes; x++)
		a8.poke(dst + c1bytes + x, c2);
	// Continue after this line's drawing loop, which is not re-implemented
	a8.cpu.pc = 0x5823;
	return a8.OP_NOP;
}

/* ------------------------------ OpenGL pass ------------------------------ */

function drawGlLine(line, type) {
	let sx = adjustX(line.startX), ex = adjustX(line.endX);
	let sy = adjustY(line.startY), ey = adjustY(line.endY);

	if (type === TYPE_LINE) {
		gl.LineWidth(4);
		gl.Begin(gl.LINES);
		gl.Vertex3f(sx, sy, -2);
		gl.Vertex3f(ex, ey, -2);
		gl.End();
		return;
	}

	// A polygon covering the line's pixels exactly, drawn left to right
	if (sx > ex) {
		[sx, ex] = [ex, sx];
		[sy, ey] = [ey, sy];
	}
	const down = ey < sy;

	gl.LineWidth(1);
	gl.Disable(gl.CULL_FACE);
	gl.PolygonMode(gl.FRONT_AND_BACK, type === TYPE_POLYGON_LINE ? gl.LINE : gl.FILL);

	gl.Begin(gl.POLYGON);
	// Start (left) pixel: top-right corner first when going down
	if (down)
		gl.Vertex3f(sx + halfPixelX, sy + halfPixelY, -2);
	gl.Vertex3f(sx - halfPixelX, sy + halfPixelY, -2);   // top-left
	gl.Vertex3f(sx - halfPixelX, sy - halfPixelY, -2);   // bottom-left
	if (!down)
		gl.Vertex3f(sx + halfPixelX, sy - halfPixelY, -2);   // bottom-right when going up
	// End (right) pixel: bottom-left corner first when going down
	if (down)
		gl.Vertex3f(ex - halfPixelX, ey - halfPixelY, -2);
	gl.Vertex3f(ex + halfPixelX, ey - halfPixelY, -2);   // bottom-right
	gl.Vertex3f(ex + halfPixelX, ey + halfPixelY, -2);   // top-right
	if (!down)
		gl.Vertex3f(ex - halfPixelX, ey + halfPixelY, -2);   // top-left when going up
	gl.End();
}

/* ------------------------------ the extension ------------------------------ */

a8.register({
	name: "MERCENARY JS HACK by ERU",

	fingerprint: { address: 0x4000, bytes: [0xA6, 0x65, 0xBC, 0x57, 0x6B] },

	menu: {
		FPS: { label: "Display FPS:", options: ["OFF", "ON"], current: 1 },
		ACCEL: { label: "Accelerate:", options: ["OFF", "ON"], current: 1 },
		LINES: { label: "Line drawing mode:", options: ["Atari native", "OpenGL", "Both"], current: MODE_GL },
		GLTYPE: { label: "GL line type:", options: ["Line", "Polygon-Line", "Polygon-Fill"], current: TYPE_LINE },
	},

	// The C version was consulted on every instruction; listing the
	// addresses keeps the script out of the CPU loop everywhere else
	codeInjections: [
		...SKIPPED_ROUTINES,
		...LINE_ROUTINES.map((r) => r[0]),
		FILL_ONE_COLOUR,
		FILL_TWO_COLOURS,
	],

	onActivate() {
		shownLines = [];
		preparedLines = [];
		shownDl = -1;
	},

	onCodeInjection(pc, op) {
		if (this.menu.ACCEL.current === 0 || a8.accelerationDisabled())
			return op;
		if (SKIPPED_ROUTINES.includes(pc))
			return a8.fakeCpuUntilAfterOp(a8.OP_RTS);
		const routine = LINE_ROUTINES.find((r) => r[0] === pc);
		if (routine)
			return drawLine(routine, this.menu.LINES.current !== MODE_GL);
		if (pc === FILL_ONE_COLOUR)
			return fillOneColour();
		if (pc === FILL_TWO_COLOURS)
			return fillTwoColours();
		return op;
	},

	onPreGlFrame() {
		if (this.menu.FPS.current === 1)
			a8.printFps(mem[0x2805], 0x9f, 0x90, 0, -1);
	},

	onPostGlFrame() {
		// A change of the display list byte is a new frame: swap the line sets
		const dl = mem[0x2805];
		if (dl !== shownDl) {
			[shownLines, preparedLines] = [preparedLines, shownLines];
			preparedLines.length = 0;
			shownDl = dl;
		}

		if (a8.accelerationDisabled())
			return;
		const mode = this.menu.LINES.current;
		if (mode === MODE_A8)
			return;
		const type = this.menu.GLTYPE.current;

		gl.PushAttrib(gl.ENABLE_BIT);
		gl.PushAttrib(gl.POLYGON_BIT);
		gl.PushAttrib(gl.LINE_BIT);
		gl.Disable(gl.TEXTURE_2D);
		gl.Disable(gl.BLEND);

		for (const line of shownLines) {
			// The current drawing colours seem to live in $A1 (AND lines) and $A4 (OR lines)
			const [r, g, b] = rgb(line.colourAnd ? mem[0xA1] : mem[0xA4]);
			gl.Color4f(r, g, b, 1);
			drawGlLine(line, type);
		}

		gl.PopAttrib();
		gl.PopAttrib();
		gl.PopAttrib();
		gl.Color4f(1, 1, 1, 1);
	},
});
