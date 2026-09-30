// Alternate Reality: The Dungeon - the maze drawn with OpenGL instead of the
// game's character-mode picture: textured walls, doors, floor and ceiling,
// fog and side shading, with steps and turns interpolated between the game's
// discrete positions.
//
// The level is 32 x 32 cells of 4 bytes at $B000 + y*128 + x*4: two bytes of
// wall nibbles (north, east, south, west), a cell type and a flags byte.
// Nibble 0 is open, 13 a wall, 1/3/5/6 doors, 8-10 locked doors. A cell is
// 36 units on a side; the player is at cell ($6313, $6314) and position
// ($6316, $6317) inside it, facing $6312 (0 north, 1 east, 2 south, 3 west).
// The game's picture occupies the middle 18 text columns of the nine mode-4
// rows: 144 colour clocks by 72 lines, a 2:1 window.

const MAP = 0xB000, CELL = 36, EYE_HEIGHT = 18;
const VIEW_RANGE = 12;                 // cells drawn around the player
const WALL = 13;
const isDoor = (n) => n >= 1 && n <= 10 && n !== WALL;
const isLocked = (n) => n >= 8 && n <= 10;

// The picture rectangle in GL screen coordinates (336 x 240 Atari pixels)
const PIC_LEFT = (8 + 11 * 8 - 168) / 168, PIC_RIGHT = (8 + 29 * 8 - 168) / 168;
const PIC_TOP = (120 - 73) / 120, PIC_BOTTOM = (120 - 145) / 120;
// The game draws its 2:1 window as if it were square (its pixels are twice as
// wide as tall), so the vertical field of view is as large as the horizontal
const H_FOV = 72 * Math.PI / 180;
const V_FOV = 80 * Math.PI / 180;

const mem = a8.mem;

/* ------------------------------ textures ------------------------------ */

// Deterministic noise so that textures are stable between runs
function noise(x, y, seed) {
	let h = (x * 374761393 + y * 668265263 + seed * 2246822519) >>> 0;
	h = ((h ^ (h >>> 13)) * 1274126177) >>> 0;
	return ((h ^ (h >>> 16)) & 0xffff) / 0xffff;
}

// Builds a size x size RGBA texture from f(x, y) -> [r, g, b] in 0..1
function makeTexture(size, f) {
	const t = gl.createTexture(size, size);
	const px = t.pixels;
	for (let y = 0; y < size; y++)
		for (let x = 0; x < size; x++) {
			const [r, g, b] = f(x, y);
			const o = 4 * (y * size + x);
			px[o] = r * 255; px[o + 1] = g * 255; px[o + 2] = b * 255; px[o + 3] = 255;
		}
	t.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
	return t;
}

function makeTextures() {
	const S = 64;
	// Stone blocks: rows of offset bricks with dark mortar and per-brick tint
	const bricks = makeTexture(S, (x, y) => {
		const row = Math.floor(y / 16), off = (row & 1) * 16;
		const bx = Math.floor((x + off) / 32), by = row;
		const inBrick = ((x + off) % 32) > 2 && (y % 16) > 2;
		const tint = 0.75 + 0.25 * noise(bx, by, 1);
		const grain = 0.9 + 0.1 * noise(x, y, 2);
		const v = inBrick ? tint * grain : 0.28;
		return [v * 0.62, v * 0.56, v * 0.5];
	});
	// Flagstone floor: larger irregular slabs
	const floor = makeTexture(S, (x, y) => {
		const sx = Math.floor(x / 32), sy = Math.floor(y / 32);
		const edge = (x % 32) < 2 || (y % 32) < 2;
		const v = edge ? 0.18 : 0.32 + 0.14 * noise(sx, sy, 3) + 0.06 * noise(x, y, 4);
		return [v * 0.85, v * 0.85, v * 0.8];
	});
	// Ceiling: rough dark rock
	const ceiling = makeTexture(S, (x, y) => {
		const v = 0.14 + 0.1 * noise(x >> 2, y >> 2, 5) + 0.04 * noise(x, y, 6);
		return [v, v * 0.95, v * 0.9];
	});
	// Door: vertical planks with a frame and iron bands
	const door = makeTexture(S, (x, y) => {
		const frame = x < 6 || x >= S - 6 || y >= S - 6;
		if (frame) return [0.3, 0.28, 0.26];
		const plank = Math.floor((x - 6) / 13);
		const seam = ((x - 6) % 13) < 1;
		const band = (y > 14 && y < 18) || (y > 44 && y < 48);
		if (band) return [0.22, 0.22, 0.24];
		const v = seam ? 0.25 : 0.42 + 0.12 * noise(plank, 0, 7) + 0.05 * noise(x, y, 8);
		return [v, v * 0.62, v * 0.35];
	});
	return { bricks, floor, ceiling, door };
}

