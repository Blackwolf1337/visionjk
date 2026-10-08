/* ------------------------------------------------------------------ */
/* the files of the game folder                                        */
/* ------------------------------------------------------------------ */

// what a click opens in the text field instead of downloading it
const FS_TEXT = new Set(['cfg', 'txt', 'log', 'bak', 'ent', 'json', 'md', 'ini', 'csv', 'dat', 'shader', 'skin', 'npc', 'sab', 'veh', 'vwp', 'bot', 'arena', 'menu', 'str', 'efx', 'sp']);
// a request is 64 KB at most on the server, head and body
const FS_CHUNK = 48 * 1024, FS_EDIT_MAX = 60000;
let fsRoot = 'home', fsPath = '', fsWritable = true, fsEntries = [], fsEdit = null;

function fsExt(name) { const i = name.lastIndexOf('.'); return i < 0 ? '' : name.slice(i + 1).toLowerCase(); }
function fsJoin(dir, name) { return dir ? dir + '/' + name : name; }
function fsSize(n) { return n < 1024 ? n + ' B' : n < 1048576 ? (n / 1024).toFixed(1) + ' KB' : (n / 1048576).toFixed(1) + ' MB'; }
function fsUrl(op, params) { return 'api/fs/' + op + '?' + new URLSearchParams(params); }
async function fsPost(op, params, body = '') {
	try {
		return await (await fetch(fsUrl(op, params), { method: 'POST', body })).json();
	} catch (e) {
		return { ok: false, message: 'Request failed: ' + e.message };
	}
}

async function fsList(path = fsPath) {
	try {
		const json = await (await fetch(fsUrl('list', { root: fsRoot, path }), { cache: 'no-store' })).json();
		if (!json.ok) { toast(json.message, false); return false; }
		fsPath = json.path;
		fsWritable = json.writable;
		fsEntries = json.entries.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
		fsRender();
		return true;
	} catch (e) {
		toast('Could not read the folder: ' + e.message, false);
		return false;
	}
}
function fsRender() {
	$('fsPath').textContent = (fsRoot === 'home' ? 'server folder' : 'installation') + ' / ' + fsPath;
	$('fsCount').textContent = fsEntries.length;
	$('fsMkdir').disabled = $('fsUpload').disabled = !fsWritable;
	$('fsUp').disabled = !fsPath;
	$('fsRows').innerHTML = fsEntries.map((e, i) => {
		// a link and a name the server does not take in a request are only shown
		const usable = !e.odd && !e.link;
		const acts = !usable ? '' :
			(e.dir ? '' : '<button data-fs="get" data-i="' + i + '">Download</button>') +
			(fsWritable && !e.code ? '<button data-fs="rename" data-i="' + i + '">Rename</button><button data-fs="delete" data-i="' + i + '">Delete</button>' : '');
		return '<tr' + (usable ? ' data-fs="open" data-i="' + i + '"' : ' class="off"') + '><td>' +
			(e.dir ? '<span class="tag">folder</span> ' : '') + escapeHtml(e.name) +
			(e.link ? ' <span class="tag">link</span>' : '') + (e.code ? ' <span class="tag">program</span>' : '') +
			'</td><td class="num">' + (e.dir || e.link ? '' : fsSize(e.size)) + '</td><td>' + (e.link ? '' : new Date(e.time * 1000).toLocaleString()) +
			'</td><td class="acts">' + acts + '</td></tr>';
	}).join('') || '<tr class="off"><td colspan="4">empty</td></tr>';
}

