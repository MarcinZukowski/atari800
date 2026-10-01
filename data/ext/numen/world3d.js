// Numen - the demo's 3D levels (the forest, the maze) rebuilt from its own data. The demo's
// engine is a sector renderer: polygonal sectors with a floor and a ceiling
// height, walls where neighbouring heights differ, sprites standing in the
// sectors and a tiled backdrop behind it all. Here the same sectors, walls,
// sprites and backdrop are drawn with OpenGL through the demo's own camera,
// at the window's resolution. The tables and the projection are described in
// numen.md.

import { scale2x } from "../common.js";

const mem = a8.mem;
const word = (lo, hi, i) => mem[lo + i] | mem[hi + i] << 8;

// The sectors: per sector, and per vertex (the edge from a vertex to the next)
const SECTOR_COUNT = 0x7D80, EDGE_START = 0x70C0;
const SECTOR_FLAGS = 0x7000, CEILING = 0x7100, FLOOR = 0x7140, CEILING_COLOUR = 0x7180, FLOOR_COLOUR = 0x71C0;
const OPEN_SKY = 0x08;
const VERTEX_X_LO = 0x7300, VERTEX_X_HI = 0x7400, VERTEX_Z_LO = 0x7500, VERTEX_Z_HI = 0x7600;
const NEXT_VERTEX = 0x7700, NEIGHBOUR = 0x7900, EDGE_COLOUR = 0x7A00, NO_NEIGHBOUR = 0x80;
const PATTERNS = 0x4C40, PATTERN_COUNT = 52;   // colour number -> the byte (two pixels) it is drawn with
// The sprites: per object, and per sprite type
const OBJECT_COUNT = 0x7D82, OBJECT_FLAGS = 0x7B00, CENTRED = 0x02;
const OBJECT_X_LO = 0x7B40, OBJECT_X_HI = 0x7B80, OBJECT_Z_LO = 0x7BC0, OBJECT_Z_HI = 0x7C00;
const OBJECT_ANCHOR = 0x7C40, OBJECT_TYPE = 0x7CC0, OBJECT_HALF_WIDTH = 0x7D00, OBJECT_HEIGHT = 0x7D40;
const TYPE_COLUMNS_LO = 0x7E80, TYPE_COLUMNS_HI = 0x7EA0, TYPE_WIDTH = 0x7EC0, TYPE_HEIGHT = 0x7EE0;
// The backdrop: 80 rows of a tile 16 bytes (32 pixels) wide
const BACKDROP_ROW_LO = 0x7DA0, BACKDROP_ROW_HI = 0x7E00, BACKDROP_ROWS = 80, BACKDROP_BYTES = 16;
// The demo's tables at $2500 (bytes scrolled by heading) and $2640 (first row
// by horizon) are these lines, rounded down
const BACKDROP_SCROLL_STEP = 163.5 / 256, BACKDROP_TOP = 32 + 2 / 3, BACKDROP_RISE = 2 / 3;
// The camera
const CAMERA_X = 0x90, CAMERA_Z = 0x92, CAMERA_EYE = 0x94, CAMERA_HEADING = 0x95, HORIZON = 0x552B;

// The scene on the screen: 80 x 48 pixels of 4 x 4 from (8, 24)
const SCREEN_W = 336, SCREEN_H = 240, SCENE = [8, 24, 328, 216], COLUMNS = 80, ROWS = 48;
// The demo's projection (measured from its own numbers, see numen.md):
// column = 40 + FOCAL * x / depth, row = horizon + 4 * FOCAL * height / depth
const FOCAL = 38, HEIGHT_SCALE = 4, SPRITE_HEIGHT_SCALE = 2;
const NEAR = 16, FAR = 1000000, FAR_OUT = 400000;
// The shading, where the demo has none: walls by how they face a light from
// the side, ceilings and the feet of walls and sprites a little darker, round
// shadows under the sprites, and a light fog
const LIGHT = [0.6, 0.8], LIGHT_AMBIENT = 0.78, LIGHT_DIRECT = 0.22, CEILING_SHADE = 0.9, FOOT_SHADE = 0.8;
const SHADOW_ALPHA = 0.35, SHADOW_SIZE = 0.75, SHADOW_STEPS = 16;
// The ground texture: a grain over every floor, ceiling and wall, and a dither
// drawn as tiles of its two colours (a tile: TILE units of ground, CELLS
// squares of the checker across)
const GROUND_SIZE = 128, GROUND_TILE = 256, GROUND_CELLS = 4, GRAIN = 0.2, GRAIN_LEVEL = 0.97;
const FOG_DENSITY = 0.00008, FOG_WHITE = 0.4, HAZE = [[10, 0], [3, 0.3], [0, 0.8]];   // the haze over the backdrop: [rows above the horizon, how thick]

