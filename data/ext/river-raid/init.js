// River Raid: the river drawn in 3D with OpenGL, plus replacement sound
// effects. A port of the former C extension (src/ext/ext-river-raid.c).
// See river-raid.md for the reverse-engineering notes the addresses come from.
import { word } from "../common.js";

// Game data layout
const COLOR_LOS = 0xBB30;    // lo byte of each enemy colour table, indexed by colour id
const COLOR_DATA = 0xB700;   // enemy colours, one byte per line
const GFX_LOS = 0xBB40;      // lo byte of each enemy bitmap, indexed by gfx id (+0x10 = mirrored)
const GFX_DATA = 0xB900;     // enemy bitmaps, one byte per line
const HEIGHTS = 0xBB60;      // height of each enemy, indexed by colour id
const WIDTHS = 0x0521;       // on-screen width of each enemy slot: 0 normal, 1 double, 3 quadruple
const EXPLOSION_GFX = [0xB91C, 0xB934, 0xB94C];   // the three explosion frames

const LINE_COUNT = 160;      // river lines in memory: 80 from $2000 and 80 from $3000
const LINE_WIDTH = 48;       // bytes per line, 4 pixels per byte

const Z_NEAR = 200;
const Z_FAR = Z_NEAR + LINE_COUNT;
const Z_2D = -2.0;           // depth used for flat drawing
const Y_ADJUSTMENT = 0x5D;   // offset of the enemy Y positions
const Y_BLANKS = 20;         // empty lines at the top of the screen

const MODE_A8 = 0, MODE_GL_2D = 1, MODE_GL_3D = 2, MODE_PIP = 3;

// Object ids: 0 and 1 nothing, 2..6 explosions, 7..15 the nine real objects
const OBJECTS_OFFSET = 7;
const NUM_OBJECTS = 9;
const NUM_PLANE_TEXTURES = 10;
const ID_TO_EXPLOSION = [-1, -1, 2, 1, 0, 1, 2];

const mem = a8.mem;
const hex = (n, width = 2) => n.toString(16).padStart(width, "0");

/* ------------------------------ textures ------------------------------ */

// Builds an 8 pixel wide texture from a 1 bit per pixel bitmap in Atari
// memory. colourOrPtr below 256 is a colour, otherwise it points at one
// colour byte per line. Set bits become opaque pixels, clear bits stay
// transparent (createTexture() starts out all zero).
function genTexture(height, colourOrPtr, gfxPtr) {
	const width = 8;
	const t = gl.createTexture(width, height);
	const px = t.pixels;
	const pal = a8.palette;
	for (let y = 0; y < height; y++) {
		const idx = height - y - 1;   // textures are stored bottom-up
		const colour = colourOrPtr < 256 ? colourOrPtr : mem[colourOrPtr + idx];
		const c = pal[colour];
		const bits = mem[gfxPtr + idx];
		for (let x = 0; x < width; x++) {
			if (bits & (0x80 >> x)) {
				const o = 4 * (y * width + x);
				px[o] = c >> 16;      // stores into a Uint8Array wrap, so
				px[o + 1] = c >> 8;   // this keeps the low byte of each shift
				px[o + 2] = c;
				px[o + 3] = 255;
			}
		}
	}
	t.finalize();
	return t;
}

/* ------------------------------ objects ------------------------------ */

let objects = [];          // { normal, mirror, explosions[3] } per object id
let planeTextures = [];    // left / right / straight plane shapes
let missileTexture = null;

function genObject(id) {
	const height = 1 + mem[HEIGHTS + id];
	const colours = COLOR_DATA | mem[COLOR_LOS + (id % 16)];
	const gfxNormal = GFX_DATA | mem[GFX_LOS + id];
	const gfxMirror = GFX_DATA | mem[GFX_LOS + id + 0x10];
	console.log(`Constructing object #${hex(id)}: height ${hex(height)} colours ${hex(colours, 4)}`
		+ ` normal ${hex(gfxNormal, 4)} mirror ${hex(gfxMirror, 4)}`);
	return {
		normal: genTexture(height, colours, gfxNormal),
		mirror: genTexture(height, colours, gfxMirror),
		explosions: EXPLOSION_GFX.map((ptr) => genTexture(height, colours, ptr)),
	};
}