/* ------------------------------ map access ------------------------------ */

function cellWalls(x, y) {
	if (x < 0 || x > 31 || y < 0 || y > 31)
		return [WALL, WALL, WALL, WALL];
	const a = MAP + y * 128 + x * 4;
	const b0 = mem[a], b1 = mem[a + 1];
	return [b0 & 0xF, b0 >> 4, b1 & 0xF, b1 >> 4];   // N, E, S, W
}

/* ------------------------------ drawing ------------------------------ */

// A vertical wall quad from world (x0, z0) to (x1, z1), full cell height,
// with the texture repeating once per cell
function wallQuad(x0, z0, x1, z1) {
	gl.Begin(gl.QUADS);
	gl.TexCoord2f(0, 1); gl.Vertex3f(x0, 0, z0);
	gl.TexCoord2f(1, 1); gl.Vertex3f(x1, 0, z1);
	gl.TexCoord2f(1, 0); gl.Vertex3f(x1, CELL, z1);
	gl.TexCoord2f(0, 0); gl.Vertex3f(x0, CELL, z0);
	gl.End();
}

// Horizontal quad at height h covering cells [x0, x1) x [y0, y1)
function planeQuad(h, x0, y0, x1, y1) {
	const X0 = x0 * CELL, X1 = x1 * CELL, Z0 = y0 * CELL, Z1 = y1 * CELL;
	gl.Begin(gl.QUADS);
	gl.TexCoord2f(x0, y0); gl.Vertex3f(X0, h, Z0);
	gl.TexCoord2f(x1, y0); gl.Vertex3f(X1, h, Z0);
	gl.TexCoord2f(x1, y1); gl.Vertex3f(X1, h, Z1);
	gl.TexCoord2f(x0, y1); gl.Vertex3f(X0, h, Z1);
	gl.End();
}

function drawWalls(textures, eyeX, eyeZ, cx, cy) {
	// Walls of every cell in range, far to near (a painter's order; the depth
	// test helps too when the context has a depth buffer)
	const items = [];
	for (let y = cy - VIEW_RANGE; y <= cy + VIEW_RANGE; y++)
		for (let x = cx - VIEW_RANGE; x <= cx + VIEW_RANGE; x++) {
			const w = cellWalls(x, y);
			if (!(w[0] | w[1] | w[2] | w[3])) continue;
			const X0 = x * CELL, X1 = X0 + CELL, Z0 = y * CELL, Z1 = Z0 + CELL;
			// each side: end points and its midpoint for sorting
			const sides = [
				[w[0], X0, Z0, X1, Z0, 0],   // north
				[w[1], X1, Z0, X1, Z1, 1],   // east
				[w[2], X1, Z1, X0, Z1, 0],   // south
				[w[3], X0, Z1, X0, Z0, 1],   // west
			];
			for (const s of sides) {
				if (s[0] === 0) continue;
				const mx = (s[1] + s[3]) / 2, mz = (s[2] + s[4]) / 2;
				items.push({ side: s, d: (mx - eyeX) ** 2 + (mz - eyeZ) ** 2 });
			}
		}
	items.sort((p, q) => q.d - p.d);
	let bound = -1;
	for (const it of items) {
		const [type, x0, z0, x1, z1, ew] = it.side;
		const tex = isDoor(type) ? textures.door : textures.bricks;
		if (tex.id !== bound) { gl.BindTexture(gl.TEXTURE_2D, tex.id); bound = tex.id; }
		// east/west facing walls a little darker than north/south ones
		const shade = ew ? 0.78 : 1.0;
		if (isLocked(type)) gl.Color4f(0.9 * shade, 0.55 * shade, 0.5 * shade, 1);
		else gl.Color4f(shade, shade, shade, 1);
		wallQuad(x0, z0, x1, z1);
	}
}

