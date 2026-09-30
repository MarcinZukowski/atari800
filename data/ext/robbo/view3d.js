// Robbo (LK Avalon, 1989) - the level drawn with OpenGL in a slight
// perspective. The screen is ANTIC mode 4 in the narrow width, 32 characters
// a row, the whole level laid out in screen memory at $1000 as 16 x 31 tiles
// of 2 x 2 characters from the set at $1C00, shown through a window of 21
// rows that scrolls vertically (the display list's first LMS and VSCROL).
// Pixel value 0 shows the four quadruple-width players parked behind the
// playfield in the colour at $02C3: the floor. Values 1-3 are COLPF0, COLPF1
// and COLPF2, or COLPF3 for characters with bit 7 set (the OS shadows
// $02C4-$02C7 hold them).
//
// Here the floor is a plane in that colour, solid tiles (walls) are blocks
// with the art on top and darkened sides, other tiles are cards lying a
// little above the floor with a shadow, and the art is upscaled with Scale2x
// twice. The camera looks down at the game's window from a little in front.

import { scale2x } from "../common.js";

const mem = a8.mem;
const SCREEN = 0x1000, ROW_BYTES = 32, CHARSET = 0x1C00;
const LEVEL_W = 16, LEVEL_H = 31;                 // tiles
const WINDOW_ROWS = 21;                           // character rows shown
const FLOOR_COLOUR = 0x02C3, PF_SHADOWS = 0x02C4; // COLPF0-3 shadows
const PLAY_DLIST = 0x36BD;                        // the display list of the play screen
// The playfield on the screen (336 x 240 hi-res pixels / lines): the narrow
// width is 128 colour clocks centred, and the rows start after 21 blank lines
const SCREEN_W = 336, SCREEN_H = 240;
const PLAYFIELD = [40, 21, 296, 21 + 8 * WINDOW_ROWS];
const TILE_W = 8, TILE_H = 16;                    // a tile in mode-4 pixels and lines
const WALL_HEIGHT = 0.45, CARD_HEIGHT = 0.12;     // in tile units
const SOLID = 0.9;                                // a tile this opaque is a block

/* ------------------------------ the tiles ------------------------------ */

// Pixel values of the tile whose top-left character is at screen address a
function tilePixels(a) {
	const codes = [mem[a], mem[a + 1], mem[a + ROW_BYTES], mem[a + ROW_BYTES + 1]];
	const px = new Uint8Array(TILE_W * TILE_H);
	for (let q = 0; q < 4; q++) {
		const c = codes[q], base = CHARSET + (c & 0x7F) * 8, x0 = (q & 1) * 4, y0 = (q >> 1) * 8;
		for (let line = 0; line < 8; line++) {
			const b = mem[base + line];
			for (let k = 0; k < 4; k++) {
				const v = (b >> (6 - 2 * k)) & 3;
				px[(y0 + line) * TILE_W + x0 + k] = v === 3 && (c & 0x80) ? 4 : v;   // 4: COLPF3
			}
		}
	}
	return px;
}

let charsetSum = 0, colourKey = "";
const textures = new Map();   // key -> { texture, solid }

function frameKey() {
	let sum = 0;
	for (let a = CHARSET; a < CHARSET + 1024; a++) sum = (sum * 31 + mem[a]) | 0;
	const colours = `${mem[PF_SHADOWS]},${mem[PF_SHADOWS + 1]},${mem[PF_SHADOWS + 2]},${mem[PF_SHADOWS + 3]}`;
	if (sum !== charsetSum || colours !== colourKey) { textures.clear(); charsetSum = sum; colourKey = colours; }
}

// The texture of the tile at screen address a (cached by its characters)
function tileTexture(a, smooth) {
	const key = `${mem[a]},${mem[a + 1]},${mem[a + ROW_BYTES]},${mem[a + ROW_BYTES + 1]}|${smooth}`;
	let t = textures.get(key);
	if (t !== undefined) return t;
	let px = tilePixels(a), w = TILE_W, h = TILE_H;
	let opaque = 0;
	for (let i = 0; i < px.length; i++) if (px[i]) opaque++;
	if (smooth) {
		px = scale2x(px, w, h); w *= 2; h *= 2;
		px = scale2x(px, w, h); w *= 2; h *= 2;
	}
	const colours = [0, a8.palette[mem[PF_SHADOWS]], a8.palette[mem[PF_SHADOWS + 1]], a8.palette[mem[PF_SHADOWS + 2]], a8.palette[mem[PF_SHADOWS + 3]]];
	const texture = gl.createTexture(w, h), out = texture.pixels;
	for (let i = 0, o = 0; i < px.length; i++, o += 4) {
		const v = px[i], c = colours[v];
		out[o] = c >> 16; out[o + 1] = (c >> 8) & 255; out[o + 2] = c & 255; out[o + 3] = v ? 255 : 0;
	}
	texture.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
	t = { texture, solid: opaque >= SOLID * TILE_W * TILE_H, empty: opaque === 0 };
	textures.set(key, t);
	return t;
}

