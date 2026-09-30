// Mercenary: accelerated drawing (the game's line and fill routines are
// re-implemented here and the 6502 code is skipped), and the 3D scene drawn
// again with OpenGL from the game's own geometry: the vertices' exact 24-bit
// positions and 10-bit view angles are read as the game projects them, the
// transform is redone in floating point, and every edge is drawn between the
// resulting sub-pixel end points. The addresses come from mercenary.md; the
// engine's structure follows the C64 version's analysis by gamesexplained.
import { rgb, word } from "../common.js";

const MODE_A8 = 0, MODE_GL = 1, MODE_BOTH = 2;                       // line drawing mode
const TYPE_LINE = 0, TYPE_POLYGON_LINE = 1, TYPE_POLYGON_FILL = 2;   // GL line type
const FACES_OFF = 0, FACES_GLASS = 1, FACES_SHADED = 2;                // face rendering

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

// The 3D pipeline: three routines compute a vertex relative to the eye as
// 24-bit integers (a building vertex, an object's model vertex after its
// orientation, the centre of a city square) and convert it to the game's
// two-byte floats; PROJECT_VERTEX transforms and projects those into slot
// $17; LINE_SETUP draws an edge between the slots in X and Y.
const VERTEX_REL = 0x49B6;
const MODEL_VERTEX_ORIENTED = 0x4A1D;   // inside model_vertex_rel, after the orientation
const SQUARE_CENTRE_REL = 0x4A63;
const PROJECT_VERTEX = 0x4B44;
const LINE_SETUP = 0x3E4E;
const BUILDING_EDGES = 0x3B58;          // start of the location's edge loop
const DRAW_OBJECT = 0x3B99;             // draw_object X
const BUILDING_EDGE_FROM = 0x1E80, BUILDING_EDGE_TO = 0x1EC0;   // the location's edge tables
const EYE = 0x70;                                        // X, height, Y: 24 bits each, low byte first
const VERTEX_X = [0x1D00, 0x1D40, 0x9F40];               // the location's vertex tables, low/mid/high
const VERTEX_H = [0x1D80, 0x1DC0, 0x9F80];
const VERTEX_Y = [0x1E00, 0x1E40, 0x9FC0];
const SLOT_SX = 0x9E80, SLOT_SY = 0x9EC0, SLOT_FLAGS = 0x9F00;   // the game's projection per slot
const VIEW_W = 160, VIEW_H = 152;                        // the 3D window in game pixels
const Z_NEAR = 16;                                       // clipping plane, in world units (65536 per square)

const mem = a8.mem;

const s24 = (lo, mid, hi) => { const v = lo | (mid << 8) | (hi << 16); return v >= 0x800000 ? v - 0x1000000 : v; };
// The game's float: mantissa byte and an exponent byte whose bits 2-7 are a
// signed power of two and bit 0 the sign; +-(1 + m/256) * 2^e
const gfloat = (m, e) => { const raw = e >> 2; const se = raw >= 32 ? raw - 64 : raw; const v = (1 + m / 256) * 2 ** se; return (e & 1) ? -v : v; };
// 10-bit angles, 1024 per turn; the game's sine table is offset by half a step
const angle = (a) => mem[a] | ((mem[a + 1] & 3) << 8);
const sin10 = (a) => Math.sin((a + 0.5) * Math.PI / 512);
const cos10 = (a) => Math.cos((a + 0.5) * Math.PI / 512);

let pendingVertex = null;      // exact eye-relative position awaiting PROJECT_VERTEX
const slots = new Array(64);   // per slot: { xp, yp, z } in view space, floats

// Edges are grouped per model (the location's building, each object) so
// that faces can be found in each model's edge graph. A group has a key
// identifying the model, the slot pairs of its structure edges and the
// vertices they use. Faces are lists of slots, cached per model key.
let currentGroup = null;
let shownGroups = [], preparedGroups = [];
const faceCache = new Map();

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

/* ------------------------------ geometry capture ------------------------------ */

function eyePosition() {
	return [s24(mem[EYE], mem[EYE + 1], mem[EYE + 2]),
	        s24(mem[EYE + 3], mem[EYE + 4], mem[EYE + 5]),
	        s24(mem[EYE + 6], mem[EYE + 7], mem[EYE + 8])];
}

