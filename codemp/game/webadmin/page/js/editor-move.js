// ---- the grid, and moving by numbers and keys

function gridStep() { return Math.max(1, +$('grid').value || 1); }

// a spot on the floor, on the grid of the editor: across, its height stays that of the floor
function snapHit(hit) {
	if (!hit || appMode !== 'editor') return hit;
	const g = gridStep();
	return [Math.round(hit[0] / g) * g, Math.round(hit[1] / g) * g, hit[2]];
}

try { const g = localStorage.getItem('grid'); if (g) $('grid').value = g; } catch (e) { /* the grid of the page then */ }
$('grid').addEventListener('change', () => { try { localStorage.setItem('grid', $('grid').value); } catch (e) { /* not kept */ } });

function entAngles(ent) {
	const a = entKey(ent, 'angles');
	if (a) {
		const v = a.trim().split(/\s+/).map(Number);
		return [v[0] || 0, v[1] || 0, v[2] || 0];
	}
	// the angle key is the yaw, with -1 and -2 for up and down
	const yaw = Number(entKey(ent, 'angle')) || 0;
	return yaw === -1 ? [-90, 0, 0] : yaw === -2 ? [90, 0, 0] : [0, yaw, 0];
}

function focusEntity(id) {
	const ent = ents.get(id);
	if (!ent) return;
	cam.follow = -1;
	cam.target = ent.pos.slice();
	cam.dist = Math.min(cam.dist, 700);
	// flying: to a place it is seen from, looking the way the camera looks
	if (cam.fly) { const d = camDir(); cam.pos = ent.pos.map((v, i) => v - d[i] * 320); }
}

for (const id of ['posX', 'posY', 'posZ']) {
	$(id).addEventListener('change', () => {
		if (ents.has(selectedEnt)) moveEntity(selectedEnt, ['posX', 'posY', 'posZ'].map(i => +$(i).value || 0));
	});
}
for (const id of ['angP', 'angY', 'angR']) {
	$(id).addEventListener('change', () => {
		const ent = ents.get(selectedEnt);
		if (!ent || !ent.keys) return;
		// the three angles take the place of the one of the angle key
		const keys = ent.keys.map(k => k.slice()).filter(k => k[0].toLowerCase() !== 'angle');
		entityOp({ op: 'update', id: selectedEnt, kv: keysText(setKey(keys, 'angles', ['angP', 'angY', 'angR'].map(i => +$(i).value || 0).join(' '))) }, 'Turned');
	});
}
// ---- the box of a trigger: its size in the panel, and its faces dragged with the mouse

function setTriggerBox(ent, mins, maxs) {
	const num = v => String(Math.round(v * 100) / 100), keys = ent.keys.map(k => k.slice());
	setKey(keys, 'mins', mins.map(num).join(' '));
	return entityOp({ op: 'update', id: ent.id, kv: keysText(setKey(keys, 'maxs', maxs.map(num).join(' '))) }, 'Resized');
}
for (const id of ['boxX', 'boxY', 'boxZ']) {
	$(id).addEventListener('change', () => {
		const ent = ents.get(selectedEnt), tb = ent && ent.keys ? triggerBox(ent) : null;
		if (!tb) return;
		// around the middle the box has, which need not be its origin
		const half = ['boxX', 'boxY', 'boxZ'].map(i => Math.max(1, +$(i).value || 0) / 2), mid = tb.mins.map((v, i) => (v + tb.maxs[i]) / 2);
		setTriggerBox(ent, mid.map((v, i) => v - half[i]), mid.map((v, i) => v + half[i]));
	});
}

