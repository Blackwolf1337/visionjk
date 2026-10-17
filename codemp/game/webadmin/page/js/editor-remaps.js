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
//
// The pane has a small picture of the selected shader, of what it would be drawn as and of every row. The map
// marks the surfaces that have the selected shader, as the Clips tab marks a clip. What to draw it as is chosen
// from the pictures of all textures, and a test object shows it with its whole shader (js/editor-textures.js).
//
// A texture that is chosen from the pictures is a draft first, as a new entity of the World tab is: the view of
// this page draws the shader as it at once, and the server and the players know nothing of it until Swap sends
// it.

// list: { old, new, own } as the server has them. own: made here or read from the file of the map, not by an
// entity of the map. to: the same for the view, by the key of the old name. selected: the shader the pane
// tells of. used, budget: the characters the swaps take of what a game is told when it connects, told and
// toldRoom: all it is told, and how much of that the swaps may bring it to. seen: the
// map the view has the swaps for. uses: the shaders of that map. mark: the surfaces of the selected shader as
// they are drawn over the map (remapMark). shown: the shader the test object is drawn with. drafts: the swaps
// that are only in this page, { old, new } by the key of the old name.
const remap = { list: [], to: new Map(), dirty: false, used: 0, budget: 0, known: true, key: '', busy: false, selected: '', seen: null, uses: null, usesOf: null, namesOf: null, namesState: -1, mark: null, shown: '', drafts: new Map(), told: 0, toldRoom: 0 };
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

// The view draws the swaps too (js/frame.js, js/scripts-world.js), and in the editor a draft as if it were one:
// the administration shows the map as the players have it. The image of a new name is asked for as the ones of
// the map are, and what the scripts draw is put together again. Not the sky: the view keeps the one of the map.
function remapApply() {
	remap.to = new Map(remap.list.map(r => [scKey(r.old), r.new]));
	if (appMode === 'editor') for (const [key, d] of remap.drafts) remap.to.set(key, d.new);
	remap.seen = world;
	if (!world) return;
	for (const name of remap.to.values()) if (!world.textures.has(name)) world.textures.set(name, gameImage(name, false));
	prepareScripted(world);
}

// ---- the pictures of the shaders, small ones for the pane
//
// The picture of a shader is the image of its name, or else the one a map editor shows for it (editorImageName):
// what the plain view of the editor draws on a wall, not what the stages of a script make of it.

const THUMB_SIZE = 128;
// the key of a shader -> { state: 0 on its way, 1 there, 2 none, url, file, w, h, blind: asked before the
// scripts were read }
const thumbs = new Map();
const thumbQueue = [];
let thumbBusy = 0;

// the file of an image as a small picture: the address the page has it at, '' when it could not be made
async function thumbDraw(f) {
	const k = Math.min(1, THUMB_SIZE / Math.max(f.w, f.h)), cv = document.createElement('canvas'), g = cv.getContext('2d');
	let src = f.img;
	cv.width = Math.max(1, Math.round(f.w * k));
	cv.height = Math.max(1, Math.round(f.h * k));
	if (f.tga) {
		// without its alpha, as the game draws an image that has no script
		const full = document.createElement('canvas'), px = new Uint8ClampedArray(f.img.data);
		for (let i = 3; i < px.length; i += 4) px[i] = 255;
		full.width = f.w; full.height = f.h;
		full.getContext('2d').putImageData(new ImageData(px, f.w, f.h), 0, 0);
		src = full;
	}
	g.imageSmoothingQuality = 'high';
	g.drawImage(src, 0, 0, cv.width, cv.height);
	if (!f.tga) f.img.close();
	const blob = await new Promise(done => cv.toBlob(done, 'image/jpeg', 0.85));
	return blob ? URL.createObjectURL(blob) : '';
}

