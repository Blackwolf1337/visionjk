// ---- the events of the game (CG_EntityEvent in cg_event.c): what a client is told once and shows for a moment
//
// That somebody fired, that two blades met, where the beam of a disruptor went. The server keeps the events of
// every frame for the pages (WA_EventsFrame in wa_state.c) and a poll brings the ones the page does not have:
// each its number, how long ago in msec, the entity, the event (the names come once, /api/list?kind=events), its
// parameter, where, origin2, the angles, and of the entity that carried it otherEntityNum, otherEntityNum2,
// weapon, eFlags, time, time2, clientNum and owner.
// gameEvent is the switch of CG_EntityEvent: every case that shows something is a function of evHandlers below.
// Events that only play a sound are not there (see the list at the end of evHandlers).

let evSeq = -1;			// the number of the last event the page has, -1 until the server said where it is
const EV = {};			// the number of an event by its name (EV.EV_FIRE_WEAPON), there once the list is read
let evNames = null, evNamed = false;
let evWaiting = [];		// what came before the names were read: [the row of the feed, when it came]
const evHeard = new Map();	// what was taken, and the time of the map it happened at: two polls that were on their way together both have it
let evMap = '';
// for the checks: the bands that were drawn in the last frame, and the flare as it was drawn last
const evStats = { beziers: 0, quads: 0, flare: null };
let evWarm = '';		// the map the media of the events were asked for

const EVENT_VALID_MSEC = 300;		// bg_public.h: how long the server keeps an event for the clients
const MUZZLE_FLASH_TIME = 20;		// cg_local.h
const EV_LATE_MSEC = 50;			// what a frame of the page still plays of the frames of the game it was too slow for
const EV_EF_FIRING = 1 << 9, EV_EF_ALT_FIRING = 1 << 10;	// eFlags (bg_public.h)
const EV_MAX_CLIENTS = 32;

// what a poll adds to its question, and what is done with its answer
const evQuery = () => evSeq >= 0 ? '&evs=' + evSeq : '';
// the game module tells the events: nothing of them is guessed then (js/fx-weapons.js)
const evOn = () => evSeq >= 0;

function evRow(e, name) {
	return { seq: e[0], ago: e[1], ent: e[2], event: e[3], name, parm: e[4], org: [e[5], e[6], e[7]], org2: [e[8], e[9], e[10]], angles: [e[11], e[12], e[13]],
		other: e[14], other2: e[15], weapon: e[16], eFlags: e[17], time: e[18], time2: e[19], client: e[20], owner: e[21] };
}

function takeEvents(s) {
	if (s.evSeq === undefined) return;
	if (!evNames) {
		evNames = [];
		fetch('api/list?kind=events').then(r => r.json()).then(j => {
			evNames = j.names || [];
			evNames.forEach((n, i) => { if (n) EV[n] = i; });
			evNamed = true;
			// what came in the meantime, as long as its moment is not over
			const now = performance.now(), waiting = evWaiting;
			evWaiting = [];
			for (const [e, at] of waiting) if (now - at < EVENT_VALID_MSEC) gameEvent(evRow(e, evNames[e[3]] || ''));
		}).catch(() => { evNames = null; evWaiting = []; });
	}
	if (s.map !== evMap) { evMap = s.map; evClear(); }
	const now = performance.now();
	for (const e of s.ev || []) {
		// One is known by its number and the time of the map it happened at, as an effect that was played once
		// is (fxTakeState): a module that was loaded anew counts from 1 again.
		const key = e[0] + ' ' + (s.time - e[1]);
		if (evHeard.has(key)) continue;
		evHeard.set(key, s.time - e[1]);
		if (!evNamed) { if (evWaiting.length < 64) evWaiting.push([e, now]); continue; }
		gameEvent(evRow(e, evNames[e[3]] || ''));
	}
	heardForget(evHeard, s.time, 5000);
	evSeq = s.evSeq;
}