// The engine's code, to tell that its bank is mapped in at $4000 right now
const engineMapped = () => mem[0x6611] === 0x20 && mem[0x6612] === 0xF6 && mem[0x6613] === 0x53
	&& mem[0x57A7] === 0xAE && mem[0x57A8] === 0x2B && mem[0x57A9] === 0x55;

// What a pixel value 0-15 is depends on the GTIA mode in PRIOR. Mode 10 (the
// forest): a colour register, 0-3 the players, 4-7 the playfields, 8-11 the
// background, 12-15 the playfields again. Mode 9 (the maze): a luminance of
// the background's hue. Mode 11: a hue at the background's luminance.
// same: the lowest pixel value that looks the same (for the scaler);
// colour: the Atari colour of a pixel value
function gtiaColours() {
	const g = a8.gtia, mode = g.prior & 0xC0;
	const registers = [g.colpm0, g.colpm1, g.colpm2, g.colpm3, g.colpf0, g.colpf1, g.colpf2, g.colpf3, g.colbk];
	const register = (value) => value < 8 ? value : value < 12 ? 8 : value - 8;
	const atari = mode === 0x40 ? (value) => (g.colbk & 0xF0) | value
		: mode === 0xC0 ? (value) => value << 4 | (g.colbk & 0x0F)
		: (value) => registers[register(value)];
	return {
		key: mode + ":" + registers.join(),
		same: mode === 0x40 || mode === 0xC0 ? (value) => value : register,
		rgb: Array.from({ length: 16 }, (_, value) => a8.palette[atari(value)]),
	};
}

/* ------------------------------ the data ------------------------------ */

// A sector's outline is one loop of vertices or several: the outer one and
// holes in it (the maze's pillars). Triangles as index triples, by ear
// clipping, after each hole is joined to the outer loop by a bridge
const cross = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
function loopArea(points, loop) {
	let area = 0;
	for (let i = 0; i < loop.length; i++) { const a = points[loop[i]], b = points[loop[(i + 1) % loop.length]]; area += a[0] * b[1] - b[0] * a[1]; }
	return area / 2;
}
// Does the segment p-q cross or touch the segment a-b (other than at a shared end)?
function blocks(p, q, a, b) {
	const same = (u, v) => u[0] === v[0] && u[1] === v[1];
	const within = (u) => !same(u, p) && !same(u, q) && cross(p, q, u) === 0 && Math.min(p[0], q[0]) <= u[0] && u[0] <= Math.max(p[0], q[0]) && Math.min(p[1], q[1]) <= u[1] && u[1] <= Math.max(p[1], q[1]);
	if (within(a) || within(b)) return true;
	if (same(a, p) || same(a, q) || same(b, p) || same(b, q)) return false;
	const d1 = cross(a, b, p), d2 = cross(a, b, q), d3 = cross(p, q, a), d4 = cross(p, q, b);
	return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}
function triangulate(points, loops) {
	const byArea = loops.slice().sort((a, b) => Math.abs(loopArea(points, b)) - Math.abs(loopArea(points, a)));
	let ring = byArea[0].slice();
	const sign = loopArea(points, ring) >= 0 ? 1 : -1;
	const holes = byArea.slice(1).map((hole) => (loopArea(points, hole) >= 0 ? 1 : -1) === sign ? hole.slice().reverse() : hole.slice());
	const rightmost = (hole) => hole.reduce((best, v) => points[v][0] > points[best][0] ? v : best, hole[0]);
	holes.sort((a, b) => points[rightmost(b)][0] - points[rightmost(a)][0]);
	holes.forEach((hole, n) => {
		const h = hole.indexOf(rightmost(hole)), H = points[hole[h]];
		const edges = [];   // every edge the bridge must not cross: the ring's and the holes' still apart
		for (let i = 0; i < ring.length; i++) edges.push([points[ring[i]], points[ring[(i + 1) % ring.length]]]);
		for (const other of holes.slice(n)) for (let i = 0; i < other.length; i++) edges.push([points[other[i]], points[other[(i + 1) % other.length]]]);
		const near = ring.map((v, i) => i).sort((i, j) => Math.hypot(points[ring[i]][0] - H[0], points[ring[i]][1] - H[1]) - Math.hypot(points[ring[j]][0] - H[0], points[ring[j]][1] - H[1]));
		const at = near.find((i) => !edges.some(([a, b]) => blocks(H, points[ring[i]], a, b)));
		if (at === undefined) return;
		ring = [...ring.slice(0, at + 1), ...hole.slice(h), ...hole.slice(0, h + 1), ...ring.slice(at)];
	});
	const left = ring.slice(), triangles = [];
	while (left.length > 3) {
		let cut = -1, flat = -1;
		for (let i = 0; i < left.length && cut < 0; i++) {
			const ia = left[(i + left.length - 1) % left.length], ib = left[i], ic = left[(i + 1) % left.length];
			const a = points[ia], b = points[ib], c = points[ic], turn = cross(a, b, c) * sign;
			if (turn === 0) { flat = i; continue; }
			if (turn < 0) continue;   // a reflex corner
			// an ear holds no other vertex, and its cut (a to c) passes through none
			let ear = true;
			for (const j of left) {
				const p = points[j];
				if (cross(a, b, p) * sign > 0 && cross(b, c, p) * sign > 0 && cross(c, a, p) * sign > 0) { ear = false; break; }
				if (cross(a, c, p) === 0 && (p[0] - a[0]) * (p[0] - c[0]) + (p[1] - a[1]) * (p[1] - c[1]) < 0) { ear = false; break; }
			}
			if (ear) { triangles.push([ia, ib, ic]); cut = i; }
		}
		if (cut < 0) cut = flat;   // nothing to cut but a vertex on a straight line: drop it
		if (cut < 0) break;
		left.splice(cut, 1);
	}
	if (left.length === 3 && cross(points[left[0]], points[left[1]], points[left[2]]) !== 0) triangles.push(left);
	return { triangles, sign };
}

