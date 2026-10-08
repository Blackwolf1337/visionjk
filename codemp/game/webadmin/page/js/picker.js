/* ------------------------------------------------------------------ */
/* class picker with a rendered preview                                */
/* ------------------------------------------------------------------ */

let allClasses = [];
const classInfo = new Map();	// item classname -> { model, view, icon, description, type }
const ITEM_TYPES = ['', 'Weapon', 'Ammo', 'Armor', 'Health', 'Powerup', 'Holdable', 'Powerup', 'Team item'];

function classCategory(cls) {
	if (cls[0] === '@') return 'Template';
	const it = classInfo.get(cls);
	if (it) return ITEM_TYPES[it.type] || 'Item';
	const cats = [[/^npc_/i, 'NPC'], [/^info_player/, 'Spawn point'], [/^info_/, 'Info'], [/^trigger_/, 'Trigger'], [/^func_/, 'Brush'],
		[/^target_/, 'Target'], [/^misc_model/, 'Model'], [/^misc_/, 'Misc'], [/^fx_/, 'Effect'], [/^light/, 'Light'], [/^team_/, 'Team']];
	for (const [re, name] of cats) if (re.test(cls)) return name;
	return '';
}

let pickerIndex = -1;
// the templates come first: "@name" in the list and in the field
function pickerMatches() {
	const f = $('newClass').value.trim().toLowerCase();
	return Object.keys(templates).sort().map(n => '@' + n).concat(allClasses).filter(c => !f || c.toLowerCase().includes(f)).slice(0, 400);
}
function renderPicker() {
	const list = pickerMatches();
	pickerIndex = Math.min(pickerIndex, list.length - 1);
	$('classPicker').innerHTML = list.map((c, i) => {
		const col = classColor(c).map(v => Math.round(v * 255)).join(',');
		return '<div class="cls' + (i === pickerIndex ? ' hl' : '') + '" data-cls="' + escapeHtml(c) + '" title="' + escapeHtml(classSummary(c)) + '"><i style="background:rgb(' + col + ')"></i>' +
			escapeHtml(c) + '<span class="ty">' + escapeHtml(classCategory(c)) + '</span></div>';
	}).join('') || '<div class="cls">' + (classesLoaded ? 'No class matches' : 'Loading...') + '</div>';
}
function openPicker() { $('classPicker').classList.add('open'); renderPicker(); }
function closePicker() { $('classPicker').classList.remove('open'); hidePreview(); pickerIndex = -1; }
function choose(cls) {
	$('newClass').value = cls;
	closePicker();
	$('newClass').blur();
	setPlacing(true);
	toast('Right-click on the map to place ' + cls);
}

$('newClass').addEventListener('focus', openPicker);
$('newClass').addEventListener('input', () => { pickerIndex = -1; openPicker(); setPlacing(false); });
$('newClass').addEventListener('blur', () => setTimeout(closePicker, 150));
$('newClass').addEventListener('keydown', e => {
	const list = pickerMatches();
	if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
		e.preventDefault();
		pickerIndex = Math.max(0, Math.min(list.length - 1, pickerIndex + (e.key === 'ArrowDown' ? 1 : -1)));
		renderPicker();
		const row = $('classPicker').children[pickerIndex];
		if (row) { row.scrollIntoView({ block: 'nearest' }); showPreview(list[pickerIndex], row); }
	} else if (e.key === 'Enter') {
		e.preventDefault();
		const cls = pickerIndex >= 0 ? list[pickerIndex] : $('newClass').value.trim();
		if (cls) choose(cls);
	} else if (e.key === 'Escape') {
		closePicker();
		$('newClass').blur();
	}
});
$('classPicker').addEventListener('mousedown', e => e.preventDefault());	// keep the focus in the input
$('classPicker').addEventListener('click', e => {
	const row = e.target.closest('.cls[data-cls]');
	if (!row) return;
	// Shift and a click takes a template away
	if (e.shiftKey && row.dataset.cls[0] === '@') { delete templates[row.dataset.cls.slice(1)]; templatesKeep(); renderPicker(); return; }
	choose(row.dataset.cls);
});
let hoverTimer = 0;
$('classPicker').addEventListener('mouseover', e => {
	const row = e.target.closest('.cls[data-cls]');
	if (!row) return;
	clearTimeout(hoverTimer);
	hoverTimer = setTimeout(() => showPreview(row.dataset.cls, row), 80);
});
$('classPicker').addEventListener('mouseleave', () => { clearTimeout(hoverTimer); hidePreview(); });

/* model loading: MD3 and Ghoul2 GLM, first frame / bind pose */

function readStr(dv, o, len) {
	let s = '';
	for (let i = 0; i < len; i++) { const c = dv.getUint8(o + i); if (!c) break; s += String.fromCharCode(c); }
	return s;
}

