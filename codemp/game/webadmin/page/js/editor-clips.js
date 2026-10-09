// ---- the clip editor: the clips of the map, turned off and on while the map runs (/api/clip)
//
// A clip is a brush of the map that stops players, NPCs, bots or shots and is not seen. It is part of the map
// file, which the server and every game load alike, so it can not be moved for everybody while the map runs. The
// server can stop minding one: it lets players through there. Their own game still has the clip, so a player who
// walks into one that is off is stopped for a moment and then set to where the server has him. To move a clip it
// is turned off and a blocking wall of its size is put where it was: that one every game is told of, and it is
// dragged and shaped in the World tab like any wall. The brushes are known by their number in the map file, which
// the page reads itself. What is off is part of the layout of the map.

// off: the numbers the server has off. known: the game module tells them (an older one does not). selected: the
// clip the pane tells of, -1 for none. offLines: the edges of the ones that are off, made when they change.
const clip = { off: new Set(), known: true, key: '', selected: -1, offLines: null };
// whom a clip stops, by its contents: CONTENTS_PLAYERCLIP, MONSTERCLIP, BOTCLIP, SHOTCLIP
const CLIP_STOPS = [[0x10, 'players'], [0x20, 'NPCs'], [0x40, 'bots'], [0x80, 'shots']];

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
const clipSize = b => b.maxs.map((v, i) => Math.round(v - b.mins[i])).join(' x ');
const clipStops = b => CLIP_STOPS.filter(s => b.contents & s[0]).map(s => s[1]).join(', ');

// The clip under a point of the screen: the first one the line of sight goes into. A brush is the space behind
// all of its planes. Not the one the camera is in.
function clipAt(x, y) {
	const ray = screenRay(x, y), o = ray.origin, d = ray.dir;
	let best = -1, bestT = Infinity;
	for (const b of clipBrushes()) {
		let t0 = -Infinity, t1 = Infinity;
		for (const [nx, ny, nz, dist] of b.planes) {
			const along = nx * d[0] + ny * d[1] + nz * d[2], front = nx * o[0] + ny * o[1] + nz * o[2] - dist;
			if (Math.abs(along) < 1e-7) { if (front > 0) { t0 = Infinity; break; } continue; }
			if (along < 0) t0 = Math.max(t0, -front / along); else t1 = Math.min(t1, -front / along);
			if (t0 > t1) break;
		}
		if (t0 <= t1 && t0 > 0 && t0 < bestT) { bestT = t0; best = b.n; }
	}
	return best;
}
// a click on the map in the Clips tab (js/input.js)
function clipClick(x, y) {
	clip.selected = clipAt(x, y);
	clipRender();
}

// what the server has off, with every poll of the entities (js/editor-entities.js)
function clipTake(list) {
	clip.known = Array.isArray(list);
	const key = clip.known ? list.join(' ') : '';
	if (key === clip.key) return;
	clip.key = key;
	clip.off = new Set(clip.known ? list : []);
	clip.offLines = null;
	if (mode === 'clips') clipRender();
}

function clipRender() {
	const all = clipBrushes(), sel = clipOf(clip.selected), isOff = !!sel && clip.off.has(sel.n);
	const walls = [...ents.values()].filter(e => !e.draft && BOX_WALL.test(e.classname));
	$('clipCount').textContent = all.length ? all.length + (clip.off.size ? ', ' + clip.off.size + ' off' : '') : '';
	$('clipToggle').disabled = $('clipToWall').disabled = !sel || !clip.known;
	$('clipToggle').textContent = isOff ? 'Turn on' : 'Turn off';
	$('clipAllOn').disabled = !clip.off.size;
	$('clipState').textContent = !world ? '' : !clip.known ? 'This game module can not turn clips off.' : !all.length ? 'This map has no clips.' :
		sel ? 'Clip ' + sel.n + (isOff ? ', off' : '') + ': ' + clipSize(sel) + ', stops ' + clipStops(sel) + (sel.box ? '' : ', not a box') : 'Click a clip on the map.';
	const row = (attr, sel, name, what, act) => '<tr ' + attr + (sel ? ' class="sel"' : '') + '><td>' + name + '</td><td>' + what + '</td><td class="acts">' + act + '</td></tr>';
	$('clipRows').innerHTML = [...clip.off].sort((a, b) => a - b).map(n => {
		const b = clipOf(n);
		return row('data-clip="' + n + '"', n === clip.selected, 'Clip ' + n, b ? 'off, ' + clipSize(b) : 'off', '<button data-on="' + n + '">Turn on</button>');
	}).concat(walls.map(e => {
		const tb = triggerBox(e);
		return row('data-wall="' + e.id + '"', false, 'Wall #' + e.id, tb ? tb.maxs.map((v, i) => Math.round(v - tb.mins[i])).join(' x ') : '', '');
	})).join('') || '<tr class="off"><td class="hint">No clip is off and no blocking wall stands.</td></tr>';
}

