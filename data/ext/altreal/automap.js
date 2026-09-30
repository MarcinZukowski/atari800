// Alternate Reality: The Dungeon - an automatic map. The cells the player
// visits and the ones seen down the corridor ahead are remembered; the M key
// shows the current level's map over the screen: the visited cells coloured
// by their kind (the map's type byte), walls, doors and arches of the known
// cells from the level data at $B000, the player, and marks set with the
// digit keys. The kinds are named from the game's own location line ("You
// are in a corridor.") the first time a cell of that kind is entered, and
// the text is drawn with the game's own font.
//
// The record is a file per character in data/ext/altreal/maps/, written
// when it changes: the saved states of this game are 64 KB machines without
// extended memory to keep it in, and a file survives states, restarts and
// the emulator's save and load alike. (The a8.xeBank() facility exists for
// games where a bank is the better place.)
//
// Record layout: 0-5 "ARMAP1"; 1024 * level (levels 1-7) a byte per cell
// (bit 7 visited, bit 6 seen, bits 0-2 mark 0-7) at y * 32 + x; 8192 + 32 *
// kind the kind's name, 32 bytes, zero padded.

import * as std from "std";
import * as os from "os";

const MAP = 0xB000, CELL_X = 0x6313, CELL_Y = 0x6314, LEVEL = 0x6315, FACING = 0x6312;
const SCREEN_STATE = 0x7600;               // 0 maze, 1 encounter: the map is kept up in these
const SECRET_DOORS_SHOWN = 0x1957;
const LOCATION_ROW = 0x04A0;               // the text row with "You are in a ..."
const KEY_DISPATCH = 0x30AA;               // the main loop has just fetched a command letter into A
const MAGIC = "ARMAP1", LEVEL_SIZE = 1024, MAX_LEVEL = 7, NAMES = 8192, NAME_SIZE = 32, KINDS = 64;
const VISITED = 0x80, SEEN = 0x40, MARK = 0x07;
const SEE_RANGE = 10;                      // cells seen down a corridor
const NAME_DELAY = 12;                     // frames after entering a cell before its line is read
const TEXT_FONT = 0x1400;                  // the game's text font: 128 glyphs of 8 bytes in ASCII order (the OS ROM is off)

const mem = a8.mem;

/* ------------------------------ the record ------------------------------ */

const NAME = 0x6321, NAME_LEN = 16;        // the character's name in the stats block
const MAPS_DIR = "data/ext/altreal/maps", RECORD_SIZE = 16384;
const SAVE_QUIET = 120, SAVE_LATEST = 600; // frames: save after a pause in the changes, or at least this often

let store = null, storeName = null, dirty = false, quiet = 0, dirtyFor = 0;

function characterName() {
	let t = "";
	for (let i = 0; i < NAME_LEN; i++) {
		const c = mem[NAME + i] & 0x7F;
		if (c < 32 || c > 126) break;
		t += String.fromCharCode(c);
	}
	t = t.trim().replace(/[^A-Za-z0-9_-]/g, "_");
	return t || "unnamed";
}

// The current character's record, loaded from its file on first use and
// whenever the character changes (another state loaded)
function record() {
	const name = characterName();
	if (store !== null && name === storeName) return store;
	if (store !== null && dirty) save();
	storeName = name; store = new Uint8Array(RECORD_SIZE); dirty = false;
	const f = std.open(`${MAPS_DIR}/${name}.map`, "rb");
	if (f !== null) { f.read(store.buffer, 0, RECORD_SIZE); f.close(); }
	if (String.fromCharCode(...store.subarray(0, MAGIC.length)) !== MAGIC) {
		store.fill(0);
		for (let i = 0; i < MAGIC.length; i++) store[i] = MAGIC.charCodeAt(i);
	}
	let known = 0;
	for (let i = LEVEL_SIZE; i < NAMES; i++) if (store[i] & (VISITED | SEEN)) known++;
	console.log(`altreal: map of ${name}: ${f !== null ? known + " cells known" : "new"} (${MAPS_DIR}/${name}.map)`);
	return store;
}

function save() {
	os.mkdir(MAPS_DIR);   // no harm when it exists
	const f = std.open(`${MAPS_DIR}/${storeName}.map`, "wb");
	if (f === null) console.log(`altreal: cannot write ${MAPS_DIR}/${storeName}.map`);
	else { f.write(store.buffer, 0, RECORD_SIZE); f.close(); }
	dirty = false; quiet = 0; dirtyFor = 0;
}

