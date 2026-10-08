// The extensions' `gl` object in the browser: the same calls the native build
// binds to fixed-function OpenGL (src/sdl/video_gl-js.c), implemented here on
// WebGL 2. Only what the binding offers is covered: immediate mode, the
// matrix stacks, one texture unit modulated by the colour, blending, fog,
// scissor and the attribute stack. One shader does all of it; every
// Begin/End pair is one draw call.

// The constants keep OpenGL's own values, so blend factors and texture
// parameters pass straight through to WebGL
const C = {
	BLEND: 0x0BE2, DEPTH_TEST: 0x0B71, LIGHTING: 0x0B50, LIGHT0: 0x4000, LIGHT1: 0x4001, FOG: 0x0B60, SCISSOR_TEST: 0x0C11,
	TEXTURE_2D: 0x0DE1, CULL_FACE: 0x0B44, LINE_SMOOTH: 0x0B20, COLOR_MATERIAL: 0x0B57, NORMALIZE: 0x0BA1, MULTISAMPLE: 0x809D,
	MODELVIEW: 0x1700, PROJECTION: 0x1701, TEXTURE: 0x1702,
	VIEWPORT: 0x0BA2, SCISSOR_BOX: 0x0C10, SAMPLES: 0x80A9,
	COLOR_BUFFER_BIT: 0x4000, DEPTH_BUFFER_BIT: 0x0100, ALL_ATTRIB_BITS: 0xFFFFF, ENABLE_BIT: 0x2000, LIGHTING_BIT: 0x0040,
	CURRENT_BIT: 0x0001, LINE_BIT: 0x0004, POLYGON_BIT: 0x0008, TEXTURE_BIT: 0x40000, TRANSFORM_BIT: 0x1000, VIEWPORT_BIT: 0x0800, SCISSOR_BIT: 0x80000,
	POINTS: 0, LINES: 1, LINE_LOOP: 2, LINE_STRIP: 3, TRIANGLES: 4, TRIANGLE_STRIP: 5, TRIANGLE_FAN: 6, QUADS: 7, QUAD_STRIP: 8, POLYGON: 9,
	ZERO: 0, ONE: 1, SRC_COLOR: 0x0300, ONE_MINUS_SRC_COLOR: 0x0301, SRC_ALPHA: 0x0302, ONE_MINUS_SRC_ALPHA: 0x0303,
	DST_ALPHA: 0x0304, ONE_MINUS_DST_ALPHA: 0x0305, DST_COLOR: 0x0306, ONE_MINUS_DST_COLOR: 0x0307,
	TEXTURE_WRAP_S: 0x2802, TEXTURE_WRAP_T: 0x2803, TEXTURE_MIN_FILTER: 0x2801, TEXTURE_MAG_FILTER: 0x2800,
	REPEAT: 0x2901, CLAMP: 0x2900, CLAMP_TO_EDGE: 0x812F, CLAMP_TO_BORDER: 0x812D, NEAREST: 0x2600, LINEAR: 0x2601,
	NEAREST_MIPMAP_NEAREST: 0x2700, LINEAR_MIPMAP_NEAREST: 0x2701, NEAREST_MIPMAP_LINEAR: 0x2702, LINEAR_MIPMAP_LINEAR: 0x2703,
	GENERATE_MIPMAP: 0x8191, TRUE: 1, FALSE: 0,
	FOG_MODE: 0x0B65, FOG_START: 0x0B63, FOG_END: 0x0B64, FOG_DENSITY: 0x0B62, FOG_COLOR: 0x0B66, EXP: 0x0800, EXP2: 0x0801,
	FRONT: 0x0404, BACK: 0x0405, FRONT_AND_BACK: 0x0408, POINT: 0x1B00, LINE: 0x1B01, FILL: 0x1B02, CW: 0x0900, CCW: 0x0901,
	POSITION: 0x1203, AMBIENT: 0x1200, DIFFUSE: 0x1201, SPECULAR: 0x1202,
};

