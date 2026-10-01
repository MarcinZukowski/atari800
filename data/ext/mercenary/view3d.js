// Mercenary - the 3D window drawn as a scene in OpenGL: the vertices the
// game computed are kept in view space (see init.js), so with a frustum that
// matches the game's projection everything can be drawn as real geometry:
// the ground as a plane to the horizon and the sky behind it instead of the
// game's row-by-row fill, edges as ribbons that get thinner with distance,
// faces lit by a fixed sun, a light fog, and a grain over ground and faces.
import { rgb } from "../common.js";

const mem = a8.mem;

// The 3D window: 160 x 152 game pixels (two screen pixels wide each) at (8, 24) of the 336 x 240 screen
const VIEW_W = 160, VIEW_H = 152, SCREEN_W = 336, SCREEN_H = 240, VIEW_LEFT = 8, VIEW_TOP = 24, PIXEL_W = 2;
const NEAR = 8, FAR = 4e8;
const Z_NEAR = 16;   // edges and faces are cut here, in world units (65536 per city square)
// The view's colours: pixel values 0-3 of the window are these registers' shadows
const COLOUR_STRUCTURE = 0xA1, COLOUR_SKY = 0xA2, COLOUR_GROUND = 0xA3, COLOUR_MARK = 0xA4;

// Lines: an edge is a ribbon LINE_THICKNESS world units wide, but never
// thinner or wider on the screen than these (window pixels)
const LINE_THICKNESS = 22, LINE_MIN = 1.6, LINE_MAX = 4.2, LINE_FIXED = 3.2, DOT_SIZE = 2.5;
// The ground: rings around the point under the eye, each twice as wide as
// the last, so that fog and textures are sampled finely near and coarsely far
const GROUND_SEGMENTS = 24, GROUND_GROWTH = 2, GROUND_REACH = 6e7;
// The grain: one tiling noise texture drawn at several sizes (world units a
// tile), each pass multiplying what is there by twice the texture, whose
// mean is a half: far away, where it blurs to its mean, it does nothing
const GRAIN_SIZE = 256, GRAIN_DEPTH = 0.07, FACE_GRAIN_DEPTH = 0.04, GRAIN_REACH = 48;
const GROUND_TILES = [512, 8192, 131072, 2097152], FACE_TILE = 768;
// Fog: per world unit; outdoors toward a paled sky, indoors toward the dark
const FOG_OUTDOORS = 0.0000009, FOG_INDOORS = 0.00012, FOG_WHITE = 0.3, FOG_DARK = 0.55;
const HAZE = [[0, 0.5], [0.05, 0.22], [0.16, 0]];        // over the sky: [tangent of the elevation, how thick]
const ZENITH = [[0.16, 0], [1.2, 0.2], [20, 0.26]];      // and the sky darkens upward
const SUN = [0.42, 0.8, 0.43];                           // the light's direction as (X, height, Y), unit length
const AMBIENT = 0.86, DIFFUSE = 0.24, FLOOR = 0.9, CEILING = 1.06;

const dot3 = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

// World (eye-relative X, height, Y) to view space, with the frame's matrix
export function toView(view, X, H, Y) {
	const m = view.m;
	return { xp: m[0][0] * X + m[0][1] * H + m[0][2] * Y, yp: m[1][0] * X + m[1][1] * H + m[1][2] * Y, z: m[2][0] * X + m[2][1] * H + m[2][2] * Y };
}
// The height above the eye of a view-space point (the matrix is orthonormal)
const heightOf = (view, p) => view.m[0][1] * p.xp + view.m[1][1] * p.yp + view.m[2][1] * p.z;
const vertex = (p) => gl.Vertex3f(p.xp, -p.yp, -p.z);
const mix = (a, b, t) => ({ xp: a.xp + t * (b.xp - a.xp), yp: a.yp + t * (b.yp - a.yp), z: a.z + t * (b.z - a.z), w: a.w && b.w ? [a.w[0] + t * (b.w[0] - a.w[0]), a.w[1] + t * (b.w[1] - a.w[1]), a.w[2] + t * (b.w[2] - a.w[2])] : undefined });

