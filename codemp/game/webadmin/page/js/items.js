// ---- the items of a map as the client draws them (CG_Item in cg_ents.c)
//
// An item of the state (expandItems in js/state.js) is its entity, its place in the list of the items of the game
// (itemDef gives what the game says of it), the flags of its entity, its place and its angles. What is drawn of
// it is what CG_Item draws: a weapon or a powerup floats and turns over a base with a cone of light, the others
// stand where they are as their angles say, and one that was taken is a ghost until it is back and fades in then.

// What CG_Item reads of bg_public.h: the kinds of items (giType), the tags it asks for by name (giTag), a flag of
// the entity, the sides of the Force and the gametype with the other flag models.
const ITEM_GAME = {
	IT_WEAPON: 1, IT_ARMOR: 3, IT_HEALTH: 4, IT_POWERUP: 5, IT_HOLDABLE: 6, IT_TEAM: 8,
	PW_REDFLAG: 4, PW_BLUEFLAG: 5, PW_FORCE_ENLIGHTENED_LIGHT: 12, PW_FORCE_ENLIGHTENED_DARK: 13, PW_FORCE_BOON: 14,
	HI_SEEKER: 1, HI_SHIELD: 2, HI_BINOCULARS: 5, WP_THERMAL: 12, WP_TRIP_MINE: 13, WP_DET_PACK: 14,
	EF_DEAD: 1 << 1, FORCE_LIGHTSIDE: 1, FORCE_DARKSIDE: 2, GT_CTY: 9
};
// how far below its origin a weapon that was dropped is drawn, by its weapon (the switch of CG_Item), 8 for the others
const ITEM_DROPPED_DOWN = { 5: 12, 6: 13, 7: 16, 8: 12, 9: 10, 10: 6, 11: 11, 12: 12, 13: 16, 14: 16 };
const ITEM_SCALEUP_TIME = 1000;		// cg_local.h: how long one that is back fades in
// cgs.media.itemHoloModel, cgs.effects.itemCone and the shaders of cg_main.c
const ITEM_HOLO = 'models/map_objects/mp/holo.md3', ITEM_CONE = 'mp/itemcone';
const ITEM_PLACEHOLDER = 'powerups/placeholder', ITEM_REZOUT = 'powerups/rezout';
const ITEM_UP = [0, 0, 1];
// The game plays the cone anew in every frame and each lives 50 msec, so how bright it is says how many frames a
// client draws in that time. With com_maxfps at its default of 125 a frame is 8 msec. In the comparison the game
// runs at timescale 0.05, where a frame is one msec of the game (Com_ModifyMsec): 51 cones lie over one another.
const ITEM_FRAME_MSEC = 8, ITEM_REF_CONES = 51;

// CG_GreyItem: the enlightenment of the other side is grey for one who has chosen his
function itemGrey(type, tag, side) {
	const G = ITEM_GAME;
	if (type !== G.IT_POWERUP) return false;
	return (side === G.FORCE_LIGHTSIDE && tag === G.PW_FORCE_ENLIGHTENED_DARK) || (side === G.FORCE_DARKSIDE && tag === G.PW_FORCE_ENLIGHTENED_LIGHT);
}

// cg.snap->ps.fd.forceSide: the side of the one whose eyes the page looks through (the feed has it for him). The
// free camera has no player state. It is taken for the light side, which is what a client has who never chose:
// the spectator of the comparison sees the dark enlightenment grey.
function itemViewerSide() {
	const hud = state && state.hud;
	return hud && hud.client === view.id && hud.force ? hud.force[2] : ITEM_GAME.FORCE_LIGHTSIDE;
}

// CG_RegisterItemVisuals: models[0] of an item, whether the renderer draws it as a Ghoul2 model, and models[1].
// Thermals, trip mines and det packs lie there as their second model, and so do the flags of Capture the Ysalamiri.
// A .glm is drawn through the Ghoul2 instance of the item, which is always the one of its first model.
function itemModels(def) {
	const G = ITEM_GAME, glm = m => /\.glm$/i.test(m || '');
	const other = (def.type === G.IT_TEAM && (def.tag === G.PW_REDFLAG || def.tag === G.PW_BLUEFLAG) && state.gametype === G.GT_CTY)
		|| (def.type === G.IT_WEAPON && (def.tag === G.WP_THERMAL || def.tag === G.WP_TRIP_MINE || def.tag === G.WP_DET_PACK));
	const first = (other ? def.model2 : def.model) || '', g2 = glm(first);
	const second = (def.type === G.IT_POWERUP || def.type === G.IT_HEALTH || def.type === G.IT_ARMOR || def.type === G.IT_HOLDABLE) && def.model2 ? def.model2 : '';
	return { first: g2 ? (glm(def.model) ? def.model : '') : first, g2, second };
}

