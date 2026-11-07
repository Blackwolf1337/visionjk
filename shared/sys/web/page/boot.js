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
//
// Opened with ?tv the page watches the TV of the server (issue #7). It then loads, in place of the pk3 files of the
// server, the packs that tools/tvpack/tvpack.py made of them: tv-index.json next to the page names them,
// /tvinfo of the server says which map runs, and the common pack and the pack of that map are loaded. A pack is
// kept encrypted and decrypted before it goes to the engine. When the map changes the page loads again. A server
// with no tv-index.json gets the TV with its pk3 files, as a game would.

(() => {
'use strict';

// the engine looks its modules up in Module.vjkModules by exactly these names
const SIDE_MODULES = ['cgamewasm32.wasm', 'uiwasm32.wasm', 'rd-vulkan-vision_wasm32.wasm'];
// Taken along when the server has them, the game runs without: Steam Audio for v_spatial, a file of its own that
// the engine loads when it wants it, as it loads the library on the desktop.
const OPTIONAL_MODULES = ['phonon.wasm'];
const ENGINE = 'visionjk.wasm32.js', ENGINE_CODE = 'visionjk.wasm32.wasm';
// The server sends files to one address only so many at a time and answers 503 beyond that, with the seconds to
// wait (two players behind one address who load at once). That often the page asks again.
const BUSY_TRIES = 40;
const LOG_KEPT = 2000, LOG_SHOWN = 10;
// how long the window has to keep a new size before the renderer is started again for it
const RESIZE_WAIT = 600;
// the TV: the key of the packs (the one of tvpack.py), what a pack begins with, how often the map is asked for
const TV_PACK_KEY = '5a1c9e3b07d24f68a1b2c3d4e5f60718';
const TV_PACK_MAGIC = 'VJKTV1';
const TV_CHECK_MSEC = 5000;
const tvMode = new URLSearchParams(location.search).has('tv');
// map: the map whose pack was loaded. all: the packs of every map, which the storage keeps
const tv = { map: '', all: [] };

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

// ---- the models of the model store of the site (client/cl_models.cpp): the engine names a model a player here
// uses that the game does not have, the page fetches it from /models/ of this host, takes it only with the size and
// the SHA-256 of the list there (index.txt), puts it into base/modelstore of the home folder and tells the engine.
// The engine and its file system take only a pak with files of that one model.

const MODEL_MOST = 100 * 1024 * 1024;
let storeList = null;

async function wantModel(name) {
	if (!/^[a-z0-9_]{2,32}$/.test(name) || !fs) return;
	if (!storeList) storeList = ask('/models/index.txt', { cache: 'no-cache' }).then(answer => answer.text()).catch(() => '');
	const line = (await storeList).split('\n').map(l => l.trim().split(' ')).find(l => l[0] === name);
	if (!line || !(Number(line[1]) > 0 && Number(line[1]) <= MODEL_MOST) || !/^[0-9a-f]{64}$/.test(line[2] || '')) return;
	const answer = await ask('/models/files/' + name + '.pk3', { cache: 'no-store' });
	// (not readAll: that moves the bar of the loading, which is gone while the game runs)
	const bytes = new Uint8Array(await answer.arrayBuffer());
	if (bytes.length !== Number(line[1])) throw new Error('it is not as large as the list says');
	const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join('');
	if (digest !== line[2]) throw new Error('it is not the file of the list');
	fs.mkdirTree('/home/base/modelstore');
	fs.writeFile('/home/base/modelstore/' + name + '.pk3', bytes, { canOwn: true });
	vjk.command('v_modelPak ' + name);
}

vjk.wantModel = name => {
	wantModel(String(name)).catch(e => note('the model ' + name + ' did not come from the model store: ' + words(e)));
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

// the panel of the loading steps makes room for it
function showError(on) {
	$('error').hidden = !on;
	document.body.classList.toggle('failed', on);
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
	$('errorButton').onclick = retry ? () => { showError(false); canvas.focus({ preventScroll: true }); vjk.phase = before; retry(); } : () => location.reload();
	showError(true);
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
	for (let tries = 0; ; tries++) {
		let answer;
		try { answer = await fetch(url, options); } catch (e) { throw new Error('no answer from the server (' + words(e) + ')'); }
		if (answer.status === 503 && tries < BUSY_TRIES) {
			if (!tries) note(url + ': the server is busy, asking again');
			const seconds = Math.min(Math.max(parseInt(answer.headers.get('Retry-After'), 10) || 2, 1), 10);
			await new Promise(resolve => setTimeout(resolve, seconds * 1000));
			continue;
		}
		if (!answer.ok) throw new Error(('the server answered ' + answer.status + ' ' + answer.statusText).trim());
		return answer;
	}
}

// ---- the packs in the browser: IndexedDB database "visionjk", version 2. A pack is kept in pieces of
// STORE_CHUNK bytes: a browser does not write one value of hundreds of megabytes (Chromium: "Failed to write
// blobs" for a pack of 562 MB), and a piece is the only second copy of bytes of a pack there ever is.
//   store "chunks"  the key is [path, index], the value an ArrayBuffer with that piece of the pack
//   store "packs"   the key is the path, the value { path, size, checksum, chunkSize, chunks }. It is written
//                   after the last chunk: a pack without it was not stored to its end and is thrown away.
// (The home path of the game is another database, which Emscripten keeps under the name of the mount point,
// "/home".)

const STORE_CHUNK = 16 * 1024 * 1024;
let db = null;				// null: this browser keeps nothing for the page

const request = r => new Promise((ok, bad) => { r.onsuccess = () => ok(r.result); r.onerror = () => bad(r.error); });
const finished = tx => new Promise((ok, bad) => { tx.oncomplete = () => ok(); tx.onabort = () => bad(tx.error || new Error('the transaction was aborted')); });
// the record of a pack, of the file the server has now
const same = (meta, pack) => !!meta && meta.path === pack.path && meta.size === pack.size && meta.checksum === pack.checksum &&
	meta.chunkSize > 0 && meta.chunks === Math.ceil(pack.size / meta.chunkSize);

// some browsers throw already when a page without storage (a private window, a sandbox) names it
function haveIndexedDB() {
	try { return !!window.indexedDB; } catch (e) { return false; }
}

function openStore() {
	return new Promise((ok, bad) => {
		if (!haveIndexedDB()) throw new Error('no IndexedDB');
		// (the packs of the TV are kept apart from the pk3 files of the game, which a page of the same server may have)
		const open = indexedDB.open(tvMode ? 'visionjk-tv' : 'visionjk', 2);
		let blocked = false;
		open.onupgradeneeded = () => {
			// version 1 kept a pack as one Blob: nothing of it is read any more
			for (const name of [...open.result.objectStoreNames]) open.result.deleteObjectStore(name);
			open.result.createObjectStore('packs', { keyPath: 'path' });
			open.result.createObjectStore('chunks');
		};
		open.onblocked = () => { blocked = true; bad(new Error('another tab holds an older form of it open')); };
		open.onerror = () => bad(open.error);
		open.onsuccess = () => {
			if (blocked) { open.result.close(); return; }
			// a later form of this page in another tab that changes the layout is not kept waiting
			open.result.onversionchange = () => open.result.close();
			ok(open.result);
		};
	});
}

// What only takes up room: packs the server does not have any more or has with another size or checksum, and
// chunks without the record of their pack (a store that was cut off, or a pack that was just removed).
async function pruneStore(packs) {
	const tx = db.transaction(['packs', 'chunks'], 'readwrite'), chunks = tx.objectStore('chunks');
	const whole = new Map();		// path: number of chunks, of the packs that stay
	let loose = 0;
	const metas = tx.objectStore('packs').openCursor();
	metas.onsuccess = () => {
		const at = metas.result;
		if (at) {
			if (packs.some(pack => same(at.value, pack))) {
				whole.set(at.key, at.value.chunks);
			} else {
				note(at.key + " in the browser's storage is not a file of this server any more: removed");
				at.delete();
			}
			at.continue();
			return;
		}
		// the keys only: the values are the packs themselves
		const keys = chunks.openKeyCursor();
		keys.onsuccess = () => {
			const chunk = keys.result;
			if (!chunk) return;
			if (!(Array.isArray(chunk.key) && chunk.key[1] < whole.get(chunk.key[0]))) {
				chunks.delete(chunk.key);
				loose++;
			}
			chunk.continue();
		};
	};
	await finished(tx);
	if (loose) note(loose + " pieces of game files whose pack is gone or was not stored to its end were removed from the browser's storage");
}

async function clearPack(path) {
	const tx = db.transaction(['packs', 'chunks'], 'readwrite');
	tx.objectStore('packs').delete(path);
	tx.objectStore('chunks').delete(IDBKeyRange.bound([path, 0], [path, Infinity]));
	return finished(tx);
}

// A chunk is written and through before the next one is cut out of the array.
async function toStore(pack, bytes) {
	await clearPack(pack.path);
	const chunks = Math.ceil(pack.size / STORE_CHUNK);
	for (let i = 0; i < chunks; i++) {
		const tx = db.transaction('chunks', 'readwrite');
		// slice() copies the piece into a buffer of its own. A view on the array would be stored with the whole
		// array behind it.
		tx.objectStore('chunks').put(bytes.slice(i * STORE_CHUNK, (i + 1) * STORE_CHUNK).buffer, [pack.path, i]);
		await finished(tx);
		$('detail').textContent = megabytes(Math.min((i + 1) * STORE_CHUNK, pack.size), pack.size);
	}
	const tx = db.transaction('packs', 'readwrite');
	tx.objectStore('packs').put({ path: pack.path, size: pack.size, checksum: pack.checksum, chunkSize: STORE_CHUNK, chunks });
	await finished(tx);
}

// null: the pack is not stored. An error: it is, but not all of it can be read.
async function fromStore(pack) {
	const meta = await request(db.transaction('packs').objectStore('packs').get(pack.path));
	if (!same(meta, pack)) return null;
	begin('Reading ' + pack.path + " from this browser's storage");
	const bytes = new Uint8Array(pack.size);
	for (let i = 0; i < meta.chunks; i++) {
		const at = i * meta.chunkSize, size = Math.min(meta.chunkSize, pack.size - at);
		const piece = await request(db.transaction('chunks').objectStore('chunks').get([pack.path, i]));
		if (!(piece instanceof ArrayBuffer) || piece.byteLength !== size) throw new Error('piece ' + (i + 1) + ' of ' + meta.chunks + ' is missing or has the wrong size');
		bytes.set(new Uint8Array(piece), at);
		onBytes(size);
	}
	return bytes;
}

// ---- the packs: one after another, the smallest first. A pack is one array from its first byte on, and that
// array is what the engine gets.

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

// A pack of the TV: "VJKTV1" and two zero bytes, the counter it starts with (16 bytes), then the zip in AES-128-CTR
// (tools/tvpack/tvpack.py). The key is in this file: it keeps the files of the game from being taken off the site
// as they are, nothing more.
async function decryptPack(bytes) {
	if (!window.crypto || !crypto.subtle) throw new Error('the TV needs a page that came over https or from this machine');
	if (bytes.length < 24 || String.fromCharCode(...bytes.subarray(0, 6)) !== TV_PACK_MAGIC) throw new Error('this is not a pack of the TV');
	const key = await crypto.subtle.importKey('raw', Uint8Array.from(TV_PACK_KEY.match(/../g), hex => parseInt(hex, 16)), 'AES-CTR', false, ['decrypt']);
	return new Uint8Array(await crypto.subtle.decrypt({ name: 'AES-CTR', counter: bytes.slice(8, 24), length: 128 }, key, bytes.subarray(24)));
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
	const began = performance.now(), seconds = () => ((performance.now() - began) / 1000).toFixed(1) + ' s';
	let bytes = null;
	if (db) {
		try {
			bytes = await fromStore(pack);
		} catch (e) {
			note(pack.path + " could not be read from the browser's storage (" + words(e) + '): it is downloaded');
		}
	}
	if (bytes) {
		pack.source = 'stored';
		note(pack.path + ": read from the browser's storage after " + seconds());
	} else {
		begin('Downloading ' + pack.path);
		// no-store: the cache of the browser is not to hold a second copy of a pack
		const answer = await ask(pack.url || 'pk3/' + pack.path.split('/').map(encodeURIComponent).join('/'), { cache: 'no-store' });
		bytes = await readAll(answer.body, pack.size);
		pack.source = 'network';
		note(pack.path + ': downloaded after ' + seconds());
		if (db) {
			paintProgress();
			$('status').textContent = 'Keeping ' + pack.path + ' in this browser for the next visit';
			try {
				await toStore(pack, bytes);
				note(pack.path + ": in the browser's storage after " + seconds());
			} catch (e) {
				// no room, most often. The packs come smallest first, so what follows would not fit either.
				note('the browser did not take ' + pack.path + ' into its storage (' + words(e) + '): the game files that are not in it yet are downloaded again at the next visit');
				// what was written of it only takes up room
				await clearPack(pack.path).catch(() => {});
				db.close();
				db = null;
			}
		}
	}
	toFS(pack.path, pack.encrypted ? await decryptPack(bytes) : bytes);
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

// The packs of the TV for the map the server plays, in the form of a manifest. null: the server has no packs.
async function getTVPacks() {
	const found = await fetch('tv-index.json', { cache: 'no-store' }).catch(() => null);
	if (!found || found.status === 404) return null;
	if (!found.ok) throw new Error('the server answered ' + found.status + ' ' + found.statusText);
	const index = await found.json();
	const info = await (await ask('/tvinfo', { cache: 'no-store' })).json();
	const entry = p => !!p && typeof p.file === 'string' && /^[A-Za-z0-9_-]+\.vpk$/.test(p.file) && Number.isSafeInteger(p.size) && p.size > 0 &&
		typeof p.sha256 === 'string';
	if (!index || !entry(index.common) || !index.maps || typeof index.maps !== 'object') throw new Error('the list of the packs of the TV is not in the form this page knows');
	const pack = p => ({ path: 'base/' + p.file.replace(/\.vpk$/, '.pk3'), url: p.file, size: p.size, checksum: p.sha256, encrypted: true });
	tv.map = String(info && info.map || '').toLowerCase();
	tv.all = [index.common, ...Object.values(index.maps)].filter(entry).map(pack);
	if (!entry(index.maps[tv.map])) throw new Error('the server plays ' + (tv.map || 'no map') + ', and the TV has no pack of it yet');
	return { game: 'japlus', packs: [pack(index.common), pack(index.maps[tv.map])] };
}

// While the TV runs: when the server plays another map, the page loads again, with the pack of that one.
function watchMap() {
	setInterval(async () => {
		try {
			const info = await (await fetch('/tvinfo', { cache: 'no-store' })).json();
			const map = String(info && info.map || '').toLowerCase();
			if (map && map !== tv.map) {
				note('the server plays ' + map + ' now: the page loads again');
				location.reload();
			}
		} catch (e) {
			// a server that does not answer for a moment: asked again next time
		}
	}, TV_CHECK_MSEC);
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
	// The script of the engine is run from a copy the page fetched (loadEngine), so it does not know where it
	// came from: its files are next to the page.
	locateFile: name => name,
	// The code of the engine, fetched by the page and not by the script, so that a busy server is asked again.
	instantiateWasm: (imports, receive) => {
		const options = { cache: 'no-cache' };
		const whole = async () => WebAssembly.instantiate(await (await ask(ENGINE_CODE, options)).arrayBuffer(), imports);
		(WebAssembly.instantiateStreaming ? WebAssembly.instantiateStreaming(ask(ENGINE_CODE, options), imports).catch(whole) : whole())
			.then(result => { receive(result.instance, result.module); prepared(ENGINE_CODE); },
				e => fail(ENGINE_CODE + ': ' + words(e)));
		return {};
	},
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
			fs.mount(Module.IDBFS || fs.filesystems.IDBFS, { autoPersist: true }, '/home');
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
const awaited = new Set([...SIDE_MODULES, ENGINE, ENGINE_CODE]);

function prepared(name) {
	awaited.delete(name);
	if (vjk.phase === 'modules') say('Preparing the code of the game', awaited.size ? 'Waiting for ' + [...awaited].join(', ') : '');
}

async function compile(name) {
	// no-cache: asked anew every time, a module of an older build next to a newer engine does not load
	const options = { cache: 'no-cache' };
	// the whole file first: for a server that does not send it as application/wasm
	const whole = async () => WebAssembly.compile(await (await ask(name, options)).arrayBuffer());
	Module.vjkModules[name] = await (WebAssembly.compileStreaming ? WebAssembly.compileStreaming(ask(name, options)).catch(whole) : whole());
	prepared(name);
}

// The engine reads the Module object above when its script runs. The page fetches the script itself and runs
// the copy: a script element does not say why the server did not send its file, and a busy server is asked again.
async function loadEngine() {
	try {
		const script = document.createElement('script');
		script.src = URL.createObjectURL(await (await ask(ENGINE, { cache: 'no-cache' })).blob());
		script.onerror = () => fail(ENGINE + ': the browser did not run the engine');
		document.body.appendChild(script);
	} catch (e) {
		fail(ENGINE + ': ' + words(e));
	}
}

async function start(manifest) {
	setPhase('starting');
	say('Starting the game');
	let frameWidth = canvas.clientWidth, frameHeight = canvas.clientHeight;
	const args = ['+set', 'fs_basepath', '/game', '+set', 'fs_homepath', '/home', '+set', 'fs_game', manifest.game,
		'+set', 'v_wsHost', location.host, '+set', 'r_mode', '-1',
		'+set', 'r_customwidth', String(frameWidth), '+set', 'r_customheight', String(frameHeight)];
	const query = new URLSearchParams(location.search);
	if (query.has('connect')) args.push('+connect', location.hostname);
	// (the packs of the TV have no sounds)
	else if (tvMode) args.push(...(tv.map ? ['+set', 's_initsound', '0'] : []), '+tv', location.host);
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
	if (tv.map) watchMap();

	// The frame keeps the size the renderer was started with, a canvas of another size only stretches it. So
	// when the window has had a new size for a moment, the renderer is started again with that size (the two
	// cvars take effect with a vid_restart, which loads the textures again: not something to do while dragging).
	// The page is what goes fullscreen (r_fullscreen of the game asks for it, shared/sys/web/sys_web.js), and a
	// player leaves it with a key of the browser the game does not see. The cvar follows the page then. It takes
	// effect with the vid_restart the new size brings.
	document.addEventListener('fullscreenchange', () => {
		if (vjk.phase === 'running') vjk.command('set r_fullscreen ' + (document.fullscreenElement ? 1 : 0));
	});

	let resizeTimer = 0;
	window.addEventListener('resize', () => {
		clearTimeout(resizeTimer);
		resizeTimer = setTimeout(() => {
			const width = canvas.clientWidth, height = canvas.clientHeight;
			if (vjk.phase !== 'running' || !width || !height || (width === frameWidth && height === frameHeight)) return;
			frameWidth = width;
			frameHeight = height;
			vjk.command(`set r_customwidth ${width}; set r_customheight ${height}; vid_restart`);
		}, RESIZE_WAIT);
	});
}

async function main() {
	if (typeof WebAssembly !== 'object') throw new Error('This browser has no WebAssembly: the game cannot run in it.');
	const probe = document.createElement('canvas').getContext('webgl2');
	if (!probe) throw new Error('This browser has no WebGL2 (or it is turned off, or the graphics driver is on its block list): the game cannot draw in it.');
	const lose = probe.getExtension('WEBGL_lose_context');
	if (lose) lose.loseContext();

	// the code of the game is fetched and compiled while the packs come, and what is wrong with it is said at
	// once and not after the packs
	const optional = OPTIONAL_MODULES.map(name => compile(name).catch(e => note(name + ': not there (' + words(e) + '), the game runs without it')));
	const modules = Promise.all([...SIDE_MODULES.map(name => named(name, compile(name))), ...optional]).then(() => true, e => { fail(words(e)); return false; });
	runtime.promise.then(() => prepared(ENGINE));
	loadEngine();

	say('Asking the server which game files there are');
	const manifest = (tvMode && await named('tv-index.json', getTVPacks())) || await named('manifest.json', getManifest());
	vjk.packs = manifest.packs;
	load.order = [...manifest.packs].sort((a, b) => a.size - b.size);
	load.total = manifest.packs.reduce((sum, pack) => sum + pack.size, 0);
	setPhase('packs');
	say('Looking what this browser has kept of the game files');
	try {
		db = await openStore();
		await pruneStore(tv.map ? tv.all : manifest.packs);
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
