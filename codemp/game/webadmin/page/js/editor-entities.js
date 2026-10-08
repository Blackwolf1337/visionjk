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
		// the draft is of the map it was begun on
		if (draft && state && state.map !== draft.map) draft = null;
		if (draft) ents.set(DRAFT_ID, draft);
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
		'<td class="num">' + (e.draft ? 'new' : e.id) + '</td><td>' + escapeHtml(e.classname) + '</td><td class="tn">' + escapeHtml(entKey(e, 'targetname')) + '</td></tr>').join('');
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
	$('entSub').textContent = ent.draft ? 'new · not placed yet' : '#' + ent.id + (ent.bmodel ? ' · brush model' : '') + (ent.linked ? '' : ' · not linked') + (picked.size ? ' · and ' + picked.size + ' more' : '');
	const editable = !!ent.keys;
	const shown = ent.id + '|' + JSON.stringify(ent.keys || null);
	if (shown !== kvShown) {
		kvShown = shown;
		$('kvRows').innerHTML = editable ? ent.keys.map(kvRow).join('') : '<tr><td class="hint">Made by the game code, there are no keys to edit. It can only be deleted.</td></tr>';
		renderEntTools(ent);
	}
	for (const id of ['kvAdd', 'entApply', 'entRotL', 'entRotR', 'entDup', 'entFloor', 'entTemplate']) $(id).disabled = !editable;
	// a draft is sent with Place and thrown away with Discard, and there is nothing of it yet to fire or to copy
	$('entApply').textContent = ent.draft ? 'Place' : 'Apply';
	if (!$('entDel').classList.contains('armed')) $('entDel').textContent = ent.draft ? 'Discard' : 'Delete';
	$('entFire').disabled = !!ent.draft;
	if (ent.draft) $('entDup').disabled = true;
	$('entHint').style.display = editable ? '' : 'none';
}

// a key whose value can be played: a sound here in the browser, an effect in the game
const playKind = key => { const k = key.trim().toLowerCase(); return k === 'noise' || k === 'music' ? 'sound' : /fx/.test(k) ? 'effect' : ''; };

