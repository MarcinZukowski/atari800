// Alternate Reality: The Dungeon - the game's own pictures smoothed. Shop
// interiors, the picture in the Atari view, monsters in it: whatever the game
// draws in the picture band is read back from the framebuffer at the game's
// pixel grid (mode-4 pixels, two hi-res pixels wide), upscaled with Scale2x
// twice like the wall art, and drawn over its place, or enlarged where the
// wide layout wants it. Reading the framebuffer needs no knowledge of the
// shop's fonts and colours, and the picture is rebuilt only when it changes.

import { scale2x } from "../common.js";

const SCREEN_W = 336, SCREEN_H = 240;      // the displayed area, in hi-res pixels and lines
const Z_2D = -2;                           // where the emulator draws its own screen

let texture = null, key = "", captured = null;   // the region the texture shows

// Reads the region [x0, y0, x1, y1] (hi-res pixels across, lines down) and
// builds its smoothed texture; call before anything is drawn over it
export function capture(region) {
	const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
	const cols = (region[2] - region[0]) >> 1, rows = region[3] - region[1];
	// the window pixel at the centre of a hi-res pixel column / a line (GL rows go up)
	const wx = (x) => vx + Math.floor((x + 0.5) * vw / SCREEN_W);
	const wy = (y) => vy + vh - 1 - Math.floor((y + 0.5) * vh / SCREEN_H);
	const px0 = wx(region[0]), px1 = wx(region[2] - 1), py0 = wy(region[3] - 1), py1 = wy(region[1]);
	const w = px1 - px0 + 1, h = py1 - py0 + 1;
	if (w <= 0 || h <= 0) return false;
	const shot = gl.readPixels(px0, py0, w, h);
	// one colour index per mode-4 pixel, sampled at its first hi-res pixel
	const idx = new Uint8Array(cols * rows), palette = [], lookup = new Map();
	let hash = 0;
	for (let r = 0; r < rows; r++) {
		const sy = wy(region[1] + r) - py0;
		for (let c = 0; c < cols; c++) {
			const sx = wx(region[0] + 2 * c) - px0, o = (sy * w + sx) * 4;
			const rgb = shot[o] << 16 | shot[o + 1] << 8 | shot[o + 2];
			let i = lookup.get(rgb);
			if (i === undefined) { i = Math.min(palette.length, 255); if (i === palette.length) palette.push(rgb); lookup.set(rgb, i); }
			idx[r * cols + c] = i;
			hash = (hash * 31 + rgb) | 0;
		}
	}
	captured = region;
	const k = `${cols}x${rows}|${hash}`;
	if (k === key && texture !== null) return true;
	key = k;
	let v = scale2x(idx, cols, rows), W = 2 * cols, H = 2 * rows;
	v = scale2x(v, W, H); W *= 2; H *= 2;
	if (texture === null || texture.width !== W || texture.height !== H) texture = gl.createTexture(W, H);
	const px = texture.pixels;
	for (let i = 0, o = 0; i < v.length; i++, o += 4) {
		const c = palette[v[i]];
		px[o] = c >> 16; px[o + 1] = (c >> 8) & 255; px[o + 2] = c & 255; px[o + 3] = 255;
	}
	texture.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
	return true;
}

// Draws the part `src` of the captured region (default: all of it) onto the
// screen rectangle `dst`, both in hi-res pixels and lines
export function draw(dst, src = captured) {
	if (texture === null || captured === null) return;
	const gx = (px) => px / (SCREEN_W / 2) - 1, gy = (py) => 1 - py / (SCREEN_H / 2);
	const u = (x) => (x - captured[0]) / (captured[2] - captured[0]), v = (y) => (y - captured[1]) / (captured[3] - captured[1]);
	gl.PushAttrib(gl.ENABLE_BIT); gl.PushAttrib(gl.CURRENT_BIT);
	gl.Enable(gl.TEXTURE_2D); gl.Disable(gl.BLEND); gl.Disable(gl.DEPTH_TEST);
	gl.BindTexture(gl.TEXTURE_2D, texture.id);
	gl.Color4f(1, 1, 1, 1);
	gl.Begin(gl.QUADS);
	gl.TexCoord2f(u(src[0]), v(src[1])); gl.Vertex3f(gx(dst[0]), gy(dst[1]), Z_2D);
	gl.TexCoord2f(u(src[2]), v(src[1])); gl.Vertex3f(gx(dst[2]), gy(dst[1]), Z_2D);
	gl.TexCoord2f(u(src[2]), v(src[3])); gl.Vertex3f(gx(dst[2]), gy(dst[3]), Z_2D);
	gl.TexCoord2f(u(src[0]), v(src[3])); gl.Vertex3f(gx(dst[0]), gy(dst[3]), Z_2D);
	gl.End();
	gl.PopAttrib(); gl.PopAttrib();
	gl.Color4f(1, 1, 1, 1);
}
