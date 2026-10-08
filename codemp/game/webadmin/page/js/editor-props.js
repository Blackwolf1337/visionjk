/* ------------------------------------------------------------------ */
/* the models of the game as props                                     */
/* ------------------------------------------------------------------ */

let mdlAll = [], mdlDone = false, mdlAsked = false, mdlPath = '', mdlPlacing = false, mdlHover = 0;

// the server reads the folders of the game a few at a frame, so the list comes in parts
async function mdlLoad() {
	mdlAsked = true;
	try {
		const json = await (await fetch('api/models', { cache: 'no-store' })).json();
		mdlAll = [...new Set(json.models)].sort();
		mdlDone = json.done;
		mdlRender();
		if (!mdlDone) setTimeout(mdlLoad, 700);
	} catch (e) {
		toast('Could not read the list of models: ' + e.message, false);
		mdlAsked = false;
	}
}
function mdlMatches() {
	const words = $('mdlFilter').value.trim().toLowerCase().split(/\s+/).filter(Boolean);
	return mdlAll.filter(p => { const low = p.toLowerCase(); return words.every(w => low.includes(w)); });
}
function mdlName(path) { return path.slice(path.lastIndexOf('/') + 1); }
function mdlRender() {
	const list = mdlMatches(), shown = list.slice(0, 500);
	$('mdlCount').textContent = (mdlDone ? '' : 'reading, ') + list.length + (list.length < mdlAll.length ? ' of ' + mdlAll.length : '');
	// "models/" is in front of all of them
	let html = shown.map(p => '<div class="cls' + (p === mdlPath ? ' hl' : '') + '" data-path="' + escapeHtml(p) + '">' + escapeHtml(mdlName(p)) +
		'<span class="ty">' + escapeHtml(p.slice(7, Math.max(7, p.lastIndexOf('/')))) + '</span></div>').join('');
	if (list.length > shown.length) html += '<div class="cls">' + (list.length - shown.length) + ' more, narrow the search</div>';
	$('mdlList').innerHTML = html || '<div class="cls">' + (mdlDone ? 'No model matches' : 'Reading the folders of the game...') + '</div>';
}
function mdlSetPlacing(on) {
	mdlPlacing = on && !!mdlPath;
	$('mdlPlace').classList.toggle('armedPlace', mdlPlacing);
	$('mdlPlace').textContent = mdlPlacing ? 'Right-click map' : 'Place';
	if (mode === 'models') canvas.style.cursor = mdlPlacing ? 'crosshair' : 'default';
}
function mdlPick(path) {
	mdlPath = path;
	$('mdlPicked').textContent = path;
	mdlRender();
	mdlSetPlacing(true);
	toast('Right-click on the map to place ' + mdlName(path));
}
async function mdlPlaceAt(hit) {
	const path = mdlPath, scale = Math.max(1, Math.min(1023, Math.round(+$('mdlScale').value) || 100));
	// The origin of a misc_prop is the middle of its box. It goes half the height of the model above the floor
	// and a bit, and drops from there. Without the model at hand a guess does.
	const model = pvSetup() ? await pvLoadModel(path) : null;
	const height = model ? model.size[2] * scale / 100 : 64;
	const keys = [['classname', 'misc_prop'], ['origin', fmtVec([hit[0], hit[1], hit[2] + height / 2 + 8])], ['model', path], ['scale', String(scale)]];
	if ($('mdlShape').value !== 'model') keys.push(['shape', $('mdlShape').value]);
	if (+$('mdlMass').value > 0) keys.push(['mass', String(+$('mdlMass').value)]);
	entityOp({ op: 'spawn', kv: keysText(keys) }, 'Placed ' + mdlName(path));
}

$('mdlClear').addEventListener('click', () => {
	if (confirmed($('mdlClear'))) entityOp({ op: 'clear', kv: 'classname=misc_prop' });
});
$('mdlFilter').addEventListener('input', mdlRender);
$('mdlPlace').addEventListener('click', () => {
	if (!mdlPath) { toast('Pick a model first', false); return; }
	mdlSetPlacing(!mdlPlacing);
});
$('mdlList').addEventListener('click', e => {
	const row = e.target.closest('.cls[data-path]');
	if (row) mdlPick(row.dataset.path);
});
$('mdlList').addEventListener('mouseover', e => {
	const row = e.target.closest('.cls[data-path]');
	if (!row) return;
	clearTimeout(mdlHover);
	mdlHover = setTimeout(() => pvShowPath(row.dataset.path, row, mdlName(row.dataset.path), row.dataset.path, row.dataset.path, 'No preview here'), 80);
});
$('mdlList').addEventListener('mouseleave', () => { clearTimeout(mdlHover); hidePreview(); });

