// ---- the texture swap: a shader of the map drawn as another one, for every player (/api/remaps, /api/remap)
//
// The game can draw a shader as another one while the map runs: the server keeps a table of names and tells it
// to every game, also to one that joins later, and each game swaps the shader by itself. The unchanged game
// does that too, it is how a button of a map changes a screen. This tab fills that table.
//
// What it can not do: swap one wall. A shader is swapped wherever the map has it. And a game only draws a
// shader it has, one of the game itself or of a pk3 it has: a name it does not know is left as it is there.
//
// A wall that is lit by a lightmap keeps it in a game of this tree. Another game draws it without one once it
// is swapped, and keeps that after the swap is taken back, until the map is loaded again.
//
// The swaps are kept by Save in a file of the map and come back with it. What the map swaps by itself is
// shown here and can be taken back, and is not saved.

// list: { old, new, own } as the server has them. own: made here or read from the file of the map, not by an
// entity of the map. to: the same for the view, by the key of the old name. selected: the shader the pane
// tells of. used, budget: the characters the swaps take of what a game is told when it connects. seen: the
// map the view has the swaps for. uses: the shaders of that map.
const remap = { list: [], to: new Map(), dirty: false, used: 0, budget: 0, known: true, key: '', busy: false, selected: '', seen: null, uses: null, usesOf: null, namesOf: null, namesState: -1 };
const REMAP_ROWS = 150;

// what a shader of the map is drawn as
const remapOf = name => remap.to.get(scKey(name)) || name;
const remapNow = name => remap.list.find(r => scKey(r.old) === scKey(name));

// The shaders the surfaces of the map have, made once for a map: name -> { surfaces, lit }. lit: one of them
// has a lightmap.
function remapUses() {
	if (!world) return new Map();
	if (remap.uses && remap.usesOf === world) return remap.uses;
	const uses = new Map();
	for (const sf of world.surfs) {
		const sh = sf && world.shaders[sf.shader];
		if (!sh) continue;
		let u = uses.get(sh.name);
		if (!u) uses.set(sh.name, u = { surfaces: 0, lit: false });
		u.surfaces++;
		if (sf.lm >= 0 && sf.lm < world.stats.lightmaps) u.lit = true;
	}
	remap.usesOf = world;
	return remap.uses = uses;
}

// The view draws the swaps too (js/frame.js, js/scripts-world.js). The image of a new name is asked for as the
// ones of the map are, and what the scripts draw is put together again. Not the sky: the view keeps the one of
// the map.
function remapApply() {
	remap.to = new Map(remap.list.map(r => [scKey(r.old), r.new]));
	remap.seen = world;
	if (!world) return;
	for (const r of remap.list) if (!world.textures.has(r.new)) world.textures.set(r.new, gameImage(r.new, false));
	prepareScripted(world);
}

// ---- what the server has. Asked in every tab and mode: a swap changes what the map looks like.

async function remapPoll() {
	if (remap.busy) return;
	remap.busy = true;
	try {
		const res = await fetch('api/remaps', { cache: 'no-store' });
		remap.known = res.ok;
		const json = res.ok ? await res.json() : { remaps: [], dirty: false, used: 0, budget: 0 };
		const key = JSON.stringify(json);
		if (key !== remap.key || remap.seen !== world) {
			remap.key = key;
			remap.list = json.remaps;
			remap.dirty = json.dirty;
			remap.used = json.used;
			remap.budget = json.budget;
			remapApply();
			if (mode === 'remaps') remapRender();
		}
	} catch (e) { /* what is shown stays */ } finally { remap.busy = false; }
}
setInterval(remapPoll, 2000);

async function remapOp(params) {
	try {
		const res = await fetch('api/remap', { method: 'POST', body: new URLSearchParams(params) });
		const json = res.status === 404 ? { ok: false, message: 'This game module has no texture swap.' } : await res.json();
		toast(json.message, json.ok);
		remap.key = '';
		await remapPoll();
		return json;
	} catch (e) {
		toast('Request failed: ' + e.message, false);
		return { ok: false };
	}
}

// ---- the pane

// a shader is chosen: by a click on the map, on a row, or typed
function remapPick(name) {
	remap.selected = name || '';
	if ($('remapOld').value !== remap.selected) $('remapOld').value = remap.selected;
	const now = remapNow(remap.selected);
	if (now) $('remapNew').value = now.new;
	remapRender();
}

// the shader of the surface under the mouse
function remapClick(x, y) {
	if (!world) return;
	const ray = screenRay(x, y), hit = pickGround(ray.origin, ray.dir);
	const sf = hit && pickTri >= 0 ? world.surfs[world.pick.surf[pickTri]] : null, sh = sf && world.shaders[sf.shader];
	if (sh) remapPick(sh.name);
}

