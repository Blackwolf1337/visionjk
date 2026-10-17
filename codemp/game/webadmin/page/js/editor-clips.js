// ---- the clip editor: what stops players without being seen. The clips of the map, turned off and on while the
// map runs, and the blocking walls, drawn, dragged and shaped here (/api/clips, /api/clip).
//
// A clip is a brush of the map that stops players, NPCs, bots or shots. It is part of the map file, which the
// server and every game load alike, so it can not be moved for everybody while the map runs. The server can stop
// minding one, and tells the games of the players which ones: a game that knows of that turns them off too, any
// other one still has the clip, stops its player there for a moment and is then set right by the server. A clip
// is known by its number in the map file, which the page reads itself.
//
// A blocking wall is a box the server puts up while the map runs and tells every game of. It is no entity of the
// World tab: it is placed, dragged, shaped by its yellow handles and removed here. To move a clip it is turned
// off and a wall of its size takes its place.
//
// Both are kept by Save, in one file of the map, as the route of the bots is kept by its save.

// off: the numbers of the clips the server has off. walls: { id, mins, maxs }, the corners in the world. dirty:
// changed since the last save. known: the game module has a clip editor. selected: the clip the pane tells of,
// wall: the wall, -1 for none, never both. placing: a right-click on the map puts a new wall there. move: how
// far the selected wall is dragged, while it is. offLines: the edges of the clips that are off.
const clip = { off: new Set(), walls: [], dirty: false, known: true, key: '', selected: -1, wall: -1, placing: false, move: null, busy: false, offLines: null };
// whom a clip stops, by its contents: CONTENTS_PLAYERCLIP, MONSTERCLIP, BOTCLIP, SHOTCLIP
const CLIP_STOPS = [[0x10, 'players'], [0x20, 'NPCs'], [0x40, 'bots'], [0x80, 'shots']];
// a new wall: so wide, thick and high, and the sizes of a wall are whole steps of 2 (the server makes them even)
const CLIP_WALL_NEW = [128, 16, 128], CLIP_WALL_STEP = 2;

// The clips of the world, made once for a map: { n, contents, mins, maxs, planes, edges, box }. As the engine
// counts them: a brush of the world that stops one of the four and is not solid. box: its faces lie along the
// axes, so the box around it is what it is.
function clipBrushes() {
	if (!world) return [];
	if (world.clips) return world.clips;
	const bd = world.brushes, model = world.models[0], out = [];
	for (let i = model ? model.firstBrush : 0; model && i < model.firstBrush + model.numBrushes && i * 3 + 2 < bd.brushes.length; i++) {
		const contents = ((world.shaders[bd.brushes[i * 3 + 2]] || {}).contents | 0);
		if (!(contents & CONTENTS_CLIP) || (contents & 1)) continue;
		const edges = [], planes = [], first = bd.brushes[i * 3], count = bd.brushes[i * 3 + 1];
		brushEdges(bd, i, edges);
		if (!edges.length) continue;
		for (let s = 0; s < count; s++) {
			const p = bd.sides[(first + s) * 3] * 4;
			if (p >= 0 && p + 3 < bd.planes.length) planes.push([bd.planes[p], bd.planes[p + 1], bd.planes[p + 2], bd.planes[p + 3]]);
		}
		const mins = [Infinity, Infinity, Infinity], maxs = [-Infinity, -Infinity, -Infinity];
		let box = true;
		for (let k = 0; k < edges.length; k += 6) {
			let along = 0;
			for (let a = 0; a < 3; a++) {
				mins[a] = Math.min(mins[a], edges[k + a], edges[k + 3 + a]);
				maxs[a] = Math.max(maxs[a], edges[k + a], edges[k + 3 + a]);
				if (Math.abs(edges[k + a] - edges[k + 3 + a]) > 0.1) along++;
			}
			if (along > 1) box = false;
		}
		out.push({ n: i, contents, mins, maxs, planes, edges: new Float32Array(edges), box });
	}
	return world.clips = out;
}
const clipOf = n => clipBrushes().find(b => b.n === n);
const clipWall = id => clip.walls.find(w => w.id === id);
const clipSize = b => b.maxs.map((v, i) => Math.round(v - b.mins[i])).join(' x ');
const clipStops = b => CLIP_STOPS.filter(s => b.contents & s[0]).map(s => s[1]).join(', ');
const clipEven = v => Math.max(CLIP_WALL_STEP, Math.ceil(Math.round(v * 100) / 100 / CLIP_WALL_STEP) * CLIP_WALL_STEP);

