// ---- completion of the keys: what the key field of a row can hold is offered while it is typed, each key with
// what is known about it. From four places, the nearest first: what the definition of the class says, what
// its spawn function reads (/api/classkeys), what other entities of the class have on this map, and the keys
// every entity takes.

const classKeys = new Map();	// classname in lower case -> [{ key, kind, def }], '*' the keys of every entity, '*item' of an item
let classKeysAsked = false;

async function loadClassKeys() {
	if (classKeysAsked) return;
	classKeysAsked = true;
	try {
		const res = await fetch('api/classkeys');
		if (!res.ok) return;	// a game module without it: the definitions and the map are still there
		for (const line of (await res.text()).split('\n')) {
			const [name, ...keys] = line.split('\t');
			if (name) classKeys.set(name.toLowerCase(), keys.map(k => { const [key, kind, def] = k.split('|'); return { key, kind, def: def || '' }; }));
		}
	} catch (e) {
		classKeysAsked = false;
	}
}

const KIND_WORDS = { Int: 'whole number', Float: 'number', Vector: 'x y z', String: 'text', Boolean: '0 or 1' };
// The keys every entity takes, the ones that are used most first, and what they are for where the game code says
// it. A class may take one of them differently: then its own definition is what the list shows.
const FIELD_FIRST = ['targetname', 'target', 'origin', 'angle', 'angles', 'spawnflags', 'model', 'wait', 'random', 'speed', 'count', 'health', 'dmg', 'delay', 'radius', 'message', 'team', 'target2'];
const FIELD_DOCS = {
	classname: 'what kind of entity it is',
	targetname: 'its name: what fires it names it by this',
	target: 'the targetname of what it fires',
	origin: 'where it is',
	angle: 'the way it looks, as a yaw. -1 is up, -2 is down',
	angles: 'the way it looks: pitch yaw roll',
	spawnflags: 'the boxes above the keys, as one number',
	model: 'a model file, or *N for brushes of the map',
	opentarget: 'a door fires this when it has reached its open position',
	closetarget: 'a door fires this when it has reached its closed position',
	paintarget: 'fired when it is hit and not destroyed',
	targetshadername: 'a shader of the map that is swapped when it fires its targets',
	targetshadernewname: 'the shader targetShaderName is swapped for',
	script_targetname: 'the name scripts look for it by',
	npc_type: 'which NPC, a name of the NPC files',
};

// every key that can be offered for a class: { key, text, kind, def, used, from }
function keyChoices(cls) {
	const lower = (cls || '').toLowerCase(), d = classDoc(cls), out = new Map();
	const add = (key, more) => {
		const k = key.toLowerCase(), has = out.get(k) || { key, from: more.from };
		for (const [name, value] of Object.entries(more)) if (value && !has[name]) has[name] = value;
		out.set(k, has);
	};
	for (const [k, text] of d ? d.keyDocs : []) add(k, { text, from: 'class' });
	const item = /^(item_|weapon_|ammo_|holocron_|team_ctf_)/.test(lower);
	for (const k of (classKeys.get(lower) || []).concat(item ? classKeys.get('*item') || [] : [])) add(k.key, { kind: k.kind, def: k.def, from: 'class' });
	// what the others of its kind have: a key that shows up here is one the map makers used
	for (const e of ents.values()) {
		if (e.classname.toLowerCase() !== lower || e.id === selectedEnt) continue;
		for (const [k, v] of e.keys || []) add(k, { used: v, from: 'map' });
	}
	const fields = (classKeys.get('*') || []).slice().sort((a, b) => {
		const ia = FIELD_FIRST.indexOf(a.key), ib = FIELD_FIRST.indexOf(b.key);
		return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib) || a.key.localeCompare(b.key);
	});
	for (const k of fields) add(k.key, { kind: k.kind, text: FIELD_DOCS[k.key] || (/script$/.test(k.key) ? 'the name of a script to run' : /^parm\d+$/.test(k.key) ? 'a value for its scripts' : ''), from: 'every' });
	return [...out.values()];
}
// what is said about a key in one line
function keyHint(c) {
	const parts = [];
	if (c.text) parts.push(c.text);
	if (c.def) parts.push('default ' + c.def);
	if (c.used && !c.text) parts.push('on this map: ' + c.used);
	return parts.join(' · ');
}

const suggest = { el: $('kvSuggest'), input: null, items: [], at: -1 };