// A building vertex: its absolute 24-bit position minus the eye
function captureBuildingVertex() {
	const i = mem[0x17];
	const [ex, eh, ey] = eyePosition();
	pendingVertex = [s24(mem[VERTEX_X[0] + i], mem[VERTEX_X[1] + i], mem[VERTEX_X[2] + i]) - ex,
	                 s24(mem[VERTEX_H[0] + i], mem[VERTEX_H[1] + i], mem[VERTEX_H[2] + i]) - eh,
	                 s24(mem[VERTEX_Y[0] + i], mem[VERTEX_Y[1] + i], mem[VERTEX_Y[2] + i]) - ey];
}

// An object's vertex: the object's eye-relative position ($D5-$DD, lowered by
// 2048 per axis) plus the oriented model offset (12 bits, model * 16 + 2048)
function captureModelVertex() {
	pendingVertex = [s24(mem[0xD5], mem[0xD6], mem[0xD7]) + (mem[0xCF] | (mem[0xD0] << 8)),
	                 s24(mem[0xD8], mem[0xD9], mem[0xDA]) + (mem[0xD1] | (mem[0xD2] << 8)),
	                 s24(mem[0xDB], mem[0xDC], mem[0xDD]) + (mem[0xD3] | (mem[0xD4] << 8))];
}

// The centre of city square A (row * 16 + column), ignoring the eye's low
// byte as the game does; the height float was set by the caller
function captureSquareCentre() {
	const sq = a8.cpu.a, col = sq & 0x0F, row = sq >> 4;
	pendingVertex = [((col << 16) | 0x8000) - ((mem[0x72] << 16) | (mem[0x71] << 8)),
	                 gfloat(mem[0x52], mem[0x53]),
	                 ((row << 16) | 0x8000) - ((mem[0x78] << 16) | (mem[0x77] << 8))];
}

// PROJECT_VERTEX: redo the game's transform in floating point. In flight the
// view matrix from roll r, pitch p and heading h applies to (X, height, Y);
// on foot and underground only the heading does.
function captureProjection() {
	let rel = pendingVertex;
	pendingVertex = null;
	if (rel === null)   // a path we do not intercept (the far-object dot): take the game's floats
		rel = [gfloat(mem[0x50], mem[0x51]), gfloat(mem[0x52], mem[0x53]), gfloat(mem[0x54], mem[0x55])];
	const [X, H, Y] = rel;
	let xp, yp, z;
	if (mem[0xA6] !== 0 || (mem[0xA7] & 0x80)) {
		const h = angle(0x2A), sh = sin10(h), ch = cos10(h);
		xp = X * ch - Y * sh;
		yp = -H;
		z = -(X * sh + Y * ch);
	}
	else {
		const r = angle(0x26), p = angle(0x28), h = angle(0x2A);
		const sr = sin10(r), cr = cos10(r), sp = sin10(p), cp = cos10(p), sh = sin10(h), ch = cos10(h);
		xp = X * (cr * ch + sr * sp * sh) + H * (-sr * cp) + Y * (-cr * sh + sr * sp * ch);
		yp = X * (sr * ch - cr * sp * sh) + H * (cr * cp) + Y * (-(sr * sh + cr * sp * ch));
		z = X * (cp * sh) + H * sp + Y * (cp * ch);
	}
	if (mem[0xF1] & 1)   // mirror flag
		xp = -xp;
	slots[mem[0x17]] = { xp, yp, z };
}

function beginGroup(key) {
	currentGroup = { key, edges: [], verts: new Map() };
	preparedGroups.push(currentGroup);
}

// The location's building: identified by its edge table
function beginBuildingGroup() {
	const count = mem[0x6F] + 1;
	let h = count;
	for (let i = 0; i < count; i++)
		h = (h * 31 + mem[BUILDING_EDGE_FROM + i] * 64 + mem[BUILDING_EDGE_TO + i]) >>> 0;
	beginGroup("b" + h.toString(16));
}

// An object: identified by its model
function beginObjectGroup() {
	const x = a8.cpu.x;
	beginGroup("o" + (mem[0x6800 + x] | (mem[0x6840 + x] << 8)).toString(16));
}