// the names the two fields offer: the shaders of the map, and for what to draw one as every script as well
function remapNames() {
	if (remap.namesOf === world && remap.namesState === scripts.state) return;
	remap.namesOf = world;
	remap.namesState = scripts.state;
	const level = [...remapUses().keys()].sort(), all = new Set(level.map(scKey));
	for (const k of scripts.bodies.keys()) all.add(k);
	const options = names => names.map(n => '<option value="' + escapeHtml(n) + '">').join('');
	$('remapOldNames').innerHTML = options(level);
	$('remapNewNames').innerHTML = options([...all].sort());
}

function remapRender() {
	const uses = remapUses(), sel = remap.selected, key = scKey(sel), now = sel ? remapNow(sel) : null;
	const used = sel ? [...uses].find(e => scKey(e[0]) === key) : null, u = used && used[1];
	remapNames();
	$('remapCount').textContent = uses.size + ' shaders' + (remap.list.length ? ', ' + remap.list.length + ' swapped' : '');
	$('remapSet').disabled = !remap.known || !sel;
	$('remapClear').disabled = !now;
	$('remapSave').classList.toggle('primary', remap.dirty);
	const lit = 'lit by a lightmap. A game of this tree keeps the light. Another game draws it without once it is swapped, and stays that way after the swap is taken back, until the map is loaded again';
	let about = 'Click a wall on the map or a row below.';
	if (sel) {
		about = escapeHtml(sel) + ': ' + (u ? u.surfaces + ' surface' + (u.surfaces === 1 ? '' : 's') + ' of the map' + (u.lit ? ', ' + lit : '') : 'no surface of the map has it (a model may)') + '.';
		if (now) {
			const t = world && world.textures.get(now.new), none = t && t.failed && !(scripts.state === 2 && scripts.bodies.has(scKey(now.new)));
			about += ' Drawn as ' + escapeHtml(now.new) + (now.own ? '' : ', by the map itself') + '.' +
				(none ? ' The server has no shader and no image of that name: a game leaves the wall as it is.' : '');
		}
	}
	$('remapState').innerHTML = !world ? '' : !remap.known ? 'This game module has no texture swap.' :
		(remap.dirty ? 'Changed and not saved: gone with the map. ' : '') + about +
		(remap.budget ? ' Room: ' + remap.used + ' of ' + remap.budget + ' characters.' : '');

	const row = (name, cls, what, act) => '<tr data-old="' + escapeHtml(name) + '"' + (cls ? ' class="' + cls + '"' : '') + '><td>' + escapeHtml(name) + '</td><td>' + what + '</td><td class="acts">' + act + '</td></tr>';
	const swapped = remap.list.map(r => row(r.old, scKey(r.old) === key ? 'sel' : '', '&rarr; ' + escapeHtml(r.new) + (r.own ? '' : ' (by the map)'), '<button data-clear="' + escapeHtml(r.old) + '">Take back</button>'));
	const typed = scKey($('remapOld').value.trim()), isSwapped = new Set(remap.list.map(r => scKey(r.old)));
	const rest = [...uses].filter(e => !isSwapped.has(scKey(e[0])) && (!typed || scKey(e[0]) === key || scKey(e[0]).includes(typed))).sort((a, b) => a[0] < b[0] ? -1 : 1);
	const shown = rest.slice(0, REMAP_ROWS).map(e => row(e[0], scKey(e[0]) === key ? 'sel' : '', e[1].surfaces + (e[1].lit ? ', lit' : ''), ''));
	$('remapRows').innerHTML = swapped.concat(shown).join('') +
		(rest.length > REMAP_ROWS ? '<tr class="off"><td class="hint" colspan="3">' + (rest.length - REMAP_ROWS) + ' more: type a part of the name above.</td></tr>' : '') ||
		'<tr class="off"><td class="hint">' + (world ? 'No shader of the map has that in its name.' : 'No map.') + '</td></tr>';
}

$('remapOld').addEventListener('input', () => {
	const name = $('remapOld').value.trim(), known = [...remapUses().keys()].find(n => scKey(n) === scKey(name));
	// what is typed narrows the rows, and is the choice once it is the name of a shader of the map
	remap.selected = known || name;
	const now = remapNow(remap.selected);
	if (now) $('remapNew').value = now.new;
	remapRender();
});
$('remapSet').addEventListener('click', () => {
	const from = $('remapOld').value.trim(), to = $('remapNew').value.trim();
	if (!from || !to) { toast('Name the shader of the map and what to draw it as.', false); return; }
	remapOp({ op: 'set', old: from, new: to });
});
$('remapNew').addEventListener('keydown', e => { if (e.key === 'Enter') $('remapSet').click(); });
$('remapClear').addEventListener('click', () => { if (remap.selected) remapOp({ op: 'clear', old: remap.selected }); });
$('remapSave').addEventListener('click', () => remapOp({ op: 'save' }));
$('remapRevert').addEventListener('click', () => {
	if (remap.dirty && !confirmed($('remapRevert'))) return;
	remapOp({ op: 'revert' });
});
$('remapRows').addEventListener('click', e => {
	const clear = e.target.closest('button[data-clear]'), tr = e.target.closest('tr[data-old]');
	if (clear) remapOp({ op: 'clear', old: clear.dataset.clear });
	else if (tr) remapPick(tr.dataset.old);
});
