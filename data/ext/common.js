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

// Profile-driven acceleration. The emulator's profile (a8.profile()) says how
// many cycles each address took; the hottest contiguous ranges of code are
// then run in no emulated time whenever execution enters them, through
// a8.fakeCpuWhileIn(), which stays within the range and gives up after a
// budget of instructions: a loop waiting for an interrupt or VCOUNT cannot
// finish without time passing, so such a range is dropped for good. Ranges
// with an executed RTI or a store to WSYNC are left alone (interrupt
// handlers and code timed to the beam). The profile is taken again now and
// then, since programs change what they run.
//
// Use: const accel = createAccelerator(); call accel.onFrame() from onFrame()
// and return accel.onCodeInjection(pc, op) from onCodeInjection(); declare
// one unused address in codeInjections (the list is replaced at run time).
export function createAccelerator(options = {}) {
	const o = { profileFrames: 150, minShare: 0.004, maxRanges: 16, budget: 2000000, reprofile: 1500, log: true, ...options };
	let frame = 0, phase = "profile", since = 0, ranges = [];
	const dropped = [];
	const overlaps = (r) => dropped.some((d) => r.lo <= d.hi && r.hi >= d.lo);
	// Interrupt handlers (an executed RTI) and code timed by WSYNC must run in
	// real time: their register writes would land at the wrong moment
	const timed = (r, counts) => {
		for (let a = r.lo; a <= r.hi; a++)
			if (counts[a] > 0 && (a8.mem[a] === 0x40 || (a8.mem[a] === 0x8D && a8.mem[a + 1] === 0x0A && a8.mem[a + 2] === 0xD4)))
				return true;
		return false;
	};

	// The hottest ranges: addresses that ran, joined over gaps of up to three
	// bytes (operands), with their share of all cycles
	function analyse() {
		const counts = a8.profile("count"), cycles = a8.profile("cycles");
		if (counts === null) return [];
		let total = 0;
		for (let a = 0; a < 65536; a++) total += cycles[a];
		const found = [];
		let lo = -1, last = -1, sum = 0;
		for (let a = 0; a <= 65536; a++) {
			if (a < 65536 && counts[a] > 0) {
				if (lo >= 0 && a - last > 3) { found.push({ lo, hi: last, share: sum / total }); lo = -1; }
				if (lo < 0) { lo = a; sum = 0; }
				sum += cycles[a]; last = a;
			}
		}
		if (lo >= 0) found.push({ lo, hi: last, share: sum / total });
		found.sort((p, q) => q.share - p.share);
		return found.filter((r) => r.share >= o.minShare && !overlaps(r) && !timed(r, counts)).slice(0, o.maxRanges);
	}

	function apply() {
		const addresses = [];
		for (const r of ranges) for (let a = r.lo; a <= r.hi; a++) addresses.push(a);
		a8.setCodeInjections(addresses);
	}

	const hex = (a) => "$" + a.toString(16).padStart(4, "0");

	return {
		get ranges() { return ranges; },

		onFrame() {
			frame++; since++;
			if (phase === "profile") {
				if (since === 1) a8.profileReset();
				if (since >= o.profileFrames) {
					ranges = analyse(); apply();
					if (o.log) console.log(`accelerating ${ranges.length} ranges: ` + ranges.map((r) => `${hex(r.lo)}-${hex(r.hi)} ${(100 * r.share).toFixed(1)}%`).join(", "));
					phase = "run"; since = 0;
				}
			}
			else if (since >= o.reprofile) {
				ranges = []; apply();
				phase = "profile"; since = 0;
			}
		},

		onCodeInjection(pc, op) {
			for (const r of ranges) {
				if (pc < r.lo || pc > r.hi) continue;
				if (a8.fakeCpuWhileIn(r.lo, r.hi, o.budget) < 0) {   // waits for something: not this one
					dropped.push(r);
					ranges = ranges.filter((x) => x !== r); apply();
					if (o.log) console.log(`dropped ${hex(r.lo)}-${hex(r.hi)}: it waits`);
				}
				return a8.OP_NOP;
			}
			return op;
		},
	};
}