function kvRow(kv) {
	const kind = playKind(kv[0]);
	return '<tr><td class="k"><input value="' + escapeHtml(kv[0]) + '" spellcheck="false" autocomplete="off"></td><td><input value="' + escapeHtml(kv[1]) + '" spellcheck="false"></td>' +
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
let entRefused = '', entRefusedKey = '';
async function entityOp(params, okMessage, record = true, quiet = false) {
	// the draft is not on the server: what is done to it is done here
	if (draft && params.id !== undefined && +params.id === DRAFT_ID) return draftOp(params);
	const ent = params.id !== undefined ? ents.get(+params.id) : null;
	const before = ent && ent.keys ? keysText(ent.keys) : null;
	try {
		const json = await (await fetch('api/entity', { method: 'POST', body: new URLSearchParams(params) })).json();
		if (!quiet) toast(json.ok ? okMessage || json.message : json.message, json.ok);
		if (!json.ok) { entRefused = json.message; entRefusedKey = json.key || ''; }
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
		entRefusedKey = '';
		if (!quiet) toast('Request failed: ' + e.message, false);
		return null;
	}
}

$('entApply').addEventListener('click', () => { if (selectedEnt === DRAFT_ID) placeDraft(); else entityOp({ op: 'update', id: selectedEnt, kv: keysText(panelKeys()) }, 'Applied'); });
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
	// nothing is lost with a draft but what was typed
	if (selectedEnt === DRAFT_ID) { entityOp({ op: 'delete', id: DRAFT_ID }); return; }
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
// A trigger is placed as a box of its own, standing on the floor: the brushes of the map are for the ones the
// map came with. All but the two that are no space to walk into.
const BOX_TRIGGER = /^trigger_(?!always$|asteroid_field$)/i, TRIGGER_BOX = [['mins', '-64 -64 -48'], ['maxs', '64 64 48']];
// Where a class sends to, from where it is placed: one that pushes or teleports is placed with a target_position
// of its own. A trigger_push without one is freed by the game in its first frame, before a key could be added.
const PLACE_AIM = { trigger_push: at => [at[0], at[1], at[2] + 256], trigger_teleport: at => {
	// towards the viewer, where the floor is seen
	const d = camDir(), l = Math.hypot(d[0], d[1]);
	return l > 0.2 ? [at[0] - d[0] / l * 192, at[1] - d[1] / l * 192, at[2]] : [at[0] + 192, at[1], at[2]];
} };
const placeHeight = cls => cls in PLACE_HEIGHT ? PLACE_HEIGHT[cls] : BOX_TRIGGER.test(cls) ? 48 : /^misc_model/.test(cls) ? 0 : 24;

// the trigger and what it is aimed at as one step. The target first: the trigger looks for it in its first frame.
async function placeAimed(cls, keys, at) {
	const names = new Set([...ents.values()].map(e => entKey(e, 'targetname').toLowerCase()));
	let n = 1;
	while (names.has('aim' + n)) n++;
	const steps = [], ids = [];
	for (const kv of [keysText([['classname', 'target_position'], ['origin', fmtVec(PLACE_AIM[cls](at))], ['targetname', 'aim' + n]]), keysText(keys.concat([['target', 'aim' + n]]))]) {
		const params = { op: 'spawn', kv }, json = await entityOp(params, null, false, true);
		if (!json) break;
		steps.push(histStepFor(params, null, null, json.id));
		ids.unshift(json.id);
	}
	await groupDone('Placed ' + cls + (steps.length === 2 ? ' and what it is aimed at:' : ', refused:'), steps, false, ids, steps.length, 2 - steps.length);
}

// ---- An entity that is not placed yet. The server refuses a class that lacks a key it can not be without (an
// effect without its file, a class of brushes without its model), and Place gives a class only its name and
// where it is. So what was refused for a key is opened as a draft: it is in the list and on the map like an
// entity, its keys are edited and it is moved like one, and Place in its panel sends it. The server knows
// nothing of it until then.

function openDraft(cls, keys, at, key) {
	draft = { id: DRAFT_ID, classname: cls, type: 0, bmodel: false, linked: false, placed: true, draft: true, map: state ? state.map : '', pos: at.slice(), keys: keys.map(k => k.slice()) };
	const lower = key.toLowerCase();
	if (!draft.keys.some(k => k[0].toLowerCase() === lower)) draft.keys.push([key, '']);
	ents.set(DRAFT_ID, draft);
	refreshLinks();
	picked.clear();
	selectEnt(DRAFT_ID);
	toast('Not placed yet: give it its ' + key + ', then Place');
	// the cursor into the field of what is missing
	const row = [...$('kvRows').querySelectorAll('tr')].find(tr => tr.querySelector('input') && tr.querySelector('input').value.toLowerCase() === lower);
	if (row) row.querySelectorAll('input')[1].focus();
}
function closeDraft() {
	draft = null;
	ents.delete(DRAFT_ID);
	if (selectedEnt === DRAFT_ID) selectEnt(-1);
	refreshLinks();
	renderEntityList();
}
// What the editor does to an entity, done to the draft: a delete throws it away, new keys are its keys. They are
// laid over what the table of the panel holds, so that a move does not undo what was typed there.
function draftOp(params) {
	if (params.op === 'delete') { closeDraft(); toast('Discarded'); return { ok: true, id: -1 }; }
	if (params.op !== 'update') { toast('It is not placed yet', false); return null; }
	const now = String(params.kv).split('\n').map(l => { const i = l.indexOf('='); return i < 0 ? ['', ''] : [l.slice(0, i), l.slice(i + 1)]; }).filter(k => k[0]);
	const same = (a, b) => a.toLowerCase() === b.toLowerCase(), was = draft.keys;
	let base = selectedEnt === DRAFT_ID && kvDirty ? panelKeys() : was.map(k => k.slice());
	for (const [k, v] of now) {
		const old = was.find(x => same(x[0], k)), has = base.find(x => same(x[0], k));
		if (old && old[1] === v) continue;
		if (has) has[1] = v; else base.push([k, v]);
	}
	base = base.filter(k => now.some(n => same(n[0], k[0])) || !was.some(x => same(x[0], k[0])));
	draft.keys = base;
	draft.classname = (base.find(k => same(k[0], 'classname')) || [])[1] || draft.classname;
	const at = ((base.find(k => same(k[0], 'origin')) || [])[1] || '').trim().split(/\s+/).map(Number);
	if (at.length === 3 && at.every(isFinite)) draft.pos = at;
	kvDirty = false;
	kvShown = '';
	refreshLinks();
	renderEntPanel();
	renderEntityList();
	return { ok: true, id: DRAFT_ID };
}
async function placeDraft() {
	const d = draft, keys = panelKeys();
	const json = await entityOp({ op: 'spawn', kv: keysText(keys) }, 'Placed ' + ((keys.find(k => k[0].toLowerCase() === 'classname') || [])[1] || d.classname));
	if (json) {
		// it is an entity of the server now, and that one is selected
		if (draft === d) { draft = null; ents.delete(DRAFT_ID); renderEntityList(); }
		return;
	}
	// refused again: it stays as it was typed, with what is missing now
	d.keys = keys;
	if (entRefusedKey && !keys.some(k => k[0].toLowerCase() === entRefusedKey.toLowerCase())) d.keys.push([entRefusedKey, '']);
	kvDirty = false;
	kvShown = '';
	renderEntPanel();
}

async function placeEntity(hit) {
	const name = $('newClass').value.trim();
	// a template: its keys, at the height its class is placed at
	if (name[0] === '@') {
		const keys = templates[name.slice(1)];
		if (!keys) { toast('No template ' + name.slice(1), false); return; }
		const tcls = (keys.find(k => k[0].toLowerCase() === 'classname') || [])[1] || '';
		const tz = placeHeight(tcls);
		entityOp({ op: 'spawn', kv: keysText(setKey(keys.map(k => k.slice()), 'origin', fmtVec([hit[0], hit[1], hit[2] + tz]))) }, 'Placed ' + name.slice(1));
		return;
	}
	const cls = name;
	// items drop to the floor themselves, players and NPCs stand on it
	const at = [hit[0], hit[1], hit[2] + placeHeight(cls)];
	const keys = [['classname', cls], ['origin', fmtVec(at)]].concat(PLACE_KEYS[cls] || (BOX_TRIGGER.test(cls) ? TRIGGER_BOX : []));
	if (cls.toLowerCase() in PLACE_AIM) { placeAimed(cls.toLowerCase(), keys, at); return; }
	// refused for a key that it needs: it is opened to be given that key
	if (!await entityOp({ op: 'spawn', kv: keysText(keys) }, 'Placed ' + cls) && entRefusedKey) openDraft(cls, keys, at, entRefusedKey);
}