function fsDownload(path) {
	// the browser sends the login of the panel with it, the server answers with a file to save
	const a = document.createElement('a');
	a.href = fsUrl('get', { root: fsRoot, path });
	a.download = '';
	document.body.appendChild(a);
	a.click();
	a.remove();
}
async function fsRename(e) {
	const to = prompt('New name of ' + e.name + ' (with a/ in front it goes into the folder a)', e.name);
	if (!to || to === e.name) return;
	const r = await fsPost('rename', { path: fsJoin(fsPath, e.name), to: fsJoin(fsPath, to) });
	toast(r.message, r.ok);
	fsList();
}
async function fsDelete(e) {
	if (!confirm('Delete ' + e.name + (e.dir ? ' (it has to be empty)' : '') + '?')) return;
	const r = await fsPost('delete', { path: fsJoin(fsPath, e.name) });
	toast(r.message, r.ok);
	fsList();
}
async function fsMkdir() {
	const name = prompt('Name of the new folder');
	if (!name) return;
	const r = await fsPost('mkdir', { path: fsJoin(fsPath, name) });
	toast(r.message, r.ok);
	fsList();
}
// a file goes up in pieces, one after the other: each names where it begins, the last one puts the file in its place
async function fsUploadFiles(files) {
	const dir = fsPath;
	let pk3 = false;
	for (const file of files) {
		if (fsEntries.some(e => e.name === file.name) && !confirm(file.name + ' is there already. Replace it?')) continue;
		let offset = 0, ok = true;
		do {
			const piece = file.slice(offset, offset + FS_CHUNK), last = offset + piece.size >= file.size;
			$('fsProgress').textContent = file.name + ': ' + fsSize(offset) + ' of ' + fsSize(file.size);
			const r = await fsPost('put', { path: fsJoin(dir, file.name), offset, last: last ? 1 : 0 }, piece);
			if (!r.ok) { toast(file.name + ': ' + r.message, false); ok = false; break; }
			offset += piece.size;
		} while (offset < file.size);
		if (ok) toast(file.name + ' uploaded');
		if (ok && fsExt(file.name) === 'pk3') pk3 = true;
	}
	$('fsProgress').textContent = '';
	fsList();
	if (pk3) fsReloadMap('A pk3 was uploaded. The server only looks for pk3 files when a map starts.\n\n');
}
function fsReloadMap(why = '') {
	if (confirm(why + 'Start the running map again now? The players stay connected, what was placed in the World tab is put back where it is.')) action({ action: 'reload' });
}

async function fsOpenText(path) {
	try {
		const res = await fetch(fsUrl('get', { root: fsRoot, path, text: 1 }), { cache: 'no-store' });
		if (!res.ok) throw new Error(await res.text());
		const text = await res.text();
		// a file of the installation is saved under the same name in the server folder, where the engine looks first
		fsEdit = { path, saved: text, colored: fsExt(path) === 'cfg', copy: fsRoot !== 'home' };
		$('fsText').value = text;
		$('fsText').scrollTop = $('fsText').scrollLeft = 0;
		fsShowEdit();
	} catch (e) {
		toast('Could not read the file: ' + e.message, false);
	}
}
function fsShowEdit() {
	const on = !!fsEdit;
	$('fsEditor').hidden = !on;
	$('fsList').hidden = on;
	$('fsEditTools').hidden = !on;
	$('fsEditState').textContent = '';
	if (!on) return;
	const dirty = $('fsText').value !== fsEdit.saved;
	codePaint('fsText', 'fsHi', fsEdit.colored);
	$('fsEditState').textContent = fsEdit.path + (dirty ? ': changed, not saved' : ': as on the server') +
		(fsEdit.copy ? ' (of the installation, Save writes it into the server folder)' : '');
	$('fsEditState').classList.toggle('dirty', dirty);
}
async function fsSave() {
	const text = $('fsText').value.replace(/\r/g, '');
	if (cfgBytes(text) > FS_EDIT_MAX) { toast('The text is larger than ' + FS_EDIT_MAX + ' bytes, the server does not take it in one piece', false); return; }
	const r = await fsPost('put', { path: fsEdit.path, offset: 0, last: 1, text: 1 }, text);
	toast(r.message, r.ok);
	if (r.ok) fsEdit.saved = text;
	fsShowEdit();
}
function fsCloseEdit() {
	if (fsEdit && $('fsText').value !== fsEdit.saved && !confirm('Drop what was changed in ' + fsEdit.path + '?')) return;
	fsEdit = null;
	fsShowEdit();
	fsList();
}

