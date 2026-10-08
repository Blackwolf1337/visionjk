// ---- the panel of an entity: its place, the names of its spawnflags, its links, what its class is

function renderEntTools(ent) {
	const d = classDoc(ent.classname), editable = !!ent.keys, ang = entAngles(ent);
	[['posX', ent.pos[0]], ['posY', ent.pos[1]], ['posZ', ent.pos[2]], ['angP', ang[0]], ['angY', ang[1]], ['angR', ang[2]]].forEach(([id, v]) => {
		$(id).value = Math.round(v * 100) / 100;
		$(id).disabled = !editable;
	});

	// a trigger that is a box of its own: how big
	const tb = triggerBox(ent);
	['boxLabel', 'boxX', 'boxY', 'boxZ', 'boxHint'].forEach((id, i) => {
		$(id).style.display = tb ? '' : 'none';
		if (!tb || !i || i > 3) return;
		$(id).value = Math.round((tb.maxs[i - 1] - tb.mins[i - 1]) * 100) / 100;
		$(id).disabled = !editable;
	});

	// a box for every spawnflag that has a name, x and - stand for bits without one
	const flags = +entKey(ent, 'spawnflags') || 0;
	$('entFlags').innerHTML = editable && d ? d.flags.map((name, i) => /^(x|-|\?)$/i.test(name) ? '' :
		'<label><input type="checkbox" data-bit="' + i + '"' + (flags & (1 << i) ? ' checked' : '') + '>' + escapeHtml(name) + '</label>').join('') : '';

	const chip = (id, key) => {
		const other = ents.get(id);
		return other ? '<a data-id="' + id + '" title="' + escapeHtml(key) + '">' + (other.draft ? 'new' : '#' + id) + ' ' + escapeHtml(other.classname) + '</a>' : '';
	};
	const fires = links.filter(l => l.from === ent.id), firedBy = links.filter(l => l.to === ent.id);
	// a push: what its throw is, as the arc of the Links view shows it
	const arc = isPush(ent) ? pushArc(ent, true) : null, r = v => Math.round(v);
	const thrown = !arc ? '' : arc.bad ? '<span class="warn">No throw: its target has to be above ' + (ent.classname.toLowerCase() === 'trigger_push' ? 'the middle of its box' : 'it') + '.</span>'
		: 'Throws ' + r(arc.up) + ' up and ' + r(arc.across) + ' across, at the top after ' + arc.time.toFixed(2) + ' s' + (arc.land ? ', meets the map at ' + arc.land.map(r).join(' ') : '') + '.';
	$('entLinks').innerHTML = [fires.length ? 'Fires ' + fires.map(l => chip(l.to, l.key)).join('') : '', firedBy.length ? 'Fired by ' + firedBy.map(l => chip(l.from, l.key)).join('') : '', thrown]
		.filter(Boolean).join('<br>');

	$('entDoc').style.display = d ? '' : 'none';
	if (d) $('entDocText').textContent = d.name + (d.flags.length ? '   ' + d.flags.join(' ') : '') + '\n\n' + d.text;
}

// a value into the table of keys, where Apply takes it from
function setPanelKey(key, value) {
	const row = [...$('kvRows').querySelectorAll('tr')].find(tr => {
		const name = tr.querySelector('input');
		return name && name.value.trim().toLowerCase() === key;
	});
	if (row) row.querySelectorAll('input')[1].value = value;
	else $('kvRows').insertAdjacentHTML('beforeend', kvRow([key, value]));
	kvDirty = true;
}

$('entFlags').addEventListener('change', () => {
	const boxes = [...$('entFlags').querySelectorAll('input')];
	const named = boxes.reduce((bits, box) => bits | (1 << +box.dataset.bit), 0);
	const now = +((panelKeys().find(k => k[0].toLowerCase() === 'spawnflags') || [])[1]) || 0;
	// the bits without a name stay as they are
	setPanelKey('spawnflags', String((now & ~named) | boxes.reduce((bits, box) => box.checked ? bits | (1 << +box.dataset.bit) : bits, 0)));
});
$('entLinks').addEventListener('click', e => {
	const a = e.target.closest('a');
	if (!a || !ents.has(+a.dataset.id)) return;
	selectEnt(+a.dataset.id);
	focusEntity(+a.dataset.id);
});

// ---- the layout: what the editor did to the map, kept on the server for every start of it (/api/layout)

function renderLayout(l) {
	// a game module that keeps none
	$('layoutRow').style.display = l ? '' : 'none';
	if (!l) return;
	const holds = l.add + l.remove ? l.add + ' placed or changed' + (l.remove ? ', ' + l.remove + ' of the map gone' : '') : 'the map as its file has it';
	$('layoutState').textContent = l.saved ? (l.dirty ? 'Layout: changed since it was saved' : 'Layout: saved, comes back with the map')
		: (l.add + l.remove ? 'Layout: not saved, lost with the map' : 'Layout: none');
	$('layoutState').title = 'What a layout of this map holds now: ' + holds + '.';
	$('layoutState').style.color = l.dirty || (!l.saved && l.add + l.remove) ? 'var(--yellow)' : '';
	$('layoutSave').disabled = l.saved ? !l.dirty : !(l.add + l.remove);
	$('layoutForget').disabled = !l.saved;
}

async function layoutOp(op) {
	try {
		const json = await (await fetch('api/layout', { method: 'POST', body: new URLSearchParams({ op }) })).json();
		toast(json.message, json.ok);
		pollEntities();
	} catch (e) { toast('Request failed: ' + e.message, false); }
}
$('layoutSave').addEventListener('click', () => layoutOp('save'));
$('layoutForget').addEventListener('click', () => { if (confirmed($('layoutForget'))) layoutOp('forget'); });