const VERTEX_SHADER = `#version 300 es
in vec4 aPosition; in vec4 aColour; in vec2 aTexCoord;
uniform mat4 uProjection, uModelView;
out vec4 vColour; out vec2 vTexCoord; out float vDepth;
void main() {
	vec4 eye = uModelView * aPosition;
	gl_Position = uProjection * eye;
	vColour = aColour; vTexCoord = aTexCoord; vDepth = abs(eye.z);
}`;
const FRAGMENT_SHADER = `#version 300 es
precision highp float;
in vec4 vColour; in vec2 vTexCoord; in float vDepth;
uniform sampler2D uTexture;
uniform bool uTextured;
uniform bvec2 uBorder;          // a texture clamped to its border is transparent outside
uniform int uFog;               // 0 none, 1 linear, 2 exp, 3 exp2
uniform vec3 uFogColour; uniform vec3 uFogParams;   // density, start, end
out vec4 colour;
void main() {
	colour = vColour;
	if (uTextured) {
		vec4 texel = texture(uTexture, vTexCoord);
		if ((uBorder.x && (vTexCoord.x < 0.0 || vTexCoord.x > 1.0)) || (uBorder.y && (vTexCoord.y < 0.0 || vTexCoord.y > 1.0))) texel = vec4(0.0);
		colour *= texel;
	}
	if (uFog != 0) {
		float f = uFog == 1 ? (uFogParams.z - vDepth) / (uFogParams.z - uFogParams.y)
			: uFog == 2 ? exp(-uFogParams.x * vDepth) : exp(-uFogParams.x * uFogParams.x * vDepth * vDepth);
		colour.rgb = mix(uFogColour, colour.rgb, clamp(f, 0.0, 1.0));
	}
}`;

const identity = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
// a * b, both column-major
function multiply(a, b) {
	const out = new Array(16);
	for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++)
		out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
	return out;
}

const FLOATS = 10;   // per vertex: position 4 (x, y, z, w), colour 4, texture coordinate 2

