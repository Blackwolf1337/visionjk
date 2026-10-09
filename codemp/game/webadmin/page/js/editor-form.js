// ---- the form of an entity: the keys its class is known to take, each as a field of its kind
//
// Above the table of all keys the panel shows what the class of the entity takes: what its definition tells of and
// what its spawn function reads (keyChoices, the ones from 'class'). A number is a number field, a vector three of
// them, a yes or no a box to tick, the rest text, with the lists, the search for files and the Play button the
// table has for the same keys. The form holds nothing itself, it is another view of the table: a field writes its
// value into the row of its key, a field that is emptied takes the row away, and what is typed into the table
// shows in its field. So Apply, Place, the drafts and undo read the table as they did. A key the class is not
// known to take is in the table alone.

// the keys that have a control of their own in the panel
const FORM_SKIP = new Set(['classname', 'origin', 'angles', 'angle', 'spawnflags', 'mins', 'maxs']);
// what a number field of the browser takes as its value
const FORM_NUMBER = /^-?(\d+(\.\d+)?|\.\d+)(e[-+]?\d+)?$/i;
const formFields = new Map();	// key in small letters -> { key, c, n, tip }: a field of the form, c is what is known of its key
let formSig = '';				// the class and the fields the form was built for
let kvOpen = null;				// the table of all keys as the user left it, unfolded or folded. null: it was not touched yet.

// what the game reads as yes (G_SpawnBoolean)
const formTrue = v => /^(q?true|yes|1)$/i.test(v);

// How the value of a key is shown: by the kind of the key. A value that is not of that kind is shown as it is, as
// text: a map may hold anything, and a number field would show it as nothing.
function formShape(c, value) {
	const three = v => v.length === 3 && v.every(n => FORM_NUMBER.test(n));
	if (c.kind === 'Boolean') return !value || /^(q?true|q?false|yes|no|0|1)$/i.test(value) ? 'check' : 'text';
	if (c.kind === 'Vector') return !value || three(value.split(/\s+/)) ? 'vec' : 'text';
	if (c.kind === 'Int' || c.kind === 'Float') return !value || FORM_NUMBER.test(value) ? 'num' : 'text';
	return 'text';
}

// the fields of one key in that shape, with its value in them. The first one is what its label is for.
function formCell(f, value, shape) {
	const c = f.c, def = c.def || '', id = n => n ? '' : ' id="entField' + f.n + '"', tip = ' title="' + escapeHtml(f.tip) + '"';
	const num = (v, hold, n) => '<input type="number" step="' + (c.kind === 'Int' ? '1' : 'any') + '"' + id(n) + ' value="' + escapeHtml(v || '') + '" placeholder="' + escapeHtml(hold) + '"' + tip + '>';
	if (shape === 'check') return '<input type="checkbox"' + id(0) + (formTrue(value || def) ? ' checked' : '') + tip + '>';
	if (shape === 'vec') {
		const v = value ? value.split(/\s+/) : [], d = def.trim().split(/\s+/);
		return [0, 1, 2].map(n => num(v[n], d.length === 3 ? d[n] : '', n)).join('');
	}
	if (shape === 'num') return num(value, def, 0);
	return '<input' + id(0) + ' value="' + escapeHtml(value) + '" placeholder="' + escapeHtml(def) + '" spellcheck="false" autocomplete="off"' + tip + '>' + playButton(f.key);
}

// A cell shows this value. Its fields are only made anew when the value is of another shape than they are for,
// and a field that shows the value already is left alone.
function formShow(cell, f, value) {
	const shape = formShape(f.c, value), fields = [...cell.querySelectorAll('input')];
	if (shape !== cell.dataset.shape) {
		cell.dataset.shape = shape;
		cell.innerHTML = formCell(f, value, shape);
	} else if (shape === 'check') {
		fields[0].checked = formTrue(value || f.c.def || '');
	} else {
		const v = shape === 'vec' ? (value ? value.split(/\s+/) : ['', '', '']) : [value];
		fields.forEach((el, n) => { if (el.value !== v[n]) el.value = v[n]; });
	}
}

// The value the fields of a cell stand for, '' for none. A box that is not ticked is no value, unless the key is
// yes without one. A vector that is begun has 0 where nothing is typed.
function formRead(cell, f) {
	const fields = [...cell.querySelectorAll('input')];
	if (cell.dataset.shape === 'check') return fields[0].checked ? '1' : formTrue(f.c.def || '') ? '0' : '';
	if (cell.dataset.shape !== 'vec') return fields[0].value.trim();
	const v = fields.map(el => el.value.trim());
	return v.some(Boolean) ? v.map(n => n || '0').join(' ') : '';
}

