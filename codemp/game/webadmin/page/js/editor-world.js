/* ------------------------------------------------------------------ */
/* world editor                                                        */
/* ------------------------------------------------------------------ */

let mode = 'players', placing = false, selectedEnt = -1, ghost = null, kvDirty = false, kvShown = '';
let boxGhost = null;		// { id, mins, maxs }: the box of a trigger or of a wall while one of its faces is dragged
// The drafts: entities that are only in the page until they are placed (editor-drafts.js). Each is in ents like an
// entity of the server, under a number the server never gives one: its own stay below 1024.
const DRAFT_BASE = 100000;
const drafts = new Map();	// id -> draft, in the order they were made
let draftNext = 0;
const ents = new Map();

const ENT_COLORS = [
	[/^weapon_/, [1, 0.6, 0.15]], [/^(item_|ammo_|holocron)/, [0.3, 0.95, 0.45]], [/^info_/, [0.95, 0.3, 0.3]],
	[/^npc_/i, [0.95, 0.35, 0.95]], [/^misc_/, [0.75, 0.75, 0.8]], [/^trigger_/, [0.95, 0.9, 0.2]], [/^func_/, [0.3, 0.55, 1]],
	[/^target_/, [0.3, 0.9, 0.95]], [/^fx_/, [1, 0.85, 0.5]], [/^light/, [1, 1, 0.7]], [/^team_/, [1, 1, 1]]];
function entColor(cls) {
	for (const [re, c] of ENT_COLORS) if (re.test(cls)) return c;
	return [0.8, 0.8, 0.8];
}
function entKey(ent, key) {
	const kv = ent.keys && ent.keys.find(k => k[0].toLowerCase() === key);
	return kv ? kv[1] : '';
}
// #/<page> of the address: a page of the server, a tool of the editor (which opens the editor) or the players
function modeFromHash() {
	const m = location.hash.replace(/^#\/?/, '');
	if (SERVER_PAGES.has(m)) { if (document.querySelector('#pages button[data-tab="' + m + '"]').offsetParent) setMode(m); return; }
	if (EDITOR_TOOLS.has(m) && !$('appMode').hidden && $('appMode').offsetParent) { if (appMode !== 'editor') setAppMode('editor'); setMode(m); return; }
	if (m === 'players' && appMode !== 'admin') setAppMode('admin');
}
window.addEventListener('hashchange', modeFromHash);

function entMatches(ent, f) {
	return !f || ent.classname.toLowerCase().includes(f) || entKey(ent, 'targetname').toLowerCase().includes(f) || String(ent.id) === f;
}
function entVisible(ent) { return entMatches(ent, $('entFilter').value.trim().toLowerCase()); }

// The two modes of the panel. Administration: the map as the game draws it, the players, the config and the
// files of the server. Editor: placing entities and models, in the plain view with its debug views.
let appMode = 'admin';
const APP_TAB = { admin: 'players', editor: 'world' };
// the pages of the server in the row under the top bar; the rest of the modes is the map with its side panel
const SERVER_PAGES = new Set(['tv', 'config', 'files', 'log', 'bans', 'load', 'account', 'master']);
const EDITOR_TOOLS = new Set(['world', 'models', 'clips', 'remaps', 'routes', 'mapinfo']);
let lastMapTool = 'players';		// the tool of the map that was open last, for the page Map

function setAppMode(a) {
	appMode = a === 'editor' ? 'editor' : 'admin';
	document.body.classList.toggle('app-admin', appMode === 'admin');
	document.body.classList.toggle('app-editor', appMode === 'editor');
	document.querySelectorAll('#appMode button').forEach(b => b.classList.toggle('on', b.dataset.app === appMode));
	try { localStorage.setItem('appMode', appMode); } catch (e) { /* it is asked for again next time */ }
	// a tool of the other mode is not there any more; a page of the server is there in both (#pages)
	const tab = document.querySelector('#tabs button[data-tab="' + mode + '"]');
	if (!SERVER_PAGES.has(mode) && (!tab || tab.dataset.app !== appMode)) setMode(APP_TAB[appMode]);
	// the drafts of the texture swap are drawn in the editor only (js/editor-remaps.js)
	if (remap.drafts.size) remapApply();
	// the scripts: how the administration draws the map, and where the plain view finds the image of a shader
	loadShaderScripts();
	if (appMode === 'editor') { loadClassDocs(); pollEntities(); }
}

function setMode(m) {
	mode = m;
	if (!SERVER_PAGES.has(m)) lastMapTool = m;
	if (m !== 'models') { mdlSetPlacing(false); hidePreview(); }
	document.querySelectorAll('#tabs button').forEach(b => b.classList.toggle('on', b.dataset.tab === m));
	document.querySelectorAll('#pages button').forEach(b => b.classList.toggle('on', SERVER_PAGES.has(m) ? b.dataset.tab === m : b.dataset.tab === 'map'));
	// (the address names the page, so a reload or a link comes back to it)
	if (location.hash !== '#/' + m) history.replaceState(null, '', '#/' + m);
	document.querySelectorAll('.pane').forEach(p => p.classList.toggle('on', p.id === 'pane-' + m));
	if (m === 'world') { select(-1); pollEntities(); loadClasses(); }
	else { selectEnt(-1); setPlacing(false); }
	// a page of the server: no chat beside it; the ones that are only a list take the whole width, the map steps aside
	document.body.classList.toggle('serverPage', SERVER_PAGES.has(m));
	document.body.classList.toggle('pageWide', m === 'bans' || m === 'load' || m === 'account');
	document.body.classList.toggle('cfgOpen', m === 'config');
	document.body.classList.toggle('fsOpen', m === 'files');
	document.body.classList.toggle('logOpen', m === 'log');
	document.body.classList.toggle('masterOpen', m === 'master');
	document.body.classList.toggle('tvOpen', m === 'tv');
	if (m === 'tv') { select(-1); tvOpen(); } else tvLeave();
	if (m === 'log') { select(-1); logPoll(); }
	if (m === 'bans') banLoad();
	if (m === 'account') { select(-1); accountLoad(); }
	if (m === 'load') { select(-1); perfPoll(); }
	if (m === 'config') { select(-1); cfgOpen(); }
	if (m === 'master') masterPoll();
	if (m === 'files') { select(-1); if (!fsEdit) fsList(); }
	if (m === 'models') { select(-1); if (!mdlAsked) mdlLoad(); mdlSetPlacing(!!mdlPath); }
	if (m === 'routes') { select(-1); wpLoad(); } else wpSetAdding(false);
	if (m === 'mapinfo') { select(-1); mapReport(); renderSurface(); }
	if (m === 'clips') { select(-1); clipPoll(); clipRender(); } else clipLeave();
	if (m === 'remaps') { select(-1); remapPoll(); remapRender(); }
	texSync();
}