// The time of the game the items are drawn at (cg.time). In the comparison it is the time of the level the view
// was taken at. Else it runs with the clock of the page, set by the polls as a client sets its time by its
// snapshots (CL_AdjustTimeDelta): it stays just behind the newest one, and jumps only when it is far off.
const itemClock = { delta: 0, poll: -1, ahead: false, set: false };
function itemTime(now) {
	if (refCam && refCam.time) return now;
	// the time of the level where the feed has it (with the HUD of a player), else the time since the map began
	const server = state.now !== undefined ? state.now : state.hud && state.hud.now !== undefined ? state.hud.now : state.time, c = itemClock;
	if (c.poll !== lastPoll) {
		const fresh = server - lastPoll, off = Math.abs(fresh - c.delta);
		if (!c.set || off > 500) c.delta = fresh;
		else if (off > 100) c.delta = (c.delta + fresh) / 2;
		else c.delta += c.ahead ? -2 : 1;
		c.poll = lastPoll; c.ahead = false; c.set = true;
	}
	if (now + c.delta >= server - 5) c.ahead = true;
	return Math.floor(now + c.delta);
}

// When an item came back (cent->miscTime of EV_ITEM_RESPAWN), by its entity. The page sees that as RespawnItem
// leaves it: an item that was in the list before and was gone is in it again, or it has lost the flag of a ghost
// or the one that hides it.
const itemTrack = { list: null, map: '', known: new Map() };
function itemsTrack(t) {
	const k = itemTrack;
	if (k.list === state.items && k.map === state.map) return;
	if (k.map !== state.map) k.known.clear();
	const here = new Set();
	for (const it of state.items) {
		const was = k.known.get(it.id), same = was && was.index === it.index, shown = !(it.flags & (EF_NODRAW | EF_ITEMPLACEHOLDER));
		here.add(it.id);
		k.known.set(it.id, { index: it.index, here: true, shown, back: same && shown && (!was.here || !was.shown) ? t : same ? was.back : -Infinity });
	}
	for (const [id, was] of k.known) if (!here.has(id)) was.here = false;
	k.list = state.items; k.map = state.map;
}

// The root matrix of a Ghoul2 model (identityMatrix in tr_ghoul2.cpp): it is turned a quarter to the left under
// the axes of its entity, its -Y is the way the entity looks.
const itemG2Place = m => new Float32Array([m[4], m[5], m[6], 0, -m[0], -m[1], -m[2], 0, m[8], m[9], m[10], 0, m[12], m[13], m[14], 1]);

// One model of an item at its place (a refEntity_t of CG_Item). ent is what drawModelScripted takes, and
// minLight for RF_MINLIGHT. The plain look of the editor draws the model as it is. False until the model is read.
function itemModel(path, g2, place, ent) {
	const model = path ? staticNow(path) : null;
	if (!model) return false;
	if (g2) place = itemG2Place(place);
	if (!modelsScripted()) drawStatic(model, place);
	else drawModelScripted(model.meshes, place, ent);
	return true;
}

// How many cones of light every base gets in this frame (see ITEM_FRAME_MSEC): as many as a client would have
// played since the last frame of the page. -1 in the comparison, where the time stands: itemCone counts there.
// None until the effect and its images are read: what is asked for before that waits in the queue of the effects,
// which is the one of the shots too.
const itemCones = { last: 0, owed: 0, time: -1, done: new Map(), room: 0 };
function itemConePlays(t) {
	const c = itemCones, ready = fxOn() && fxReady(fxGet(ITEM_CONE));
	if (refCam && refCam.time) {
		if (c.time !== t) { c.time = t; c.done.clear(); }
		// the effects take a few hundred at a time
		c.room = ready ? 200 : 0;
		return -1;
	}
	c.owed += Math.min(50, Math.max(0, t - c.last)) / ITEM_FRAME_MSEC;
	c.last = t;
	const n = Math.floor(c.owed);
	c.owed -= n;
	return ready ? n : 0;
}

