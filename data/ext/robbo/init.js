// Robbo (LK Avalon, 1989) - the level drawn with OpenGL in a slight
// perspective: the floor in the level's colour, walls as blocks, the other
// tiles as cards above the floor, the art upscaled (view3d.js).

import { createView3D } from "./view3d.js";

const view3d = createView3D();

export default {
	name: "ROBBO JS HACK by Eru",

	fingerprint: { address: 0xA87F, bytes: [0xa2, 0xff, 0x9a, 0xa5, 0x9d, 0xa6, 0xa3, 0xf0] },   // the start-up code

	menu: {
		VIEW3D: { label: "Level view:", options: ["Atari", "OpenGL"], current: 1 },
		TEXTURES: { label: "Textures:", options: ["Original", "Smooth 4x"], current: 1 },
		TILT: { label: "Tilt:", options: ["Flat", "Slight", "More"], current: 1 },
	},

	onPostGlFrame() {
		view3d.options.smooth = this.menu.TEXTURES.current === 1;
		view3d.options.tilt = [0, 30, 45][this.menu.TILT.current];
		if (this.menu.VIEW3D.current === 1)
			view3d.render();
	},
};
