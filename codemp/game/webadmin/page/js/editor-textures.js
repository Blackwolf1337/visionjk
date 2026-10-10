// ---- the textures of the game to choose from, and a test object that is drawn with one
//
// For the Textures tab (js/editor-remaps.js). What a shader of the map is drawn as does not have to be typed:
// the textures are shown as pictures below the map view, a folder of them at a time or all of them, narrowed by
// what is typed into the search, a batch of them at a time. A click on one draws the selected shader of the tab
// as it, as a draft (remapDraft).
//
// The pictures are one image each. The test object in the pane is the shader itself: a ball or a cube drawn
// with every stage of its script, moving if it moves, by the code that draws the models of the map.

// folders: the folder -> the names of its images, without their endings. queue: the folders that are not read
// yet. listed: the server tells the images (a game module from before this one does not). entries: every
// texture there is to choose from (texEntries), made for entriesOf. sets: the folders below textures/ that have
// something, each with how many. set: the one that is shown, '' for everything, '*map' for the shaders of the
// map. find: what is typed into the search. shown: how many of what matches are in the page.
const tex = { open: false, asked: false, folders: new Map(), queue: [], busy: 0, version: 0, listed: true, entries: null, entriesOf: '', sets: [], set: '', find: '', shown: 0, later: 0, typed: 0 };
const TEX_BATCH = 150;

// ---- what there is

// The folders below textures/, one after the other and two at a time: the server looks through all the files
// of the game for each of them.
function texLoad() {
	if (tex.asked) return;
	tex.asked = true;
	tex.queue.push('textures');
	texPump();
}
function texPump() {
	while (tex.busy < 2 && tex.queue.length) {
		const dir = tex.queue.shift();
		tex.busy++;
		fetch('api/list?kind=dir&dir=' + encodeURIComponent(dir) + '&ext=' + encodeURIComponent('.jpg,.png,.tga')).then(res => res.json()).then(json => {
			const names = new Map();
			for (const n of json.names || []) {
				if (n.endsWith('/')) tex.queue.push(n.slice(0, -1));
				else names.set(scKey(n), n.replace(/\.(tga|jpg|png)$/i, ''));
			}
			tex.folders.set(dir.toLowerCase(), [...names.values()]);
			if (dir === 'textures' && !(json.names || []).length) tex.listed = false;
		}, () => { /* a folder the server did not tell: what has a script in it is there all the same */ }).finally(() => {
			tex.busy--;
			tex.version++;
			texLater();
			texPump();
		});
	}
}
const texReading = () => tex.busy > 0 || tex.queue.length > 0;

// Every texture there is to choose from: the images of the folders that are read so far, and the shaders that
// have a script. { name, key, set: its folder, script: it has a script }, the ones below textures/ first.
function texEntries() {
	const stamp = tex.version + '|' + scripts.state + '|' + scripts.bodies.size;
	if (tex.entries && tex.entriesOf === stamp) return tex.entries;
	const all = new Map(), below = e => e.key.startsWith('textures/') ? 0 : 1, sets = new Map();
	for (const names of tex.folders.values()) for (const name of names) all.set(scKey(name), { name, key: scKey(name), script: false });
	for (const key of scripts.bodies.keys()) {
		const e = all.get(key);
		if (e) e.script = true; else all.set(key, { name: key, key, script: true });
	}
	tex.entries = [...all.values()].sort((a, b) => below(a) - below(b) || (a.key < b.key ? -1 : a.key > b.key ? 1 : 0));
	for (const e of tex.entries) {
		e.set = e.key.includes('/') ? e.key.slice(0, e.key.lastIndexOf('/')) : '';
		if (!below(e)) sets.set(e.set, (sets.get(e.set) || 0) + 1);
	}
	tex.sets = [...sets].sort((a, b) => a[0] < b[0] ? -1 : 1);
	tex.entriesOf = stamp;
	return tex.entries;
}

// what the folder that is shown has, with every word of the search in its name
function texMatches() {
	const words = tex.find.toLowerCase().split(/\s+/).filter(Boolean);
	// the shaders of the map are there whether the server tells the images or not
	const from = tex.set === '*map' ? [...remapUses().keys()].sort().map(name => ({ name, key: scKey(name), script: scripts.bodies.has(scKey(name)) }))
		: tex.set ? texEntries().filter(e => e.set === tex.set) : texEntries();
	return words.length ? from.filter(e => words.every(w => e.key.includes(w))) : from;
}

// ---- the list below the map view

