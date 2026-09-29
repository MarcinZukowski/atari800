// A minimal Wavefront .obj/.mtl loader for the extensions' 3D models.
//
// Supports what the Yoomp! models use: v, vn, vt, f (three or more vertices,
// fan-triangulated, negative indices allowed), usemtl, mtllib, and the Kd
// diffuse colour from the .mtl file. Faces are expanded into flat
// Float32Arrays per material, ready for gl.drawTriangles().
import * as std from "std";

function readFile(path) {
	const text = std.loadFile(path);
	if (text === null)
		throw new Error(`cannot read ${path}`);
	return text;
}

function dirname(path) {
	const i = path.lastIndexOf("/");
	return i < 0 ? "." : path.slice(0, i);
}

// material name -> [r, g, b] diffuse colour
function loadMtl(path) {
	const materials = new Map();
	let current = null;
	for (const line of readFile(path).split("\n")) {
		const t = line.trim().split(/\s+/);
		if (t[0] === "newmtl") {
			current = t[1];
			materials.set(current, [1, 1, 1]);
		}
		else if (t[0] === "Kd" && current !== null) {
			materials.set(current, [+t[1], +t[2], +t[3]]);
		}
	}
	return materials;
}

// Loads a model. The result has a groups array, one entry per material with
// { color, positions, normals }, and render(r, g, b), which draws every
// group with its material colour multiplied by r, g, b.
export function loadObj(path) {
	const positions = [];
	const normals = [];
	let materials = new Map();
	const groups = new Map();   // material name -> { name, positions, normals }
	let group = null;

	const useMaterial = (name) => {
		if (!groups.has(name))
			groups.set(name, { name, positions: [], normals: [] });
		group = groups.get(name);
	};
	useMaterial("");   // faces before any usemtl

	// .obj indices are 1-based; negative ones count back from the end
	const resolve = (idx, count) => (idx < 0 ? count + idx : idx - 1);

	for (const line of readFile(path).split("\n")) {
		const t = line.trim().split(/\s+/);
		switch (t[0]) {
		case "v":
			positions.push(+t[1], +t[2], +t[3]);
			break;
		case "vn":
			normals.push(+t[1], +t[2], +t[3]);
			break;
		case "mtllib":
			materials = loadMtl(dirname(path) + "/" + t[1]);
			break;
		case "usemtl":
			useMaterial(t[1]);
			break;
		case "f": {
			// each vertex is v, v/vt, v//vn or v/vt/vn; split polygons into a fan
			const verts = t.slice(1).map((s) => s.split("/"));
			for (let i = 2; i < verts.length; i++) {
				for (const v of [verts[0], verts[i - 1], verts[i]]) {
					const pi = 3 * resolve(+v[0], positions.length / 3);
					group.positions.push(positions[pi], positions[pi + 1], positions[pi + 2]);
					if (v[2]) {
						const ni = 3 * resolve(+v[2], normals.length / 3);
						group.normals.push(normals[ni], normals[ni + 1], normals[ni + 2]);
					}
				}
			}
			break;
		}
		}
	}

	const result = [];
	for (const g of groups.values()) {
		if (g.positions.length === 0)
			continue;
		result.push({
			color: materials.get(g.name) || [1, 1, 1],
			positions: Float32Array.from(g.positions),
			normals: g.normals.length === g.positions.length ? Float32Array.from(g.normals) : null,
		});
	}
	const triangles = result.reduce((n, g) => n + g.positions.length / 9, 0);
	console.log(`obj: ${path}: ${triangles} triangles, ${result.length} material(s)`);

	return {
		groups: result,
		render(r = 1, g = 1, b = 1) {
			for (const grp of result) {
				gl.Color4f(grp.color[0] * r, grp.color[1] * g, grp.color[2] * b, 1);
				gl.drawTriangles(grp.positions, grp.normals);
			}
		},
	};
}