async function thumbMake(name) {
	const images = [name, editorImageName(name)].filter((n, i, all) => n && all.findIndex(o => scKey(o) === scKey(n)) === i);
	for (const image of images) {
		const f = await fetchImageFile(image);
		if (!f || !f.w || !f.h) continue;
		let url = '';
		try { url = await thumbDraw(f); } catch (e) { /* an image the page can not draw: the next one */ }
		if (url) return { state: 1, url, file: f.file, w: f.w, h: f.h };
	}
	return { state: 2 };
}

// a few on their way at a time
function thumbPump() {
	while (thumbBusy < 3 && thumbQueue.length) {
		const [t, name] = thumbQueue.shift(), key = scKey(name);
		thumbBusy++;
		thumbMake(name).then(made => Object.assign(t, made), () => {
			// the server did not answer: asked again when it is shown again
			if (thumbs.get(key) === t) thumbs.delete(key);
		}).finally(() => {
			thumbBusy--;
			for (const el of document.querySelectorAll('[data-thumb]')) if (scKey(el.dataset.thumb) === key) thumbShow(el, t);
			thumbPump();
		});
	}
}

// The picture of a shader. ask false: only when it was asked for before, else null.
function thumbOf(name, ask = true) {
	const key = scKey(name);
	let t = thumbs.get(key);
	// none while the scripts were not read: its script may name an image
	if (t && t.state === 2 && t.blind && scripts.state === 2) t = undefined;
	if (!t && ask) {
		thumbs.set(key, t = { state: 0, url: '', file: '', w: 0, h: 0, blind: scripts.state !== 2 });
		thumbQueue.push([t, name]);
		thumbPump();
	}
	return t || null;
}

// An element of the page that names a shader (data-thumb) shows the picture of it. Its title, and a caption
// right after it, say which file that is.
function thumbShow(el, t) {
	const there = !!t && t.state === 1, none = !!t && t.state === 2, cap = el.nextElementSibling;
	const what = there ? t.file + ', ' + t.w + ' x ' + t.h : !none ? '' :
		scripts.bodies.has(scKey(el.dataset.thumb)) ? 'a script without an image to show' : 'no image of that name';
	el.style.backgroundImage = there ? 'url(' + t.url + ')' : '';
	el.classList.toggle('none', none);
	el.title = what;
	if (cap && cap.tagName === 'FIGCAPTION') cap.textContent = what;
}
function thumbFill(el) {
	thumbShow(el, el.dataset.thumb ? thumbOf(el.dataset.thumb) : null);
}

// What a list shows asks for its pictures when it comes into view. A list is watched anew when it is built
// again: the ones it had before are let go.
const thumbSeen = new IntersectionObserver(seen => {
	for (const s of seen) if (s.isIntersecting) { thumbSeen.unobserve(s.target); thumbFill(s.target); }
}, { rootMargin: '120px' });
const thumbWaiting = new WeakMap();
function thumbWatch(box) {
	const waiting = [];
	for (const el of thumbWaiting.get(box) || []) thumbSeen.unobserve(el);
	for (const el of box.querySelectorAll('[data-thumb]')) {
		const t = thumbOf(el.dataset.thumb, false);
		if (t && t.state) thumbShow(el, t); else { thumbSeen.observe(el); waiting.push(el); }
	}
	thumbWaiting.set(box, waiting);
}

// ---- the selected shader on the map: its surfaces tinted and their rims drawn, as the Clips tab marks a clip

function remapMarkFree() {
	if (!remap.mark) return;
	for (const p of remap.mark.parts) for (const m of [p.fill, p.lines]) { gl.deleteVertexArray(m.vao); gl.deleteBuffer(m.buf); }
	remap.mark = null;
}

