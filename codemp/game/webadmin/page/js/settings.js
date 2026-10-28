/* ------------------------------------------------------------------ */
/* the settings: the form, the switch to the text, the cleanup         */
/* ------------------------------------------------------------------ */

// The Settings tab shows one file three ways (wa_settings.c): a form with every setting of the server, the text of
// the file (config.js), and what the file sets that no code uses. The form and the text are the same file, so a
// save in one makes the other read it again.

let cfgView = 'form';
let setData = null;			// GET api/settings
let setLoaded = false;
const setEdits = new Map();	// name -> value typed, not saved
let cleanData = null;

const SET_APPLY = { live: '', map: 'next map', restart: 'restart' };

try { if (['form', 'text', 'cleanup'].includes(localStorage.getItem('cfgView'))) cfgView = localStorage.getItem('cfgView'); } catch (e) { /* the form */ }

function setDirty() { return setEdits.size > 0; }
function cfgTextDirty() { return cfgLoaded && $('cfgText').value !== cfgSaved; }

function cfgShowView(v) {
	if (v === cfgView) return;
	if (cfgView === 'form' && setDirty() && !confirm('Drop the settings changed in the form?')) return;
	if (cfgView === 'text' && cfgTextDirty() && !confirm('Drop what was typed in the text?')) return;
	if (cfgView === 'form') setEdits.clear();
	if (cfgView === 'text' && cfgTextDirty()) $('cfgText').value = cfgSaved;
	cfgView = v;
	try { localStorage.setItem('cfgView', v); } catch (e) { /* asked again next time */ }
	cfgOpen();
}

// what the tab shows when it is opened, or the view changes
function cfgOpen() {
	document.querySelectorAll('#cfgSwitch button').forEach(b => b.classList.toggle('on', b.dataset.view === cfgView));
	document.querySelectorAll('#pane-config .cfgSide, #cfgView .cfgMain').forEach(e => { e.hidden = e.dataset.view !== cfgView; });
	if (cfgView === 'form' && !setLoaded) setLoad();
	if (cfgView === 'text' && !cfgLoaded) loadConfig();
	if (cfgView === 'cleanup') cleanLoad();
	$('cfgSize').textContent = cfgView === 'text' && cfgLoaded ? cfgBytes($('cfgText').value) + ' bytes' : '';
}

// a save of the text: the form reads the file again when it is shown
function setStale() { setLoaded = false; cleanData = null; }

/* the form */

async function setLoad() {
	try {
		const res = await fetch('api/settings', { cache: 'no-store' });
		if (!res.ok) throw new Error('HTTP ' + res.status);
		setData = await res.json();
		setLoaded = true;
		setEdits.clear();
		$('cfgFileName').textContent = setData.file;
	} catch (e) {
		toast('Could not read the settings: ' + e.message, false);
	}
	setRender();
}

// "0=Free for all|1=Holocron" -> [['0', 'Free for all'], ...]
function setOptions(text) {
	return (text || '').split('|').map(o => { const i = o.indexOf('='); return [o.slice(0, i), o.slice(i + 1)]; });
}

function setValueOf(cv) { return setEdits.has(cv.name) ? setEdits.get(cv.name) : cv.value; }

