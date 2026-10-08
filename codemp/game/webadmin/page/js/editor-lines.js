// ---- boxes and links

// Not in the game as it was: the box a trigger has by its keys mins and maxs when it names no brushes of the map
// (InitTrigger in g_trigger.c), from its origin. null: it has none.
function triggerBox(ent) {
	if (ent.bmodel || !/^trigger_/i.test(ent.classname)) return null;
	const vec = key => { const v = entKey(ent, key).trim().split(/\s+/).map(Number); return v.length === 3 && v.every(isFinite) ? v : null; };
	const mins = vec('mins'), maxs = vec('maxs');
	return mins && maxs && maxs.every((v, i) => v > mins[i]) ? { mins, maxs } : null;
}

// The box of an entity. Of one that is made of brushes: the size of those, where the server says it is now.
// Of a trigger that is a box: that one. Of another one: the box its class is defined with. null: none is known.
function entBox(ent) {
	if (ent.bmodel) {
		const n = /^\*(\d+)$/.exec(entKey(ent, 'model')), m = n && world && world.models[+n[1]];
		return m ? { center: ent.pos, size: m.maxs.map((v, i) => Math.max(1, v - m.mins[i])) } : null;
	}
	const d = (boxGhost && boxGhost.id === ent.id ? boxGhost : triggerBox(ent)) || classDoc(ent.classname);
	if (!d || !d.mins) return null;
	return { center: ent.pos.map((v, i) => v + (d.mins[i] + d.maxs[i]) / 2), size: d.maxs.map((v, i) => Math.max(1, v - d.mins[i])) };
}

// the edges of a box one unit wide around its middle
const wireBox = (() => {
	const pos = [];
	for (let i = 0; i < 8; i++) pos.push(i & 1 ? 0.5 : -0.5, i & 2 ? 0.5 : -0.5, i & 4 ? 0.5 : -0.5);
	const vao = gl.createVertexArray(), pb = gl.createBuffer(), ib = gl.createBuffer();
	gl.bindVertexArray(vao);
	gl.bindBuffer(gl.ARRAY_BUFFER, pb);
	gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(pos), gl.STATIC_DRAW);
	gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
	gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
	gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 1, 3, 4, 6, 5, 7, 0, 4, 1, 5, 2, 6, 3, 7]), gl.STATIC_DRAW);
	gl.bindVertexArray(null);
	return vao;
})();

// lines from point to point, filled anew for what is drawn
const linkLines = (() => {
	const vao = gl.createVertexArray(), buf = gl.createBuffer();
	gl.bindVertexArray(vao);
	gl.bindBuffer(gl.ARRAY_BUFFER, buf);
	gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
	gl.bindVertexArray(null);
	return { vao, buf };
})();

// the keys with which an entity names the ones it fires
const LINK_KEYS = new Set(['target', 'target2', 'target3', 'target4', 'target5', 'target6', 'opentarget', 'closetarget', 'paintarget', 'deathtarget']);
let links = [];		// { from, to, key }: from fires to
// What fired what just now (used of /api/entities): the line from the one to the other and the box of the one
// that was fired light up for a few seconds. from is the number of the world when it was fired through the panel.
const FIRED_MS = 3000;
let fired = [], firedSeq = -1;		// { from, to, at }, and the number of the last event that was taken

function takeFired(used) {
	if (!used || !used.length) return;
	// the numbers start anew with a map
	if (used[used.length - 1][0] < firedSeq) firedSeq = -1;
	const now = performance.now();
	for (const [seq, ago, from, to] of used) {
		if (seq <= firedSeq) continue;
		firedSeq = seq;
		if (ago < FIRED_MS) fired.push({ from, to, at: now - ago });
	}
}

function refreshLinks() {
	const byName = new Map();
	for (const e of ents.values()) {
		const name = entKey(e, 'targetname').toLowerCase();
		if (!name) continue;
		if (!byName.has(name)) byName.set(name, []);
		byName.get(name).push(e.id);
	}
	links = [];
	for (const e of ents.values()) {
		for (const [k, v] of e.keys || []) {
			if (!v || !LINK_KEYS.has(k.toLowerCase())) continue;
			for (const to of byName.get(v.toLowerCase()) || []) if (to !== e.id) links.push({ from: e.id, to, key: k });
		}
	}
}