function parseMd3(buf) {
	const dv = new DataView(buf), surfs = [];
	if (readStr(dv, 0, 4) !== 'IDP3') return null;
	let o = dv.getInt32(100, true);
	for (let s = 0, n = dv.getInt32(84, true); s < n; s++) {
		const name = readStr(dv, o + 4, 64), numShaders = dv.getInt32(o + 76, true), nv = dv.getInt32(o + 80, true), nt = dv.getInt32(o + 84, true);
		const ofsTri = dv.getInt32(o + 88, true), ofsSh = dv.getInt32(o + 92, true), ofsSt = dv.getInt32(o + 96, true), ofsXyz = dv.getInt32(o + 100, true);
		const pos = new Float32Array(nv * 3), norm = new Float32Array(nv * 3), uv = new Float32Array(nv * 2), idx = new Uint32Array(nt * 3);
		for (let v = 0; v < nv; v++) {
			const b = o + ofsXyz + v * 8;
			pos[v * 3] = dv.getInt16(b, true) / 64; pos[v * 3 + 1] = dv.getInt16(b + 2, true) / 64; pos[v * 3 + 2] = dv.getInt16(b + 4, true) / 64;
			const lat = dv.getUint8(b + 7) * Math.PI * 2 / 255, lng = dv.getUint8(b + 6) * Math.PI * 2 / 255;
			norm[v * 3] = Math.cos(lat) * Math.sin(lng); norm[v * 3 + 1] = Math.sin(lat) * Math.sin(lng); norm[v * 3 + 2] = Math.cos(lng);
			uv[v * 2] = dv.getFloat32(o + ofsSt + v * 8, true); uv[v * 2 + 1] = dv.getFloat32(o + ofsSt + v * 8 + 4, true);
		}
		for (let i = 0; i < nt * 3; i++) idx[i] = dv.getInt32(o + ofsTri + i * 4, true);
		surfs.push({ name, shader: numShaders ? readStr(dv, o + ofsSh, 64) : '', pos, norm, uv, idx });
		o += dv.getInt32(o + 104, true);
	}
	return surfs;
}

function parseGlm(buf) {
	const dv = new DataView(buf), surfs = [];
	if (readStr(dv, 0, 4) !== '2LGM') return null;
	const numSurfaces = dv.getInt32(152, true), ofsLods = dv.getInt32(148, true), hier = 164;
	const info = [];
	for (let i = 0; i < numSurfaces; i++) {
		const e = hier + dv.getInt32(hier + i * 4, true);
		info.push({ name: readStr(dv, e, 64), flags: dv.getUint32(e + 64, true), shader: readStr(dv, e + 68, 64) });
	}
	// LOD 0 only
	for (let i = 0; i < numSurfaces; i++) {
		const s = ofsLods + 4 + dv.getInt32(ofsLods + 4 + i * 4, true);
		const h = info[dv.getInt32(s + 4, true)] || info[i];
		if ((h.flags & 2) || h.name.startsWith('*') || /_off$/i.test(h.name)) continue;
		const nv = dv.getInt32(s + 12, true), ofsV = dv.getInt32(s + 16, true), nt = dv.getInt32(s + 20, true), ofsT = dv.getInt32(s + 24, true);
		const pos = new Float32Array(nv * 3), norm = new Float32Array(nv * 3), uv = new Float32Array(nv * 2), idx = new Uint32Array(nt * 3);
		for (let v = 0; v < nv; v++) {
			const b = s + ofsV + v * 32;
			for (let k = 0; k < 3; k++) { norm[v * 3 + k] = dv.getFloat32(b + k * 4, true); pos[v * 3 + k] = dv.getFloat32(b + 12 + k * 4, true); }
			const t = s + ofsV + nv * 32 + v * 8;
			uv[v * 2] = dv.getFloat32(t, true); uv[v * 2 + 1] = dv.getFloat32(t + 4, true);
		}
		for (let k = 0; k < nt * 3; k++) idx[k] = dv.getInt32(s + ofsT + k * 4, true);
		surfs.push({ name: h.name, shader: h.shader, pos, norm, uv, idx });
	}
	return surfs;
}

// .skin files: "surface,texture" per line
async function loadSkin(modelPath) {
	try {
		const res = await fetch('api/file?name=' + encodeURIComponent(modelPath.replace(/[^/]*$/, 'model_default.skin')));
		if (!res.ok) return {};
		const map = {};
		for (const line of (await res.text()).split(/\r?\n/)) {
			const [surf, tex] = line.split(',').map(x => x && x.trim());
			if (surf && tex) map[surf.toLowerCase()] = tex;
		}
		return map;
	} catch (e) { return {}; }
}