// ---- what the line of sight goes into first

// how far along a ray a box begins, Infinity when the ray misses it or starts in it
function clipRayBox(o, d, mins, maxs) {
	let t0 = -Infinity, t1 = Infinity;
	for (let a = 0; a < 3; a++) {
		if (Math.abs(d[a]) < 1e-7) { if (o[a] < mins[a] || o[a] > maxs[a]) return Infinity; continue; }
		const x = (mins[a] - o[a]) / d[a], y = (maxs[a] - o[a]) / d[a];
		t0 = Math.max(t0, Math.min(x, y));
		t1 = Math.min(t1, Math.max(x, y));
	}
	return t0 <= t1 && t0 > 0 ? t0 : Infinity;
}
// the same for a brush, which is the space behind all of its planes
function clipRayBrush(o, d, planes) {
	let t0 = -Infinity, t1 = Infinity;
	for (const [nx, ny, nz, dist] of planes) {
		const along = nx * d[0] + ny * d[1] + nz * d[2], front = nx * o[0] + ny * o[1] + nz * o[2] - dist;
		if (Math.abs(along) < 1e-7) { if (front > 0) return Infinity; continue; }
		if (along < 0) t0 = Math.max(t0, -front / along); else t1 = Math.min(t1, -front / along);
		if (t0 > t1) return Infinity;
	}
	return t0 > 0 ? t0 : Infinity;
}
// what is under a point of the screen: { wall } or { clip }, the nearer one, null for neither
function clipAt(x, y) {
	const ray = screenRay(x, y);
	let best = null, bestT = Infinity;
	for (const w of clip.walls) {
		const t = clipRayBox(ray.origin, ray.dir, w.mins, w.maxs);
		if (t < bestT) { bestT = t; best = { wall: w.id }; }
	}
	for (const b of clipBrushes()) {
		const t = clipRayBrush(ray.origin, ray.dir, b.planes);
		if (t < bestT) { bestT = t; best = { clip: b.n }; }
	}
	return best;
}
function clipPick(what) {
	clip.selected = what && what.clip !== undefined ? what.clip : -1;
	clip.wall = what && what.wall !== undefined ? what.wall : -1;
	clip.move = null;
	clipRender();
}
// a click on the map in the Clips tab (js/input.js)
function clipClick(x, y) { clipPick(clipAt(x, y)); }

// ---- what the server has

async function clipPoll() {
	if (appMode !== 'editor' || (mode !== 'clips' && !$('dbgClip').checked) || clip.busy) return;
	clip.busy = true;
	try {
		const res = await fetch('api/clips', { cache: 'no-store' });
		clip.known = res.ok;
		const json = res.ok ? await res.json() : { off: [], walls: [], dirty: false };
		const key = JSON.stringify(json);
		if (key !== clip.key) {
			clip.key = key;
			clip.off = new Set(json.off);
			clip.walls = json.walls;
			clip.dirty = json.dirty;
			clip.offLines = null;
			if (clip.wall >= 0 && !clipWall(clip.wall)) clip.wall = -1;
			if (mode === 'clips') clipRender();
		}
	} catch (e) { /* what is shown stays */ } finally { clip.busy = false; }
}
setInterval(clipPoll, 1000);

// A change. The answer of the server is shown, and what it has now is asked for at once: a clip is turned off
// in its next frame, so once more a moment later.
async function clipOp(params) {
	const json = await apiPost('api/clip', params, 'This game module has no clip editor.');
	toast(json.message, json.ok);
	clip.key = '';
	await clipPoll();
	setTimeout(clipPoll, 250);
	return json;
}
const clipSet = (numbers, on) => clipOp({ op: on ? 'on' : 'off', brush: numbers.join(' ') });