// Reads the level: null unless the tables look like one
function readWorld() {
	const count = mem[SECTOR_COUNT];
	if (count < 1 || count > 48) return null;
	const sectors = [];
	for (let s = 0; s < count; s++) {
		const start = mem[EDGE_START + s], end = mem[EDGE_START + s + 1];
		if (end < start + 3 || end > 0x80) return null;
		const points = [], edges = [];
		for (let k = start; k < end; k++) points.push([word(VERTEX_X_LO, VERTEX_X_HI, k), word(VERTEX_Z_LO, VERTEX_Z_HI, k)]);
		for (let k = start; k < end; k++) {
			const next = mem[NEXT_VERTEX + k];
			if (next < start || next >= end) return null;
			edges.push({ a: points[k - start], b: points[next - start], neighbour: mem[NEIGHBOUR + k] >= NO_NEIGHBOUR ? -1 : mem[NEIGHBOUR + k], colour: mem[EDGE_COLOUR + k] });
		}
		// the loops of the outline, following each vertex to the next
		const loops = [], seen = new Set();
		for (let k = start; k < end; k++) {
			if (seen.has(k)) continue;
			const loop = [];
			for (let v = k; !seen.has(v); v = mem[NEXT_VERTEX + v]) { seen.add(v); loop.push(v - start); }
			if (loop.length >= 3) loops.push(loop);
		}
		if (loops.length === 0) return null;
		const { triangles, sign } = triangulate(points, loops);
		sectors.push({
			outward: sign,
			sky: (mem[SECTOR_FLAGS + s] & OPEN_SKY) !== 0, ceiling: mem[CEILING + s], floor: mem[FLOOR + s],
			ceilingColour: mem[CEILING_COLOUR + s], floorColour: mem[FLOOR_COLOUR + s],
			points, edges, triangles,
		});
	}
	for (const sector of sectors) for (const edge of sector.edges) if (edge.neighbour >= count) return null;

	// What to draw, as flat-coloured triangles and quads [colour number, x, y, z, ...]; y is up
	const y = (height) => -HEIGHT_SCALE * height;
	const triangles = [], quads = [], open = [];
	// (the unit vector out of the sector across an edge)
	const outward = (edge, sector) => { const dx = edge.b[0] - edge.a[0], dz = edge.b[1] - edge.a[1], l = Math.hypot(dx, dz) || 1; return [dz / l * sector.outward, -dx / l * sector.outward]; };
	// a wall: colour, how it faces the light, then its corners: the two on the floor side first
	const wall = (edge, sector, from, to) => {
		const [nx, nz] = outward(edge, sector);
		quads.push([edge.colour, LIGHT_AMBIENT + LIGHT_DIRECT * Math.max(0, -(nx * LIGHT[0] + nz * LIGHT[1])),
			edge.a[0], y(from), edge.a[1], edge.b[0], y(from), edge.b[1], edge.b[0], y(to), edge.b[1], edge.a[0], y(to), edge.a[1]]);
	};
	// An outer edge of colour 0 has no wall: the demo fills its columns with the
	// floor's colour up to the horizon and with the backdrop above, as if the
	// floor went on for ever beyond it (and so the ceiling, where there is one)
	const openEdge = (edge, sector) => {
		const [nx, nz] = outward(edge, sector);
		return { a: edge.a, b: edge.b, nx, nz, floor: y(sector.floor), floorColour: sector.floorColour, ceiling: sector.sky ? null : y(sector.ceiling), ceilingColour: sector.ceilingColour };
	};
	for (const sector of sectors) {
		for (const [ia, ib, ic] of sector.triangles) {
			const a = sector.points[ia], b = sector.points[ib], c = sector.points[ic];
			triangles.push([sector.floorColour, 1, a[0], y(sector.floor), a[1], b[0], y(sector.floor), b[1], c[0], y(sector.floor), c[1]]);
			if (!sector.sky) triangles.push([sector.ceilingColour, CEILING_SHADE, a[0], y(sector.ceiling), a[1], b[0], y(sector.ceiling), b[1], c[0], y(sector.ceiling), c[1]]);
		}
		for (const edge of sector.edges) {
			if (edge.neighbour < 0) {
				if (edge.colour === 0) open.push(openEdge(edge, sector));
				else if (sector.floor > sector.ceiling) wall(edge, sector, sector.floor, sector.ceiling);
				continue;
			}
			const other = sectors[edge.neighbour];
			if (other.floor < sector.floor) wall(edge, sector, sector.floor, Math.max(other.floor, sector.ceiling));              // a step up
			if (!(sector.sky && other.sky) && other.ceiling > sector.ceiling) wall(edge, sector, Math.min(other.ceiling, sector.floor), sector.ceiling);   // a lower ceiling beyond
		}
	}
	const objects = [];
	const objectCount = Math.min(mem[OBJECT_COUNT], 64);
	for (let o = 0; o < objectCount; o++) {
		objects.push({
			x: word(OBJECT_X_LO, OBJECT_X_HI, o), z: word(OBJECT_Z_LO, OBJECT_Z_HI, o), type: mem[OBJECT_TYPE + o] & 31,
			anchor: mem[OBJECT_ANCHOR + o], halfWidth: mem[OBJECT_HALF_WIDTH + o], height: mem[OBJECT_HEIGHT + o], centred: (mem[OBJECT_FLAGS + o] & CENTRED) !== 0,
		});
	}
	// The sprites: per type a list of column pointers; a column is a byte per
	// row, zero for nothing, else the pixel value inverted in both nibbles.
	// Kept as pictures of 1 + pixel value (0: transparent) with an empty
	// border, for the scaler
	const types = new Map();
	for (const object of objects) {
		if (types.has(object.type)) continue;
		const t = object.type, table = word(TYPE_COLUMNS_LO, TYPE_COLUMNS_HI, t), w = mem[TYPE_WIDTH + t], h = mem[TYPE_HEIGHT + t];
		if (w < 1 || w > 32 || h < 1 || h > 32) return null;
		const picture = new Uint8Array((w + 2) * (h + 2));
		for (let column = 0; column < w; column++) {
			const data = mem[table + 2 * column] | mem[table + 2 * column + 1] << 8;
			for (let row = 0; row < h; row++) {
				const byte = mem[(data + row) & 0xFFFF];
				if (byte) picture[(row + 1) * (w + 2) + column + 1] = 1 + (15 - (byte >> 4));
			}
		}
		types.set(t, { w: w + 2, h: h + 2, picture });
	}
	// The backdrop, as a picture of pixel values
	const backdrop = new Uint8Array(BACKDROP_BYTES * 2 * BACKDROP_ROWS);
	for (let row = 0; row < BACKDROP_ROWS; row++) {
		const data = word(BACKDROP_ROW_LO, BACKDROP_ROW_HI, row);
		for (let i = 0; i < BACKDROP_BYTES; i++) {
			const byte = mem[(data + i) & 0xFFFF];
			backdrop[row * BACKDROP_BYTES * 2 + 2 * i] = byte >> 4;
			backdrop[row * BACKDROP_BYTES * 2 + 2 * i + 1] = byte & 15;
		}
	}
	triangles.sort((a, b) => a[0] - b[0]); quads.sort((a, b) => a[0] - b[0]);
	return { triangles, quads, open, objects, types, backdrop, outdoor: sectors.some((sector) => sector.sky), patterns: mem.slice(PATTERNS, PATTERNS + PATTERN_COUNT) };
}

