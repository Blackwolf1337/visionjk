/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// boot.js -- the page of the client that runs in a browser. It asks the server that handed it out which game
// files (packs) there are, takes them from the storage of the browser or downloads them, compiles the modules of
// the game and starts the engine (visionjk.wasm32.js), which then draws and reads keys and mouse by itself.
// What the engine expects of the page is on the Module object below, see shared/sys/web/sys_web.h for its side.
// window.vjk is for whoever drives the page from outside (the test driver).

(() => {
'use strict';

// the engine looks its modules up in Module.vjkModules by exactly these names
const SIDE_MODULES = ['cgamewasm32.wasm', 'uiwasm32.wasm', 'rd-vulkan-vision_wasm32.wasm'];
const ENGINE = 'visionjk.wasm32.js';
const LOG_KEPT = 2000, LOG_SHOWN = 10;

const $ = id => document.getElementById(id);
const canvas = $('canvas');
const words = e => e && e.message ? (e.name && e.name !== 'Error' ? e.name + ': ' : '') + e.message : String(e);
const megabytes = (count, of) => (count / 1e6).toFixed(1) + ' of ' + (of / 1e6).toFixed(1) + ' MB';
// an error that names the file it is about
const named = (name, promise) => promise.catch(e => { throw new Error(name + ': ' + words(e)); });

function pending() {
	let done;
	const promise = new Promise(resolve => { done = resolve; });
	return { promise, done };
}

// callMain() is through: the functions of the engine may be called
let engineRuns = false;

const vjk = window.vjk = {
	// 'manifest', 'packs', 'modules', 'starting', 'running' or 'error'
	phase: 'manifest',
	log: [],
	// the list of the manifest, each pack with source 'network' or 'stored' once it is loaded
	packs: [],
	// connstate_t of the client, 8 is "in the game". -1: the engine does not run
	state() {
		try { return engineRuns ? Module._Web_ClientState() : -1; } catch (e) { return -1; }
	},
	// a line for the console of the game, run with its next frame
	command(line) {
		if (!engineRuns) return false;
		Module.ccall('Web_Command', null, ['string'], [String(line)]);
		return true;
	},
};

// ---- the log: what the engine prints to its console, and what the page has to say ("page: ...")

let logShown = true, logQueued = false;

function drawLog() {
	logQueued = false;
	if (logShown) $('log').textContent = vjk.log.slice(-LOG_SHOWN).join('\n');
}

function addLog(line) {
	vjk.log.push(line);
	if (vjk.log.length > LOG_KEPT) vjk.log.shift();
	// the engine prints hundreds of lines in one go: they are drawn once a frame
	if (!logQueued) { logQueued = true; requestAnimationFrame(drawLog); }
}

const note = line => addLog('page: ' + line);

function showLog(on) {
	logShown = on;
	document.body.classList.toggle('nolog', !on);
	$('logToggle').textContent = on ? 'Hide log' : 'Show log';
	drawLog();
}

// ---- what the panel says, and errors: the first one is shown over everything, what follows it is in the log

function setPhase(phase) {
	if (vjk.phase !== 'error') vjk.phase = phase;
}

function say(status, detail) {
	$('status').textContent = status;
	$('detail').textContent = detail || '';
}

// retry: what the button does instead of loading the page anew
function fail(message, retry) {
	note('error: ' + message);
	if (vjk.phase === 'error') return;
	const before = vjk.phase;
	vjk.phase = 'error';
	if (document.pointerLockElement) document.exitPointerLock();
	$('errorText').textContent = message;
	$('errorButton').textContent = retry ? 'Try again' : 'Reload the page';
	$('errorButton').onclick = retry ? () => { $('error').hidden = true; canvas.focus({ preventScroll: true }); vjk.phase = before; retry(); } : () => location.reload();
	$('error').hidden = false;
	$('errorButton').focus();
	showLog(true);
}

// Emscripten leaves main() and ends the program by throwing these: no errors
const isControlFlow = e => e === 'unwind' || (!!e && e.name === 'ExitStatus');

window.addEventListener('error', ev => {
	if (isControlFlow(ev.error)) return;
	const where = ev.filename ? ' (' + ev.filename.split('/').pop() + ':' + ev.lineno + ')' : '';
	fail((ev.error ? words(ev.error) : ev.message || 'An error without a text') + where);
});
window.addEventListener('unhandledrejection', ev => {
	if (isControlFlow(ev.reason)) return;
	// Once the game runs, a promise nobody waited for is most often the browser refusing the mouse capture, the
	// full screen or the sound outside of a click, and the game goes on. After a trap of the engine it does not.
	if (engineRuns && !(ev.reason instanceof WebAssembly.RuntimeError)) note('a promise was rejected: ' + words(ev.reason));
	else fail(words(ev.reason));
});

async function ask(url, options) {
	let answer;
	try { answer = await fetch(url, options); } catch (e) { throw new Error('no answer from the server (' + words(e) + ')'); }
	if (!answer.ok) throw new Error(('the server answered ' + answer.status + ' ' + answer.statusText).trim());
	return answer;
}

// ---- the packs in the browser: IndexedDB database "visionjk", object store "packs", a record for each pack:
// { path, size, checksum, blob }, the key is the path. (The home path of the game is another database, which
// Emscripten keeps under the name of the mount point, "/home".)

const request = r => new Promise((ok, bad) => { r.onsuccess = () => ok(r.result); r.onerror = () => bad(r.error); });
const finished = tx => new Promise((ok, bad) => { tx.oncomplete = () => ok(); tx.onabort = () => bad(tx.error || new Error('the transaction was aborted')); });
const same = (entry, pack) => !!entry && entry.path === pack.path && entry.size === pack.size && entry.checksum === pack.checksum &&
	entry.blob instanceof Blob && entry.blob.size === pack.size;

// some browsers throw already when a page without storage (a private window, a sandbox) names it
function haveIndexedDB() {
	try { return !!window.indexedDB; } catch (e) { return false; }
}

function openStore() {
	if (!haveIndexedDB()) return Promise.reject(new Error('no IndexedDB'));
	const open = indexedDB.open('visionjk', 1);
	open.onupgradeneeded = () => open.result.createObjectStore('packs', { keyPath: 'path' });
	return request(open);
}

// what the server does not have any more, or has with another size or checksum, only takes up room
function pruneStore(db, packs) {
	const tx = db.transaction('packs', 'readwrite'), walk = tx.objectStore('packs').openCursor();
	walk.onsuccess = () => {
		const at = walk.result;
		if (!at) return;
		if (!packs.some(pack => same(at.value, pack))) {
			note(at.key + " in the browser's storage is not a file of this server any more: removed");
			at.delete();
		}
		at.continue();
	};
	return finished(tx);
}

// the Blob is the browser's own copy of the bytes: the array is free for the engine when this is through
function putStore(db, pack, bytes) {
	const blob = new Blob([bytes]), tx = db.transaction('packs', 'readwrite');
	tx.objectStore('packs').put({ path: pack.path, size: pack.size, checksum: pack.checksum, blob });
	return finished(tx);
}

// ---- the packs: one after another, the smallest first. A pack is one array from its first byte on, and at
// most that array and the copy the browser's storage is given exist at a time.

let db = null;				// null: this browser keeps nothing for the page
let fs = null;				// Module.FS, once the file system of the engine is there
const forFS = [];			// packs that were ready before it: [path, bytes]
const packsDone = pending();
// order: the packs as they are loaded. at: the one in work, got: its bytes so far, done: the bytes of the ones
// before it. samples: [time, got] of the last seconds, for the speed
const load = { order: [], at: 0, got: 0, done: 0, total: 0, painted: 0, samples: [] };

function paintProgress() {
	const now = performance.now(), pack = load.order[load.at];
	load.painted = now;
	load.samples.push([now, load.got]);
	while (load.samples.length > 2 && now - load.samples[0][0] > 3000) load.samples.shift();
	const [since, gotThen] = load.samples[0], rate = now > since ? (load.got - gotThen) * 1000 / (now - since) : 0;
	const all = load.done + load.got;
	if (pack) $('detail').textContent = megabytes(load.got, pack.size) + (rate > 0 ? ', ' + (rate / 1e6).toFixed(1) + ' MB/s' : '');
	$('total').textContent = (pack ? 'File ' + (load.at + 1) + ' of ' + load.order.length : load.order.length + ' files') + ', ' + megabytes(all, load.total) + ' in all';
	$('fill').style.width = (load.total ? all * 100 / load.total : 100) + '%';
}

function onBytes(count) {
	load.got += count;
	if (performance.now() - load.painted >= 100) paintProgress();
}

// the pack in work is read from its first byte (again, after a failed try)
function begin(status) {
	load.got = 0;
	load.samples = [];
	$('status').textContent = status;
	paintProgress();
}

// Reads a stream into one array that has the size of the file from the start. Collecting the chunks and joining
// them at the end would hold a pack of 650 MB twice.
async function readAll(stream, size) {
	const bytes = new Uint8Array(size), reader = stream.getReader();
	let at = 0;
	for (;;) {
		const { done, value } = await reader.read();
		if (done) break;
		if (at + value.length > size) {
			reader.cancel().catch(() => {});
			throw new Error('more came than the ' + size + ' bytes the list of the server says');
		}
		bytes.set(value, at);
		at += value.length;
		onBytes(value.length);
	}
	if (at !== size) throw new Error('the transfer ended after ' + at + ' of ' + size + ' bytes');
	return bytes;
}

// The array becomes the file of the engine's file system as it is (canOwn): no copy is made, and the page must
// not keep or touch it afterwards.
function toFS(path, bytes) {
	if (!fs) { forFS.push([path, bytes]); return; }
	const file = '/game/' + path;
	fs.mkdirTree(file.slice(0, file.lastIndexOf('/')));
	fs.writeFile(file, bytes, { canOwn: true });
}

async function loadPack(pack) {
	let bytes = null;
	const entry = db && await request(db.transaction('packs').objectStore('packs').get(pack.path)).catch(() => null);
	if (same(entry, pack)) {
		begin('Reading ' + pack.path + " from this browser's storage");
		try {
			bytes = await readAll(entry.blob.stream(), pack.size);
		} catch (e) {
			note(pack.path + " could not be read from the browser's storage (" + words(e) + '): it is downloaded');
		}
	}
	if (bytes) {
		pack.source = 'stored';
	} else {
		begin('Downloading ' + pack.path);
		// no-store: the cache of the browser is not to hold a second copy of a pack
		const answer = await ask('pk3/' + pack.path.split('/').map(encodeURIComponent).join('/'), { cache: 'no-store' });
		bytes = await readAll(answer.body, pack.size);
		pack.source = 'network';
		if (db) {
			paintProgress();
			$('status').textContent = 'Keeping ' + pack.path + ' in this browser for the next visit';
			try {
				await putStore(db, pack, bytes);
			} catch (e) {
				// no room, most often. The packs come smallest first, so what follows would not fit either.
				note('the browser did not take ' + pack.path + ' into its storage (' + words(e) + '): the game files that are not in it yet are downloaded again at the next visit');
				db.close();
				db = null;
			}
		}
	}
	toFS(pack.path, bytes);
}

// A pack that fails stops here with its error and a button that calls this again: the packs before it are in
// place, the failed one is tried anew from its first byte.
async function loadPacks() {
	while (load.at < load.order.length) {
		// something else failed for good meanwhile (a module that does not compile): no more is downloaded for it
		if (vjk.phase === 'error') return;
		const pack = load.order[load.at];
		try {
			await loadPack(pack);
		} catch (e) {
			fail(pack.path + ': ' + (e instanceof RangeError ? 'this browser has no ' + (pack.size / 1e6).toFixed(0) + ' MB of memory left for it' : words(e)), loadPacks);
			return;
		}
		load.done += pack.size;
		load.got = 0;
		load.at++;
	}
	if (db) db.close();
	paintProgress();
	packsDone.done();
}

async function getManifest() {
	const manifest = await (await ask('manifest.json', { cache: 'no-store' })).json();
	// a path becomes a file under /game: plain names with single slashes between them
	const plain = pack => !!pack && typeof pack.path === 'string' && /^[^/\\]+(\/[^/\\]+)*$/.test(pack.path) && !pack.path.split('/').some(part => part === '.' || part === '..') &&
		Number.isSafeInteger(pack.size) && pack.size >= 0 && Number.isSafeInteger(pack.checksum);
	if (!manifest || typeof manifest.game !== 'string' || !Array.isArray(manifest.packs) || !manifest.packs.every(plain)) throw new Error('the list of game files is not in the form this page knows');
	return manifest;
}

// ---- the engine

const runtime = pending();
// Built with assertions, Emscripten says every ten seconds what the start is held by. It is held by the packs,
// and the panel shows how far those are.
const HELD_NOTICE = /^(still waiting on run dependencies:|dependency: |\(end of list\))/;

const Module = window.Module = {
	canvas,
	vjkModules: {},
	print: (...parts) => addLog(parts.join(' ')),
	printErr: (...parts) => {
		const line = parts.join(' ');
		if (engineRuns || !HELD_NOTICE.test(line)) addLog(line);
	},
	// a fatal error of the engine, which halts after it
	vjkError: message => fail(String(message)),
	onAbort: what => fail('The engine stopped' + (what ? ': ' + words(what) : '.')),
	onExit: code => fail('The game has ended (exit code ' + code + ').'),
	onRuntimeInitialized: () => runtime.done(),
	// The file system of the engine is there, and the runtime does not start before the packs are in it and the
	// home path is read. "packs" is held from here until the last pack is written: this Emscripten goes on as
	// soon as nothing is held, also when something would be taken again later, so "home" is taken while "packs"
	// still is.
	preRun: [() => {
		try {
			fs = Module.FS;
			for (const [path, bytes] of forFS.splice(0)) toFS(path, bytes);
			Module.addRunDependency('packs');
			packsDone.promise.then(() => Module.removeRunDependency('packs'));
			fs.mkdirTree('/home');
			if (!haveIndexedDB()) {
				note('this browser keeps nothing for the page: the settings of the game last until the page is closed');
				return;
			}
			// the files the game writes (its config). autoPersist: Emscripten stores every write by itself.
			Module.addRunDependency('home');
			fs.mount(fs.filesystems.IDBFS, { autoPersist: true }, '/home');
			fs.syncfs(true, error => {
				if (error) note('the stored settings of the game could not be read (' + words(error) + ')');
				Module.removeRunDependency('home');
			});
		} catch (e) {
			fail('The game files could not be given to the engine: ' + words(e));
		}
	}],
};

// what the start still waits for once the packs are in
const awaited = new Set([...SIDE_MODULES, ENGINE]);

function prepared(name) {
	awaited.delete(name);
	if (vjk.phase === 'modules') say('Preparing the code of the game', awaited.size ? 'Waiting for ' + [...awaited].join(', ') : '');
}

async function compile(name) {
	// no-cache: asked anew every time, a module of an older build next to a newer engine does not load
	const options = { cache: 'no-cache' };
	// the whole file first: for a server that does not send it as application/wasm
	const whole = async () => WebAssembly.compile(await (await ask(name, options)).arrayBuffer());
	Module.vjkModules[name] = await (WebAssembly.compileStreaming ? WebAssembly.compileStreaming(fetch(name, options)).catch(whole) : whole());
	prepared(name);
}

// the engine reads the Module object above when its script runs
function loadEngine() {
	const script = document.createElement('script');
	script.src = ENGINE;
	script.onerror = () => fail(ENGINE + ': the server did not send the engine');
	document.body.appendChild(script);
}

async function start(manifest) {
	setPhase('starting');
	say('Starting the game');
	const args = ['+set', 'fs_basepath', '/game', '+set', 'fs_homepath', '/home', '+set', 'fs_game', manifest.game,
		'+set', 'net_wsHost', location.host, '+set', 'r_mode', '-1',
		'+set', 'r_customwidth', String(canvas.clientWidth), '+set', 'r_customheight', String(canvas.clientHeight)];
	const query = new URLSearchParams(location.search);
	if (query.has('connect')) args.push('+connect', location.hostname);
	args.push(...(query.get('args') || '').split(' ').filter(Boolean));
	note('the engine starts with: ' + args.join(' '));
	canvas.focus({ preventScroll: true });
	// callMain() keeps this thread for seconds, and the line above is to be on the screen before. A tab in the
	// back gets no frame: there the timer goes on.
	await new Promise(resolve => { requestAnimationFrame(() => setTimeout(resolve, 0)); setTimeout(resolve, 250); });
	if (vjk.phase === 'error') return;
	Module.callMain(args);
	engineRuns = true;
	if (vjk.phase === 'error') return;
	setPhase('running');
	document.body.classList.add('running');
	showLog(false);
}

async function main() {
	if (typeof WebAssembly !== 'object') throw new Error('This browser has no WebAssembly: the game cannot run in it.');
	const probe = document.createElement('canvas').getContext('webgl2');
	if (!probe) throw new Error('This browser has no WebGL2 (or it is turned off, or the graphics driver is on its block list): the game cannot draw in it.');
	const lose = probe.getExtension('WEBGL_lose_context');
	if (lose) lose.loseContext();

	// the code of the game is fetched and compiled while the packs come, and what is wrong with it is said at
	// once and not after the packs
	const modules = Promise.all(SIDE_MODULES.map(name => named(name, compile(name)))).then(() => true, e => { fail(words(e)); return false; });
	runtime.promise.then(() => prepared(ENGINE));
	loadEngine();

	say('Asking the server which game files there are');
	const manifest = await named('manifest.json', getManifest());
	vjk.packs = manifest.packs;
	load.order = [...manifest.packs].sort((a, b) => a.size - b.size);
	load.total = manifest.packs.reduce((sum, pack) => sum + pack.size, 0);
	setPhase('packs');
	try {
		db = await openStore();
		await pruneStore(db, manifest.packs);
	} catch (e) {
		note('this browser keeps no files for the page (' + words(e) + '): the game files are downloaded at every visit');
		if (db) db.close();
		db = null;
	}
	// not waited for by itself: a pack that failed goes on from its button
	loadPacks();
	await packsDone.promise;

	setPhase('modules');
	prepared();
	if (!await modules) return;
	await runtime.promise;
	if (vjk.phase !== 'error') await start(manifest);
}

// ---- keys and mouse. The engine (SDL) has its own listeners on the window and the canvas. Nothing here stops an
// event on its way to them, only what the browser itself would do with the key is taken away.

window.addEventListener('keydown', ev => {
	if (document.activeElement !== canvas) return;
	// AltGr types a character, and counts as Ctrl+Alt on Windows
	const combo = (ev.ctrlKey || ev.altKey) && !ev.getModifierState('AltGraph') && (/^Key[A-Z]$/.test(ev.code) || /^[a-z]$/i.test(ev.key));
	// Alt alone opens the menu bar of some browsers
	if (combo || /^(F([1-9]|1[0-2])|Tab|Backspace|Alt|Arrow(Up|Down|Left|Right))$/.test(ev.key)) ev.preventDefault();
}, true);
// The keys that type a character (space, quote, tilde, slash) are not taken on keydown: the browser would then
// send no keypress, and keypress is where the engine reads typed text from. What the browser does with them
// (space scrolls, quote and slash open the quick find of Firefox) hangs on keypress, and is taken there.
window.addEventListener('keypress', ev => {
	if (document.activeElement === canvas) ev.preventDefault();
}, true);

// the engine takes the default of a mouse button away, and with it the focus a click would give
canvas.addEventListener('pointerdown', () => canvas.focus({ preventScroll: true }));
canvas.addEventListener('contextmenu', ev => ev.preventDefault());
// after the listeners of the engine: a renderer that brings its context back says so by taking the default away
canvas.addEventListener('webglcontextlost', ev => setTimeout(() => {
	if (!ev.defaultPrevented) fail('The browser took the graphics context away from the game (out of graphics memory, most often).');
}, 0));
document.addEventListener('pointerlockchange', () => document.body.classList.toggle('locked', document.pointerLockElement === canvas));

// the button does not take the keyboard away from the canvas
$('logToggle').addEventListener('mousedown', ev => ev.preventDefault());
$('logToggle').addEventListener('click', () => showLog(!logShown));

canvas.focus({ preventScroll: true });
main().catch(e => fail(words(e)));
})();