// A wall with these corners: a new one, or the one of that number in another place or of another size. It is
// the selected one afterwards (the server gives it a new number).
async function clipWallPut(id, mins, maxs) {
	const r = v => Math.round(v * 10) / 10, params = { op: 'wall', x1: r(mins[0]), y1: r(mins[1]), z1: r(mins[2]), x2: r(maxs[0]), y2: r(maxs[1]), z2: r(maxs[2]) };
	if (id >= 0) params.id = id;
	const json = await clipOp(params);
	if (json.ok && json.id >= 0) clipPick({ wall: json.id });
	return json.ok;
}

// ---- the selected wall: its handles, its size in the pane, moving it with the mouse

// for the yellow handles (boxSubject in js/editor-move.js): its box from its middle
function clipWallSubject() {
	const w = clipWall(clip.wall);
	if (!w) return null;
	const origin = w.mins.map((v, i) => (v + w.maxs[i]) / 2);
	return { id: 'wall' + w.id, origin, mins: w.mins.map((v, i) => v - origin[i]), maxs: w.maxs.map((v, i) => v - origin[i]), step: CLIP_WALL_STEP,
		show: (mins, maxs) => clipShowSize(maxs.map((v, i) => v - mins[i])),
		set: (mins, maxs) => clipWallPut(w.id, mins.map((v, i) => v + origin[i]), maxs.map((v, i) => v + origin[i])) };
}
function clipShowSize(size) {
	['clipSizeX', 'clipSizeY', 'clipSizeZ'].forEach((id, i) => { if (document.activeElement !== $(id)) $(id).value = size ? Math.round(size[i]) : ''; });
}
// the corners of a wall as it is drawn now: while a face of it or all of it is dragged, where that would put it
function clipWallShown(w) {
	if (clip.move && clip.move.id === w.id && clip.move.by) return { mins: w.mins.map((v, i) => v + clip.move.by[i]), maxs: w.maxs.map((v, i) => v + clip.move.by[i]) };
	if (boxGhost && boxGhost.id === 'wall' + w.id) {
		const origin = w.mins.map((v, i) => (v + w.maxs[i]) / 2);
		return { mins: boxGhost.mins.map((v, i) => v + origin[i]), maxs: boxGhost.maxs.map((v, i) => v + origin[i]) };
	}
	return w;
}

// The selected wall is grabbed (js/input.js): the middle of its foot follows the floor under the mouse, as high
// above it as it stood.
function clipDragStart(id) {
	const w = clipWall(id), foot = [(w.mins[0] + w.maxs[0]) / 2, (w.mins[1] + w.maxs[1]) / 2, w.mins[2]];
	const below = pickGround([foot[0], foot[1], foot[2] + 1], [0, 0, -1]);
	return clip.move = { id, foot, height: below ? foot[2] - below[2] : 0, by: null };
}
function clipDragMove(m, ray) {
	const hit = snapHit(pickGround(ray.origin, ray.dir));
	if (hit) m.by = [hit[0] - m.foot[0], hit[1] - m.foot[1], hit[2] + m.height - m.foot[2]];
}
async function clipDragEnd(m) {
	const w = clipWall(m.id);
	if (w && m.by) await clipWallPut(w.id, w.mins.map((v, i) => v + m.by[i]), w.maxs.map((v, i) => v + m.by[i]));
	clip.move = null;
}
// A right-click on the map (js/input.js) at a point of the floor. A new wall stands there with its wide side
// across the view, or the selected wall goes there.
function clipRightClick(hit) {
	const w = clipWall(clip.wall);
	if (clip.placing) {
		// the long side along Y when the view goes along X, and along X when it goes along Y
		const d = camDir(), [wide, thick, high] = CLIP_WALL_NEW, size = Math.abs(d[0]) > Math.abs(d[1]) ? [thick, wide, high] : [wide, thick, high];
		clipWallPut(-1, [hit[0] - size[0] / 2, hit[1] - size[1] / 2, hit[2]], [hit[0] + size[0] / 2, hit[1] + size[1] / 2, hit[2] + size[2]]);
		clipSetPlacing(false);
	} else if (w) {
		const by = [hit[0] - (w.mins[0] + w.maxs[0]) / 2, hit[1] - (w.mins[1] + w.maxs[1]) / 2, hit[2] - w.mins[2]];
		clipWallPut(w.id, w.mins.map((v, i) => v + by[i]), w.maxs.map((v, i) => v + by[i]));
	} else toast('Select a wall to move it here, or use New wall', false);
}
function clipSetPlacing(on) {
	clip.placing = on;
	$('clipNew').classList.toggle('armedPlace', on);
	$('clipNew').textContent = on ? 'Right-click map' : 'New wall';
	if (mode === 'clips') canvas.style.cursor = on ? 'crosshair' : 'default';
}
// another tab: nothing waits for a click (js/editor-world.js)
function clipLeave() { if (clip.placing) clipSetPlacing(false); clip.move = null; }

