// Alternate Reality: The Dungeon - the maze drawn with OpenGL instead of the
// game's character-mode picture: the game's own wall art as textures on
// walls, doors and arches, floor and ceiling in the game's colours, fog and
// side shading, with steps and turns interpolated between the game's discrete
// positions.
//
// Monsters, the game's player/missile sprites, are drawn over the view.
//
// altreal.md describes the memory this reads. In short: the level is 32 x 32
// cells of 4 bytes at $B000 + y*128 + x*4, two bytes of wall nibbles (north,
// east, south, west) then a type and a flags byte; a cell is 36 units on a
// side; the player is at cell ($6313, $6314), position ($6316, $6317) inside
// it, facing $6312 (0 north, 1 east, 2 south, 3 west). The wall art is found
// through the table at $96F1/$9701 and drawn in the picture's colours at
// $18BA-$18BE. The game's picture occupies the middle 18 text columns of nine
// mode-4 rows: 144 colour clocks by 72 lines, a 2:1 window, and this view
// reproduces its projection (see below) so that both look alike.

const MAP = 0xB000, CELL = 36, EYE_HEIGHT = 18;
const VIEW_RANGE = 10;                 // cells drawn around the player: the table ends there
const TURN_FRAMES = 12;                // frames a turn takes
const isLocked = (n) => n >= 8 && n <= 10;

// The wall art: 16 pointers indexed by the wall nibble (0 = no wall), each to
// a 72-byte header followed by a 72 x 72 picture at 2 bits per pixel, 18
// bytes a row, then 36 x 36 and 18 x 18 copies of it for farther walls
const ART_TABLE_LO = 0x96F1, ART_TABLE_HI = 0x9701;
const ART_HEADER = 72, ART_SIZE = 72, ART_STRIDE = 18;
const ART_BYTES = ART_SIZE * ART_STRIDE;
const SECRET_DOORS_SHOWN = 0x1957;     // bit 7: wall types 5 and 6 are drawn with the art of 3 and 4
// The picture's colours (Atari colour numbers): pixel values 1-3 are
// playfields 0-2, value 0 is the background: the ceiling colour in the upper
// half of the picture and the floor colour in the lower half
const COLOUR_PF0 = 0x18BA, COLOUR_PF1 = 0x18BB, COLOUR_PF2 = 0x18BC;
const COLOUR_CEILING = 0x18BD, COLOUR_FLOOR = 0x18BE;

// The game's screen, in pixels of the displayed area (336 x 240, y down): the
// picture, the text rows above it (name, stats, experience, the message),
// the compass beside it, the inventory and the messages below
const SCREEN_W = 336, SCREEN_H = 240;
const PICTURE = [8 + 11 * 8, 73, 8 + 29 * 8, 145];   // x0, y0, x1, y1
const TOP_TEXT = [8, 20, 328, 72];
const COMPASS = [256, 82, 312, 138];         // in the cells right of the picture
const LEFT_EMBLEM = [24, 80, 88, 140];        // in the cells left of it (an item's or a clock's emblem, at times)
const TEXT_X0 = 8, TEXT_X1 = 328;
// The wide layout: the view over the full width, still 2:1, in the middle;
// the texts and the compass shrunk into the bands above and below. The text
// below the picture varies (inventory, messages, an encounter's menu down to
// line 235): the rows in use are taken from the display list, shrunk by a
// fixed factor, and the bottom band grows upward over the view when they
// need more than its 36 lines.
const WIDE_Y0 = (SCREEN_H - SCREEN_W / 2) / 2, WIDE_Y1 = SCREEN_H - WIDE_Y0;   // 36..204
const TEXT_SCALE = WIDE_Y0 / (TOP_TEXT[3] - TOP_TEXT[1]);   // the top block fills its band
const WIDE_VIEW = [0, WIDE_Y0, SCREEN_W, WIDE_Y1];
// Screen pixels to GL coordinates
const gx = (px) => px / (SCREEN_W / 2) - 1, gy = (py) => 1 - py / (SCREEN_H / 2);
const Z_2D = -2;   // where the emulator draws its own screen

// Monsters are player/missile graphics: four players with DMA from the
// P/M area, one colour clock per pixel, a pair of players overlapping for
// three colours (GTIA multicolour). The picture's first scanline is 81 and
// its left edge is colour clock 92, so a player at HPOS h starts at picture
// column h - 92. The game's interrupts zero the player colours below the
// picture, so the registers are captured at the picture's first interrupt.
const PICTURE_FIRST_SCANLINE = 81, PICTURE_FIRST_CLOCK = 92;
const PLAYER_DLI = 0x1B56;             // where to capture (init.js hooks it)
// The game's projection, reproduced so that the view matches its picture: the
// picture is 72 x 72 pixels (shown 2:1), a wall's half-height in lines is
// 35 minus a "depth" that the table at $8D72 gives at each cell boundary
// ahead, linear in between (see altreal.md), and a frontal wall is as many
// pixels wide as it is tall. There is no division by the distance: a wall
// right in front is 70 lines, one cell away 34, two cells 22.
const PIC = 72, PIC_CENTRE = 36, MAX_HALF_HEIGHT = 35;
const DEPTH_AT_CELL = [0, 18, 24, 27, 29, 30, 31, 32, 33, 34, 35];