// g: the WebGL2 context; screen: { width, height } of the Atari picture shown
export function makeGl(g, screen) {
	const program = g.createProgram();
	for (const [type, source] of [[g.VERTEX_SHADER, VERTEX_SHADER], [g.FRAGMENT_SHADER, FRAGMENT_SHADER]]) {
		const shader = g.createShader(type);
		g.shaderSource(shader, source); g.compileShader(shader);
		if (!g.getShaderParameter(shader, g.COMPILE_STATUS)) throw new Error("gl shim shader: " + g.getShaderInfoLog(shader));
		g.attachShader(program, shader);
	}
	g.bindAttribLocation(program, 0, "aPosition"); g.bindAttribLocation(program, 1, "aColour"); g.bindAttribLocation(program, 2, "aTexCoord");
	g.linkProgram(program);
	if (!g.getProgramParameter(program, g.LINK_STATUS)) throw new Error("gl shim program: " + g.getProgramInfoLog(program));
	g.useProgram(program);
	const uniform = {};
	for (const name of ["uProjection", "uModelView", "uTexture", "uTextured", "uBorder", "uFog", "uFogColour", "uFogParams"]) uniform[name] = g.getUniformLocation(program, name);
	g.uniform1i(uniform.uTexture, 0);
	const vao = g.createVertexArray(), buffer = g.createBuffer();
	g.bindVertexArray(vao); g.bindBuffer(g.ARRAY_BUFFER, buffer);
	g.enableVertexAttribArray(0); g.vertexAttribPointer(0, 4, g.FLOAT, false, FLOATS * 4, 0);
	g.enableVertexAttribArray(1); g.vertexAttribPointer(1, 4, g.FLOAT, false, FLOATS * 4, 16);
	g.enableVertexAttribArray(2); g.vertexAttribPointer(2, 2, g.FLOAT, false, FLOATS * 4, 32);

	/* ------------------------------ state ------------------------------ */

	const textures = new Map();   // id -> { handle, border: [s, t], mipmap, uploaded }
	let nextTexture = 1;
	const defaults = () => ({
		enabled: new Set([C.TEXTURE_2D, C.BLEND, C.MULTISAMPLE]),
		colour: [1, 1, 1, 1], texCoord: [0, 0],
		blend: [C.SRC_ALPHA, C.ONE_MINUS_SRC_ALPHA],
		fog: { mode: C.EXP, density: 1, start: 0, end: 1, colour: [0, 0, 0, 0] },
		polygonMode: C.FILL, lineWidth: 1,
		viewport: [0, 0, g.drawingBufferWidth, g.drawingBufferHeight], scissor: [0, 0, g.drawingBufferWidth, g.drawingBufferHeight],
		clearColour: [0, 0, 0, 0], bound: 0, matrixMode: C.MODELVIEW,
	});
	let state = defaults();
	const attribStack = [];
	const matrices = { [C.MODELVIEW]: [identity()], [C.PROJECTION]: [identity()], [C.TEXTURE]: [identity()] };
	const stack = () => matrices[state.matrixMode];
	const top = (mode) => { const s = matrices[mode]; return s[s.length - 1]; };
	const apply = (m) => { const s = stack(); s[s.length - 1] = multiply(s[s.length - 1], m); };

	// The capabilities WebGL has too
	const native = { [C.BLEND]: g.BLEND, [C.DEPTH_TEST]: g.DEPTH_TEST, [C.SCISSOR_TEST]: g.SCISSOR_TEST, [C.CULL_FACE]: g.CULL_FACE };
	const setEnabled = (cap, on) => {
		if (on) state.enabled.add(cap); else state.enabled.delete(cap);
		if (native[cap] !== undefined) { if (on) g.enable(native[cap]); else g.disable(native[cap]); }
	};
	// Brings WebGL in line with the whole of `state` (after PopAttrib and at the start of a frame)
	function syncState() {
		for (const cap of Object.keys(native)) { if (state.enabled.has(+cap)) g.enable(native[cap]); else g.disable(native[cap]); }
		g.blendFunc(state.blend[0], state.blend[1]);
		g.viewport(...state.viewport); g.scissor(...state.scissor);
		g.clearColor(...state.clearColour);
		const t = textures.get(state.bound);
		g.bindTexture(g.TEXTURE_2D, t ? t.handle : null);
	}

	/* ------------------------------ drawing ------------------------------ */

	let data = new Float32Array(FLOATS * 4096), count = 0, mode = -1;
	// w: the homogeneous coordinate (1 but for Vertex4f), by which GL divides the
	// position and interpolates the texture coordinates in perspective
	const push = (x, y, z, colour, s, t, w = 1) => {
		if ((count + 1) * FLOATS > data.length) { const bigger = new Float32Array(data.length * 2); bigger.set(data); data = bigger; }
		const o = count++ * FLOATS;
		data[o] = x; data[o + 1] = y; data[o + 2] = z; data[o + 3] = w;
		data[o + 4] = colour[0]; data[o + 5] = colour[1]; data[o + 6] = colour[2]; data[o + 7] = colour[3];
		data[o + 8] = s; data[o + 9] = t;
	};
	// Vertex `from` copied to the end (for turning quads and fans into triangles)
	const repeat = (list, from) => { for (let i = 0; i < FLOATS; i++) list.push(data[from * FLOATS + i]); };

	// Draws `vertices` (a Float32Array of whole triangles) with the current state; flat: already in clip space
	function drawTriangleData(vertices, flat) {
		const texture = textures.get(state.bound);
		const textured = state.enabled.has(C.TEXTURE_2D) && texture !== undefined && texture.uploaded;
		if (host.trace) host.trace.push({ vertices: vertices.length / FLOATS, textured, bound: state.bound, blend: state.enabled.has(C.BLEND) ? state.blend.map((v) => v.toString(16)).join("/") : "off", colour: Array.from(vertices.subarray(4, 8)).map((v) => +v.toFixed(2)), first: Array.from(vertices.subarray(0, 3)).map((v) => +v.toFixed(2)), tex: Array.from(vertices.subarray(8, 10)).map((v) => +v.toFixed(3)), error: g.getError() });
		g.uniformMatrix4fv(uniform.uProjection, false, flat ? identity() : top(C.PROJECTION));
		g.uniformMatrix4fv(uniform.uModelView, false, flat ? identity() : top(C.MODELVIEW));
		g.uniform1i(uniform.uTextured, textured ? 1 : 0);
		if (textured) g.uniform2i(uniform.uBorder, texture.border[0] ? 1 : 0, texture.border[1] ? 1 : 0);
		const fog = state.enabled.has(C.FOG) && !flat ? (state.fog.mode === C.LINEAR ? 1 : state.fog.mode === C.EXP2 ? 3 : 2) : 0;
		g.uniform1i(uniform.uFog, fog);
		if (fog) { g.uniform3f(uniform.uFogColour, state.fog.colour[0], state.fog.colour[1], state.fog.colour[2]); g.uniform3f(uniform.uFogParams, state.fog.density, state.fog.start, state.fog.end); }
		g.bufferData(g.ARRAY_BUFFER, vertices, g.STREAM_DRAW);
		g.drawArrays(g.TRIANGLES, 0, vertices.length / FLOATS);
	}

	// A line as a quad of the line width, worked out in window pixels (WebGL draws lines one pixel wide)
	function lineQuads(pairs) {
		const pm = multiply(top(C.PROJECTION), top(C.MODELVIEW)), [, , vw, vh] = state.viewport, out = [];
		const clip = (i) => {
			const x = data[i * FLOATS], y = data[i * FLOATS + 1], z = data[i * FLOATS + 2];
			const w = pm[3] * x + pm[7] * y + pm[11] * z + pm[15];
			return w > 1e-6 ? [(pm[0] * x + pm[4] * y + pm[8] * z + pm[12]) / w, (pm[1] * x + pm[5] * y + pm[9] * z + pm[13]) / w, (pm[2] * x + pm[6] * y + pm[10] * z + pm[14]) / w] : null;
		};
		for (const [a, b] of pairs) {
			const p = clip(a), q = clip(b);
			if (p === null || q === null) continue;
			const dx = (q[0] - p[0]) * vw, dy = (q[1] - p[1]) * vh, length = Math.hypot(dx, dy) || 1;
			const ox = -dy / length * state.lineWidth / vw, oy = dx / length * state.lineWidth / vh;   // across, in clip units
			const corner = (point, source, sign) => out.push(point[0] + sign * ox, point[1] + sign * oy, point[2], 1,
				data[source * FLOATS + 4], data[source * FLOATS + 5], data[source * FLOATS + 6], data[source * FLOATS + 7], 0, 0);
			corner(p, a, -1); corner(p, a, 1); corner(q, b, 1);
			corner(p, a, -1); corner(q, b, 1); corner(q, b, -1);
		}
		return new Float32Array(out);
	}

	function end() {
		const n = count, m = mode;
		count = 0; mode = -1;
		if (n === 0) return;
		const outline = state.polygonMode === C.LINE && m >= C.TRIANGLES;
		if (m === C.LINES || m === C.LINE_STRIP || m === C.LINE_LOOP || outline) {
			const pairs = [];
			if (m === C.LINES) for (let i = 0; i + 1 < n; i += 2) pairs.push([i, i + 1]);
			else if (m === C.QUADS && outline) for (let i = 0; i + 3 < n; i += 4) for (let k = 0; k < 4; k++) pairs.push([i + k, i + (k + 1) % 4]);
			else if (m === C.TRIANGLES && outline) for (let i = 0; i + 2 < n; i += 3) for (let k = 0; k < 3; k++) pairs.push([i + k, i + (k + 1) % 3]);
			else { for (let i = 0; i + 1 < n; i++) pairs.push([i, i + 1]); if (m !== C.LINE_STRIP) pairs.push([n - 1, 0]); }
			const quads = lineQuads(pairs);
			const textured = state.enabled.has(C.TEXTURE_2D);
			state.enabled.delete(C.TEXTURE_2D);
			if (quads.length) drawTriangleData(quads, true);
			if (textured) state.enabled.add(C.TEXTURE_2D);
			return;
		}
		if (m === C.TRIANGLES) { drawTriangleData(data.subarray(0, n * FLOATS), false); return; }
		const list = [];
		if (m === C.QUADS) for (let i = 0; i + 3 < n; i += 4) for (const k of [0, 1, 2, 0, 2, 3]) repeat(list, i + k);
		else if (m === C.POLYGON || m === C.TRIANGLE_FAN) for (let i = 1; i + 1 < n; i++) for (const k of [0, i, i + 1]) repeat(list, k);
		else if (m === C.TRIANGLE_STRIP) for (let i = 0; i + 2 < n; i++) for (const k of i % 2 ? [i + 1, i, i + 2] : [i, i + 1, i + 2]) repeat(list, k);
		else if (m === C.QUAD_STRIP) for (let i = 0; i + 3 < n; i += 2) for (const k of [0, 1, 3, 0, 3, 2]) repeat(list, i + k);
		else return;   // points: nothing uses them
		drawTriangleData(new Float32Array(list), false);
	}

	/* ------------------------------ textures ------------------------------ */

	class Texture {
		constructor(width, height) {
			this.width = width; this.height = height;
			this.pixels = new Uint8Array(width * height * 4);
			this.id = nextTexture++;
			textures.set(this.id, { handle: g.createTexture(), border: [true, true], mipmap: false, uploaded: false });
		}
		// Uploads the pixels; like the native one it leaves the texture bound, unfiltered and clamped to a transparent border
		finalize() {
			const t = textures.get(this.id);
			state.bound = this.id;
			g.bindTexture(g.TEXTURE_2D, t.handle);
			g.pixelStorei(g.UNPACK_ALIGNMENT, 1);
			g.texImage2D(g.TEXTURE_2D, 0, g.RGBA, this.width, this.height, 0, g.RGBA, g.UNSIGNED_BYTE, this.pixels);
			if (t.mipmap) g.generateMipmap(g.TEXTURE_2D);
			g.texParameteri(g.TEXTURE_2D, g.TEXTURE_MIN_FILTER, g.NEAREST); g.texParameteri(g.TEXTURE_2D, g.TEXTURE_MAG_FILTER, g.NEAREST);
			g.texParameteri(g.TEXTURE_2D, g.TEXTURE_WRAP_S, g.CLAMP_TO_EDGE); g.texParameteri(g.TEXTURE_2D, g.TEXTURE_WRAP_T, g.CLAMP_TO_EDGE);
			t.border = [true, true]; t.uploaded = true;
		}
		draw(texL, texR, texT, texB, scrL, scrR, scrT, scrB, z = -2) {
			api.BindTexture(C.TEXTURE_2D, this.id);
			api.Begin(C.QUADS);
			api.TexCoord2f(texL, texB); api.Vertex3f(scrL, scrB, z);
			api.TexCoord2f(texR, texB); api.Vertex3f(scrR, scrB, z);
			api.TexCoord2f(texR, texT); api.Vertex3f(scrR, scrT, z);
			api.TexCoord2f(texL, texT); api.Vertex3f(scrL, scrT, z);
			api.End();
		}
	}

	// The Atari's picture as a texture, kept by the host (see setScreen)
	const screenTexture = new Texture(screen.width, screen.height);

	/* ------------------------------ the API ------------------------------ */

	const api = {
		...C,
		Enable(cap) { setEnabled(cap, true); },
		Disable(cap) { setEnabled(cap, false); },
		Begin(m) { mode = m; count = 0; },
		End() { end(); },
		Vertex3f(x, y, z) { push(x, y, z, state.colour, state.texCoord[0], state.texCoord[1]); },
		Vertex4f(x, y, z, w) { push(x, y, z, state.colour, state.texCoord[0], state.texCoord[1], w); },
		TexCoord2f(s, t) { state.texCoord = [s, t]; },
		Color4f(r, gg, b, a) { state.colour = [r, gg, b, a]; },
		Normal3f() { },
		MatrixMode(m) { state.matrixMode = m; },
		PushMatrix() { const s = stack(); s.push(s[s.length - 1].slice()); },
		PopMatrix() { const s = stack(); if (s.length > 1) s.pop(); },
		LoadIdentity() { const s = stack(); s[s.length - 1] = identity(); },
		Translatef(x, y, z) { apply([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]); },
		Scalef(x, y, z) { apply([x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1]); },
		Rotatef(angle, x, y, z) {
			const l = Math.hypot(x, y, z) || 1, c = Math.cos(angle * Math.PI / 180), s = Math.sin(angle * Math.PI / 180), t = 1 - c;
			x /= l; y /= l; z /= l;
			apply([x * x * t + c, y * x * t + z * s, x * z * t - y * s, 0, x * y * t - z * s, y * y * t + c, y * z * t + x * s, 0, x * z * t + y * s, y * z * t - x * s, z * z * t + c, 0, 0, 0, 0, 1]);
		},
		Ortho(l, r, b, t, n, f) { apply([2 / (r - l), 0, 0, 0, 0, 2 / (t - b), 0, 0, 0, 0, -2 / (f - n), 0, -(r + l) / (r - l), -(t + b) / (t - b), -(f + n) / (f - n), 1]); },
		Frustum(l, r, b, t, n, f) { apply([2 * n / (r - l), 0, 0, 0, 0, 2 * n / (t - b), 0, 0, (r + l) / (r - l), (t + b) / (t - b), -(f + n) / (f - n), -1, 0, 0, -2 * f * n / (f - n), 0]); },
		// The attribute stack: the whole state is copied, and the groups named in the mask come back
		PushAttrib(mask) { attribStack.push({ mask, state: { ...state, enabled: new Set(state.enabled), colour: state.colour.slice(), texCoord: state.texCoord.slice(), blend: state.blend.slice(), fog: { ...state.fog, colour: state.fog.colour.slice() }, viewport: state.viewport.slice(), scissor: state.scissor.slice(), clearColour: state.clearColour.slice() } }); },
		PopAttrib() {
			const saved = attribStack.pop();
			if (saved === undefined) return;
			const { mask, state: old } = saved, has = (bit) => (mask & bit) !== 0;
			const caps = (list) => { for (const cap of list) { if (old.enabled.has(cap)) state.enabled.add(cap); else state.enabled.delete(cap); } };
			if (has(C.ENABLE_BIT)) state.enabled = old.enabled;
			if (has(C.CURRENT_BIT)) { state.colour = old.colour; state.texCoord = old.texCoord; }
			if (has(C.COLOR_BUFFER_BIT)) { caps([C.BLEND]); state.blend = old.blend; state.clearColour = old.clearColour; }
			if (has(C.DEPTH_BUFFER_BIT)) caps([C.DEPTH_TEST]);
			if (has(C.LINE_BIT)) { state.lineWidth = old.lineWidth; caps([C.LINE_SMOOTH]); }
			if (has(C.POLYGON_BIT)) { state.polygonMode = old.polygonMode; caps([C.CULL_FACE]); }
			if (has(C.SCISSOR_BIT)) { state.scissor = old.scissor; caps([C.SCISSOR_TEST]); }
			if (has(C.VIEWPORT_BIT)) state.viewport = old.viewport;
			if (has(C.TEXTURE_BIT)) { state.bound = old.bound; caps([C.TEXTURE_2D]); }
			if (has(C.TRANSFORM_BIT)) state.matrixMode = old.matrixMode;
			if (has(C.LIGHTING_BIT)) caps([C.LIGHTING, C.COLOR_MATERIAL]);
			if (mask === C.ALL_ATTRIB_BITS) { state.fog = old.fog; caps([C.FOG]); }   // the fog group has no constant of its own in the binding
			syncState();
		},
		Clear(mask) { g.clear((mask & C.COLOR_BUFFER_BIT ? g.COLOR_BUFFER_BIT : 0) | (mask & C.DEPTH_BUFFER_BIT ? g.DEPTH_BUFFER_BIT : 0)); },
		ClearColor(r, gg, b, a) { state.clearColour = [r, gg, b, a]; g.clearColor(r, gg, b, a); },
		BlendFunc(s, d) { state.blend = [s, d]; g.blendFunc(s, d); },
		BindTexture(target, id) { state.bound = id; const t = textures.get(id); g.bindTexture(g.TEXTURE_2D, t ? t.handle : null); },
		TexParameteri(target, name, value) {
			const t = textures.get(state.bound);
			if (t === undefined) return;
			if (name === C.GENERATE_MIPMAP) { t.mipmap = value !== C.FALSE; return; }
			if (name === C.TEXTURE_WRAP_S || name === C.TEXTURE_WRAP_T) {
				t.border[name === C.TEXTURE_WRAP_S ? 0 : 1] = value === C.CLAMP_TO_BORDER;
				value = value === C.REPEAT ? g.REPEAT : g.CLAMP_TO_EDGE;
			}
			g.texParameteri(g.TEXTURE_2D, name, value);
		},
		PolygonMode(face, m) { state.polygonMode = m; },
		CullFace(face) { g.cullFace(face); },
		FrontFace(dir) { g.frontFace(dir); },
		LineWidth(w) { state.lineWidth = w; },
		Viewport(x, y, w, h) { state.viewport = [x, y, w, h]; g.viewport(x, y, w, h); },
		Scissor(x, y, w, h) { state.scissor = [x, y, w, h]; g.scissor(x, y, w, h); },
		Fogf(name, value) {
			if (name === C.FOG_MODE) state.fog.mode = value;
			else if (name === C.FOG_DENSITY) state.fog.density = value;
			else if (name === C.FOG_START) state.fog.start = value;
			else if (name === C.FOG_END) state.fog.end = value;
		},
		Fogfv(name, values) { if (name === C.FOG_COLOR) state.fog.colour = Array.from(values); },
		Lightfv() { },
		GetIntegerv(name) {
			if (name === C.VIEWPORT) return state.viewport.slice();
			if (name === C.SCISSOR_BOX) return state.scissor.slice();
			if (name === C.SAMPLES) return [g.getParameter(g.SAMPLES)];
			return [0];
		},
		createTexture(width, height) { return new Texture(width, height); },
		// loadTextureRGBA(path, width, height) is added by the host, which has the files
		drawTriangles(positions) {
			const out = new Float32Array(positions.length / 3 * FLOATS), c = state.colour;
			for (let i = 0, o = 0; i < positions.length; i += 3, o += FLOATS) {
				out[o] = positions[i]; out[o + 1] = positions[i + 1]; out[o + 2] = positions[i + 2]; out[o + 3] = 1;
				out[o + 4] = c[0]; out[o + 5] = c[1]; out[o + 6] = c[2]; out[o + 7] = c[3];
			}
			drawTriangleData(out, false);
		},
		readPixels(x, y, w, h) { const out = new Uint8Array(w * h * 4); g.readPixels(x, y, w, h, g.RGBA, g.UNSIGNED_BYTE, out); return out; },
		screenSize() { return [screen.width, screen.height]; },
		// A region of the Atari's picture (its pixels, y down) on a rectangle, smoothed
		drawScreen(x0, y0, x1, y1, l, r, t, b, z = -2) {
			const s0 = x0 / screen.width, s1 = x1 / screen.width, t0 = y0 / screen.height, t1 = y1 / screen.height;
			api.BindTexture(C.TEXTURE_2D, screenTexture.id);
			g.texParameteri(g.TEXTURE_2D, g.TEXTURE_MIN_FILTER, g.LINEAR); g.texParameteri(g.TEXTURE_2D, g.TEXTURE_MAG_FILTER, g.LINEAR);
			api.Begin(C.QUADS);
			api.TexCoord2f(s0, t0); api.Vertex3f(l, t, z);
			api.TexCoord2f(s1, t0); api.Vertex3f(r, t, z);
			api.TexCoord2f(s1, t1); api.Vertex3f(r, b, z);
			api.TexCoord2f(s0, t1); api.Vertex3f(l, b, z);
			api.End();
			g.texParameteri(g.TEXTURE_2D, g.TEXTURE_MIN_FILTER, g.NEAREST); g.texParameteri(g.TEXTURE_2D, g.TEXTURE_MAG_FILTER, g.NEAREST);
		},
	};

	// For the host: the state the native display leaves before the hooks run
	// (its own projection, texturing on with the screen bound, blending on),
	// and the picture itself
	const host = {
		beginFrame() {
			state = defaults();
			attribStack.length = 0;
			matrices[C.MODELVIEW] = [identity()]; matrices[C.PROJECTION] = [identity()]; matrices[C.TEXTURE] = [identity()];
			state.matrixMode = C.PROJECTION; api.Ortho(-1, 1, -1, 1, 0, 10); state.matrixMode = C.MODELVIEW;
			g.useProgram(program); g.bindVertexArray(vao); g.bindBuffer(g.ARRAY_BUFFER, buffer);
			g.activeTexture(g.TEXTURE0); g.depthFunc(g.LESS); g.depthMask(true); g.colorMask(true, true, true, true);
			state.bound = screenTexture.id;
			syncState();
			g.clear(g.COLOR_BUFFER_BIT | g.DEPTH_BUFFER_BIT);
		},
		// rgba: the picture's pixels; then drawn over the whole canvas
		drawScreen(rgba) {
			screenTexture.pixels.set(rgba);
			screenTexture.finalize();
			g.disable(g.BLEND);
			screenTexture.draw(0, 1, 0, 1, -1, 1, 1, -1);
			if (state.enabled.has(C.BLEND)) g.enable(g.BLEND);
		},
		Texture,
		trace: null,   // for tests: an array here collects a line per draw call
	};
	return { gl: api, host };
}