function renderBudget(b) {
	if (!b) { $('entBudget').textContent = ''; return; }
	const part = (name, v) => {
		const text = name + ' ' + v[0] + '/' + v[1];
		return v[0] >= v[1] * 0.9 ? '<b>' + text + '</b>' : text;
	};
	// the memory of the game for this map, which every spawn takes from and only a start of the map gives back
	const memory = b.memory ? ['Memory ' + Math.round(b.memory[0] / b.memory[1] * 100) + '%'].map(t => b.memory[0] >= b.memory[1] * 0.9 ? '<b>' + t + '</b>' : t) : [];
	$('entBudget').innerHTML = [part('Entities', b.entities), part('Models', b.models), part('Sounds', b.sounds), part('Effects', b.effects)].concat(memory).join(' &middot; ');
	$('entBudget').title = b.memory ? 'Memory: ' + Math.round(b.memory[0] / 1024) + ' of ' + Math.round(b.memory[1] / 1024) + ' KB of what the game has for a map. Every spawn takes some for the text of its keys, also every move of an entity, and only a start of the map gives it back (Reload map keeps what was placed).' : '';
}

// ---- values to pick from, so that the name of a map, an effect or an NPC does not have to be known by heart

const PICK_KINDS = { pickMaps: 'maps', pickBots: 'bots', pickEffects: 'effects', pickMusic: 'music', pickNpcs: 'npcs' };
const pickAsked = new Set();

function pickOptions(id, names) {
	$(id).innerHTML = [...new Set(names)].filter(Boolean).sort().map(n => '<option value="' + escapeHtml(n) + '">').join('');
}

// the lists the server has are asked for once, the ones the page knows itself are made when they are needed
async function pickFill(id) {
	if (id === 'pickClasses') { pickOptions(id, allClasses); return; }
	if (id === 'pickNames') { pickOptions(id, [...ents.values()].map(e => entKey(e, 'targetname'))); return; }
	if (!(id in PICK_KINDS) || pickAsked.has(id)) return;
	pickAsked.add(id);
	try {
		pickOptions(id, (await (await fetch('api/list?kind=' + PICK_KINDS[id])).json()).names);
	} catch (e) {
		pickAsked.delete(id);
	}
}

// the list the value of a key is picked from: what it fires from the names there are, an effect, an NPC type, ...
function pickFor(key) {
	const k = key.trim().toLowerCase();
	if (k === 'classname') return 'pickClasses';
	if (LINK_KEYS.has(k)) return 'pickNames';
	if (k === 'npc_type') return 'pickNpcs';
	if (k === 'mapname') return 'pickMaps';
	if (k === 'music') return 'pickMusic';
	return /fx/.test(k) ? 'pickEffects' : '';
}

// Keys whose value is a file of the game: the folder it is looked for in and the endings that count. The field
// offers what is in the folder that is typed so far, a folder that is picked shows its own files next.
const PATH_KEYS = { noise: ['sound', '.wav,.mp3'], model: ['models', '.md3,.glm'], model2: ['models', '.md3,.glm'] };
const pathLists = new Map();

async function pickPath(input, key) {
	const [root, ext] = PATH_KEYS[key], value = input.value.trim();
	let dir = value.includes('/') ? value.slice(0, value.lastIndexOf('/')) : root;
	if (dir.toLowerCase() !== root && !dir.toLowerCase().startsWith(root + '/')) dir = root;
	const id = dir.toLowerCase() + '|' + ext;
	if (!pathLists.has(id)) {
		pathLists.set(id, fetch('api/list?kind=dir&dir=' + encodeURIComponent(dir) + '&ext=' + encodeURIComponent(ext)).then(r => r.json()).then(j => j.names)
			.catch(() => { pathLists.delete(id); return []; }));
	}
	const names = await pathLists.get(id);
	if (document.activeElement === input) pickOptions('pickPath', names);
}

// every brush model of the map by its number, each with the entities that have it now
function pickBrushes() {
	const used = new Map();
	for (const e of ents.values()) {
		const m = /^\*(\d+)$/.exec(entKey(e, 'model'));
		if (m && !e.draft) used.set(+m[1], (used.get(+m[1]) || []).concat(e.classname + ' #' + e.id));
	}
	const count = world ? world.models.length - 1 : 0;
	$('pickBrush').innerHTML = Array.from({ length: Math.max(0, count) }, (_, i) => '<option value="*' + (i + 1) + '" label="' + escapeHtml((used.get(i + 1) || ['no entity has it now']).join(', ')) + '">').join('');
}

// the value field of a row of the key table that the event is for, and the key of that row
function kvValueField(e) {
	const row = e.target.closest('tr'), inputs = row ? row.querySelectorAll('input') : [];
	return inputs.length === 2 && e.target === inputs[1] ? inputs[0].value.trim().toLowerCase() : null;
}

$('kvRows').addEventListener('focusin', e => {
	const key = kvValueField(e);
	if (key === null) return;
	// the model of a class that is made of brushes is one of the brush models of the map
	if (key === 'model' && /^(func_|trigger_)/i.test(panelClass())) { e.target.setAttribute('list', 'pickBrush'); pickBrushes(); return; }
	if (key in PATH_KEYS) { e.target.setAttribute('list', 'pickPath'); pickPath(e.target, key); return; }
	const id = pickFor(key);
	if (id) { e.target.setAttribute('list', id); pickFill(id); } else e.target.removeAttribute('list');
});
$('kvRows').addEventListener('input', e => {
	const key = kvValueField(e);
	if (key !== null && key in PATH_KEYS) pickPath(e.target, key);
});
$('mapName').addEventListener('focus', () => pickFill('pickMaps'));
$('botName').addEventListener('focus', () => pickFill('pickBots'));