// Writes a byte of the record, noting the change
function put(i, v) {
	const s = record();
	if (s[i] === v) return;
	s[i] = v; dirty = true; quiet = 0;
}

const cellIndex = (level, x, y) => level >= 1 && level <= MAX_LEVEL ? level * LEVEL_SIZE + y * 32 + x : -1;

function kindName(kind) {
	const s = record(), o = NAMES + kind * NAME_SIZE;
	let t = "";
	for (let i = 0; i < NAME_SIZE && s[o + i]; i++) t += String.fromCharCode(s[o + i]);
	return t;
}

function setKindName(kind, name) {
	const o = NAMES + kind * NAME_SIZE;
	for (let i = 0; i < NAME_SIZE; i++) put(o + i, i < name.length ? name.charCodeAt(i) & 0x7F : 0);
}

/* ------------------------------ the level data ------------------------------ */

function cellWalls(x, y) {
	if (x < 0 || x > 31 || y < 0 || y > 31) return [13, 13, 13, 13];
	const a = MAP + y * 128 + x * 4;
	return [mem[a] & 0xF, mem[a] >> 4, mem[a + 1] & 0xF, mem[a + 1] >> 4];   // N, E, S, W
}
const cellKind = (x, y) => mem[MAP + y * 128 + x * 4 + 2];
const cellSpecial = (x, y) => (mem[MAP + y * 128 + x * 4 + 3] & 0x80) !== 0;
const DX = [0, 1, 0, -1], DY = [-1, 0, 1, 0];   // by facing
const seeThrough = (n) => n === 0 || n === 1 || n === 2;   // open, or an arch

/* ------------------------------ tracking ------------------------------ */

let lastCell = -1, nameCountdown = 0, nameKind = -1;

// Call every frame: marks the player's cell visited, the corridor ahead
// seen, and reads the location line for a kind not yet named
function track() {
	if (mem[SCREEN_STATE] > 1) return;
	const s = record(), level = mem[LEVEL], x = mem[CELL_X], y = mem[CELL_Y], f = mem[FACING] & 3;
	const i = cellIndex(level, x, y);
	if (i < 0) return;
	put(i, s[i] | VISITED);
	let cx = x, cy = y;
	for (let n = 0; n < SEE_RANGE; n++) {
		if (!seeThrough(cellWalls(cx, cy)[f])) break;
		cx += DX[f]; cy += DY[f];
		if (cx < 0 || cx > 31 || cy < 0 || cy > 31) break;
		const j = cellIndex(level, cx, cy);
		put(j, s[j] | SEEN);
	}
	if (dirty && (++quiet >= SAVE_QUIET || ++dirtyFor >= SAVE_LATEST)) save();
	if (i !== lastCell) {
		lastCell = i;
		nameKind = cellKind(x, y) & (KINDS - 1);
		nameCountdown = kindName(nameKind) ? 0 : NAME_DELAY;
	}
	if (nameCountdown > 0 && --nameCountdown === 0) {
		let line = "";
		for (let k = 0; k < 40; k++) { const c = mem[LOCATION_ROW + k] & 0x7F; line += c >= 32 && c < 127 ? String.fromCharCode(c) : " "; }
		line = line.trim().replace(/\.$/, "");
		if (line.startsWith("You are ")) setKindName(nameKind, line.slice(8));
	}
}

/* ------------------------------ drawing ------------------------------ */

let fontTexture = null;

// The game's text font as a 128 x 64 texture, 16 glyphs a row; white on transparent
function font() {
	if (fontTexture !== null) return fontTexture;
	const t = gl.createTexture(128, 64), px = t.pixels;
	for (let g = 0; g < 128; g++)
		for (let row = 0; row < 8; row++) {
			const b = mem[TEXT_FONT + g * 8 + row];
			for (let bit = 0; bit < 8; bit++) {
				const o = 4 * (((g >> 4) * 8 + row) * 128 + (g & 15) * 8 + bit), on = (b >> (7 - bit)) & 1;
				px[o] = px[o + 1] = px[o + 2] = 255; px[o + 3] = on ? 255 : 0;
			}
		}
	t.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
	fontTexture = t;
	return t;
}

function rect(x0, y0, x1, y1) {
	gl.Vertex3f(x0, y0, 0); gl.Vertex3f(x1, y0, 0); gl.Vertex3f(x1, y1, 0); gl.Vertex3f(x0, y1, 0);
}