// The triangles of the map that have the selected shader, made once for it: { parts: { model, fill, lines } for
// every brush model that has some, spots: the box around each of its surfaces }. lines: the edges only one of
// those triangles has, the rim of what the shader covers.
function remapMark() {
	const key = scKey(remap.selected);
	if (remap.mark && remap.mark.key === key && remap.mark.of === world) return remap.mark;
	remapMarkFree();
	const pk = world.pick, want = world.shaders.map(sh => scKey(sh.name) === key), byModel = new Map(), spots = new Map();
	for (let t = 0, count = want.includes(true) ? pk.surf.length : 0; t < count; t++) {
		const sf = world.surfs[pk.surf[t]];
		if (!sf || !want[sf.shader]) continue;
		let tris = byModel.get(pk.model[t]), spot = spots.get(pk.surf[t]);
		if (!tris) byModel.set(pk.model[t], tris = []);
		if (!spot) spots.set(pk.surf[t], spot = { mins: [Infinity, Infinity, Infinity], maxs: [-Infinity, -Infinity, -Infinity] });
		tris.push(t);
		for (let c = 0; c < 3; c++) {
			for (let a = 0; a < 3; a++) {
				const v = pk.pos[pk.idx[t * 3 + c] * 3 + a];
				if (v < spot.mins[a]) spot.mins[a] = v;
				if (v > spot.maxs[a]) spot.maxs[a] = v;
			}
		}
	}
	const parts = [];
	for (const [model, tris] of byModel) {
		const fill = new Float32Array(tris.length * 9), places = new Map(), edges = new Map();
		// corners that lie in the same place are one: every surface of the map has its own
		const corner = v => {
			const k = Math.round(pk.pos[v * 3] * 4) + ' ' + Math.round(pk.pos[v * 3 + 1] * 4) + ' ' + Math.round(pk.pos[v * 3 + 2] * 4);
			let n = places.get(k);
			if (n === undefined) places.set(k, n = places.size);
			return n;
		};
		tris.forEach((t, i) => {
			for (let e = 0; e < 3; e++) {
				const a = pk.idx[t * 3 + e], b = pk.idx[t * 3 + (e + 1) % 3], ca = corner(a), cb = corner(b);
				fill.set(pk.pos.subarray(a * 3, a * 3 + 3), i * 9 + e * 3);
				if (ca === cb) continue;
				const k = Math.min(ca, cb) * 0x400000 + Math.max(ca, cb), edge = edges.get(k);
				if (edge) edge[2]++; else edges.set(k, [a, b, 1]);
			}
		});
		const rim = [...edges.values()].filter(e => e[2] === 1), lines = new Float32Array(rim.length * 6);
		rim.forEach((e, i) => {
			lines.set(pk.pos.subarray(e[0] * 3, e[0] * 3 + 3), i * 6);
			lines.set(pk.pos.subarray(e[1] * 3, e[1] * 3 + 3), i * 6 + 3);
		});
		parts.push({ model, fill: pointsVao(fill), lines: pointsVao(lines) });
	}
	return remap.mark = { key, of: world, parts, spots: [...spots.values()] };
}

// Drawn with the lines of the editor (js/editor-lines.js). The tint and the rim where the surface is seen, a
// hair in front of it, and the rim faintly through everything: so a wall in another room is found. No tint
// while the shader is drawn as another one, swapped or as a draft: that one is what is looked at then. What is
// blended leaves the alpha of the canvas alone.
function drawRemapMarks() {
	const mark = remapMark();
	if (!mark.parts.length) return;
	const culled = gl.isEnabled(gl.CULL_FACE);
	gl.disable(gl.CULL_FACE);
	gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); gl.colorMask(true, true, true, false);
	gl.depthMask(false); gl.depthFunc(gl.LEQUAL); gl.depthRange(0, 0.9997);
	const passes = [['lines', gl.LINES, 0.3, true], ['lines', gl.LINES, 1, false]];
	if (!remap.to.has(mark.key)) passes.unshift(['fill', gl.TRIANGLES, 0.2, false]);
	for (const [what, kind, alpha, through] of passes) {
		gl.uniform4f(lineProg.u.uColor, 1, 0.9, 0.2, alpha);
		if (through) gl.disable(gl.DEPTH_TEST);
		for (const p of mark.parts) {
			gl.bindVertexArray(p[what].vao);
			// a brush model that is an entity: where that is
			for (const place of moverPlaces(p.model, frameNow)) {
				gl.uniformMatrix4fv(lineProg.u.uMvp, false, place ? M4.mul(viewProj, place) : viewProj);
				gl.drawArrays(kind, 0, p[what].count);
			}
		}
		if (through) gl.enable(gl.DEPTH_TEST);
	}
	gl.depthMask(true); gl.depthFunc(gl.LESS); gl.depthRange(0, 1);
	gl.disable(gl.BLEND); gl.colorMask(true, true, true, true);
	if (culled) gl.enable(gl.CULL_FACE);
	gl.uniformMatrix4fv(lineProg.u.uMvp, false, viewProj);
}