$('fsRows').addEventListener('click', ev => {
	const el = ev.target.closest('[data-fs]');
	if (!el) return;
	const e = fsEntries[+el.dataset.i], path = fsJoin(fsPath, e.name);
	switch (el.dataset.fs) {
	case 'open':
		if (e.dir) fsList(path);
		else if (FS_TEXT.has(fsExt(e.name)) && e.size <= FS_EDIT_MAX) fsOpenText(path);
		else fsDownload(path);
		break;
	case 'get': fsDownload(path); break;
	case 'rename': fsRename(e); break;
	case 'delete': fsDelete(e); break;
	}
});
$('fsRoot').addEventListener('change', async () => {
	const before = fsRoot;
	fsRoot = $('fsRoot').value;
	if (!await fsList('')) { fsRoot = $('fsRoot').value = before; }
});
$('fsUp').addEventListener('click', () => fsList(fsPath.includes('/') ? fsPath.slice(0, fsPath.lastIndexOf('/')) : ''));
$('fsRefresh').addEventListener('click', () => fsList());
$('fsMkdir').addEventListener('click', fsMkdir);
$('fsUpload').addEventListener('click', () => $('fsFile').click());
$('fsReload').addEventListener('click', () => fsReloadMap());
$('fsFile').addEventListener('change', () => { const files = [...$('fsFile').files]; $('fsFile').value = ''; if (files.length) fsUploadFiles(files); });
$('fsText').addEventListener('input', fsShowEdit);
$('fsText').addEventListener('scroll', () => codeScroll('fsText', 'fsHi'));
$('fsSave').addEventListener('click', fsSave);
$('fsClose').addEventListener('click', fsCloseEdit);
document.querySelectorAll('#tabs button').forEach(b => b.addEventListener('click', () => setMode(b.dataset.tab)));
document.querySelectorAll('#appMode button').forEach(b => b.addEventListener('click', () => setAppMode(b.dataset.app)));

let classesLoaded = false;
async function loadClasses() {
	if (classesLoaded) return;
	try {
		const json = await (await fetch('api/classes')).json();
		allClasses = [...new Set(json.classes)].sort();
		for (const it of json.items) classInfo.set(it.classname, it);
		classesLoaded = true;
		if ($('classPicker').classList.contains('open')) renderPicker();
	} catch (e) { /* retry next time */ }
}

async function pollEntities() {
	if (appMode !== 'editor') return;
	try {
		const json = await (await fetch('api/entities', { cache: 'no-store' })).json();
		ents.clear();
		for (const e of json.entities) ents.set(e.id, e);
		takeFired(json.used);
		renderBudget(json.budget);
		renderLayout(json.layout);
		refreshLinks();
		if (selectedEnt >= 0 && !ents.has(selectedEnt)) selectEnt(-1);
		renderEntityList();
		// not while the cursor is in a key field: a rebuilt table takes the focus away
		if (!kvDirty && !$('kvRows').contains(document.activeElement)) renderEntPanel();
	} catch (e) { /* keep the old list */ }
}

function renderEntityList() {
	const f = $('entFilter').value.trim().toLowerCase();
	const list = [...ents.values()].filter(e => entMatches(e, f));
	$('entCount').textContent = list.length + (list.length !== ents.size ? ' / ' + ents.size : '');
	$('entities').innerHTML = list.slice(0, 600).map(e =>
		'<tr class="player ent' + (e.id === selectedEnt ? ' sel' : '') + (e.keys ? '' : ' ro') + '" data-id="' + e.id + '">' +
		'<td class="num">' + e.id + '</td><td>' + escapeHtml(e.classname) + '</td><td class="tn">' + escapeHtml(entKey(e, 'targetname')) + '</td></tr>').join('');
}
$('entFilter').addEventListener('input', renderEntityList);
$('entities').addEventListener('click', e => {
	const tr = e.target.closest('tr.ent');
	if (!tr) return;
	selectEnt(+tr.dataset.id);
	const ent = ents.get(selectedEnt);
	if (ent) { const far = cam.dist; focusEntity(selectedEnt); cam.dist = far; }
});

function entityAt(x, y) {
	let best = -1, bestD = 16;
	for (const ent of ents.values()) {
		if (!entVisible(ent)) continue;
		const sp = project(ent.pos);
		if (!sp) continue;
		const d = Math.hypot(sp[0] - x, sp[1] - y);
		if (d < bestD) { bestD = d; best = ent.id; }
	}
	return best;
}

