// The extension framework's self-test: a small extension that uses nearly
// every call of the API on a tiny Atari program of its own, checks what
// comes back, and shows the result on the screen and on the console.
//
//     atari800 -nobasic data/ext/selftest/selftest.xex      then TAB
//     A8_EXT_SELECT=SELF-TEST atari800 -nobasic data/ext/selftest/selftest.xex
//
// It doubles as an example of an extension: fingerprint, menu, hooks, code
// injections, files of its own, drawing.
//
// selftest.xex, at $2000:
//
//     2000  A9 00      LDA #$00
//     2002  85 80      STA $80         ; a counter of the loop's turns
//     2004  20 0C 20   JSR $200C       ; LOOP
//     2007  E6 80      INC $80         ; AFTER
//     2009  4C 04 20   JMP $2004
//     200C  A2 00      LDX #$00        ; WORK: fills page 6 with 0..255
//     200E  8A         TXA
//     200F  9D 00 06   STA $0600,X
//     2012  E8         INX
//     2013  D0 F9      BNE $200E
//     2015  60         RTS             ; DONE
import * as std from "std";

const mem = a8.mem;
const DIR = a8.extDir;   // this extension's directory, for the files it ships
const AFTER = 0x2007, WORK = 0x200C, DONE = 0x2015, PAGE = 0x0600, COUNTER = 0x80;
const WORK_INSTRUCTIONS = 1 + 256 * 4;   // LDX, then TXA STA INX BNE 256 times
const FONT = 0xE000;                     // the operating system's character set

/* ------------------------------ the tests' book-keeping ------------------------------ */

const results = [];   // { name, ok, detail }
function check(name, ok, detail = "") {
	results.push({ name, ok: !!ok, detail: String(detail) });
	console.log(`selftest: ${ok ? "PASS" : "FAIL"} ${name}${detail !== "" ? " (" + detail + ")" : ""}`);
}
function attempt(name, test) {
	try { test(); }
	catch (e) { check(name, false, e); }
}
const pageFilled = () => { for (let i = 0; i < 256; i++) if (mem[PAGE + i] !== i) return false; return true; };
const pageEmpty = () => { for (let i = 0; i < 256; i++) if (mem[PAGE + i] !== 0) return false; return true; };
const clearPage = () => mem.fill(0, PAGE, PAGE + 256);

/* ------------------------------ memory, registers, files ------------------------------ */