function setInput(cv) {
	const v = setValueOf(cv), name = escapeHtml(cv.name);
	const range = (cv.min !== undefined ? ' min="' + cv.min + '"' : '') + (cv.max !== undefined && cv.max > cv.min ? ' max="' + cv.max + '"' : '');
	switch (cv.type) {
	case 'bool':
		return '<label class="setBool"><input type="checkbox" data-set="' + name + '"' + (v !== '0' && v !== '' ? ' checked' : '') + '><span>' + (v !== '0' && v !== '' ? 'On' : 'Off') + '</span></label>';
	case 'int':
	case 'float':
		return '<input type="number" data-set="' + name + '" value="' + escapeHtml(v) + '"' + range + (cv.type === 'float' ? ' step="any"' : '') + '>';
	case 'enum': {
		const opts = setOptions(cv.opts);
		let html = '<select data-set="' + name + '">';
		if (!opts.some(o => o[0] === v)) html += '<option value="' + escapeHtml(v) + '" selected>' + escapeHtml(v) + ' (not a choice)</option>';
		for (const [k, label] of opts) html += '<option value="' + escapeHtml(k) + '"' + (k === v ? ' selected' : '') + '>' + escapeHtml(k + ': ' + label) + '</option>';
		return html + '</select>';
	}
	case 'bits': {
		const n = parseInt(v, 10) || 0;
		let html = '<input type="number" data-set="' + name + '" value="' + escapeHtml(v) + '" min="0"><div class="setBits">';
		for (const [bit, label] of setOptions(cv.opts))
			html += '<label><input type="checkbox" data-bit="' + bit + '" data-of="' + name + '"' + ((n >>> bit) & 1 ? ' checked' : '') + '>' + escapeHtml(label) + '</label>';
		return html + '</div>';
	}
	case 'secret':
		return '<span class="inline"><input type="password" data-set="' + name + '" value="' + escapeHtml(v) + '" autocomplete="off"><button type="button" data-show="' + name + '" title="Shows or hides the password">Show</button></span>';
	default:
		return '<input type="text" data-set="' + name + '" value="' + escapeHtml(v) + '" autocomplete="off">';
	}
}

function setMarks(cv) {
	const marks = [];
	if (setEdits.has(cv.name)) marks.push('<b class="mk changed">changed</b>');
	if (SET_APPLY[cv.apply]) marks.push('<b class="mk">' + SET_APPLY[cv.apply] + '</b>');
	if (cv.file === undefined) marks.push('<b class="mk muted" title="The file does not set it, the value comes from elsewhere or is the default">not in file</b>');
	else if (cv.file !== cv.value) marks.push('<b class="mk warn val" title="What the file sets is not what the server has now">file: ' + escapeHtml(cv.file) + '</b>');
	if (cv.local !== undefined) marks.push('<b class="mk warn val" title="local.cfg runs after the file and sets it again at the next start">local.cfg: ' + escapeHtml(cv.local) + '</b>');
	if (cv.def && setValueOf(cv) === cv.def) marks.push('<b class="mk muted">default</b>');
	return marks.join('');
}

// the left half of a row: the name, the marks, what it does
function setNameHtml(cv) {
	return '<code>' + escapeHtml(cv.name) + '</code>' + setMarks(cv) +
		(cv.desc ? '<div class="setDesc">' + escapeHtml(cv.desc) + '</div>' : '') +
		(cv.def ? '<div class="setDef">Default ' + escapeHtml(cv.def) + '</div>' : '');
}

function setMatches(cv, q) {
	return !q || cv.name.toLowerCase().includes(q) || (cv.desc || '').toLowerCase().includes(q);
}

function setRender() {
	const form = $('setForm'), cats = $('setCats');
	if (!setData) { form.innerHTML = ''; cats.innerHTML = ''; setShowState(); return; }
	const q = $('setSearch').value.trim().toLowerCase();
	const groups = setData.categories.map(() => []);
	for (const cv of setData.cvars) if (setMatches(cv, q)) groups[cv.cat].push(cv);

	let html = '', side = '';
	groups.forEach((list, cat) => {
		if (!list.length) return;
		const changed = list.filter(cv => setEdits.has(cv.name)).length;
		side += '<div class="setCat" data-cat="' + cat + '"><span>' + escapeHtml(setData.categories[cat]) + '</span><span class="n">' + (changed ? '<b>' + changed + '</b> / ' : '') + list.length + '</span></div>';
		html += '<section id="setCat' + cat + '"><h3>' + escapeHtml(setData.categories[cat]) + '</h3>';
		for (const cv of list) {
			html += '<div class="setRow' + (setEdits.has(cv.name) ? ' changed' : '') + '" data-row="' + escapeHtml(cv.name) + '">' +
				'<div class="setName">' + setNameHtml(cv) + '</div><div class="setField">' + setInput(cv) + '</div></div>';
		}
		html += '</section>';
	});
	if (!html) html = '<div class="hint" style="padding:12px">' + (q ? 'No setting matches.' : 'No settings.') + '</div>';
	if (!setData.flags) html = '<div class="hint" style="padding:12px 12px 0">This engine does not tell the panel which settings take effect with the next map or a restart: the marks of the cvars of the engine may be missing.</div>' + html;
	form.innerHTML = html;
	cats.innerHTML = side;
	setShowState();
}

