// ---- the route of the bots: /api/waypoints, and /api/waypoint to change it

const wp = { points: [], max: 0, editing: false, selected: -1, adding: false, vao: null, buf: null, trail: 0, links: 0, cross: 0 };
const WP_FLAGS = [[0x10, 'jump'], [0x20, 'duck'], [0x400, 'no sight needed'], [0x800, 'snipe or camp, standing'], [0x1000, 'wait for a mover'], [0x2000, 'snipe or camp, ducked'],
	[0x4000, 'one way forward'], [0x8000, 'one way back'], [0x10000, 'goal'], [0x20000, 'red flag'], [0x40000, 'blue flag'], [0x80000, 'rebel objective'], [0x100000, 'imperial objective']];

async function wpLoad() {
	try {
		const json = await (await fetch('api/waypoints', { cache: 'no-store' })).json();
		wp.points = json.points;
		wp.max = json.max;
		wp.editing = json.editing;
		if (wp.selected >= wp.points.length) wp.selected = -1;
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
	const sel = wp.points[wp.selected];
	if (sel) {
		// a player stands there: his box, seen through the walls
		gl.disable(gl.DEPTH_TEST);
		gl.uniformMatrix4fv(lineProg.u.uMvp, false, M4.mul(viewProj, new Float32Array([30, 0, 0, 0, 0, 30, 0, 0, 0, 0, 64, 0, sel.pos[0], sel.pos[1], sel.pos[2] + 8, 1])));
		gl.uniform4f(lineProg.u.uColor, 1, 0.25, 0.35, 1);
		gl.bindVertexArray(wireBox);
		gl.drawElements(gl.LINES, 24, gl.UNSIGNED_SHORT, 0);
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

function wpSelect(i) {
	wp.selected = i;
	wpRender();
}

function wpSetAdding(on) {
	wp.adding = on;
	$('wpAdd').classList.toggle('armedPlace', on);
	$('wpAdd').textContent = on ? 'Right-click map' : 'Add';
	if (mode === 'routes') canvas.style.cursor = on ? 'crosshair' : 'default';
}

function wpRender() {
	const used = wp.points.filter(Boolean).length, sel = wp.points[wp.selected];
	$('wpCount').textContent = used + ' / ' + wp.max;
	$('wpRemove').disabled = !sel;
	$('wpSetFlags').disabled = !sel;
	const flags = sel ? WP_FLAGS.filter(f => sel.flags & f[0]).map(f => f[1]).join(', ') : '';
	$('wpState').textContent = (wp.editing ? 'Changed and not saved: the bots stand still. ' : '') +
		(sel ? 'Waypoint ' + wp.selected + ' at ' + fmtVec(sel.pos) + (flags ? ': ' + flags : '') : used ? '' : 'This map has no route for the bots.');
}

async function wpOp(params) {
	try {
		const json = await (await fetch('api/waypoint', { method: 'POST', body: new URLSearchParams(params) })).json();
		toast(json.message, json.ok);
		await wpLoad();
		return json.ok;
	} catch (e) {
		toast('Request failed: ' + e.message, false);
		return false;
	}
}

// a waypoint is where the origin of a player is who stands there: that much above the floor
async function wpAddAt(hit) {
	const after = wp.selected, last = after < 0 || after >= wp.points.length - 1;
	if (await wpOp({ op: 'add', x: hit[0].toFixed(1), y: hit[1].toFixed(1), z: (hit[2] + 24).toFixed(1), flags: $('wpFlags').value, after: last ? '' : after })) {
		wpSelect(last ? wp.points.length - 1 : after + 1);
	}
}

$('wpAdd').addEventListener('click', () => wpSetAdding(!wp.adding));
$('wpRemove').addEventListener('click', async () => {
	if (wp.selected < 0) return;
	const i = wp.selected;
	if (await wpOp({ op: 'remove', index: i })) wpSelect(Math.min(i, wp.points.length - 1));
});
$('wpSetFlags').addEventListener('click', () => { if (wp.selected >= 0) wpOp({ op: 'flags', index: wp.selected, flags: $('wpFlags').value }); });
$('wpSave').addEventListener('click', () => wpOp({ op: 'save' }));
$('wpReload').addEventListener('click', wpLoad);