/* ------------------------------ drawing ------------------------------ */

// A textured quad in world space: x across, y down the level, z up
function quad(x0, y0, x1, y1, z, u0 = 0, v0 = 0, u1 = 1, v1 = 1) {
	gl.Begin(gl.QUADS);
	gl.TexCoord2f(u0, v0); gl.Vertex3f(x0, y0, z);
	gl.TexCoord2f(u1, v0); gl.Vertex3f(x1, y0, z);
	gl.TexCoord2f(u1, v1); gl.Vertex3f(x1, y1, z);
	gl.TexCoord2f(u0, v1); gl.Vertex3f(x0, y1, z);
	gl.End();
}

// A block over the tile (x, y): the art on top, the sides in the same art, darker
function block(x, y, height, shade) {
	gl.Color4f(1, 1, 1, 1);
	quad(x, y, x + 1, y + 1, height);
	gl.Color4f(shade, shade, shade, 1);
	gl.Begin(gl.QUADS);
	// south (towards the camera) and north
	gl.TexCoord2f(0, 1); gl.Vertex3f(x, y + 1, 0); gl.TexCoord2f(1, 1); gl.Vertex3f(x + 1, y + 1, 0);
	gl.TexCoord2f(1, 0.6); gl.Vertex3f(x + 1, y + 1, height); gl.TexCoord2f(0, 0.6); gl.Vertex3f(x, y + 1, height);
	gl.TexCoord2f(0, 0); gl.Vertex3f(x, y, 0); gl.TexCoord2f(1, 0); gl.Vertex3f(x + 1, y, 0);
	gl.TexCoord2f(1, 0.4); gl.Vertex3f(x + 1, y, height); gl.TexCoord2f(0, 0.4); gl.Vertex3f(x, y, height);
	// west and east
	gl.TexCoord2f(0, 0); gl.Vertex3f(x, y, 0); gl.TexCoord2f(0, 1); gl.Vertex3f(x, y + 1, 0);
	gl.TexCoord2f(0.4, 1); gl.Vertex3f(x, y + 1, height); gl.TexCoord2f(0.4, 0); gl.Vertex3f(x, y, height);
	gl.TexCoord2f(1, 0); gl.Vertex3f(x + 1, y, 0); gl.TexCoord2f(1, 1); gl.Vertex3f(x + 1, y + 1, 0);
	gl.TexCoord2f(0.6, 1); gl.Vertex3f(x + 1, y + 1, height); gl.TexCoord2f(0.6, 0); gl.Vertex3f(x + 1, y, height);
	gl.End();
}