/* preview renderer, its own WebGL context */

const pv = { gl: null, prog: null, models: new Map(), textures: new Map(), model: null, cls: '', visible: false, t0: 0 };

function pvSetup() {
	if (pv.gl) return true;
	const g = $('previewGl').getContext('webgl2', { antialias: true, premultipliedAlpha: false });
	if (!g) return false;
	pv.gl = g;
	pv.prog = compile(`#version 300 es
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNorm; layout(location=2) in vec2 aUv;
uniform mat4 uMvp; uniform mat4 uModel; out vec3 vN; out vec2 vUv;
void main() { vN = mat3(uModel) * aNorm; vUv = aUv; gl_Position = uMvp * uModel * vec4(aPos, 1.0); }`,
	`#version 300 es
precision mediump float;
in vec3 vN; in vec2 vUv; uniform sampler2D uTex; out vec4 o;
void main() {
	// model textures use alpha for shine masks, so no alpha test here
	vec4 t = texture(uTex, vUv);
	vec3 n = normalize(vN);
	float l = 0.45 + 0.65 * max(dot(n, normalize(vec3(0.5, -0.4, 0.75))), 0.0) + 0.2 * max(dot(n, normalize(vec3(-0.6, 0.5, 0.2))), 0.0);
	o = vec4(t.rgb * l, 1.0);
}`, g);
	pv.gray = pvTexture(1, 1, new Uint8Array([170, 170, 170, 255]));
	return true;
}

function pvTexture(w, h, data) {
	const g = pv.gl, t = g.createTexture();
	g.bindTexture(g.TEXTURE_2D, t);
	g.texImage2D(g.TEXTURE_2D, 0, g.RGBA8, w, h, 0, g.RGBA, g.UNSIGNED_BYTE, data);
	g.generateMipmap(g.TEXTURE_2D);
	g.texParameteri(g.TEXTURE_2D, g.TEXTURE_MIN_FILTER, g.LINEAR_MIPMAP_LINEAR);
	return t;
}

async function pvLoadTexture(name) {
	const base = name.replace(/\.(tga|jpg|png)$/i, '');
	if (pv.textures.has(base)) return pv.textures.get(base);
	const g = pv.gl;
	let tex = pv.gray;
	for (const ext of ['.jpg', '.png', '.tga']) {
		try {
			const res = await fetch('api/file?name=' + encodeURIComponent(base + ext));
			if (!res.ok) continue;
			const data = await res.arrayBuffer();
			if (ext === '.tga') {
				const img = decodeTga(data);
				if (!img) continue;
				tex = pvTexture(img.w, img.h, img.data);
			} else {
				const bmp = await createImageBitmap(new Blob([data]));
				tex = g.createTexture();
				g.bindTexture(g.TEXTURE_2D, tex);
				g.texImage2D(g.TEXTURE_2D, 0, g.RGBA8, g.RGBA, g.UNSIGNED_BYTE, bmp);
				g.generateMipmap(g.TEXTURE_2D);
				g.texParameteri(g.TEXTURE_2D, g.TEXTURE_MIN_FILTER, g.LINEAR_MIPMAP_LINEAR);
			}
			break;
		} catch (e) { /* next extension */ }
	}
	pv.textures.set(base, tex);
	return tex;
}

// the best model file of an item: an MD3 if there is one, else the GLM
function itemModelPath(it) {
	const md3 = [it.model, it.view].find(m => /\.md3$/i.test(m || ''));
	return md3 || it.model || it.view || '';
}