function testMachine() {
	attempt("a8.mem", () => {
		mem[0x0680] = 0x15A;   // a store wraps to a byte
		check("a8.mem", mem[0x2000] === 0xA9 && mem[0x0680] === 0x5A, `length ${mem.length}`);
	});
	attempt("a8.peek, a8.poke", () => {
		a8.poke(0xD01A, 0x46);   // through the hardware: the background colour register
		const vcount = a8.peek(0xD40B);
		check("a8.peek, a8.poke (the hardware)", a8.gtia.colbk === 0x46 && vcount >= 0 && vcount < 160 && a8.peek(0x2000) === 0xA9, `COLBK ${a8.gtia.colbk}, VCOUNT ${vcount}`);
	});
	attempt("a8.antic, a8.gtia", () => {
		const antic = ["dlist", "hscrol", "vscrol", "chbase", "pmbase", "dmactl"].map((r) => a8.antic[r]);
		const gtia = ["colbk", "colpf0", "colpf1", "colpf2", "colpf3", "colpm0", "prior", "gractl", "hposp0", "sizep0", "grafp0"].map((r) => a8.gtia[r]);
		check("a8.antic, a8.gtia", antic.concat(gtia).every((v) => Number.isInteger(v)) && a8.antic.dlist > 0, `display list at $${a8.antic.dlist.toString(16)}`);
	});
	attempt("a8.palette, a8.rgb", () => {
		const white = a8.rgb(0x0F), black = a8.rgb(0x00);
		check("a8.palette, a8.rgb", a8.palette.length === 256 && Math.min(...white) > 180 && Math.max(...black) < 40
			&& ((a8.palette[0x0F] >> 16) & 255) === white[0], `white ${white}`);
	});
	attempt("a8.extDir, std", () => {
		const text = std.loadFile(`${DIR}/selftest.txt`);
		const f = std.open(`${DIR}/selftest.txt`, "rb"), bytes = new Uint8Array(5);
		if (f !== null) { f.seek(8, std.SEEK_SET); f.read(bytes.buffer, 0, 5); f.close(); }
		check("a8.extDir, the extension's files", text !== null && text.startsWith("atari800 extension self-test") && String.fromCharCode(...bytes) === " exte", DIR);
	});
	attempt("a8.xeBank", () => {
		const bank = a8.xeBank(0);
		check("a8.xeBank", bank === null || bank.length === 16384, bank === null ? "no extended memory" : "16 KB");
	});
	attempt("a8.profile", () => {
		a8.profileReset();
		const counts = a8.profile("count");
		check("a8.profile, a8.profileReset", counts === null || counts.length === 65536, counts === null ? "built without the profile" : "65536 counts");
	});
	attempt("a8.loadSound", () => {
		beep = a8.loadSound(`${DIR}/beep.wav`);
		check("a8.loadSound", beep !== null && typeof beep.play === "function");
	});
	attempt("a8.accelerationDisabled", () => check("a8.accelerationDisabled", typeof a8.accelerationDisabled() === "boolean"));
}

/* ------------------------------ code injections and the fake CPU ------------------------------ */

// The program calls WORK over and over: each call is one step of the test,
// and the instruction after the call (AFTER) is where a step's effect shows
let call = 0, afterHits = 0, afterHitsBefore = 0, cpuDone = false, pending = null;
function atWork(pc, op) {
	call++;
	switch (call) {
	case 1:
		check("onCodeInjection at the addresses asked", pc === WORK && op === 0xA2, `pc $${pc.toString(16)}, opcode $${op.toString(16)}`);
		a8.cpu.y = 0x42;   // the routine leaves Y alone
		pending = () => check("a8.cpu; letting the routine run", a8.cpu.y === 0x42 && pageFilled() && a8.cpu.s <= 0xFF, `Y $${a8.cpu.y.toString(16)}`);
		clearPage();
		return op;
	case 2:
		clearPage();
		pending = () => check("a8.OP_RTS skips the routine", pageEmpty());
		return a8.OP_RTS;
	case 3: {
		clearPage();
		const r = a8.fakeCpuUntilAfterOp(a8.OP_RTS);
		check("a8.fakeCpuUntilAfterOp", r === a8.OP_NOP && pageFilled() && a8.cpu.pc === AFTER, `pc $${a8.cpu.pc.toString(16)}`);
		return r;
	}
	case 4: {
		clearPage();
		const r = a8.fakeCpuUntilPc(DONE);
		check("a8.fakeCpuUntilPc", r === a8.OP_NOP && pageFilled() && a8.cpu.pc === DONE, `pc $${a8.cpu.pc.toString(16)}`);
		return r;
	}
	case 5: {
		clearPage();
		const r = a8.fakeCpuUntilOp(a8.OP_RTS);
		check("a8.fakeCpuUntilOp", r === a8.OP_NOP && pageFilled() && mem[a8.cpu.pc] === a8.OP_RTS, `pc $${a8.cpu.pc.toString(16)}`);
		return r;
	}
	case 6: {
		clearPage();
		const n = a8.fakeCpuWhileIn(WORK, DONE - 1, 100000);
		check("a8.fakeCpuWhileIn, counting", n === WORK_INSTRUCTIONS && pageFilled() && a8.cpu.pc === DONE, `${n} instructions`);
		return a8.OP_NOP;
	}
	case 7: {
		const n = a8.fakeCpuWhileIn(WORK, DONE - 1, 10);
		check("a8.fakeCpuWhileIn, out of budget", n === -10, `returned ${n}`);
		return a8.OP_NOP;
	}
	case 8:
		a8.setCodeInjections([WORK]);   // AFTER is no longer asked for
		afterHitsBefore = afterHits;
		return op;
	case 9:
		check("a8.setCodeInjections", afterHits === afterHitsBefore, `${afterHits - afterHitsBefore} calls at the dropped address`);
		a8.setCodeInjections([]);       // done: the program runs untouched from here
		cpuDone = true;
		return op;
	}
	return op;
}

