// Smoothing of what a game draws: a region of the screen is read back from
// the framebuffer at the game's own pixel grid (a pixel being pixelW hi-res
// pixels wide and pixelH lines tall), its colours turned into indices,
// upscaled with Scale2x twice and drawn over its place, or wherever asked.
// Reading the framebuffer needs no knowledge of the game's modes, fonts and
// colours, and catches sprites too. The texture is rebuilt only when the
// sampled picture changes.
//
// Use: const s = createSmoother(2, 1); each frame, after the game's screen
// is drawn and before drawing over it, s.capture(region) and then s.draw(dst).
// Regions are [x0, y0, x1, y1] in hi-res pixels across (0-335) and lines
// down (0-239) of the displayed area.

import { scale2x } from "./common.js";

const SCREEN_W = 336, SCREEN_H = 240;
const Z_2D = -2;                           // where the emulator draws its own screen

export function createSmoother(pixelW, pixelH) {
	let texture = null, key = "", captured = null;

	return {
		capture(region) {
			const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
			const cols = Math.floor((region[2] - region[0]) / pixelW), rows = Math.floor((region[3] - region[1]) / pixelH);
			// the window pixel of a hi-res pixel's centre, and of a line's first and
			// second sub-rows (GL rows go up; the emulator may darken every other
			// window row for scanlines, so the brighter of the two is taken)
			const wx = (x) => vx + Math.floor((x + 0.5) * vw / SCREEN_W);
			const wy = (y, part) => vy + vh - 1 - Math.floor((y + part) * vh / SCREEN_H);
			const px0 = wx(region[0]), px1 = wx(region[2] - 1), py0 = wy(region[3] - 1, 0.75), py1 = wy(region[1], 0.1);
			const w = px1 - px0 + 1, h = py1 - py0 + 1;
			if (w <= 0 || h <= 0 || cols <= 0 || rows <= 0) return false;
			const shot = gl.readPixels(px0, py0, w, h);
			const idx = new Uint8Array(cols * rows), palette = [], lookup = new Map();
			let hash = 0;
			for (let r = 0; r < rows; r++) {
				const line = region[1] + r * pixelH + (pixelH >> 1);
				const sy0 = wy(line, 0.1) - py0, sy1 = wy(line, 0.75) - py0;
				for (let c = 0; c < cols; c++) {
					const sx = wx(region[0] + c * pixelW + (pixelW >> 1)) - px0;
					const o0 = (sy0 * w + sx) * 4, o1 = (sy1 * w + sx) * 4;
					const rgb = Math.max(shot[o0], shot[o1]) << 16 | Math.max(shot[o0 + 1], shot[o1 + 1]) << 8 | Math.max(shot[o0 + 2], shot[o1 + 2]);
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
		},

		// Draws the part `src` of the captured region (default: all of it) onto the
		// screen rectangle `dst`, both in hi-res pixels and lines
		draw(dst, src = captured) {
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
		},
	};
}
