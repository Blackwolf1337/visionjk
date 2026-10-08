// ---- the report of a map: what its file holds, what stands on it, and what is missing

let mapReportTimer = 0;

// ---- what a surface is made of: a click on the map in the Map tab

const SURF_FLAGS = { 0x2000: 'sky', 0x4000: 'slick', 0x8000: 'metal steps', 0x10000: 'force field', 0x40000: 'no damage', 0x80000: 'no impact', 0x100000: 'no marks',
	0x200000: 'nodraw', 0x400000: 'no steps', 0x800000: 'no dynamic light', 0x1000000: 'no misc_model on it', 0x2000000: 'only with force sight' };
const CONTENT_FLAGS = { 0x1: 'solid', 0x2: 'lava', 0x4: 'water', 0x8: 'fog', 0x10: 'player clip', 0x20: 'monster clip', 0x40: 'bot clip', 0x80: 'shot clip', 0x100: 'body', 0x200: 'corpse',
	0x400: 'trigger', 0x800: 'no drop', 0x1000: 'terrain', 0x2000: 'ladder', 0x4000: 'abseil', 0x8000: 'opaque', 0x10000: 'outside', 0x20000: 'slime', 0x40000: 'light saber',
	0x80000: 'teleporter', 0x100000: 'item', 0x200000: 'no shot', 0x8000000: 'detail', 0x10000000: 'inside', 0x80000000: 'translucent' };
const MATERIALS = ['none', 'solid wood', 'hollow wood', 'solid metal', 'hollow metal', 'short grass', 'long grass', 'dirt', 'sand', 'gravel', 'glass', 'concrete', 'marble', 'water',
	'snow', 'ice', 'flesh', 'mud', 'bulletproof glass', 'dry leaves', 'green leaves', 'fabric', 'canvas', 'rock', 'rubber', 'plastic', 'tiles', 'carpet', 'plaster', 'shatter glass',
	'armor', 'computer'];
const SORT_NAMES = Object.fromEntries(Object.entries(SC_SORT).map(([name, n]) => [n, name]));
const flagNames = (bits, names) => Object.keys(names).filter(bit => (bits >>> 0) & +bit).map(bit => names[bit]);

let surfShown = null;		// { surface, wire: the lines of its triangles }

function inspectSurface(x, y) {
	const ray = screenRay(x, y), hit = pickGround(ray.origin, ray.dir);
	if (surfShown && surfShown.wire) { gl.deleteVertexArray(surfShown.wire.vao); gl.deleteBuffer(surfShown.wire.buf); }
	surfShown = hit && pickTri >= 0 ? { surface: world.pick.surf[pickTri], at: hit, wire: null } : null;
	renderSurface();
}

