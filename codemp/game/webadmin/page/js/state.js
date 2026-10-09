/* ------------------------------------------------------------------ */
/* game state                                                          */
/* ------------------------------------------------------------------ */

let state = null;
let selected = -1;
const players = new Map();		// id -> { data, prev, draw, t }
const labels = new Map();
let lastPoll = 0, pollInterval = 100;

let itemsRev = 0, itemsKept = [];		// the number of the item list the server sent last, and that list

// the flags of an entity the page looks at (eFlags, bg_public.h)
const EF_NODRAW = 1 << 8, EF_ITEMPLACEHOLDER = 1 << 23, EF_DROPPEDWEAPON = 1 << 25;
// What the game says of an item (its class, its models, its kind), by its place in the list of the items of the
// game (bg_itemlist): that place is what an item of the state names itself by. Filled when the classes are read.
const itemDefs = [];
const itemDef = it => it.name ? classInfo.get(it.name) : itemDefs[it.index];

// An item comes as a row (see WA_StateItems): its entity, its place in the list of the items, the eFlags of its
// entity, where it is, its angles. All of them the clients are sent, also one that is not drawn. A game module
// that sends an item as an object: as it is.
function expandItems(rows) {
	return rows.map(r => Array.isArray(r) ? { id: r[0], index: r[1], flags: r[2], pos: [r[3], r[4], r[5]], angles: [r[6], r[7], r[8]] } : r);
}

// A player comes in two parts (see WA_ApiState): "s" is what stays the same of him, sent with a number "r" and
// then only again when it changed, "f" and "b" are rows of numbers for what changes all the time. Here they
// become the one object the rest of the page reads. A game module that sends a player as one object: as it is.
const playerSlow = new Map();		// client -> { rev, data }

function expandPlayers(s) {
	const out = [], seen = new Set();
	for (const e of s.players || []) {
		if (e.f === undefined) { out.push(e); continue; }
		seen.add(e.id);
		if (e.s) playerSlow.set(e.id, { rev: e.r, data: e.s });
		const slow = playerSlow.get(e.id);
		// what the page has is not what the server thinks it has: asked for again, he is back with the next poll
		if (!slow || slow.rev !== e.r) { playerSlow.delete(e.id); continue; }
		const f = e.f, d = slow.data;
		let n = 0;
		out.push(Object.assign({}, d, {
			id: e.id, health: f[0], armor: f[1], score: f[2], ping: f[3], weapon: f[4], forcePower: f[5],
			dead: !!(f[6] & 1), saberOn: !!(f[6] & 2), saberInFlight: !!(f[6] & 4), saberHolstered: f[7], saberEnt: f[8],
			pos: [f[9], f[10], f[11]], vel: [f[12], f[13], f[14]], yaw: f[15], pitch: f[16], legsAnim: f[17], torsoAnim: f[18], viewheight: f[19], ammo: f[20],
			// What a client draws him by. The frames are the ones the server has his skeleton at, at the time of this
			// state, and the speeds are frames per 50 ms. undefined with a game module that does not send them.
			legsFrame: f[21], legsSpeed: f[22], torsoFrame: f[23], torsoSpeed: f[24], eFlags: f[25], eFlags2: f[26], powerups: f[27], forceActive: f[28],
			weaponstate: f[29], saberMove: f[30], legsFlip: f[31] & 1, torsoFlip: (f[31] & 2) >> 1, ground: f[32], brokenLimbs: f[33], saberLevel: f[34],
			minutes: Math.max(0, Math.floor(((s.time || 0) - (d.entered || 0)) / 60000)),
			sabers: (d.sabers || []).map(sb => ({ model: sb.model, blades: sb.blades.map(bl => {
				const now = e.b[n++] || [0, 0, 0, 0, 0, 0, 0];
				return { length: bl.length, radius: bl.radius, color: bl.color, current: now[0], org: [now[1], now[2], now[3]], dir: [now[4], now[5], now[6]] };
			}) }))
		}));
	}
	for (const id of [...playerSlow.keys()]) if (!seen.has(id)) playerSlow.delete(id);
	return out;
}