const mem = a8.mem;

/* ------------------------------ wall art ------------------------------ */

// The art as it was when the game last drew the maze. An encounter loads its
// code over the table and the pictures ($9500-$98FF) while the maze stays on
// the screen, so they are copied at every redraw (init.js calls snapshotArt()
// at $7856, the copy of the picture into the fonts) and used from the copy.
const art = { table: new Uint16Array(16), pictures: new Map(), version: 0, sum: 0 };

function snapshotArt(src = mem) {
	const table = new Uint16Array(16), ptrs = [];
	let sum = 0;
	for (let i = 0; i < 16; i++) {
		const p = src[ART_TABLE_LO + i] | src[ART_TABLE_HI + i] << 8;
		table[i] = p;
		if (p === 0 || ptrs.includes(p)) continue;
		ptrs.push(p);
		for (let a = p + ART_HEADER, end = a + ART_BYTES; a < end; a++)
			sum = (sum * 31 + src[a]) | 0;
	}
	if (art.version && sum === art.sum && table.every((p, i) => p === art.table[i]))
		return;
	art.table = table; art.sum = sum; art.version++;
	art.pictures = new Map(ptrs.map((p) => [p, src.slice(p + ART_HEADER, p + ART_HEADER + ART_BYTES)]));
}

// The art for a wall nibble, as the game chooses it ($7917-$793C): the table
// entry, with types 5 and 6 (secret doors) shown as 3 and 4 when $1957 says so
function artPointer(nibble) {
	let i = nibble;
	if ((mem[SECRET_DOORS_SHOWN] & 0x80) && (i === 5 || i === 6))
		i -= 2;
	return art.table[i];
}

// The pixel values of a 72 x 72 picture (its 1296 bytes), one byte each
function artPixels(bytes) {
	const out = new Uint8Array(ART_SIZE * ART_SIZE);
	let o = 0;
	for (let i = 0; i < ART_BYTES; i++) {
		const b = bytes[i];
		for (let shift = 6; shift >= 0; shift -= 2)
			out[o++] = (b >> shift) & 3;
	}
	return out;
}

// Scale2x (EPX): doubles a picture of pixel values, rounding the stairs of
// diagonal edges from the four neighbours without inventing colours. The
// picture is taken to repeat, as it does on the walls.
function scale2x(src, w, h) {
	const out = new Uint8Array(w * h * 4), w2 = 2 * w;
	for (let y = 0; y < h; y++) {
		const up = ((y + h - 1) % h) * w, row = y * w, down = ((y + 1) % h) * w;
		for (let x = 0; x < w; x++) {
			const P = src[row + x], A = src[up + x], D = src[down + x];
			const C = src[row + (x + w - 1) % w], B = src[row + (x + 1) % w];   // left, right
			const o = 2 * y * w2 + 2 * x;
			out[o] = (C === A && C !== D && A !== B) ? A : P;
			out[o + 1] = (A === B && A !== C && B !== D) ? B : P;
			out[o + w2] = (D === C && D !== B && C !== A) ? C : P;
			out[o + w2 + 1] = (B === D && B !== A && D !== C) ? D : P;
		}
	}
	return out;
}

// A texture from a 72 x 72 picture's bytes, with colours[v] (0x00RRGGBB) for
// pixel value v. Value 0, the background, is made transparent: the game
// shows the background colour through an arch, here what lies beyond shows.
// Smooth: Scale2x twice, 288 x 288, drawn with mipmaps and linear filtering;
// otherwise the art as is, with nearest sampling like the game's scaling.
function artTexture(bytes, colours, smooth) {
	let values = artPixels(bytes), size = ART_SIZE;
	if (smooth) {
		values = scale2x(values, size, size); size *= 2;
		values = scale2x(values, size, size); size *= 2;
	}
	const t = gl.createTexture(size, size);
	const px = t.pixels;
	let transparent = false;
	for (let i = 0, o = 0; i < values.length; i++, o += 4) {
		const v = values[i], c = colours[v];
		px[o] = c >> 16; px[o + 1] = (c >> 8) & 255; px[o + 2] = c & 255;
		px[o + 3] = v ? 255 : 0;
		if (v === 0) transparent = true;
	}
	if (smooth) {
		gl.BindTexture(gl.TEXTURE_2D, t.id);
		gl.TexParameteri(gl.TEXTURE_2D, gl.GENERATE_MIPMAP, gl.TRUE);   // built when finalize() uploads
	}
	t.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, smooth ? gl.LINEAR_MIPMAP_LINEAR : gl.NEAREST);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, smooth ? gl.LINEAR : gl.NEAREST);
	t.transparent = transparent;
	return t;
}