/* ------------------------------ the grain ------------------------------ */

const grains = new Map();   // by depth
function grainTexture(depth) {
	if (grains.has(depth)) return grains.get(depth);
	let seed = 0x4D455243;
	const random = () => { seed = (seed + 0x6D2B79F5) | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
	const noise = new Float32Array(GRAIN_SIZE * GRAIN_SIZE);
	for (const [cells, weight] of [[8, 0.4], [32, 0.35], [128, 0.25]]) {   // value noise in three sizes, tiling
		const lattice = Float32Array.from({ length: cells * cells }, random), step = GRAIN_SIZE / cells;
		const ease = (t) => t * t * (3 - 2 * t), at = (i, j) => lattice[(j % cells) * cells + i % cells];
		for (let y = 0; y < GRAIN_SIZE; y++) for (let x = 0; x < GRAIN_SIZE; x++) {
			const cx = Math.floor(x / step), cy = Math.floor(y / step), fx = ease(x / step - cx), fy = ease(y / step - cy);
			noise[y * GRAIN_SIZE + x] += weight * ((at(cx, cy) * (1 - fx) + at(cx + 1, cy) * fx) * (1 - fy) + (at(cx, cy + 1) * (1 - fx) + at(cx + 1, cy + 1) * fx) * fy);
		}
	}
	let mean = 0;
	for (const v of noise) mean += v / noise.length;
	const grain = gl.createTexture(GRAIN_SIZE, GRAIN_SIZE);
	const px = grain.pixels;
	for (let i = 0, o = 0; i < noise.length; i++, o += 4) {
		px[o] = px[o + 1] = px[o + 2] = Math.max(0, Math.min(255, 127.5 + 255 * depth * (noise[i] - mean) * 2));
		px[o + 3] = 255;
	}
	gl.BindTexture(gl.TEXTURE_2D, grain.id); gl.TexParameteri(gl.TEXTURE_2D, gl.GENERATE_MIPMAP, gl.TRUE);
	grain.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT); gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR); gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
	grains.set(depth, grain);
	return grain;
}
// Starts and ends a pass that multiplies the picture by twice the grain
function grainPass(on, depth) {
	if (on) {
		gl.Enable(gl.TEXTURE_2D); gl.BindTexture(gl.TEXTURE_2D, grainTexture(depth).id);
		gl.Enable(gl.BLEND); gl.BlendFunc(gl.DST_COLOR, gl.SRC_COLOR);
		gl.Disable(gl.FOG); gl.Color4f(1, 1, 1, 1);
	}
	else { gl.Disable(gl.TEXTURE_2D); gl.Disable(gl.BLEND); }
}

/* ------------------------------ sky and ground ------------------------------ */

// The directions around the eye that can be in the picture: all of them when
// looking steeply up or down, else those within a right angle and a bit of the heading
function segmentsInView(view) {
	const fx = view.m[2][0], fy = view.m[2][2], level = Math.hypot(fx, fy);
	const out = [];
	for (let s = 0; s < GROUND_SEGMENTS; s++) {
		const mid = (s + 0.5) / GROUND_SEGMENTS * 2 * Math.PI;
		if (level < 0.75 || (Math.cos(mid) * fx + Math.sin(mid) * fy) / level > -0.25) out.push(s);
	}
	return out;
}