function selectEnt(id) {
	selectedEnt = id;
	kvDirty = false;
	ghost = null;
	kvShown = '';
	renderEntPanel();
	if (mode === 'world') renderEntityList();
}

// kvShown is what the key table shows: the entity and its keys. The poll comes every second, the table is only
// built again when this changed, so fields keep focus, cursor and selection (JKAdmin rebuilds it every time).
function renderEntPanel() {
	const ent = selectedEnt >= 0 && ents.get(selectedEnt);
	$('entPanel').classList.toggle('open', !!ent);
	if (!ent) return;
	$('entTitle').textContent = ent.classname;
	$('entSub').textContent = '#' + ent.id + (ent.bmodel ? ' · brush model' : '') + (ent.linked ? '' : ' · not linked') + (picked.size ? ' · and ' + picked.size + ' more' : '');
	const editable = !!ent.keys;
	const shown = ent.id + '|' + JSON.stringify(ent.keys || null);
	if (shown !== kvShown) {
		kvShown = shown;
		$('kvRows').innerHTML = editable ? ent.keys.map(kvRow).join('') : '<tr><td class="hint">Made by the game code, there are no keys to edit. It can only be deleted.</td></tr>';
		renderEntTools(ent);
	}
	for (const id of ['kvAdd', 'entApply', 'entRotL', 'entRotR', 'entDup', 'entFloor', 'entTemplate']) $(id).disabled = !editable;
	$('entHint').style.display = editable ? '' : 'none';
}

// a key whose value can be played: a sound here in the browser, an effect in the game
const playKind = key => { const k = key.trim().toLowerCase(); return k === 'noise' || k === 'music' ? 'sound' : /fx/.test(k) ? 'effect' : ''; };

function kvRow(kv) {
	const kind = playKind(kv[0]);
	return '<tr><td class="k"><input value="' + escapeHtml(kv[0]) + '" spellcheck="false" list="kvKeys"></td><td><input value="' + escapeHtml(kv[1]) + '" spellcheck="false"></td>' +
		'<td class="x">' + (kind ? '<button class="play" title="' + (kind === 'sound' ? 'Plays the sound here, in the browser' : 'Plays the effect once in the game, where the entity is') + '">Play</button>' : '') +
		'<button title="Remove key">&#10005;</button></td></tr>';
}
$('kvRows').addEventListener('input', () => kvDirty = true);
$('kvRows').addEventListener('click', e => {
	if (e.target.tagName !== 'BUTTON') return;
	if (e.target.classList.contains('play')) { playValue(e.target.closest('tr')); return; }
	e.target.closest('tr').remove();
	kvDirty = true;
});

let playing = null;
async function playValue(row) {
	const [key, value] = [...row.querySelectorAll('input')].map(i => i.value.trim());
	if (!value) return;
	if (playKind(key) === 'effect') {
		const ent = ents.get(selectedEnt);
		if (ent) action({ action: 'effect', value, x: ent.pos[0].toFixed(1), y: ent.pos[1].toFixed(1), z: ent.pos[2].toFixed(1) });
		return;
	}
	// a name without an ending is a .wav for the game, and some are .mp3
	if (playing) playing.pause();
	for (const name of /\.(wav|mp3)$/i.test(value) ? [value] : [value + '.wav', value + '.mp3']) {
		const audio = new Audio('api/file?name=' + encodeURIComponent(name));
		try { await audio.play(); playing = audio; return; } catch (e) { /* the next ending */ }
	}
	toast('The browser can not play ' + value, false);
}
$('kvAdd').addEventListener('click', () => {
	$('kvRows').insertAdjacentHTML('beforeend', kvRow(['', '']));
	kvDirty = true;
	$('kvRows').lastElementChild.querySelector('input').focus();
});

function panelKeys() {
	return [...$('kvRows').querySelectorAll('tr')].map(tr => [...tr.querySelectorAll('input')].map(i => i.value.trim())).filter(kv => kv.length === 2 && kv[0]);
}
function keysText(keys) { return keys.map(kv => kv[0] + '=' + kv[1]).join('\n'); }
function setKey(keys, key, value) {
	const kv = keys.find(k => k[0].toLowerCase() === key);
	if (kv) kv[1] = value; else keys.push([key, value]);
	return keys;
}
const fmtVec = v => v.map(n => Math.round(n)).join(' ');