// The handles of the selected trigger that is a box: one in the middle of each face, { axis, side, pos }. Only
// of one entity: several that are selected are moved, not shaped.
function boxHandles() {
	const ent = appMode === 'editor' && mode === 'world' && !picked.size ? ents.get(selectedEnt) : null, tb = ent && ent.keys ? triggerBox(ent) : null;
	if (!tb) return [];
	const b = boxGhost && boxGhost.id === ent.id ? boxGhost : tb, out = [];
	for (let axis = 0; axis < 3; axis++) {
		for (const side of [0, 1]) out.push({ axis, side, pos: ent.pos.map((v, i) => v + (i === axis ? (side ? b.maxs[i] : b.mins[i]) : (b.mins[i] + b.maxs[i]) / 2)) });
	}
	return out;
}
// as big on the screen wherever it is
const handleScale = pos => Math.min(12, Math.max(0.3, Math.hypot(pos[0] - eye[0], pos[1] - eye[1], pos[2] - eye[2]) / 1000));
function boxHandleAt(x, y) {
	let best = null, bestD = 13;
	for (const h of boxHandles()) {
		const sp = project(h.pos), d = sp ? Math.hypot(sp[0] - x, sp[1] - y) : 99;
		if (d < bestD) { bestD = d; best = h; }
	}
	return best;
}
function drawBoxHandles() {
	const list = boxHandles();
	if (!list.length) return;
	const over = drag && drag.face ? drag.face : mouseAt && !drag ? boxHandleAt(mouseAt[0], mouseAt[1]) : null;
	gl.useProgram(flatProg.p);
	gl.disable(gl.DEPTH_TEST);
	for (const h of list) {
		const hot = over && over.axis === h.axis && over.side === h.side;
		drawMesh(cubeMesh, h.pos[0], h.pos[1], h.pos[2], 0, handleScale(h.pos) * (hot ? 1.5 : 1), hot ? [1, 1, 1, 1] : [1, 0.8, 0.15, 1]);
	}
	gl.enable(gl.DEPTH_TEST);
}

// A face is dragged along its axis: to the point of that line the ray of the mouse comes closest to, on the grid
// of the editor. The other faces stay, and so does the origin. The box keeps a width.
function boxDragStart(h) {
	const ent = ents.get(selectedEnt), tb = triggerBox(ent);
	return { id: ent.id, axis: h.axis, side: h.side, from: h.pos.slice(), origin: ent.pos.slice(), mins: tb.mins.slice(), maxs: tb.maxs.slice() };
}
function boxDragMove(f, ray) {
	const w = f.from.map((v, i) => v - ray.origin[i]), b = ray.dir[f.axis], dw = ray.dir[0] * w[0] + ray.dir[1] * w[1] + ray.dir[2] * w[2];
	// looking along the axis the mouse says nothing about it
	if (1 - b * b < 0.002) return;
	const g = gridStep(), at = Math.round((f.from[f.axis] + (b * dw - w[f.axis]) / (1 - b * b)) / g) * g - f.origin[f.axis];
	const mins = f.mins.slice(), maxs = f.maxs.slice();
	if (f.side) maxs[f.axis] = Math.min(mins[f.axis] + 32768, Math.max(mins[f.axis] + 1, at));
	else mins[f.axis] = Math.max(maxs[f.axis] - 32768, Math.min(maxs[f.axis] - 1, at));
	boxGhost = { id: f.id, mins, maxs };
	// the size in the panel follows
	['boxX', 'boxY', 'boxZ'].forEach((id, i) => { $(id).value = Math.round((maxs[i] - mins[i]) * 100) / 100; });
}
async function boxDragEnd(f) {
	const g = boxGhost, ent = ents.get(f.id);
	try {
		if (g && ent && ent.keys && (g.mins[f.axis] !== f.mins[f.axis] || g.maxs[f.axis] !== f.maxs[f.axis])) await setTriggerBox(ent, g.mins, g.maxs);
	} finally { boxGhost = null; }
}
$('entFocus').addEventListener('click', () => focusEntity(selectedEnt));

// Fire: what a trigger does to the entity it has as its target. The map is not changed by it, so it is no step
// of the history, and what is typed into the keys stays.
async function fireEntities() {
	// a draft is not there yet to be fired
	const ids = selectedIds().filter(id => !drafts.has(id));
	let done = 0, said = '';
	for (const id of ids) {
		try {
			const json = await (await fetch('api/entity', { method: 'POST', body: new URLSearchParams({ op: 'use', id }) })).json();
			if (json.ok) done++; else said = json.message;
		} catch (e) { said = e.message; }
	}
	if (ids.length === 1) toast(done ? 'Fired' : said, !!done);
	else toast('Fired ' + done + (done < ids.length ? ', ' + (ids.length - done) + ' refused: ' + said : ''), done === ids.length);
	// what breaks or fires only once is gone now
	pollEntities();
}
$('entFire').addEventListener('click', fireEntities);
$('flyBtn').addEventListener('click', () => setFly(!cam.fly));
$('entFloor').addEventListener('click', () => {
	const ent = ents.get(selectedEnt);
	if (!ent) return;
	const hit = pickGround([ent.pos[0], ent.pos[1], ent.pos[2] + 1], [0, 0, -1]);
	if (!hit) { toast('No floor below it', false); return; }
	// its box stands on the floor. A class without one goes where placing puts it.
	const box = entBox(ent);
	const lift = box ? ent.pos[2] - (box.center[2] - box.size[2] / 2) : ent.classname in PLACE_HEIGHT ? PLACE_HEIGHT[ent.classname] : 24;
	moveEntity(selectedEnt, [ent.pos[0], ent.pos[1], hit[2] + lift]);
});

