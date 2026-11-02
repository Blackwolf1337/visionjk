/* ------------------------------------------------------------------ */
/* the files of the game folder                                        */
/* ------------------------------------------------------------------ */

// what a click opens in the text field instead of downloading it
const FS_TEXT = new Set(['cfg', 'txt', 'log', 'bak', 'ent', 'json', 'md', 'ini', 'csv', 'dat', 'shader', 'skin', 'npc', 'sab', 'veh', 'vwp', 'bot', 'arena', 'menu', 'str', 'efx', 'sp']);
// a request is 64 KB at most on the server, head and body
const FS_CHUNK = 48 * 1024, FS_EDIT_MAX = 60000;
// The two folders the server shows. home: the game folder in its home path, where it writes and where every change
// goes. install: the game folder of the installation, which can only be read.
const FS_ROOTS = { home: 'Server files', install: 'Game installation' };
// the folder the list shows: its root, its path in it, what the server listed of it
let fsRoot = 'home', fsPath = '', fsWritable = true, fsEntries = [], fsEdit = null;
// The tree: what the server listed of a folder, by root and path, and which folders are open in it. A folder is
// asked for when it is opened the first time, and anew whenever the list shows it.
const fsCache = new Map(), fsOpen = new Set(['home:']);
let fsAsked = 0;
const fsKey = (root, path) => root + ':' + path;

function fsExt(name) { const i = name.lastIndexOf('.'); return i < 0 ? '' : name.slice(i + 1).toLowerCase(); }
function fsJoin(dir, name) { return dir ? dir + '/' + name : name; }
function fsParent(path) { return path.includes('/') ? path.slice(0, path.lastIndexOf('/')) : ''; }
function fsSize(n) { return n < 1024 ? n + ' B' : n < 1048576 ? (n / 1024).toFixed(1) + ' KB' : (n / 1048576).toFixed(1) + ' MB'; }
function fsUrl(op, params) { return 'api/fs/' + op + '?' + new URLSearchParams(params); }
async function fsPost(op, params, body = '') {
	return apiPost(fsUrl(op, params), body);
}

// A folder as the server lists it, null when it does not. What the tree has of it is taken, fresh asks the server anew.
async function fsRead(root, path, fresh) {
	if (!fresh && fsCache.has(fsKey(root, path))) return fsCache.get(fsKey(root, path));
	try {
		const json = await (await fetch(fsUrl('list', { root, path }), { cache: 'no-store' })).json();
		if (!json.ok) { toast(json.message, false); return null; }
		json.entries.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
		fsCache.set(fsKey(root, json.path), json);
		return json;
	} catch (e) {
		toast('Could not read the folder: ' + e.message, false);
		return null;
	}
}
// the tree forgets a folder and all below it: it is not there any more, or not under that name
function fsForget(root, path) {
	const key = fsKey(root, path), below = path ? key + '/' : key;
	for (const set of [fsCache, fsOpen]) for (const k of [...set.keys()]) if (k === key || k.startsWith(below)) set.delete(k);
}

// The list shows a folder, read from the server anew. The tree has it open and marks it.
async function fsList(path = fsPath, root = fsRoot) {
	const asked = ++fsAsked, json = await fsRead(root, path, true);
	// (another folder was asked for while this one was on its way)
	if (!json || asked !== fsAsked) return false;
	fsRoot = root;
	fsPath = json.path;
	fsWritable = json.writable;
	fsEntries = json.entries;
	// it and the way to it are open in the tree
	for (let dir = fsPath; ; dir = fsParent(dir)) { fsOpen.add(fsKey(root, dir)); if (!dir) break; }
	fsRender();
	return true;
}
// a folder asked for in the tree or in the line of the path: a text that is open is closed first
function fsShow(root, path) {
	if (fsDropEdit()) fsList(path, root);
}
function fsRender() {
	// the way to the folder, every part of it to click on
	const parts = fsPath ? fsPath.split('/') : [];
	$('fsPath').innerHTML = '<a data-path="">' + FS_ROOTS[fsRoot] + '</a>' +
		parts.map((name, i) => ' / <a data-path="' + escapeHtml(parts.slice(0, i + 1).join('/')) + '">' + escapeHtml(name) + '</a>').join('');
	$('fsCount').textContent = fsEntries.length;
	$('fsMkdir').disabled = !fsWritable;
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
	fsTreeRender();
	const on = $('fsTree').querySelector('.on');
	if (on) on.scrollIntoView({ block: 'nearest' });
}