function drawGround(view, colour, textured, fogged) {
	const [ex, eh, ey] = view.eye, h = Math.max(eh, 4);
	const radii = [0];
	for (let r = h / 2; r < GROUND_REACH; r *= GROUND_GROWTH) radii.push(r);
	const segments = segmentsInView(view);
	const cos = [], sin = [];
	for (let s = 0; s <= GROUND_SEGMENTS; s++) { cos.push(Math.cos(s / GROUND_SEGMENTS * 2 * Math.PI)); sin.push(Math.sin(s / GROUND_SEGMENTS * 2 * Math.PI)); }
	// the corners in view space, once for all passes
	const corner = radii.map((r) => cos.map((c, s) => toView(view, r * c, -h, r * sin[s])));
	const pass = (tile) => {
		const ux = tile ? ex % tile : 0, uy = tile ? ey % tile : 0;
		const at = (ring, s) => {
			if (tile) gl.TexCoord2f((ux + radii[ring] * cos[s]) / tile, (uy + radii[ring] * sin[s]) / tile);
			vertex(corner[ring][s]);
		};
		gl.Begin(gl.QUADS);
		for (let ring = 0; ring + 1 < radii.length; ring++) {
			if (tile && radii[ring] > tile * GRAIN_REACH) break;   // from here on the grain is its mean
			for (const s of segments) { at(ring, s); at(ring + 1, s); at(ring + 1, s + 1); at(ring, s + 1); }
		}
		gl.End();
	};
	if (fogged) gl.Enable(gl.FOG);
	gl.Color4f(colour[0], colour[1], colour[2], 1);
	pass(0);
	if (textured) {
		grainPass(true, GRAIN_DEPTH);
		for (const tile of GROUND_TILES) pass(tile);
		grainPass(false);
	}
}

// Bands around the horizon, as parts of a far cylinder around the eye: [tangent of the elevation, alpha] steps of one colour
function drawBands(view, bands, colour) {
	const R = 1e6, segments = segmentsInView(view);
	gl.Enable(gl.BLEND); gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
	gl.Begin(gl.QUADS);
	for (let i = 0; i + 1 < bands.length; i++) {
		const [t0, a0] = bands[i], [t1, a1] = bands[i + 1];
		for (const s of segments) {
			const p0 = s / GROUND_SEGMENTS * 2 * Math.PI, p1 = (s + 1) / GROUND_SEGMENTS * 2 * Math.PI;
			gl.Color4f(colour[0], colour[1], colour[2], a0);
			vertex(toView(view, R * Math.cos(p0), R * t0, R * Math.sin(p0))); vertex(toView(view, R * Math.cos(p1), R * t0, R * Math.sin(p1)));
			gl.Color4f(colour[0], colour[1], colour[2], a1);
			vertex(toView(view, R * Math.cos(p1), R * t1, R * Math.sin(p1))); vertex(toView(view, R * Math.cos(p0), R * t1, R * Math.sin(p0)));
		}
	}
	gl.End();
	gl.Disable(gl.BLEND);
}

/* ------------------------------ edges ------------------------------ */

// Cuts the segment a-b to the side of a plane where f is positive; null when nothing is left
function cut(a, b, f) {
	const fa = f(a), fb = f(b);
	if (fa <= 0 && fb <= 0) return null;
	if (fa > 0 && fb > 0) return [a, b];
	const p = mix(a, b, fa / (fa - fb));
	return fa > 0 ? [a, p] : [p, b];
}

// An edge as a ribbon facing the eye; its half width at depth z in world units
function ribbon(a, b, halfWidth) {
	const dx = b.xp - a.xp, dy = b.yp - a.yp, dz = b.z - a.z, length = Math.hypot(dx, dy, dz);
	if (length === 0) return;
	const mx = (a.xp + b.xp) / 2, my = (a.yp + b.yp) / 2, mz = (a.z + b.z) / 2;
	let sx = dy * mz - dz * my, sy = dz * mx - dx * mz, sz = dx * my - dy * mx;   // across: square to the edge and to the line of sight
	const sl = Math.hypot(sx, sy, sz);
	if (sl < 1e-9 * length * Math.hypot(mx, my, mz)) return;   // seen end on
	sx /= sl; sy /= sl; sz /= sl;
	const wa = halfWidth(a.z), wb = halfWidth(b.z);
	// a little longer at both ends, so that edges meet at the corners
	const ax = a.xp - dx / length * wa, ay = a.yp - dy / length * wa, az = Math.max(a.z - dz / length * wa, Z_NEAR / 2);
	const bx = b.xp + dx / length * wb, by = b.yp + dy / length * wb, bz = Math.max(b.z + dz / length * wb, Z_NEAR / 2);
	gl.Vertex3f(ax - sx * wa, -(ay - sy * wa), -(az - sz * wa)); gl.Vertex3f(ax + sx * wa, -(ay + sy * wa), -(az + sz * wa));
	gl.Vertex3f(bx + sx * wb, -(by + sy * wb), -(bz + sz * wb)); gl.Vertex3f(bx - sx * wb, -(by - sy * wb), -(bz - sz * wb));
}