// ByteToDir (q_math.c): the direction an event has in its parameter, one of 162
const EV_BYTEDIRS = [
	-0.525731, 0, 0.850651, -0.442863, 0.238856, 0.864188, -0.295242, 0, 0.955423, -0.309017, 0.5, 0.809017, -0.16246, 0.262866, 0.951056, 0, 0, 1, 0, 0.850651, 0.525731, -0.147621, 0.716567, 0.681718, 0.147621, 0.716567, 0.681718,
	0, 0.525731, 0.850651, 0.309017, 0.5, 0.809017, 0.525731, 0, 0.850651, 0.295242, 0, 0.955423, 0.442863, 0.238856, 0.864188, 0.16246, 0.262866, 0.951056, -0.681718, 0.147621, 0.716567, -0.809017, 0.309017, 0.5, -0.587785, 0.425325, 0.688191,
	-0.850651, 0.525731, 0, -0.864188, 0.442863, 0.238856, -0.716567, 0.681718, 0.147621, -0.688191, 0.587785, 0.425325, -0.5, 0.809017, 0.309017, -0.238856, 0.864188, 0.442863, -0.425325, 0.688191, 0.587785, -0.716567, 0.681718, -0.147621, -0.5, 0.809017, -0.309017,
	-0.525731, 0.850651, 0, 0, 0.850651, -0.525731, -0.238856, 0.864188, -0.442863, 0, 0.955423, -0.295242, -0.262866, 0.951056, -0.16246, 0, 1, 0, 0, 0.955423, 0.295242, -0.262866, 0.951056, 0.16246, 0.238856, 0.864188, 0.442863,
	0.262866, 0.951056, 0.16246, 0.5, 0.809017, 0.309017, 0.238856, 0.864188, -0.442863, 0.262866, 0.951056, -0.16246, 0.5, 0.809017, -0.309017, 0.850651, 0.525731, 0, 0.716567, 0.681718, 0.147621, 0.716567, 0.681718, -0.147621, 0.525731, 0.850651, 0,
	0.425325, 0.688191, 0.587785, 0.864188, 0.442863, 0.238856, 0.688191, 0.587785, 0.425325, 0.809017, 0.309017, 0.5, 0.681718, 0.147621, 0.716567, 0.587785, 0.425325, 0.688191, 0.955423, 0.295242, 0, 1, 0, 0, 0.951056, 0.16246, 0.262866,
	0.850651, -0.525731, 0, 0.955423, -0.295242, 0, 0.864188, -0.442863, 0.238856, 0.951056, -0.16246, 0.262866, 0.809017, -0.309017, 0.5, 0.681718, -0.147621, 0.716567, 0.850651, 0, 0.525731, 0.864188, 0.442863, -0.238856, 0.809017, 0.309017, -0.5,
	0.951056, 0.16246, -0.262866, 0.525731, 0, -0.850651, 0.681718, 0.147621, -0.716567, 0.681718, -0.147621, -0.716567, 0.850651, 0, -0.525731, 0.809017, -0.309017, -0.5, 0.864188, -0.442863, -0.238856, 0.951056, -0.16246, -0.262866, 0.147621, 0.716567, -0.681718,
	0.309017, 0.5, -0.809017, 0.425325, 0.688191, -0.587785, 0.442863, 0.238856, -0.864188, 0.587785, 0.425325, -0.688191, 0.688191, 0.587785, -0.425325, -0.147621, 0.716567, -0.681718, -0.309017, 0.5, -0.809017, 0, 0.525731, -0.850651, -0.525731, 0, -0.850651,
	-0.442863, 0.238856, -0.864188, -0.295242, 0, -0.955423, -0.16246, 0.262866, -0.951056, 0, 0, -1, 0.295242, 0, -0.955423, 0.16246, 0.262866, -0.951056, -0.442863, -0.238856, -0.864188, -0.309017, -0.5, -0.809017, -0.16246, -0.262866, -0.951056,
	0, -0.850651, -0.525731, -0.147621, -0.716567, -0.681718, 0.147621, -0.716567, -0.681718, 0, -0.525731, -0.850651, 0.309017, -0.5, -0.809017, 0.442863, -0.238856, -0.864188, 0.16246, -0.262866, -0.951056, 0.238856, -0.864188, -0.442863, 0.5, -0.809017, -0.309017,
	0.425325, -0.688191, -0.587785, 0.716567, -0.681718, -0.147621, 0.688191, -0.587785, -0.425325, 0.587785, -0.425325, -0.688191, 0, -0.955423, -0.295242, 0, -1, 0, 0.262866, -0.951056, -0.16246, 0, -0.850651, 0.525731, 0, -0.955423, 0.295242,
	0.238856, -0.864188, 0.442863, 0.262866, -0.951056, 0.16246, 0.5, -0.809017, 0.309017, 0.716567, -0.681718, 0.147621, 0.525731, -0.850651, 0, -0.238856, -0.864188, -0.442863, -0.5, -0.809017, -0.309017, -0.262866, -0.951056, -0.16246, -0.850651, -0.525731, 0,
	-0.716567, -0.681718, -0.147621, -0.716567, -0.681718, 0.147621, -0.525731, -0.850651, 0, -0.5, -0.809017, 0.309017, -0.238856, -0.864188, 0.442863, -0.262866, -0.951056, 0.16246, -0.864188, -0.442863, 0.238856, -0.809017, -0.309017, 0.5, -0.688191, -0.587785, 0.425325,
	-0.681718, -0.147621, 0.716567, -0.442863, -0.238856, 0.864188, -0.587785, -0.425325, 0.688191, -0.309017, -0.5, 0.809017, -0.147621, -0.716567, 0.681718, -0.425325, -0.688191, 0.587785, -0.16246, -0.262866, 0.951056, 0.442863, -0.238856, 0.864188, 0.16246, -0.262866, 0.951056,
	0.309017, -0.5, 0.809017, 0.147621, -0.716567, 0.681718, 0, -0.525731, 0.850651, 0.425325, -0.688191, 0.587785, 0.587785, -0.425325, 0.688191, 0.688191, -0.587785, 0.425325, -0.955423, 0.295242, 0, -0.951056, 0.16246, 0.262866, -1, 0, 0,
	-0.850651, 0, 0.525731, -0.955423, -0.295242, 0, -0.951056, -0.16246, 0.262866, -0.864188, 0.442863, -0.238856, -0.951056, 0.16246, -0.262866, -0.809017, 0.309017, -0.5, -0.864188, -0.442863, -0.238856, -0.951056, -0.16246, -0.262866, -0.809017, -0.309017, -0.5,
	-0.681718, 0.147621, -0.716567, -0.681718, -0.147621, -0.716567, -0.850651, 0, -0.525731, -0.688191, 0.587785, -0.425325, -0.587785, 0.425325, -0.688191, -0.425325, 0.688191, -0.587785, -0.425325, -0.688191, -0.587785, -0.587785, -0.425325, -0.688191, -0.688191, -0.587785, -0.425325
];
function byteToDir(b) {
	return b >= 0 && b < 162 ? [EV_BYTEDIRS[b * 3], EV_BYTEDIRS[b * 3 + 1], EV_BYTEDIRS[b * 3 + 2]] : [0, 0, 0];
}

