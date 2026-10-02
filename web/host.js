// The page's side of the web build: loads the emulator (a WebAssembly
// module, src/web/web.c), runs it frame by frame, shows its picture with
// WebGL, plays its sound, feeds it the keyboard, and hosts the game
// extensions: the same scripts as the native build's data/ext, run by the
// browser, with `a8` (api.js) and `gl` (glshim.js) as their two globals.
import createAtari800 from "./atari800.js";
import { makeGl } from "./glshim.js";
import { makeA8 } from "./api.js";
import { files } from "./std.js";

const BUFFER_W = 384, SCREEN_W = 336, SCREEN_H = 240, SCREEN_LEFT = 24;   // the emulator's screen buffer and the part shown
const AKEY_NONE = -1, AKEY_WARMSTART = -2, AKEY_COLDSTART = -3, AKEY_BREAK = -5, AKEY_SHFT = 0x40, AKEY_CTRL = 0x80;
const KEYS = {
	KeyA: 0x3f, KeyB: 0x15, KeyC: 0x12, KeyD: 0x3a, KeyE: 0x2a, KeyF: 0x38, KeyG: 0x3d, KeyH: 0x39, KeyI: 0x0d, KeyJ: 0x01, KeyK: 0x05, KeyL: 0x00, KeyM: 0x25,
	KeyN: 0x23, KeyO: 0x08, KeyP: 0x0a, KeyQ: 0x2f, KeyR: 0x28, KeyS: 0x3e, KeyT: 0x2d, KeyU: 0x0b, KeyV: 0x10, KeyW: 0x2e, KeyX: 0x16, KeyY: 0x2b, KeyZ: 0x17,
	Digit0: 0x32, Digit1: 0x1f, Digit2: 0x1e, Digit3: 0x1a, Digit4: 0x18, Digit5: 0x1d, Digit6: 0x1b, Digit7: 0x33, Digit8: 0x35, Digit9: 0x30,
	Space: 0x21, Enter: 0x0c, Escape: 0x1c, Backspace: 0x34, Tab: 0x2c, Comma: 0x20, Period: 0x22, Slash: 0x26, Semicolon: 0x02,
	Minus: 0x0e, Equal: 0x0f, CapsLock: 0x3c, Backquote: 0x27, F6: 0x11,
};
const JOYSTICK = { ArrowUp: 1, ArrowDown: 2, ArrowLeft: 4, ArrowRight: 8 }, TRIGGER = ["ControlRight", "AltRight", "Numpad0"];
const CONSOLE = { F4: 1, F3: 2, F2: 4 };   // Start, Select, Option

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
// (with ?report in the address every line is also sent to the server, where a test can read it in the access log)
const consoleLog = console.log.bind(console);
const log = (text) => {
	const out = $("log");
	if (out) { out.textContent += text + "\n"; out.scrollTop = out.scrollHeight; }
	consoleLog(text);
	if (params.has("report")) fetch("__log?" + encodeURIComponent(String(text).slice(0, 600))).catch(() => { });
};
console.log = (...args) => log(args.join(" "));   // what the extensions print shows in the page's log too

/* ------------------------------ the emulator ------------------------------ */

const M = await createAtari800({ print: (s) => { if (!/adler32/.test(s)) log(s); }, printErr: (s) => log(s) });
M.callMain(["-nobasic", "-320xe"]);
const heap = M.HEAPU8.buffer;
const screen = new Uint8Array(heap, M._web_screen(), BUFFER_W * SCREEN_H);
const colours = new Int32Array(heap, M._web_colours(), 256);

const canvas = $("screen");
const scale = Math.max(1, Math.round(window.devicePixelRatio || 1));
canvas.width = SCREEN_W * 2 * scale; canvas.height = SCREEN_H * 2 * scale;
const g = canvas.getContext("webgl2", { antialias: true, alpha: false, depth: true, preserveDrawingBuffer: true });
if (!g) { log("This page needs WebGL 2."); throw new Error("no WebGL 2"); }
const shim = makeGl(g, { width: SCREEN_W, height: SCREEN_H });

/* ------------------------------ sound ------------------------------ */