/* ------------------------------ faces ------------------------------ */

// Sutherland-Hodgman clip of a view-space polygon against the near plane
function clipPolygonNear(pts) {
	const out = [];
	for (let i = 0; i < pts.length; i++) {
		const a = pts[i], b = pts[(i + 1) % pts.length];
		const ain = a.z > Z_NEAR, bin = b.z > Z_NEAR;
		if (ain) out.push(a);
		if (ain !== bin) out.push(mix(a, b, (Z_NEAR - a.z) / (b.z - a.z)));
	}
	return out;
}

// The unit normal of a polygon given in world coordinates (X, height, Y); null when it has no area
function worldNormal(pts) {
	const n = [0, 0, 0];
	for (let i = 0; i < pts.length; i++) {   // Newell's method
		const a = pts[i].w, b = pts[(i + 1) % pts.length].w;
		n[0] += (a[1] - b[1]) * (a[2] + b[2]); n[1] += (a[2] - b[2]) * (a[0] + b[0]); n[2] += (a[0] - b[0]) * (a[1] + b[1]);
	}
	const l = Math.hypot(n[0], n[1], n[2]);
	return l === 0 ? null : [n[0] / l, n[1] / l, n[2] / l];
}

/* ------------------------------ the scene ------------------------------ */

// frame: { view, lines, points, faces: [[points with xp, yp, z, w], ...] };
// options: scenery (sky and ground drawn here), taper (line width by
// distance), textures, shade (fog and lighting), glass or shaded faces
export function drawScene(frame, options) {
	const view = frame.view;
	const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
	const x0 = Math.round(vx + VIEW_LEFT / SCREEN_W * vw), x1 = Math.round(vx + (VIEW_LEFT + VIEW_W * PIXEL_W) / SCREEN_W * vw);
	const y0 = Math.round(vy + (1 - (VIEW_TOP + VIEW_H) / SCREEN_H) * vh), y1 = Math.round(vy + (1 - VIEW_TOP / SCREEN_H) * vh);
	const sky = rgb(mem[COLOUR_SKY]), ground = rgb(mem[COLOUR_GROUND]), structure = rgb(mem[COLOUR_STRUCTURE]), mark = rgb(mem[COLOUR_MARK]);
	const outdoors = !view.indoor;
	const fog = outdoors ? sky.map((c) => FOG_WHITE + (1 - FOG_WHITE) * c) : ground.map((c) => c * FOG_DARK);

	gl.PushAttrib(gl.ALL_ATTRIB_BITS);
	gl.Viewport(x0, y0, x1 - x0, y1 - y0);
	gl.Scissor(x0, y0, x1 - x0, y1 - y0);
	gl.Enable(gl.SCISSOR_TEST);
	gl.Disable(gl.TEXTURE_2D); gl.Disable(gl.BLEND); gl.Disable(gl.DEPTH_TEST); gl.Disable(gl.CULL_FACE); gl.Disable(gl.LIGHTING); gl.Disable(gl.FOG);
	gl.PolygonMode(gl.FRONT_AND_BACK, gl.FILL);
	// The game's projection as a frustum: x = centre + focal * x'/z, y = centre + 2 focal * y'/z,
	// a pixel's coordinate being its middle
	const cx = view.cx + 0.5, cy = view.cy + 0.5, f = view.focal;
	gl.MatrixMode(gl.PROJECTION); gl.PushMatrix(); gl.LoadIdentity();
	gl.Frustum(-cx / f * NEAR, (VIEW_W - cx) / f * NEAR, -(VIEW_H - cy) / (2 * f) * NEAR, cy / (2 * f) * NEAR, NEAR, FAR);
	gl.MatrixMode(gl.MODELVIEW); gl.PushMatrix(); gl.LoadIdentity();
	if (options.shade) { gl.Fogf(gl.FOG_MODE, gl.EXP); gl.Fogf(gl.FOG_DENSITY, outdoors ? FOG_OUTDOORS : FOG_INDOORS); gl.Fogfv(gl.FOG_COLOR, [fog[0], fog[1], fog[2], 1]); }

	if (options.scenery) {
		// Indoors the game's picture is one colour; outdoors sky above a ground that ends at the horizon
		const back = outdoors ? sky : ground;
		gl.ClearColor(back[0], back[1], back[2], 1);
		gl.Clear(gl.COLOR_BUFFER_BIT);
		if (outdoors) {
			if (options.shade) { drawBands(view, ZENITH, [0, 0, 0]); drawBands(view, HAZE, fog); }
			drawGround(view, ground, options.textures, options.shade);
			gl.Disable(gl.FOG);
		}
	}

	// The width of a line: so many world units, within limits on the screen
	const perPixel = 1 / (f * (x1 - x0) / VIEW_W);   // world units a window pixel covers at depth 1
	const halfWidth = options.taper
		? (z) => Math.max(LINE_MIN * perPixel * z, Math.min(LINE_MAX * perPixel * z, LINE_THICKNESS)) / 2
		: (z) => LINE_FIXED * perPixel * z / 2;
	const near = (p) => p.z - Z_NEAR;
	const below = (p) => -heightOf(view, p);   // under the eye's level: on the screen, under the horizon
	const drawLines = (lines, colour) => {
		if (options.shade) gl.Enable(gl.FOG);
		gl.Color4f(colour[0], colour[1], colour[2], 1);
		gl.Begin(gl.QUADS);
		for (const line of lines) {
			if (line.a.flat || line.b.flat) continue;
			let ends = cut(line.a, line.b, near);
			// The game draws its ground marks by setting a bit that the sky's pixels already have:
			// they show on the ground only. Here they are cut at the horizon
			if (ends !== null && outdoors && !line.colourAnd) ends = cut(ends[0], ends[1], below);
			if (ends !== null) ribbon(ends[0], ends[1], halfWidth);
		}
		gl.End();
		gl.Disable(gl.FOG);
	};

	if (outdoors) drawLines(frame.lines.filter((l) => !l.colourAnd), mark);

	// The faces, far to near: glass over what is behind; a room's walls solid in the room's colour
	const list = [];
	for (const face of frame.faces) {
		const clipped = clipPolygonNear(face.points);
		if (clipped.length < 3) continue;
		const normal = worldNormal(face.points);
		if (normal === null) continue;
		let z = 0, height = 0;
		for (const p of clipped) { z += p.z / clipped.length; height += p.w[1] / clipped.length; }
		list.push({ points: clipped, normal, z, height, room: face.room });
	}
	list.sort((p, q) => q.z - p.z);
	if (options.shade) gl.Enable(gl.FOG);
	for (const face of list) {
		const lit = options.shade ? AMBIENT + DIFFUSE * Math.abs(dot3(face.normal, SUN)) : 1;
		if (face.room) {
			// floor darker than the walls, the ceiling lighter
			const level = Math.abs(face.normal[1]) > 0.7 ? (face.height < 0 ? FLOOR : CEILING) : lit;
			const shade = options.shade ? level : 1;
			gl.Disable(gl.BLEND);
			gl.Color4f(Math.min(1, ground[0] * shade), Math.min(1, ground[1] * shade), Math.min(1, ground[2] * shade), 1);
		}
		else {
			gl.Enable(gl.BLEND); gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
			if (options.faces === 2) gl.Color4f(0.85 * lit, 0.9 * lit, 1.0 * lit, 0.65);
			else gl.Color4f(0.55 * lit, 0.75 * lit, Math.min(1, 1.0 * lit), 0.28);
		}
		gl.Begin(gl.POLYGON);
		for (const p of face.points) vertex(p);
		gl.End();
		if (options.textures) {
			// the grain, laid along the face: by X and Y on a level face, along the wall and up it otherwise
			const n = face.normal, flat = Math.abs(n[1]) > 0.7, alongX = Math.abs(n[2]) >= Math.abs(n[0]);
			const across = (p) => flat || alongX ? p.w[0] + view.eye[0] : p.w[2] + view.eye[2], up = (p) => flat ? p.w[2] + view.eye[2] : p.w[1] + view.eye[1];
			const first = face.points[0], u0 = Math.floor(across(first) / FACE_TILE), v0 = Math.floor(up(first) / FACE_TILE);   // (small numbers for the texture)
			const u = (p) => across(p) / FACE_TILE - u0, v = (p) => up(p) / FACE_TILE - v0;
			grainPass(true, FACE_GRAIN_DEPTH);
			gl.Begin(gl.POLYGON);
			for (const p of face.points) { gl.TexCoord2f(u(p), v(p)); vertex(p); }
			gl.End();
			grainPass(false);
			if (options.shade) gl.Enable(gl.FOG);
		}
	}
	gl.Disable(gl.BLEND); gl.Disable(gl.FOG);

	if (!outdoors) drawLines(frame.lines.filter((l) => !l.colourAnd), mark);
	drawLines(frame.lines.filter((l) => l.colourAnd), structure);

	// Far objects are single dots
	if (frame.points.length) {
		if (options.shade) gl.Enable(gl.FOG);
		gl.Color4f(structure[0], structure[1], structure[2], 1);
		gl.Begin(gl.QUADS);
		for (const p of frame.points) {
			if (p.flat || p.z <= Z_NEAR) continue;
			const half = DOT_SIZE * perPixel * p.z / 2;
			gl.Vertex3f(p.xp - half, -p.yp - half, -p.z); gl.Vertex3f(p.xp + half, -p.yp - half, -p.z);
			gl.Vertex3f(p.xp + half, -p.yp + half, -p.z); gl.Vertex3f(p.xp - half, -p.yp + half, -p.z);
		}
		gl.End();
		gl.Disable(gl.FOG);
	}

	// What only has the game's own projection (rare): plain lines and dots in the window's pixels
	gl.MatrixMode(gl.PROJECTION); gl.LoadIdentity(); gl.Ortho(0, VIEW_W, VIEW_H, 0, -1, 1);
	const pixel = (p) => p.flat ? [p.sx + 0.5, p.sy + 0.5] : p.z > Z_NEAR ? [cx + f * p.xp / p.z, cy + 2 * f * p.yp / p.z] : null;
	gl.LineWidth(LINE_FIXED);
	for (const line of frame.lines) {
		if (!line.a.flat && !line.b.flat) continue;
		if (line.a.behind || line.b.behind) continue;
		const a = pixel(line.a), b = pixel(line.b);
		if (a === null || b === null) continue;
		const colour = line.colourAnd ? structure : mark;
		gl.Color4f(colour[0], colour[1], colour[2], 1);
		gl.Begin(gl.LINES); gl.Vertex3f(a[0], a[1], 0); gl.Vertex3f(b[0], b[1], 0); gl.End();
	}
	gl.Color4f(structure[0], structure[1], structure[2], 1);
	gl.Begin(gl.QUADS);
	for (const p of frame.points) {
		if (!p.flat) continue;
		gl.Vertex3f(p.sx, p.sy, 0); gl.Vertex3f(p.sx + 1, p.sy, 0); gl.Vertex3f(p.sx + 1, p.sy + 1, 0); gl.Vertex3f(p.sx, p.sy + 1, 0);
	}
	gl.End();

	gl.MatrixMode(gl.PROJECTION); gl.PopMatrix();
	gl.MatrixMode(gl.MODELVIEW); gl.PopMatrix();
	gl.PopAttrib();
	gl.Viewport(vx, vy, vw, vh);
	gl.Color4f(1, 1, 1, 1);
}
