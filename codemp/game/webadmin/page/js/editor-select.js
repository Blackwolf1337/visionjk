// ---- several entities at once: Ctrl and a click selects one more. What is done to the selected one is done to
// all of them, and is one step of the history.

const picked = new Set();	// the ones that are selected besides selectedEnt
// all that are selected and still there, the one the panel shows first
function selectedIds() { return [...new Set([selectedEnt, ...picked])].filter(id => ents.has(id)); }

// Every entity whose marker is in the rectangle on the screen is selected, the one nearest to where the drag
// began as the one the panel shows. more: they are added to what is selected already.
function bandSelect(b, more) {
	const x0 = Math.min(b.x, b.x2), x1 = Math.max(b.x, b.x2), y0 = Math.min(b.y, b.y2), y1 = Math.max(b.y, b.y2), inside = [];
	for (const ent of ents.values()) {
		if (!entVisible(ent)) continue;
		const sp = project(ent.pos);
		if (sp && sp[0] >= x0 && sp[0] <= x1 && sp[1] >= y0 && sp[1] <= y1) inside.push([Math.hypot(sp[0] - b.x, sp[1] - b.y), ent.id]);
	}
	inside.sort((p, q) => p[0] - q[0]);
	const ids = inside.map(p => p[1]);
	if (more && selectedEnt >= 0) pickOnly([...new Set([selectedEnt, ...picked, ...ids])]);
	else pickOnly(ids);
	if (ids.length) toast(ids.length + ' in the rectangle');
}

// the selection is these and nothing else
function pickOnly(ids) {
	picked.clear();
	ids.slice(1).forEach(id => picked.add(id));
	selectEnt(ids.length ? ids[0] : -1);
}

// A change to every selected entity, one after the other. what( ent ) is the change, null for an entity it can
// not be made to. A draft among them is changed here in the page (entityOp), and is no part of the step of the
// history: the server never had it.
async function groupRun(label, what) {
	const list = selectedIds().map(id => ents.get(id)), steps = [], now = [];
	let refused = 0, lost = false;
	for (const ent of list) {
		const params = what(ent), before = ent.keys ? keysText(ent.keys) : null;
		const json = params && await entityOp(params, null, false, true);
		if (!json) { refused++; continue; }
		if (!ent.draft) {
			const step = histStepFor(params, ent, before, json.id);
			if (step) steps.push(step); else lost = true;
		}
		if (params.op !== 'delete') now.push(json.id);
	}
	await groupDone(label, steps, lost, now, list.length - refused, refused);
}

async function groupDone(label, steps, lost, now, done, refused) {
	// one that can not be taken back is in it (an entity without keys): the history can not go behind it
	if (lost) histPush(null); else if (steps.length) histPush({ op: 'group', steps });
	kvDirty = false;
	await pollEntities();
	pickOnly(now.filter(id => ents.has(id)));
	// one alone that was refused: "Not placed", and why, instead of a count of none and one
	toast(!done && refused === 1 ? 'Not ' + label.charAt(0).toLowerCase() + label.slice(1) + ': ' + entRefused : label + ' ' + done + (refused ? ', ' + refused + ' refused: ' + entRefused : ''), !refused);
}

