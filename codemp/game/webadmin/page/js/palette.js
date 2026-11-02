/* ------------------------------------------------------------------ */
/* the command palette: every command and page of the panel by name,   */
/* Ctrl+K or the button Commands of the top bar                        */
/* ------------------------------------------------------------------ */

// A command that wants a value (a map, a bot, a number) takes the words after its name, "map mp/ffa3" or
// "change map mp/ffa3", and offers what the server has that fits. What the role of the account may not use is not
// offered, as the page does not show it either; the server refuses it anyway.
const PAL_MAX = 30;
let palItems = [], palAt = 0;
const palAsked = new Set();		// the lists of the server asked for while the palette was open

function palShown(el) { return !!el && el.offsetParent !== null; }

function palCommands() {
	const out = [];
	document.querySelectorAll('#pages button').forEach(b => {
		// (the rail shows a short label where the full name does not fit: the full one is its aria-label)
		if (palShown(b)) out.push({ group: 'Pages', name: b.getAttribute('aria-label') || b.textContent, about: b.title, run: () => b.click() });
	});
	if (palShown($('appMode'))) document.querySelectorAll('#appMode button').forEach(b => {
		out.push({ group: 'Modes', name: b.textContent + ' mode', about: b.title, run: () => setAppMode(b.dataset.app) });
	});
	if (appMode === 'editor') document.querySelectorAll('#tabs button[data-app="editor"]').forEach(b => {
		out.push({ group: 'Editor', name: b.textContent, about: 'A tool of the editor', run: () => setMode(b.dataset.tab) });
	});
	if (roleAtLeast('admin')) {
		out.push({ group: 'Server', name: 'Add bot', arg: 'name', pick: 'pickBots', about: 'A bot joins the game.', run: v => action({ action: 'addbot', value: v }) });
		out.push({ group: 'Server', name: 'Change map', arg: 'map', pick: 'pickMaps', about: 'Ends the running game and starts this map for everybody.',
			run: v => action({ action: 'map', value: v }) });
		out.push({ group: 'Server', name: 'Gravity', arg: 'number', about: 'g_gravity for everybody, 800 is normal' + (serverGravity !== '' ? ', now ' + serverGravity : '') +
			'. The map sets it again when it starts.', run: v => action({ action: 'gravity', value: v }) });
	}
	// what can be done to the selected player, by the names of the buttons of his panel (not Kick and Ban, which want a
	// second click to be sure)
	if (selected >= 0 && $('command').classList.contains('open')) {
		const who = $('cmdName').textContent;
		document.querySelectorAll('#command .actions button').forEach(b => {
			if (b.hasAttribute('data-confirm') || b.hidden || b.disabled) return;
			out.push({ group: 'Player', name: b.textContent.trim() + ' (' + who + ')', about: b.title || 'For the selected player.', run: () => b.click() });
		});
	}
	out.push({ group: 'View', name: 'Side panel', about: 'Shows or hides the side panel.', run: () => $('toggleSide').click() });
	out.push({ group: 'View', name: 'Keys of the map', about: 'Shows or hides what the keys and the mouse do on the map (?).', run: () => helpToggle() });
	if (palShown($('flyBtn'))) out.push({ group: 'View', name: 'Fly', about: $('flyBtn').title || 'The camera flies.', run: () => $('flyBtn').click() });
	if (palShown($('shotBtn'))) out.push({ group: 'View', name: 'Screenshot', about: $('shotBtn').title || 'A picture of the map view.', run: () => $('shotBtn').click() });
	return out;
}

