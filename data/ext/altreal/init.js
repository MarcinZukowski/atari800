// Alternate Reality: The Dungeon - faster rendering by skipping busy code.

export default {
	name: "ALT.REAL. JS HACK by Eru",

	fingerprint: { address: 0x29B6, bytes: [0x44, 0x75, 0x6E, 0x67, 0x65, 0x6F, 0x6E] },   // "Dungeon"

	menu: {
		FPS: { label: "Display FPS:", options: ["OFF", "ON"], current: 1 },
		ACCEL: { label: "Acceleration:", options: ["NO", "LOW", "HIGH"], current: 1 },
	},

	// Counts calls to $7856 (once per drawn frame), used for the FPS display
	calls7856: 0,

	// We intercept execution at these addresses
	codeInjections: [0x7856, 0x0090, 0x4A69, 0x3884, 0x7858, 0x7A1F, 0x7F1B],

	onCodeInjection(pc, op) {
		if (pc === 0x7856)   // moving into font memory? once per drawn frame
			this.calls7856++;

		if (a8.accelerationDisabled() || this.menu.ACCEL.current === 0)
			return op;

		// This one on LOW and HIGH
		if (pc === 0x90)   // drawing?
			return a8.fakeCpuUntilPc(0x00D5);

		if (this.menu.ACCEL.current === 1)
			return op;

		// These on HIGH only
		switch (pc) {
		case 0x4A69: return a8.fakeCpuUntilPc(0x4A82);
		case 0x3884: return a8.fakeCpuUntilPc(0x38CE);
		case 0x7858: return a8.fakeCpuUntilPc(0x7887);   // moving into font memory?
		case 0x7A1F: return a8.fakeCpuUntilPc(0x7A36);
		case 0x7F1B: return a8.fakeCpuUntilPc(0x7F4A);
		}
		return op;
	},

	onPreGlFrame() {
		if (this.menu.FPS.current === 1)
			a8.printFps(this.calls7856, 0x9f, 0x90, 0, -2);
	},
};
