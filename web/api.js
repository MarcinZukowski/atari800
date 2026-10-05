// The extensions' `a8` object in the browser: what src/ext-js.c gives the
// scripts natively, made here of the WebAssembly module's memory and the
// functions src/web/web.c exports. The memory views are over the module's
// heap itself, so `a8.mem[addr]` reads and writes the emulated RAM directly,
// as it does natively. (The module is built without memory growth, which
// would detach the views.)

const CPU = { a: 0, x: 1, y: 2, s: 3, p: 4, pc: 5 };
const ANTIC = ["dlist", "hscrol", "vscrol", "chbase", "pmbase", "dmactl"];   // registers 0-5 of web_reg()
const GTIA = ["colbk", "colpf0", "colpf1", "colpf2", "colpf3", "colpm0", "colpm1", "colpm2", "colpm3",
	"hposp0", "hposp1", "hposp2", "hposp3", "sizep0", "sizep1", "sizep2", "sizep3",
	"grafp0", "grafp1", "grafp2", "grafp3", "prior", "gractl"];              // registers 6-28

// M: the module; host: { accelerationDisabled(), setTimeLimit(seconds), showFps(text), files: Map, audio() -> AudioContext or null }
export function makeA8(M, host) {
	const heap = M.HEAPU8.buffer;
	const cpu = {}, antic = {}, gtia = {};
	for (const [name, reg] of Object.entries(CPU))
		Object.defineProperty(cpu, name, { get: () => M._web_cpu_get(reg), set: (v) => M._web_cpu_set(reg, v), enumerable: true });
	ANTIC.forEach((name, i) => Object.defineProperty(antic, name, { get: () => M._web_reg(i), enumerable: true }));
	GTIA.forEach((name, i) => Object.defineProperty(gtia, name, { get: () => M._web_reg(6 + i), enumerable: true }));

	const palette = new Int32Array(heap, M._web_colours(), 256);
	const profileBuffer = M._malloc(0x10000 * 8);
	let fpsValue = -1, fpsFrames = 0, fpsShown = 0;

	return {
		host: "web",
		panel: null,   // the active extension's own element in the page; the host sets it
		mem: new Uint8Array(heap, M._web_mem(), 0x10000),
		palette,
		cpu, antic, gtia,
		OP_RTS: 0x60, OP_NOP: 0xEA,
		peek: (addr) => M._web_peek(addr),
		poke: (addr, value) => M._web_poke(addr, value),
		rgb: (colour) => { const c = palette[colour & 255]; return [(c >> 16) & 255, (c >> 8) & 255, c & 255]; },
		fakeCpuUntilPc: (pc, maxInstructions = 0) => M._web_fakecpu_until_pc(pc, maxInstructions),
		fakeCpuUntilOp: (op, maxInstructions = 0) => M._web_fakecpu_until_op(op, maxInstructions),
		fakeCpuUntilAfterOp: (op, maxInstructions = 0) => M._web_fakecpu_until_after_op(op, maxInstructions),
		fakeCpuWhileIn: (lo, hi, budget) => M._web_fakecpu_while_in(lo, hi, budget),
		setCodeInjections(addresses) {
			const list = Int32Array.from(addresses), p = M._malloc(list.length * 4 || 4);
			new Int32Array(heap, p, list.length).set(list);
			M._web_set_code_injections(p, list.length);
			M._free(p);
		},
		xeBank(n) { const p = M._web_xe_bank(n); return p ? new Uint8Array(heap, p, 16384) : null; },
		profile(what) { M._web_profile(what === "cycles" ? 1 : 0, profileBuffer); return new Float64Array(heap, profileBuffer, 0x10000).slice(); },
		profileReset: () => M._web_profile_reset(),
		accelerationDisabled: () => host.accelerationDisabled(),
		// Natively the limit on a call into the script; here only the slow-frame report's threshold
		setTimeLimit(seconds) { if (!(seconds > 0)) throw new RangeError("setTimeLimit(seconds) needs a positive number"); host.setTimeLimit(seconds); },
		// Natively this prints into the Atari's screen; here the page shows it
		printFps(value) {
			fpsFrames++;
			if (value !== fpsValue) { fpsShown = fpsFrames; fpsFrames = 0; fpsValue = value; }
			host.showFps(`${fpsShown} frames`);
		},
		// Natively into an AVI file at that path; here a download named after it
		recordVideo: (path) => host.recordVideo(path),
		stopRecording: () => host.stopRecording(),
		// A sound file (WAV) the extension plays over the emulator's own sound
		loadSound(path) {
			let buffer = null, wanted = false;
			const sound = {
				play() {
					const context = host.audio();
					if (context === null) return;
					if (buffer === null) {
						if (wanted) return;
						wanted = true;
						const bytes = host.files.get(path);
						if (bytes) context.decodeAudioData(bytes.slice().buffer).then((b) => { buffer = b; }, () => { });
						return;
					}
					const source = context.createBufferSource();
					source.buffer = buffer; source.connect(context.destination); source.start();
				},
			};
			return sound;
		},
	};
}