// The form for what the table of keys holds now. fresh: the table was built anew, and so is the form. Without it
// the form is only built again when the class or what is known of its keys changed (a classname was typed, the
// keys of the classes came in): else the fields that are there take their values from the table, all but the one
// the cursor is in. So a field keeps focus and cursor, as the rows of the table do.
function renderEntForm(fresh) {
	const ent = ents.get(selectedEnt), editable = !!(ent && ent.keys), cls = panelClass();
	const list = editable ? keyChoices(cls).filter(c => c.from === 'class' && !FORM_SKIP.has(c.key.toLowerCase())) : [];
	const sig = cls.toLowerCase() + '|' + list.map(c => c.key + ':' + (c.kind || '') + ':' + (c.def || '')).join('|');
	// a key that is there twice counts as its first row, which is the one a field writes to
	const keys = panelKeys(), values = new Map();
	for (const [k, v] of keys) if (!values.has(k.toLowerCase())) values.set(k.toLowerCase(), v);
	const made = !!fresh || sig !== formSig;
	if (made) {
		formSig = sig;
		formFields.clear();
		list.forEach((c, n) => formFields.set(c.key.toLowerCase(), { key: c.key, c, n, tip: [keyHint(c), KIND_WORDS[c.kind]].filter(Boolean).join(' · ') }));
		$('entForm').innerHTML = [...formFields].map(([lower, f]) => {
			const value = values.get(lower) || '', shape = formShape(f.c, value);
			return '<label for="entField' + f.n + '" title="' + escapeHtml(f.tip) + '"><b>' + escapeHtml(f.key) + '</b><i>' + escapeHtml(keyHint(f.c)) + '</i></label>' +
				'<span class="v" data-key="' + escapeHtml(lower) + '" data-shape="' + shape + '">' + formCell(f, value, shape) + '</span>';
		}).join('');
	} else {
		for (const cell of $('entForm').querySelectorAll('.v')) {
			if (!cell.contains(document.activeElement)) formShow(cell, formFields.get(cell.dataset.key), values.get(cell.dataset.key) || '');
		}
	}
	$('kvCount').textContent = editable ? '(' + keys.length + ')' : '';
	// The table of all keys: folded when the form has fields, and as the user left it once it was touched. Always
	// unfolded for what only the table can show: that there are no keys to edit, and a key that waits for its value
	// and has no field. Not while the cursor is in it.
	if (made && !$('kvBlock').contains(document.activeElement)) {
		const waits = keys.some(k => !k[1] && !formFields.has(k[0].toLowerCase()));
		$('kvBlock').open = !editable || waits || (kvOpen === null ? !formFields.size : kvOpen);
	}
}

// The field a key is typed into: the one of the form, or the one of its row in the table of all keys, which is
// unfolded for it. null: the entity has no such key and the form no field for it.
function keyField(key) {
	const lower = key.toLowerCase(), cell = formFields.has(lower) && [...$('entForm').querySelectorAll('.v')].find(el => el.dataset.key === lower);
	if (cell) return cell.querySelector('input');
	const row = panelRow(key);
	if (row) $('kvBlock').open = true;
	return row ? row.querySelectorAll('input')[1] : null;
}

$('entForm').addEventListener('input', e => {
	const cell = e.target.closest('.v'), f = cell && formFields.get(cell.dataset.key);
	// a number that is half typed ("-", "1e") is no value yet
	if (!f || e.target.validity.badInput) return;
	const value = formRead(cell, f);
	if (value) {
		setPanelKey(f.key, value);
	} else {
		for (let row; (row = panelRow(f.key));) row.remove();
		renderEntForm();
	}
	// what typing into the table does
	kvDirty = true;
	draftTyped();
	if (cell.dataset.shape === 'text' && cell.dataset.key in PATH_KEYS) pickPath(e.target, cell.dataset.key);
});
$('entForm').addEventListener('focusin', e => {
	const cell = e.target.closest('.v');
	if (cell && cell.dataset.shape === 'text' && e.target.tagName === 'INPUT') pickList(e.target, cell.dataset.key);
});
$('entForm').addEventListener('click', e => {
	const cell = e.target.closest('.v');
	if (cell && e.target.classList.contains('play')) playValue(cell.dataset.key, formRead(cell, formFields.get(cell.dataset.key)));
});
// a click on its title folds or unfolds the table of all keys: it is on its way to the other state here
$('kvBlock').querySelector('summary').addEventListener('click', () => { kvOpen = !$('kvBlock').open; });
