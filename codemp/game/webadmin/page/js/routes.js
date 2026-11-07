// ---- the route of the bots: /api/waypoints, and /api/waypoint to change it

// selected: the waypoint the pane tells of and a new one is put behind, picked: the ones that are selected besides
// it. ghost: how far the selected ones are dragged from where they are, while they are dragged.
const wp = { points: [], max: 0, editing: false, selected: -1, picked: new Set(), adding: false, ghost: null, vao: null, buf: null, trail: 0, links: 0, cross: 0 };
const WP_FLAGS = [[0x10, 'jump'], [0x20, 'duck'], [0x400, 'no sight needed'], [0x800, 'snipe or camp, standing'], [0x1000, 'wait for a mover'], [0x2000, 'snipe or camp, ducked'],
	[0x4000, 'one way forward'], [0x8000, 'one way back'], [0x10000, 'goal'], [0x20000, 'red flag'], [0x40000, 'blue flag'], [0x80000, 'rebel objective'], [0x100000, 'imperial objective']];

// all that are selected and still there, by their number
function wpSelected() { return [...new Set([wp.selected, ...wp.picked])].filter(i => wp.points[i]).sort((a, b) => a - b); }

async function wpLoad() {
	try {
		const json = await (await fetch('api/waypoints', { cache: 'no-store' })).json();
		wp.points = json.points;
		wp.max = json.max;
		wp.editing = json.editing;
		// what is selected is a number: one that has no waypoint any more is not selected
		if (!wp.points[wp.selected]) { wp.selected = -1; wp.picked.clear(); }
		for (const i of [...wp.picked]) if (!wp.points[i]) wp.picked.delete(i);
		wpBuild();
		wpRender();
	} catch (e) { /* the route that is shown stays */ }
}

// The route as lines in one buffer: the trail from each waypoint to the next, then the other waypoints one
// leads to, then a small cross at every one.
function wpBuild() {
	const P = wp.points, trail = [], links = [], cross = [], r = 7;
	for (let i = 0; i < P.length; i++) {
		const a = P[i];
		if (!a) continue;
		const [x, y, z] = a.pos;
		cross.push(x - r, y, z, x + r, y, z, x, y - r, z, x, y + r, z, x, y, z - r, x, y, z + r);
		if (P[i + 1]) trail.push(x, y, z, P[i + 1].pos[0], P[i + 1].pos[1], P[i + 1].pos[2]);
		for (const n of a.next) if (n > i + 1 && P[n]) links.push(x, y, z, P[n].pos[0], P[n].pos[1], P[n].pos[2]);
	}
	if (!wp.vao) {
		wp.vao = gl.createVertexArray();
		wp.buf = gl.createBuffer();
		gl.bindVertexArray(wp.vao);
		gl.bindBuffer(gl.ARRAY_BUFFER, wp.buf);
		gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
		gl.bindVertexArray(null);
	}
	gl.bindBuffer(gl.ARRAY_BUFFER, wp.buf);
	gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(trail.concat(links, cross)), gl.DYNAMIC_DRAW);
	wp.trail = trail.length / 3;
	wp.links = links.length / 3;
	wp.cross = cross.length / 3;
}

