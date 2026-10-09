// ---- the blade of a saber as the client draws it (CG_DoSaber and CG_AddSaberBlade in cg_players.c): the glow,
// the core, the trail and its light
//
// The server says where a blade starts and which way it points (org and dir of the state: the bolt of the hilt as
// WP_SaberPositionUpdate reads it) and which sabers a player has. The rest is the client's own and is worked out
// here as cgame does it: how long a blade is at the moment (the server never sets that), which colour it has (the
// colours the player picked, not the one of the saber file), the trail behind it, and what it is drawn with.

// saber_colors_t (q_shared.h) is red, orange, yellow, green, blue, purple. JA+ has five RGB colours and black above.
const SB_RGB_FIRST = 6, SB_RGB_LAST = 10, SB_BLACK = 11;
const SB_NAMES = ['red', 'orange', 'yellow', 'green', 'blue', 'purple'];
// CG_RGBForSaberColor: the colour of the light of a blade
const SB_LIGHT = [[1, 0.2, 0.2], [1, 0.5, 0.1], [1, 1, 0.2], [0.2, 1, 0.2], [0.2, 0.4, 1], [0.9, 0.2, 1]];
// the colour of the vertices of a trail (CG_AddSaberBlade), 0 to 255
const SB_TRAIL = [[255, 0, 0], [255, 64, 0], [255, 255, 0], [0, 255, 0], [0, 64, 255], [220, 0, 255]];
// The shaders of the blades in the order cgame registers them (CG_RegisterGraphics in cg_main.c): the engine draws
// what has the same sort in that order.
const SB_SHADERS = [...SB_NAMES.flatMap(n => ['gfx/effects/sabers/' + n + '_glow', 'gfx/effects/sabers/' + n + '_line']),
	...['saberBlur', 'swordTrail', 'RGBGlow', 'RGBGlow4', 'RGBLine', 'RGBcore2', 'RGBcore3', 'RGBcore4', 'RGBcore5', 'RGBtrail2', 'RGBtrail3', 'RGBtrail4',
		'black_glow', 'black_line', 'blacksaberBlur'].map(n => 'gfx/effects/sabers/' + n)];
// saberFlags2 of a saber file (bg_public.h)
const SFL2_NO_WALL_MARKS = 1, SFL2_NO_DLIGHT = 2, SFL2_NO_BLADE = 4;
// trailLength of saberMoveData (bg_saber.c) by saber move, as [value, how many moves in a row have it]
const SB_TRAIL_RUNS = [[0, 4], [200, 46], [500, 2], [2000, 3], [200, 21], [150, 81], [300, 5], [200, 3], [1000, 2], [850, 1]];
const SB_MOVE_TRAIL = SB_TRAIL_RUNS.flatMap(([v, n]) => new Array(n).fill(v));		// LS_MOVE_MAX (168) of them
const SABER_TRAIL_TIME = 40, EF_DEAD = 2, PW_SPEED = 10;

// The part of a size the game picks anew in every frame (Q_flrand). Next to a picture of the game (the comparison)
// the middle of it is taken: no two frames of the game are alike there either.
const sbRand = () => refCam ? 0.5 : Math.random();
const sbCrand = () => refCam ? 0 : Math.random() * 2 - 1;

const sbIsRgb = color => color >= SB_RGB_FIRST && color <= SB_RGB_LAST;
// CG_SaberRGBBytes: the RGB colour of a player (r | g << 8 | b << 16), white when he has none
function sbBytes(packed) {
	const c = packed || 0xffffff;
	return [c & 255, c >> 8 & 255, c >> 16 & 255];
}

// CG_RGBForSaberColor, 0 to 1. A number the game does not know leaves what the caller had: white in CG_DoSaber.
function sbLightRgb(color, packed) {
	if (color === SB_BLACK) return [0, 0, 0];
	if (sbIsRgb(color)) return sbBytes(packed).map(v => v / 255);
	return SB_LIGHT[color] || [1, 1, 1];
}

// the glow and the core of a blade by its colour (the switch of CG_DoSaber): blue for a number the game does not know
function sbShaders(color) {
	const dir = 'gfx/effects/sabers/';
	if (color === SB_RGB_FIRST) return [dir + 'RGBGlow', dir + 'RGBLine'];
	if (color === SB_RGB_FIRST + 1 || color === SB_RGB_FIRST + 2) return [dir + 'RGBGlow', dir + 'RGBcore' + (color - SB_RGB_FIRST + 1)];
	if (color === SB_RGB_FIRST + 3) return [dir + 'RGBGlow4', dir + 'RGBcore4'];
	if (color === SB_RGB_LAST) return [dir + 'RGBGlow', dir + 'RGBcore5'];
	if (color === SB_BLACK) return [dir + 'black_glow', dir + 'black_line'];
	const name = SB_NAMES[color] || 'blue';
	return [dir + name + '_glow', dir + name + '_line'];
}

