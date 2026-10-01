// Numen (Taquart, 2003) - the demo's 3D scenes run faster (the hottest code,
// found with the emulator's profile, runs in no emulated time: see
// createAccelerator in common.js), and they are drawn again with OpenGL from
// the demo's own level data and camera (world3d.js). With that off, the
// demo's picture is smoothed with Scale2x over its own pixel grid
// (../smooth2d.js). See numen.md.

import { createAccelerator } from "../common.js";
import { createSmoother } from "../smooth2d.js";
import { createWorld3D } from "./world3d.js";

const mem = a8.mem;
const accel = createAccelerator({ minShare: 0.001, maxRanges: 48 });   // the hottest code down to 0.1% of the cycles
// The 3D scenes are GTIA mode 10 (PRIOR $81) in mode-F rows stretched to four
// lines by the VSCROL trick: pixels four hi-res pixels wide and four lines
// tall, 80 x 48 of them, from screen line 24 (display list $1F80, buffers at
// $1000 and $1800)
const smoother = createSmoother(4, 4);
const world = createWorld3D();
const SCENE_DLIST = 0x1F80, SCENE = [8, 24, 328, 24 + 48 * 4];
const inScene = () => a8.antic.dlist === SCENE_DLIST && [0x1000, 0x1800].includes(mem[0x1F84] | mem[0x1F85] << 8);
let frame = 0, flips = 0, lastLms = -1;

export default {
	name: "NUMEN JS HACK by Eru",

	fingerprint: { address: 0xFF83, bytes: [0x48, 0x2c, 0x0f, 0xd4, 0x10, 0x03, 0x4c, 0xbb] },   // the NMI handler, in the RAM under the OS

	menu: {
		ACCEL: { label: "Acceleration:", options: ["OFF", "ON"], current: 1 },
		SMOOTH: { label: "Smooth picture:", options: ["OFF", "Scale2x 4x"], current: 1 },
		WORLD: { label: "3D scene:", options: ["Atari", "OpenGL"], current: 1 },
		SHADE: { label: "Shading and fog:", options: ["OFF", "ON"], current: 1 },
		FULL: { label: "Whole picture:", options: ["OFF", "ON"], current: 1 },
		GROUND: { label: "Ground texture:", options: ["OFF", "ON"], current: 1 },
		ANTIALIAS: { label: "Smooth edges:", options: ["OFF", "ON"], current: 1 },
		RATE: { label: "Log frame rate:", options: ["OFF", "ON"], current: 0 },
	},

	onPostGlFrame() {
		if (!inScene()) return;
		const smooth = this.menu.SMOOTH.current === 1;
		const on = (name) => this.menu[name].current === 1;
		if (on("WORLD") && world.render({ smooth, shade: on("SHADE"), full: on("FULL"), ground: on("GROUND"), antialias: on("ANTIALIAS") })) return;
		if (smooth && smoother.capture(SCENE)) smoother.draw(SCENE);
	},

	codeInjections: [0xD000],   // replaced at run time by the accelerator

	onCodeInjection(pc, op) {
		return this.menu.ACCEL.current === 1 && !a8.accelerationDisabled() ? accel.onCodeInjection(pc, op) : op;
	},

	onFrame() {
		if (this.menu.ACCEL.current === 1) accel.onFrame();
		// the rendered frames: the display list's first LMS flips between the two buffers
		frame++;
		const lms = mem[0x1F84] | mem[0x1F85] << 8;
		if (lms !== lastLms) { flips++; lastLms = lms; }
		if (this.menu.RATE.current === 1 && frame % 250 === 0) { console.log(`numen: ${(flips / 5).toFixed(1)} frames a second`); flips = 0; }
	},
};