// the angles of an event that are a direction (es->angles as fxDir): never no direction at all
const evDir = a => !a[0] && !a[1] && !a[2] ? [0, 1, 0] : a;

// What an event does with the time of the frame it is taken in (cg.time of CG_EntityEvent). An event comes with
// a poll, between two frames: the frame that comes next is the one it is taken in, and these run at its start.
// No more than a few are kept: no frame comes while the tab of the page is in the back.
let evTaken = [];
const evAtFrame = fn => { if (evTaken.length < 256) evTaken.push(fn); };

// What cgame keeps of an entity for its events (centity_t), by the number of the entity
const evCents = new Map();
function evCent(id) {
	let c = evCents.get(id);
	if (!c) evCents.set(id, c = { muzzleFlashTime: -1e9, flashFrames: 0, damageTime: -1e9, damageAngles: [0, 0, 0], teamPowerEffectTime: -1e9, teamPowerType: 0 });
	return c;
}

// cg_saberFlashTime and cg_saberFlashPos (cg_draw.c): where two blades met last
const evFlash = { time: -1e9, pos: [0, 0, 0] };
// the primitives cgame hands the effects that the page draws by itself here (FX_AddBezier)
let evBeziers = [];

function evClear() {
	evCents.clear();
	evTaken = [];
	evBeziers = [];
	evFlash.time = -1e9;
	evShake.intensity = evShake.duration = evShake.start = 0;
}

// What the page knows of the entity an event came from, as far as cgame reads it from its currentState: a player
// or an NPC. null: something else, or one the page is not told of.
function evEntState(id) {
	const p = players.get(id);
	if (p) return { weapon: p.data.weapon, eFlags: p.data.eFlags | 0, npc: null };
	const n = id >= EV_MAX_CLIENTS && state ? (state.npcs || []).find(n => n.id === id) : null;
	return n ? { weapon: n.weapon, eFlags: n.f ? n.f[4] | 0 : 0, npc: n } : null;
}

// CGCam_Shake (cg_view.c): the view shakes for duration msec, less and less
const EV_MAX_SHAKE_INTENSITY = 16;
const evShake = { intensity: 0, duration: 0, start: 0 };
function evCamShakeStart(intensity, duration) {
	evAtFrame(now => {
		evShake.intensity = Math.min(intensity, EV_MAX_SHAKE_INTENSITY);
		evShake.duration = duration;
		evShake.start = now;
	});
}

// CG_SE_UpdateShake (cg_view.c): what the shake adds to the eye and to the angles of the view (pitch, yaw) in the
// frame of the time now, anew in every frame. null: nothing shakes. For the camera of a view through the eyes of
// a player (firstPersonCamera of js/weapons.js).
function evCamShake(now) {
	if (evShake.duration <= 0) return null;
	if (now > evShake.start + evShake.duration) { evShake.intensity = evShake.duration = evShake.start = 0; return null; }
	// the scale also takes the FOV into account, with 90 as normal: both of its FOVs are 90 here
	const intensity = evShake.intensity * (1 - (now - evShake.start) / evShake.duration), r = () => (Math.random() * 2 - 1) * intensity;
	return { origin: [r(), r(), r()], angles: [r(), r()] };
}

// CG_FireWeapon (cg_weapons.c): caused by EV_FIRE_WEAPON and EV_ALT_FIRE. The entity is marked as muzzle flashing:
// the flash itself is added where its weapon is drawn (evAddPlayerWeapon). The sound is not played.
function cgFireWeapon(e, altFire) {
	if (e.weapon === WP_NONE || e.weapon >= WP_NUM_WEAPONS) return;
	const c = evCent(e.ent);
	evAtFrame(now => { c.muzzleFlashTime = now; c.flashFrames = 0; });
	if (e.ent !== view.id) return;
	// The kick of the weapons that have one, for the one the page looks through the eyes of. The pistol, the
	// bowcaster and the DEMP 2 shake by how long they were charged (cg.time - constantLight), which the feed does
	// not tell: the least of it, 0.2 times 2. The concussion rifle kicks the angles of the view instead (ownKick of
	// js/weapons.js).
	const w = e.weapon;
	if (((w === WP_BRYAR_PISTOL || w === WP_BRYAR_OLD || w === WP_DEMP2) && altFire) || (w === WP_BOWCASTER && !altFire)) evCamShakeStart(0.2 * 2, 250);
	else if (w === WP_CONCUSSION && !altFire) evAtFrame(now => ownKick(e.ent, now));
	else if (w === WP_ROCKET_LAUNCHER || (w === WP_REPEATER && altFire) || (w === WP_FLECHETTE && altFire)) evCamShakeStart(2 + Math.random(), 350);
	else if (w === WP_FLECHETTE) evCamShakeStart(1.5, 250);
}