// the folders to choose from. Not while the list of them is open: it would close.
function texSetOptions() {
	const sel = $('texSet');
	if (document.activeElement === sel && sel.options.length) return;
	const key = tex.entriesOf + '|' + (world ? remapUses().size : 0);
	if (sel.dataset.key === key) return;
	sel.dataset.key = key;
	sel.innerHTML = '<option value="">Everything (' + tex.entries.length + ')</option><option value="*map">Of this map (' + (world ? remapUses().size : 0) + ')</option>' +
		tex.sets.map(([set, n]) => '<option value="' + escapeHtml(set) + '">' + escapeHtml(set.replace(/^textures\//, '')) + ' (' + n + ')</option>').join('');
	sel.value = tex.set;
	// a folder that is not there (any more): everything
	if (sel.value !== tex.set) sel.value = tex.set = '';
}

function texRender() {
	if (!document.body.classList.contains('texOpen')) return;
	texEntries();
	texSetOptions();
	const list = texMatches(), shown = list.slice(0, tex.shown), left = list.length - shown.length, chosen = scKey($('remapNew').value.trim());
	$('texCount').textContent = list.length + (list.length === 1 ? ' texture' : ' textures') + (texReading() ? ', reading the folders...' : '') +
		(tex.listed ? '' : '. This game module does not tell the images: only what has a shader script is here.');
	$('texTiles').innerHTML = shown.map(e => '<div class="tile' + (e.key === chosen ? ' sel' : '') + (e.script ? ' script' : '') + '" data-name="' + escapeHtml(e.name) + '" title="' + escapeHtml(e.name) +
		(e.script ? ' (has a shader script)' : '') + '"><i class="thumb" data-thumb="' + escapeHtml(e.name) + '"></i><span>' + escapeHtml(e.name.slice(e.name.lastIndexOf('/') + 1)) + '</span></div>').join('') +
		(left > 0 ? '<button id="texMore" title="The next batch">' + Math.min(TEX_BATCH, left) + ' more of ' + left + '</button>' : '') ||
		'<div class="hint">' + (texReading() ? 'Reading the folders of the textures...' : 'No texture has that in its name.') + '</div>';
	thumbWatch($('texTiles'));
}
// again in a moment: for the folders as they come in
function texLater() {
	if (!tex.later) tex.later = setTimeout(() => { tex.later = 0; texRender(); }, 400);
}
// the one that is chosen in the tab has a frame
function texChosen() {
	const chosen = scKey($('remapNew').value.trim());
	for (const tile of $('texTiles').querySelectorAll('.tile')) tile.classList.toggle('sel', scKey(tile.dataset.name) === chosen);
}

// The list is there in the Textures tab only, and the map view makes room for it (setMode in js/editor-world.js).
function texSync() {
	const on = tex.open && mode === 'remaps';
	if (document.body.classList.contains('texOpen') === on) return;
	document.body.classList.toggle('texOpen', on);
	// the map view is another size now
	resize();
	if (on) texRender();
}
function texOpen(on) {
	tex.open = on;
	$('remapBrowse').textContent = on ? 'Close the textures' : 'Pick a texture';
	if (on) { texLoad(); tex.shown = tex.shown || TEX_BATCH; }
	texSync();
	if (on) $('texFind').focus();
}

$('remapBrowse').addEventListener('click', () => texOpen(!tex.open));
$('texClose').addEventListener('click', () => texOpen(false));
$('texFind').addEventListener('input', () => {
	tex.find = $('texFind').value.trim();
	tex.shown = TEX_BATCH;
	clearTimeout(tex.typed);
	tex.typed = setTimeout(() => { $('texTiles').scrollTop = 0; texRender(); }, 150);
});
$('texSet').addEventListener('change', () => {
	tex.set = $('texSet').value;
	tex.shown = TEX_BATCH;
	$('texTiles').scrollTop = 0;
	texRender();
});
$('texTiles').addEventListener('click', e => {
	if (e.target.id === 'texMore') { tex.shown += TEX_BATCH; texRender(); return; }
	const tile = e.target.closest('.tile');
	if (!tile) return;
	// the selected shader of the tab is drawn as it in this page: a draft, which Swap sends
	remapDraft(tile.dataset.name);
});

// ---- the test object

const PREVIEW_PX = 512, PREVIEW_MSEC = 60;
const PREVIEW_PLACE = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
// the light of a place of a map that has no light grid
const PREVIEW_LIGHT = { ambient: new Array(3).fill(LIGHT_NO_GRID.ambient), directed: new Array(3).fill(LIGHT_NO_GRID.directed), dir: LIGHT_NO_GRID.dir };
// shape: 'ball' or 'cube'. meshes: each made once. at: when it was drawn last. failed: it threw once and is
// left alone since, so that the frame of the map that comes after it is drawn all the same.
const preview = { shape: 'ball', meshes: {}, at: 0, g: null, failed: false };

// A ball or a cube as a model is: positions, normals, texture coordinates, and triangles whose corners run
// clockwise seen from outside, which is the front of a triangle of the game. The image lies once on each face
// of the cube, upright on the four sides, and twice around the ball.
function previewMesh(shape) {
	if (preview.meshes[shape]) return preview.meshes[shape];
	const pos = [], norm = [], uv = [], idx = [];
	if (shape === 'cube') {
		const half = 19, cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
		for (const n of [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]]) {
			// seen from outside: across to the right, up
			const up = n[2] ? [0, 1, 0] : [0, 0, 1], across = cross(up, n), base = pos.length / 3;
			// the corners at the lower left, the lower right, the upper right, the upper left
			for (const [a, u] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
				pos.push(...n.map((v, i) => (v + across[i] * a + up[i] * u) * half));
				norm.push(...n);
				uv.push((a + 1) / 2, (1 - u) / 2);
			}
			idx.push(base + 3, base + 2, base + 1, base + 3, base + 1, base);
		}
	} else {
		const radius = 24, rows = 24, cols = 48;
		for (let r = 0; r <= rows; r++) {
			const down = r / rows * Math.PI;
			for (let c = 0; c <= cols; c++) {
				const around = c / cols * Math.PI * 2, n = [Math.sin(down) * Math.cos(around), Math.sin(down) * Math.sin(around), Math.cos(down)];
				pos.push(n[0] * radius, n[1] * radius, n[2] * radius);
				norm.push(...n);
				uv.push(c / cols * 2, r / rows);
			}
		}
		for (let r = 0; r < rows; r++) {
			for (let c = 0; c < cols; c++) {
				// seen from outside: the upper left, the upper right, the lower left, the lower right
				const a = r * (cols + 1) + c, b = a + 1, d = a + cols + 1, e = d + 1;
				idx.push(a, b, e, a, e, d);
			}
		}
	}
	return preview.meshes[shape] = { vao: modelVao(new Float32Array(pos), new Float32Array(norm), new Float32Array(uv), new Uint32Array(idx)), count: idx.length };
}

// Drawn before the frame of the map (js/frame.js), into a corner of the canvas of the map, which the frame then
// draws over, and copied from there into the pane: so it is the code and the graphics context of the map that
// draw it. The camera goes around it, the light stays where it is. A few times a second are enough.
function drawRemapPreview() {
	// as many pixels as the pane shows it with
	const cv = $('remapBall'), size = Math.min(Math.round(cv.clientWidth * canvasScale()), PREVIEW_PX, canvas.width, canvas.height);
	if (preview.failed || mode !== 'remaps' || !world || scripts.state !== 2 || !remap.shown || size < 16 || Math.abs(frameNow - preview.at) < PREVIEW_MSEC) return;
	preview.at = frameNow;
	const mat = materialFor(remap.shown, 'model'), t = frameNow / 1000, turn = t * 0.5, wasView = viewProj, wasEye = eye;
	gl.viewport(0, 0, size, size);
	gl.enable(gl.SCISSOR_TEST);
	gl.scissor(0, 0, size, size);
	gl.clearColor(0.09, 0.09, 0.11, 1);
	gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
	gl.enable(gl.DEPTH_TEST);
	try {
		if (!mat.nodraw) {
			eye = [Math.cos(turn) * 115, Math.sin(turn) * 115, 35];
			viewProj = M4.mul(M4.perspective(0.6, 1, 4, 600), M4.lookAt(eye, [0, 0, 0], [0, 0, 1]));
			modelPassBegin();
			modelPlace(PREVIEW_PLACE, PREVIEW_LIGHT);
			drawModelMesh(previewMesh(preview.shape), mat, t, null);
		}
		if (cv.width !== size) cv.width = cv.height = size;
		// the corner is the lower left one of the canvas, whose rows are counted from the top
		(preview.g || (preview.g = cv.getContext('2d'))).drawImage(canvas, 0, canvas.height - size, size, size, 0, 0, size, size);
	} catch (e) {
		preview.failed = true;
		console.error('the test object of the Textures tab failed: ' + String(e && e.stack || e));
	}
	// what the frame of the map expects, also after a failure
	modelPassEnd();
	viewProj = wasView;
	eye = wasEye;
	gl.disable(gl.SCISSOR_TEST);
	gl.disable(gl.POLYGON_OFFSET_FILL);
	gl.bindVertexArray(null);
}

$('remapShape').addEventListener('click', () => {
	preview.shape = preview.shape === 'ball' ? 'cube' : 'ball';
	preview.at = 0;
	$('remapShape').textContent = preview.shape === 'ball' ? 'Ball' : 'Cube';
});