function drawRoute() {
	if (!wp.vao || !wp.cross) return;
	gl.useProgram(lineProg.p);
	gl.uniformMatrix4fv(lineProg.u.uMvp, false, viewProj);
	gl.bindVertexArray(wp.vao);
	// Twice: faint and through everything, so that the route under a roof is seen from above, then as it is
	// where nothing is in front of it. The faint lines leave the alpha of the canvas alone.
	for (const faint of [true, false]) {
		const a = faint ? 0.3 : 1;
		if (faint) { gl.disable(gl.DEPTH_TEST); gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); gl.colorMask(true, true, true, false); }
		gl.uniform4f(lineProg.u.uColor, 0.3, 0.6, 1, a);
		gl.drawArrays(gl.LINES, wp.trail, wp.links);
		gl.uniform4f(lineProg.u.uColor, 1, 0.85, 0.2, a);
		gl.drawArrays(gl.LINES, 0, wp.trail);
		gl.uniform4f(lineProg.u.uColor, 1, 1, 1, a);
		gl.drawArrays(gl.LINES, wp.trail + wp.links, wp.cross);
		if (faint) { gl.enable(gl.DEPTH_TEST); gl.disable(gl.BLEND); gl.colorMask(true, true, true, true); }
	}
	const ids = wpSelected();
	if (ids.length) {
		// A player stands there: his box, seen through the walls, at every selected one. While they are dragged
		// the boxes are where they would go, the route stays where it is.
		const g = wp.ghost || [0, 0, 0];
		gl.disable(gl.DEPTH_TEST);
		gl.uniform4f(lineProg.u.uColor, 1, 0.25, 0.35, 1);
		gl.bindVertexArray(wireBox);
		for (const i of ids) {
			const pos = wp.points[i].pos;
			gl.uniformMatrix4fv(lineProg.u.uMvp, false, M4.mul(viewProj, new Float32Array([30, 0, 0, 0, 0, 30, 0, 0, 0, 0, 64, 0, pos[0] + g[0], pos[1] + g[1], pos[2] + g[2] + 8, 1])));
			gl.drawElements(gl.LINES, 24, gl.UNSIGNED_SHORT, 0);
		}
		gl.enable(gl.DEPTH_TEST);
	}
	gl.useProgram(flatProg.p);
}

function wpAt(x, y) {
	let best = -1, bestD = 14;
	wp.points.forEach((p, i) => {
		const sp = p && project(p.pos);
		if (!sp) return;
		const d = Math.hypot(sp[0] - x, sp[1] - y);
		if (d < bestD) { bestD = d; best = i; }
	});
	return best;
}

// this one and no other, -1 for none
function wpSelect(i) {
	wp.selected = i;
	wp.picked.clear();
	wpRender();
}

// A click on the map: that waypoint alone, or none where there is none. more (Ctrl): it is added to the selected
// ones, or taken out of them when it is one.
function wpClick(i, more) {
	if (!more || i < 0 || wp.selected < 0) { wpSelect(i); return; }
	if (i === wp.selected) {
		// the pane tells of another one of them from now on
		const [next = -1] = wp.picked;
		wp.picked.delete(next);
		wp.selected = next;
	} else if (!wp.picked.delete(i)) wp.picked.add(i);
	wpRender();
}

// Every waypoint whose point is in the rectangle on the screen is selected, the one nearest to where the drag
// began as the one the pane tells of. more: they are added to what is selected already.
function wpBandSelect(b, more) {
	const x0 = Math.min(b.x, b.x2), x1 = Math.max(b.x, b.x2), y0 = Math.min(b.y, b.y2), y1 = Math.max(b.y, b.y2), inside = [];
	wp.points.forEach((p, i) => {
		const sp = p && project(p.pos);
		if (sp && sp[0] >= x0 && sp[0] <= x1 && sp[1] >= y0 && sp[1] <= y1) inside.push([Math.hypot(sp[0] - b.x, sp[1] - b.y), i]);
	});
	inside.sort((p, q) => p[0] - q[0]);
	if (!more || wp.selected < 0) { wp.picked.clear(); wp.selected = inside.length ? inside[0][1] : -1; }
	for (const [, i] of inside) if (i !== wp.selected) wp.picked.add(i);
	wpRender();
	if (inside.length) toast(inside.length + ' in the rectangle');
}

function wpSetAdding(on) {
	wp.adding = on;
	$('wpAdd').classList.toggle('armedPlace', on);
	$('wpAdd').textContent = on ? 'Right-click map' : 'Add waypoint';
	if (mode === 'routes') canvas.style.cursor = on ? 'crosshair' : 'default';
}