// CG_AddPlayerWeapon (cg_weapons.c) for one who is seen from outside, the part after the weapon itself: the effect
// of the muzzle at the first bolt of the weapon model on him (*flash, wpMuzzles of js/weapons.js has it for the
// frame he was drawn in) along what cgame reads as POSITIVE_X of that bolt. Which of the two effects is by
// EF_ALT_FIRING of the entity, not by the event.
// The game plays it anew in every frame of a client while cg.time - muzzleFlashTime is at most MUZZLE_FLASH_TIME:
// with frames of 8 msec (FX_FRAME_MSEC of js/fx-run.js) at 0, 8 and 16 msec after the shot. A frame of the page is
// longer, so it plays as many as the game had frames since the last one (as fxFramePlays does for the effects that
// never stop),
// and none that would come more than EV_LATE_MSEC late. In the comparison with the game the time stands: the one
// of the frame the shot is taken in.
// frame: the time of the frame his muzzle is taken from.
function evAddPlayerWeapon(id, c, frame) {
	const d = evEntState(id), dt = frame - c.muzzleFlashTime;
	if (!d || d.weapon === WP_EMPLACED_GUN || dt < 0) return;
	// The flash goes on while a DEMP 2 fires (continuous flash), else it is a moment (impulse flash). The effect
	// itself is played for 10 msec more than that moment, which only the DEMP 2 gets to.
	const time = d.weapon === WP_DEMP2 && d.eFlags & EV_EF_FIRING ? MUZZLE_FLASH_TIME + 10 : MUZZLE_FLASH_TIME;
	if (dt > time + EV_LATE_MSEC) return;
	// without a weapon model on him there is no flash (G2API_HasGhoul2ModelOnIndex), nor is there one for somebody
	// who is not drawn in this frame. From inside his own head the muzzle is the one of the weapon in front of the
	// camera (tag_flash, drawViewWeapon of js/weapons.js); the game plays the effect bound to that entity with all
	// three of its axes there, the page along its first.
	const muzzle = wpMuzzles.get(id);
	if (!muzzle || muzzle.now !== frame) return;
	const frames = Math.floor(Math.min(dt, time) / FX_FRAME_MSEC) + 1, plays = frames - c.flashFrames;
	if (plays <= 0) return;
	c.flashFrames = frames;
	const w = fxShot(d.weapon, d.eFlags & EV_EF_ALT_FIRING);
	if (w && w.muzzle) for (let i = 0; i < plays; i++) fxPlay(w.muzzle, muzzle.org, muzzle.dir);
}

// What the events do in a frame besides drawing: the flashes of the muzzles.
let evFrameAt = -1, evHookAt = -1, evHookBefore = -1;
function evFrame(frame) {
	evFrameAt = frame;
	if (!fxOn()) return;
	for (const [id, c] of evCents) evAddPlayerWeapon(id, c, frame);
}

// Before anything of a frame is drawn (the lights of the frame are gathered then, js/dlights.js): the events that
// came since the last frame are taken with the time of this one. And the light of a muzzle that flashes
// (weapon->flashDlightColor in CG_AddPlayerWeapon: of what a hand fires only the stun baton has one), at the
// muzzle of the frame before, the one of this frame is not known yet.
dlightSources.push(now => {
	// A frame that did not get to drawEvents: frame.js calls that for the game look only, and the plain view of
	// the editor plays effects too. Its flashes are played here, a frame late.
	if (evHookAt >= 0 && evFrameAt !== evHookAt) evFrame(evHookAt);
	evHookBefore = evHookAt;
	evHookAt = now;
	if (evWarm !== loadedMap && fxOn()) {
		// what cgame registers when it starts (CG_RegisterGraphics, CG_RegisterEffects), so that the first spark is not late
		evWarm = loadedMap;
		hudShader('gfx/effects/saberFlare', true);
		for (const name of ['saber/saber_block', 'saber/saber_cut', 'saber/blood_sparks_mp', 'saber/blood_sparks_25_mp', 'saber/blood_sparks_50_mp', 'blaster/deflect', 'mp/spawn']) fxGet(name);
	}
	const taken = evTaken;
	evTaken = [];
	for (const fn of taken) fn(now);
	for (const [id, c] of evCents) {
		// nothing of him goes on any more (what was taken just now has its time by here)
		if (now - Math.max(c.muzzleFlashTime, c.damageTime, c.teamPowerEffectTime) > 1000) { evCents.delete(id); continue; }
		const d = now - c.muzzleFlashTime <= MUZZLE_FLASH_TIME ? evEntState(id) : null, muzzle = d && d.weapon === WP_STUN_BATON ? wpMuzzles.get(id) : null;
		// 300 and up to 31 more, anew in every frame (rand() & 31)
		if (muzzle && muzzle.now === evHookBefore && fxOn()) dlightAdd(muzzle.org, 300 + (refCam ? 15.5 : Math.floor(Math.random() * 32)), [0.6, 0.6, 1]);
	}
});

// vectoangles (q_math.c): the angles of a direction
function evVectoangles(v) {
	if (!v[0] && !v[1]) return [v[2] > 0 ? -90 : -270, 0, 0];
	let yaw = v[0] ? Math.atan2(v[1], v[0]) * 180 / Math.PI : v[1] > 0 ? 90 : 270, pitch = Math.atan2(v[2], Math.hypot(v[0], v[1])) * 180 / Math.PI;
	if (yaw < 0) yaw += 360;
	if (pitch < 0) pitch += 360;
	return [-pitch, yaw, 0];
}

// CG_PlayerShieldHit (cg_players.c), caused by EV_SHIELD_HIT: the shield of somebody took a hit from dir. He shows
// half a shield towards it for a time that grows with what the shield took (amount), at most two seconds.
const EV_MAX_SHIELD_TIME = 2000, EV_MIN_SHIELD_TIME = 2000;
function cgPlayerShieldHit(entitynum, dir, amount) {
	if (entitynum < 0 || entitynum >= 1024) return;
	const c = evCent(entitynum);
	evAtFrame(now => {
		const time = amount > 100 ? now + EV_MAX_SHIELD_TIME : now + 500 + amount * 15;
		if (time > c.damageTime) { c.damageTime = time; c.damageAngles = evVectoangles([-dir[0], -dir[1], -dir[2]]); }
	});
}