// The textures follow the art copy and the colours (the game flashes them):
// a key says when they must be rebuilt; the smoothing option is part of it
function artKey(smooth) {
	return `${smooth ? "s" : "o"}|${mem[COLOUR_PF0]},${mem[COLOUR_PF1]},${mem[COLOUR_PF2]},${mem[COLOUR_CEILING]}|${art.version}`;
}

// Textures for all the pictures of the art copy, keyed by pointer
function artTextures(smooth) {
	const colours = [mem[COLOUR_CEILING], mem[COLOUR_PF0], mem[COLOUR_PF1], mem[COLOUR_PF2]].map((c) => a8.palette[c]);
	return new Map([...art.pictures].map(([p, bytes]) => [p, artTexture(bytes, colours, smooth)]));
}

/* ------------------------------ floor and ceiling ------------------------------ */

// The game has no floor or ceiling art, only their colours: these are grey
// patterns that the game's colours tint (the texture environment modulates)

// Deterministic noise so that textures are stable between runs
function noise(x, y, seed) {
	let h = (x * 374761393 + y * 668265263 + seed * 2246822519) >>> 0;
	h = ((h ^ (h >>> 13)) * 1274126177) >>> 0;
	return ((h ^ (h >>> 16)) & 0xffff) / 0xffff;
}

// Builds a size x size grey texture from f(x, y) -> brightness in 0..1
function makeTexture(size, f) {
	const t = gl.createTexture(size, size);
	const px = t.pixels;
	for (let y = 0; y < size; y++)
		for (let x = 0; x < size; x++) {
			const v = f(x, y) * 255, o = 4 * (y * size + x);
			px[o] = v; px[o + 1] = v; px[o + 2] = v; px[o + 3] = 255;
		}
	t.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
	return t;
}

function makePlanes() {
	const S = 64;
	// Floor and ceiling: rough stone, without lines that would show how the
	// projection bends at the cell boundaries
	const floor = makeTexture(S, (x, y) => 0.85 + 0.15 * noise(x >> 3, y >> 3, 3) - 0.08 * noise(x, y, 4));
	const ceiling = makeTexture(S, (x, y) => 0.8 + 0.2 * noise(x >> 2, y >> 2, 5) - 0.1 * noise(x, y, 6));
	return { floor, ceiling };
}

/* ------------------------------ the screen ------------------------------ */

// Is the maze picture on the screen? The game has several display lists;
// the maze's (also used by encounters, with the battle menu below) shows the
// picture as nine mode-4 rows from screen memory $04F0 with interrupt bits
// on rows 3, 4, 6 and 9 that switch the font banks. A shop's list shows
// nine mode-4 rows from $04F0 too, but without those interrupts (one font),
// and its picture is the shop's, not the maze's.
// The game's screen state at $7600 (0 in the maze, 1 in an encounter, 13 in a
// shop, 234 when dead) tells the rest apart: the death screen keeps the maze's
// display list but shows its own picture.
const PICTURE_SCREEN = 0x04F0, SCREEN_STATE = 0x7600;
function pictureDisplayed() {
	if (mem[SCREEN_STATE] > 1) return false;
	let a = a8.antic.dlist;
	for (let i = 0; i < 64; i++) {
		const ins = mem[a++], mode = ins & 0x0F;
		if (mode === 1) { if (ins & 0x40) return false; a = mem[a] | mem[a + 1] << 8; continue; }   // jump; JVB ends the list
		if (mode === 0) continue;
		if (ins & 0x40) {
			const lms = mem[a] | mem[a + 1] << 8;
			a += 2;
			if (mode === 4 && lms === PICTURE_SCREEN) {
				let interrupts = 0;
				for (let r = 0; r < 8; r++)
					if ((mem[a + r] & 0x8F) === 0x84) interrupts++;
				return interrupts >= 3;
			}
		}
	}
	return false;
}

// Do the font cells beside the picture show anything? The eleven columns on
// each side are the frame's colour (every byte $FF) unless the game draws
// there: the compass on the right, an emblem on the left in some situations
function sideCellsUsed(right) {
	const first = right ? 29 : 0;
	for (const font of [0x0800, 0x0C00, 0x1000])
		for (let row = 0; row < 3; row++)
			for (let c = first; c < first + 11; c++) {
				const a = font + (40 * row + c) * 8;
				for (let i = 0; i < 8; i++)
					if (mem[a + i] !== 0xFF) return true;
			}
	return false;
}