let audio = null, playhead = 0;
const soundOn = () => $("sound") === null || $("sound").checked;
function startAudio() {
	if (audio === null) { try { audio = new AudioContext({ sampleRate: M._web_sound_rate() }); } catch (e) { audio = null; } }
	if (audio !== null && audio.state === "suspended") audio.resume();
}
// The frame's samples (16 bits, as the emulator made them) queued right after the last frame's
function playFrame() {
	const bytes = M._web_sound_bytes(), channels = M._web_sound_channels();
	if (audio === null || audio.state !== "running" || !soundOn() || bytes === 0 || M._web_sound_sample_size() !== 2) return;
	const samples = new Int16Array(heap, M._web_sound(), bytes / 2), frames = samples.length / channels;
	const buffer = audio.createBuffer(channels, frames, M._web_sound_rate());
	for (let c = 0; c < channels; c++) { const out = buffer.getChannelData(c); for (let i = 0; i < frames; i++) out[i] = samples[i * channels + c] / 32768; }
	const now = audio.currentTime;
	if (playhead < now + 0.02 || playhead > now + 0.3) playhead = now + 0.06;   // fell behind, or ran ahead: start over a little ahead
	const source = audio.createBufferSource();
	source.buffer = buffer; source.connect(audio.destination); source.start(playhead);
	playhead += buffer.duration;
}

/* ------------------------------ keyboard ------------------------------ */

const held = new Set();
let lastKey = null;
window.addEventListener("keydown", (e) => {
	startAudio();
	if (e.target instanceof HTMLSelectElement || e.target instanceof HTMLInputElement && e.target.type !== "checkbox") return;
	if (e.code in KEYS || e.code in JOYSTICK || e.code in CONSOLE || TRIGGER.includes(e.code) || ["F5", "F7", "Tab"].includes(e.code)) e.preventDefault();
	held.add(e.code);
	if (e.code in KEYS) lastKey = e.code;
});
window.addEventListener("keyup", (e) => { held.delete(e.code); if (e.code === lastKey) lastKey = null; });
window.addEventListener("blur", () => { held.clear(); lastKey = null; });
window.addEventListener("pointerdown", startAudio);

function sendInput() {
	const shift = held.has("ShiftLeft") || held.has("ShiftRight"), control = held.has("ControlLeft");
	let key = AKEY_NONE;
	if (held.has("F5")) key = shift ? AKEY_COLDSTART : AKEY_WARMSTART;
	else if (held.has("F7")) key = AKEY_BREAK;
	else if (lastKey !== null && held.has(lastKey)) key = KEYS[lastKey] | (shift ? AKEY_SHFT : 0) | (control ? AKEY_CTRL : 0);
	let stick = 0, consol = 0;
	for (const [code, bit] of Object.entries(JOYSTICK)) if (held.has(code)) stick |= bit;
	for (const [code, bit] of Object.entries(CONSOLE)) if (held.has(code)) consol |= bit;
	M._web_input(key, shift ? 1 : 0, consol, stick, TRIGGER.some((code) => held.has(code)) ? 1 : 0, 0, 0);
}

/* ------------------------------ the extensions ------------------------------ */

const fileBytes = async (url) => { const r = await fetch(url); if (!r.ok) throw new Error(`${url}: ${r.status}`); return new Uint8Array(await r.arrayBuffer()); };

globalThis.a8 = makeA8(M, {
	accelerationDisabled: () => held.has("ControlLeft"),
	showFps: (text) => { $("fps").textContent = text; },
	files,
	audio: () => audio !== null && audio.state === "running" && soundOn() ? audio : null,
});
globalThis.gl = shim.gl;
// A raw RGBA picture, read from the files fetched for the extension
globalThis.gl.loadTextureRGBA = (path, width, height) => {
	const bytes = files.get(path);
	if (!bytes) throw new ReferenceError(`loadTextureRGBA: cannot open ${path}`);
	const texture = new shim.host.Texture(width, height);
	texture.pixels.set(bytes.subarray(0, texture.pixels.length));
	texture.finalize();
	return texture;
};

