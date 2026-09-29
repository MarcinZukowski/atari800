// Zybex: scrolling high-resolution background (grayscale or colour).
import { drawQuad } from "../common.js";

a8.register({
	name: "ZYBEX JS HACK by Eru",

	// Memory fingerprint that turns this extension on
	fingerprint: { address: 0x3000, bytes: [0x18, 0x69, 0x14, 0xA8, 0xC0, 0x50] },

	menu: {
		FPS: { label: "Display FPS:", options: ["OFF", "ON"], current: 1 },
		BKG: { label: "Background:", options: ["OFF", "GRAYSCALE", "COLOR"], current: 2 },
	},

	textures: null,   // [unused, grayscale, colour], filled lazily
	lastHscrol: 0,
	textureHscrol: 0,

	init() {
		if (this.textures)
			return;
		console.log("Loading Zybex textures");
		const load = (file) => {
			const t = gl.loadTextureRGBA(file, 512, 512);
			t.finalize();
			gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
			gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
			return t;
		};
		this.textures = [
			null,
			load("data/ext/zybex/bkg1-gs-512x512.rgba"),
			load("data/ext/zybex/bkg1-512x512.rgba"),
		];
	},

	onPreGlFrame() {
		if (this.menu.FPS.current === 1)
			a8.printFps(a8.antic.dlist, 0x9f, 0x90, 0, -2);
	},

	onPostGlFrame() {
		this.init();
		const mode = this.menu.BKG.current;
		if (mode === 0)
			return;

		// Screen coordinates
		const L = -0.95, R = 0.95, T = 0.78, B = -0.65;

		// When HSCROL changes the screen moved, so scroll the texture too
		const hscrol = a8.antic.hscrol;
		if (hscrol !== this.lastHscrol) {
			this.textureHscrol++;
			this.lastHscrol = hscrol;
		}

		// Texture coordinates
		const TL = 0.0015 * this.textureHscrol, TR = TL + 0.5, TT = 0.70, TB = 0.30;

		gl.BindTexture(gl.TEXTURE_2D, this.textures[mode].id);
		gl.Disable(gl.DEPTH_TEST);
		gl.Color4f(0.2, 0.2, 0.2, 0.5);
		gl.Enable(gl.BLEND);
		gl.BlendFunc(gl.ONE_MINUS_DST_COLOR, gl.ONE);
		drawQuad(TL, TR, TT, TB, L, R, T, B);
		gl.Color4f(1, 1, 1, 1);
		gl.Disable(gl.BLEND);
	},
});