// The text rows below the picture that are in use: [first line, last line + 1]
// from the display list (mode-2 rows after the picture's rows, up to the
// last one with a character in it), or null
function bottomTextExtent() {
	const heights = [0, 0, 8, 10, 8, 16, 8, 16, 8, 4, 4, 2, 1, 2, 1, 1], widths = [0, 0, 40, 40, 40, 40, 20, 20, 10, 10, 20, 20, 20, 40, 40, 40];
	let a = a8.antic.dlist, lms = 0, line = 0, seenPicture = false, first = null, last = null;
	for (let i = 0; i < 100; i++) {
		const ins = mem[a++], mode = ins & 0x0F;
		if (mode === 1) { if (ins & 0x40) break; a = mem[a] | mem[a + 1] << 8; continue; }
		if (mode === 0) { line += ((ins >> 4) & 7) + 1; continue; }
		if (ins & 0x40) { lms = mem[a] | mem[a + 1] << 8; a += 2; }
		if (mode === 4 && lms === PICTURE_SCREEN) seenPicture = true;
		else if (seenPicture && mode === 2) {
			if (first === null) first = line;
			for (let x = 0; x < 40; x++)
				if ((mem[lms + x] & 0x7F) > 0x20) { last = line + 8; break; }
		}
		line += heights[mode]; lms += widths[mode];
	}
	return first === null || last === null ? null : [first, last];
}

/* ------------------------------ map access ------------------------------ */

function cellWalls(x, y) {
	if (x < 0 || x > 31 || y < 0 || y > 31)
		return [13, 13, 13, 13];
	const a = MAP + y * 128 + x * 4;
	const b0 = mem[a], b1 = mem[a + 1];
	return [b0 & 0xF, b0 >> 4, b1 & 0xF, b1 >> 4];   // N, E, S, W
}

/* ------------------------------ projection ------------------------------ */

// Half-height in picture lines of a wall at distance d along the view. Under
// the game's law a wall right in front is 70 of the 72 lines, and what is
// beside it shows at the edges (the game paints the picture one flat colour
// when you touch a wall); here the first two units are the full 36 and the
// first cell runs linearly from there to the table's value, so a wall you
// stand at fills the picture.
const TOUCH = 2, FULL_HALF_HEIGHT = PIC / 2;
function halfHeight(d) {
	const c = d / CELL;
	if (c >= VIEW_RANGE) return 0;
	if (d <= TOUCH) return FULL_HALF_HEIGHT + (TOUCH - d) * DEPTH_AT_CELL[1] / CELL;   // at and behind the eye: growing
	if (c < 1) return FULL_HALF_HEIGHT - (FULL_HALF_HEIGHT - (MAX_HALF_HEIGHT - DEPTH_AT_CELL[1])) * (d - TOUCH) / (CELL - TOUCH);
	const i = Math.floor(c), t = c - i;
	return MAX_HALF_HEIGHT - (DEPTH_AT_CELL[i] + (DEPTH_AT_CELL[i + 1] - DEPTH_AT_CELL[i]) * t);
}

// The eye: position and the sines of its yaw, set once per frame
const eye = { x: 0, z: 0, sin: 0, cos: 1 };

// A world point to picture coordinates: x, y in pixels (y down) and its
// distance along the view. The scale at a distance is the wall half-height
// over the half cell, the same across and up, as in the game.
function project(wx, wy, wz) {
	const dx = wx - eye.x, dz = wz - eye.z;
	const l = dx * eye.cos + dz * eye.sin;       // to the right
	const d = dx * eye.sin - dz * eye.cos;       // ahead
	const s = halfHeight(d) / (CELL / 2);
	return [PIC_CENTRE + l * s, PIC_CENTRE - (wy - EYE_HEIGHT) * s, d];
}

function vertex(u, v, wx, wy, wz) {
	const [x, y, d] = project(wx, wy, wz);
	gl.TexCoord2f(u, v);
	gl.Vertex3f(x, y, -d);   // the depth buffer and the fog see the distance
}

/* ------------------------------ drawing ------------------------------ */

// A wall from (x0, z0) to (x1, z1), left to right as seen, full cell height,
// the picture once along it with its first row at the top. Both ends keep
// their own height, and the texture runs linearly between them, which is
// how the game draws walls seen at an angle.
function wallQuad(x0, z0, x1, z1) {
	gl.Begin(gl.QUADS);
	vertex(0, 1, x0, 0, z0);
	vertex(1, 1, x1, 0, z1);
	vertex(1, 0, x1, CELL, z1);
	vertex(0, 0, x0, CELL, z0);
	gl.End();
}