// The tree: the two roots, and under every open folder its sub folders. A folder the server has not listed yet
// shows closed, one that is known to have no sub folders has no arrow.
function fsTreeRender() {
	const rows = [];
	const add = (root, path, name, depth) => {
		const key = fsKey(root, path), list = fsCache.get(key), open = !!list && fsOpen.has(key);
		// a link and a name the server does not take in a request are no folders to go into
		const dirs = list ? list.entries.filter(e => e.dir && !e.odd && !e.link) : null;
		rows.push('<div class="fsNode' + (open ? ' open' : '') + (dirs && !dirs.length ? ' leaf' : '') + (root === fsRoot && path === fsPath ? ' on' : '') +
			'" data-root="' + root + '" data-path="' + escapeHtml(path) + '" style="padding-left:' + (6 + depth * 12) + 'px"><i></i>' + escapeHtml(name) + '</div>');
		if (open) for (const d of dirs) add(root, fsJoin(path, d.name), d.name, depth + 1);
	};
	for (const root in FS_ROOTS) add(root, '', FS_ROOTS[root] + (root === 'home' ? '' : ', read only'), 0);
	$('fsTree').innerHTML = rows.join('');
}
// the arrow of a folder: open or closed, the list stays as it is
async function fsToggle(root, path) {
	const key = fsKey(root, path);
	if (fsOpen.has(key) && fsCache.has(key)) fsOpen.delete(key);
	else if (await fsRead(root, path)) fsOpen.add(key);
	fsTreeRender();
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
	const from = fsJoin(fsPath, e.name), dest = fsJoin(fsPath, to);
	const r = await fsPost('rename', { path: from, to: dest });
	toast(r.message, r.ok);
	// the tree: a folder is not under its old name any more, and the folder it went into has one more
	if (r.ok && e.dir) { fsForget(fsRoot, from); fsCache.delete(fsKey(fsRoot, fsParent(dest))); }
	fsList();
}
async function fsDelete(e) {
	if (!confirm('Delete ' + e.name + (e.dir ? ' (it has to be empty)' : '') + '?')) return;
	const r = await fsPost('delete', { path: fsJoin(fsPath, e.name) });
	toast(r.message, r.ok);
	if (r.ok && e.dir) fsForget(fsRoot, fsJoin(fsPath, e.name));
	fsList();
}
async function fsMkdir() {
	const name = prompt('Name of the new folder');
	if (!name) return;
	const r = await fsPost('mkdir', { path: fsJoin(fsPath, name) });
	toast(r.message, r.ok);
	// (a name with a/ in front makes it in the folder a)
	if (r.ok) fsCache.delete(fsKey(fsRoot, fsParent(fsJoin(fsPath, name))));
	fsList();
}
// A file goes up in pieces, one after the other: each names where it begins, the last one puts the file in its
// place. It goes into the folder that is shown, when that is one of the server files. A pk3 goes into the top
// folder of the server files whatever is shown: the engine finds one nowhere else.
async function fsUploadFiles(files) {
	const here = fsRoot === 'home' ? fsPath : null, packs = files.filter(f => fsExt(f.name) === 'pk3');
	// what is in the top folder already, when that is not the folder that is shown
	const top = packs.length && here !== '' ? await fsRead('home', '', true) : null;
	let pk3 = false, refused = 0;
	for (const file of files) {
		const pack = packs.includes(file), dir = pack ? '' : here;
		if (dir === null) { refused++; continue; }
		const there = dir === here ? fsEntries : top ? top.entries : [];
		if (there.some(e => e.name === file.name) && !confirm(file.name + ' is there already. Replace it?')) continue;
		let offset = 0, ok = true;
		do {
			const piece = file.slice(offset, offset + FS_CHUNK), last = offset + piece.size >= file.size;
			$('fsProgress').textContent = file.name + ': ' + fsSize(offset) + ' of ' + fsSize(file.size);
			const r = await fsPost('put', { path: fsJoin(dir, file.name), offset, last: last ? 1 : 0 }, piece);
			if (!r.ok) { toast(file.name + ': ' + r.message, false); ok = false; break; }
			offset += piece.size;
		} while (offset < file.size);
		if (ok) toast(file.name + ' uploaded' + (dir === here ? '' : ' into the top folder of the server files: the engine finds a pk3 nowhere else'));
		if (ok && pack) pk3 = true;
	}
	if (refused) toast((refused === 1 ? 'One file was' : refused + ' files were') + ' not uploaded: the game installation can only be read. Show a folder of the server files first.', false);
	$('fsProgress').textContent = '';
	fsList();
	if (pk3) fsReloadMap('A pk3 was uploaded. The server only looks for pk3 files when a map starts.\n\n');
}
function fsReloadMap(why = '') {
	if (confirm(why + 'Restart the running map now? The players stay connected, what was placed in the World tab is put back where it is.')) action({ action: 'reload' });
}