export function createView3D() {
	return {
		options: { smooth: true, tilt: 30, height: WALL_HEIGHT },

		// Is the play screen up? Its display list, with the window in the level
		playing() {
			if (a8.antic.dlist !== PLAY_DLIST) return false;
			const lms = mem[PLAY_DLIST + 4] | mem[PLAY_DLIST + 5] << 8;
			return lms >= SCREEN && lms < SCREEN + ROW_BYTES * 2 * LEVEL_H;
		},

		// Draws the level over the playfield; returns false when not playing
		render() {
			if (!this.playing()) return false;
			frameKey();
			const lms = mem[PLAY_DLIST + 4] | mem[PLAY_DLIST + 5] << 8;
			const topRow = (lms - SCREEN) / ROW_BYTES, fine = a8.antic.vscrol & 7;
			const windowTop = (topRow * 8 + fine) / TILE_H, windowHeight = (WINDOW_ROWS * 8) / TILE_H;   // in tiles
			// looking a little above the window's centre: the far rows shrink, so
			// this trades a cut of the row below the window for less black above it
			const centreY = windowTop + windowHeight / 2 - 0.8;

			// The viewport: the playfield's rectangle
			const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
			const gx = (px) => px / (SCREEN_W / 2) - 1, gy = (py) => 1 - py / (SCREEN_H / 2);
			const px0 = Math.round(vx + (gx(PLAYFIELD[0]) + 1) / 2 * vw), px1 = Math.round(vx + (gx(PLAYFIELD[2]) + 1) / 2 * vw);
			const py0 = Math.round(vy + (gy(PLAYFIELD[3]) + 1) / 2 * vh), py1 = Math.round(vy + (gy(PLAYFIELD[1]) + 1) / 2 * vh);
			gl.PushAttrib(gl.ENABLE_BIT); gl.PushAttrib(gl.SCISSOR_BIT); gl.PushAttrib(gl.CURRENT_BIT);
			gl.Viewport(px0, py0, px1 - px0, py1 - py0);
			gl.Scissor(px0, py0, px1 - px0, py1 - py0);
			gl.Enable(gl.SCISSOR_TEST);
			gl.ClearColor(0, 0, 0, 1);
			gl.Clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

			// The camera: above the window's centre, tilted; the window fills the
			// viewport at the board's distance
			const aspect = (px1 - px0) / (py1 - py0);
			const fovY = 38 * Math.PI / 180, dist = (windowHeight / 2) / Math.tan(fovY / 2);
			const near = 1, far = dist * 3, hh = Math.tan(fovY / 2) * near, hw = hh * aspect;
			gl.MatrixMode(gl.PROJECTION); gl.PushMatrix(); gl.LoadIdentity();
			gl.Frustum(-hw, hw, -hh, hh, near, far);
			gl.MatrixMode(gl.MODELVIEW); gl.PushMatrix(); gl.LoadIdentity();
			gl.Translatef(0, 0, -dist);
			gl.Rotatef(-this.options.tilt, 1, 0, 0);         // the far rows lean away
			gl.Scalef(1, -1, 1);                               // level rows go down the screen
			gl.Translatef(-LEVEL_W / 2, -centreY, 0);

			gl.Enable(gl.DEPTH_TEST);
			gl.Disable(gl.CULL_FACE);
			gl.Disable(gl.TEXTURE_2D);
			gl.Disable(gl.BLEND);

			// The floor: the players' colour, the whole level
			const [fr, fg, fb] = a8.rgb(mem[FLOOR_COLOUR]);
			gl.Color4f(fr / 255, fg / 255, fb / 255, 1);
			quad(0, 0, LEVEL_W, LEVEL_H, 0);

			// The tiles in the window and a little around it
			gl.Enable(gl.TEXTURE_2D);
			gl.Enable(gl.BLEND);
			gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
			const row0 = Math.max(0, Math.floor(windowTop) - 1), row1 = Math.min(LEVEL_H, Math.ceil(windowTop + windowHeight) + 2);
			const cards = [];
			for (let ty = row0; ty < row1; ty++)
				for (let tx = 0; tx < LEVEL_W; tx++) {
					const a = SCREEN + ty * 2 * ROW_BYTES + tx * 2;
					const t = tileTexture(a, this.options.smooth);
					if (t.empty) continue;
					if (t.solid) {
						gl.BindTexture(gl.TEXTURE_2D, t.texture.id);
						block(tx, ty, this.options.height, 0.55);
					}
					else cards.push([tx, ty, t]);
				}
			// the shadows, then the cards over them
			gl.Disable(gl.TEXTURE_2D);
			gl.Color4f(0, 0, 0, 0.35);
			for (const [tx, ty] of cards) quad(tx + 0.08, ty + 0.08, tx + 1.08, ty + 1.08, 0.01);
			gl.Enable(gl.TEXTURE_2D);
			gl.Color4f(1, 1, 1, 1);
			for (const [tx, ty, t] of cards) {
				gl.BindTexture(gl.TEXTURE_2D, t.texture.id);
				quad(tx, ty, tx + 1, ty + 1, CARD_HEIGHT);
			}

			gl.MatrixMode(gl.PROJECTION); gl.PopMatrix();
			gl.MatrixMode(gl.MODELVIEW); gl.PopMatrix();
			gl.Viewport(vx, vy, vw, vh);
			gl.PopAttrib(); gl.PopAttrib(); gl.PopAttrib();
			gl.Color4f(1, 1, 1, 1);
			return true;
		},
	};
}