function wpRender() {
	const used = wp.points.filter(Boolean).length, sel = wp.points[wp.selected], count = wpSelected().length;
	$('wpCount').textContent = used + ' / ' + wp.max;
	$('wpRemove').disabled = !count;
	$('wpSetFlags').disabled = !count;
	const flags = sel ? WP_FLAGS.filter(f => sel.flags & f[0]).map(f => f[1]).join(', ') : '';
	$('wpState').textContent = (wp.editing ? 'Changed and not saved: the bots stand still. ' : '') + (count > 1 ? count + ' waypoints selected. ' : '') +
		(sel ? 'Waypoint ' + wp.selected + ' at ' + fmtVec(sel.pos) + (flags ? ': ' + flags : '') : used ? '' : 'This map has no route for the bots.');
}

async function wpOp(params) {
	const json = await apiPost('api/waypoint', params);
	toast(json.message, json.ok);
	await wpLoad();
	return json.ok;
}

// which waypoints a request is for: one by its number, as a game module before the lists knows it, several as a list
function wpWhich(ids) { return ids.length === 1 ? { index: ids[0] } : { indexes: ids.join(',') }; }

// a waypoint is where the origin of a player is who stands there: that much above the floor
async function wpAddAt(hit) {
	const after = wp.selected, last = after < 0 || after >= wp.points.length - 1;
	if (await wpOp({ op: 'add', x: hit[0].toFixed(1), y: hit[1].toFixed(1), z: (hit[2] + 24).toFixed(1), flags: $('wpFlags').value, after: last ? '' : after })) {
		wpSelect(last ? wp.points.length - 1 : after + 1);
	}
}

// ---- moving the selected waypoints with the mouse: the one that is grabbed follows the floor under the mouse, as
// high above it as it was, and the others go with it

function wpDragStart(i) {
	const pos = wp.points[i].pos, below = pickGround([pos[0], pos[1], pos[2] + 1], [0, 0, -1]);
	return { index: i, height: below ? pos[2] - below[2] : 0 };
}
function wpDragMove(grab, ray) {
	const hit = snapHit(pickGround(ray.origin, ray.dir)), p = wp.points[grab.index];
	if (hit && p) wp.ghost = [hit[0] - p.pos[0], hit[1] - p.pos[1], hit[2] + grab.height - p.pos[2]];
}
// let go: all of them are moved by one request. The boxes stay where they were dragged to until the route is read again.
async function wpDragEnd() {
	const g = wp.ghost, ids = wpSelected();
	if (!g) return;
	if (ids.length) await wpOp({ op: 'move', indexes: ids.join(','), dx: g[0].toFixed(1), dy: g[1].toFixed(1), dz: g[2].toFixed(1) });
	if (wp.ghost === g) wp.ghost = null;
}

$('wpAdd').addEventListener('click', () => wpSetAdding(!wp.adding));
$('wpRemove').addEventListener('click', async () => {
	const ids = wpSelected();
	// the numbers of the ones behind a removed one change: nothing is selected afterwards
	if (ids.length && await wpOp({ op: 'remove', ...wpWhich(ids) })) wpSelect(-1);
});
$('wpSetFlags').addEventListener('click', () => {
	const ids = wpSelected();
	if (ids.length) wpOp({ op: 'flags', ...wpWhich(ids), flags: $('wpFlags').value });
});
$('wpSave').addEventListener('click', () => wpOp({ op: 'save' }));
// another route: what was selected of this one is not
$('wpRevert').addEventListener('click', async () => { if (await wpOp({ op: 'revert' })) wpSelect(-1); });
$('wpDefault').addEventListener('click', async () => {
	if (!confirmed($('wpDefault'))) return;
	if (await wpOp({ op: 'default' })) wpSelect(-1);
});
// Esc drops the selection (the other keys of the map are in js/input.js)
window.addEventListener('keydown', e => {
	if (e.key !== 'Escape' || mode !== 'routes' || /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
	wpSelect(-1);
});