function initObjects() {
	objects = [];
	for (let i = 0; i < NUM_OBJECTS; i++)
		objects[i] = genObject(OBJECTS_OFFSET + i);

	// Plane shapes: bitmap addresses come from the table at $BACD, colour is player 1
	planeTextures = [];
	for (let i = 0; i < NUM_PLANE_TEXTURES; i++)
		planeTextures[i] = genTexture(14, a8.gtia.colpm1, word(0xBACD + 2 * i));

	// The missile is a single line of $20 at $A5B3
	missileTexture = genTexture(1, a8.gtia.colpm1, 0xA5B3);
}

function renderObjects(perspective) {
	let idx = mem[0x004D];   // first active enemy slot
	while (idx < 11) {
		let y = mem[0x000C + idx] - Y_ADJUSTMENT;
		const gfxId = mem[0x0500 + idx];
		const colourId = mem[0x050B + idx];
		const flags = mem[0x0042 + idx];
		const h = mem[HEIGHTS + colourId];
		const w = 2 * (8 + 8 * mem[WIDTHS + idx]);
		const x = mem[0x0516 + idx];

		// Slots with an unknown colour id show up while the game is not running
		if (gfxId >= 2 && colourId >= OBJECTS_OFFSET && colourId <= 15) {
			let o = objects[colourId - OBJECTS_OFFSET];

			let sx, sy, sw, sh, z;
			if (perspective) {
				sh = 0.25 * o.normal.height;
				sy = sh / 2;
				sx = 2 * (x - 128);
				sw = w;
				z = -(Z_FAR - y);
			}
			else {
				y += Y_BLANKS;
				sy = 120 - y;
				sh = o.normal.height;
				sw = w;
				sx = 2 * (x - 128);
				z = Z_2D;
			}

			let t;
			if (gfxId <= 6) {
				t = o.explosions[ID_TO_EXPLOSION[gfxId]];
			}
			else {
				o = objects[gfxId - OBJECTS_OFFSET];
				t = o ? ((flags & 8) ? o.mirror : o.normal) : null;
			}
			if (t)
				t.draw(0, 1, 0, 1, sx, sx + sw, sy, sy - sh, z);
		}

		idx++;
		if (y + h > LINE_COUNT)
			break;
	}
}

/* ------------------------------ river lines ------------------------------ */

const lines = [];        // one 192x1 texture per line, ordered as in Atari memory
let lastActive = false;
let lastLineNr = 0;

// Line index of a river line address, or -1 when the address is not in
// river memory (the display list points elsewhere while the game is not running)
function addrToLine(addr) {
	const line = addr < 0x3000
		? Math.floor((addr - 0x2000) / LINE_WIDTH)
		: 80 + Math.floor((addr - 0x3000) / LINE_WIDTH);
	return line >= 0 && line < LINE_COUNT ? line : -1;
}

function lineToAddr(line) {
	return line < 80 ? 0x2000 + line * LINE_WIDTH : 0x3000 + (line - 80) * LINE_WIDTH;
}

// Converts one Atari line (4 colours, 2 bits per pixel) to its texture
function renderLine(lineNr) {
	const addr = lineToAddr(lineNr);
	const t = lines[lineNr];
	const px = t.pixels;
	const pal = a8.palette;
	const colours = [pal[a8.gtia.colbk], pal[a8.gtia.colpf0], pal[a8.gtia.colpf1], pal[a8.gtia.colpf2]];
	for (let xb = 0; xb < LINE_WIDTH; xb++) {
		const byte = mem[addr + xb];
		for (let xp = 0; xp < 4; xp++) {
			const c = colours[(byte >> (2 * (3 - xp))) & 3];
			const o = 4 * (4 * xb + xp);
			px[o] = c >> 16;
			px[o + 1] = c >> 8;
			px[o + 2] = c;
			px[o + 3] = 0xff;
		}
	}
	t.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
}