// The colour a client draws the blades of a saber of a player in (CG_AddSaberBlade): the one he picked for his
// first or his second saber (c1 and c2 of his configstring: saberColors of the state), red or blue by his team in
// a team game other than siege. A state without saberColors: the colour the server has for the blade, which is
// the one of the saber file and not always what the clients show.
function sbColor(d, si, blade) {
	if (state && state.gametype >= 6 && state.gametype !== 7) {
		if (d.team === 'red') return 0;
		if (d.team === 'blue') return 4;
	}
	return d.saberColors ? d.saberColors[si ? 1 : 0] | 0 : blade.fileColor;
}

// BG_SuperBreakWinAnim: the 18 animations of the one who wins a saber lock with a super break (BOTH_LK_S_DL_S_SB_1_W
// to BOTH_LK_ST_S_T_SB_1_W in anims.h, every fifth one)
const sbSuperBreakWin = anim => anim >= 744 && anim <= 829 && (anim - 744) % 5 === 0;

// Where a blade is taken from. false: from the server (org and dir of the state), and the hilt is put to it. true:
// from the hand the page draws, as the client does it (the bolt of the hilt on the bolt of the hand), and the
// server is only asked while the page has drawn no hand yet. That is the right one once the skeleton the page
// draws is the one a client has: the skeleton of the server is not quite that (a blade is off by a unit or two).
let sbFromHand = false;

// What the page keeps of the sabers of a player from frame to frame: client -> { shape, sabers, prev, blades, now,
// hand }. blades: for every blade its length, where its trail was last and where it last touched the map. now: for
// every saber where the server has its first blade in this frame, null where it does not say. hand: for every
// saber where its blades are by the hand the page drew last (drawHilt in js/weapons.js).
const sbPlayers = new Map();
// the players whose first saber is in the air, in the order drawModelEntities (js/frame.js) hands their blades in
const sbThrown = [];
const sbStats = { blades: 0, quads: 0, trails: 0 };

// The length of every blade of a player now, as CG_Player sets what it should be (BG_SI_SetDesiredLength) and
// BG_SI_SetLengthGradual lets it grow or shrink towards that, once a frame. A player the page sees for the first
// time has his blades as long as they should be.
function sbLengths(id, d, now) {
	const shape = d.sabers.map(s => s.blades.length).join();
	let st = sbPlayers.get(id);
	const first = !st || st.shape !== shape;
	if (first) sbPlayers.set(id, st = { shape, sabers: d.sabers, prev: null, now: [], hand: [], blades: d.sabers.map(s => s.blades.map(() => ({ length: 0, debounce: 0, base: null, tip: null, last: -1e9, drawn: null, wall: null }))) });
	// the sabers of the poll before: a blade is drawn on its way from there
	if (st.sabers !== d.sabers) { st.prev = st.sabers; st.sabers = d.sabers; }
	const on = d.weapon === 3, dead = d.eFlags !== undefined ? !!(d.eFlags & EF_DEAD) : d.dead, held = on && !dead && d.saberHolstered < 2;
	d.sabers.forEach((saber, si) => saber.blades.forEach((blade, bi) => {
		const b = st.blades[si][bi], max = blade.length;
		// saberHolstered 1: the second saber is off, or all blades of a staff but its first. A broken left arm
		// holds no second saber, and without a saber entity there is no first one.
		let want = held;
		if (want && si === 0) want = !!d.saberEnt && !(saber.blades.length > 1 && d.saberHolstered === 1 && bi > 0);
		else if (want) want = d.saberHolstered !== 1 && !(d.brokenLimbs & 2);
		const to = want ? max : 0;
		if (!on) b.length = 0;
		else if (first) b.length = to;
		else if (b.length !== to) {
			if (b.length === max || b.length === 0) { b.debounce = now; b.length += b.length === 0 ? 1 : -1; }
			const amt = Math.max(0.2, (now - b.debounce) * 0.01);
			b.length = b.length < to ? Math.min(b.length + amt, to, max) : Math.max(b.length - amt, to, 0);
		}
	}));
	return st;
}