// Which way an entity looks: an arrow from where it is, for every one whose keys say so (angle or angles). For a
// start point that is where the player looks, for a shooter where it fires, for a door the way it opens. Drawn
// with the markers. The one that is selected has a longer one in its colour.
function arrowInto(data, n, pos, angles, length) {
	const r = Math.PI / 180, cp = Math.cos(angles[0] * r), f = [cp * Math.cos(angles[1] * r), cp * Math.sin(angles[1] * r), -Math.sin(angles[0] * r)];
	// across it, for the head: level unless the arrow stands upright
	const flat = Math.hypot(f[0], f[1]), side = flat > 0.01 ? [-f[1] / flat, f[0] / flat, 0] : [1, 0, 0];
	const tip = [pos[0] + f[0] * length, pos[1] + f[1] * length, pos[2] + f[2] * length], head = length * 0.28;
	const put = (a, b) => { data.set(a, n); data.set(b, n + 3); n += 6; };
	put(pos, tip);
	for (const w of [0.45, -0.45]) put(tip, [tip[0] - f[0] * head + side[0] * head * w, tip[1] - f[1] * head + side[1] * head * w, tip[2] - f[2] * head + side[2] * head * w]);
	return n;
}

function drawFacing() {
	const has = e => e.keys && e.keys.some(k => /^angles?$/i.test(k[0]));
	const all = [...ents.values()].filter(e => entVisible(e) && has(e) && e.id !== selectedEnt), sel = ents.get(selectedEnt);
	const data = new Float32Array((all.length + 1) * 18);
	let n = 0;
	for (const e of all) n = arrowInto(data, n, e.pos, entAngles(e), 28);
	gl.useProgram(lineProg.p);
	gl.uniformMatrix4fv(lineProg.u.uMvp, false, viewProj);
	gl.bindVertexArray(linkLines.vao);
	gl.bindBuffer(gl.ARRAY_BUFFER, linkLines.buf);
	if (n) {
		gl.bufferData(gl.ARRAY_BUFFER, data.subarray(0, n), gl.DYNAMIC_DRAW);
		gl.uniform4f(lineProg.u.uColor, 0.92, 0.92, 0.95, 1);
		gl.drawArrays(gl.LINES, 0, n / 3);
	}
	if (sel && has(sel)) {
		const pos = ghost && ghost.id === sel.id ? ghost.pos : sel.pos, m = arrowInto(data, 0, pos, entAngles(sel), 48);
		gl.bufferData(gl.ARRAY_BUFFER, data.subarray(0, m), gl.DYNAMIC_DRAW);
		gl.uniform4f(lineProg.u.uColor, 1, 0.25, 0.35, 1);
		gl.disable(gl.DEPTH_TEST);
		gl.drawArrays(gl.LINES, 0, m / 3);
		gl.enable(gl.DEPTH_TEST);
	}
	gl.useProgram(flatProg.p);
}

function drawLinkSet(list, color) {
	const data = new Float32Array(list.length * 6);
	let n = 0;
	for (const l of list) {
		const a = ents.get(l.from), b = ents.get(l.to);
		if (!a || !b) continue;
		data.set(a.pos, n);
		data.set(b.pos, n + 3);
		n += 6;
	}
	if (!n) return;
	gl.bindVertexArray(linkLines.vao);
	gl.bindBuffer(gl.ARRAY_BUFFER, linkLines.buf);
	gl.bufferData(gl.ARRAY_BUFFER, data.subarray(0, n), gl.DYNAMIC_DRAW);
	gl.uniform4fv(lineProg.u.uColor, color);
	gl.drawArrays(gl.LINES, 0, n / 3);
}