// dist/files.txt lists what ships under ext/: every directory with an init.js is an extension
const listing = (await (await fetch("files.txt")).text()).split("\n").filter(Boolean);
const extensions = [];
for (const path of listing.filter((p) => /^ext\/[^/]+\/init\.js$/.test(p))) {
	try {
		a8.extDir = path.replace(/\/init\.js$/, "");   // the extension's directory, as the native host sets it while loading
		const ext = (await import("./" + path)).default;
		ext.dir = a8.extDir;
		extensions.push(ext);
	}
	catch (e) { log(`extension ${path}: ${e.message}`); }
}
log(`${extensions.length} extensions: ${extensions.map((e) => e.dir.split("/").pop()).join(", ")}`);

let active = null, loading = null, pending = null, failed = false;
const extensionsOn = () => $("extensions") === null || $("extensions").checked;
const matches = (ext) => { const f = ext.fingerprint, mem = a8.mem; return f && f.bytes.every((b, i) => mem[f.address + i] === b); };
const wanted = params.get("ext");

function hook(name, ...args) {
	if (active === null || failed || typeof active[name] !== "function") return undefined;
	try { return active[name](...args); }
	catch (e) { failed = true; log(`${active.name}: ${name} failed: ${e.stack || e}`); return undefined; }
}

function buildMenu(ext) {
	const menu = $("menu");
	menu.textContent = "";
	for (const [key, item] of Object.entries(ext.menu || {})) {
		const label = document.createElement("label"), select = document.createElement("select");
		label.textContent = item.label + " ";
		item.options.forEach((text, i) => { const option = document.createElement("option"); option.value = i; option.textContent = text; select.append(option); });
		select.value = item.current;
		select.dataset.key = key;
		select.addEventListener("change", () => { item.current = +select.value; select.blur(); });
		label.append(select); menu.append(label);
	}
}

// The extension whose fingerprint is in memory: its data files are fetched first, since the scripts read them without waiting
async function activate(ext) {
	loading = ext;
	for (const path of listing.filter((p) => p.startsWith(ext.dir + "/") && !/\.(js|md)$/.test(p))) {
		if (!files.has(path)) { try { files.set(path, await fileBytes(path)); } catch (e) { log(e.message); } }
	}
	if (loading !== ext) return;   // something else was loaded meanwhile
	loading = null; failed = false; active = ext;
	$("extension").textContent = ext.name;
	// the menu can be preset from the address: ?menu=KEY:2,OTHER:0
	for (const part of (params.get("menu") || "").split(",").filter(Boolean)) { const [key, value] = part.split(":"); if (ext.menu && ext.menu[key]) ext.menu[key].current = +value; }
	buildMenu(ext);
	// its own corner of the page, empty: a8.panel, for whatever it wants to add there
	$("panel").textContent = "";
	a8.panel = $("panel");
	a8.extDir = ext.dir;
	a8.setCodeInjections(ext.codeInjections || []);
	hook("onActivate");
	log(`active extension: ${ext.name}`);
}

function deactivate() {
	active = loading = null; failed = false;
	a8.setCodeInjections([]);
	a8.panel = null;
	$("extension").textContent = "none"; $("menu").textContent = ""; $("panel").textContent = ""; $("fps").textContent = "";
}

M.onCodeInjection = (pc, op) => {
	if (active === null || failed || !extensionsOn() || typeof active.onCodeInjection !== "function") return op;
	try { return active.onCodeInjection(pc, op); }
	catch (e) { failed = true; log(`${active.name}: onCodeInjection failed: ${e.stack || e}`); return op; }
};

/* ------------------------------ loading programs ------------------------------ */

