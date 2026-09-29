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