// Floor or ceiling at height h over the cells around (cx, cy), one quad per
// cell so that the projection bends with the distance; the texture repeats
// once per cell
function plane(h, cx, cy) {
	gl.Begin(gl.QUADS);
	for (let y = cy - VIEW_RANGE; y <= cy + VIEW_RANGE; y++)
		for (let x = cx - VIEW_RANGE; x <= cx + VIEW_RANGE; x++) {
			const X0 = x * CELL, X1 = X0 + CELL, Z0 = y * CELL, Z1 = Z0 + CELL;
			// skip cells wholly behind the eye
			const dc = (X0 + CELL / 2 - eye.x) * eye.sin - (Z0 + CELL / 2 - eye.z) * eye.cos;
			if (dc < -CELL) continue;
			vertex(x, y, X0, h, Z0);
			vertex(x + 1, y, X1, h, Z0);
			vertex(x + 1, y + 1, X1, h, Z1);
			vertex(x, y + 1, X0, h, Z1);
		}
	gl.End();
}

function drawWalls(textures, cx, cy) {
	// Walls of every cell in range, far to near (a painter's order, which also
	// lets arches blend over what is behind them; the depth test helps too).
	// Each cell keeps its own four walls, so a wall between two cells exists
	// twice, possibly as different types: only the side facing the eye is
	// drawn, the one the game would draw, with the art's left column on the
	// left as seen from inside the cell.
	const items = [];
	for (let y = cy - VIEW_RANGE; y <= cy + VIEW_RANGE; y++)
		for (let x = cx - VIEW_RANGE; x <= cx + VIEW_RANGE; x++) {
			const w = cellWalls(x, y);
			if (!(w[0] | w[1] | w[2] | w[3])) continue;
			const X0 = x * CELL, X1 = X0 + CELL, Z0 = y * CELL, Z1 = Z0 + CELL;
			// each side: type, end points (left to right from inside), whether it
			// faces east/west, whether the eye is on its inner side
			const sides = [
				[w[0], X0, Z0, X1, Z0, 0, eye.z > Z0],   // north
				[w[1], X1, Z0, X1, Z1, 1, eye.x < X1],   // east
				[w[2], X1, Z1, X0, Z1, 0, eye.z < Z1],   // south
				[w[3], X0, Z1, X0, Z0, 1, eye.x > X0],   // west
			];
			for (const s of sides) {
				if (s[0] === 0 || !s[6]) continue;
				const tex = textures.get(artPointer(s[0]));
				if (tex === undefined) continue;   // the game draws nothing for it either
				// distance of the ends along the view; nothing to see when both are behind
				const d0 = (s[1] - eye.x) * eye.sin - (s[2] - eye.z) * eye.cos;
				const d1 = (s[3] - eye.x) * eye.sin - (s[4] - eye.z) * eye.cos;
				if (d0 < 0.5 && d1 < 0.5) continue;
				items.push({ side: s, tex, d: (d0 + d1) / 2 });
			}
		}
	items.sort((p, q) => q.d - p.d);
	let bound = null;
	for (const it of items) {
		const [type, x0, z0, x1, z1, ew] = it.side;
		if (it.tex !== bound) {
			bound = it.tex;
			gl.BindTexture(gl.TEXTURE_2D, bound.id);
			if (bound.transparent) gl.Enable(gl.BLEND); else gl.Disable(gl.BLEND);
		}
		// east/west facing walls a little darker than north/south ones
		const shade = ew ? 0.78 : 1.0;
		if (isLocked(type)) gl.Color4f(0.9 * shade, 0.55 * shade, 0.5 * shade, 1);
		else gl.Color4f(shade, shade, shade, 1);
		wallQuad(x0, z0, x1, z1);
	}
	gl.Disable(gl.BLEND);
}

/* ------------------------------ sprites ------------------------------ */

// The GTIA state of the players as it is during the picture
const sprites = { gractl: 0, prior: 0, hpos: [0, 0, 0, 0], size: [0, 0, 0, 0], colpm: [0, 0, 0, 0] };
let spriteTexture = null;

function captureSprites() {
	const g = a8.gtia;
	sprites.gractl = g.gractl; sprites.prior = g.prior;
	sprites.hpos = [g.hposp0, g.hposp1, g.hposp2, g.hposp3];
	sprites.size = [g.sizep0, g.sizep1, g.sizep2, g.sizep3];
	sprites.colpm = [g.colpm0, g.colpm1, g.colpm2, g.colpm3];
}

