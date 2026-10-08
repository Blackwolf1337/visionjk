/* ------------------------------------------------------------------ */
/* the files of the game folder                                        */
/* ------------------------------------------------------------------ */

// what a click opens in the text field instead of downloading it
const FS_TEXT = new Set(['cfg', 'txt', 'log', 'bak', 'ent', 'json', 'md', 'ini', 'csv', 'dat', 'shader', 'skin', 'npc', 'sab', 'veh', 'vwp', 'bot', 'arena', 'menu', 'str', 'efx', 'sp']);
// a request is 64 KB at most on the server, head and body
const FS_CHUNK = 48 * 1024, FS_EDIT_MAX = 60000;
let fsRoot = 'home', fsPath = '', fsWritable = true, fsEntries = [], fsEdit = null;

function fsExt(name) { const i = name.lastIndexOf('.'); return i < 0 ? '' : name.slice(i + 1).toLowerCase(); }
function fsJoin(dir, name) { return dir ? dir + '/' + name : name; }
function fsSize(n) { return n < 1024 ? n + ' B' : n < 1048576 ? (n / 1024).toFixed(1) + ' KB' : (n / 1048576).toFixed(1) + ' MB'; }
function fsUrl(op, params) { return 'api/fs/' + op + '?' + new URLSearchParams(params); }
async function fsPost(op, params, body = '') {
	try {
		return await (await fetch(fsUrl(op, params), { method: 'POST', body })).json();
	} catch (e) {
		return { ok: false, message: 'Request failed: ' + e.message };
	}
}

async function fsList(path = fsPath) {
	try {
		const json = await (await fetch(fsUrl('list', { root: fsRoot, path }), { cache: 'no-store' })).json();
		if (!json.ok) { toast(json.message, false); return false; }
		fsPath = json.path;
		fsWritable = json.writable;
		fsEntries = json.entries.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
		fsRender();
		return true;
	} catch (e) {
		toast('Could not read the folder: ' + e.message, false);
		return false;
	}
}
function fsRender() {
	$('fsPath').textContent = (fsRoot === 'home' ? 'server folder' : 'installation') + ' / ' + fsPath;
	$('fsCount').textContent = fsEntries.length;
	$('fsMkdir').disabled = $('fsUpload').disabled = !fsWritable;
	$('fsUp').disabled = !fsPath;
	$('fsRows').innerHTML = fsEntries.map((e, i) => {
		// a link and a name the server does not take in a request are only shown
		const usable = !e.odd && !e.link;
		const acts = !usable ? '' :
			(e.dir ? '' : '<button data-fs="get" data-i="' + i + '">Download</button>') +
			(fsWritable && !e.code ? '<button data-fs="rename" data-i="' + i + '">Rename</button><button data-fs="delete" data-i="' + i + '">Delete</button>' : '');
		return '<tr' + (usable ? ' data-fs="open" data-i="' + i + '"' : ' class="off"') + '><td>' +
			(e.dir ? '<span class="tag">folder</span> ' : '') + escapeHtml(e.name) +
			(e.link ? ' <span class="tag">link</span>' : '') + (e.code ? ' <span class="tag">program</span>' : '') +
			'</td><td class="num">' + (e.dir || e.link ? '' : fsSize(e.size)) + '</td><td>' + (e.link ? '' : new Date(e.time * 1000).toLocaleString()) +
			'</td><td class="acts">' + acts + '</td></tr>';
	}).join('') || '<tr class="off"><td colspan="4">empty</td></tr>';
}

function fsDownload(path) {
	// the browser sends the login of the panel with it, the server answers with a file to save
	const a = document.createElement('a');
	a.href = fsUrl('get', { root: fsRoot, path });
	a.download = '';
	document.body.appendChild(a);
	a.click();
	a.remove();
}
async function fsRename(e) {
	const to = prompt('New name of ' + e.name + ' (with a/ in front it goes into the folder a)', e.name);
	if (!to || to === e.name) return;
	const r = await fsPost('rename', { path: fsJoin(fsPath, e.name), to: fsJoin(fsPath, to) });
	toast(r.message, r.ok);
	fsList();
}
async function fsDelete(e) {
	if (!confirm('Delete ' + e.name + (e.dir ? ' (it has to be empty)' : '') + '?')) return;
	const r = await fsPost('delete', { path: fsJoin(fsPath, e.name) });
	toast(r.message, r.ok);
	fsList();
}
async function fsMkdir() {
	const name = prompt('Name of the new folder');
	if (!name) return;
	const r = await fsPost('mkdir', { path: fsJoin(fsPath, name) });
	toast(r.message, r.ok);
	fsList();
}
// a file goes up in pieces, one after the other: each names where it begins, the last one puts the file in its place
async function fsUploadFiles(files) {
	const dir = fsPath;
	let pk3 = false;
	for (const file of files) {
		if (fsEntries.some(e => e.name === file.name) && !confirm(file.name + ' is there already. Replace it?')) continue;
		let offset = 0, ok = true;
		do {
			const piece = file.slice(offset, offset + FS_CHUNK), last = offset + piece.size >= file.size;
			$('fsProgress').textContent = file.name + ': ' + fsSize(offset) + ' of ' + fsSize(file.size);
			const r = await fsPost('put', { path: fsJoin(dir, file.name), offset, last: last ? 1 : 0 }, piece);
			if (!r.ok) { toast(file.name + ': ' + r.message, false); ok = false; break; }
			offset += piece.size;
		} while (offset < file.size);
		if (ok) toast(file.name + ' uploaded');
		if (ok && fsExt(file.name) === 'pk3') pk3 = true;
	}
	$('fsProgress').textContent = '';
	fsList();
	if (pk3) fsReloadMap('A pk3 was uploaded. The server only looks for pk3 files when a map starts.\n\n');
}
function fsReloadMap(why = '') {
	if (confirm(why + 'Start the running map again now? The players stay connected, what was placed in the World tab is put back where it is.')) action({ action: 'reload' });
}