// Every change to an entity goes through here. record: it is one the user made, it goes into the history
// (not the steps of undo and redo themselves). Gives the answer of the server, null if it was refused.
// quiet: one of several changes that are made together. It says nothing, leaves the list and the selection as
// they are, and why it was refused is in entRefused for whoever makes them.
let entRefused = '';
async function entityOp(params, okMessage, record = true, quiet = false) {
	const ent = params.id !== undefined ? ents.get(+params.id) : null;
	const before = ent && ent.keys ? keysText(ent.keys) : null;
	try {
		const json = await (await fetch('api/entity', { method: 'POST', body: new URLSearchParams(params) })).json();
		if (!quiet) toast(json.ok ? okMessage || json.message : json.message, json.ok);
		if (!json.ok) entRefused = json.message;
		if (json.ok) {
			if (record) histPush(histStepFor(params, ent, before, json.id));
			if (!quiet) {
				kvDirty = false;
				await pollEntities();
				selectEnt(params.op === 'delete' || params.op === 'clear' ? -1 : json.id);
			}
		}
		return json.ok ? json : null;
	} catch (e) {
		entRefused = e.message;
		if (!quiet) toast('Request failed: ' + e.message, false);
		return null;
	}
}

$('entApply').addEventListener('click', () => entityOp({ op: 'update', id: selectedEnt, kv: keysText(panelKeys()) }, 'Applied'));
$('entDup').addEventListener('click', () => {
	const ent = ents.get(selectedEnt);
	if (ent.bmodel) { toast('Brush models can not be duplicated, they use the map geometry', false); return; }
	const beside = (keys, one) => keysText(setKey(keys, 'origin', fmtVec([one.pos[0] + 48, one.pos[1], one.pos[2]])));
	if (picked.size) {
		const more = [...picked].map(id => ents.get(id)).filter(e => e && e.keys && !e.bmodel);
		spawnMany('Duplicated', [beside(panelKeys(), ent)].concat(more.map(e => beside(e.keys.map(k => k.slice()), e))));
		return;
	}
	entityOp({ op: 'spawn', kv: beside(panelKeys(), ent) }, 'Duplicated');
});
// the keys of an entity that is turned by delta degrees around the upright axis
function turnKeys(keys, delta) {
	const angles = keys.find(k => k[0].toLowerCase() === 'angles');
	if (angles) {
		const a = angles[1].split(/\s+/).map(Number);
		a[1] = (((a[1] || 0) + delta) % 360 + 360) % 360;
		angles[1] = fmtVec(a);
	} else {
		const cur = Number((keys.find(k => k[0].toLowerCase() === 'angle') || [])[1] || 0);
		setKey(keys, 'angle', String(((cur + delta) % 360 + 360) % 360));
	}
	return keys;
}

for (const [id, delta] of [['entRotL', 15], ['entRotR', -15]]) {
	$(id).addEventListener('click', () => {
		if (!picked.size) { entityOp({ op: 'update', id: selectedEnt, kv: keysText(turnKeys(panelKeys(), delta)) }, 'Rotated'); return; }
		// Several: they turn together around the middle of where they stand, each as far as the others. What is
		// made of brushes stays: its angle is the way a door opens, not how it stands.
		const all = [selectedEnt, ...picked].map(n => ents.get(n)).filter(e => e && e.keys && !e.bmodel);
		if (!all.length) { toast('Nothing of what is selected can be turned', false); return; }
		const mid = [0, 1].map(i => (Math.min(...all.map(e => e.pos[i])) + Math.max(...all.map(e => e.pos[i]))) / 2);
		const r = delta * Math.PI / 180, c = Math.cos(r), sn = Math.sin(r);
		groupRun('Rotated', ent => {
			if (!ent.keys || ent.bmodel) { entRefused = 'what is made of brushes stays as it is'; return null; }
			const dx = ent.pos[0] - mid[0], dy = ent.pos[1] - mid[1];
			const keys = turnKeys(ent.keys.map(k => k.slice()), delta);
			return { op: 'update', id: ent.id, kv: keysText(setKey(keys, 'origin', fmtVec([mid[0] + dx * c - dy * sn, mid[1] + dx * sn + dy * c, ent.pos[2]]))) };
		});
	});
}
$('entDel').addEventListener('click', () => {
	if (!confirmed($('entDel'))) return;
	if (picked.size) groupRun('Deleted', ent => ({ op: 'delete', id: ent.id }));
	else entityOp({ op: 'delete', id: selectedEnt }, 'Deleted');
});
$('entClose').addEventListener('click', () => selectEnt(-1));