/* ------------------------------ drawing ------------------------------ */

let glDone = false, atlas = null, beep = null;

// The pixel at a place given in the picture's own coordinates (-1..1, y up)
function pixelAt(x, y) {
	const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
	return gl.readPixels(Math.floor(vx + (x + 1) / 2 * vw), Math.floor(vy + (y + 1) / 2 * vh), 1, 1);
}
const near = (pixel, r, g, b, tolerance = 12) => Math.abs(pixel[0] - r) <= tolerance && Math.abs(pixel[1] - g) <= tolerance && Math.abs(pixel[2] - b) <= tolerance;
function quad(x, y, size, z = -2) {
	gl.Begin(gl.QUADS);
	gl.TexCoord2f(0, 1); gl.Vertex3f(x, y, z);
	gl.TexCoord2f(1, 1); gl.Vertex3f(x + size, y, z);
	gl.TexCoord2f(1, 0); gl.Vertex3f(x + size, y + size, z);
	gl.TexCoord2f(0, 0); gl.Vertex3f(x, y + size, z);
	gl.End();
}

// Each test draws a small square in a row along the bottom and reads its middle back
function testDrawing() {
	const S = 0.1, Y = -0.9, at = (i) => -0.95 + i * 0.12, middle = (i) => pixelAt(at(i) + S / 2, Y + S / 2);
	gl.PushAttrib(gl.ALL_ATTRIB_BITS);
	gl.Disable(gl.TEXTURE_2D); gl.Disable(gl.BLEND); gl.Disable(gl.DEPTH_TEST); gl.Disable(gl.FOG); gl.Disable(gl.CULL_FACE);

	attempt("gl.GetIntegerv, gl.screenSize", () => {
		const viewport = gl.GetIntegerv(gl.VIEWPORT), size = gl.screenSize();
		check("gl.GetIntegerv, gl.screenSize", viewport.length === 4 && viewport[2] > 0 && viewport[3] > 0 && size[0] >= 320 && size[1] >= 192, `viewport ${viewport}, screen ${size}`);
	});
	attempt("quads", () => {
		gl.Color4f(1, 0, 0, 1); quad(at(0), Y, S);
		check("gl.Begin/Vertex3f/Color4f, readPixels", near(middle(0), 255, 0, 0), middle(0).slice(0, 3));
	});
	attempt("textures", () => {
		const t = gl.createTexture(2, 2);
		for (let i = 0; i < 4; i++) t.pixels.set([0, 255, 0, 255], 4 * i);
		t.finalize();
		gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.Enable(gl.TEXTURE_2D); gl.BindTexture(gl.TEXTURE_2D, t.id); gl.Color4f(1, 1, 1, 1);
		quad(at(1), Y, S);
		const plain = middle(1);
		gl.Color4f(0.5, 0.5, 0.5, 1); quad(at(2), Y, S);   // the colour multiplies the texture
		gl.Disable(gl.TEXTURE_2D);
		check("gl.createTexture; colour x texture", t.width === 2 && near(plain, 0, 255, 0) && near(middle(2), 0, 128, 0), `${plain.slice(0, 3)} and ${middle(2).slice(0, 3)}`);
	});
	attempt("loadTextureRGBA", () => {
		const t = gl.loadTextureRGBA(`${DIR}/selftest.rgba`, 4, 4);
		t.finalize();
		gl.Enable(gl.TEXTURE_2D); gl.Color4f(1, 1, 1, 1);
		t.draw(0, 1, 0, 1, at(3), at(3) + S, Y + S, Y);
		gl.Disable(gl.TEXTURE_2D);
		check("gl.loadTextureRGBA, Texture.draw", near(middle(3), 255, 128, 0), middle(3).slice(0, 3));
	});
	attempt("blending", () => {
		gl.Color4f(1, 1, 1, 1); quad(at(4), Y, S);
		gl.Enable(gl.BLEND); gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
		gl.Color4f(0, 0, 0, 0.5); quad(at(4), Y, S);
		gl.Disable(gl.BLEND);
		check("gl.BlendFunc", near(middle(4), 128, 128, 128, 16), middle(4).slice(0, 3));
	});
	attempt("matrices", () => {
		gl.MatrixMode(gl.MODELVIEW); gl.PushMatrix();
		gl.Translatef(at(5), Y, 0); gl.Scalef(S, S, 1);
		gl.Color4f(0, 0, 1, 1); quad(0, 0, 1);
		gl.PopMatrix();
		const moved = middle(5);
		gl.MatrixMode(gl.PROJECTION); gl.PushMatrix(); gl.LoadIdentity(); gl.Ortho(0, 20, 0, 20, -1, 1);   // the picture as 20 x 20 units
		gl.MatrixMode(gl.MODELVIEW); gl.PushMatrix(); gl.LoadIdentity();
		gl.Color4f(1, 1, 0, 1); quad((at(6) + 1) * 10, (Y + 1) * 10, S * 10, 0);
		gl.PopMatrix(); gl.MatrixMode(gl.PROJECTION); gl.PopMatrix(); gl.MatrixMode(gl.MODELVIEW);
		check("gl matrix stacks, Ortho", near(moved, 0, 0, 255) && near(middle(6), 255, 255, 0), `${moved.slice(0, 3)} and ${middle(6).slice(0, 3)}`);
	});
	attempt("fog", () => {
		gl.Enable(gl.FOG);
		gl.Fogf(gl.FOG_MODE, gl.LINEAR); gl.Fogf(gl.FOG_START, 0); gl.Fogf(gl.FOG_END, 1); gl.Fogfv(gl.FOG_COLOR, [0, 1, 1, 1]);
		gl.Color4f(1, 0, 0, 1); quad(at(7), Y, S);   // two units deep: wholly in the fog
		gl.Disable(gl.FOG);
		check("gl.Fogf", near(middle(7), 0, 255, 255), middle(7).slice(0, 3));
	});
	attempt("scissor, attributes", () => {
		const [vx, vy, vw, vh] = gl.GetIntegerv(gl.VIEWPORT);
		gl.Color4f(0, 0, 0, 1); quad(at(8), Y, S); quad(at(9), Y, S);
		gl.PushAttrib(gl.SCISSOR_BIT | gl.ENABLE_BIT);
		gl.Enable(gl.SCISSOR_TEST);
		gl.Scissor(Math.round(vx + (at(8) + 1) / 2 * vw), Math.round(vy + (Y + 1) / 2 * vh), Math.round(S / 2 * vw), Math.round(S / 2 * vh));
		gl.ClearColor(1, 0, 1, 1); gl.Clear(gl.COLOR_BUFFER_BIT);   // the clear is held to the scissor box
		gl.PopAttrib();
		gl.Color4f(0, 1, 0, 1); gl.Begin(gl.TRIANGLES);            // and after PopAttrib nothing is: a triangle beside it
		gl.Vertex3f(at(9), Y, -2); gl.Vertex3f(at(9) + S, Y, -2); gl.Vertex3f(at(9) + S / 2, Y + S, -2);
		gl.End();
		check("gl.Scissor, gl.PushAttrib/PopAttrib", near(middle(8), 255, 0, 255) && near(middle(9), 0, 255, 0) && !near(pixelAt(at(8) - 0.01, Y + S / 2), 255, 0, 255),
			`${middle(8).slice(0, 3)} and ${middle(9).slice(0, 3)}`);
	});
	attempt("drawTriangles", () => {
		const x = at(10);
		gl.Color4f(1, 0.5, 0, 1);
		gl.drawTriangles(new Float32Array([x, Y, -2, x + S, Y, -2, x + S, Y + S, -2, x, Y, -2, x + S, Y + S, -2, x, Y + S, -2]));
		check("gl.drawTriangles", near(middle(10), 255, 128, 0), middle(10).slice(0, 3));
	});
	attempt("drawScreen", () => {
		const [w, h] = gl.screenSize(), source = pixelAt(0, 0.5);   // a piece of the Atari's own picture, a quarter down from the top
		gl.Enable(gl.TEXTURE_2D); gl.Color4f(1, 1, 1, 1);
		gl.drawScreen(w / 2 - 2, h / 4 - 2, w / 2 + 2, h / 4 + 2, at(11), at(11) + S, Y + S, Y);
		gl.Disable(gl.TEXTURE_2D);
		check("gl.drawScreen", near(middle(11), source[0], source[1], source[2], 24), `${middle(11).slice(0, 3)} for ${source.slice(0, 3)}`);
	});
	attempt("lines", () => {
		gl.Color4f(1, 1, 1, 1); gl.LineWidth(6);
		gl.Begin(gl.LINES); gl.Vertex3f(at(12), Y + S / 2, -2); gl.Vertex3f(at(12) + S, Y + S / 2, -2); gl.End();
		gl.LineWidth(1);
		check("gl.LINES, gl.LineWidth", near(middle(12), 255, 255, 255, 40), middle(12).slice(0, 3));
	});
	gl.PopAttrib();
	glDone = true;
}