// A sum over the level's tables and pictures, to notice another level
function worldSum() {
	let sum = 0;
	for (let i = 0x7000; i < 0x7F00; i++) sum = (sum * 31 + mem[i]) | 0;
	for (let i = 0xE000; i < 0xEC40; i++) sum = (sum * 31 + mem[i]) | 0;
	return sum;
}

/* ------------------------------ the textures ------------------------------ */

// A picture of pixel values (offset by one when zero means transparent) as a
// texture; smooth: Scale2x twice and linear filtering
function pictureTexture(picture, w, h, colours, transparent, smooth, repeat) {
	picture = picture.map((v) => transparent ? (v ? 1 + colours.same(v - 1) : 0) : colours.same(v));
	if (smooth) {
		picture = scale2x(picture, w, h); w *= 2; h *= 2;
		picture = scale2x(picture, w, h); w *= 2; h *= 2;
	}
	const texture = gl.createTexture(w, h), px = texture.pixels;
	for (let i = 0, o = 0; i < picture.length; i++, o += 4) {
		const v = picture[i];
		if (transparent && v === 0) { px[o] = px[o + 1] = px[o + 2] = px[o + 3] = 0; continue; }
		const c = colours.rgb[transparent ? v - 1 : v];
		px[o] = c >> 16; px[o + 1] = (c >> 8) & 255; px[o + 2] = c & 255; px[o + 3] = 255;
	}
	if (transparent && smooth) {
		// linear filtering mixes in the colour of transparent neighbours: give them the colour next to them
		for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) {
			const o = 4 * (yy * w + xx);
			if (px[o + 3]) continue;
			for (const n of [xx > 0 ? o - 4 : -1, xx < w - 1 ? o + 4 : -1, yy > 0 ? o - 4 * w : -1, yy < h - 1 ? o + 4 * w : -1]) {
				if (n >= 0 && px[n + 3] === 255) { px[o] = px[n]; px[o + 1] = px[n + 1]; px[o + 2] = px[n + 2]; break; }
			}
		}
	}
	if (smooth) { gl.BindTexture(gl.TEXTURE_2D, texture.id); gl.TexParameteri(gl.TEXTURE_2D, gl.GENERATE_MIPMAP, gl.TRUE); }
	texture.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, repeat ? gl.REPEAT : gl.CLAMP_TO_EDGE);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, smooth ? gl.LINEAR_MIPMAP_LINEAR : gl.NEAREST);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, smooth ? gl.LINEAR : gl.NEAREST);
	return texture;
}