const NUDGE_KEYS = new Set(['arrowup', 'arrowdown', 'arrowleft', 'arrowright', 'pageup', 'pagedown']);
let nudging = false;

async function nudgeEntity(k) {
	const ent = ents.get(selectedEnt);
	if (!ent || nudging) return;
	const g = gridStep(), y = cam.yaw * Math.PI / 180;
	// along the axis of the world the camera looks along most, so that up is away from the viewer
	const fwd = Math.abs(Math.cos(y)) >= Math.abs(Math.sin(y)) ? [Math.sign(Math.cos(y)), 0] : [0, Math.sign(Math.sin(y))];
	const d = k === 'arrowup' ? [fwd[0], fwd[1], 0] : k === 'arrowdown' ? [-fwd[0], -fwd[1], 0] : k === 'arrowright' ? [fwd[1], -fwd[0], 0]
		: k === 'arrowleft' ? [-fwd[1], fwd[0], 0] : k === 'pageup' ? [0, 0, 1] : [0, 0, -1];
	nudging = true;
	try { await moveEntity(selectedEnt, ent.pos.map((v, i) => v + d[i] * g)); } finally { nudging = false; }
}

// ---- copy and paste: Ctrl+C takes the keys of the selected entity, Ctrl+V makes a draft with them where the mouse
// is on the map, as high above the floor as the copied one was. Ctrl+D is the Duplicate of its panel.

let entClip = null;		// { keys, pos, lift }
let mouseAt = null;		// where the mouse is over the map, null when it is not
canvas.addEventListener('mousemove', e => {
	const rect = canvas.getBoundingClientRect();
	mouseAt = [e.clientX - rect.left, e.clientY - rect.top];
});
canvas.addEventListener('mouseleave', () => { mouseAt = null; });

function entCopy() {
	draftTakePanel();
	const ent = ents.get(selectedEnt);
	if (!ent || !ent.keys) { toast('This entity has no keys to copy', false); return; }
	if (ent.bmodel) { toast('Brush models can not be copied, they use the map geometry', false); return; }
	const below = pickGround([ent.pos[0], ent.pos[1], ent.pos[2] + 1], [0, 0, -1]);
	// the ones that are selected with it come along, each where it is from the first
	const more = [...picked].map(id => ents.get(id)).filter(e => e && e.keys && !e.bmodel).map(e => ({ keys: e.keys.map(k => k.slice()), from: e.pos.map((v, i) => v - ent.pos[i]) }));
	entClip = { keys: ent.keys.map(k => k.slice()), pos: ent.pos.slice(), lift: below ? ent.pos[2] - below[2] : 0, more };
	toast('Copied ' + ent.classname + (more.length ? ' and ' + more.length + ' more' : ''));
}

function entPaste() {
	let origin = [entClip.pos[0] + 48, entClip.pos[1], entClip.pos[2]];
	if (mouseAt) {
		const ray = screenRay(mouseAt[0], mouseAt[1]), hit = snapHit(pickGround(ray.origin, ray.dir));
		if (hit) origin = [hit[0], hit[1], hit[2] + entClip.lift];
	}
	openDrafts([draftAt(entClip.keys, origin)].concat(entClip.more.map(m => draftAt(m.keys, origin.map((v, i) => v + m.from[i])))), 'Pasted');
}

// the keys of the editor. true: the key was one of them
function editorKey(e) {
	if (appMode !== 'editor') return false;
	const k = e.key.toLowerCase();
	// not while text of the page is marked: then Ctrl+C is for that
	if ((e.ctrlKey || e.metaKey) && !e.altKey && !e.shiftKey && mode === 'world' && !String(window.getSelection())) {
		if (k === 'c' && selectedEnt >= 0) { entCopy(); return true; }
		if (k === 'v' && entClip) { entPaste(); return true; }
		if (k === 'd' && selectedEnt >= 0 && ents.has(selectedEnt) && ents.get(selectedEnt).keys) { $('entDup').click(); return true; }
	}
	if ((e.ctrlKey || e.metaKey) && !e.altKey && (k === 'z' || k === 'y')) {
		histStep(k === 'z' && !e.shiftKey);
		return true;
	}
	if (e.shiftKey && mode === 'world' && selectedEnt >= 0 && NUDGE_KEYS.has(k)) {
		nudgeEntity(k);
		return true;
	}
	return false;
}