// Builds the overlay of the players over the picture from the P/M memory and
// the captured registers: GTIA priority, with the multicolour mode's OR of
// the colours of an overlapping pair. Smooth: Scale2x twice, like the art.
// Rebuilt only when the sprite or the registers change; returns false when
// the players are off or show nothing.
let spriteKey = "";
function buildSprites(smooth) {
	if (!(sprites.gractl & 2) || !(a8.antic.dmactl & 8)) return false;
	const single = (a8.antic.dmactl & 0x10) !== 0, base = a8.antic.pmbase << 8;
	const multi = (sprites.prior & 0x20) !== 0;
	// the shapes of the picture's lines, and a key over everything that matters
	const shapes = new Uint8Array(4 * PIC);
	let sum = 0;
	for (let y = 0; y < PIC; y++) {
		const line = PICTURE_FIRST_SCANLINE + y;
		for (let p = 0; p < 4; p++) {
			const b = single ? mem[base + 0x400 + p * 0x100 + line] : mem[base + 0x200 + p * 0x80 + (line >> 1)];
			shapes[p * PIC + y] = b;
			sum = (sum * 31 + b) | 0;
		}
	}
	const key = `${smooth ? "s" : "o"}|${sprites.hpos}|${sprites.size}|${sprites.colpm}|${sprites.prior}|${sum}`;
	if (key === spriteKey) return spriteTexture !== null;
	spriteKey = key;

	// colour indices per picture pixel: 0 transparent, else 1 + index into palette
	const widths = [1, 2, 1, 4], palette = [];
	let idx = new Uint8Array(PIC * PIC), any = false;
	for (let y = 0; y < PIC; y++)
		for (let x = 0; x < PIC; x++) {
			const clock = PICTURE_FIRST_CLOCK + x;
			let on = 0;
			for (let p = 0; p < 4; p++) {
				const w = widths[sprites.size[p] & 3], k = clock - sprites.hpos[p];
				if (k >= 0 && k < 8 * w && (shapes[p * PIC + y] >> (7 - (k / w | 0))) & 1) on |= 1 << p;
			}
			if (!on) continue;
			let colour;
			if (multi) {   // pairs 0-1 and 2-3, the first pair in front
				const pair = on & 3 ? 0 : 2, hi = on >> pair;
				colour = (hi & 1 ? sprites.colpm[pair] : 0) | (hi & 2 ? sprites.colpm[pair + 1] : 0);
			}
			else colour = sprites.colpm[on & 1 ? 0 : on & 2 ? 1 : on & 4 ? 2 : 3];
			let i = palette.indexOf(colour);
			if (i < 0) { i = palette.length; palette.push(colour); }
			idx[y * PIC + x] = i + 1;
			any = true;
		}
	if (!any) { spriteTexture = null; return false; }
	let size = PIC;
	if (smooth) {
		idx = scale2x(idx, size, size); size *= 2;
		idx = scale2x(idx, size, size); size *= 2;
	}
	if (spriteTexture === null || spriteTexture.width !== size)
		spriteTexture = gl.createTexture(size, size);
	const px = spriteTexture.pixels;
	px.fill(0);
	for (let i = 0, o = 0; i < idx.length; i++, o += 4) {
		if (!idx[i]) continue;
		const c = a8.palette[palette[idx[i] - 1]];
		px[o] = c >> 16; px[o + 1] = (c >> 8) & 255; px[o + 2] = c & 255; px[o + 3] = 255;
	}
	spriteTexture.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
	return true;
}

// Draws the overlay over the whole picture, in front of everything (the
// game gives the players priority over the playfield)
function drawSprites() {
	gl.Disable(gl.DEPTH_TEST); gl.Disable(gl.FOG); gl.Enable(gl.BLEND);
	gl.BindTexture(gl.TEXTURE_2D, spriteTexture.id);
	gl.Color4f(1, 1, 1, 1);
	gl.Begin(gl.QUADS);
	gl.TexCoord2f(0, 0); gl.Vertex3f(0, 0, -1);
	gl.TexCoord2f(1, 0); gl.Vertex3f(PIC, 0, -1);
	gl.TexCoord2f(1, 1); gl.Vertex3f(PIC, PIC, -1);
	gl.TexCoord2f(0, 1); gl.Vertex3f(0, PIC, -1);
	gl.End();
	gl.Disable(gl.BLEND);
}

// Draws a region of the game's screen with its top-left corner at (x, y),
// scaled; all in screen pixels
function drawScreenRegion(region, x, y, scale) {
	const [x0, y0, x1, y1] = region;
	gl.drawScreen(x0, y0, x1, y1, gx(x), gx(x + (x1 - x0) * scale), gy(y), gy(y + (y1 - y0) * scale), Z_2D);
}

/* ------------------------------ the view ------------------------------ */

export const SPRITE_HOOK = PLAYER_DLI;