// CG_DoSaberLight: one light for a saber with more than two blades, of all their colours. It takes the colours of
// the saber file, and where the blades are from the client's own copy of the saber, which cgame never fills in a
// multiplayer game: so the light is where the map has its origin, as in the game.
function sbSaberLight(saber, lens) {
	if ((saber.flags2 | 0) & SFL2_NO_DLIGHT) return;
	const lit = saber.blades.map((blade, i) => [lens[i].length, sbLightRgb(blade.fileColor, blade.packed)]).filter(e => e[0] >= 0.5);
	if (!lit.length) return;
	// one blade gives its colour as it is, several are weighed by their lengths
	const total = lit.reduce((sum, e) => sum + e[0], 0), diameter = Math.max(...lit.map(e => e[0] * 2));
	dlightAdd([0, 0, 0], diameter + sbRand() * 8, lit.length === 1 ? lit[0][1] : [0, 1, 2].map(k => lit.reduce((sum, e) => sum + e[0] * e[1][k], 0) / total));
}

// The blades of this frame, before anything is drawn (gatherDlights calls it): each blade a player holds goes into
// "blades" with what CG_AddSaberBlade hands CG_DoSaber, its light goes to the lights of the frame, and the trail
// behind it to the effects.
function sabersPrepare(now) {
	const real = performance.now(), k = Math.min(1, (real - lastPoll) / pollInterval), seen = new Set();
	sbThrown.length = 0;
	const flying = new Map();
	for (const [id, p] of players) {
		const d = p.data;
		if (d.team === 'spectator' || !d.sabers || !d.sabers.length) continue;
		seen.add(id);
		// the colours first: a saber in the air is handed in by drawModelEntities with the colour of its blade
		d.sabers.forEach((saber, si) => saber.blades.forEach(blade => {
			if (blade.fileColor === undefined) blade.fileColor = blade.color;
			blade.color = sbColor(d, si, blade);
			blade.packed = d.saberRgb ? d.saberRgb[si ? 1 : 0] | 0 : 0;
		}));
		const st = sbLengths(id, d, now);
		st.now.length = 0;
		if (d.weapon !== 3) continue;
		if (d.saberInFlight && d.saberEnt > 0 && !d.dead) flying.set(d.saberEnt, [d, st]);
		// The server reads the bolts of the blades while the saber is the weapon that is up and its holder lives
		// (WP_SaberPositionUpdate): not while a weapon is raised or lowered (weaponstate 1 and 2), not for a second
		// saber that is off or in a broken arm, not for the blades of a staff that are off.
		const dead = d.dead || !!(d.eFlags & EF_DEAD), told = !dead && d.weaponstate !== 1 && d.weaponstate !== 2;
		// the first saber is not in the hand while it is in the air or gone
		for (let si = !d.saberEnt || d.saberInFlight ? 1 : 0; si < d.sabers.length; si++) {
			const saber = d.sabers[si], flags2 = saber.flags2 | 0, was = st.prev && st.prev[si];
			const live = told && (si === 0 || (d.saberHolstered !== 1 && !(d.brokenLimbs & 2)));
			saber.blades.forEach((blade, bi) => {
				const b = st.blades[si][bi], pb = was && was.blades[bi], held = st.hand[si] && st.hand[si][bi];
				let len = b.length, org = blade.org, dir = blade.dir;
				b.drawn = null;
				if (!(sbFromHand && held) && live && !(bi > 0 && d.sabers.length < 2 && d.saberHolstered === 1) && dir && (dir[0] || dir[1] || dir[2])) {
					if (pb && pb.dir && (pb.dir[0] || pb.dir[1] || pb.dir[2])) {
						org = org.map((v, i) => pb.org[i] + (v - pb.org[i]) * k);
						dir = dir.map((v, i) => pb.dir[i] + (v - pb.dir[i]) * k);
					}
					dir = fxUnit(dir);
					if (bi === 0) st.now[si] = { org, dir };
				} else {
					// where the hand the page drew last has it
					if (!held) return;
					org = held.org; dir = held.dir;
				}
				if (len <= 0) return;
				// the blade ends where it runs into the map, and sparks fly there (mSparks of cgame)
				let end = [0, 1, 2].map(i => org[i] + dir[i] * (len + 1));
				const tr = traceWorld(org, end);
				if (tr && tr.fraction < 1) {
					const n = tr.normal && (tr.normal[0] || tr.normal[1] || tr.normal[2]) ? tr.normal : [0, 1, 0];
					if (!(flags2 & SFL2_NO_WALL_MARKS)) fxPlay('sparks/spark_nosnd', tr.pos, n);
					len = Math.hypot(org[0] - tr.pos[0], org[1] - tr.pos[1], org[2] - tr.pos[2]);
					end = tr.pos.slice();
					// CG_CreateSaberMarks: a burn on the map from where the blade touched it a frame ago to where it
					// does now. That is no mark of CG_ImpactMark (a quad between two points, burn and glow): it waits
					// for a function of js/marks.js that makes one.
					if (b.wall && !(flags2 & SFL2_NO_WALL_MARKS) && typeof saberMarks === 'function') saberMarks(b.wall, tr.pos, n);
					b.wall = tr.pos.slice();
				} else b.wall = null;
				// the second style of a saber file begins at one of its blades (WP_SaberBladeUseSecondBladeStyle)
				const second = saber.bladeStyle2Start > 0 && bi >= saber.bladeStyle2Start;
				sbTrail(d, si, b, org, dir, end, blade, (second ? saber.trailStyle2 : saber.trailStyle) | 0, real);
				if (flags2 & SFL2_NO_BLADE) {
					if (saber.blades.length < 3 && !(flags2 & SFL2_NO_DLIGHT)) sbSaberLight(saber, st.blades[si]);
					return;
				}
				// CG_DoSaber: a blade shorter than half a unit is not drawn and gives no light
				if (len < 0.5) return;
				if (saber.blades.length < 3 && !(flags2 & SFL2_NO_DLIGHT)) dlightAdd([0, 1, 2].map(i => org[i] + dir[i] * len * 0.5), len * 1.4 + sbRand() * 3, sbLightRgb(blade.color, blade.packed));
				blades.push(b.drawn = { org, dir, len, lenMax: blade.length, radius: blade.radius, color: blade.color, packed: blade.packed, mine: true });
			});
			if (saber.blades.length > 2) sbSaberLight(saber, st.blades[si]);
		}
	}
	for (const id of [...sbPlayers.keys()]) if (!seen.has(id)) sbPlayers.delete(id);

	// The sabers in the air: drawModelEntities draws them and hands in their blades, in the order of the models of
	// the state. The light of such a blade is found here, where that function will put the blade.
	for (const m of (state && state.models) || []) {
		if (!flying.has(m.id)) continue;
		const [d, st] = flying.get(m.id);
		sbThrown.push(d);
		const saber = d.sabers[0], blade = saber.blades[0];
		if (saber.blades.length > 2) sbSaberLight(saber, st.blades[0]);
		if (!blade || saber.blades.length > 2 || (saber.flags2 | 0) & SFL2_NO_DLIGHT) continue;
		const model = staticNow(m.model), tag = model && model.tags['*blade1'], from = tag ? tag[11] : 6;
		const yaw = (m.angles[1] + SABER_SPIN * Math.min(pollInterval * 2, real - lastPoll) / 1000) * Math.PI / 180, mid = from + blade.length * 0.5;
		const pos = m.prev.map((v, i) => v + (m.pos[i] - v) * k);
		dlightAdd([pos[0] + Math.cos(yaw) * mid, pos[1] + Math.sin(yaw) * mid, pos[2]], blade.length * 1.4 + sbRand() * 3, sbLightRgb(blade.color, blade.packed));
	}
}