// ---- the pane

function clipRender() {
	const all = clipBrushes(), sel = clipOf(clip.selected), wall = clipWall(clip.wall), isOff = !!sel && clip.off.has(sel.n);
	$('clipCount').textContent = all.length + ' clips' + (clip.off.size ? ', ' + clip.off.size + ' off' : '') + ' · ' + clip.walls.length + ' walls';
	$('clipToggle').disabled = $('clipToWall').disabled = !sel || !clip.known;
	$('clipToggle').textContent = isOff ? 'Turn on' : 'Turn off';
	$('clipAllOn').disabled = !clip.off.size;
	$('clipNew').disabled = !clip.known;
	$('clipDelete').disabled = !wall;
	['clipSizeX', 'clipSizeY', 'clipSizeZ'].forEach(id => { $(id).disabled = !wall; });
	clipShowSize(wall ? wall.maxs.map((v, i) => v - wall.mins[i]) : null);
	$('clipSave').classList.toggle('primary', clip.dirty);
	$('clipState').textContent = !world ? '' : !clip.known ? 'This game module has no clip editor.' : (clip.dirty ? 'Changed and not saved: gone with the map. ' : '') +
		(wall ? 'Wall ' + clipSize(wall) + ' at ' + fmtVec(wall.mins.map((v, i) => (v + wall.maxs[i]) / 2)) + '. Drag it, or its yellow handles.'
			: sel ? 'Clip ' + sel.n + (isOff ? ', off' : '') + ': ' + clipSize(sel) + ', stops ' + clipStops(sel) + (sel.box ? '' : ', not a box') : 'Click a clip or a wall on the map.');
	const row = (attr, on, name, what, act) => '<tr ' + attr + (on ? ' class="sel"' : '') + '><td>' + name + '</td><td>' + what + '</td><td class="acts">' + act + '</td></tr>';
	$('clipRows').innerHTML = clip.walls.map(w => row('data-wall="' + w.id + '"', w.id === clip.wall, 'Wall', clipSize(w), '<button data-del="' + w.id + '">Remove</button>'))
		.concat([...clip.off].sort((a, b) => a - b).map(n => {
			const b = clipOf(n);
			return row('data-clip="' + n + '"', n === clip.selected, 'Clip ' + n, b ? 'off, ' + clipSize(b) : 'off', '<button data-on="' + n + '">Turn on</button>');
		})).join('') || '<tr class="off"><td class="hint">No wall stands and no clip is off.</td></tr>';
}

// ---- drawn with the clips of the map (js/editor-lines.js): the walls, the clips that are off, what is selected

function clipBoxEdges(mins, maxs, out) {
	const c = i => [(i & 1) ? maxs[0] : mins[0], (i & 2) ? maxs[1] : mins[1], (i & 4) ? maxs[2] : mins[2]];
	// every corner to the three that differ from it in one axis
	for (let i = 0; i < 8; i++) for (let a = 0; a < 3; a++) if (!(i & (1 << a))) out.push(...c(i), ...c(i | (1 << a)));
}
function drawClipMarks() {
	if (!clip.offLines) {
		const parts = [...clip.off].map(clipOf).filter(Boolean).map(b => b.edges);
		clip.offLines = new Float32Array(parts.reduce((n, e) => n + e.length, 0));
		parts.reduce((at, e) => { clip.offLines.set(e, at); return at + e.length; }, 0);
	}
	const here = mode === 'clips', sel = here ? clipOf(clip.selected) : null, walls = [], chosen = [];
	for (const w of clip.walls) {
		const s = clipWallShown(w);
		clipBoxEdges(s.mins, s.maxs, here && w.id === clip.wall ? chosen : walls);
	}
	gl.bindVertexArray(linkLines.vao);
	gl.bindBuffer(gl.ARRAY_BUFFER, linkLines.buf);
	// the walls as they stand in the map, the rest through everything
	for (const [data, c, through] of [[new Float32Array(walls), [0.62, 0.45, 1, 1], false], [clip.offLines, [1, 0.2, 0.2, 1], true],
		[sel ? sel.edges : null, [1, 0.9, 0.2, 1], true], [new Float32Array(chosen), [1, 0.9, 0.2, 1], true]]) {
		if (!data || !data.length) continue;
		if (through) gl.disable(gl.DEPTH_TEST);
		gl.bufferData(gl.ARRAY_BUFFER, data, gl.DYNAMIC_DRAW);
		gl.uniform4fv(lineProg.u.uColor, c);
		gl.drawArrays(gl.LINES, 0, data.length / 3);
		if (through) gl.enable(gl.DEPTH_TEST);
	}
}