async function pvLoadModel(path) {
	if (pv.models.has(path)) return pv.models.get(path);
	const promise = (async () => {
		const res = await fetch('api/file?name=' + encodeURIComponent(path));
		if (!res.ok) return null;
		const buf = await res.arrayBuffer();
		const surfs = /\.glm$/i.test(path) ? parseGlm(buf) : parseMd3(buf);
		if (!surfs || !surfs.length) return null;
		const skin = /\.glm$/i.test(path) ? await loadSkin(path) : {};
		const g = pv.gl, meshes = [];
		const mins = [Infinity, Infinity, Infinity], maxs = [-Infinity, -Infinity, -Infinity];
		for (const s of surfs) {
			let shader = skin[s.name.toLowerCase()] || s.shader;
			if (!shader || shader === '[nomaterial]' || /^\*off$/i.test(shader)) {
				if (skin[s.name.toLowerCase()] === '*off') continue;
				if (!shader || shader === '[nomaterial]') shader = '';
			}
			for (let v = 0; v < s.pos.length; v += 3) for (let k = 0; k < 3; k++) { mins[k] = Math.min(mins[k], s.pos[v + k]); maxs[k] = Math.max(maxs[k], s.pos[v + k]); }
			const vao = g.createVertexArray();
			g.bindVertexArray(vao);
			[[0, s.pos, 3], [1, s.norm, 3], [2, s.uv, 2]].forEach(([loc, data, size]) => {
				const b = g.createBuffer(); g.bindBuffer(g.ARRAY_BUFFER, b); g.bufferData(g.ARRAY_BUFFER, data, g.STATIC_DRAW);
				g.enableVertexAttribArray(loc); g.vertexAttribPointer(loc, size, g.FLOAT, false, 0, 0);
			});
			const ib = g.createBuffer(); g.bindBuffer(g.ELEMENT_ARRAY_BUFFER, ib); g.bufferData(g.ELEMENT_ARRAY_BUFFER, s.idx, g.STATIC_DRAW);
			g.bindVertexArray(null);
			meshes.push({ vao, count: s.idx.length, tex: shader ? await pvLoadTexture(shader) : pv.gray });
		}
		const center = mins.map((m, k) => (m + maxs[k]) / 2);
		const radius = Math.max(1, Math.hypot(maxs[0] - mins[0], maxs[1] - mins[1], maxs[2] - mins[2]) / 2);
		return { meshes, center, radius, size: maxs.map((m, k) => m - mins[k]) };
	})().catch(() => null);
	pv.models.set(path, promise);
	return promise;
}

// The preview box next to a row: the model at path, or a line that says why there is none. token tells the
// call that is shown from one that came later.
async function pvShowPath(token, row, name, subText, path, none) {
	const box = $('preview'), r = row.getBoundingClientRect();
	box.style.top = Math.max(50, Math.min(window.innerHeight - 290, r.top - 100)) + 'px';
	box.classList.add('open');
	pv.cls = token;
	pv.model = null;
	$('pvName').textContent = name;
	$('pvSub').textContent = subText;
	const showNone = text => { $('previewGl').style.display = 'none'; $('pvNone').style.display = ''; $('pvNone').textContent = text; };
	if (!path || !pvSetup()) {
		showNone(none);
		return;
	}
	$('previewGl').style.display = '';
	$('pvNone').style.display = 'none';
	const model = await pvLoadModel(path);
	if (pv.cls !== token) return;
	if (!model) { showNone('Model not found: ' + path); return; }
	pv.model = model;
	if (!pv.visible) { pv.visible = true; pv.t0 = performance.now(); requestAnimationFrame(pvFrame); }
}
function showPreview(cls, row) {
	const it = classInfo.get(cls);
	return pvShowPath(cls, row, cls, it ? (ITEM_TYPES[it.type] || 'Item') + (it.quantity ? ' · ' + it.quantity : '') + (it.description && it.description[0] !== '@' ? ' · ' + it.description : '') : (classCategory(cls) || 'Entity'), it ? itemModelPath(it) : '',
		/^npc_/i.test(cls) ? 'NPC, the model comes from its .npc file' : 'No model, this entity is game logic');
}

function hidePreview() {
	$('preview').classList.remove('open');
	pv.cls = '';
	pv.visible = false;
}

function pvFrame(now) {
	if (!pv.visible) return;
	const g = pv.gl, m = pv.model;
	g.viewport(0, 0, g.canvas.width, g.canvas.height);
	g.clearColor(0, 0, 0, 0);
	g.clear(g.COLOR_BUFFER_BIT | g.DEPTH_BUFFER_BIT);
	if (m) {
		g.enable(g.DEPTH_TEST);
		g.useProgram(pv.prog.p);
		const r = m.radius, yaw = (now - pv.t0) / 1400;
		const proj = M4.perspective(32 * Math.PI / 180, g.canvas.width / g.canvas.height, r * 0.1, r * 20);
		const view = M4.lookAt([r * 2.4, -r * 1.2, r * 1.3], [0, 0, 0], [0, 0, 1]);
		const c = Math.cos(yaw), s = Math.sin(yaw);
		// spin around the model's center
		const model = new Float32Array([c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0,
			-(c * m.center[0] - s * m.center[1]), -(s * m.center[0] + c * m.center[1]), -m.center[2], 1]);
		g.uniformMatrix4fv(pv.prog.u.uMvp, false, M4.mul(proj, view));
		g.uniformMatrix4fv(pv.prog.u.uModel, false, model);
		g.uniform1i(pv.prog.u.uTex, 0);
		g.activeTexture(g.TEXTURE0);
		for (const mesh of m.meshes) {
			g.bindTexture(g.TEXTURE_2D, mesh.tex);
			g.bindVertexArray(mesh.vao);
			g.drawElements(g.TRIANGLES, mesh.count, g.UNSIGNED_INT, 0);
		}
	}
	requestAnimationFrame(pvFrame);
}