// The server turns them off or on in its next frame, the list of the entities tells of it then.
async function clipSet(numbers, on) {
	try {
		const res = await fetch('api/clip', { method: 'POST', body: new URLSearchParams({ brush: numbers.join(' '), on: on ? 1 : 0 }) });
		const json = res.status === 404 ? { ok: false, message: 'This game module can not turn clips off.' } : await res.json();
		toast(json.message, json.ok);
		if (json.ok) setTimeout(pollEntities, 200);
		return json.ok;
	} catch (e) {
		toast('Request failed: ' + e.message, false);
		return false;
	}
}

// Drawn with the clips of the map (js/editor-lines.js): the ones that are off in red, the selected one in yellow,
// both through the walls.
function drawClipMarks() {
	if (!clip.offLines) {
		const parts = [...clip.off].map(clipOf).filter(Boolean).map(b => b.edges);
		clip.offLines = new Float32Array(parts.reduce((n, e) => n + e.length, 0));
		parts.reduce((at, e) => { clip.offLines.set(e, at); return at + e.length; }, 0);
	}
	const sel = mode === 'clips' ? clipOf(clip.selected) : null;
	if (!clip.offLines.length && !sel) return;
	gl.bindVertexArray(linkLines.vao);
	gl.bindBuffer(gl.ARRAY_BUFFER, linkLines.buf);
	gl.disable(gl.DEPTH_TEST);
	for (const [data, c] of [[clip.offLines, [1, 0.2, 0.2, 1]], [sel ? sel.edges : null, [1, 0.9, 0.2, 1]]]) {
		if (!data || !data.length) continue;
		gl.bufferData(gl.ARRAY_BUFFER, data, gl.DYNAMIC_DRAW);
		gl.uniform4fv(lineProg.u.uColor, c);
		gl.drawArrays(gl.LINES, 0, data.length / 3);
	}
	gl.enable(gl.DEPTH_TEST);
}

$('clipToggle').addEventListener('click', () => {
	const b = clipOf(clip.selected);
	if (b) clipSet([b.n], clip.off.has(b.n));
});
$('clipAllOn').addEventListener('click', () => { if (clip.off.size) clipSet([...clip.off], true); });
// Move it: a blocking wall of its size where it is, then the clip is off. The wall first: the server refuses one
// that is too large, and the clip stays as it is then. The sizes of a wall are even numbers.
$('clipToWall').addEventListener('click', async () => {
	const b = clipOf(clip.selected);
	if (!b) return;
	const size = b.maxs.map((v, i) => Math.max(2, Math.ceil((v - b.mins[i]) / 2) * 2)), mid = b.mins.map((v, i) => Math.round((v + b.maxs[i]) * 5) / 10);
	const keys = [['classname', 'InvisibleBlockingWall'], ['origin', mid.join(' ')], ['mins', size.map(v => -v / 2).join(' ')], ['maxs', size.map(v => v / 2).join(' ')]];
	const json = await entityOp({ op: 'spawn', kv: keysText(keys) }, 'A wall stands where the clip was' + (b.box ? '' : ' (the box around it: the clip is no box)') + '. Drag it, or its yellow handles.');
	if (!json) return;
	if (!clip.off.has(b.n)) await clipSet([b.n], false);
	setMode('world');
	await pollEntities();
	selectEnt(json.id);
	focusEntity(json.id);
});
$('clipNew').addEventListener('click', () => { setMode('world'); choose('InvisibleBlockingWall'); });
$('clipSave').addEventListener('click', () => layoutOp('save'));
$('clipRows').addEventListener('click', e => {
	const on = e.target.closest('[data-on]'), row = e.target.closest('tr');
	if (on) { clipSet([+on.dataset.on], true); return; }
	if (row && row.dataset.wall) { const id = +row.dataset.wall; setMode('world'); selectEnt(id); focusEntity(id); return; }
	if (!row || !row.dataset.clip) return;
	// to the clip: the camera looks at its middle
	const b = clipOf(+row.dataset.clip);
	clip.selected = b ? b.n : -1;
	if (b) {
		const mid = b.mins.map((v, i) => (v + b.maxs[i]) / 2);
		cam.follow = -1;
		cam.target = mid;
		cam.dist = Math.min(cam.dist, 900);
		if (cam.fly) { const d = camDir(); cam.pos = mid.map((v, i) => v - d[i] * 400); }
	}
	clipRender();
});