async function pollState() {
	try {
		// The list of the items is not sent again as long as it is the one that is here, and neither is what stays
		// the same of a player: the server is told the number of what the page has of each.
		const has = [...playerSlow].map(([id, slow]) => id + '.' + slow.rev).join('-');
		// through the eyes of a player: what his HUD and his own view are drawn from comes with it (state.hud)
		const res = await fetch('api/state?items=' + itemsRev + fxQuery() + (has ? '&p=' + has : '') + (view.id >= 0 ? '&hud=' + view.id : ''), { cache: 'no-store' });
		if (!res.ok) throw new Error('HTTP ' + res.status);
		const s = await res.json();
		$('status').className = 'live';
		if (s.items) { itemsKept = expandItems(s.items); itemsRev = s.itemsRev || 0; }
		s.items = itemsKept;
		s.players = expandPlayers(s);
		applyState(s);
	} catch (e) {
		$('status').className = 'err';
	}
}

// The brush models that are entities (state.movers): doors, lifts, what turns, what can be switched off or
// broken. The game draws such a model where its entity is, and not at all without one. movers has, for the
// number of a brush model, where it is drawn, as often as an entity shows it. null: the server does not say
// (an older game module), then all of them are drawn where the map file has them.
let movers = null;
const ZERO3 = [0, 0, 0], AT_REST = [null], moverNow = new Map();
let moverStamp = -1;

function applyMovers(list) {
	if (!list) { movers = null; return; }
	const next = new Map();
	for (const m of list) {
		// a number alone: where the map has it
		const flat = typeof m === 'number', n = flat ? m : m[0], pos = flat ? ZERO3 : [m[1], m[2], m[3]], angles = flat ? ZERO3 : [m[4], m[5], m[6]];
		const places = next.get(n) || [], old = movers && movers.get(n) && movers.get(n)[places.length];
		// drawn on its way from where it was at the last poll
		// frame: the frame its shaders are at (-1: the one the time gives), model2: the model drawn with it ('' for none)
		places.push({ pos, angles, prevPos: old ? old.pos : pos, prevAngles: old ? old.angles : angles, frame: flat || m[7] === undefined ? -1 : m[7], model2: (!flat && m[8]) || '' });
		next.set(n, places);
	}
	movers = next;
	moverStamp = -1;
}

// Where a brush model is drawn in the frame of the time now: a matrix for each time it is drawn, null for where
// the map file has it. An empty list: no entity shows it.
function moverPlaces(model, now) {
	if (!model || !movers) return AT_REST;
	if (moverStamp !== now) { moverNow.clear(); moverStamp = now; }
	let places = moverNow.get(model);
	if (!places) {
		const k = Math.min(1, (now - lastPoll) / pollInterval);
		places = (movers.get(model) || []).map(m => {
			const pos = m.prevPos.map((v, i) => v + (m.pos[i] - v) * k), angles = m.prevAngles.map((v, i) => lerpAngle(v, m.angles[i], k));
			return pos.some(v => v !== 0) || angles.some(v => v !== 0) ? anglesMatrix(pos, angles, 1) : null;
		});
		moverNow.set(model, places);
	}
	return places;
}

// the eye as a model that stands at place sees it
function eyeAt(place) {
	const at = M4.transform(M4.invert(place), eye);
	return [at[0], at[1], at[2]];
}