// The report: the results as text, in the operating system's own characters
// (a texture made of the bytes of its character set)
function buildAtlas() {
	atlas = gl.createTexture(128, 64);
	for (let c = 0; c < 128; c++) for (let row = 0; row < 8; row++) for (let bit = 0; bit < 8; bit++) {
		const on = (mem[FONT + c * 8 + row] >> (7 - bit)) & 1, o = 4 * (((c >> 4) * 8 + row) * 128 + (c & 15) * 8 + bit);
		atlas.pixels[o] = atlas.pixels[o + 1] = atlas.pixels[o + 2] = 255; atlas.pixels[o + 3] = on ? 255 : 0;
	}
	atlas.finalize();
	gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.TexParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
}
function text(line, column, row, colour) {
	const W = 2 / 84, H = 2 / 30;   // 84 x 30 characters over the picture: narrow ones, as in an 80-column mode
	gl.Color4f(colour[0], colour[1], colour[2], 1);
	gl.Begin(gl.QUADS);
	for (let i = 0; i < line.length; i++) {
		const a = line.charCodeAt(i), c = a < 32 ? a + 64 : a < 96 ? a - 32 : a & 127;   // ASCII to the character set's order
		const u = (c & 15) / 16, v = (c >> 4) / 8, x = -1 + (column + i) * W, y = 1 - (row + 1) * H;
		gl.TexCoord2f(u, v + 1 / 8); gl.Vertex3f(x, y, -2);
		gl.TexCoord2f(u + 1 / 16, v + 1 / 8); gl.Vertex3f(x + W, y, -2);
		gl.TexCoord2f(u + 1 / 16, v); gl.Vertex3f(x + W, y + H, -2);
		gl.TexCoord2f(u, v); gl.Vertex3f(x, y + H, -2);
	}
	gl.End();
}
function drawReport() {
	if (atlas === null) buildAtlas();
	const failed = results.filter((r) => !r.ok);
	gl.PushAttrib(gl.ALL_ATTRIB_BITS);
	gl.Disable(gl.DEPTH_TEST); gl.Disable(gl.FOG);
	gl.Enable(gl.BLEND); gl.BlendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
	gl.Disable(gl.TEXTURE_2D);
	gl.Color4f(0, 0, 0, 0.8);
	gl.Begin(gl.QUADS); gl.Vertex3f(-1, -1, -2); gl.Vertex3f(1, -1, -2); gl.Vertex3f(1, 1, -2); gl.Vertex3f(-1, 1, -2); gl.End();
	gl.Enable(gl.TEXTURE_2D); gl.BindTexture(gl.TEXTURE_2D, atlas.id);
	text("EXTENSION SELF-TEST", 2, 1, [1, 1, 1]);
	text(`${results.length - failed.length} passed, ${failed.length} failed`, 2, 2, failed.length ? [1, 0.4, 0.3] : [0.4, 1, 0.5]);
	// two columns of 24 lines
	results.slice(0, 48).forEach((r, i) => text((r.ok ? "+ " : "- ") + r.name.slice(0, 38), 2 + 41 * Math.floor(i / 24), 4 + i % 24, r.ok ? [0.7, 0.9, 0.7] : [1, 0.4, 0.3]));
	gl.PopAttrib();
	gl.Color4f(1, 1, 1, 1);
}

