// The part of QuickJS's "os" module the extensions use: directories need no
// making here, and removing a file forgets it.
import { forget } from "./std.js";

export function mkdir() { return 0; }
export function remove(path) { return forget(path) ? 0 : -2; }