// LINE_SETUP: remember the edge between slots X and Y with its 3D end points.
// The game's integer projection is kept as a fallback for a slot whose float
// data is missing or does not agree with it.
function captureEdge() {
	const ends = [];
	for (const s of [a8.cpu.x, a8.cpu.y]) {
		const f = slots[s];
		const flags = mem[SLOT_FLAGS + s];
		const gx = mem[SLOT_SX + s], gy = mem[SLOT_SY + s];
		let ok = f !== undefined && (flags & 0x80 ? f.z <= Z_NEAR : f.z > 0);
		if (ok && !(flags & 0x83)) {
			// both projected it: they must agree to within the game's rounding
			const [sx, sy] = projectPoint(f);
			ok = Math.abs(sx - gx) <= 2 && Math.abs(sy - gy) <= 2;
		}
		ends.push(ok ? f : { flat: true, sx: gx, sy: gy, behind: (flags & 0x80) !== 0 });
	}
	const colourAnd = mem[0x5243] === 0x3D;
	if (preparedLines.length < MAX_LINES)
		preparedLines.push({ a: ends[0], b: ends[1], colourAnd });
	// structure edges (white) with full 3D data take part in face detection
	if (currentGroup !== null && colourAnd && !ends[0].flat && !ends[1].flat) {
		const sa = a8.cpu.x, sb = a8.cpu.y;
		currentGroup.edges.push([sa, sb]);
		currentGroup.verts.set(sa, ends[0]);
		currentGroup.verts.set(sb, ends[1]);
	}
}

/* ------------------------------ faces ------------------------------ */

// Finds the faces of a wireframe: chordless cycles of up to MAX_FACE_EDGES
// edges whose vertices are coplanar. Vertex positions are in view space;
// planarity does not depend on the view, so the result is cached per model.
const MAX_FACE_EDGES = 8;

function planeOf(pts) {
	// normal from the first non-degenerate triple, and the tolerance from the extent
	let n = null, extent = 0;
	for (let i = 0; i < pts.length && n === null; i++)
		for (let j = i + 1; j < pts.length && n === null; j++)
			for (let k = j + 1; k < pts.length && n === null; k++) {
				const a = pts[i], b = pts[j], c = pts[k];
				const ux = b.xp - a.xp, uy = b.yp - a.yp, uz = b.z - a.z;
				const vx = c.xp - a.xp, vy = c.yp - a.yp, vz = c.z - a.z;
				const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
				const len = Math.hypot(nx, ny, nz);
				if (len > 1e-6 * (1 + Math.hypot(ux, uy, uz) * Math.hypot(vx, vy, vz)))
					n = { x: nx / len, y: ny / len, z: nz / len, px: a.xp, py: a.yp, pz: a.z };
			}
	for (const p of pts) for (const q of pts) extent = Math.max(extent, Math.abs(p.xp - q.xp), Math.abs(p.yp - q.yp), Math.abs(p.z - q.z));
	return n === null ? null : { ...n, tol: Math.max(2, 0.01 * extent) };
}

function onPlane(plane, p) {
	return Math.abs((p.xp - plane.px) * plane.x + (p.yp - plane.py) * plane.y + (p.z - plane.pz) * plane.z) <= plane.tol;
}

function detectFaces(edges, verts) {
	const adj = new Map();
	for (const [u, v] of edges) {
		if (u === v) continue;
		if (!adj.has(u)) adj.set(u, new Set());
		if (!adj.has(v)) adj.set(v, new Set());
		adj.get(u).add(v); adj.get(v).add(u);
	}
	const faces = new Map();
	const consider = (cycle) => {
		const pts = cycle.map((s) => verts.get(s));
		const plane = planeOf(pts);
		if (plane === null || !pts.every((p) => onPlane(plane, p)))
			return;
		// no chord: an edge between two vertices that are not neighbours on the cycle
		for (let i = 0; i < cycle.length; i++)
			for (let j = i + 2; j < cycle.length; j++)
				if (!(i === 0 && j === cycle.length - 1) && adj.get(cycle[i]).has(cycle[j]))
					return;
		// canonical form: start at the smallest slot, smaller neighbour second
		let k = cycle.indexOf(Math.min(...cycle));
		let c = cycle.slice(k).concat(cycle.slice(0, k));
		if (c[1] > c[c.length - 1]) c = [c[0]].concat(c.slice(1).reverse());
		faces.set(c.join(","), c);
	};
	for (const [u, nbrs] of adj) {
		for (const v of nbrs) {
			if (u > v) continue;
			const path = [u, v], onPath = new Set(path);
			let plane = null;
			const dfs = (cur) => {
				if (path.length >= MAX_FACE_EDGES) return;
				for (const w of adj.get(cur)) {
					if (w === u && path.length >= 3) { consider(path.slice()); continue; }
					if (onPath.has(w)) continue;
					const p = verts.get(w);
					const savedPlane = plane;   // a plane fixed on this branch must not leak to its siblings
					if (plane === null) {
						const trial = planeOf(path.map((s) => verts.get(s)).concat([p]));
						if (trial !== null) plane = trial;   // three non-collinear points define the plane
					}
					else if (!onPlane(plane, p)) continue;
					path.push(w); onPath.add(w);
					dfs(w);
					path.pop(); onPath.delete(w);
					plane = savedPlane;
				}
			};
			dfs(v);
		}
	}
	return [...faces.values()];
}

