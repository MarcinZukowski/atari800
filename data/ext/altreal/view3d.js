// Alternate Reality: The Dungeon - the maze drawn with OpenGL instead of the
// game's character-mode picture: the game's own wall art as textures on
// walls, doors and arches, floor and ceiling in the game's colours, fog and
// side shading, with steps and turns interpolated between the game's discrete
// positions.
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

// The picture rectangle in GL screen coordinates (336 x 240 Atari pixels)
const PIC_LEFT = (8 + 11 * 8 - 168) / 168, PIC_RIGHT = (8 + 29 * 8 - 168) / 168;
const PIC_TOP = (120 - 73) / 120, PIC_BOTTOM = (120 - 145) / 120;
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

// The art for a wall nibble, as the game chooses it ($7917-$793C): the table
// entry, with types 5 and 6 (secret doors) shown as 3 and 4 when $1957 says so
function artPointer(nibble) {
	let i = nibble;
	if ((mem[SECRET_DOORS_SHOWN] & 0x80) && (i === 5 || i === 6))
		i -= 2;
	return mem[ART_TABLE_LO + i] | mem[ART_TABLE_HI + i] << 8;
}

// The pixel values of the 72 x 72 picture at ptr, one byte each
function artPixels(ptr) {
	const out = new Uint8Array(ART_SIZE * ART_SIZE);
	let src = ptr + ART_HEADER, o = 0;
	for (let y = 0; y < ART_SIZE; y++, src += ART_STRIDE)
		for (let bx = 0; bx < ART_STRIDE; bx++) {
			const b = mem[src + bx];
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

// A texture from the 72 x 72 picture at ptr, with colours[v] (0x00RRGGBB) for
// pixel value v. Value 0, the background, is made transparent: the game
// shows the background colour through an arch, here what lies beyond shows.
// Smooth: Scale2x twice, 288 x 288, drawn with mipmaps and linear filtering;
// otherwise the art as is, with nearest sampling like the game's scaling.
function artTexture(ptr, colours, smooth) {
	let values = artPixels(ptr), size = ART_SIZE;
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

// The textures follow memory: a cheap key over the colours, the table and
// the pictures themselves says when they must be rebuilt (the game flashes
// the colours, and could load other art); the smoothing option is part of it
function artKey(smooth) {
	let sum = 0;
	const ptrs = [];
	for (let i = 1; i < 16; i++) {
		const p = mem[ART_TABLE_LO + i] | mem[ART_TABLE_HI + i] << 8;
		if (p === 0 || ptrs.includes(p)) continue;
		ptrs.push(p);
		for (let a = p + ART_HEADER, end = a + ART_BYTES; a < end; a++)
			sum = (sum * 31 + mem[a]) | 0;
	}
	return `${smooth ? "s" : "o"}|${mem[COLOUR_PF0]},${mem[COLOUR_PF1]},${mem[COLOUR_PF2]},${mem[COLOUR_CEILING]}|${ptrs}|${sum}`;
}

// All the art the table points to, keyed by pointer
function artTextures(smooth) {
	const colours = [mem[COLOUR_CEILING], mem[COLOUR_PF0], mem[COLOUR_PF1], mem[COLOUR_PF2]].map((c) => a8.palette[c]);
	const art = new Map();
	for (let i = 1; i < 16; i++) {
		const p = mem[ART_TABLE_LO + i] | mem[ART_TABLE_HI + i] << 8;
		if (p !== 0 && !art.has(p))
			art.set(p, artTexture(p, colours, smooth));
	}
	return art;
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

/* ------------------------------ map access ------------------------------ */

function cellWalls(x, y) {
	if (x < 0 || x > 31 || y < 0 || y > 31)
		return [13, 13, 13, 13];
	const a = MAP + y * 128 + x * 4;
	const b0 = mem[a], b1 = mem[a + 1];
	return [b0 & 0xF, b0 >> 4, b1 & 0xF, b1 >> 4];   // N, E, S, W
}

/* ------------------------------ projection ------------------------------ */

// Half-height in picture lines of a wall at distance d along the view
function halfHeight(d) {
	const c = d / CELL;
	if (c >= VIEW_RANGE) return 0;
	if (c <= 0) return MAX_HALF_HEIGHT - c * DEPTH_AT_CELL[1];   // behind the eye: the first segment continued
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

function drawWalls(art, cx, cy) {
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
				const tex = art.get(artPointer(s[0]));
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

/* ------------------------------ the view ------------------------------ */

export function createView3D() {
	let art = null, key = null, planes = null;
	// Interpolation state; tracking is false until a frame has been drawn
	// from the current position, so the first frame after the view was off
	// starts where the player is instead of sweeping there
	let tracking = false;
	let prevX = 0, prevZ = 0, prevYaw = 0;
	let curX = 0, curZ = 0, curYaw = 0;
	let moveFrames = 0, turnFrames = 0;

	return {
		// Settings the menu changes
		options: { smoothTextures: true },

		// Call when a frame goes by without render(): the next one starts fresh
		reset() { tracking = false; },

		// Call every frame; draws when the game shows the maze and no monster
		// is present (the game draws monsters into its own picture).
		render(movesPerSecond) {
			if (a8.antic.dlist !== 0x19BE || mem[0x1938] !== 0) {
				tracking = false;
				return false;
			}
			const k = artKey(this.options.smoothTextures);
			if (k !== key) { art = artTextures(this.options.smoothTextures); key = k; }
			if (planes === null)
				planes = makePlanes();

			const cx = mem[0x6313], cy = mem[0x6314], facing = mem[0x6312];
			const x = cx * CELL + mem[0x6316] + 0.5, z = cy * CELL + mem[0x6317] + 0.5;
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
			if (x !== curX || z !== curZ) { prevX = curX; prevZ = curZ; curX = x; curZ = z; moveFrames = 0; }
			if (yaw !== curYaw) { prevYaw = curYaw; curYaw = yaw; turnFrames = 0; }
			const moveT = Math.min(1, ++moveFrames / Math.max(1, 60 / Math.max(1, movesPerSecond)));
			const turnT = Math.min(1, ++turnFrames / 12);
			const ease = (t) => t * t * (3 - 2 * t);
			eye.x = prevX + (curX - prevX) * ease(moveT);
			eye.z = prevZ + (curZ - prevZ) * ease(moveT);
			let dyaw = curYaw - prevYaw;
			if (dyaw > 180) dyaw -= 360; else if (dyaw < -180) dyaw += 360;
			const eyeYaw = (prevYaw + dyaw * ease(turnT)) * Math.PI / 180;
			eye.sin = Math.sin(eyeYaw); eye.cos = Math.cos(eyeYaw);

			// Viewport: the game's picture rectangle
			const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
			const px0 = Math.round(vx + (PIC_LEFT + 1) / 2 * vw), px1 = Math.round(vx + (PIC_RIGHT + 1) / 2 * vw);
			const py0 = Math.round(vy + (PIC_BOTTOM + 1) / 2 * vh), py1 = Math.round(vy + (PIC_TOP + 1) / 2 * vh);
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
			drawWalls(art, cx, cy);

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