// the commands that fit what is typed, the ones with a value first when their name was typed
function palFilter() {
	const q = $('palInput').value.trim(), ql = q.toLowerCase(), items = [];

	for (const c of palCommands()) {
		const n = c.name.toLowerCase();
		if (c.arg) {
			const head = [n, n.split(' ').pop()].find(h => ql === h || ql.startsWith(h + ' '));
			if (head !== undefined) {
				const v = q.slice(head.length).trim(), vl = v.toLowerCase();
				let options = [];
				if (c.pick) {
					if (!palAsked.has(c.pick)) { palAsked.add(c.pick); Promise.resolve(pickFill(c.pick)).then(() => { if (!$('palette').hidden) palFilter(); }); }
					options = [...$(c.pick).options].map(o => o.value).filter(o => o !== v && o.toLowerCase().includes(vl)).slice(0, 10);
				}
				// (what fits from the list of the server first, so Enter takes a name that is there; the words as typed after it)
				for (const o of options) items.push({ group: c.group, name: c.name + ': ' + o, about: c.about, run: () => c.run(o) });
				if (v) items.push({ group: c.group, name: c.name + ': ' + v, about: c.about, run: () => c.run(v) });
				continue;
			}
			if (!ql || n.includes(ql)) items.push({ group: c.group, name: c.name + ' ...', about: 'Type the ' + c.arg + ' after it. ' + c.about, fill: c.name + ' ' });
			continue;
		}
		if (!ql || n.includes(ql) || c.about.toLowerCase().includes(ql)) items.push(c);
	}
	palItems = items.slice(0, PAL_MAX);
	palAt = 0;
	palDraw();
}

function palDraw() {
	const list = $('palList');
	list.replaceChildren();
	if (!palItems.length) {
		const none = document.createElement('div');
		none.className = 'palEmpty';
		none.textContent = 'No command has these words.';
		list.appendChild(none);
		return;
	}
	// a heading where the group changes: Pages, Modes, Editor, Server, Player, View
	const rows = [];
	palItems.forEach((item, i) => {
		if (item.group && (!i || palItems[i - 1].group !== item.group)) {
			const head = document.createElement('div');
			head.className = 'palGroup';
			head.textContent = item.group;
			list.appendChild(head);
		}
		const row = document.createElement('div'), name = document.createElement('b'), about = document.createElement('span');
		row.className = 'palItem' + (i === palAt ? ' on' : '');
		name.textContent = item.name;
		about.textContent = item.about || '';
		row.append(name, about);
		row.addEventListener('mousedown', e => { e.preventDefault(); palAt = i; palRun(); });
		list.appendChild(row);
		rows.push(row);
	});
	const on = rows[palAt];
	if (on) on.scrollIntoView({ block: 'nearest' });
}

function palRun() {
	const item = palItems[palAt];
	if (!item) return;
	if (item.fill) {
		$('palInput').value = item.fill;
		palFilter();
		return;
	}
	palClose();
	item.run();
}

function palOpen() {
	$('palette').hidden = false;
	$('palInput').value = '';
	palFilter();
	$('palInput').focus();
}

function palClose() {
	$('palette').hidden = true;
	$('palInput').blur();
}

$('paletteBtn').addEventListener('click', palOpen);
// + Add bot over the players: Commands with the command typed, the name of the bot comes after it
$('addBotBtn').addEventListener('click', () => { palOpen(); $('palInput').value = 'Add bot '; palFilter(); });
$('palInput').addEventListener('input', palFilter);
$('palInput').addEventListener('keydown', e => {
	if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
		e.preventDefault();
		if (palItems.length) palAt = (palAt + (e.key === 'ArrowDown' ? 1 : palItems.length - 1)) % palItems.length;
		palDraw();
	} else if (e.key === 'Enter') {
		e.preventDefault();
		palRun();
	} else if (e.key === 'Escape') {
		e.preventDefault();
		e.stopPropagation();
		palClose();
	}
});
// a click beside the box closes it
$('palette').addEventListener('mousedown', e => { if (e.target === $('palette')) palClose(); });
document.addEventListener('keydown', e => {
	if ((e.ctrlKey || e.metaKey) && !e.altKey && e.key.toLowerCase() === 'k') {
		e.preventDefault();
		if ($('palette').hidden) palOpen(); else palClose();
	}
});

// the keys of the map, behind the button ? (the page keeps whether they are shown)
function helpToggle(on) {
	const show = on === undefined ? !document.body.classList.contains('helpOpen') : on;
	document.body.classList.toggle('helpOpen', show);
	try { localStorage.setItem('wa.help', show ? '1' : ''); } catch (e) { /* not kept */ }
}
$('helpBtn').addEventListener('click', () => helpToggle());
try { if (localStorage.getItem('wa.help')) document.body.classList.add('helpOpen'); } catch (e) { /* hidden then */ }
document.addEventListener('keydown', e => { if (e.key === '?' && !uiKeyIsTyped(e) && $('palette').hidden) helpToggle(); });