// CG_PlayerHitFX with CG_DrawPlayerShield (cg_players.c): the half shield of one whose shield took a hit, a model
// around him that is turned to where the hit came from, fainter and wider as its time runs out. For the one who
// draws a player or an NPC (plDrawModel of js/player-fx.js, which does not call it yet), with his cent: number,
// lerpOrigin, eFlags, vehicle. Not from inside his own head, not for the dead (EF_DEAD), not for a vehicle.
function evPlayerHitFX(cent, ownView) {
	const c = evCents.get(cent.number), now = frameNow;
	if (!c || !(c.damageTime > now) || ownView || (cent.eFlags & 2) || cent.vehicle) return;
	const model = staticNow('models/weaphits/testboom.md3');
	if (!model || !modelsScripted()) return;
	let alpha = Math.trunc(255 * ((c.damageTime - now) / EV_MIN_SHIELD_TIME) + (refCam ? 0.5 : Math.random()) * 16);
	if (alpha > 255) alpha = 255;
	// bigger, but tighter if more solid: 1.0 to 1.4
	const scale = 1.4 - alpha * (0.4 / 255), o = cent.lerpOrigin || cent.origin, v = alpha / 255;
	drawModelScripted(model.meshes, anglesMatrix([o[0], o[1], o[2] + 10], c.damageAngles, scale), { shader: 'halfShieldShell', rgba: [v, v, v, 1] });
}

// The shell CG_Player draws around one a team power, or a drain, has just reached (teamPowerEffectTime): his model
// again with powerups/ysalimarishell, in green for a heal, blue for force given and red for one who is drained,
// fading in a second. For plDrawModel of js/player-fx.js, which does not call it yet: { shader, rgba } to draw his
// meshes with (tint and forceAlpha both set), or null. EV_FORCE_DRAINED sets it here; EV_TEAM_POWER needs the
// clients it is for (the bits of trickedentindex of its entity), which the feed does not have.
function evTeamPower(cent) {
	const c = evCents.get(cent.number), left = c ? c.teamPowerEffectTime - frameNow : 0;
	if (!(left > 0) || c.teamPowerType === 3) return null;
	const rgb = c.teamPowerType === 1 ? [0, 1, 0] : c.teamPowerType === 0 ? [0, 0, 1] : [1, 0, 0];
	return { shader: 'powerups/ysalimarishell', rgba: [rgb[0], rgb[1], rgb[2], Math.trunc(left / 8) / 255] };
}

// CG_GetClientWeaponMuzzleBoltPoint: where the muzzle of the weapon model on a client is, into what the server
// said (to) when he has such a model. The page has it for one it drew a moment ago.
function evClientMuzzlePoint(client, to) {
	const muzzle = client >= 0 && client < EV_MAX_CLIENTS ? wpMuzzles.get(client) : null;
	return muzzle && Math.abs(frameNow - muzzle.now) < 250 ? muzzle.org.slice() : to;
}

// The start of the beam of a disruptor (EV_DISRUPTOR_MAIN_SHOT and EV_DISRUPTOR_SNIPER_SHOT): the muzzle of the
// weapon on the one who fired (the parameter of the event) instead of the point the server shot from. Through
// his own eyes the game takes the muzzle of the weapon in front of the camera as it last was
// (cg.lastFPFlashPoint, view.flashPoint of js/weapons.js), and the point of the server while there is none.
function evDisruptorStart(e) {
	if (e.parm !== view.id || view.thirdPerson) return evClientMuzzlePoint(e.parm, e.org2);
	return view.flashPoint ? view.flashPoint.slice() : e.org2;
}

// FX_AddBezier of the engine (client/fx/FxUtil.cpp): a band along a curve from start to end with two control points
// that move, for killTime msec. The effects of the page have no such primitive (js/fx-draw.js), so these are kept
// and drawn here (evDrawBeziers). The values go from the first to the second as the flags say, as for fxAddLine.
function evAddBezier(start, end, control1, control1Vel, control2, control2Vel, size1, size2, sizeParm, alpha1, alpha2, alphaParm, sRGB, eRGB, rgbParm, killTime, shader, flags) {
	if (!fxOn() || evBeziers.length >= 64) return;
	const parm = (v, shift) => { const mode = flags >> shift & FX_PARM_MASK; return mode === FX_WAVE ? v * 3.14159 * 0.001 : mode ? v * 0.01 * killTime + fxTime : 0; };
	evBeziers.push({ flags, t0: fxTime, t1: fxTime + killTime, org: start.slice(0, 3), org2: end.slice(0, 3), c1: control1.slice(), c1Vel: control1Vel.slice(), c2: control2.slice(), c2Vel: control2Vel.slice(),
		shader, size: [size1, size2, parm(sizeParm, 8)], alpha: [alpha1, alpha2, parm(alphaParm, 0)], rgb: [sRGB.slice(), eRGB.slice(), parm(rgbParm, 4)], moved: fxTime });
}

