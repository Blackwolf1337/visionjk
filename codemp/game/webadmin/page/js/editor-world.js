/* ------------------------------------------------------------------ */
/* world editor                                                        */
/* ------------------------------------------------------------------ */

let mode = 'players', placing = false, selectedEnt = -1, ghost = null, kvDirty = false, kvShown = '';
let boxGhost = null;		// { id, mins, maxs }: the box of a trigger while one of its faces is dragged
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
function entMatches(ent, f) {
	return !f || ent.classname.toLowerCase().includes(f) || entKey(ent, 'targetname').toLowerCase().includes(f) || String(ent.id) === f;
}
function entVisible(ent) { return entMatches(ent, $('entFilter').value.trim().toLowerCase()); }

// The two modes of the panel. Administration: the map as the game draws it, the players, the config and the
// files of the server. Editor: placing entities and models, in the plain view with its debug views.
let appMode = 'admin';
const APP_TAB = { admin: 'players', editor: 'world' };

function setAppMode(a) {
	appMode = a === 'editor' ? 'editor' : 'admin';
	document.body.classList.toggle('app-admin', appMode === 'admin');
	document.body.classList.toggle('app-editor', appMode === 'editor');
	document.querySelectorAll('#appMode button').forEach(b => b.classList.toggle('on', b.dataset.app === appMode));
	try { localStorage.setItem('appMode', appMode); } catch (e) { /* it is asked for again next time */ }
	// a tab of the other mode is not there any more
	const tab = document.querySelector('#tabs button[data-tab="' + mode + '"]');
	if (!tab || tab.dataset.app !== appMode) setMode(APP_TAB[appMode]);
	// the scripts: how the administration draws the map, and where the plain view finds the image of a shader
	loadShaderScripts();
	if (appMode === 'editor') { loadClassDocs(); pollEntities(); }
}

function setMode(m) {
	mode = m;
	if (m !== 'models') { mdlSetPlacing(false); hidePreview(); }
	document.querySelectorAll('#tabs button').forEach(b => b.classList.toggle('on', b.dataset.tab === m));
	document.querySelectorAll('.pane').forEach(p => p.classList.toggle('on', p.id === 'pane-' + m));
	if (m === 'world') { select(-1); pollEntities(); loadClasses(); }
	else { selectEnt(-1); setPlacing(false); }
	document.body.classList.toggle('cfgOpen', m === 'config');
	document.body.classList.toggle('fsOpen', m === 'files');
	document.body.classList.toggle('logOpen', m === 'log');
	if (m === 'log') { select(-1); logPoll(); }
	if (m === 'bans') banLoad();
	if (m === 'load') { select(-1); perfPoll(); }
	if (m === 'config') { select(-1); if (!cfgLoaded) loadConfig(); }
	if (m === 'files') { select(-1); if (!fsEdit) fsList(); }
	if (m === 'models') { select(-1); if (!mdlAsked) mdlLoad(); mdlSetPlacing(!!mdlPath); }
	if (m === 'routes') { select(-1); wpLoad(); } else wpSetAdding(false);
	if (m === 'mapinfo') { select(-1); mapReport(); renderSurface(); }
}

