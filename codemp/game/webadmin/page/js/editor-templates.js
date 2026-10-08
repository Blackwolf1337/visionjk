// ---- templates: the keys of an entity under a name, kept by the browser, to place more of the same

let templates = {};
try { templates = JSON.parse(localStorage.getItem('templates') || '{}') || {}; } catch (e) { /* none then */ }
function templatesKeep() {
	try { localStorage.setItem('templates', JSON.stringify(templates)); } catch (e) { toast('The browser does not keep it', false); }
}

$('entTemplate').addEventListener('click', () => {
	const ent = ents.get(selectedEnt);
	if (!ent || !ent.keys) return;
	if (ent.bmodel) { toast('Brush models make no template, they use the map geometry', false); return; }
	const name = (prompt('Name of the template', entKey(ent, 'targetname') || ent.classname) || '').trim().replace(/[@"<>]/g, '');
	if (!name) return;
	// without its place: that is where it is put
	templates[name] = panelKeys().filter(k => k[0].toLowerCase() !== 'origin');
	templatesKeep();
	toast('Template ' + name + ' kept: search for it where the classes are, Shift+click there takes it away');
});

// ---- import: every entity of a .ent file is placed, one after the other

function parseEnt(text) {
	const out = [];
	for (const m of text.matchAll(/\{([^{}]*)\}/g)) {
		const keys = [...m[1].matchAll(/"([^"]*)"\s+"([^"]*)"/g)].map(k => [k[1], k[2]]);
		if (keys.length) out.push(keys);
	}
	return out;
}

$('entImport').addEventListener('click', () => $('entImportFile').click());
$('entImportFile').addEventListener('change', async () => {
	const file = $('entImportFile').files[0];
	$('entImportFile').value = '';
	if (!file) return;
	const all = parseEnt(await file.text()), value = (keys, key) => (keys.find(k => k[0].toLowerCase() === key) || [])[1] || '';
	// not the worldspawn, and nothing that is made of the brushes of the map it came from
	const list = all.filter(keys => value(keys, 'classname').toLowerCase() !== 'worldspawn' && value(keys, 'model')[0] !== '*');
	if (!list.length) { toast('No entity to place in ' + file.name, false); return; }
	if (!confirm('Place ' + list.length + ' entities of ' + file.name + '?' + (all.length > list.length ? ' ' + (all.length - list.length) + ' are left out (worldspawn, brush models).' : ''))) return;
	let placed = 0, refused = 0, why = '';
	for (const keys of list) {
		try {
			const json = await (await fetch('api/entity', { method: 'POST', body: new URLSearchParams({ op: 'spawn', kv: keysText(keys) }) })).json();
			if (json.ok) placed++; else { refused++; why = why || value(keys, 'classname') + ': ' + json.message; }
		} catch (e) {
			refused++;
			why = why || e.message;
		}
	}
	// that many at once are not one step of undo, and the steps before name numbers that may be taken now
	histClear();
	histButtons();
	await pollEntities();
	toast('Imported ' + placed + (refused ? ', ' + refused + ' refused (' + why + ')' : ''), !refused);
});

// ---- where the mouse is on the map, and how far that is from the selected entity

let coordsWanted = false;
canvas.addEventListener('mousemove', () => { coordsWanted = true; });
canvas.addEventListener('mouseleave', () => { $('coords').textContent = ''; });
setInterval(() => {
	if (!coordsWanted || appMode !== 'editor' || !mouseAt || !world || drag) return;
	coordsWanted = false;
	const ray = screenRay(mouseAt[0], mouseAt[1]), hit = snapHit(pickGround(ray.origin, ray.dir)), sel = ents.get(selectedEnt);
	$('coords').textContent = hit ? fmtVec(hit) + (sel ? '   ' + Math.round(Math.hypot(hit[0] - sel.pos[0], hit[1] - sel.pos[1], hit[2] - sel.pos[2])) + ' from #' + sel.id : '') : '';
}, 80);

