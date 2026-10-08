// ---- the drafts: entities that are only in the page until they are placed
//
// Everything new is a draft first: a class or a template that is placed, a trigger with what it is aimed at, a
// copy (Duplicate, paste). A draft is in the list and on the map like an entity: its keys are edited and it is
// moved, turned and selected like one, and the server knows nothing of it until Place in its panel or Place all
// sends it. So a class that can not be without a key (an effect without its file, a class of brushes without its
// model) gets that key before the first spawn, and what belongs together is placed as one step of the history.
// Undo and redo, the import of a file and the Models tab go to the server at once: their keys are complete.

const draftWaiting = new Set();	// the drafts whose keys changed since the server was asked about them
let draftTimer = 0, draftBusy = false;
let draftFocus = -1;		// the draft that gets the cursor into the key it lacks, when the server has named it

// keys with a place of their own: a draft that is made of the keys of something else
const draftAt = (keys, at) => ({ keys: setKey(keys.map(k => k.slice()), 'origin', fmtVec(at)), pos: at });
// the keys of a draft as they stand: what was typed into the table of the panel counts
const draftShown = d => d.id === selectedEnt && kvDirty ? panelKeys() : d.keys;

// The keys of a draft are these now. Its class and its place are what they say, as the server would read them.
function draftKeys(d, keys) {
	d.keys = keys;
	d.classname = entKey(d, 'classname') || d.classname;
	const at = entKey(d, 'origin').trim().split(/\s+/).map(Number);
	if (at.length === 3 && at.every(isFinite)) d.pos = at;
	draftAsk(d);
}

// What was typed into the keys of the selected draft is its keys: a draft has no Apply. Before anything that
// reads its keys or builds the table again. The table is not built again here, it shows these keys already: only
// what the panel says besides them follows (its place, its flags, its links).
function draftTakePanel() {
	const d = kvDirty ? drafts.get(selectedEnt) : null;
	if (!d) return;
	draftKeys(d, panelKeys());
	kvDirty = false;
	kvShown = kvSig(d);
	refreshLinks();
	renderEntTools(d);
}
function draftTyped() {
	const d = drafts.get(selectedEnt);
	if (d) draftAsk(d);
}

function newDraft(keys, pos) {
	const d = { id: DRAFT_BASE + draftNext++, classname: '', keys: [], pos: pos.slice(), draft: true, map: state ? state.map : loadedMap, need: '', why: '', bmodel: false, linked: false, placed: true, type: 0 };
	draftKeys(d, keys.map(k => k.slice()));
	drafts.set(d.id, d);
	ents.set(d.id, d);
	return d;
}

// What is new begins here, as drafts that are selected, the first as the one the panel shows. list: { keys, pos }
// of each. Nothing is spawned.
function openDrafts(list, label) {
	const made = list.map(l => newDraft(l.keys, l.pos));
	refreshLinks();
	pickOnly(made.map(d => d.id));
	renderDraftRow();
	draftFocus = made.length === 1 ? made[0].id : -1;
	draftAskNow();
	toast(label + ': ' + (made.length === 1 ? 'a draft, Place puts it' : made.length + ' drafts, Place puts them') + ' on the server');
	return made;
}

// The server is asked what a spawn of a draft would be refused for (op check, which spawns nothing and is no line
// of its log), a moment after its keys changed: typing changes them with every letter.
function draftAsk(d) {
	draftWaiting.add(d.id);
	clearTimeout(draftTimer);
	draftTimer = setTimeout(draftAskNow, 300);
}
async function draftAskNow() {
	clearTimeout(draftTimer);
	const ids = [...draftWaiting];
	draftWaiting.clear();
	for (const id of ids) {
		const d = drafts.get(id);
		if (!d) continue;
		const kv = keysText(draftShown(d));
		try {
			const json = await (await fetch('api/entity', { method: 'POST', body: new URLSearchParams({ op: 'check', kv }) })).json();
			// placed, thrown away or changed while the answer was on its way: the change has asked again
			if (drafts.get(id) === d && keysText(draftShown(d)) === kv) draftSays(d, json.ok ? '' : json.message, json.ok ? '' : json.key || '');
		} catch (e) { /* it is asked again with the next change */ }
	}
}

// What the server says of a draft: why a spawn of it is refused ('' when it is not), and the key it lacks when a
// key is what is missing. That key gets a row to be typed into. The table of the panel is not built again for
// it: the cursor may be in it.
function draftSays(d, why, need) {
	const shown = d.id === selectedEnt, lower = need.toLowerCase();
	d.why = why;
	d.need = need;
	if (need && !draftShown(d).some(k => k[0].toLowerCase() === lower)) {
		if (shown) draftTakePanel();
		d.keys.push([need, '']);
		if (shown) { $('kvRows').insertAdjacentHTML('beforeend', kvRow([need, ''])); kvShown = kvSig(d); }
	}
	if (shown) renderEntPanel();
	if (draftFocus === d.id) { draftNeedFocus(d); draftFocus = -1; }
}
// the cursor into the field of what the selected draft lacks, unless it is in a field of the table already
function draftNeedFocus(d) {
	if (!d || !d.need || d.id !== selectedEnt || $('kvRows').contains(document.activeElement)) return;
	const lower = d.need.toLowerCase(), row = [...$('kvRows').querySelectorAll('tr')].find(tr => tr.querySelector('input') && tr.querySelector('input').value.trim().toLowerCase() === lower);
	if (row) row.querySelectorAll('input')[1].focus();
}

