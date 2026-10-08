// ---- the names of the entities above their markers
//
// The class of an entity, its targetname, and of a draft that it is one and what it lacks. Not of all of them: of
// what is selected, of every draft, and of the ones nearest to the camera. A frame comes sixty times a second and
// a map has hundreds of entities, so which ones is worked out a few times a second and without the screen (how
// far, and on which side of the camera). A frame only projects the chosen ones and moves their lines of text.

const ELABEL_ALWAYS = 64, ELABEL_NEAR = 40, ELABEL_MS = 150;
// list: the chosen ones, what always has a name first. put: the ones of them a frame shows, and where.
// pool: the lines of text, made once and used again, the first shown of them in use.
const entLabel = { list: [], put: [], pool: [], shown: 0, at: 0, names: $('dbgNames'), markers: $('dbgMarkers'), layer: $('entLabels') };

function entLabelsChoose() {
	const list = [], has = new Set();
	const add = ent => {
		const cn = ent.classname, name = entKey(ent, 'targetname'), tn = name ? ' "' + name + '"' : '', tag = ent.draft ? (ent.need ? ' [draft: needs ' + ent.need + ']' : ' [draft]') : '';
		const cls = 'elabel' + (ent.id === selectedEnt || picked.has(ent.id) ? ' sel' : '') + (ent.draft ? ' draft' : '');
		has.add(ent.id);
		// half: half of how wide the line is, by its letters
		list.push({ id: ent.id, ent, cn, tn, tag, cls, key: cls + '\n' + cn + tn + '\n' + tag, half: (cn.length + tn.length + tag.length) * 3 + 4 });
	};
	for (const id of selectedIds()) if (list.length < ELABEL_ALWAYS) add(ents.get(id));
	for (const d of drafts.values()) if (list.length < ELABEL_ALWAYS && !has.has(d.id)) add(d);
	// with the markers off there is nothing else to name
	if (entLabel.markers.checked) {
		// How far: further when the camera is far from the point it turns around, which says nothing when it flies or
		// looks through the eyes of a player. In front of the camera: where the projection has a positive w.
		const f = $('entFilter').value.trim().toLowerCase(), m = viewProj, far = cam.fly || view.id >= 0 ? 1500 : Math.max(1500, cam.dist * 1.25), near = [];
		for (const ent of ents.values()) {
			const p = ent.pos, x = p[0] - eye[0], y = p[1] - eye[1], z = p[2] - eye[2], dd = x * x + y * y + z * z;
			if (dd > far * far || m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15] <= 0 || has.has(ent.id) || !entMatches(ent, f)) continue;
			near.push([dd, ent]);
		}
		near.sort((a, b) => a[0] - b[0]);
		for (const n of near.slice(0, ELABEL_NEAR)) add(n[1]);
	}
	entLabel.list = list;
}

function entLabelLine() {
	const el = document.createElement('div'), line = { el, cn: document.createElement('span'), tn: document.createElement('i'), tag: document.createElement('b'), key: '', x: -1, y: -1 };
	el.append(line.cn, line.tn, line.tag);
	entLabel.layer.appendChild(el);
	entLabel.pool.push(line);
	return line;
}

function placeEntLabels() {
	const L = entLabel, put = L.put;
	let n = 0;
	if (appMode === 'editor' && L.names.checked) {
		const now = performance.now(), w = canvas.clientWidth, h = canvas.clientHeight;
		if (now - L.at >= ELABEL_MS) { L.at = now; entLabelsChoose(); }
		for (const it of L.list) {
			const pos = ghost && ghost.id === it.id ? ghost.pos : it.ent.pos, sp = project([pos[0], pos[1], pos[2] + 14]);
			if (!sp || sp[0] < 0 || sp[1] < 0 || sp[0] > w || sp[1] > h) continue;
			// one that would lie over a line that is placed gives way: what always has a name comes first
			let i = 0;
			while (i < n && (Math.abs(put[i].x - sp[0]) >= put[i].it.half + it.half || Math.abs(put[i].y - sp[1]) >= 14)) i++;
			if (i < n) continue;
			const p = put[n] || (put[n] = { it: null, x: 0, y: 0 });
			p.it = it;
			p.x = Math.round(sp[0]);
			p.y = Math.round(sp[1]);
			n++;
		}
	}
	// Everything is read by now, and only now the page is written to: a read after a write makes the browser work
	// out the page again. And only what changed is written.
	for (let i = 0; i < n; i++) {
		const p = put[i], it = p.it, line = L.pool[i] || entLabelLine();
		if (line.key !== it.key) {
			line.key = it.key;
			line.el.className = it.cls;
			line.cn.textContent = it.cn;
			line.tn.textContent = it.tn;
			line.tag.textContent = it.tag;
		}
		if (line.x !== p.x || line.y !== p.y) {
			line.x = p.x;
			line.y = p.y;
			line.el.style.transform = 'translate(' + p.x + 'px,' + p.y + 'px) translate(-50%,-100%)';
		}
		if (i >= L.shown) line.el.style.display = '';
	}
	for (let i = n; i < L.shown; i++) L.pool[i].el.style.display = 'none';
	L.shown = n;
}