async function fsOpenText(path) {
	try {
		const res = await fetch(fsUrl('get', { root: fsRoot, path, text: 1 }), { cache: 'no-store' });
		if (!res.ok) throw new Error(await res.text());
		const text = await res.text();
		// a file of the installation is saved under the same name in the server folder, where the engine looks first
		fsEdit = { path, saved: text, colored: fsExt(path) === 'cfg', copy: fsRoot !== 'home' };
		$('fsText').value = text;
		$('fsText').scrollTop = $('fsText').scrollLeft = 0;
		fsShowEdit();
	} catch (e) {
		toast('Could not read the file: ' + e.message, false);
	}
}
function fsShowEdit() {
	const on = !!fsEdit;
	$('fsEditor').hidden = !on;
	$('fsList').hidden = on;
	$('fsEditTools').hidden = !on;
	$('fsEditState').textContent = '';
	if (!on) return;
	const dirty = $('fsText').value !== fsEdit.saved;
	codePaint('fsText', 'fsHi', fsEdit.colored);
	$('fsEditState').textContent = fsEdit.path + (dirty ? ': changed, not saved' : ': as on the server') +
		(fsEdit.copy ? ' (of the installation, Save writes it into the server folder)' : '');
	$('fsEditState').classList.toggle('dirty', dirty);
}
async function fsSave() {
	const text = $('fsText').value.replace(/\r/g, '');
	if (cfgBytes(text) > FS_EDIT_MAX) { toast('The text is larger than ' + FS_EDIT_MAX + ' bytes, the server does not take it in one piece', false); return; }
	const r = await fsPost('put', { path: fsEdit.path, offset: 0, last: 1, text: 1 }, text);
	toast(r.message, r.ok);
	if (r.ok) fsEdit.saved = text;
	fsShowEdit();
}
function fsCloseEdit() {
	if (fsEdit && $('fsText').value !== fsEdit.saved && !confirm('Drop what was changed in ' + fsEdit.path + '?')) return;
	fsEdit = null;
	fsShowEdit();
	fsList();
}

$('fsRows').addEventListener('click', ev => {
	const el = ev.target.closest('[data-fs]');
	if (!el) return;
	const e = fsEntries[+el.dataset.i], path = fsJoin(fsPath, e.name);
	switch (el.dataset.fs) {
	case 'open':
		if (e.dir) fsList(path);
		else if (FS_TEXT.has(fsExt(e.name)) && e.size <= FS_EDIT_MAX) fsOpenText(path);
		else fsDownload(path);
		break;
	case 'get': fsDownload(path); break;
	case 'rename': fsRename(e); break;
	case 'delete': fsDelete(e); break;
	}
});
$('fsRoot').addEventListener('change', async () => {
	const before = fsRoot;
	fsRoot = $('fsRoot').value;
	if (!await fsList('')) { fsRoot = $('fsRoot').value = before; }
});
$('fsUp').addEventListener('click', () => fsList(fsPath.includes('/') ? fsPath.slice(0, fsPath.lastIndexOf('/')) : ''));
$('fsRefresh').addEventListener('click', () => fsList());
$('fsMkdir').addEventListener('click', fsMkdir);
$('fsUpload').addEventListener('click', () => $('fsFile').click());
$('fsReload').addEventListener('click', () => fsReloadMap());
$('fsFile').addEventListener('change', () => { const files = [...$('fsFile').files]; $('fsFile').value = ''; if (files.length) fsUploadFiles(files); });
$('fsText').addEventListener('input', fsShowEdit);
$('fsText').addEventListener('scroll', () => codeScroll('fsText', 'fsHi'));
$('fsSave').addEventListener('click', fsSave);
$('fsClose').addEventListener('click', fsCloseEdit);
document.querySelectorAll('#tabs button').forEach(b => b.addEventListener('click', () => setMode(b.dataset.tab)));
document.querySelectorAll('#appMode button').forEach(b => b.addEventListener('click', () => setAppMode(b.dataset.app)));