function setShowState() {
	const n = setEdits.size;
	$('setState').textContent = !setLoaded ? 'not loaded' : n ? n + (n === 1 ? ' setting changed, not saved' : ' settings changed, not saved') : 'as on the server';
	$('setState').classList.toggle('dirty', n > 0);
	$('setSave').disabled = !n;
}

// a field changed: kept as an edit, unless it is back at what the server has
function setEdit(name, value) {
	const cv = setData.cvars.find(c => c.name === name);
	if (!cv) return;
	if (value === cv.value) setEdits.delete(name); else setEdits.set(name, value);
	const row = document.querySelector('.setRow[data-row="' + CSS.escape(name) + '"]');
	if (row) {
		row.classList.toggle('changed', setEdits.has(name));
		row.querySelector('.setName').innerHTML = setNameHtml(cv);
	}
	// the counts in the list of groups
	const cat = document.querySelector('.setCat[data-cat="' + cv.cat + '"] .n');
	if (cat) {
		const list = setData.cvars.filter(c => c.cat === cv.cat && setMatches(c, $('setSearch').value.trim().toLowerCase()));
		const changed = list.filter(c => setEdits.has(c.name)).length;
		cat.innerHTML = (changed ? '<b>' + changed + '</b> / ' : '') + list.length;
	}
	setShowState();
}