// The edges of a brush, as pairs of points into out. A brush is the space behind all of its planes: every side
// is a sheet on its plane, cut down by the planes of the other sides.
function brushEdges(bd, index, out) {
	const first = bd.brushes[index * 3], count = bd.brushes[index * 3 + 1], planes = [];
	for (let i = 0; i < count; i++) {
		const n = bd.sides[(first + i) * 3] * 4;
		if (n >= 0 && n + 3 < bd.planes.length) planes.push([bd.planes[n], bd.planes[n + 1], bd.planes[n + 2], bd.planes[n + 3]]);
	}
	for (let i = 0; i < planes.length; i++) {
		const [nx, ny, nz, d] = planes[i];
		// two directions in the plane, and a sheet far larger than a map
		let ux = Math.abs(nz) > 0.9 ? 1 : 0, uy = 0, uz = Math.abs(nz) > 0.9 ? 0 : 1;
		const along = ux * nx + uy * ny + uz * nz;
		ux -= nx * along; uy -= ny * along; uz -= nz * along;
		const len = Math.hypot(ux, uy, uz);
		ux /= len; uy /= len; uz /= len;
		const vx = ny * uz - nz * uy, vy = nz * ux - nx * uz, vz = nx * uy - ny * ux, far = 131072;
		let poly = [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([a, b]) => [nx * d + (ux * a + vx * b) * far, ny * d + (uy * a + vy * b) * far, nz * d + (uz * a + vz * b) * far]);
		for (let j = 0; j < planes.length && poly.length >= 3; j++) {
			if (j === i) continue;
			const [ax, ay, az, ad] = planes[j], next = [];
			for (let q = 0; q < poly.length; q++) {
				const a = poly[q], b = poly[(q + 1) % poly.length];
				const da = a[0] * ax + a[1] * ay + a[2] * az - ad, db = b[0] * ax + b[1] * ay + b[2] * az - ad;
				if (da <= 0.01) next.push(a);
				if ((da > 0.01) !== (db > 0.01)) {
					const t = da / (da - db);
					next.push([a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]);
				}
			}
			poly = next;
		}
		// a plane that only helps the collision (a bevel) leaves no face
		if (poly.length < 3) continue;
		for (let q = 0; q < poly.length; q++) {
			const a = poly[q], b = poly[(q + 1) % poly.length];
			if (Math.abs(a[0] - b[0]) + Math.abs(a[1] - b[1]) + Math.abs(a[2] - b[2]) > 0.05) out.push(a[0], a[1], a[2], b[0], b[1], b[2]);
		}
	}
}

// the brushes that stop something without being seen: for players, NPCs, bots and shots
const CONTENTS_CLIP = 0x10 | 0x20 | 0x40 | 0x80;

// The edges of some brushes of the map as lines, made once: of a brush model (its number), or of the clip
// brushes of the world ('clip'). null: there are none.
function brushWire(key) {
	if (world.wires.has(key)) return world.wires.get(key);
	const bd = world.brushes, model = world.models[key === 'clip' ? 0 : key], out = [];
	if (model) {
		for (let i = model.firstBrush; i < model.firstBrush + model.numBrushes && i * 3 + 2 < bd.brushes.length; i++) {
			if (key === 'clip' && !(((world.shaders[bd.brushes[i * 3 + 2]] || {}).contents | 0) & CONTENTS_CLIP)) continue;
			brushEdges(bd, i, out);
		}
	}
	let wire = null;
	if (out.length) {
		const vao = gl.createVertexArray(), buf = gl.createBuffer();
		gl.bindVertexArray(vao);
		gl.bindBuffer(gl.ARRAY_BUFFER, buf);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(out), gl.STATIC_DRAW);
		gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
		gl.bindVertexArray(null);
		wire = { vao, count: out.length / 3 };
	}
	world.wires.set(key, wire);
	return wire;
}

