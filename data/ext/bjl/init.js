// Behind Jaggi Lines - faster rendering by skipping busy code.

a8.register({
	name: "BJL JS HACK by Eru",

	fingerprint: { address: 0x41FC, bytes: [0x6A, 0x61, 0x67, 0x67, 0x69] },   // "jaggi"

	menu: {
		FPS: { label: "Display FPS:", options: ["OFF", "ON"], current: 1 },
		ACCEL: { label: "Acceleration:", options: ["NO", "LOW", "HIGH"], current: 2 },
	},

	codeInjections: [0xb51c, 0x9da7, 0xaf32],

	onCodeInjection(pc, op) {
		if (a8.accelerationDisabled() || this.menu.ACCEL.current === 0)
			return op;
		// Accelerate this one on LOW and HIGH
		if (pc === 0xb51c)
			return a8.fakeCpuUntilOp(a8.OP_RTS);
		// The others only on HIGH
		if (this.menu.ACCEL.current === 2)
			return a8.fakeCpuUntilOp(a8.OP_RTS);
		return op;
	},

	onPreGlFrame() {
		if (this.menu.FPS.current === 1)
			a8.printFps(a8.antic.dlist, 0x9f, 0x90, 0, -2);
	},
});