// the cone of light over a base: the effect of the game, upwards from where the item is
function itemCone(it, plays) {
	const c = itemCones;
	if (plays < 0) {
		// the comparison: the cones the game has alive at a time that stands, once for that time
		const had = c.done.get(it.id) || 0;
		plays = Math.min(ITEM_REF_CONES - had, c.room);
		c.room -= plays;
		c.done.set(it.id, had + plays);
	}
	for (let i = 0; i < plays; i++) fxPlay(ITEM_CONE, it.pos, ITEM_UP);
}

// Where CG_Item draws the first model of an item at the time t (cg.time), and how it is turned: org and angles.
function itemPose(it, def, flags, t) {
	const G = ITEM_GAME, type = def.type, tag = def.tag, dropped = !!(flags & EF_DROPPEDWEAPON), floats = type === G.IT_WEAPON || type === G.IT_POWERUP;
	const org = it.pos.slice();
	let angles = it.angles;
	if (floats && !dropped) org[2] += 16;
	if (floats && (!dropped || type === G.IT_POWERUP)) {
		// it bobs up and down, every entity a little faster than the one before it, and turns once in 2048 msec
		// (cg.autoAngles, CG_AddPacketEntities). The engine works the phase out in floats.
		const scale = Math.fround(0.005 + it.id * 0.00001);
		org[2] += 4 + Math.cos(Math.fround((t + 1000) * scale)) * 4;
		angles = [0, (t & 2047) * 360 / 2048, 0];
	} else if (type === G.IT_HOLDABLE) {
		if (tag === G.HI_SEEKER) org[2] += 5;
		if (tag === G.HI_SHIELD || tag === G.HI_BINOCULARS) org[2] += 2;
	} else if (type === G.IT_HEALTH) {
		org[2] += 2;
	} else if (type === G.IT_ARMOR && def.quantity === 100) {
		org[2] += 7;
	}
	// A weapon is moved by the middle of its model, so that it turns around itself. That middle is the one of the
	// bounds of an MD3 (R_ModelBounds), and every weapon of the game has a Ghoul2 model there: it is the origin.
	if (!dropped) { if (type === G.IT_WEAPON) org[2] += 8; }
	else org[2] -= ITEM_DROPPED_DOWN[tag] || 8;
	return { org, angles };
}

