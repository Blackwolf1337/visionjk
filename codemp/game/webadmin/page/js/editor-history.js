// ---- undo and redo

// The server spawns an entity anew with every change, so it has another number afterwards. The history names an
// entity by a reference that is told the new number.
const hist = { undo: [], redo: [], refs: new Map(), map: '' };

function histRef(id) {
	let ref = hist.refs.get(id);
	if (!ref) { ref = { id }; hist.refs.set(id, ref); }
	return ref;
}
function histRenumber(ref, id) {
	if (hist.refs.get(ref.id) === ref) hist.refs.delete(ref.id);
	ref.id = id;
	hist.refs.set(id, ref);
}
function histClear() {
	hist.undo.length = 0;
	hist.redo.length = 0;
	hist.refs.clear();
}
function histButtons() {
	$('entUndo').disabled = !hist.undo.length;
	$('entRedo').disabled = !hist.redo.length;
}
// another map has other entities under the numbers
function histOfThisMap() {
	if (hist.map !== loadedMap) { histClear(); hist.map = loadedMap; }
}

// the step of the history a change is, null for one that can not be taken back
function histStepFor(params, ent, before, newId) {
	histOfThisMap();
	if (params.op === 'spawn') return { op: 'spawn', ref: histRef(newId), after: params.kv };
	if (params.op === 'update' && before !== null) {
		const ref = histRef(+params.id);
		histRenumber(ref, newId);
		return { op: 'update', ref, before, after: params.kv };
	}
	if (params.op === 'delete' && before !== null) return { op: 'delete', ref: histRef(+params.id), before, ofMap: !ent.placed };
	return null;
}

function histPush(step) {
	histOfThisMap();
	if (step) {
		hist.undo.push(step);
		if (hist.undo.length > 200) hist.undo.shift();
		hist.redo.length = 0;
	} else {
		// what has no keys, or a whole class at once: that can not be taken back, and the steps before it may
		// name entities that are gone with it
		histClear();
	}
	histButtons();
}

let histBusy = false;
async function histStep(undo) {
	histOfThisMap();
	const from = undo ? hist.undo : hist.redo, to = undo ? hist.redo : hist.undo;
	if (histBusy || !from.length) return;
	histBusy = true;
	const step = from.pop();
	let done;
	if (step.op === 'group') {
		// several changes that were made together are taken back together, the last one first
		done = true;
		for (const part of undo ? step.steps.slice().reverse() : step.steps) if (!(await histApply(part, undo, true))) done = false;
		kvDirty = false;
		await pollEntities();
		pickOnly(step.steps.map(part => part.ref.id).filter(id => ents.has(id)));
		toast((undo ? 'Undone' : 'Redone') + (done ? '' : ', not all of it: ' + entRefused), done);
	} else {
		done = await histApply(step, undo, false);
	}
	// refused: something else changed or removed an entity meanwhile. The step is dropped.
	if (done) to.push(step);
	histBusy = false;
	histButtons();
}

// one step of the history on the server: the keys of the other side, or the delete of a spawn and the other way round
async function histApply(step, undo, quiet) {
	let params;
	if (step.op === 'update') {
		params = { op: 'update', id: step.ref.id, kv: undo ? step.before : step.after };
	} else if ((step.op === 'spawn') === undo) {
		params = { op: 'delete', id: step.ref.id };
	} else {
		params = { op: 'spawn', kv: step.op === 'spawn' ? step.after : step.before };
		// an entity of the map that comes back stays one of the map
		if (step.ofMap) params.map = 1;
	}
	const json = await entityOp(params, undo ? 'Undone' : 'Redone', false, quiet);
	if (json && params.op !== 'delete') histRenumber(step.ref, json.id);
	return !!json;
}
$('entUndo').addEventListener('click', () => histStep(true));
$('entRedo').addEventListener('click', () => histStep(false));

setInterval(pollEntities, 1000);