/* ------------------------------ the view ------------------------------ */

export function createView3D() {
	let textures = null;
	// Interpolation state
	let prevX = null, prevZ = null, prevYaw = null;
	let curX = 0, curZ = 0, curYaw = 0;
	let moveFrames = 0, turnFrames = 0;

	return {
		// Call every frame; draws when the game shows the maze and no monster
		// is present (the game draws monsters into its own picture).
		render(movesPerSecond) {
			if (a8.antic.dlist !== 0x19BE || mem[0x1938] !== 0)
				return false;
			if (textures === null)
				textures = makeTextures();

			const cx = mem[0x6313], cy = mem[0x6314], facing = mem[0x6312];
			const x = cx * CELL + mem[0x6316] + 0.5, z = cy * CELL + mem[0x6317] + 0.5;
			// The game's map is mirrored relative to a right-handed world: what the
			// nibbles call north appears on the left when facing west. The scene is
			// mirrored in depth below, and the yaw follows.
			const yaw = 180 - facing * 90;

			// Interpolate between the game's discrete positions and facings
			if (prevX === null) { prevX = x; prevZ = z; prevYaw = yaw; }
			if (x !== curX || z !== curZ) { prevX = curX || x; prevZ = curZ || z; curX = x; curZ = z; moveFrames = 0; }
			if (yaw !== curYaw) { prevYaw = curYaw; curYaw = yaw; turnFrames = 0; }
			if (curX === 0 && curZ === 0) { curX = x; curZ = z; curYaw = yaw; }
			const moveT = Math.min(1, ++moveFrames / Math.max(1, 60 / Math.max(1, movesPerSecond)));
			const turnT = Math.min(1, ++turnFrames / 12);
			const ease = (t) => t * t * (3 - 2 * t);
			const eyeX = prevX + (curX - prevX) * ease(moveT);
			const eyeZ = prevZ + (curZ - prevZ) * ease(moveT);
			let dyaw = curYaw - prevYaw;
			if (dyaw > 180) dyaw -= 360; else if (dyaw < -180) dyaw += 360;
			const eyeYaw = prevYaw + dyaw * ease(turnT);

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

			// Projection: 2:1 window
			const near = 1, far = CELL * (VIEW_RANGE + 2);
			const hw = Math.tan(H_FOV / 2) * near, hh = Math.tan(V_FOV / 2) * near;
			gl.MatrixMode(gl.PROJECTION); gl.PushMatrix(); gl.LoadIdentity();
			gl.Frustum(-hw, hw, -hh, hh, near, far);
			gl.MatrixMode(gl.MODELVIEW); gl.PushMatrix(); gl.LoadIdentity();
			gl.Rotatef(eyeYaw, 0, 1, 0);
			gl.Scalef(1, 1, -1);
			gl.Translatef(-eyeX, -EYE_HEIGHT, -eyeZ);

			gl.Enable(gl.TEXTURE_2D);
			gl.Disable(gl.BLEND);
			gl.Enable(gl.DEPTH_TEST);
			gl.Disable(gl.CULL_FACE);
			gl.Enable(gl.FOG);
			gl.Fogf(gl.FOG_MODE, gl.LINEAR);
			gl.Fogf(gl.FOG_START, CELL * 1.5);
			gl.Fogf(gl.FOG_END, CELL * (VIEW_RANGE - 1));
			gl.Fogfv(gl.FOG_COLOR, [0, 0, 0, 1]);

			// Floor and ceiling over the drawn range, then the walls
			gl.Color4f(1, 1, 1, 1);
			gl.BindTexture(gl.TEXTURE_2D, textures.floor.id);
			planeQuad(0, cx - VIEW_RANGE, cy - VIEW_RANGE, cx + VIEW_RANGE + 1, cy + VIEW_RANGE + 1);
			gl.BindTexture(gl.TEXTURE_2D, textures.ceiling.id);
			planeQuad(CELL, cx - VIEW_RANGE, cy - VIEW_RANGE, cx + VIEW_RANGE + 1, cy + VIEW_RANGE + 1);
			drawWalls(textures, eyeX, eyeZ, cx, cy);

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