// Text in pixels, glyphs of 8 * scale, in the game's font (ASCII order)
function text(str, x, y, scale, r, g, b) {
	gl.Enable(gl.TEXTURE_2D); gl.Enable(gl.BLEND);
	gl.BindTexture(gl.TEXTURE_2D, font().id);
	gl.Color4f(r, g, b, 1);
	gl.Begin(gl.QUADS);
	for (let i = 0; i < str.length; i++) {
		const glyph = str.charCodeAt(i) & 0x7F;
		const u0 = (glyph & 15) / 16, v0 = (glyph >> 4) / 8, u1 = u0 + 1 / 16, v1 = v0 + 1 / 8;
		const px = x + i * 8 * scale;
		gl.TexCoord2f(u0, v0); gl.Vertex3f(px, y, 0);
		gl.TexCoord2f(u1, v0); gl.Vertex3f(px + 8 * scale, y, 0);
		gl.TexCoord2f(u1, v1); gl.Vertex3f(px + 8 * scale, y + 8 * scale, 0);
		gl.TexCoord2f(u0, v1); gl.Vertex3f(px, y + 8 * scale, 0);
	}
	gl.End();
	gl.Disable(gl.TEXTURE_2D);
}

// A colour per kind: hues spread around the wheel
function kindColour(kind, v) {
	const h = ((kind * 47) % 360) / 60, sat = 0.55, c = v * sat, x = c * (1 - Math.abs(h % 2 - 1)), m = v - c;
	const [r, g, b] = [[c, x, 0], [x, c, 0], [0, c, x], [0, x, c], [x, 0, c], [c, 0, x]][Math.floor(h) % 6];
	return [r + m, g + m, b + m];
}
const MARK_COLOURS = [null, [1, 0.3, 0.3], [1, 0.6, 0.2], [1, 1, 0.3], [0.4, 1, 0.4], [0.3, 1, 1], [0.5, 0.6, 1], [1, 0.5, 1]];

// The side of a cell as a strip of the given colour by wall type; null = nothing
function wallColour(n) {
	if (n === 0) return null;
	if (n === 1 || n === 2) return [0.4, 0.8, 1];                       // arch
	if (n === 3 || n === 4) return [1, 0.85, 0.3];                      // door
	if (n >= 8 && n <= 10) return [1, 0.4, 0.4];                        // locked door
	if ((n === 5 || n === 6) && (mem[SECRET_DOORS_SHOWN] & 0x80)) return [1, 0.85, 0.3];   // secret door, revealed
	return [0.9, 0.9, 0.9];                                             // wall
}