// new origin for a moved entity, brush models keep their offset to the map geometry
function moveParams(ent, pos) {
	if (!ent.keys) return null;
	const keys = ent.keys.map(k => k.slice());
	const cur = (entKey(ent, 'origin') || '0 0 0').split(/\s+/).map(Number);
	const origin = ent.bmodel ? cur.map((v, i) => v + pos[i] - ent.pos[i]) : pos;
	return { op: 'update', id: ent.id, kv: keysText(setKey(keys, 'origin', fmtVec(origin))) };
}

function moveEntity(id, pos) {
	const ent = ents.get(id), params = ent && moveParams(ent, pos);
	ghost = null;
	if (!params) { toast('This entity has no keys, it can not be moved', false); return; }
	// one of several that are selected: the others go the same way
	if (picked.size && (id === selectedEnt || picked.has(id))) {
		const by = pos.map((v, i) => v - ent.pos[i]);
		return groupRun('Moved', one => moveParams(one, one.pos.map((v, i) => v + by[i])));
	}
	return entityOp(params, 'Moved');
}

function setPlacing(on) {
	placing = on && !!$('newClass').value.trim();
	$('placeBtn').classList.toggle('armedPlace', placing);
	$('placeBtn').textContent = placing ? 'Right-click map' : 'Place';
	canvas.style.cursor = placing ? 'crosshair' : 'default';
}
$('placeBtn').addEventListener('click', () => {
	if (!$('newClass').value.trim()) { toast('Pick a classname first', false); $('newClass').focus(); return; }
	setPlacing(!placing);
});

// not in JKAdmin: keys a class gets when it is placed, so they are there to edit, and its height above the floor
const PLACE_KEYS = {
	misc_blackhole: [['radius', '600'], ['strength', '2400'], ['coreradius', '48'], ['dmg', '1000']],
	misc_ball: [['radius', '18'], ['modelscale', '600'], ['bounce', '0.7'], ['friction', '90'], ['mass', '1'], ['kick', '1']],
	misc_cube: [['size', '32'], ['bounce', '0.25'], ['friction', '0.7'], ['mass', '4'], ['kick', '1']],
	misc_prop: [['model', 'models/map_objects/cairn/cargo_sm.md3'], ['scale', '100']],
};
const PLACE_HEIGHT = { misc_blackhole: 72, misc_ball: 64, misc_cube: 48, misc_prop: 48 };

function placeEntity(hit) {
	const name = $('newClass').value.trim();
	// a template: its keys, at the height its class is placed at
	if (name[0] === '@') {
		const keys = templates[name.slice(1)];
		if (!keys) { toast('No template ' + name.slice(1), false); return; }
		const tcls = (keys.find(k => k[0].toLowerCase() === 'classname') || [])[1] || '';
		const tz = tcls in PLACE_HEIGHT ? PLACE_HEIGHT[tcls] : /^misc_model/.test(tcls) ? 0 : 24;
		entityOp({ op: 'spawn', kv: keysText(setKey(keys.map(k => k.slice()), 'origin', fmtVec([hit[0], hit[1], hit[2] + tz]))) }, 'Placed ' + name.slice(1));
		return;
	}
	const cls = name;
	// items drop to the floor themselves, players and NPCs stand on it
	const z = cls in PLACE_HEIGHT ? PLACE_HEIGHT[cls] : /^misc_model/.test(cls) ? 0 : 24;
	entityOp({ op: 'spawn', kv: keysText([['classname', cls], ['origin', fmtVec([hit[0], hit[1], hit[2] + z])]].concat(PLACE_KEYS[cls] || [])) }, 'Placed ' + cls);
}

