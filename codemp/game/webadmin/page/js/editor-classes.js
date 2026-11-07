/* ------------------------------------------------------------------ */
/* the tools of the editor                                             */
/* ------------------------------------------------------------------ */

// Not in JKAdmin. What a level editor has on top of a table of keys: what a class is (read from the game code),
// the box of an entity and what it is linked to, moving by numbers and on a grid, undo and redo, and how much
// of what the engine has a limit for is in use.

// ---- the entity definitions of the game code: /api/classdoc is every QUAKED comment of its source files

const classDocs = new Map();	// classname in lower case -> { name, color, mins, maxs, flags, text, keys, keyDocs }
let classDocsAsked = false;

// The keys a definition tells of, each with what it says about it. The definitions write them in two ways:
// "key" what it does, with or without a - or : between, and key - what it does. The second way is also how they
// describe a spawnflag, and how a sentence may begin: so only for a name that is written small (or NPC_...) and
// is no flag of the class.
function parseKeyDocs(body, flags) {
	const out = new Map(), flag = new Set(flags.map(f => f.toLowerCase()));
	for (const line of body.split('\n')) {
		const m = /^\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*[-:=]?\s*(.*)$/.exec(line) || /^\s*([a-z][A-Za-z0-9_]*|NPC_[A-Za-z0-9_]+)\s*(?:-|:|=)\s+(.*)$/.exec(line);
		if (m && !flag.has(m[1].toLowerCase()) && !out.has(m[1])) out.set(m[1], m[2].trim());
	}
	return out;
}

function parseClassDocs(text) {
	const vec = t => t.trim().split(/\s+/).map(Number);
	const three = v => v.length === 3 && v.every(Number.isFinite);
	for (const block of text.split(/^\/\*QUAKED\s+/m).slice(1)) {
		const nl = block.indexOf('\n'), head = (nl < 0 ? block : block.slice(0, nl)).trim();
		// name (colour) (mins) (maxs) FLAG FLAG ..., with a ? for the box of a class that is made of brushes
		const m = /^(\S+?)\s*\(([^)]*)\)\s*(?:\?|\(([^)]*)\)\s*\(([^)]*)\))?\s*(.*)$/.exec(head);
		if (!m || classDocs.has(m[1].toLowerCase())) continue;
		const body = (nl < 0 ? '' : block.slice(nl + 1)).replace(/\s*\*\/\s*$/, '').trim();
		const color = vec(m[2]), mins = m[3] ? vec(m[3]) : null, maxs = m[4] ? vec(m[4]) : null;
		const boxed = !!mins && !!maxs && three(mins) && three(maxs);
		const flags = m[5].trim() ? m[5].trim().split(/\s+/) : [], keyDocs = parseKeyDocs(body, flags);
		classDocs.set(m[1].toLowerCase(), {
			name: m[1],
			// a class that is defined black would not be seen here
			color: three(color) && Math.max(...color) >= 0.25 ? color : null,
			mins: boxed ? mins : null,
			maxs: boxed ? maxs : null,
			flags,
			text: body,
			keys: [...keyDocs.keys()],
			keyDocs
		});
	}
}

async function loadClassDocs() {
	loadClassKeys();
	if (classDocsAsked) return;
	classDocsAsked = true;
	try {
		const res = await fetch('api/classdoc');
		if (!res.ok) return;	// a game module without it: the editor is what it was
		parseClassDocs(await res.text());
		kvShown = '';
		renderEntPanel();
		if ($('classPicker').classList.contains('open')) renderPicker();
	} catch (e) {
		classDocsAsked = false;
	}
}

const classDoc = cls => classDocs.get((cls || '').toLowerCase());
// the colour its definition gives a class, as a map editor shows it
function classColor(cls) {
	const d = classDoc(cls);
	return d && d.color ? d.color : entColor(cls);
}
function classSummary(cls) {
	const d = classDoc(cls);
	return d ? d.text.split('\n').map(l => l.trim()).filter(Boolean).slice(0, 3).join(' ').slice(0, 300) : '';
}