// CG_Item. t is cg.time, side the side of the Force of the viewer, cones how many cones a base gets now.
function drawItem(it, t, side, cones) {
	const G = ITEM_GAME, def = itemDef(it), game = modelsScripted();
	let flags = it.flags;
	if ((flags & EF_NODRAW) && (flags & EF_ITEMPLACEHOLDER)) flags &= ~EF_NODRAW;
	if (!it.index) return;
	if (!def) {
		// the list of the items of the game is not read yet: where it is, in the plain look
		if (!game && !(flags & EF_NODRAW)) itemDiamond(it, null, t);
		return;
	}
	const type = def.type, tag = def.tag, dropped = !!(flags & EF_DROPPEDWEAPON), floats = type === G.IT_WEAPON || type === G.IT_POWERUP;
	const grey = itemGrey(type, tag, side);

	// the base of a weapon or a powerup with its cone of light, also while the item itself is not drawn
	if (floats && !dropped) {
		itemModel(ITEM_HOLO, false, anglesMatrix(it.pos, it.angles, 1), grey ? { rgba: [150 / 255, 150 / 255, 150 / 255, 0], tint: true } : undefined);
		// the base is in the view: a cone lives a few frames and is not missed when it comes into it
		const P = viewSides, x = it.pos[0], y = it.pos[1], z = it.pos[2], r = 64;
		if (!grey && cones && !(P[0] * x + P[1] * y + P[2] * z + P[3] < -r || P[4] * x + P[5] * y + P[6] * z + P[7] < -r || P[8] * x + P[9] * y + P[10] * z + P[11] < -r || P[12] * x + P[13] * y + P[14] * z + P[15] < -r)) itemCone(it, cones);
	}
	if (flags & EF_NODRAW) return;

	const pose = itemPose(it, def, flags, t), org = pose.org, angles = pose.angles;

	const models = itemModels(def), ent = models.g2 ? { radius: 60 } : {};
	if (grey) {
		Object.assign(ent, { rgba: [150 / 255, 150 / 255, 150 / 255, 200 / 255], tint: true, forceAlpha: true,
			shader: tag === G.PW_FORCE_ENLIGHTENED_LIGHT ? 'gfx/misc/mp_light_enlight_disable' : 'gfx/misc/mp_dark_enlight_disable' });
		if (!itemModel(models.first, models.g2, anglesMatrix(org, angles, 1), ent) && !game) itemDiamond(it, def, t);
		return;
	}
	const ghost = !!(flags & EF_ITEMPLACEHOLDER);
	if (ghost) {
		// one that was taken: not at all if it had been dropped, else its shape in green until it is back
		if (flags & G.EF_DEAD) return;
		Object.assign(ent, { rgba: [0, 200 / 255, 85 / 255, 0], tint: true, shader: ITEM_PLACEHOLDER });
	}
	// weapons are larger when they are shown as items
	let place = anglesMatrix(org, angles, type === G.IT_WEAPON ? 1.5 : 1);
	if (!dropped && floats) ent.minLight = true;

	const known = itemTrack.known.get(it.id), msec = t - (known ? known.back : -Infinity);
	let drawn;
	if (type !== G.IT_TEAM && msec >= 0 && msec < ITEM_SCALEUP_TIME && !ghost && !dropped) {
		// just back: it fades in, and the green of the ghost is drawn over it once more
		const a = Math.max(1, Math.trunc(msec / ITEM_SCALEUP_TIME * 255)) / 255;
		ent.rgba = [0, 0, 0, a];
		ent.forceAlpha = type !== G.IT_POWERUP || tag !== G.PW_FORCE_BOON;
		drawn = itemModel(models.first, models.g2, place, ent);
		Object.assign(ent, { forceAlpha: false, shader: ITEM_REZOUT, tint: true, rgba: [0, 200 / 255, 85 / 255, a] });
		itemModel(models.first, models.g2, place, ent);
	} else {
		// the flags stand smaller
		if (type === G.IT_TEAM && (tag === G.PW_REDFLAG || tag === G.PW_BLUEFLAG)) place = anglesMatrix(org, angles, 0.7);
		drawn = itemModel(models.first, models.g2, place, ent);
	}
	if (!drawn && !game) itemDiamond(it, def, t);

	// the ring or the ball that goes with a powerup or a health item: the second model, with what the first was drawn by
	if ((type === G.IT_HEALTH || type === G.IT_POWERUP) && models.second) {
		const spin = [0, 0, 0];
		if (type === G.IT_POWERUP) { org[2] += 12; spin[1] = (t & 1023) * 360 / -1024; }
		itemModel(models.second, false, anglesMatrix(org, spin, 1), ent);
	}
}

// the plain look, an item whose model is not there: a diamond in the colour of its kind
function itemDiamond(it, def, t) {
	const col = ITEM_COLORS[def ? def.type : it.type] || [1, 1, 1], spin = t / 600;
	gl.useProgram(flatProg.p);
	gl.uniformMatrix4fv(flatProg.u.uMvp, false, viewProj);
	drawMesh(diamondMesh, it.pos[0], it.pos[1], it.pos[2] + 6 + Math.sin(spin + it.id) * 3, spin, 1, [col[0], col[1], col[2], 1]);
}

// Whether a client at the eye of the page is sent this item (snapshotSees in js/trace.js). The box of an item
// that was placed is 16 wide and 16 high (FinishSpawningItem), the one of a dropped one ITEM_RADIUS to every side.
function itemSent(from, it) {
	const p = it.pos, dropped = it.flags & EF_DROPPEDWEAPON, r = dropped ? 16 : 9;
	return snapshotSees(from, [p[0] - r, p[1] - r, p[2] - (dropped ? 16 : 1)], [p[0] + r, p[1] + r, p[2] + (dropped ? 16 : 17)]);
}

// The items of the map. From inside the map only the ones a client there is sent: around the map from above the
// page shows what no client sees at once.
function drawItems(now) {
	if (state) {
		const t = itemTime(now), side = itemViewerSide(), cones = itemConePlays(t), from = camInMap() ? snapshotFrom(eye) : null;
		itemsTrack(t);
		setViewSides(viewProj);
		for (const it of state.items) if (itemSent(from, it)) drawItem(it, t, side, cones);
		gl.useProgram(flatProg.p);
		gl.uniformMatrix4fv(flatProg.u.uMvp, false, viewProj);
	}
}