// An entity as lines. One that is made of brushes with their real shape, moved to where the server says it is
// now. Another one, or one whose brushes the map file does not give, as its box.
function drawEntBox(ent, color) {
	gl.uniform4f(lineProg.u.uColor, color[0], color[1], color[2], 1);
	const n = ent.bmodel ? /^\*(\d+)$/.exec(entKey(ent, 'model')) : null, wire = n && world ? brushWire(+n[1]) : null;
	if (wire) {
		// The engine draws the brushes of an entity moved and turned from where the map file has them, and the
		// server says by how much. A game module that does not: moved by where the middle of its box is.
		const m = world.models[+n[1]];
		const place = ent.moved ? anglesMatrix(ent.moved, ent.turned, 1) : new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
			ent.pos[0] - (m.mins[0] + m.maxs[0]) / 2, ent.pos[1] - (m.mins[1] + m.maxs[1]) / 2, ent.pos[2] - (m.mins[2] + m.maxs[2]) / 2, 1]);
		gl.uniformMatrix4fv(lineProg.u.uMvp, false, M4.mul(viewProj, place));
		gl.bindVertexArray(wire.vao);
		gl.drawArrays(gl.LINES, 0, wire.count);
		return;
	}
	const box = entBox(ent);
	if (!box) return;
	gl.uniformMatrix4fv(lineProg.u.uMvp, false, M4.mul(viewProj, new Float32Array([box.size[0], 0, 0, 0, 0, box.size[1], 0, 0, 0, 0, box.size[2], 0, box.center[0], box.center[1], box.center[2], 1])));
	gl.bindVertexArray(wireBox);
	gl.drawElements(gl.LINES, 24, gl.UNSIGNED_SHORT, 0);
}

// The lines of the editor: the boxes of the entities and what they are linked to. What belongs to the selected
// entity is drawn last and through the walls.
function drawEditorLines() {
	const volumes = $('dbgVolumes').checked, linked = $('dbgLinks').checked, clip = world && $('dbgClip').checked ? brushWire('clip') : null;
	// a box that has handles to drag is drawn, whatever the views are set to
	const handled = boxHandles().length > 0;
	if (!volumes && !linked && !clip && !handled) return;
	const sel = ents.get(selectedEnt);
	gl.useProgram(lineProg.p);
	if (clip) {
		gl.uniformMatrix4fv(lineProg.u.uMvp, false, viewProj);
		gl.uniform4f(lineProg.u.uColor, 1, 0.35, 0.9, 1);
		gl.bindVertexArray(clip.vao);
		gl.drawArrays(gl.LINES, 0, clip.count);
	}
	if (volumes) {
		for (const ent of ents.values()) if (ent !== sel && entVisible(ent)) drawEntBox(ent, classColor(ent.classname));
	}
	gl.uniformMatrix4fv(lineProg.u.uMvp, false, viewProj);
	if (linked) drawLinkSet(links.filter(l => l.from !== selectedEnt && l.to !== selectedEnt), [0.3, 0.7, 0.8, 1]);
	if (sel) {
		gl.disable(gl.DEPTH_TEST);
		if (linked) {
			// what it fires, and what fires it
			drawLinkSet(links.filter(l => l.from === selectedEnt), [1, 0.65, 0.15, 1]);
			drawLinkSet(links.filter(l => l.to === selectedEnt), [0.4, 1, 0.45, 1]);
		}
		if (volumes || handled) drawEntBox(sel, [1, 0.25, 0.35]);
		// and the ones that are selected with it
		if (volumes) for (const id of picked) if (ents.has(id)) drawEntBox(ents.get(id), [1, 0.55, 0.25]);
		gl.enable(gl.DEPTH_TEST);
	}
	// what fired just now, over everything else
	const now = performance.now();
	if (fired.length) fired = fired.filter(f => now - f.at < FIRED_MS);
	if (fired.length) {
		gl.disable(gl.DEPTH_TEST);
		gl.uniformMatrix4fv(lineProg.u.uMvp, false, viewProj);
		if (linked) drawLinkSet(fired, [1, 1, 0.2, 1]);
		if (volumes) for (const f of fired) if (ents.has(f.to)) drawEntBox(ents.get(f.to), [1, 1, 0.2]);
		gl.enable(gl.DEPTH_TEST);
	}
	gl.useProgram(flatProg.p);
}