// the camera looks at the surface with the selected shader that is nearest to where it looks now
function remapLookAt() {
	if (!world || !remap.selected) return;
	const from = cam.fly ? cam.pos : cam.target;
	let best = null, bestFar = Infinity;
	for (const s of remapMark().spots) {
		const far = Math.hypot(...s.mins.map((v, i) => (v + s.maxs[i]) / 2 - from[i]));
		if (far < bestFar) { bestFar = far; best = s; }
	}
	if (best) boxLookAt(best);
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
			remap.told = json.told || 0;
			remap.toldRoom = json.toldRoom || 0;
			// a draft is of the map it was made on
			if (remap.seen && remap.seen !== world) remap.drafts.clear();
			remapApply();
			if (mode === 'remaps') remapRender();
		}
	} catch (e) { /* what is shown stays */ } finally { remap.busy = false; }
}
setInterval(remapPoll, 2000);

// a change is sent: what the server says of it, { ok, message }
async function remapPost(params) {
	return apiPost('api/remap', params, 'This game module has no texture swap.');
}
async function remapOp(params) {
	const json = await remapPost(params);
	toast(json.message, json.ok);
	// what was a draft is on the server now
	if (json.ok && params.op === 'set') remap.drafts.delete(scKey(params.old));
	remap.key = '';
	await remapPoll();
	return json;
}

// ---- the drafts

// what the field of the tab says a shader is drawn as: its draft, else what the server has
function remapFieldFor(name) {
	const draft = remap.drafts.get(scKey(name)), now = remapNow(name);
	if (draft || now) $('remapNew').value = (draft || now).new;
}

// A texture is chosen from the pictures (js/editor-textures.js): the selected shader is drawn as it at once, in
// this page only.
function remapDraft(name) {
	const old = remap.selected, now = old ? remapNow(old) : null;
	$('remapNew').value = name;
	if (!old) {
		toast('Click a wall of the map first: its shader is what is drawn as this one', false);
	} else {
		// what the shader is anyway is no draft
		if (scKey(name) === scKey(old) || (now && scKey(name) === scKey(now.new))) remap.drafts.delete(scKey(old));
		else remap.drafts.set(scKey(old), { old, new: name });
		remapApply();
	}
	remapRender();
}
function remapDraftDrop(old) {
	if (!remap.drafts.delete(scKey(old))) return;
	remapApply();
	if (scKey(old) === scKey(remap.selected)) { $('remapNew').value = ''; remapFieldFor(old); }
	remapRender();
}

// ---- the pane