function renderSurface() {
	const box = $('surfInfo');
	// the one of another map
	if (surfShown && (!world || !world.surfs[surfShown.surface])) surfShown = null;
	if (!surfShown) {
		box.innerHTML = '<h3>A surface</h3><div class="hint">Click on the map to see what a surface is made of: its shader and the script of it, what it is for the game, its lightmap.</div>';
		return;
	}
	const sf = world.surfs[surfShown.surface], sh = world.shaders[sf.shader], lit = sf.lm >= 0 && sf.lm < world.stats.lightmaps;
	const body = scripts.bodies.get(scKey(sh.name)), mat = scripts.state === 2 ? materialFor(sh.name, lit) : null;
	const row = (name, value) => '<tr><td>' + name + '</td><td class="num">' + value + '</td></tr>';
	const kind = sf.type === MST_PATCH ? 'a patch of ' + sf.patch[0] + ' x ' + sf.patch[1] + ' points' : sf.type === MST_TRIANGLE_SOUP ? 'triangles (a model of the map)' : 'a face of a brush';
	let owner = 'the world';
	if (sf.model) {
		const ent = [...ents.values()].find(e => entKey(e, 'model') === '*' + sf.model);
		owner = '*' + sf.model + (ent ? ', ' + escapeHtml(ent.classname) + ' #' + ent.id : '') + (movers && !movers.has(sf.model) ? ', not drawn by the game' : '');
	}
	const fog = sf.fog >= 0 && world.fogs[sf.fog] ? escapeHtml(world.fogs[sf.fog].shader) : 'none';
	const game = [MATERIALS[sh.flags & 31] !== 'none' ? MATERIALS[sh.flags & 31] : null, ...flagNames(sh.flags, SURF_FLAGS)].filter(Boolean);
	let drawn = '';
	if (mat) {
		drawn = row('Stages', mat.stages.length + (mat.nodraw ? ', not drawn' : '')) +
			row('Sort', (SORT_NAMES[mat.sort] || mat.sort) + (mat.sort > SC_SORT_BANNER ? ', see-through' : '')) +
			row('Side', ['the front', 'the back', 'both'][mat.cull]) +
			(mat.deforms.length ? row('Moves', mat.deforms.map(d => d.kind).join(', ')) : '') +
			(mat.polygonOffset ? row('Lies on', 'another surface (polygonOffset)') : '');
	}
	box.innerHTML = '<h3>A surface</h3><table>' + row('Shader', escapeHtml(sh.name)) + row('Kind', kind + ', ' + sf.tris + ' triangles') +
		row('Belongs to', owner) + row('Lightmap', lit ? '#' + sf.lm : 'none, lit by its vertices') + row('Fog', fog) +
		row('For the game', game.length ? game.join(', ') : 'nothing special') + row('Contents', flagNames(sh.contents, CONTENT_FLAGS).join(', ') || 'none') +
		row('Where', surfShown.at.map(Math.round).join(' ')) + drawn + '</table>' +
		(body !== undefined ? '<pre>' + escapeHtml(sh.name + '\n{' + body + '}') + '</pre>'
			: '<div class="hint">No script: the image of that name ' + (lit ? 'times the lightmap' : 'times the light of its vertices') + ', as the engine makes it.</div>');
}

// the triangles of the surface that is shown, as lines over the map
function drawSurfaceWire(now) {
	const pk = world.pick, sf = world.surfs[surfShown.surface];
	if (!sf) return;
	if (!surfShown.wire) {
		const pts = [];
		for (let t = 0; t < pk.surf.length; t++) {
			if (pk.surf[t] !== surfShown.surface) continue;
			for (const [a, b] of [[0, 1], [1, 2], [2, 0]]) for (const v of [pk.idx[t * 3 + a], pk.idx[t * 3 + b]]) pts.push(pk.pos[v * 3], pk.pos[v * 3 + 1], pk.pos[v * 3 + 2]);
		}
		const vao = gl.createVertexArray(), buf = gl.createBuffer();
		gl.bindVertexArray(vao);
		gl.bindBuffer(gl.ARRAY_BUFFER, buf);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(pts), gl.STATIC_DRAW);
		gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
		gl.bindVertexArray(null);
		surfShown.wire = { vao, buf, count: pts.length / 3 };
	}
	gl.useProgram(lineProg.p);
	gl.uniform4f(lineProg.u.uColor, 0.2, 1, 1, 1);
	gl.disable(gl.DEPTH_TEST);
	gl.bindVertexArray(surfShown.wire.vao);
	// a brush model that is an entity: where that is
	for (const place of moverPlaces(sf.model, now)) {
		gl.uniformMatrix4fv(lineProg.u.uMvp, false, place ? M4.mul(viewProj, place) : viewProj);
		gl.drawArrays(gl.LINES, 0, surfShown.wire.count);
	}
	gl.enable(gl.DEPTH_TEST);
	gl.useProgram(flatProg.p);
}

// ---- entities that look wrong, for the report of the map

// Classes that only do something when they are fired: with a name that nothing fires they never do. Not the ones
// that are aimed at or looked up by their name (a position, a place on the map), and not what plays by itself.
const NEEDS_FIRING = /^(func_door|func_usable|target_(?!position|location|speaker|push$))/;