async function setSave() {
	if (!setEdits.size) return;
	for (const v of setEdits.values()) {
		if (/["\n\r\t]/.test(v)) { toast('A value may have no quotes, tabs or line breaks', false); return; }
	}
	const body = [...setEdits].map(([n, v]) => n + '\t' + v).join('\n');
	const json = await apiPost('api/settings', body);
	if (!json.results) { toast(json.message, false); return; }
	const refused = json.results.filter(r => r.state === 'refused');
	const later = json.results.filter(r => r.state === 'map' || r.state === 'restart');
	let msg = json.message;
	if (later.length) msg += '. ' + later.map(r => r.name + ' (' + SET_APPLY[r.state] + ')').join(', ');
	if (refused.length) msg += '. Refused: ' + refused.map(r => r.name + ': ' + r.why).join(', ');
	toast(msg, !refused.length);
	// the text editor reads the file again next time
	cfgLoaded = false;
	cleanData = null;
	// what the server has now, a moment after the console ran the lines
	setTimeout(async () => {
		const keep = new Map(refused.map(r => [r.name, setEdits.get(r.name)]));
		await setLoad();
		for (const [n, v] of keep) if (v !== undefined) setEdits.set(n, v);
		if (keep.size) setRender();
	}, 300);
}

$('cfgSwitch').addEventListener('click', e => { const b = e.target.closest('button[data-view]'); if (b) cfgShowView(b.dataset.view); });
$('setSearch').addEventListener('input', setRender);
$('setSave').addEventListener('click', setSave);
$('setDiscard').addEventListener('click', () => {
	if (!setEdits.size || confirm('Drop the changed settings and read them again?')) setLoad();
});
$('setCats').addEventListener('click', e => {
	const c = e.target.closest('.setCat');
	if (c) $('setCat' + c.dataset.cat).scrollIntoView({ block: 'start' });
});
$('setForm').addEventListener('input', e => {
	const t = e.target;
	if (t.dataset.set !== undefined) {
		const value = t.type === 'checkbox' ? (t.checked ? '1' : '0') : t.value;
		if (t.type === 'checkbox') t.nextElementSibling.textContent = t.checked ? 'On' : 'Off';
		setEdit(t.dataset.set, value);
		// the boxes of a set of bits follow its number
		const n = parseInt(value, 10) || 0;
		document.querySelectorAll('[data-of="' + CSS.escape(t.dataset.set) + '"]').forEach(b => { b.checked = !!((n >>> b.dataset.bit) & 1); });
	} else if (t.dataset.of !== undefined) {
		const num = document.querySelector('[data-set="' + CSS.escape(t.dataset.of) + '"]');
		let n = parseInt(num.value, 10) || 0;
		n = t.checked ? (n | (1 << t.dataset.bit)) : (n & ~(1 << t.dataset.bit));
		num.value = String(n >>> 0);
		setEdit(t.dataset.of, num.value);
	}
});
$('setForm').addEventListener('click', e => {
	const b = e.target.closest('button[data-show]');
	if (!b) return;
	const input = document.querySelector('[data-set="' + CSS.escape(b.dataset.show) + '"]');
	input.type = input.type === 'password' ? 'text' : 'password';
	b.textContent = input.type === 'password' ? 'Show' : 'Hide';
});
window.addEventListener('beforeunload', e => { if (setDirty()) e.preventDefault(); });

/* the cleanup */

async function cleanLoad() {
	try {
		const res = await fetch('api/settings/cleanup', { cache: 'no-store' });
		if (!res.ok) throw new Error('HTTP ' + res.status);
		cleanData = await res.json();
	} catch (e) {
		toast('Could not read the list: ' + e.message, false);
		cleanData = null;
	}
	cleanRender();
}

function cleanRender() {
	const rows = $('cleanRows');
	if (!cleanData) { rows.innerHTML = ''; $('cleanState').textContent = ''; return; }
	if (!cleanData.available) {
		rows.innerHTML = '';
		$('cleanState').textContent = 'The engine of this server does not note which cvars are used: it needs a newer VisionJK.';
		return;
	}
	const items = cleanData.items;
	rows.innerHTML = items.map((it, i) => '<tr>' +
		'<td><input type="checkbox" data-clean="' + i + '"></td>' +
		'<td><code>' + escapeHtml(it.name) + '</code></td>' +
		'<td class="val">' + escapeHtml(it.value) + '</td>' +
		'<td>' + (it.group === 'overridden' ? '' : it.day ? (cleanData.today - it.day) + ' days ago' : 'never') + '</td>' +
		'<td>' + (it.line ? 'line ' + it.line : 'only on the server') + '</td>' +
		'<td><span class="mk' + (it.group === 'unused' ? ' warn' : ' muted') + '">' + it.group + '</span></td></tr>').join('') ||
		'<tr><td colspan="6" class="hint">Nothing to clean up.</td></tr>';
	cleanShowState();
}

function cleanPicked() {
	return [...document.querySelectorAll('#cleanRows input[data-clean]:checked')].map(b => cleanData.items[b.dataset.clean]);
}

function cleanShowState() {
	const picked = cleanData && cleanData.available ? cleanPicked().length : 0;
	if (cleanData && cleanData.available)
		$('cleanState').textContent = cleanData.items.length + ' found' + (picked ? ', ' + picked + ' selected' : '');
	$('cleanRemove').disabled = !picked;
}

async function cleanRemove() {
	const picked = cleanPicked();
	if (!picked.length) return;
	const names = [...new Set(picked.map(it => it.name))];
	if (!confirm('Remove these from ' + (setData ? setData.file : 'the config') + ' and the server?\n\n' + names.join(', '))) return;
	const body = cleanData.rev + '\n' + picked.map(it => it.line + '\t' + it.name + '\t' + (it.group === 'unused' ? 1 : 0)).join('\n');
	const json = await apiPost('api/settings/cleanup', body);
	toast(json.message, json.ok);
	cfgLoaded = false;
	setLoaded = false;
	// the server runs cvar_cleanup in its next frame
	setTimeout(cleanLoad, 300);
}

$('cleanRows').addEventListener('change', cleanShowState);
$('cleanAll').addEventListener('click', () => {
	const boxes = [...document.querySelectorAll('#cleanRows input[data-clean]')];
	const on = boxes.some(b => !b.checked);
	boxes.forEach(b => { b.checked = on; });
	cleanShowState();
});
$('cleanRemove').addEventListener('click', cleanRemove);
$('cleanLoad').addEventListener('click', cleanLoad);