// CBezier::Update, Cull, Draw and DrawSegment (client/fx/FxPrimitives.cpp): the curve in 16 pieces (BEZIER_RESOLUTION),
// each a quad that shows its flat side to the eye and is glued to the one before. The band is size wide from its
// start to its end, and its colour fades to nothing along it: at its start it is black.
function evBezierQuads(b, fwd) {
	const sec = (fxTime - b.moved) * 0.001, k = sec + sec * sec * 0.5;
	b.moved = fxTime;
	for (let i = 0; i < 3; i++) { b.c1[i] += k * b.c1Vel[i]; b.c2[i] += k * b.c2Vel[i]; }
	// all of it behind the viewer: not drawn
	if ([b.org, b.org2, b.c1].every(p => (p[0] - eye[0]) * fwd[0] + (p[1] - eye[1]) * fwd[1] + (p[2] - eye[2]) * fwd[2] < 0)) return;
	fxColour(b);
	const rgba = [fxColor[0], fxColor[1], fxColor[2], fxColor[3]], incr = 1 / 16;
	const tint = tc => { for (let i = 0; i < 3; i++) fxColor[i] = tc < 0 ? 0 : Math.trunc(rgba[i] * (1 - tc)); fxColor[3] = tc < 0 ? 0 : rgba[3]; };
	let old = b.org, tc1 = 0, low = null;
	for (let mu = incr; mu <= 1; mu += incr) {
		const m1 = 1 - mu, g0 = m1 * m1 * m1, g1 = 3 * mu * m1 * m1, g2 = 3 * mu * mu * m1, g3 = mu * mu * mu;
		const pos = [0, 1, 2].map(i => g0 * b.org[i] + g1 * b.c1[i] + g2 * b.c2[i] + g3 * b.org2[i]);
		const cross = fxUnit(fxCross([pos[0] - old[0], pos[1] - old[1], pos[2] - old[2]], [pos[0] - eye[0], pos[1] - eye[1], pos[2] - eye[2]]));
		const bottom = (b.size[0] + (b.size[1] - b.size[0]) * (mu - incr)) * 0.5, top = (b.size[0] + (b.size[1] - b.size[0]) * mu) * 0.5;
		if (!low) low = [[old[0] - cross[0] * bottom, old[1] - cross[1] * bottom, old[2] - cross[2] * bottom], [old[0] + cross[0] * bottom, old[1] + cross[1] * bottom, old[2] + cross[2] * bottom]];
		const high = [[pos[0] - cross[0] * top, pos[1] - cross[1] * top, pos[2] - cross[2] * top], [pos[0] + cross[0] * top, pos[1] + cross[1] * top, pos[2] + cross[2] * top]];
		// the corners of the engine 0 1 3 2: the quads of the page are the triangles 0 1 2 and 2 1 3
		tint(tc1 === 0 ? -1 : tc1);
		fxVertex(low[0][0], low[0][1], low[0][2], 0, tc1);
		fxVertex(low[1][0], low[1][1], low[1][2], 1, tc1);
		tint(mu);
		fxVertex(high[0][0], high[0][1], high[0][2], 0, mu);
		fxVertex(high[1][0], high[1][1], high[1][2], 1, mu);
		low = high; old = pos; tc1 = mu;
	}
}

// the bands that live, drawn as the effects are (their quads go into the buffer of the effects, as the blades do)
function evDrawBeziers(now) {
	evStats.beziers = evStats.quads = 0;
	if (!evBeziers.length) return;
	evBeziers = evBeziers.filter(b => fxTime <= b.t1);
	if (!evBeziers.length || !fxOn()) return;
	const fwd = cameraAxes().fwd, fog = fogShown() && world ? world.globalFog : null, batches = [];
	fxVerts = 0;
	for (const shader of new Set(evBeziers.map(b => b.shader))) {
		const mat = materialFor(shader, 'fx'), from = fxVerts;
		if (mat.nodraw || !mat.stages.length) continue;
		for (const b of evBeziers) {
			if (b.shader !== shader || fxVerts + 16 * 4 > FX_MAX_QUADS * 4) continue;
			const start = fxVerts;
			fxBad = false;
			evBezierQuads(b, fwd);
			if (fxBad) fxVerts = start;
		}
		if (fxVerts > from) batches.push({ mat, t: now / 1000, first: from / 4 * 6, count: (fxVerts - from) / 4 * 6, fog });
	}
	evStats.beziers = evBeziers.length;
	evStats.quads = fxVerts / 4;
	fxDrawBatches(batches);
}

// CG_SaberClashFlare (cg_draw.c): the flare where two blades met, a picture of the 2D that is 100 msec there and
// gets smaller, larger the nearer the place is. Not behind the viewer, not behind a wall, not from further than
// 1200. The game draws it with its 2D (CG_Draw2D): for one who watches, and for one who plays while he lives.
function evSaberClashFlare(now) {
	const t = now - evFlash.time, maxTime = 150;
	// (not in a picture that is taken without the 2D, cg_draw2D 0: the comparison with the game takes the views of
	// its own spectator that way)
	if (t <= 0 || t >= maxTime || document.body.classList.contains('nohud') || (refCam && view.id < 0)) return;
	if (view.id >= 0) { const p = players.get(view.id); if (p && p.data.team !== 'spectator' && (p.data.dead || p.data.health <= 0)) return; }
	const dif = [evFlash.pos[0] - eye[0], evFlash.pos[1] - eye[1], evFlash.pos[2] - eye[2]], fwd = cameraAxes().fwd;
	if (dif[0] * fwd[0] + dif[1] * fwd[1] + dif[2] * fwd[2] < 0.2) return;
	// CONTENTS_SOLID alone is in the way
	if (traceBox(eye, evFlash.pos, null, null, 0x1).fraction < 1) return;
	const len = Math.hypot(dif[0], dif[1], dif[2]);
	if (len > 1200) return;
	let v = (1 - t / maxTime) * ((1 - len / 800) * 2 + 0.35);
	if (v < 0.001) v = 0.001;
	// CG_WorldCoordToScreenCoord: the place on the 640 x 480 screen, cut to whole numbers. The game works it out
	// from the angles of its view, the page from its projection, which is the one of the game.
	const sp = project(evFlash.pos);
	if (!sp || !hudSettleShaders()) return;
	const x = Math.trunc(sp[0] * 640 / canvas.clientWidth), y = Math.trunc(sp[1] * 480 / canvas.clientHeight);
	evStats.flare = { now, t, x, y, v, len };
	hudSetColor([0.8, 0.8, 0.8, 1]);
	hudPic(x - v * 300, y - v * 300, v * 600, v * 600, hudShader('gfx/effects/saberFlare', true));
	hudSetColor(null);
	hud2dFlush();
}

