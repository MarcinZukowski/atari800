// Alternate Reality: The Dungeon - faster rendering by skipping busy code,
// smooth walking, the maze drawn with OpenGL (view3d.js) and the disk sides
// served from files (disks.js).
import { createView3D } from "./view3d.js";
import { disks } from "./disks.js";
//
// Movement: the position inside the current cell is $6316/$6317 on a 36-unit
// grid, and each step adds the step size $6383, which the game derives from
// the character's speed (6-15 units, halved by a flag, at least 4, 2 when
// exhausted; set at $4430-$445E). The renderer draws any position, so with
// the drawing accelerated the step can be one unit and the joystick gated so
// that the walking speed stays what the game meant, times a chosen factor:
// many small steps instead of five big ones per cell.

const STEP_SIZE = 0x6383, JOYSTICK = 0x2E, JOYSTICK_PACKED = 0x2642, STEP_SIZE_SET = 0x445E;
const MASK_FORWARD = 0x01, MASK_BACK = 0x02;   // bits of $2E (see $3B74-$3B76)
const GAME_STEPS_PER_SECOND = 2.5;       // measured: one redraw per 20-30 frames
const TURNS_PER_SECOND = 2.5;            // one turn per game-loop pass, as without acceleration
const MAX_MOVES_PER_SECOND = 28;         // the accelerated loop manages about 30 passes a second
const SPEED_FACTORS = [1, 1.5, 2, 3];    // the "Walking speed" options

const mem = a8.mem;
let gameStep = 7;         // the step size the game computed for the character
const view3d = createView3D();
let movesPerSecond = GAME_STEPS_PER_SECOND;
let moveBudget = 0, turnBudget = 0;

export default {
	name: "ALT.REAL. JS HACK by Eru",

	fingerprint: { address: 0x29B6, bytes: [0x44, 0x75, 0x6E, 0x67, 0x65, 0x6F, 0x6E] },   // "Dungeon"

	menu: {
		FPS: { label: "Display FPS:", options: ["OFF", "ON"], current: 1 },
		ACCEL: { label: "Acceleration:", options: ["NO", "LOW", "HIGH"], current: 1 },
		SMOOTH: { label: "Smooth walking:", options: ["OFF", "ON"], current: 0 },
		SPEED: { label: "Walking speed:", options: ["1x", "1.5x", "2x", "3x"], current: 1 },
		VIEW3D: { label: "Maze view:", options: ["Atari", "OpenGL"], current: 1 },
		TEXTURES: { label: "Textures:", options: ["Original", "Smooth 4x"], current: 1 },
		// Wide: the view over the whole width, the texts shrunk above and below
		LAYOUT: { label: "Layout:", options: ["Game", "Wide"], current: 0 },
		// Automatic: the five disk sides are read from the .atr files next to
		// this script, and the game never asks for a disk (disks.js)
		DISKS: { label: "Disk swaps:", options: ["Manual", "Automatic"], current: 1 },
	},

	onPostGlFrame() {
		view3d.options.smoothTextures = this.menu.TEXTURES.current === 1;
		view3d.options.wide = this.menu.LAYOUT.current === 1;
		let drawn = false;
		if (this.menu.VIEW3D.current === 1)
			drawn = view3d.render(this.smoothActive() ? movesPerSecond : GAME_STEPS_PER_SECOND);
		else
			view3d.reset();
		if (view3d.options.wide)
			view3d.drawWideLayout(drawn);
	},

	smoothActive() {
		return this.menu.SMOOTH.current === 1 && this.menu.ACCEL.current === 2 && !a8.accelerationDisabled();
	},

	onActivate() {
		gameStep = mem[STEP_SIZE];
	},

	// Counts calls to $7856 (once per drawn frame), used for the FPS display
	calls7856: 0,

	// We intercept execution at these addresses
	codeInjections: [0x7856, 0x0090, 0x4A69, 0x3884, 0x7858, 0x7A1F, 0x7F1B, STEP_SIZE_SET, JOYSTICK_PACKED, ...disks.hooks],

	onCodeInjection(pc, op) {
		if (disks.hooks.includes(pc))
			return this.menu.DISKS.current === 1 ? disks.onCodeInjection(pc, op) : op;
		if (pc === 0x7856)   // moving into font memory? once per drawn frame
			this.calls7856++;

		if (pc === STEP_SIZE_SET) {   // the game just computed the character's step size
			gameStep = mem[STEP_SIZE];   // onPreGlFrame applies the smooth step from it
			return op;
		}
		if (pc === JOYSTICK_PACKED) {
			// The joystick was just packed into $2E (and is still in A, which the
			// caller tests): let a move or a turn through only when its budget allows
			if (this.smoothActive()) {
				const j = mem[JOYSTICK];
				let allow = true;
				if (j & (MASK_FORWARD | MASK_BACK)) {
					if (moveBudget >= 1) moveBudget -= 1; else allow = false;
				}
				else if (j & 0x0F) {
					if (turnBudget >= 1) turnBudget -= 1; else allow = false;
				}
				if (!allow) {
					mem[JOYSTICK] = j & 0x80;   // directions released, trigger bit kept
					a8.cpu.a = j & 0x80;
				}
			}
			return op;
		}

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

		if (this.smoothActive()) {
			// Small steps at the speed the game's own step size implies, times the
			// chosen factor; the step grows when the loop could not redraw often enough
			const factor = SPEED_FACTORS[this.menu.SPEED.current];
			const unitsPerSecond = gameStep * GAME_STEPS_PER_SECOND * factor;
			const step = Math.max(1, Math.ceil(unitsPerSecond / MAX_MOVES_PER_SECOND));
			mem[STEP_SIZE] = step;
			movesPerSecond = unitsPerSecond / step;
			moveBudget = Math.min(moveBudget + movesPerSecond / 60, 2);
			turnBudget = Math.min(turnBudget + TURNS_PER_SECOND * factor / 60, 1);
		}
		else if (mem[STEP_SIZE] !== gameStep) {
			mem[STEP_SIZE] = gameStep;   // smooth walking was switched off: give the game its step back
		}
	},
};