// What the editor does to an entity, done to a draft (entityOp): new keys are its keys, a delete throws it away.
// The keys the caller gives are those of the draft with a change. They are laid over what the table of the panel
// holds when it shows this draft and something was typed there, so that a move does not undo what was typed.
function draftOp(params, quiet) {
	const d = drafts.get(+params.id);
	if (params.op === 'delete') {
		if (quiet) draftDrop(d.id); else discardDrafts([d.id]);
		return { ok: true, id: -1 };
	}
	if (params.op !== 'update') {
		entRefused = 'a draft is not on the server yet';
		entRefusedKey = '';
		if (!quiet) toast('A draft is not on the server yet', false);
		return null;
	}
	const now = String(params.kv).split('\n').map(l => { const i = l.indexOf('='); return i < 0 ? ['', ''] : [l.slice(0, i), l.slice(i + 1)]; }).filter(k => k[0]);
	const same = (a, b) => a.toLowerCase() === b.toLowerCase(), was = d.keys, shown = d.id === selectedEnt;
	let base = draftShown(d).map(k => k.slice());
	for (const [k, v] of now) {
		const old = was.find(x => same(x[0], k)), has = base.find(x => same(x[0], k));
		if (old && old[1] === v) continue;
		if (has) has[1] = v; else base.push([k, v]);
	}
	base = base.filter(k => now.some(n => same(n[0], k[0])) || !was.some(x => same(x[0], k[0])));
	draftKeys(d, base);
	refreshLinks();
	if (shown) { kvDirty = false; kvShown = ''; renderEntPanel(); }
	if (!quiet) renderEntityList();
	return { ok: true, id: d.id };
}

// Place: these drafts are sent to the server, and with them every draft they name as what they fire, and what
// those name. What is aimed at goes before what aims at it: a trigger_push whose target is not there yet is freed
// by the game in its first frame. All that are placed are one step of the history. One that is refused stays a
// draft with what the server said of it, and is what is selected afterwards. What aims at one that was not placed
// is not sent.
async function placeDrafts(ids, label) {
	if (draftBusy) return;
	draftBusy = true;
	try {
		draftTakePanel();
		refreshLinks();
		const order = [], seen = new Set(), steps = [], placed = new Map(), kept = new Set();
		const visit = id => {
			if (seen.has(id) || !drafts.has(id)) return;
			seen.add(id);
			for (const l of links) if (l.from === id) visit(l.to);
			order.push(id);
		};
		ids.forEach(visit);
		if (!order.length) return;
		for (const id of order) {
			const d = drafts.get(id);
			// thrown away while the ones before it were sent
			if (!d) continue;
			const params = { op: 'spawn', kv: keysText(d.keys) };
			if (links.some(l => l.from === id && kept.has(l.to))) { kept.add(id); continue; }
			const json = await entityOp(params, null, false, true);
			if (json) {
				steps.push(histStepFor(params, null, null, json.id));
				placed.set(id, json.id);
			} else {
				kept.add(id);
				draftSays(d, entRefused, entRefusedKey);
			}
		}
		for (const id of placed.keys()) draftDrop(id);
		// What is selected then, the chosen ones before what was sent with them: what was placed, or what was not, to
		// be put right. One that lacks a key is the one the panel shows, with the cursor in that key.
		const all = [...new Set(ids.filter(id => seen.has(id)).concat(order))], left = all.filter(id => kept.has(id) && drafts.has(id));
		left.sort((a, b) => !drafts.get(a).need - !drafts.get(b).need);
		await groupDone(label, steps, false, left.length ? left : all.map(id => placed.get(id)), placed.size, left.length);
		draftNeedFocus(drafts.get(selectedEnt));
	} finally {
		draftBusy = false;
	}
}

// a draft is gone: placed, thrown away, or of another map
function draftDrop(id) {
	if (!drafts.delete(id)) return false;
	ents.delete(id);
	picked.delete(id);
	draftWaiting.delete(id);
	return true;
}
function discardDrafts(ids) {
	const n = ids.filter(id => draftDrop(id)).length;
	if (!n) return;
	refreshLinks();
	// what else was selected stays selected
	if (selectedEnt >= 0 && !ents.has(selectedEnt)) pickOnly(selectedIds());
	else { renderEntPanel(); renderEntityList(); }
	renderDraftRow();
	toast(n > 1 ? 'Discarded ' + n : 'Discarded');
}

// After every poll of the list: the server knows nothing of the drafts, they are put back among its entities. A
// draft is of the map it was begun on.
function draftsKeep() {
	for (const d of [...drafts.values()]) {
		if (state && d.map !== state.map) draftDrop(d.id); else ents.set(d.id, d);
	}
	renderDraftRow();
}

function renderDraftRow() {
	$('draftRow').hidden = !drafts.size;
	$('draftCount').textContent = drafts.size + (drafts.size === 1 ? ' draft' : ' drafts') + ', not on the server yet';
}
$('draftPlaceAll').addEventListener('click', () => placeDrafts([...drafts.keys()], 'Placed'));
$('draftDiscardAll').addEventListener('click', () => {
	// one is thrown away as its panel does it, more are asked about once
	if (drafts.size > 1 && !confirmed($('draftDiscardAll'))) return;
	discardDrafts([...drafts.keys()]);
});

// The marker of a draft on the map. Not the cube of an entity that is placed: a diamond in the colour of its
// class made half white, that turns and breathes over a ring. Red when it is selected, as the others are.
function drawDraftMark(pos, c, sel, now) {
	const col = sel ? [1, 0.2, 0.3, 1] : [(c[0] + 1) / 2, (c[1] + 1) / 2, (c[2] + 1) / 2, 1];
	drawMesh(diamondMesh, pos[0], pos[1], pos[2], now / 500, (sel ? 1.6 : 1.25) * (1 + 0.16 * Math.sin(now / 260)), col);
	drawMesh(ringMesh, pos[0], pos[1], pos[2] - 18, -now / 900, 0.4, col);
}