// a shader is chosen: by a click on the map, on a row, or typed
function remapPick(name) {
	remap.selected = name || '';
	if ($('remapOld').value !== remap.selected) $('remapOld').value = remap.selected;
	remapFieldFor(remap.selected);
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

// The pictures of the pane: of a shader, and of what is chosen to draw it as. The test object next to them is
// drawn with the second, or with the first while there is no second.
function remapPictures(old) {
	const as = $('remapNew').value.trim();
	for (const [id, name] of [['remapOldPic', old], ['remapNewPic', as]]) {
		const el = $(id);
		if (name) el.dataset.thumb = name; else delete el.dataset.thumb;
		thumbFill(el);
	}
	remap.shown = as || old;
	$('remapBall').title = !remap.shown ? '' : remap.shown + ', as the game draws it on a model: every stage of its script, with the light of a place where it has none. Not the lightmap of a wall, a model has none.' +
		(scripts.state === 2 && materialFor(remap.shown, 'model').nodraw ? ' This shader draws nothing.' : '');
	// the one that is chosen has a frame in the list of the textures
	texChosen();
}

function remapRender() {
	const uses = remapUses(), sel = remap.selected, key = scKey(sel), now = sel ? remapNow(sel) : null, draft = sel ? remap.drafts.get(key) : null;
	const used = sel ? [...uses].find(e => scKey(e[0]) === key) : null, u = used && used[1], drafts = remap.drafts.size;
	remapNames();
	// not of what is half typed: of a shader the map has, one that is swapped, or one that has a script
	remapPictures(used || now || (sel && scripts.bodies.has(key)) ? sel : '');
	$('remapCount').textContent = uses.size + ' shaders' + (remap.list.length ? ', ' + remap.list.length + ' swapped' : '');
	$('remapSet').disabled = !remap.known || !sel;
	$('remapSet').classList.toggle('primary', !!draft);
	// a draft of the selected shader is thrown away before a swap of it is taken back
	$('remapClear').disabled = !now && !draft;
	$('remapClear').textContent = draft ? 'Discard draft' : 'Take back';
	$('remapClear').title = draft ? 'The draft of the selected shader is thrown away. No player saw it.' : 'The selected shader is drawn as itself again';
	$('remapDraftRow').hidden = !drafts;
	$('remapDraftCount').textContent = drafts + (drafts === 1 ? ' draft' : ' drafts') + ', only in this page';
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
		if (draft) about += ' A draft: this page draws it as ' + escapeHtml(draft.new) + ', no player sees that until Swap sends it.';
	}
	$('remapState').innerHTML = !world ? '' : !remap.known ? 'This game module has no texture swap.' :
		(remap.dirty ? 'Changed and not saved: gone with the map. ' : '') + about +
		(remap.budget ? ' Room: ' + remap.used + ' of ' + remap.budget + ' characters.' : '') +
		(remap.toldRoom ? ' All a game is told when it connects: ' + remap.told + ' of ' + remap.toldRoom + '.' : '');

	const row = (name, cls, what, act) => '<tr data-old="' + escapeHtml(name) + '"' + (cls ? ' class="' + cls + '"' : '') + '><td class="pic"><i class="thumb" data-thumb="' + escapeHtml(name) + '"></i></td><td>' +
		escapeHtml(name) + '</td><td>' + what + '</td><td class="acts">' + act + '</td></tr>';
	const act = (what, name, label) => '<button data-' + what + '="' + escapeHtml(name) + '">' + label + '</button>';
	const drafted = [...remap.drafts.values()].map(d => row(d.old, 'draft' + (scKey(d.old) === key ? ' sel' : ''), '&rarr; ' + escapeHtml(d.new) + ' (draft)', act('swap', d.old, 'Swap') + act('drop', d.old, 'Discard')));
	const swapped = remap.list.map(r => row(r.old, scKey(r.old) === key ? 'sel' : '', '&rarr; ' + escapeHtml(r.new) + (r.own ? '' : ' (by the map)'), act('clear', r.old, 'Take back')));
	const typed = scKey($('remapOld').value.trim()), isSwapped = new Set(remap.list.map(r => scKey(r.old)).concat([...remap.drafts.keys()]));
	const rest = [...uses].filter(e => !isSwapped.has(scKey(e[0])) && (!typed || scKey(e[0]) === key || scKey(e[0]).includes(typed))).sort((a, b) => a[0] < b[0] ? -1 : 1);
	const shown = rest.slice(0, REMAP_ROWS).map(e => row(e[0], scKey(e[0]) === key ? 'sel' : '', e[1].surfaces + (e[1].lit ? ', lit' : ''), ''));
	$('remapRows').innerHTML = drafted.concat(swapped, shown).join('') +
		(rest.length > REMAP_ROWS ? '<tr class="off"><td class="hint" colspan="4">' + (rest.length - REMAP_ROWS) + ' more: type a part of the name above.</td></tr>' : '') ||
		'<tr class="off"><td class="hint" colspan="4">' + (world ? 'No shader of the map has that in its name.' : 'No map.') + '</td></tr>';
	thumbWatch($('remapRows'));
}

