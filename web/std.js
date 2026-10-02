// The part of QuickJS's "std" module the extensions use, in the browser:
// files are byte arrays the host fetched beforehand (a script reads them
// without waiting, as it does natively), and files a script writes are kept
// in the page's local storage.
export const SEEK_SET = 0, SEEK_CUR = 1, SEEK_END = 2;
export const files = new Map();   // path -> Uint8Array
const STORE = "atari800:file:";

class File {
	constructor(path, bytes, writing) { this.path = path; this.bytes = bytes; this.writing = writing; this.at = 0; }
	seek(offset, whence = SEEK_SET) { this.at = whence === SEEK_END ? this.bytes.length + offset : whence === SEEK_CUR ? this.at + offset : offset; return 0; }
	tell() { return this.at; }
	read(buffer, position, length) {
		const n = Math.max(0, Math.min(length, this.bytes.length - this.at));
		new Uint8Array(buffer, position, n).set(this.bytes.subarray(this.at, this.at + n));
		this.at += n;
		return n;
	}
	write(buffer, position, length) {
		const grown = new Uint8Array(Math.max(this.bytes.length, this.at + length));
		grown.set(this.bytes); grown.set(new Uint8Array(buffer, position, length), this.at);
		this.bytes = grown; this.at += length;
		return length;
	}
	close() {
		if (!this.writing) return;
		files.set(this.path, this.bytes);
		try { localStorage.setItem(STORE + this.path, btoa(String.fromCharCode(...this.bytes))); } catch (e) { /* no storage: the file lives as long as the page */ }
	}
}

function stored(path) {
	if (files.has(path)) return files.get(path);
	try {
		const text = globalThis.localStorage && localStorage.getItem(STORE + path);
		if (text) { const bytes = Uint8Array.from(atob(text), (c) => c.charCodeAt(0)); files.set(path, bytes); return bytes; }
	} catch (e) { /* no storage */ }
	return null;
}

export function open(path, mode = "r") {
	if (mode.includes("w")) return new File(path, new Uint8Array(0), true);
	const bytes = stored(path);
	return bytes === null ? null : new File(path, bytes, false);
}
export function loadFile(path) { const bytes = stored(path); return bytes === null ? null : new TextDecoder().decode(bytes); }
export function getenv() { return undefined; }