/* ------------------------------ the extension ------------------------------ */

let frames = 0, glFrames = 0, summarised = false;
function summarise() {
	const failed = results.filter((r) => !r.ok);
	console.log(`selftest: ${results.length - failed.length} passed, ${failed.length} failed` + (glDone ? "" : " (the drawing tests need an OpenGL display)"));
	for (const r of failed) console.log(`selftest: failed: ${r.name} (${r.detail})`);
	summarised = true;
}

export default {
	name: "SELF-TEST of the extension API",

	// The program's first bytes
	fingerprint: { address: 0x2000, bytes: [0xA9, 0x00, 0x85, 0x80, 0x20, 0x0C, 0x20, 0xE6, 0x80, 0x4C, 0x04, 0x20] },

	menu: {
		REPORT: { label: "Report:", options: ["Shown", "Hidden"], current: 0 },
		SOUND: { label: "Sound when done:", options: ["OFF", "ON"], current: 0 },
	},

	codeInjections: [WORK, AFTER],

	onActivate() {
		results.length = 0; call = afterHits = frames = glFrames = 0; cpuDone = glDone = summarised = false; pending = null;
		a8.setCodeInjections([WORK, AFTER]);
		check("onActivate, the menu", typeof this.menu.REPORT.current === "number");
		testMachine();
	},

	onCodeInjection(pc, op) {
		if (pc === AFTER) {
			afterHits++;
			if (pending !== null) { const test = pending; pending = null; attempt("after a call", test); }
			return op;
		}
		try { return atWork(pc, op); }
		catch (e) { check(`step ${call} of the code injection tests`, false, e); cpuDone = true; a8.setCodeInjections([]); return op; }
	},

	onFrame() {
		frames++;
		if (frames === 2) check("onFrame", mem[COUNTER] !== 0 || call > 0, `${call} calls of the routine so far`);
		// without an OpenGL display the drawing hooks never come: report what there is
		if (!summarised && cpuDone && frames > 100 && glFrames === 0) summarise();
	},

	onPreGlFrame() {
		a8.printFps(mem[0x14], 0x0F, 0x00, 0, 0);   // into the Atari's own screen, before it is shown
	},

	onPostGlFrame() {
		glFrames++;
		if (!glDone && cpuDone && glFrames > 3) {
			attempt("the drawing tests", testDrawing);
			glDone = true;
			summarise();
			if (this.menu.SOUND.current === 1 && beep !== null) beep.play();
		}
		if (this.menu.REPORT.current === 0 && summarised) drawReport();
	},
};
