// The light an entity gets at a place, as the engine works it out (R_SetupEntityLightingGrid and
// R_SetupEntityLighting in tr_light.cpp): the eight samples of the light grid around the place, weighed by how
// near they are, without the ones inside walls. The ambient light times r_ambientScale (0.6) with a little added
// for everything, the directed light, and the direction it comes from. All three in out, colours from 0 to 1.
const lightNow = { ambient: [0, 0, 0], directed: [0, 0, 0], dir: [0, 0, 1] };

function gridLight(pos, out = lightNow) {
	const g = world && world.grid, a = [0, 0, 0], d = [0, 0, 0], n = [0, 0, 0];
	if (!g || g.data.length < 30 || !g.array.length) {
		// a map without a grid: the same light everywhere, from where the sun of the engine is
		out.ambient.fill((150 + 32) / 255);
		out.directed.fill(150 / 255);
		out.dir[0] = 0.433; out.dir[1] = 0.289; out.dir[2] = 0.854;
		return out;
	}
	const at = [0, 0, 0], frac = [0, 0, 0];
	for (let i = 0; i < 3; i++) {
		const v = (pos[i] - g.origin[i]) / g.size[i];
		at[i] = Math.floor(v);
		frac[i] = v - at[i];
		if (at[i] < 0) at[i] = 0; else if (at[i] >= g.bounds[i] - 1) at[i] = g.bounds[i] - 1;
	}
	const step = [1, g.bounds[0], g.bounds[0] * g.bounds[1]], start = at[0] + at[1] * step[1] + at[2] * step[2];
	let total = 0;
	for (let i = 0; i < 8; i++) {
		let factor = 1, cellAt = start;
		for (let j = 0; j < 3; j++) {
			if (i & (1 << j)) { factor *= frac[j]; cellAt += step[j]; } else factor *= 1 - frac[j];
		}
		if (cellAt < 0 || cellAt >= g.array.length) continue;
		// a sample: 4 x 3 bytes of ambient light, 4 x 3 of directed, 4 styles, 2 for the direction
		const o = g.array[cellAt] * 30;
		if (o + 30 > g.data.length || g.data[o + 24] === 255) continue;	// none there, or one inside a wall
		total += factor;
		for (let j = 0; j < 4 && g.data[o + 24 + j] !== 255; j++) {
			for (let c = 0; c < 3; c++) { a[c] += factor * g.data[o + j * 3 + c]; d[c] += factor * g.data[o + 12 + j * 3 + c]; }
		}
		const lat = g.data[o + 29] / 256 * Math.PI * 2, lng = g.data[o + 28] / 256 * Math.PI * 2;
		n[0] += factor * Math.cos(lat) * Math.sin(lng);
		n[1] += factor * Math.sin(lat) * Math.sin(lng);
		n[2] += factor * Math.cos(lng);
	}
	const scale = total > 0 && total < 0.99 ? 1 / total : 1, len = Math.hypot(n[0], n[1], n[2]);
	for (let c = 0; c < 3; c++) {
		out.ambient[c] = Math.min(255, a[c] * scale * 0.6 + 32) / 255;
		out.directed[c] = d[c] * scale / 255;
		out.dir[c] = len ? n[c] / len : 0;
	}
	return out;
}

// The surfaces of a model with the stages of their shaders, in the view of the administration. place: where the
// model stands. What is solid is drawn at once. A surface that is see-through (a sort after the banners) waits
// in lateModels until everything solid is there, and is then drawn in the order of its sort with the see-through
// surfaces of the map, as the game sorts all of them together. Without that, what is drawn later behind such a
// surface would wipe it out.
const NO_DEFORM = { deforms: [] };
const lateModels = [];
let deferLate = false;		// true from the start of a frame of the administration until its late part is drawn

function modelPlace(place, light) {
	const u = modelStageProg.u, at = M4.transform(M4.invert(place), eye);
	gl.uniformMatrix4fv(u.uModel, false, place);
	gl.uniform3f(u.uEyeLocal, at[0], at[1], at[2]);
	gl.uniform3fv(u.uAmbient, light.ambient);
	gl.uniform3fv(u.uDirected, light.directed);
	gl.uniform3fv(u.uLightDir, light.dir);
}

function modelPassBegin() {
	gl.useProgram(modelStageProg.p);
	gl.uniformMatrix4fv(modelStageProg.u.uMvp, false, viewProj);
	gl.uniform1i(modelStageProg.u.uTex, 0);
	gl.activeTexture(gl.TEXTURE0);
	gl.colorMask(true, true, true, false);
}

function modelPassEnd() {
	gl.disable(gl.CULL_FACE);
	gl.disable(gl.BLEND);
	gl.depthMask(true);
	gl.depthFunc(gl.LESS);
	gl.colorMask(true, true, true, true);
}

function drawModelMesh(mesh, mat, t, globalFog) {
	setCull(mat.cull);
	setDeform(modelStageProg, mat, t);
	if (mat.polygonOffset) { gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(-1, -2); }
	const fog = globalFog && mat.sort <= SC_SORT_SEE_THROUGH && !mat.noFog ? globalFog : null;
	for (const st of mat.stages) drawStage(st, t, null, mesh.vao, mesh.count, gl.UNSIGNED_INT, 0, modelStageProg, fog);
	if (mat.polygonOffset) gl.disable(gl.POLYGON_OFFSET_FILL);
}

function drawModelScripted(meshes, place) {
	const t = performance.now() / 1000;
	modelPassBegin();
	const light = gridLight([place[12], place[13], place[14]]);
	modelPlace(place, light);
	// a model is in the fog of the whole map like everything else, the fog of a brush is not drawn on models
	const globalFog = fogShown() && world ? world.globalFog : null;
	let kept = null;
	for (const mesh of meshes) {
		const mat = mesh.mat || (mesh.mat = materialFor(mesh.shaderName || '', 'model'));
		if (mat.nodraw) continue;
		if (deferLate && mat.sort > SC_SORT_BANNER) {
			// the place and the light are the ones of now: the next model changes both
			if (!kept) kept = { place: place.slice(), light: { ambient: light.ambient.slice(), directed: light.directed.slice(), dir: light.dir.slice() } };
			lateModels.push({ mesh, mat, at: kept, order: lateModels.length });
			continue;
		}
		drawModelMesh(mesh, mat, t, globalFog);
	}
	modelPassEnd();
}

// Draws what waits in lateModels with a sort up to upTo, lowest sort first. True when something was drawn: the
// program in use and the state of the blending are then those of a model.
function drawLateModels(upTo) {
	if (!lateModels.length) return false;
	lateModels.sort((a, b) => a.mat.sort - b.mat.sort || a.order - b.order);
	let n = 0;
	while (n < lateModels.length && lateModels[n].mat.sort <= upTo) n++;
	if (!n) return false;
	const t = performance.now() / 1000, globalFog = fogShown() && world ? world.globalFog : null;
	modelPassBegin();
	let at = null;
	for (const m of lateModels.splice(0, n)) {
		if (m.at !== at) { at = m.at; modelPlace(at.place, at.light); }
		drawModelMesh(m.mesh, m.mat, t, globalFog);
	}
	modelPassEnd();
	return true;
}
// the administration draws models with their scripts once those are read
const modelsScripted = () => gameLook() && scripts.state === 2;