function facesOf(group) {
	let faces = faceCache.get(group.key);
	if (faces === undefined) {
		faces = group.edges.length >= 3 ? detectFaces(group.edges, group.verts) : [];
		faceCache.set(group.key, faces);
	}
	return faces;
}

// Sutherland-Hodgman clip of a view-space polygon against the near plane
function clipPolygonNear(pts) {
	const out = [];
	for (let i = 0; i < pts.length; i++) {
		const a = pts[i], b = pts[(i + 1) % pts.length];
		const ain = a.z > Z_NEAR, bin = b.z > Z_NEAR;
		if (ain) out.push(a);
		if (ain !== bin) {
			const t = (Z_NEAR - a.z) / (b.z - a.z);
			out.push({ xp: a.xp + t * (b.xp - a.xp), yp: a.yp + t * (b.yp - a.yp), z: Z_NEAR });
		}
	}
	return out;
}

// Draws the faces of the shown groups as translucent polygons, far to near
function drawFaces(style) {
	const list = [];
	for (const g of shownGroups) {
		for (const face of facesOf(g)) {
			const pts = face.map((s) => g.verts.get(s)).filter((p) => p !== undefined);
			if (pts.length < 3) continue;
			const clipped = clipPolygonNear(pts);
			if (clipped.length < 3) continue;
			let zsum = 0;
			for (const p of clipped) zsum += p.z;
			// brightness from the face's orientation to the viewer, for the shaded style
			const plane = planeOf(pts);
			const facing = plane === null ? 1 : Math.abs(plane.z);
			list.push({ pts: clipped, z: zsum / clipped.length, facing });
		}
	}
	list.sort((p, q) => q.z - p.z);
	for (const f of list) {
		if (style === FACES_SHADED)
			gl.Color4f(0.35 + 0.5 * f.facing, 0.45 + 0.45 * f.facing, 0.6 + 0.4 * f.facing, 0.65);
		else
			gl.Color4f(0.55, 0.75, 1.0, 0.28);
		gl.Begin(gl.POLYGON);
		for (const p of f.pts) {
			const [sx, sy] = projectPoint(p);
			gl.Vertex3f(adjustX(sx), adjustY(sy), -2);
		}
		gl.End();
	}
}

// Restricts drawing to the game's 3D window (scissor in window pixels)
function scissorToView() {
	const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
	const px = (gx) => vx + (adjustX(gx) + 1) / 2 * vw;
	const py = (gy) => vy + (adjustY(gy) + 1) / 2 * vh;
	const x0 = Math.round(px(0)), x1 = Math.round(px(VIEW_W));
	const y0 = Math.round(py(VIEW_H)), y1 = Math.round(py(0));
	gl.Scissor(x0, y0, x1 - x0, y1 - y0);
	gl.Enable(gl.SCISSOR_TEST);
}

// View space to game pixels, like the game: x = centre + f * xp / z, y = centre + 2f * yp / z
function projectPoint(v) {
	const fe = mem[0x1F] >> 2;
	const focal = 2 ** (fe >= 32 ? fe - 64 : fe);
	return [mem[0x8C] + focal * v.xp / v.z, mem[0x8D] + 2 * focal * v.yp / v.z];
}

/* ------------------------------ OpenGL pass ------------------------------ */

// Clips the edge to the near plane and the view window and returns its end
// points in game pixels, or null when nothing is left
function edgeEndPoints(edge) {
	let a = edge.a, b = edge.b;
	if (a.flat || b.flat) {
		// at least one end only has the game's integer projection
		if (a.behind || b.behind) return null;
		const pa = a.flat ? [a.sx, a.sy] : projectPoint(a);
		const pb = b.flat ? [b.sx, b.sy] : projectPoint(b);
		if (!a.flat && a.z <= Z_NEAR || !b.flat && b.z <= Z_NEAR) return null;
		return clipToWindow(pa, pb);
	}
	if (a.z <= Z_NEAR && b.z <= Z_NEAR) return null;
	if (a.z <= Z_NEAR || b.z <= Z_NEAR) {
		// move the end behind the near plane onto it
		const t = (Z_NEAR - a.z) / (b.z - a.z);
		const p = { xp: a.xp + t * (b.xp - a.xp), yp: a.yp + t * (b.yp - a.yp), z: Z_NEAR };
		if (a.z <= Z_NEAR) a = p; else b = p;
	}
	return clipToWindow(projectPoint(a), projectPoint(b));
}