function initLines() {
	for (let i = 0; i < LINE_COUNT; i++) {
		const t = gl.createTexture(LINE_WIDTH * 4, 1);
		t.finalize();
		lines[i] = t;
	}
	lastActive = false;
}

function renderLines(perspective) {
	const curLineNr = addrToLine(word(0x3F04));   // first line of the display list
	if (curLineNr < 0)
		return;   // title screen or the like: nothing to draw yet

	// $3EFF is zero while the game is not running; re-render everything
	// when it starts, otherwise only the lines that scrolled in
	const active = mem[0x3EFF] !== 0;
	if (active && !lastActive) {
		for (let i = 0; i < LINE_COUNT; i++)
			renderLine(i);
		lastLineNr = curLineNr;
	}
	else {
		while (lastLineNr !== curLineNr) {
			lastLineNr = (lastLineNr + LINE_COUNT - 1) % LINE_COUNT;
			renderLine(lastLineNr);
		}
	}
	lastActive = active;

	// A line is 48 bytes = 384 Atari pixels, of which the screen shows 336
	const margin = (1 - 0.875) / 2;

	for (let y = 0; y < LINE_COUNT; y++) {
		const t = lines[(curLineNr + y) % LINE_COUNT];
		if (perspective) {
			// 384 units wide around x=0, 2 units tall, receding in depth; the
			// texture repeats sideways to cover the wide frustum
			const sx = -192, sw = 384;
			t.draw(-168 / 384, 1 + 168 / 384, 0, 1,
				sx - 168, sx + sw + 168, 0, -2, -(Z_FAR - y));
		}
		else {
			const sy = 120 - (Y_BLANKS + y);
			t.draw(margin, 1 - margin, 0, 1, -168, 168, sy, sy + 1, Z_2D);
		}
	}
}

/* ------------------------------ plane and missile ------------------------------ */

function showPlaneAndMissile(perspective) {
	// Plane: $5E selects the shape (left / right / straight), $57 is its X
	const planeIdx = mem[0x5E];
	if (planeIdx < NUM_PLANE_TEXTURES) {
		let sh = 14, sx = 2 * (mem[0x57] - 128), sy, z;
		const sw = 2 * 8;
		if (perspective) {
			sh /= 4;
			sy = -25 - 1 - sh;
			z = -Z_NEAR - 24;
			sx = -4;
		}
		else {
			sy = 120 - 0xAA - 6;
			z = Z_2D;
		}
		planeTextures[planeIdx].draw(0, 1, 0, 1, sx, sx + sw, sy, sy + sh, z);
	}

	// Missile: $56 is its Y position, 0 or 1 when not flying
	const missileY = mem[0x56];
	if (missileY > 1) {
		let sh = 8, sx = 2 * (mem[0x57] - 128), sy, z;
		const sw = 2 * 8;
		if (perspective) {
			sh = 2;
			sy = -25 - sh / 2;
			sx = 0;
			z = -(Z_FAR - missileY);
		}
		else {
			sx += 4;
			sy = 120 - missileY;
			z = Z_2D;
		}
		missileTexture.draw(0, 1, 0, 1, sx, sx + sw, sy, sy + sh, z);
	}
}

/* ------------------------------ rendering ------------------------------ */

function renderSubwindow(x, y, w, h, perspective) {
	gl.Viewport(x, y, w, h);
	gl.Scissor(x, y, w, h);
	gl.Enable(gl.SCISSOR_TEST);
	gl.ClearColor(0, 0, 0.2, 0);
	gl.Clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
	gl.Disable(gl.SCISSOR_TEST);

	gl.MatrixMode(gl.PROJECTION);
	gl.LoadIdentity();
	if (perspective) {
		gl.Frustum(-168, 168, -25, 5, Z_NEAR, Z_FAR + 160);
		gl.MatrixMode(gl.MODELVIEW);
		gl.LoadIdentity();

		// Follow the plane sideways and look from slightly above the river
		const playerX = mem[0x0057] - 128;
		gl.Translatef(-2 * (playerX + 2), 0, 0);
		gl.Translatef(0, -25, 0);

		gl.Enable(gl.FOG);
		gl.Fogf(gl.FOG_MODE, gl.LINEAR);
		gl.Fogf(gl.FOG_START, Z_FAR - 50);
		gl.Fogf(gl.FOG_END, Z_FAR + 50);
		gl.Fogfv(gl.FOG_COLOR, [0, 0.1, 0.3, 1]);

		renderLines(true);
		renderObjects(true);

		gl.Disable(gl.FOG);
		gl.LoadIdentity();
		showPlaneAndMissile(true);
	}
	else {
		gl.Ortho(-168, 168, -120, 120, 0, 10);
		renderLines(false);
		renderObjects(false);
		showPlaneAndMissile(false);
	}
}