// bad: this does not work as it is. maybe: it may be meant, or a script does what is missing.
function entityProblems() {
	const out = [], named = new Map(), fired = new Set(), b = world.bounds;
	for (const e of ents.values()) {
		const name = entKey(e, 'targetname').toLowerCase();
		if (name) named.set(name, (named.get(name) || 0) + 1);
	}
	for (const e of ents.values()) {
		for (const [k, v] of e.keys || []) {
			if (!v || !LINK_KEYS.has(k.toLowerCase())) continue;
			fired.add(v.toLowerCase());
			if (!named.has(v.toLowerCase())) out.push({ id: e.id, bad: true, text: e.classname + ': its ' + k + ' "' + v + '" names no entity' });
		}
	}
	for (const e of ents.values()) {
		const name = entKey(e, 'targetname');
		if (name && NEEDS_FIRING.test(e.classname) && !fired.has(name.toLowerCase())) {
			out.push({ id: e.id, bad: false, text: e.classname + ' "' + name + '": nothing fires it' + (e.classname === 'func_door' ? ', so it never opens' : '') });
		}
		if (e.pos.some((v, i) => v < b.mins[i] - 64 || v > b.maxs[i] + 64)) out.push({ id: e.id, bad: true, text: e.classname + ': outside of the map, at ' + e.pos.map(Math.round).join(' ') });
		if (e.bmodel && !/^\*\d+$/.test(entKey(e, 'model'))) out.push({ id: e.id, bad: false, text: e.classname + ': made of brushes, but its keys name no brush model' });
	}
	// what the game type needs to be there at all
	const count = cls => [...ents.values()].filter(e => e.classname === cls).length, general = [];
	if (!count('info_player_deathmatch')) general.push({ bad: true, text: 'No info_player_deathmatch: players have nowhere to start' });
	if (state && (state.gametype === 8 || state.gametype === 9)) {
		for (const cls of ['team_CTF_redflag', 'team_CTF_blueflag']) if (!count(cls)) general.push({ bad: true, text: 'Capture the flag without a ' + cls });
		for (const cls of ['team_CTF_redspawn', 'team_CTF_bluespawn']) if (!count(cls)) general.push({ bad: false, text: 'No ' + cls + ': that team starts at the points for everybody' });
	}
	if (state && (state.gametype === 3 || state.gametype === 4) && !count('info_player_duel') && !count('info_player_duel1')) {
		general.push({ bad: false, text: 'A duel without info_player_duel: the two start at the points for everybody' });
	}
	return general.concat(out.sort((x, y) => (y.bad ? 1 : 0) - (x.bad ? 1 : 0) || x.id - y.id));
}

function entityProblemsHtml() {
	if (!ents.size) return '';
	const list = entityProblems(), shown = list.slice(0, 60);
	return '<h3>Entities to look at' + (list.length ? ', ' + list.length : '') + '</h3>' +
		(list.length ? '' : '<div>Nothing: every target names an entity, and nothing that waits to be fired waits in vain.</div>') +
		shown.map(p => '<div class="look ' + (p.bad ? 'bad' : 'maybe') + '">' + (p.id !== undefined ? '<a data-ent="' + p.id + '" title="Show it in the World tab">#' + p.id + '</a> ' : '') + escapeHtml(p.text) + '</div>').join('') +
		(list.length > shown.length ? '<div class="hint">and ' + (list.length - shown.length) + ' more</div>' : '') +
		(list.some(p => !p.bad) ? '<div class="hint">Yellow: it may be meant that way, or a script does what seems to be missing.</div>' : '');
}