function applyState(s) {
	// where the models were at the last poll: they are drawn on their way from there
	const before = new Map(((state && state.models) || []).map(m => [m.id, m.pos]));
	for (const m of s.models || []) m.prev = before.get(m.id) || m.pos;
	applyMovers(s.movers);
	// the effects: the ones that loop and the ones that were played once (state is still the poll before)
	fxTakeState(s);
	// the same for what is in the air. A new shot comes from where it was a poll ago, by its speed.
	const flew = new Map(((state && state.shots) || []).map(m => [m[0], m]));
	for (const m of s.shots || []) {
		const was = flew.get(m[0]), back = pollInterval / 1000;
		m.prev = was ? [was[3], was[4], was[5]] : [m[3] - m[6] * back, m[4] - m[7] * back, m[5] - m[8] * back];
		// it was fired since the last poll: where its muzzle effect belongs
		m.fresh = !was && !!state;
		flew.delete(m[0]);
	}
	// What was in the air and is not any more has hit something, about half a poll further on. Not when the map
	// is another one: then it is simply gone.
	if (state && s.map === state.map) {
		for (const was of flew.values()) {
			const on = pollInterval / 2000;
			if (flashes.length < 64) flashes.push({ pos: [was[3] + was[6] * on, was[4] + was[7] * on, was[5] + was[8] * on], weapon: was[1], alt: was[2], vel: [was[6], was[7], was[8]], t: performance.now() });
		}
	}
	state = s;
	const now = performance.now();
	if (s.map && s.map !== loadedMap) loadMap(s.map);

	const seen = new Set();
	for (const p of s.players) {
		seen.add(p.id);
		const old = players.get(p.id);
		if (old) {
			old.prev = old.draw.slice();
			old.prevYaw = old.data.yaw;
			old.prevPitch = old.data.pitch;
			const inst = g2.instances.get(p.id);
			if (inst) inst.prevSabers = old.data.sabers;
			old.data = p;
			old.t = now;
		} else {
			players.set(p.id, { data: p, prev: p.pos.slice(), draw: p.pos.slice(), t: now });
		}
	}
	for (const id of [...players.keys()]) {
		if (!seen.has(id)) {
			players.delete(id);
			labels.get(id)?.remove();
			labels.delete(id);
		}
	}
	if (selected >= 0 && !players.has(selected)) select(-1);
	lastPoll = now;
	renderTopbar();
	renderTable();
	renderCommand();
}

function fmtTime(ms) {
	const s = Math.floor(ms / 1000);
	return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
}

function renderTopbar() {
	$('host').innerHTML = colorName(state.host || '');
	$('map').textContent = state.map;
	$('gametype').textContent = state.gametypeName || ('Gametype ' + state.gametype);
	$('time').textContent = fmtTime(state.time) + (state.timelimit ? ' / ' + state.timelimit + ':00' : '');
	if (state.gravity !== undefined) $('gravValue').placeholder = state.gravity;
	const team = state.gametype >= 6;
	$('teamScores').style.display = team ? '' : 'none';
	$('scoreRed').textContent = state.scores.red;
	$('scoreBlue').textContent = state.scores.blue;
	$('itemsInfo').textContent = state.items.filter(it => !(it.flags & EF_NODRAW)).length + ' items on the map';
}

function renderTable() {
	const rows = [...players.values()].map(p => p.data).sort((a, b) => b.score - a.score);
	$('playerCount').textContent = rows.length + ' / ' + state.maxClients;
	$('players').innerHTML = rows.map(p =>
		'<tr class="player' + (p.id === selected ? ' sel' : '') + (p.dead ? ' dead' : '') + '" data-id="' + p.id + '">' +
		'<td><span class="team ' + p.team + '"></span>' + colorName(p.name) + (p.bot ? '<span class="tag">bot</span>' : '') + (p.god ? '<span class="tag">god</span>' : '') + (p.noclip ? '<span class="tag">noclip</span>' : '') + (p.schizo ? '<span class="tag">schizo</span>' : '') + (p.flatulence ? '<span class="tag">gas</span>' : '') + (p.disorient ? '<span class="tag">dizzy</span>' : '') + (p.jp && p.jp.admin ? '<span class="tag">' + (JP_RANKS[p.jp.admin] || 'admin') + '</span>' : '') + '</td>' +
		'<td class="num">' + (p.team === 'spectator' ? '-' : Math.max(p.health, 0)) + '</td>' +
		'<td class="num">' + (p.team === 'spectator' ? '-' : p.armor) + '</td>' +
		'<td class="num">' + p.score + '</td>' +
		'<td class="num">' + (p.bot ? '-' : p.ping) + '</td></tr>').join('');
}

$('players').addEventListener('click', e => {
	const tr = e.target.closest('tr.player');
	if (!tr) return;
	select(+tr.dataset.id);
	const p = players.get(selected);
	if (p) cam.target = p.draw.slice();
});
$('players').addEventListener('dblclick', e => {
	const tr = e.target.closest('tr.player');
	if (tr) cam.follow = +tr.dataset.id;
});