// ---- the buttons

$('clipToggle').addEventListener('click', () => {
	const b = clipOf(clip.selected);
	if (b) clipSet([b.n], clip.off.has(b.n));
});
$('clipAllOn').addEventListener('click', () => { if (clip.off.size) clipSet([...clip.off], true); });
// Move it: a wall of its size where it is, then the clip is off. The wall first: the server refuses one that is
// too large, and the clip stays as it is then. A clip that is no box gets the box around it.
$('clipToWall').addEventListener('click', async () => {
	const b = clipOf(clip.selected);
	if (!b) return;
	const size = b.maxs.map((v, i) => clipEven(v - b.mins[i])), mid = b.mins.map((v, i) => (v + b.maxs[i]) / 2);
	if (!await clipWallPut(-1, mid.map((v, i) => v - size[i] / 2), mid.map((v, i) => v + size[i] / 2))) return;
	if (!clip.off.has(b.n)) await clipSet([b.n], false);
	if (!b.box) toast('The clip is no box: the wall is the box around it');
});
$('clipNew').addEventListener('click', () => clipSetPlacing(!clip.placing));
$('clipDelete').addEventListener('click', async () => {
	if (clip.wall < 0) return;
	const id = clip.wall;
	clipPick(null);
	await clipOp({ op: 'delete', id });
});
$('clipSave').addEventListener('click', () => clipOp({ op: 'save' }));
$('clipRevert').addEventListener('click', async () => {
	if (clip.dirty && !confirmed($('clipRevert'))) return;
	clipPick(null);
	await clipOp({ op: 'revert' });
});
// the size of the selected wall, typed: around its middle, its foot stays on the floor it stands on
for (const id of ['clipSizeX', 'clipSizeY', 'clipSizeZ']) {
	$(id).addEventListener('change', () => {
		const w = clipWall(clip.wall);
		if (!w) return;
		const size = ['clipSizeX', 'clipSizeY', 'clipSizeZ'].map((f, i) => clipEven(+$(f).value || w.maxs[i] - w.mins[i])), mid = w.mins.map((v, i) => (v + w.maxs[i]) / 2);
		clipWallPut(w.id, [mid[0] - size[0] / 2, mid[1] - size[1] / 2, w.mins[2]], [mid[0] + size[0] / 2, mid[1] + size[1] / 2, w.mins[2] + size[2]]);
	});
}
$('clipRows').addEventListener('click', e => {
	const on = e.target.closest('[data-on]'), del = e.target.closest('[data-del]'), row = e.target.closest('tr');
	if (on) { clipSet([+on.dataset.on], true); return; }
	if (del) { if (+del.dataset.del === clip.wall) clipPick(null); clipOp({ op: 'delete', id: del.dataset.del }); return; }
	if (!row || (!row.dataset.clip && !row.dataset.wall)) return;
	// to it: the camera looks at its middle
	const b = row.dataset.wall ? clipWall(+row.dataset.wall) : clipOf(+row.dataset.clip);
	if (!b) return;
	clipPick(row.dataset.wall ? { wall: b.id } : { clip: b.n });
	boxLookAt(b);
});
// Esc: nothing is selected and nothing waits for a click (the other keys of the map are in js/input.js)
window.addEventListener('keydown', e => {
	if (e.key !== 'Escape' || mode !== 'clips' || uiKeyIsTyped(e)) return;
	clipSetPlacing(false);
	clipPick(null);
});