// A grain that tiles: value noise in three sizes, 0..1, the same on every run
let grain = null;
function groundGrain() {
	if (grain !== null) return grain;
	let seed = 0x4E554D45;
	const random = () => { seed = (seed + 0x6D2B79F5) | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
	grain = new Float32Array(GROUND_SIZE * GROUND_SIZE);
	for (const [cells, weight] of [[4, 0.4], [16, 0.3], [64, 0.3]]) {
		const lattice = Float32Array.from({ length: cells * cells }, random), step = GROUND_SIZE / cells;
		const ease = (t) => t * t * (3 - 2 * t);
		for (let y = 0; y < GROUND_SIZE; y++) for (let x = 0; x < GROUND_SIZE; x++) {
			const cx = Math.floor(x / step), cy = Math.floor(y / step), fx = ease(x / step - cx), fy = ease(y / step - cy);
			const at = (i, j) => lattice[(j % cells) * cells + i % cells];
			grain[y * GROUND_SIZE + x] += weight * ((at(cx, cy) * (1 - fx) + at(cx + 1, cy) * fx) * (1 - fy) + (at(cx, cy + 1) * (1 - fx) + at(cx + 1, cy + 1) * fx) * fy);
		}
	}
	return grain;
}
// The texture of a colour number: its two colours as a checker (one colour
// when they are the same), under the grain
function groundTexture(byte, colours) {
	const texture = gl.createTexture(GROUND_SIZE, GROUND_SIZE), px = texture.pixels, noise = groundGrain();
	const pair = [colours.rgb[byte >> 4], colours.rgb[byte & 15]], cell = GROUND_SIZE / GROUND_CELLS;
	for (let y = 0, o = 0; y < GROUND_SIZE; y++) for (let x = 0; x < GROUND_SIZE; x++, o += 4) {
		const c = pair[(Math.floor(x / cell) + Math.floor(y / cell)) & 1], f = GRAIN_LEVEL + GRAIN * (noise[y * GROUND_SIZE + x] - 0.5);
		px[o] = Math.min(255, (c >> 16) * f); px[o + 1] = Math.min(255, ((c >> 8) & 255) * f); px[o + 2] = Math.min(255, (c & 255) * f); px[o + 3] = 255;
	}
	gl.BindTexture(gl.TEXTURE_2D, texture.id); gl.TexParameteri(gl.TEXTURE_2D, gl.GENERATE_MIPMAP, gl.TRUE);
	texture.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT); gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR); gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
	return texture;
}

/* ------------------------------ the view ------------------------------ */