// The trail of a blade (the part of CG_AddSaberBlade after CheckTrail): while the move of the saber has a trail, a
// quad from where the blade was a frame ago to where it is now is handed to the effects in every frame, where it
// lives for a fifth of the trail length of the move. style: trailStyle of the saber file (1 the faint trail of a
// sword, above 1 none).
function sbTrail(d, si, b, org, dir, end, blade, style, now) {
	const tip = [0, 1, 2].map(i => end[i] + dir[i] * 3);
	if (style <= 1 && now > b.last + 2) {
		const duration = d.saberMove >= 0 && d.saberMove < SB_MOVE_TRAIL.length ? SB_MOVE_TRAIL[d.saberMove] : 150, win = sbSuperBreakWin(d.torsoAnim);
		let life = Math.trunc(duration / 5) || (win ? 150 : SABER_TRAIL_TIME);
		// how far the quad before this one had come along the image when this one begins
		const st = (now - b.last) / life;
		if ((win || duration > 0 || (d.powerups & 1 << PW_SPEED) || (d.saberInFlight && si === 0)) && now < b.last + 2000 && b.base) {
			const color = blade.color, dirName = 'gfx/effects/sabers/';
			let rgb = SB_TRAIL[color] || (color === SB_BLACK ? [1, 1, 1] : sbIsRgb(color) ? sbBytes(blade.packed) : SB_TRAIL[4]);
			let shader = dirName + (color === SB_BLACK ? 'blacksaberBlur' : color > SB_RGB_FIRST && color < SB_RGB_LAST ? 'RGBtrail' + (color - SB_RGB_FIRST + 1) : color === SB_RGB_LAST ? 'swordTrail' : 'saberBlur');
			if (style === 1) { shader = dirName + 'swordTrail'; rgb = [32, 32, 32]; life *= 2; }
			fxAddTrail(shader, [org, tip, b.tip, b.base], rgb, [[0, 1, 1, 1], [0, 0, 1, 0], [st, 0, 1 + st, 0], [st, 1, 1 + st, 1]], life);
			sbStats.trails++;
		}
		b.base = org.slice(); b.tip = tip; b.last = now;
	}
}