// Liang-Barsky clip of a segment to the view window (game pixels)
function clipToWindow([x0, y0], [x1, y1]) {
	const dx = x1 - x0, dy = y1 - y0;
	let t0 = 0, t1 = 1;
	for (const [p, q] of [[-dx, x0], [dx, VIEW_W - x0], [-dy, y0], [dy, VIEW_H - y0]]) {
		if (p === 0) { if (q < 0) return null; continue; }
		const r = q / p;
		if (p < 0) { if (r > t1) return null; if (r > t0) t0 = r; }
		else { if (r < t0) return null; if (r < t1) t1 = r; }
	}
	return [[x0 + t0 * dx, y0 + t0 * dy], [x0 + t1 * dx, y0 + t1 * dy]];
}

function drawGlLine(edge, type) {
	const pts = edgeEndPoints(edge);
	if (pts === null)
		return;
	let sx = adjustX(pts[0][0]), ex = adjustX(pts[1][0]);
	let sy = adjustY(pts[0][1]), ey = adjustY(pts[1][1]);

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

export default {
	name: "MERCENARY JS HACK by ERU",

	fingerprint: { address: 0x4000, bytes: [0xA6, 0x65, 0xBC, 0x57, 0x6B] },

	menu: {
		FPS: { label: "Display FPS:", options: ["OFF", "ON"], current: 1 },
		ACCEL: { label: "Accelerate:", options: ["OFF", "ON"], current: 1 },
		LINES: { label: "Line drawing mode:", options: ["Atari native", "OpenGL", "Both"], current: MODE_GL },
		GLTYPE: { label: "GL line type:", options: ["Line", "Polygon-Line", "Polygon-Fill"], current: TYPE_LINE },
		FACES: { label: "Faces:", options: ["OFF", "Glass", "Shaded"], current: FACES_GLASS },
	},

	// The C version was consulted on every instruction; listing the
	// addresses keeps the script out of the CPU loop everywhere else
	codeInjections: [
		...SKIPPED_ROUTINES,
		...LINE_ROUTINES.map((r) => r[0]),
		FILL_ONE_COLOUR,
		FILL_TWO_COLOURS,
		VERTEX_REL, MODEL_VERTEX_ORIENTED, SQUARE_CENTRE_REL, PROJECT_VERTEX, LINE_SETUP,
		BUILDING_EDGES, DRAW_OBJECT,
	],

	onActivate() {
		shownLines = [];
		preparedLines = [];
		shownGroups = [];
		preparedGroups = [];
		currentGroup = null;
		shownDl = -1;
		pendingVertex = null;
		slots.fill(undefined);
	},

	onCodeInjection(pc, op) {
		// The geometry is captured whatever the settings; it is what the OpenGL pass draws
		switch (pc) {
		case VERTEX_REL: captureBuildingVertex(); return op;
		case MODEL_VERTEX_ORIENTED: captureModelVertex(); return op;
		case SQUARE_CENTRE_REL: captureSquareCentre(); return op;
		case PROJECT_VERTEX: captureProjection(); return op;
		case BUILDING_EDGES: beginBuildingGroup(); return op;
		case DRAW_OBJECT: beginObjectGroup(); return op;
		case LINE_SETUP:
			captureEdge();
			// In OpenGL-only mode the game need not draw the edge at all
			return this.menu.LINES.current === MODE_GL && !a8.accelerationDisabled() ? a8.OP_RTS : op;
		}

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
			[shownGroups, preparedGroups] = [preparedGroups, shownGroups];
			preparedLines.length = 0;
			preparedGroups.length = 0;
			currentGroup = null;
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
		gl.PushAttrib(gl.SCISSOR_BIT);
		gl.Disable(gl.TEXTURE_2D);
		gl.Disable(gl.BLEND);
		scissorToView();

		const faces = this.menu.FACES.current;
		if (faces !== FACES_OFF) {
			gl.Enable(gl.BLEND);
			gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
			gl.Disable(gl.CULL_FACE);
			gl.PolygonMode(gl.FRONT_AND_BACK, gl.FILL);
			drawFaces(faces);
			gl.Disable(gl.BLEND);
		}

		for (const line of shownLines) {
			// The current drawing colours seem to live in $A1 (AND lines) and $A4 (OR lines)
			const [r, g, b] = rgb(line.colourAnd ? mem[0xA1] : mem[0xA4]);
			gl.Color4f(r, g, b, 1);
			drawGlLine(line, type);
		}

		gl.PopAttrib();
		gl.PopAttrib();
		gl.PopAttrib();
		gl.PopAttrib();
		gl.Color4f(1, 1, 1, 1);
	},
};