export function createWorld3D() {
	let world = null, sum = 0, age = 0, unmapped = 0, horizon = 18;
	let textures = null, textureKey = "";
	let calls = 0, from = null, to = null, since = 0, interval = 1;

	// The demo moves its camera by the time that passed, once per picture it
	// renders: a few times a second. Here the view glides from each of those
	// positions to the next over the time the last step took, one step behind
	const turn = (a, b) => (((b - a + 128) % 256) + 256) % 256 - 128;
	const glide = (t) => ({
		x: from.x + (to.x - from.x) * t, z: from.z + (to.z - from.z) * t, eye: from.eye + (to.eye - from.eye) * t,
		heading: from.heading + turn(from.heading, to.heading) * t, horizon: from.horizon + (to.horizon - from.horizon) * t,
	});
	function liveCamera() {
		const now = { x: word(CAMERA_X, CAMERA_X + 1, 0), z: word(CAMERA_Z, CAMERA_Z + 1, 0), eye: mem[CAMERA_EYE], heading: mem[CAMERA_HEADING], horizon };
		if (to === null || now.x !== to.x || now.z !== to.z || now.eye !== to.eye || now.heading !== to.heading || now.horizon !== to.horizon) {
			const cut = to === null || Math.hypot(now.x - to.x, now.z - to.z) > 400 || Math.abs(turn(to.heading, now.heading)) > 40 || calls - since > 50;
			from = cut ? now : glide(Math.min(1, (calls - since) / interval));   // from where the view is
			interval = Math.max(1, Math.min(25, calls - since));
			to = now; since = calls;
		}
		return glide(Math.min(1, (calls - since) / interval));
	}

	// Keeps the copy of the level current; false when there is none
	function track() {
		if (!engineMapped()) {
			if (++unmapped > 100) world = null;   // the demo went elsewhere
			return world !== null;
		}
		unmapped = 0;
		horizon = mem[HORIZON];
		if (world === null || age++ % 32 === 0) {
			const now = worldSum();
			if (world === null || now !== sum) { world = readWorld(); sum = now; textures = null; }
		}
		return world !== null;
	}

	return {
		// Draws the scene over its place on the screen; false when the level is
		// not there to draw. options: smooth (the pictures scaled up), shade
		// (shading, shadows and fog), full (the whole picture's area, not only
		// the demo's rectangle), ground (the ground texture), antialias;
		// camera: {x, z, eye, heading, horizon} instead of the demo's
		render(options, camera) {
			if (!track()) return false;
			const { smooth, shade, full, ground, antialias } = options;
			const colours = gtiaColours();
			const key = colours.key + smooth;
			if (textures === null || key !== textureKey) {
				textureKey = key;
				textures = { backdrop: pictureTexture(world.backdrop, BACKDROP_BYTES * 2, BACKDROP_ROWS, colours, false, smooth, true), types: new Map(), ground: new Map() };
				for (const [t, type] of world.types) textures.types.set(t, pictureTexture(type.picture, type.w, type.h, colours, true, smooth, false));
			}
			// a colour number is a byte of two pixels: drawn here as their mix
			const mixed = (number) => {
				const byte = world.patterns[number < PATTERN_COUNT ? number : 0], a = colours.rgb[byte >> 4], b = colours.rgb[byte & 15];
				return [((a >> 16) + (b >> 16)) / 510, (((a >> 8) & 255) + ((b >> 8) & 255)) / 510, ((a & 255) + (b & 255)) / 510];
			};
			const flat = (number, light) => { const [r, g, b] = ground ? [1, 1, 1] : mixed(number), l = shade ? light : 1; gl.Color4f(r * l, g * l, b * l, 1); };
			// with the ground texture a colour number is a texture, made when first met
			const textured = (number) => {
				if (!ground) return;
				const byte = world.patterns[number < PATTERN_COUNT ? number : 0];
				if (!textures.ground.has(byte)) textures.ground.set(byte, groundTexture(byte, colours));
				gl.BindTexture(gl.TEXTURE_2D, textures.ground.get(byte).id);
			};
			// (lists sorted by colour number: one Begin for each)
			const each = (list, mode, emit) => {
				let number = -1;
				for (const item of list) {
					if (item[0] !== number) { if (number >= 0) gl.End(); number = item[0]; textured(number); gl.Begin(mode); }
					emit(item);
				}
				if (number >= 0) gl.End();
			};
			const floorAt = (x, y, z) => { gl.TexCoord2f(x / GROUND_TILE, z / GROUND_TILE); gl.Vertex3f(x, y, z); };

			calls++;
			const cam = camera || liveCamera();
			const angle = cam.heading / 256 * 2 * Math.PI, cos = Math.cos(angle), sin = Math.sin(angle);

			const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
			// The area drawn: the demo's rectangle, or the whole picture at the same
			// scale, which shows more around it. In the scene's columns and rows:
			const pixel = SCENE[2] / COLUMNS - SCENE[0] / COLUMNS;   // screen pixels per column and per row
			const c0 = full ? -SCENE[0] / pixel : 0, c1 = full ? (SCREEN_W - SCENE[0]) / pixel : COLUMNS;
			const r0 = full ? -SCENE[1] / pixel : 0, r1 = full ? (SCREEN_H - SCENE[1]) / pixel : ROWS;
			const px0 = full ? vx : Math.round(vx + SCENE[0] / SCREEN_W * vw), px1 = full ? vx + vw : Math.round(vx + SCENE[2] / SCREEN_W * vw);
			const py0 = full ? vy : Math.round(vy + (1 - SCENE[3] / SCREEN_H) * vh), py1 = full ? vy + vh : Math.round(vy + (1 - SCENE[1] / SCREEN_H) * vh);
			gl.PushAttrib(gl.ALL_ATTRIB_BITS);
			if (!antialias && gl.MULTISAMPLE !== undefined) gl.Disable(gl.MULTISAMPLE);
			gl.Viewport(px0, py0, px1 - px0, py1 - py0);
			gl.Scissor(px0, py0, px1 - px0, py1 - py0);
			gl.Enable(gl.SCISSOR_TEST);
			gl.Disable(gl.LIGHTING); gl.Disable(gl.FOG); gl.Disable(gl.CULL_FACE); gl.Disable(gl.BLEND);
			gl.ClearColor(0, 0, 0, 1);
			gl.Clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
			gl.MatrixMode(gl.PROJECTION); gl.PushMatrix(); gl.LoadIdentity();
			gl.MatrixMode(gl.MODELVIEW); gl.PushMatrix(); gl.LoadIdentity();

			// The fog: outdoors a haze the colour of a paled sky, indoors the dark
			const sky = colours.rgb[world.backdrop[0]];
			const pale = (c) => FOG_WHITE + (1 - FOG_WHITE) * c / 255;
			const fog = world.outdoor ? [pale(sky >> 16), pale((sky >> 8) & 255), pale(sky & 255)] : [0, 0, 0];

			// The backdrop, in the scene's own pixels: the tile scrolls with the
			// heading and moves up and down with the horizon, as the demo's does
			gl.MatrixMode(gl.PROJECTION); gl.Ortho(c0, c1, r1, r0, -1, 1);
			gl.Disable(gl.DEPTH_TEST);
			if (world.outdoor) {
				gl.Enable(gl.TEXTURE_2D);
				gl.BindTexture(gl.TEXTURE_2D, textures.backdrop.id);
				gl.Color4f(1, 1, 1, 1);
				const tile = BACKDROP_BYTES * 2;
				const u = (column) => (2 * (cam.heading * BACKDROP_SCROLL_STEP + 9) % tile + column) / tile;
				const v = (row) => (Math.max(0, BACKDROP_TOP - cam.horizon * BACKDROP_RISE) + row) / BACKDROP_ROWS;   // (beyond the tile its first and last rows repeat)
				gl.Begin(gl.QUADS);
				gl.TexCoord2f(u(c0), v(r0)); gl.Vertex3f(c0, r0, 0);
				gl.TexCoord2f(u(c1), v(r0)); gl.Vertex3f(c1, r0, 0);
				gl.TexCoord2f(u(c1), v(r1)); gl.Vertex3f(c1, r1, 0);
				gl.TexCoord2f(u(c0), v(r1)); gl.Vertex3f(c0, r1, 0);
				gl.End();
				gl.Disable(gl.TEXTURE_2D);
				if (shade) {   // the haze over the far hills, thickest at the horizon
					gl.Enable(gl.BLEND); gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
					gl.Begin(gl.QUADS);
					for (let i = 0; i + 1 < HAZE.length; i++) {
						const [rows0, thick0] = HAZE[i], [rows1, thick1] = HAZE[i + 1];
						gl.Color4f(fog[0], fog[1], fog[2], thick0); gl.Vertex3f(c0, cam.horizon - rows0, 0); gl.Vertex3f(c1, cam.horizon - rows0, 0);
						gl.Color4f(fog[0], fog[1], fog[2], thick1); gl.Vertex3f(c1, cam.horizon - rows1, 0); gl.Vertex3f(c0, cam.horizon - rows1, 0);
					}
					gl.Vertex3f(c0, cam.horizon, 0); gl.Vertex3f(c1, cam.horizon, 0); gl.Vertex3f(c1, r1, 0); gl.Vertex3f(c0, r1, 0);
					gl.End();
					gl.Disable(gl.BLEND);
				}
			}

			// The demo's camera: x to the right, the depth along the heading,
			// heights four times as large on the screen, the eye level at the
			// horizon row
			gl.LoadIdentity();
			gl.Frustum(NEAR * (c0 - COLUMNS / 2) / FOCAL, NEAR * (c1 - COLUMNS / 2) / FOCAL, -NEAR * (r1 - cam.horizon) / FOCAL, NEAR * (cam.horizon - r0) / FOCAL, NEAR, FAR);
			gl.MatrixMode(gl.MODELVIEW);
			gl.Rotatef(cam.heading / 256 * 360 + 90, 0, 1, 0);
			gl.Translatef(-cam.x, HEIGHT_SCALE * cam.eye, -cam.z);
			if (shade) {
				gl.Enable(gl.FOG);
				gl.Fogf(gl.FOG_MODE, gl.EXP); gl.Fogf(gl.FOG_DENSITY, FOG_DENSITY); gl.Fogfv(gl.FOG_COLOR, [fog[0], fog[1], fog[2], 1]);
			}

			gl.Enable(gl.DEPTH_TEST);
			if (ground) gl.Enable(gl.TEXTURE_2D); else gl.Disable(gl.TEXTURE_2D);
			each(world.triangles, gl.TRIANGLES, (t) => { flat(t[0], t[1]); floorAt(t[2], t[3], t[4]); floorAt(t[5], t[6], t[7]); floorAt(t[8], t[9], t[10]); });
			each(world.quads, gl.QUADS, (q) => {
				const along = Math.hypot(q[5] - q[2], q[7] - q[4]) / GROUND_TILE;
				flat(q[0], q[1] * FOOT_SHADE);
				gl.TexCoord2f(0, q[3] / GROUND_TILE); gl.Vertex3f(q[2], q[3], q[4]); gl.TexCoord2f(along, q[6] / GROUND_TILE); gl.Vertex3f(q[5], q[6], q[7]);
				flat(q[0], q[1]);
				gl.TexCoord2f(along, q[9] / GROUND_TILE); gl.Vertex3f(q[8], q[9], q[10]); gl.TexCoord2f(0, q[12] / GROUND_TILE); gl.Vertex3f(q[11], q[12], q[13]);
			});
			// Beyond the open edges the floor (and the ceiling) go on: each as a
			// fan from the camera through its edge, the columns the demo fills.
			// A little apart in height, the nearest edge's on top, so that they
			// never fight each other or a real floor
			const fans = world.open.filter((o) => (cam.x - o.a[0]) * o.nx + (cam.z - o.a[1]) * o.nz < 0)
				.map((o) => ({ o, d: Math.hypot((o.a[0] + o.b[0]) / 2 - cam.x, (o.a[1] + o.b[1]) / 2 - cam.z) })).sort((p, q) => p.d - q.d);
			fans.forEach(({ o }, rank) => {
				const far = (point) => { const dx = point[0] - cam.x, dz = point[1] - cam.z, l = Math.hypot(dx, dz) || 1; return [point[0] + dx / l * FAR_OUT, point[1] + dz / l * FAR_OUT]; };
				const fa = far(o.a), fb = far(o.b), apart = 2 + 0.4 * rank;
				const fan = (number, light, y) => {
					textured(number);
					gl.Begin(gl.QUADS); flat(number, light);
					floorAt(o.a[0], y, o.a[1]); floorAt(o.b[0], y, o.b[1]); floorAt(fb[0], y, fb[1]); floorAt(fa[0], y, fa[1]);
					gl.End();
				};
				fan(o.floorColour, 1, o.floor - apart);
				if (o.ceiling !== null) fan(o.ceilingColour, CEILING_SHADE, o.ceiling + apart);
			});
			gl.Disable(gl.TEXTURE_2D);

			// The sprites: upright cards facing the camera, the farthest first,
			// each with a round shadow at its foot
			gl.Enable(gl.BLEND); gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
			const depth = (o) => (o.x - cam.x) * cos + (o.z - cam.z) * sin;
			const rightX = -sin, rightZ = cos;
			const sprites = world.objects.filter((o) => depth(o) > NEAR).sort((a, b) => depth(b) - depth(a));
			const foot = (o) => -HEIGHT_SCALE * o.anchor - (o.centred ? SPRITE_HEIGHT_SCALE * o.height / 2 : 0);
			if (shade) {
				for (const o of sprites) {
					const r = o.halfWidth * SHADOW_SIZE, yy = foot(o) + 1.5;
					gl.Begin(gl.TRIANGLE_FAN);
					gl.Color4f(0, 0, 0, SHADOW_ALPHA); gl.Vertex3f(o.x, yy, o.z);
					gl.Color4f(0, 0, 0, 0);
					for (let i = 0; i <= SHADOW_STEPS; i++) { const a = i / SHADOW_STEPS * 2 * Math.PI; gl.Vertex3f(o.x + Math.cos(a) * r, yy, o.z + Math.sin(a) * r); }
					gl.End();
				}
			}
			gl.Enable(gl.TEXTURE_2D);
			const low = shade ? FOOT_SHADE : 1;
			for (const o of sprites) {
				const texture = textures.types.get(o.type), type = world.types.get(o.type);
				const bottom = foot(o), top = bottom + SPRITE_HEIGHT_SCALE * o.height;
				const lx = o.x - rightX * o.halfWidth, lz = o.z - rightZ * o.halfWidth, rx = o.x + rightX * o.halfWidth, rz = o.z + rightZ * o.halfWidth;
				const ub = 1 / type.w, vb = 1 / type.h;   // the empty border stays outside
				gl.BindTexture(gl.TEXTURE_2D, texture.id);
				gl.Begin(gl.QUADS);
				gl.Color4f(low, low, low, 1);
				gl.TexCoord2f(ub, 1 - vb); gl.Vertex3f(lx, bottom, lz);
				gl.TexCoord2f(1 - ub, 1 - vb); gl.Vertex3f(rx, bottom, rz);
				gl.Color4f(1, 1, 1, 1);
				gl.TexCoord2f(1 - ub, vb); gl.Vertex3f(rx, top, rz);
				gl.TexCoord2f(ub, vb); gl.Vertex3f(lx, top, lz);
				gl.End();
			}

			gl.MatrixMode(gl.PROJECTION); gl.PopMatrix();
			gl.MatrixMode(gl.MODELVIEW); gl.PopMatrix();
			gl.PopAttrib();
			gl.Viewport(vx, vy, vw, vh);
			gl.Color4f(1, 1, 1, 1);
			return true;
		},
	};
}