// EV_SABER_HIT: sparks where a blade hit, by what it hit and how hard (the parameter). The effects are the ones of
// cgs.effects (cg_main.c); a saber with effects of its own (hitPersonEffect and so on of its .sab file) is not
// known to the page.
function evSaberHit(e) {
	const dir = evDir(e.angles), play = (name, times) => { for (let i = 0; i < times; i++) fxPlay(name, e.org, dir); };
	if (e.parm === 16) play('saber/blood_sparks_mp', 6);			// lots of sparks, something special happened
	else if (e.parm === 3) play('saber/blood_sparks_25_mp', 1);		// a person, a light hit
	else if (e.parm === 2) play('saber/blood_sparks_50_mp', 1);
	else if (e.parm) play('saber/blood_sparks_mp', 3);
	else play('saber/saber_cut', 1);								// something else
}

// cg_saberFlashTime = cg.time - 50 and cg_saberFlashPos: the flare of CG_SaberClashFlare is set off
function evSaberFlash(org) {
	const pos = org.slice();
	evAtFrame(now => { evFlash.time = now - 50; evFlash.pos = pos; });
}

// EV_SABER_BLOCK: a blade was stopped by a blade (the parameter is set), with the flare, or a shot by a blade
function evSaberBlock(e) {
	const dir = evDir(e.angles);
	if (!e.parm) { fxPlay('blaster/deflect', e.org, dir); return; }
	fxPlay('saber/saber_block', e.org, dir);
	evSaberFlash(e.org);
}

// EV_PLAYER_TELEPORT_IN and OUT: the effect of a spawn on the floor below the place, found with the box of a
// player moved down (CG_Trace with playerMins and playerMaxs), played upwards. Not where there is no floor.
function evTeleport(e, effect = 'mp/spawn') {
	const tr = traceBox(e.org, [e.org[0], e.org[1], e.org[2] - 4096], [-15, -15, -16], [15, 15, 40]);
	if (tr.fraction === 1) return;
	fxPlay(effect, tr.pos, [0, 0, 1]);
}