const sbStamp = (o, r, view) => fxStamp(o, view.left[0] * r, view.left[1] * r, view.left[2] * r, view.up[0] * r, view.up[1] * r, view.up[2] * r);

// CG_DoSaber and what the engine makes of its two entities. The glow (RT_SABER_GLOW, RB_SurfaceSaberGlow): sprites
// from the tip down, each 0.017 larger than the one before and 0.65 of its radius further down, and a large one at
// the hilt. The core (RT_LINE, RB_SurfaceLine): a strip from the tip to one unit behind the hilt that shows its
// flat side to the eye. Each into the list of its shader: { fog, write }.
function sbBlade(b, jobs, view, fogged) {
	const length = b.len, lengthMax = b.lenMax === undefined ? b.len : b.lenMax, color = b.color, org = b.org, dir = b.dir;
	if (!(length >= 0.5)) return;
	const [glow, core] = sbShaders(color), f = Math.fround;
	// a blade that is not out all the way is wider
	const mult = length < lengthMax ? 1 + 2 / length : 1, range = b.radius * 0.075;
	const glowRadius = (b.radius - range + sbCrand() * range) * mult, coreRadius = (b.radius / 3 + sbCrand() * range) * mult;
	const bytes = sbIsRgb(color) ? sbBytes(b.packed) : [255, 255, 255], hilt = 5.5 + sbRand() * 0.25;
	const tip = [org[0] + dir[0] * length, org[1] + dir[1] * length, org[2] + dir[2] * length], base = [org[0] - dir[0], org[1] - dir[1], org[2] - dir[2]];
	const add = (name, fog, write) => { let list = jobs.get(name); if (!list) jobs.set(name, list = []); list.push({ fog, write }); };
	// the fog of an entity is the first one its ball reaches into (R_SpriteFogNum)
	add(glow, fogged ? fogAt(org, glowRadius) : null, () => {
		fxColor[0] = bytes[0]; fxColor[1] = bytes[1]; fxColor[2] = bytes[2]; fxColor[3] = 255;
		let r = f(glowRadius);
		for (let i = f(length); i > 0; i = f(i - f(r * 0.65))) {
			sbStamp([org[0] + dir[0] * i, org[1] + dir[1] * i, org[2] + dir[2] * i], r, view);
			r = f(r + 0.017);
		}
		sbStamp(org, hilt, view);
	});
	add(core, fogged ? fogAt(tip, coreRadius) : null, () => {
		fxColor[0] = fxColor[1] = fxColor[2] = fxColor[3] = 255;
		fxStrip(tip, base, fxAcross(tip, base), coreRadius, coreRadius);
	});
}

// The saber blades of the frame drawn with the shaders of the game: their quads go into the buffer of the effects,
// shader by shader in the order of cgame, and are drawn as the effects are.
function sbDraw(list) {
	const view = cameraAxes(), jobs = new Map(), fogged = fogShown() && world && world.fogInfo.length > 0, batches = [];
	let thrown = 0;
	for (const b of list) {
		// a blade of a saber in the air comes from drawModelEntities without the RGB colour of its player
		if (!b.mine && b.packed === undefined) { const d = sbThrown[thrown++], blade = d && d.sabers[0] && d.sabers[0].blades[0]; b.packed = blade ? blade.packed : 0; }
		sbBlade(b, jobs, view, fogged);
	}
	fxVerts = 0;
	const t = frameNow / 1000;
	for (const name of [...SB_SHADERS, ...[...jobs.keys()].filter(n => !SB_SHADERS.includes(n))]) {
		const todo = jobs.get(name), mat = todo && materialFor(name, 'fx');
		if (!todo || mat.nodraw || !mat.stages.length) continue;
		// one batch for every fog the blades of this shader are in
		for (const fog of new Set(todo.map(j => j.fog))) {
			const from = fxVerts;
			for (const j of todo) if (j.fog === fog && fxVerts + 80 * 4 <= FX_MAX_QUADS * 4) j.write();
			batches.push({ mat, t, first: from / 4 * 6, count: (fxVerts - from) / 4 * 6, fog });
		}
	}
	sbStats.blades = list.length;
	sbStats.quads = fxVerts / 4;
	fxDrawBatches(batches);
}