function mapReport() {
	clearInterval(mapReportTimer);
	if (!world) { $('mapReport').textContent = 'No map is loaded.'; return; }
	// every image the map uses is asked for, so that the ones that are not there show. The plain view only
	// asks for the first one of a shader.
	if (scripts.state === 2) {
		for (const b of world.batches) {
			for (const st of materialFor(b.shaderName, !!b.lm).stages) if (!st.images) st.images = st.maps.map(name => gameImage(name, st.clamp));
		}
	}
	renderMapReport();
	// and again while they come in
	mapReportTimer = setInterval(() => {
		if (mode !== 'mapinfo' || !world) { clearInterval(mapReportTimer); return; }
		renderMapReport();
		if (!imageBusy && !imageQueue.length && scripts.state >= 2) clearInterval(mapReportTimer);
	}, 800);
}

function renderMapReport() {
	const w = world, st = w.stats, scripted = scripts.state === 2;
	const used = [...new Set(w.batches.map(b => b.shaderName))];
	// a shader of a surface that is neither a script nor an image
	const noImage = used.filter(n => w.textures.get(n).failed && !(scripted && scripts.bodies.has(scKey(n))));
	const noStage = new Set();
	let loading = imageBusy + imageQueue.length;
	if (scripted) {
		for (const b of w.batches) {
			for (const stage of materialFor(b.shaderName, !!b.lm).stages) {
				(stage.images || []).forEach((h, i) => { if (h.failed) noStage.add(stage.maps[i] + '  (' + b.shaderName + ')'); });
			}
		}
	}
	const byClass = new Map();
	for (const e of ents.values()) byClass.set(e.classname, (byClass.get(e.classname) || 0) + 1);
	const size = w.bounds.maxs.map((v, i) => Math.round(v - w.bounds.mins[i]));
	const row = (name, value) => '<tr><td>' + name + '</td><td class="num">' + value + '</td></tr>';
	const n = v => Number(v).toLocaleString('en-US');
	$('mapReportName').textContent = loadedMap;
	$('mapReport').innerHTML =
		// what is wrong comes first, then what the file holds
		entityProblemsHtml() +
		'<h3>Missing' + (loading ? ' (' + loading + ' images on their way)' : '') + '</h3>' +
		(noImage.length || noStage.size ? '' : '<div>' + (scripted ? 'Nothing: every image the map names is there.' : 'The shader scripts are not read yet.') + '</div>') +
		noImage.map(name => '<div class="bad" title="A surface names this shader, and it is neither a script nor an image">' + escapeHtml(name) + '</div>').join('') +
		[...noStage].sort().map(name => '<div class="bad" title="An image a stage of a shader names">' + escapeHtml(name) + '</div>').join('') +
		'<h3>The file</h3><table>' + row('Size', n(Math.round(st.bytes / 1024)) + ' KB') + row('Extent', size.join(' x ')) + row('Surfaces', n(st.surfaces)) +
		row('Triangles drawn', n(w.batches.reduce((sum, b) => sum + b.count / 3, 0))) + row('Vertices', n(st.vertices)) + row('Shaders', n(st.shaders) + ', ' + used.length + ' drawn') +
		row('Lightmaps', n(st.lightmaps)) + row('Brushes', n(st.brushes)) + row('Planes', n(st.planes)) + row('Brush models', n(st.brushModels - 1)) +
		row('Sky', escapeHtml(w.sky || 'none')) + '</table>' +
		'<h3>Entities, ' + ents.size + '</h3><table>' + [...byClass].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0])).map(([cls, count]) =>
			'<tr><td><a data-cls="' + escapeHtml(cls) + '" title="Show them in the World tab">' + escapeHtml(cls) + '</a></td><td class="num">' + count + '</td></tr>').join('') + '</table>';
}

$('mapReportAgain').addEventListener('click', mapReport);
$('mapReport').addEventListener('click', e => {
	// an entity to look at: it is selected in the World tab and the camera goes there
	const one = e.target.closest('a[data-ent]');
	if (one && ents.has(+one.dataset.ent)) {
		$('entFilter').value = '';
		setMode('world');
		selectEnt(+one.dataset.ent);
		focusEntity(+one.dataset.ent);
		return;
	}
	const a = e.target.closest('a[data-cls]');
	if (!a) return;
	$('entFilter').value = a.dataset.cls;
	setMode('world');
	renderEntityList();
});