// A saved state (.a8s) or anything the emulator can boot (disk image, executable, cartridge)
function load(name, bytes) {
	deactivate();
	const path = "/" + name.replace(/[^A-Za-z0-9._-]/g, "_"), p = M.stringToNewUTF8(path);
	M.FS.writeFile(path, bytes);
	const ok = /\.a8s$/i.test(name) ? M._web_load_state(p) : M._web_open_file(p);
	M._free(p);
	log(ok ? `loaded ${name}` : `could not load ${name}`);
	$("drop").classList.toggle("hidden", !!ok);
	return ok;
}
window.addEventListener("dragover", (e) => e.preventDefault());
window.addEventListener("drop", async (e) => { e.preventDefault(); const file = e.dataTransfer.files[0]; if (file) load(file.name, new Uint8Array(await file.arrayBuffer())); });
$("file").addEventListener("change", async (e) => { const file = e.target.files[0]; if (file) load(file.name, new Uint8Array(await file.arrayBuffer())); e.target.blur(); });
// ?state=numen.a8s or ?file=game.xex in the address: fetched from the server
for (const key of ["state", "file"]) {
	const url = params.get(key);
	if (url) { try { load(key === "state" && !/\.a8s$/i.test(url) ? url + ".a8s" : url.split("/").pop(), await fileBytes(url)); } catch (e) { log(e.message); } }
}
// The framework's self-test, when the site has it: an extension with a small program of its own
if (listing.includes("ext/selftest/selftest.xex")) {
	const link = document.createElement("a");
	link.href = "?file=ext/selftest/selftest.xex"; link.textContent = "Extension self-test";
	$("demos").append(link);
}
// demos.json, if the site has one: [{ "title": ..., "state": ... or "file": ..., "ext": ..., "menu": ... }]
try {
	const demos = await (await fetch("demos.json")).json();
	for (const demo of demos) {
		const link = document.createElement("a"), query = new URLSearchParams();
		for (const key of ["state", "file", "ext", "menu"]) if (demo[key]) query.set(key, demo[key]);
		link.href = "?" + query; link.textContent = demo.title;
		$("demos").append(link);
	}
} catch (e) { /* no demos listed */ }

/* ------------------------------ the frame loop ------------------------------ */

const rgba = new Uint8Array(SCREEN_W * SCREEN_H * 4), rgba32 = new Uint32Array(rgba.buffer), abgr = new Uint32Array(256);
function showFrame() {
	shim.host.beginFrame();
	if (extensionsOn()) hook("onPreGlFrame");
	for (let i = 0; i < 256; i++) { const c = colours[i]; abgr[i] = 0xFF000000 | (c & 0xFF) << 16 | (c & 0xFF00) | (c >> 16) & 0xFF; }
	for (let y = 0, o = 0; y < SCREEN_H; y++) { const row = y * BUFFER_W + SCREEN_LEFT; for (let x = 0; x < SCREEN_W; x++) rgba32[o++] = abgr[screen[row + x]]; }
	shim.host.drawScreen(rgba);
	if (extensionsOn()) hook("onPostGlFrame");
}

let frames = 0;
function emulateFrame() {
	sendInput();
	M._web_frame();
	frames++;
	playFrame();
	if (!extensionsOn()) return;
	if (active === null && loading === null) {
		const ext = extensions.find((e) => (!wanted || e.name.includes(wanted) || e.dir.endsWith("/" + wanted)) && matches(e));
		if (ext) pending = activate(ext).finally(() => { pending = null; });
	}
	hook("onFrame");
}

// The emulator's frames are paced by the clock, not by the display's refresh: 49.86 a second in PAL, 59.92 in NTSC
let last = performance.now(), owed = 0;
function tick(now) {
	const period = 1000 / (M._web_is_pal() ? 49.8607 : 59.9227);
	owed = Math.min(owed + now - last, 4 * period);
	last = now;
	let ran = 0;
	while (owed >= period) { emulateFrame(); owed -= period; ran++; }
	if (ran) showFrame();
	requestAnimationFrame(tick);
}

// For tests: ?frames=N runs so many frames at once before the clock takes over
// (a headless browser shows few frames in its time budget), and ?trace logs
// the draw calls of the last of them
if (params.has("frames")) {
	for (let i = +params.get("frames"); i > 0; i--) {
		emulateFrame();
		if (pending !== null) await pending;
		if (i === 1 && params.has("trace")) shim.host.trace = [];
		showFrame();
	}
	if (shim.host.trace) { for (const line of shim.host.trace) log("draw " + JSON.stringify(line)); shim.host.trace = null; }
	log(`ran ${frames} frames`);
	last = performance.now();
}
requestAnimationFrame(tick);

// For tests and the curious
globalThis.atari800 = { M, load, frames: () => frames, active: () => active, emulateFrame, showFrame, canvas };
