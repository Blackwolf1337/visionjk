/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

const keys = new Set();
let drag = null, marker = null;

function playerAt(x, y) {
	let best = -1, bestD = 28;
	for (const [id, p] of players) {
		if (p.data.team === 'spectator') continue;
		const s = (p.data.scale || 100) / 100;
		for (const z of [10, 36 * s, 64 * s]) {
			const sp = project([p.draw[0], p.draw[1], p.draw[2] - 24 + z]);
			if (!sp) continue;
			const d = Math.hypot(sp[0] - x, sp[1] - y);
			if (d < bestD) { bestD = d; best = id; }
		}
	}
	return best;
}

canvas.addEventListener('contextmenu', e => e.preventDefault());
canvas.addEventListener('mousedown', e => {
	const rect = canvas.getBoundingClientRect(), x = e.clientX - rect.left, y = e.clientY - rect.top;
	drag = { x: e.clientX, y: e.clientY, button: e.button, moved: false, target: cam.target.slice(), yaw: cam.yaw, pitch: cam.pitch };
	// Shift and a drag in the World tab of the editor: a rectangle, and what is in it is selected
	if (appMode === 'editor' && mode === 'world' && e.button === 0 && e.shiftKey) {
		drag.band = { x, y };
		return;
	}
	// a face of the box of the selected trigger
	const face = e.button === 0 ? boxHandleAt(x, y) : null;
	if (face) { drag.face = boxDragStart(face); return; }
	// grab the selected entity to move it
	if (mode === 'world' && e.button === 0 && selectedEnt >= 0 && entityAt(x, y) === selectedEnt) {
		const ent = ents.get(selectedEnt), below = pickGround([ent.pos[0], ent.pos[1], ent.pos[2] + 1], [0, 0, -1]);
		drag.ent = { id: selectedEnt, height: below ? ent.pos[2] - below[2] : 0 };
	}
});
window.addEventListener('mousemove', e => {
	if (!drag) return;
	const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
	if (Math.abs(dx) + Math.abs(dy) > 4) drag.moved = true;
	if (!drag.moved) return;
	if (drag.band) {
		const rect = canvas.getBoundingClientRect(), b = drag.band, el = $('band');
		b.x2 = Math.min(rect.width, Math.max(0, e.clientX - rect.left));
		b.y2 = Math.min(rect.height, Math.max(0, e.clientY - rect.top));
		el.style.display = 'block';
		el.style.left = Math.min(b.x, b.x2) + 'px';
		el.style.top = Math.min(b.y, b.y2) + 'px';
		el.style.width = Math.abs(b.x2 - b.x) + 'px';
		el.style.height = Math.abs(b.y2 - b.y) + 'px';
	} else if (drag.face) {
		const rect = canvas.getBoundingClientRect();
		boxDragMove(drag.face, screenRay(e.clientX - rect.left, e.clientY - rect.top));
	} else if (drag.ent) {
		const rect = canvas.getBoundingClientRect(), ray = screenRay(e.clientX - rect.left, e.clientY - rect.top);
		const hit = snapHit(pickGround(ray.origin, ray.dir));
		if (hit) ghost = { id: drag.ent.id, pos: [hit[0], hit[1], hit[2] + drag.ent.height] };
	} else if (cam.fly && drag.button !== 2) {
		// flying: the mouse turns the head
		cam.yaw = drag.yaw - dx * 0.25;
		cam.pitch = Math.min(89, Math.max(-89, drag.pitch + dy * 0.25));
	} else if (drag.button === 1 || (drag.button === 0 && e.altKey)) {
		cam.yaw = drag.yaw - dx * 0.3;
		cam.pitch = Math.min(89, Math.max(10, drag.pitch + dy * 0.3));
	} else if (drag.button === 0) {
		cam.follow = -1;
		const scale = cam.dist / canvas.clientHeight * 1.1, y = cam.yaw * Math.PI / 180;
		cam.target[0] = drag.target[0] - (Math.sin(y) * dx - Math.cos(y) * dy / Math.sin(cam.pitch * Math.PI / 180)) * scale;
		cam.target[1] = drag.target[1] + (Math.cos(y) * dx + Math.sin(y) * dy / Math.sin(cam.pitch * Math.PI / 180)) * scale;
	}
});
window.addEventListener('mouseup', e => {
	if (!drag) return;
	const d = drag;
	drag = null;
	if (d.band) {
		$('band').style.display = 'none';
		if (d.moved && d.band.x2 !== undefined) bandSelect(d.band, e.ctrlKey || e.metaKey);
		return;
	}
	if (d.face) { boxDragEnd(d.face); return; }
	if (d.ent && d.moved) {
		if (ghost) moveEntity(d.ent.id, ghost.pos);
		return;
	}
	if (d.moved) return;
	const rect = canvas.getBoundingClientRect(), x = e.clientX - rect.left, y = e.clientY - rect.top;
	if (e.target !== canvas) return;
	if (d.button === 0) {
		if (mode === 'routes') wpSelect(wpAt(x, y));
		else if (mode === 'mapinfo') inspectSurface(x, y);
		else if (mode === 'world') {
			// Ctrl and a click adds an entity to the selected ones or takes it out again
			const hit = entityAt(x, y);
			if ((e.ctrlKey || e.metaKey) && hit >= 0 && selectedEnt >= 0 && hit !== DRAFT_ID && selectedEnt !== DRAFT_ID) {
				if (hit !== selectedEnt) { if (picked.has(hit)) picked.delete(hit); else picked.add(hit); }
				kvShown = '';
				renderEntPanel();
			} else {
				picked.clear();
				selectEnt(hit);
			}
		}
		else select(playerAt(x, y));
		return;
	}
	if (d.button !== 2) return;
	const ray = screenRay(x, y), hit = snapHit(pickGround(ray.origin, ray.dir));
	if (mode === 'routes' && wp.adding) {
		if (!hit) { toast('No floor there', false); return; }
		marker = { pos: hit, t: performance.now() };
		wpAddAt(hit);
	} else if (mode === 'world' && placing) {
		if (!hit) { toast('No floor there', false); return; }
		marker = { pos: hit, t: performance.now() };
		placeEntity(hit);
	} else if (mode === 'models' && mdlPlacing) {
		if (!hit) { toast('No floor there', false); return; }
		marker = { pos: hit, t: performance.now() };
		mdlPlaceAt(hit);
	} else if (mode === 'world' && selectedEnt >= 0) {
		if (!hit) { toast('No floor there', false); return; }
		const ent = ents.get(selectedEnt), below = pickGround([ent.pos[0], ent.pos[1], ent.pos[2] + 1], [0, 0, -1]);
		marker = { pos: hit, t: performance.now() };
		moveEntity(selectedEnt, [hit[0], hit[1], hit[2] + (below ? ent.pos[2] - below[2] : 0)]);
	} else if (mode === 'players' && selected >= 0) {
		if (!hit) { toast('No floor there', false); return; }
		marker = { pos: hit, t: performance.now() };
		action({ action: 'teleport', id: selected, x: hit[0].toFixed(1), y: hit[1].toFixed(1), z: hit[2].toFixed(1) });
	}
});
canvas.addEventListener('wheel', e => {
	e.preventDefault();
	if (cam.fly) cam.flySpeed = Math.min(6000, Math.max(50, cam.flySpeed * Math.pow(1.0015, -e.deltaY)));
	else cam.dist = Math.min(20000, Math.max(80, cam.dist * Math.pow(1.0015, e.deltaY)));
}, { passive: false });
window.addEventListener('keydown', e => {
	if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
	if (editorKey(e)) { e.preventDefault(); return; }
	const digit = /^Digit([1-9])$/.exec(e.code || '');
	if (digit && !e.ctrlKey && !e.altKey && !e.metaKey) { camBookmark(+digit[1], e.shiftKey); return; }
	keys.add(e.key.toLowerCase());
	if (e.key === 'Escape') { if (view.id >= 0) { setFirstPerson(-1); return; } select(-1); picked.clear(); selectEnt(-1); setPlacing(false); }
	if (e.key.toLowerCase() === 'v' && (selected >= 0 || view.id >= 0)) setFirstPerson(view.id >= 0 ? -1 : selected);
	if ((e.key === 'Delete' || e.key === 'Backspace') && mode === 'world' && selectedEnt >= 0) $('entDel').click();
	if ((e.key === 'Delete' || e.key === 'Backspace') && mode === 'routes' && wp.selected >= 0) $('wpRemove').click();
	if (e.key.toLowerCase() === 'c' && !e.ctrlKey && !e.metaKey && !e.altKey) setFly(!cam.fly);
	if (e.key.toLowerCase() === 'f' && selected >= 0) { cam.follow = cam.follow === selected ? -1 : selected; renderCommand(); }
});
window.addEventListener('keyup', e => keys.delete(e.key.toLowerCase()));
window.addEventListener('blur', () => keys.clear());