export function createView3D() {
	let textures = null, key = null, planes = null;
	// Interpolation state; tracking is false until a frame has been drawn
	// from the current position, so the first frame after the view was off
	// starts where the player is instead of sweeping there
	let tracking = false;
	let prevX = 0, prevZ = 0, prevYaw = 0;
	let curX = 0, curZ = 0, curYaw = 0;
	let moveFrames = 0, turnFrames = 0, moveDuration = 1;

	return {
		// Settings the menu changes
		options: { smoothTextures: true, wide: false },

		// The wide layout, after render(): black bands, the game's texts and
		// compass shrunk into them, and, when the view was not drawn (a
		// monster, or the Atari view chosen), the game's own picture enlarged
		// into the view's place. Nothing when the game shows another screen.
		drawWideLayout(viewDrawn) {
			if (!pictureDisplayed())
				return;
			// the bottom band: the 36 lines, or more when the text in use needs them
			const bottom = bottomTextExtent();
			const bottomHeight = bottom ? (bottom[1] - bottom[0]) * TEXT_SCALE : 0;
			const bandTop = Math.min(WIDE_Y1, SCREEN_H - bottomHeight);
			gl.PushAttrib(gl.ENABLE_BIT); gl.PushAttrib(gl.CURRENT_BIT);
			gl.Disable(gl.DEPTH_TEST); gl.Disable(gl.BLEND); gl.Disable(gl.TEXTURE_2D);
			gl.Color4f(0, 0, 0, 1);
			for (const [y0, y1] of [[0, WIDE_Y0], [bandTop, SCREEN_H]]) {
				gl.Begin(gl.QUADS);
				gl.Vertex3f(-1, gy(y0), Z_2D); gl.Vertex3f(1, gy(y0), Z_2D);
				gl.Vertex3f(1, gy(y1), Z_2D); gl.Vertex3f(-1, gy(y1), Z_2D);
				gl.End();
			}
			gl.Enable(gl.TEXTURE_2D);
			gl.Color4f(1, 1, 1, 1);
			if (!viewDrawn)
				drawScreenRegion(PICTURE, WIDE_VIEW[0], WIDE_VIEW[1], (WIDE_Y1 - WIDE_Y0) / (PICTURE[3] - PICTURE[1]));
			// the texts, centred; the compass fits the top band's right end and the
			// left emblem its left end, each when the game shows one
			drawScreenRegion(TOP_TEXT, (SCREEN_W - (TOP_TEXT[2] - TOP_TEXT[0]) * TEXT_SCALE) / 2, 0, TEXT_SCALE);
			if (sideCellsUsed(true)) {
				const sc = (WIDE_Y0 - 2) / (COMPASS[3] - COMPASS[1]);
				drawScreenRegion(COMPASS, SCREEN_W - 2 - (COMPASS[2] - COMPASS[0]) * sc, 1, sc);
			}
			if (sideCellsUsed(false)) {
				const se = (WIDE_Y0 - 2) / (LEFT_EMBLEM[3] - LEFT_EMBLEM[1]);
				drawScreenRegion(LEFT_EMBLEM, 2, 1, se);
			}
			if (bottom)
				drawScreenRegion([TEXT_X0, bottom[0], TEXT_X1, bottom[1]], (SCREEN_W - (TEXT_X1 - TEXT_X0) * TEXT_SCALE) / 2, SCREEN_H - bottomHeight, TEXT_SCALE);
			gl.PopAttrib(); gl.PopAttrib();
		},

		// Call when a frame goes by without render(): the next one starts fresh
		reset() { tracking = false; },

		// Call from a code injection at PLAYER_DLI: records the players' registers
		captureSprites,
		// Call when the game has drawn the maze ($7856): copies the wall art
		snapshotArt,

		// Call every frame; draws when the game shows the maze. Monsters are the
		// game's own player/missile sprites, drawn over the view.
		render(movesPerSecond) {
			if (!pictureDisplayed() || art.version === 0) {   // no maze, or no art copied yet
				tracking = false;
				return false;
			}
			const k = artKey(this.options.smoothTextures);
			if (k !== key) { textures = artTextures(this.options.smoothTextures); key = k; }
			if (planes === null)
				planes = makePlanes();

			const cx = mem[0x6313], cy = mem[0x6314], facing = mem[0x6312];
			// The eye: the game's position along the facing, but centred across
			// it, as the game's own renderer ignores the position across (a turn
			// therefore slides the eye to the middle of the cell)
			const alongX = facing & 1, x = cx * CELL + (alongX ? mem[0x6316] + 0.5 : CELL / 2);
			const z = cy * CELL + (alongX ? CELL / 2 : mem[0x6317] + 0.5);
			// World: x east, z south (the map's y), y up; yaw 0 looks north and
			// turns clockwise with the facing
			const yaw = facing * 90;

			// Interpolate between the game's discrete positions and facings;
			// a jump of more than a cell (a new level, a teleport) is not walked
			if (!tracking || Math.abs(x - curX) > CELL || Math.abs(z - curZ) > CELL) {
				prevX = curX = x; prevZ = curZ = z; prevYaw = curYaw = yaw;
				moveFrames = turnFrames = 1000;
				tracking = true;
			}
			const turned = yaw !== curYaw;
			if (turned) { prevYaw = curYaw; curYaw = yaw; turnFrames = 0; }
			if (x !== curX || z !== curZ) {
				prevX = curX; prevZ = curZ; curX = x; curZ = z; moveFrames = 0;
				// a step takes the time between steps; the slide of a turn, the turn's
				moveDuration = turned ? TURN_FRAMES : Math.max(1, 60 / Math.max(1, movesPerSecond));
			}
			const moveT = Math.min(1, ++moveFrames / moveDuration);
			const turnT = Math.min(1, ++turnFrames / TURN_FRAMES);
			const ease = (t) => t * t * (3 - 2 * t);
			eye.x = prevX + (curX - prevX) * ease(moveT);
			eye.z = prevZ + (curZ - prevZ) * ease(moveT);
			let dyaw = curYaw - prevYaw;
			if (dyaw > 180) dyaw -= 360; else if (dyaw < -180) dyaw += 360;
			const eyeYaw = (prevYaw + dyaw * ease(turnT)) * Math.PI / 180;
			eye.sin = Math.sin(eyeYaw); eye.cos = Math.cos(eyeYaw);

			// Viewport: the game's picture rectangle, or the wide one
			const rect = this.options.wide ? WIDE_VIEW : PICTURE;
			const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
			const px0 = Math.round(vx + (gx(rect[0]) + 1) / 2 * vw), px1 = Math.round(vx + (gx(rect[2]) + 1) / 2 * vw);
			const py0 = Math.round(vy + (gy(rect[3]) + 1) / 2 * vh), py1 = Math.round(vy + (gy(rect[1]) + 1) / 2 * vh);
			gl.PushAttrib(gl.ENABLE_BIT); gl.PushAttrib(gl.SCISSOR_BIT); gl.PushAttrib(gl.CURRENT_BIT);
			gl.Viewport(px0, py0, px1 - px0, py1 - py0);
			gl.Scissor(px0, py0, px1 - px0, py1 - py0);
			gl.Enable(gl.SCISSOR_TEST);
			gl.ClearColor(0, 0, 0, 1);
			gl.Clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

			// The picture's 72 x 72 pixels fill the viewport (2:1, as the game
			// shows it); depth is the distance along the view, from a little
			// behind the eye to the end of the table
			const zNear = -2 * CELL, zFar = CELL * (VIEW_RANGE + 1);
			gl.MatrixMode(gl.PROJECTION); gl.PushMatrix(); gl.LoadIdentity();
			gl.Translatef(-1, 1, -(zFar + zNear) / (zFar - zNear));
			gl.Scalef(2 / PIC, -2 / PIC, -2 / (zFar - zNear));
			gl.MatrixMode(gl.MODELVIEW); gl.PushMatrix(); gl.LoadIdentity();

			gl.Enable(gl.TEXTURE_2D);
			gl.Disable(gl.BLEND);
			gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
			gl.Enable(gl.DEPTH_TEST);
			gl.Disable(gl.CULL_FACE);
			gl.Enable(gl.FOG);
			gl.Fogf(gl.FOG_MODE, gl.LINEAR);
			gl.Fogf(gl.FOG_START, CELL * 1.5);
			gl.Fogf(gl.FOG_END, CELL * (VIEW_RANGE - 1));
			gl.Fogfv(gl.FOG_COLOR, [0, 0, 0, 1]);

			// Floor and ceiling in the game's colours, then the walls
			const [fr, fg, fb] = a8.rgb(mem[COLOUR_FLOOR]), [cr, cg, cb] = a8.rgb(mem[COLOUR_CEILING]);
			gl.Color4f(fr / 255, fg / 255, fb / 255, 1);
			gl.BindTexture(gl.TEXTURE_2D, planes.floor.id);
			plane(0, cx, cy);
			gl.Color4f(cr / 255, cg / 255, cb / 255, 1);
			gl.BindTexture(gl.TEXTURE_2D, planes.ceiling.id);
			plane(CELL, cx, cy);
			drawWalls(textures, cx, cy);
			if (buildSprites(this.options.smoothTextures))
				drawSprites();

			gl.Disable(gl.FOG);
			gl.Disable(gl.DEPTH_TEST);
			gl.MatrixMode(gl.PROJECTION); gl.PopMatrix();
			gl.MatrixMode(gl.MODELVIEW); gl.PopMatrix();
			gl.Viewport(vx, vy, vw, vh);
			gl.PopAttrib(); gl.PopAttrib(); gl.PopAttrib();
			gl.Color4f(1, 1, 1, 1);
			return true;
		},
	};
}