// The cases of CG_EntityEvent that show something, by the name of the event. position is e.org (the place of the
// entity that carried the event), es->origin of an event entity is that place too but for the rounding of the
// server.
const evHandlers = {
	EV_FIRE_WEAPON(e) {
		const d = evEntState(e.ent);
		// A turret (an entity that is no player and no NPC) plays emplaced/muzzle_flash at a bolt of its model: the
		// page does not have its bolts. The emplaced gun of a player and a vehicle show nothing here.
		if (e.ent >= EV_MAX_CLIENTS && !(d && d.npc)) return;
		if (e.weapon === WP_EMPLACED_GUN && !(d && d.npc)) return;
		if (d && d.npc && d.npc.class === PG.CLASS_VEHICLE) return;
		cgFireWeapon(e, false);
	},
	EV_ALT_FIRE(e) {
		const d = evEntState(e.ent);
		if (e.weapon === WP_EMPLACED_GUN || (d && d.npc && d.npc.class === PG.CLASS_VEHICLE)) return;
		cgFireWeapon(e, true);
	},
	EV_SABER_HIT: evSaberHit,
	EV_SABER_BLOCK: evSaberBlock,
	EV_SABER_CLASHFLARE(e) { evSaberFlash(e.org); },
	EV_DISRUPTOR_MAIN_SHOT(e) { fxDisruptorMainShot(evDisruptorStart(e), e.org); },
	// whether it was fully charged (shouldtarget of the entity) the feed does not tell: the beam without the second one
	EV_DISRUPTOR_SNIPER_SHOT(e) { fxDisruptorAltShot(evDisruptorStart(e), e.org, false); },
	EV_DISRUPTOR_SNIPER_MISS(e) { const dir = byteToDir(e.parm); if (e.weapon) fxDisruptorHitWall(e.org, dir); else fxDisruptorAltMiss(e.org, dir); },
	EV_DISRUPTOR_HIT(e) { const dir = byteToDir(e.parm); if (e.weapon) fxDisruptorHitPlayer(e.org, dir); else fxDisruptorHitWall(e.org, dir); },
	// Missile impacts. Not known here: an effect of its own (emplacedOwner), the shot of a vehicle weapon with an
	// impact of its own (CG_VehicleWeaponImpact), how far the pistol was charged (generic1), the mark on a model
	// (CG_G2MarkEvent).
	EV_MISSILE_HIT(e) { cgMissileHitPlayer(e.weapon, e.org, byteToDir(e.parm), !!(e.eFlags & EV_EF_ALT_FIRING)); },
	EV_MISSILE_MISS(e) { cgMissileHitWall(e.weapon, e.org, byteToDir(e.parm), !!(e.eFlags & EV_EF_ALT_FIRING), 0); },
	EV_MISSILE_MISS_METAL(e) { cgMissileHitWall(e.weapon, e.org, byteToDir(e.parm), !!(e.eFlags & EV_EF_ALT_FIRING), 0); },
	// The other way to fire the concussion rifle: a ring every 64 units of its way, what it leaves at the wall,
	// its beam, and the wisp of smoke of the disruptor. The way is in the angles of the event (the end minus the
	// muzzle). The rings look along angles2, which the feed does not have: it is the way the shot went too.
	EV_CONC_ALT_IMPACT(e) {
		const shotDist = Math.hypot(e.angles[0], e.angles[1], e.angles[2]), fwd = fxUnit(e.angles), dir = byteToDir(e.parm);
		let spot = null;
		for (let dist = 0; dist < shotDist; dist += 64) {
			spot = [e.org2[0] + dist * fwd[0], e.org2[1] + dist * fwd[1], e.org2[2] + dist * fwd[2]];
			fxPlay('concussion/alt_ring', spot, fwd);
		}
		cgMissileHitWall(WP_CONCUSSION, e.org, dir, true, 0);
		if (spot) fxConcAltShot(e.org2, spot);
		fxDisruptorAltMiss(e.org, dir);
	},
	EV_PLAYER_TELEPORT_IN(e) { evTeleport(e); },
	EV_PLAYER_TELEPORT_OUT(e) { evTeleport(e); },
	// the same below the one who became the Jedi Master, with another effect
	EV_BECOME_JEDIMASTER(e) { evTeleport(e, 'mp/jedispawn'); },
	EV_SHIELD_HIT(e) { cgPlayerShieldHit(e.other, byteToDir(e.parm), e.time2); },
	EV_FORCE_DRAINED(e) { const c = evCent(e.owner); evAtFrame(now => { c.teamPowerEffectTime = now + 1000; c.teamPowerType = 2; }); },
	// The view shakes: angles[0] is how much, time for how long. For everybody, or for one client alone (modelindex
	// of the entity is his number plus one), which the feed does not tell: taken as for everybody. It shakes the
	// view through the eyes of a player (evCamShake).
	EV_SCREENSHAKE(e) { if (view.id >= 0) evCamShakeStart(e.angles[0], e.time); },
	// The fall of the one the page looks through the eyes of dips his view (DoFall, ownFall of js/weapons.js); not
	// while he falls to his death. For everybody else it is a sound.
	// who killed whom: for the 2D of the one the page looks through the eyes of (js/hud-scores.js). At the next
	// frame, when the player state that came with the event is in the state. And the line it prints to the console
	// (js/hud-console.js).
	EV_OBITUARY(e) { hudObituaryPrint(e); evAtFrame(() => hudObituary(e)); },
	// what the one the page looks through the eyes of picked up shows as an icon in his 2D (js/hud.js)
	EV_ITEM_PICKUP(e) { hudItemPickup(e); },
	EV_FALL(e) { if (e.ent === view.id && !(state && state.hud && state.hud.client === view.id && state.hud.fallingToDeath)) evAtFrame(now => ownFall(e.ent, e.parm, now)); }
	// Cases that show something and are not here, each for what it needs: EV_GLASS_SHATTER, EV_DEBRIS and
	// EV_MISC_MODEL_EXP (pieces that fly: local entities, and fields of the entity the feed does not have),
	// EV_TEAM_POWER (see evTeamPower), EV_VEH_FIRE (the muzzles of a vehicle), EV_SCOREPLUM (a local entity),
	// EV_ITEM_PICKUP, EV_NOAMMO and EV_LOCALTIMER (the HUD of the one who plays), EV_GHOUL2_MARK (marks on models),
	// EV_BODYFADE, EV_USE_ITEM (the binoculars), EV_OBITUARY and the other texts, EV_TESTLINE and EV_DEBUG_LINE.
	// EV_PLAY_EFFECT, EV_PLAY_EFFECT_ID and EV_PLAY_PORTAL_EFFECT_ID come with the feed of the effects (fxTakeState).
	// Cases that only play a sound or change what the one who plays hears or predicts, as a group: the steps
	// (EV_FOOTSTEP to EV_SWIM, EV_FALL, EV_STEP_4 to 16, EV_JUMP, EV_ROLL), EV_TAUNT and all the voice events
	// (EV_ANGER1 to EV_GLOAT3, EV_PUSHFAIL, EV_VOICECMD_SOUND), the water events, EV_CHANGE_WEAPON,
	// EV_SABER_ATTACK, EV_SABER_UNHOLSTER, EV_DISRUPTOR_ZOOMSOUND, EV_PREDEFSOUND, EV_ITEM_POP, EV_ITEM_RESPAWN
	// (its item is scaled up by js/items.js, which sees it come back), EV_GRENADE_BOUNCE, EV_MISSILE_STICK,
	// EV_PLAYDOORSOUND, EV_PLAYDOORLOOPSOUND, EV_BMODEL_SOUND, EV_MUTE_SOUND, EV_GENERAL_SOUND, EV_GLOBAL_SOUND,
	// EV_GLOBAL_TEAM_SOUND, EV_ENTITY_SOUND, EV_PAIN, EV_DEATH1 to 3, EV_STARTLOOPINGSOUND, EV_STOPLOOPINGSOUND,
	// EV_WEAPON_CHARGE, EV_WEAPON_CHARGE_ALT, EV_GLOBAL_DUEL, EV_PRIVATE_DUEL, EV_ITEMUSEFAIL.
};

// one event of the game, as a client gets it in CG_EntityEvent
function gameEvent(e) {
	// what is older than the server keeps an event for no client is told: the page was not looking then
	if (e.ago > EVENT_VALID_MSEC || !fxOn()) return;
	const handler = evHandlers[e.name];
	if (handler) handler(e);
}

// at the end of the scene: what of the events is drawn by itself (the flare where two blades met)
function drawEvents(now) {
	evFrame(now);
	evDrawBeziers(now);
	evSaberClashFlare(now);
}