// Draws the map of the current level over the whole window
function draw() {
	const s = record(), level = mem[LEVEL];
	const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
	gl.PushAttrib(gl.ENABLE_BIT); gl.PushAttrib(gl.CURRENT_BIT);
	gl.MatrixMode(gl.PROJECTION); gl.PushMatrix(); gl.LoadIdentity();
	gl.Translatef(-1, 1, 0); gl.Scalef(2 / vw, -2 / vh, 1);           // pixels, y down
	gl.MatrixMode(gl.MODELVIEW); gl.PushMatrix(); gl.LoadIdentity();
	gl.Disable(gl.DEPTH_TEST); gl.Disable(gl.FOG); gl.Disable(gl.TEXTURE_2D);
	gl.Enable(gl.BLEND); gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

	// the shade over the screen
	gl.Color4f(0, 0, 0, 0.8);
	gl.Begin(gl.QUADS); rect(0, 0, vw, vh); gl.End();

	// the grid: square cells, the map at the left, text to its right
	const ts = Math.max(1, Math.floor(vh / 240)), margin = 8 * ts;
	const cs = Math.floor(Math.min(vh - 2 * margin, vw * 0.62) / 32);
	const mx = margin, my = Math.floor((vh - 32 * cs) / 2), w = Math.max(1, Math.floor(cs / 6));
	const known = [];
	for (let y = 0; y < 32; y++)
		for (let x = 0; x < 32; x++) {
			const i = cellIndex(level, x, y), b = i < 0 ? 0 : s[i];
			if (!(b & (VISITED | SEEN))) continue;
			const kind = cellKind(x, y) & (KINDS - 1);
			if (!known.includes(kind)) known.push(kind);
			const [r, g, bl] = kindColour(kind, b & VISITED ? 0.75 : 0.35);
			const x0 = mx + x * cs, y0 = my + y * cs;
			gl.Color4f(r, g, bl, 1);
			gl.Begin(gl.QUADS); rect(x0, y0, x0 + cs, y0 + cs); gl.End();
			if (cellSpecial(x, y)) {
				gl.Color4f(1, 1, 1, 1);
				gl.Begin(gl.QUADS); rect(x0 + cs * 0.4, y0 + cs * 0.4, x0 + cs * 0.6, y0 + cs * 0.6); gl.End();
			}
			const walls = cellWalls(x, y), x1 = x0 + cs, y1 = y0 + cs;
			const sides = [[x0, y0, x1, y0 + w], [x1 - w, y0, x1, y1], [x0, y1 - w, x1, y1], [x0, y0, x0 + w, y1]];
			for (let k = 0; k < 4; k++) {
				const c = wallColour(walls[k]);
				if (c === null) continue;
				gl.Color4f(c[0], c[1], c[2], 1);
				gl.Begin(gl.QUADS); rect(...sides[k]); gl.End();
			}
			const mark = b & MARK;
			if (mark) {
				const c = MARK_COLOURS[mark];
				gl.Color4f(c[0], c[1], c[2], 1);
				gl.Begin(gl.QUADS); rect(x0 + w, y0 + w, x1 - w, y1 - w); gl.End();
				const fs = Math.max(1, Math.floor(cs / 10));
				text(String(mark), x0 + (cs - 8 * fs) / 2, y0 + (cs - 8 * fs) / 2, fs, 0, 0, 0);
				gl.Disable(gl.TEXTURE_2D);
			}
		}
	// the player: a triangle pointing the way it faces
	{
		const px = mx + mem[CELL_X] * cs + cs / 2, py = my + mem[CELL_Y] * cs + cs / 2, f = mem[FACING] & 3, r = cs * 0.38;
		const tip = [px + DX[f] * r, py + DY[f] * r], base = [px - DX[f] * r * 0.6, py - DY[f] * r * 0.6];
		const side = [DY[f] * r * 0.6, -DX[f] * r * 0.6];
		gl.Color4f(1, 1, 1, 1);
		gl.Begin(gl.TRIANGLES);
		gl.Vertex3f(tip[0], tip[1], 0); gl.Vertex3f(base[0] + side[0], base[1] + side[1], 0); gl.Vertex3f(base[0] - side[0], base[1] - side[1], 0);
		gl.End();
	}
	// the legend, in the room right of the map: a text size that fits 26 characters
	const lx = mx + 32 * cs + 2 * margin, lw = vw - lx - margin;
	const ls = Math.max(1, Math.floor(lw / (8 * 26))), lh = 10 * ls, chars = Math.floor(lw / (8 * ls)) - 2;
	let ly = my;
	text(`Level ${level}`, lx, ly, ls, 1, 1, 1); ly += lh * 1.5;
	known.sort((a, b) => a - b);
	for (const kind of known.slice(0, Math.max(0, Math.floor((vh - my - ly - 3 * lh) / lh)))) {
		const [r, g, b] = kindColour(kind, 0.75);
		gl.Color4f(r, g, b, 1);
		gl.Begin(gl.QUADS); rect(lx, ly, lx + 8 * ls, ly + 8 * ls); gl.End();
		text((kindName(kind) || `kind ${kind}`).slice(0, chars), lx + 12 * ls, ly, ls, 0.9, 0.9, 0.9);
		ly += lh;
	}
	ly = vh - my - 2 * lh;
	text("Marks: 1-7 set, 0 clear".slice(0, chars + 2), lx, ly, ls, 0.7, 0.7, 0.7);
	text("M closes the map", lx, ly + lh, ls, 0.7, 0.7, 0.7);

	gl.MatrixMode(gl.PROJECTION); gl.PopMatrix();
	gl.MatrixMode(gl.MODELVIEW); gl.PopMatrix();
	gl.PopAttrib(); gl.PopAttrib();
	gl.Color4f(1, 1, 1, 1);
}

/* ------------------------------ the interface ------------------------------ */

export const automap = {
	shown: false,
	hooks: [KEY_DISPATCH],

	// The main loop fetched a command letter into A: M toggles the map, a
	// digit marks the player's cell while it is shown; neither is a game command
	onCodeInjection(pc, op) {
		if (pc !== KEY_DISPATCH) return op;
		const key = a8.cpu.a;
		if (key === 0x6D) this.shown = !this.shown;
		else if (this.shown && key >= 0x30 && key <= 0x37) {
			const i = cellIndex(mem[LEVEL], mem[CELL_X], mem[CELL_Y]);
			if (i >= 0) put(i, (record()[i] & ~MARK) | (key - 0x30));
		}
		return op;
	},

	track,
	draw,
};