function suggestClose() {
	suggest.el.style.display = 'none';
	suggest.el.innerHTML = '';
	suggest.input = null;
	suggest.items = [];
	suggest.at = -1;
}
// under the field, or above it where there is no room below
function suggestPlace() {
	if (!suggest.input) return;
	const r = suggest.input.getBoundingClientRect(), below = innerHeight - r.bottom - 8, up = below < 140 && r.top > below, s = suggest.el.style;
	s.left = Math.max(4, Math.min(r.left, innerWidth - 444)) + 'px';
	s.maxHeight = Math.max(80, Math.min(280, up ? r.top - 8 : below)) + 'px';
	s.top = up ? '' : r.bottom + 2 + 'px';
	s.bottom = up ? innerHeight - r.top + 2 + 'px' : '';
}
function suggestDraw() {
	suggest.el.innerHTML = suggest.items.map((c, i) => '<div class="opt' + (i === suggest.at ? ' on' : '') + '" data-i="' + i + '"><b>' + escapeHtml(c.key) + '</b><i>' +
		(c.from === 'every' ? 'every entity · ' : c.from === 'map' ? 'used on this map · ' : '') + (KIND_WORDS[c.kind] || '') + '</i><span>' + escapeHtml(keyHint(c)) + '</span></div>').join('');
	const on = suggest.el.querySelector('.on');
	if (on) on.scrollIntoView({ block: 'nearest' });
}
// the class as the table has it now: a classname that was just typed counts
function panelClass() {
	const ent = ents.get(selectedEnt);
	return (panelKeys().find(k => k[0].toLowerCase() === 'classname') || [])[1] || (ent ? ent.classname : '');
}
function suggestFor(input) {
	const row = input.closest('tr'), typed = input.value.trim().toLowerCase();
	// a key the entity has already is not offered a second time
	const taken = new Set([...$('kvRows').querySelectorAll('tr')].filter(tr => tr !== row).map(tr => tr.querySelector('input')).filter(Boolean).map(i => i.value.trim().toLowerCase()));
	const all = keyChoices(panelClass()).filter(c => !taken.has(c.key.toLowerCase()));
	// what begins with what is typed, then what has it somewhere
	suggest.items = all.filter(c => c.key.toLowerCase().startsWith(typed)).concat(typed ? all.filter(c => !c.key.toLowerCase().startsWith(typed) && c.key.toLowerCase().includes(typed)) : []);
	if (!suggest.items.length) { suggestClose(); return; }
	suggest.input = input;
	suggest.at = typed ? 0 : -1;
	suggest.el.style.display = 'block';
	suggestDraw();
	suggestPlace();
}
// the key is taken into its field, and the field of its value is next
function suggestTake(i, focus) {
	const c = suggest.items[i], input = suggest.input;
	if (!c || !input) return;
	const value = input.closest('tr').querySelectorAll('input')[1];
	input.value = c.key;
	kvDirty = true;
	suggestClose();
	if (focus && value) value.focus();
}
const isKeyField = el => el.tagName === 'INPUT' && !!el.closest('td.k');

$('kvRows').addEventListener('focusin', e => {
	if (isKeyField(e.target)) { suggestFor(e.target); return; }
	// the field of a value says what its key is for, and what it is when nothing is given
	const row = e.target.closest('tr'), fields = row ? row.querySelectorAll('input') : [];
	if (fields.length !== 2 || e.target !== fields[1]) return;
	const c = keyChoices(panelClass()).find(k => k.key.toLowerCase() === fields[0].value.trim().toLowerCase());
	e.target.title = c ? [keyHint(c), KIND_WORDS[c.kind]].filter(Boolean).join(' · ') : '';
	e.target.placeholder = c && c.def ? c.def : '';
});
$('kvRows').addEventListener('input', e => { if (isKeyField(e.target)) suggestFor(e.target); });
$('kvRows').addEventListener('focusout', e => { if (e.target === suggest.input) suggestClose(); });
$('kvRows').addEventListener('keydown', e => {
	if (e.target !== suggest.input) {
		// the list is closed and the field is asked again
		if (e.key === 'ArrowDown' && isKeyField(e.target)) { suggestFor(e.target); e.preventDefault(); }
		return;
	}
	const n = suggest.items.length;
	if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
		suggest.at = (suggest.at + (e.key === 'ArrowDown' ? 1 : suggest.at < 0 ? 0 : -1) + n) % n;
		suggestDraw();
		e.preventDefault();
	} else if (e.key === 'Enter' && suggest.at >= 0) {
		suggestTake(suggest.at, true);
		e.preventDefault();
	} else if (e.key === 'Tab' && !e.shiftKey && suggest.at >= 0 && e.target.value.trim()) {
		// the browser goes on to the field of the value by itself
		suggestTake(suggest.at, false);
	} else if (e.key === 'Escape') {
		suggestClose();
		e.stopPropagation();
	}
});
// a click must not take the focus out of the field, or the list is gone before the click is done
suggest.el.addEventListener('mousedown', e => {
	e.preventDefault();
	const opt = e.target.closest('.opt');
	if (opt) suggestTake(+opt.dataset.i, true);
});
suggest.el.addEventListener('mousemove', e => {
	const opt = e.target.closest('.opt');
	if (opt && +opt.dataset.i !== suggest.at) { suggest.at = +opt.dataset.i; for (const o of suggest.el.children) o.classList.toggle('on', o === opt); }
});
window.addEventListener('resize', suggestPlace);
document.addEventListener('scroll', suggestPlace, true);
