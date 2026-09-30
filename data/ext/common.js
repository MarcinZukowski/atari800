// Shared helpers for atari800 JavaScript extensions.
// Import with: import { drawQuad, word, rgb } from "../common.js";

// Draws a textured quad: texture coordinates first (left, right, top,
// bottom), then screen/world coordinates, then depth.
export function drawQuad(tl, tr, tt, tb, l, r, t, b, z = -2.0) {
	gl.Begin(gl.QUADS);
	gl.TexCoord2f(tl, tb);
	gl.Vertex3f(l, b, z);
	gl.TexCoord2f(tr, tb);
	gl.Vertex3f(r, b, z);
	gl.TexCoord2f(tr, tt);
	gl.Vertex3f(r, t, z);
	gl.TexCoord2f(tl, tt);
	gl.Vertex3f(l, t, z);
	gl.End();
}

// 16-bit little-endian word at addr, like the 6502 sees it.
export function word(addr) {
	return a8.mem[addr] | (a8.mem[addr + 1] << 8);
}

// Atari colour byte -> [r, g, b] in 0..1, for gl.Color4f().
export function rgb(colour) {
	const v = a8.palette[colour & 0xff];
	return [((v >> 16) & 0xff) / 255, ((v >> 8) & 0xff) / 255, (v & 0xff) / 255];
}

// Scale2x (EPX): doubles a picture of pixel values, rounding the stairs of
// diagonal edges from the four neighbours without inventing colours. The
// picture is taken to repeat, as it does on the walls.
export function scale2x(src, w, h) {
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