$('remapOld').addEventListener('input', () => {
	const name = $('remapOld').value.trim(), known = [...remapUses().keys()].find(n => scKey(n) === scKey(name));
	// what is typed narrows the rows, and is the choice once it is the name of a shader of the map
	remap.selected = known || name;
	remapFieldFor(remap.selected);
	remapRender();
});
$('remapSet').addEventListener('click', () => {
	const from = $('remapOld').value.trim(), to = $('remapNew').value.trim();
	if (!from || !to) { toast('Name the shader of the map and what to draw it as.', false); return; }
	remapOp({ op: 'set', old: from, new: to });
});
$('remapNew').addEventListener('keydown', e => { if (e.key === 'Enter') $('remapSet').click(); });
// the picture of what is typed there, once the typing rests
let remapTyped = 0;
$('remapNew').addEventListener('input', () => { clearTimeout(remapTyped); remapTyped = setTimeout(remapRender, 300); });
$('remapClear').addEventListener('click', () => {
	if (!remap.selected) return;
	if (remap.drafts.has(scKey(remap.selected))) remapDraftDrop(remap.selected); else remapOp({ op: 'clear', old: remap.selected });
});
// Every draft is sent, one after the other, until the server refuses one: that one and the ones after it stay
// drafts.
$('remapDraftsSwap').addEventListener('click', async () => {
	let sent = 0, refused = '';
	for (const d of [...remap.drafts.values()]) {
		const json = await remapPost({ op: 'set', old: d.old, new: d.new });
		if (!json.ok) { refused = json.message || 'refused'; break; }
		remap.drafts.delete(scKey(d.old));
		sent++;
	}
	toast(refused ? (sent ? sent + ' swapped, then: ' : '') + refused : sent + (sent === 1 ? ' swap' : ' swaps') + ' sent: every player sees ' + (sent === 1 ? 'it' : 'them') + ' now', !refused);
	remap.key = '';
	await remapPoll();
});
$('remapDraftsDrop').addEventListener('click', () => {
	remap.drafts.clear();
	remapApply();
	$('remapNew').value = '';
	remapFieldFor(remap.selected);
	remapRender();
});
$('remapSave').addEventListener('click', () => remapOp({ op: 'save' }));
$('remapRevert').addEventListener('click', () => {
	if (remap.dirty && !confirmed($('remapRevert'))) return;
	remapOp({ op: 'revert' });
});
$('remapRows').addEventListener('click', e => {
	const b = e.target.closest('button'), tr = e.target.closest('tr[data-old]'), draft = b && b.dataset.swap ? remap.drafts.get(scKey(b.dataset.swap)) : null;
	if (b && b.dataset.clear) remapOp({ op: 'clear', old: b.dataset.clear });
	else if (draft) remapOp({ op: 'set', old: draft.old, new: draft.new });
	else if (b && b.dataset.drop) remapDraftDrop(b.dataset.drop);
	// to it: the camera looks at the nearest surface that has it
	else if (!b && tr) { remapPick(tr.dataset.old); remapLookAt(); }
});