/* ------------------------------ sounds ------------------------------ */

let fireSound = null;
let explosionSounds = [];

function initSounds() {
	fireSound = a8.loadSound("data/ext/river-raid/Flash-laser-04.wav");
	// Silence the Atari fire sound: NOP out STA $D204 / STX $D205
	mem.fill(0xEA, 0xB3B0, 0xB3B0 + 6);

	explosionSounds = [
		"data/ext/river-raid/snd-boom1.wav",
		"data/ext/river-raid/snd-boom2.wav",
		"data/ext/river-raid/snd-expl1.wav",
		"data/ext/river-raid/snd-expl2.wav",
		"data/ext/river-raid/snd-expl3.wav",
	].map((file) => a8.loadSound(file));
	// Silence the Atari enemy explosion: NOP out STA $D202 / STX $D203
	mem.fill(0xEA, 0xB37E, 0xB37E + 6);
}

function doSounds() {
	// $7D is the fire volume, $0E marks the start of a shot
	if (mem[0x007D] === 0x0E)
		fireSound.play();
	// $7C is the enemy crash phase, it starts at $16
	if (mem[0x007C] === 0x16)
		explosionSounds[Math.floor(Math.random() * explosionSounds.length)].play();
}

/* ------------------------------ the extension ------------------------------ */

export default {
	name: "River Raid JS HACK by Eru",

	fingerprint: { address: 0xB55C, bytes: [0xA4, 0x4D, 0xA2, 0x5D, 0xD0, 0x03] },

	menu: {
		MODE: {
			label: "Screen drawing mode:",
			options: ["Atari native", "OpenGL - 2D", "OpenGL - 3D", "Picture-in-picture"],
			current: MODE_GL_3D,
		},
	},

	// Called once the fingerprint matches, with the game loaded in memory
	onActivate() {
		console.log("RIVER RAID detected");
		initLines();
		initObjects();
		initSounds();
	},

	onPostGlFrame() {
		const mode = this.menu.MODE.current;
		if (mode !== MODE_A8) {
			gl.Disable(gl.DEPTH_TEST);
			gl.Color4f(1, 1, 1, 1);
			gl.Enable(gl.BLEND);
			gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

			const oldViewport = gl.GetIntegerv(gl.VIEWPORT);
			gl.MatrixMode(gl.MODELVIEW);
			gl.PushMatrix();
			gl.MatrixMode(gl.PROJECTION);
			gl.PushMatrix();

			if (mode === MODE_PIP) {
				// Two small windows in the bottom corners: flat and perspective
				renderSubwindow(0, 0, 336, 240, false);
				renderSubwindow(336 * 2, 0, 336, 240, true);
			}
			else {
				// Over the main window, flat or in perspective
				renderSubwindow(0, 180, 1008, 720, mode === MODE_GL_3D);
			}

			gl.Viewport(...oldViewport);
			gl.Color4f(1, 1, 1, 1);
			gl.Disable(gl.BLEND);
			gl.MatrixMode(gl.PROJECTION);
			gl.PopMatrix();
			gl.MatrixMode(gl.MODELVIEW);
			gl.PopMatrix();
		}

		// The Atari sounds were patched out on activation, so play ours in
		// every mode (the C version only did so outside "Atari native")
		doSounds();
	},
};