async function fsOpenText(path) {
	try {
		const res = await fetch(fsUrl('get', { root: fsRoot, path, text: 1 }), { cache: 'no-store' });
		if (!res.ok) throw new Error(await res.text());
		// the textarea turns CR LF into LF: what it holds is the text that counts as unchanged
		$('fsText').value = await res.text();
		// a file of the installation is saved under the same name in the server files, where the engine looks first
		fsEdit = { path, saved: $('fsText').value, colored: fsExt(path) === 'cfg', copy: fsRoot !== 'home' };
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
		(fsEdit.copy ? ' (of the installation, Save writes it into the server files)' : '');
	$('fsEditState').classList.toggle('dirty', dirty);
}
async function fsSave() {
	const text = $('fsText').value.replace(/\r/g, ''), edit = fsEdit;
	if (cfgBytes(text) > FS_EDIT_MAX) { toast('The text is larger than ' + FS_EDIT_MAX + ' bytes, the server does not take it in one piece', false); return; }
	const r = await fsPost('put', { path: edit.path, offset: 0, last: 1, text: 1 }, text);
	toast(r.message, r.ok);
	if (r.ok) edit.saved = text;
	// A copy of a file of the installation may have made its folders in the server files: the tree asks for them
	// again, for the top folder at once.
	if (r.ok && edit.copy) {
		for (const key of [...fsCache.keys()]) if (key.startsWith('home:')) fsCache.delete(key);
		fsRead('home', '').then(fsTreeRender);
	}
	fsShowEdit();
}
// the text that is open is closed. false: it has changes and they are kept
function fsDropEdit() {
	if (fsEdit && $('fsText').value !== fsEdit.saved && !confirm('Drop what was changed in ' + fsEdit.path + '?')) return false;
	fsEdit = null;
	fsShowEdit();
	return true;
}
function fsCloseEdit() {
	if (fsDropEdit()) fsList();
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
$('fsTree').addEventListener('click', ev => {
	const el = ev.target.closest('.fsNode');
	if (!el) return;
	// the arrow opens and closes, the name shows the folder
	if (ev.target.tagName === 'I') fsToggle(el.dataset.root, el.dataset.path);
	else fsShow(el.dataset.root, el.dataset.path);
});
$('fsPath').addEventListener('click', ev => {
	const a = ev.target.closest('a');
	if (a) fsShow(fsRoot, a.dataset.path);
});
// Files dropped on the list go up as the ones of the Upload button do. A file that misses the list while the tab
// is open is not taken, or the browser would open it in place of the panel.
const fsDragsFiles = e => !!e.dataTransfer && [...e.dataTransfer.types].includes('Files');
$('fsView').addEventListener('dragover', e => {
	if (!fsDragsFiles(e)) return;
	e.preventDefault();
	e.dataTransfer.dropEffect = 'copy';
	$('fsView').classList.add('drop');
});
$('fsView').addEventListener('dragleave', e => { if (!$('fsView').contains(e.relatedTarget)) $('fsView').classList.remove('drop'); });
$('fsView').addEventListener('drop', e => {
	if (!fsDragsFiles(e)) return;
	e.preventDefault();
	$('fsView').classList.remove('drop');
	// a folder comes as a file without content: it is left out
	const isDir = it => { const en = it.webkitGetAsEntry && it.webkitGetAsEntry(); return !!en && en.isDirectory; };
	const items = [...e.dataTransfer.items].filter(it => it.kind === 'file'), files = items.filter(it => !isDir(it)).map(it => it.getAsFile()).filter(Boolean);
	if (files.length < items.length) toast('Only files can be dropped, not a folder', false);
	if (files.length) fsUploadFiles(files);
});
for (const type of ['dragover', 'drop']) {
	window.addEventListener(type, e => {
		if (mode !== 'files' || !fsDragsFiles(e) || $('fsView').contains(e.target)) return;
		e.preventDefault();
		e.dataTransfer.dropEffect = 'none';
	});
}
$('fsRefresh').addEventListener('click', () => fsList());
$('fsMkdir').addEventListener('click', fsMkdir);
$('fsUpload').addEventListener('click', () => $('fsFile').click());
$('fsReload').addEventListener('click', () => fsReloadMap());
$('fsFile').addEventListener('change', () => { const files = [...$('fsFile').files]; $('fsFile').value = ''; if (files.length) fsUploadFiles(files); });
$('fsText').addEventListener('input', fsShowEdit);
$('fsText').addEventListener('scroll', () => codeScroll('fsText', 'fsHi'));
$('fsSave').addEventListener('click', fsSave);
$('fsClose').addEventListener('click', fsCloseEdit);
fsTreeRender();
document.querySelectorAll('#tabs button').forEach(b => b.addEventListener('click', () => setMode(b.dataset.tab)));
// the pages: Map goes back to the map and the tool that was open there
document.querySelectorAll('#pages button').forEach(b => b.addEventListener('click', () => setMode(b.dataset.tab === 'map' ? (EDITOR_TOOLS.has(lastMapTool) && appMode === 'editor' ? lastMapTool : APP_TAB[appMode]) : b.dataset.tab)));
document.querySelectorAll('#appMode button').forEach(b => b.addEventListener('click', () => setAppMode(b.dataset.app)));

