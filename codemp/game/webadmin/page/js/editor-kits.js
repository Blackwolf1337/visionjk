// ---- kits: entities that belong together, placed in one go and wired already
//
// A kit is offered where the classes are searched, with a + before its name. Placing one makes drafts of all it is
// made of where the floor was clicked: what is to be different is changed in their forms, and Place all sends
// them, what is aimed at before what aims at it. A kit is a name, a line that says what it makes, the classes it
// is made of (the first one gives it its colour in the list, and a game module that lacks one of them does not
// offer the kit) and make(at, names): the list of { keys, pos } openDrafts takes. at is the spot on the floor,
// names(stem) gives a targetname that is free, another one with every call.
//
// Every key and spawnflag here is one the spawn function of its class reads (g_trigger.c, g_target.c, g_misc.c,
// g_client.c, g_items.c, props/g_props.c), so that a kit is complete as it is placed. There are no doors and no
// other movers: those are made of brushes of the map.

// one of a kit where its class is placed above that spot of the floor, with the keys its class begins with and more
const kitEnt = (cls, spot, more) => placedAt(cls, placeSpot(cls, spot), more);

// A trigger of a kit as a box of its own size that stands on the floor: half is half of how wide, deep and high.
function kitBox(cls, spot, half, more = []) {
	const pos = [spot[0], spot[1], spot[2] + half[2]];
	return { keys: [['classname', cls], ['origin', fmtVec(pos)], ['mins', half.map(v => -v).join(' ')], ['maxs', half.join(' ')]].concat(more), pos };
}

// The spot beside the one that was clicked, dx and dy away, on the floor that is there when it is about as high.
// Else as high as the click: nothing of a kit goes a storey up or down.
function kitSpot(at, dx, dy) {
	const hit = pickGround([at[0] + dx, at[1] + dy, at[2] + 40], [0, 0, -1]);
	return [at[0] + dx, at[1] + dy, hit && Math.abs(hit[2] - at[2]) <= 40 ? hit[2] : at[2]];
}

// the items of the kit Pickups, from bg_itemlist (bg_misc.c): a weapon, its ammo, health, shield
const KIT_PICKUPS = ['weapon_blaster', 'ammo_blaster', 'item_medpak_instant', 'item_shield_sm_instant'];

const KITS = [
	// trigger_teleport sends who touches it to the origin of its target (trigger_teleporter_touch). What the class
	// is placed with by itself: its box, and a target_position towards the viewer.
	{ name: 'Teleporter', about: 'a trigger_teleport box and where it sends to', classes: ['trigger_teleport', 'target_position'],
		make: (at, names) => aimedPair(kitEnt('trigger_teleport', at), names('aim')) },
	// trigger_push throws who touches it so that its target is the top of the way (AimAtTarget, which frees a push
	// whose target is not above the middle of its box): a flat box, the target 256 above its middle
	{ name: 'Jump pad', about: 'a trigger_push box and the top of its jump', classes: ['trigger_push', 'target_position'],
		make: (at, names) => aimedPair(kitBox('trigger_push', at, [48, 48, 16]), names('aim')) },
	// trigger_multiple with CLIENTONLY (1) and USE_BUTTON (4) fires its target for a player who presses use in it
	// (Touch_Multi). wait: a second until it fires again, without it it fires in every frame the button is held.
	// Its target is empty: it is what the user picks.
	{ name: 'Use button', about: 'a trigger_multiple box that fires on use', classes: ['trigger_multiple'],
		make: at => [kitBox('trigger_multiple', at, [32, 32, 32], [['spawnflags', '5'], ['wait', '1'], ['target', '']])] },
	// trigger_hurt does dmg to all that is in it, with SLOW (16) once a second instead of ten times (hurt_touch)
	{ name: 'Hurt zone', about: 'a trigger_hurt box, 10 damage a second', classes: ['trigger_hurt'],
		make: at => [kitEnt('trigger_hurt', at, [['spawnflags', '16'], ['dmg', '10']])] },
	// target_speaker with looped-on (1) plays its noise as a loop from the start (SP_target_speaker). The sound is
	// one of the base game (assets0.pk3).
	{ name: 'Sound emitter', about: 'a target_speaker with a sound that loops', classes: ['target_speaker'],
		make: at => [kitEnt('target_speaker', at, [['spawnflags', '1'], ['noise', 'sound/ambience/water_med_lp.wav']])] },
	// fx_runner without a spawnflag plays its fxFile again and again, upwards (SP_fx_runner, fx_runner_link). The
	// effect is one of the base game (effects/env/small_fire.efx in assets1.pk3), and it stands on the floor.
	{ name: 'Effect emitter', about: 'an fx_runner that plays a small fire', classes: ['fx_runner'],
		make: at => [placedAt('fx_runner', [at[0], at[1], at[2] + 8], [['fxFile', 'env/small_fire']])] },
	// info_player_deathmatch: where a player starts, looking along its angle (SelectSpawnPoint)
	{ name: 'Spawn points', about: 'four info_player_deathmatch in a ring', classes: ['info_player_deathmatch'],
		make: at => [0, 90, 180, 270].map(yaw => {
			const r = yaw * Math.PI / 180;
			return kitEnt('info_player_deathmatch', kitSpot(at, Math.round(Math.cos(r)) * 96, Math.round(Math.sin(r)) * 96), [['angle', String(yaw)]]);
		}) },
	// items drop to the floor by themselves (FinishSpawningItem): in a row across the view, 48 apart
	{ name: 'Pickups', about: 'a blaster, its ammo, a medpack and a shield', classes: KIT_PICKUPS,
		make: at => {
			const f = viewAxis();
			return KIT_PICKUPS.map((cls, i) => kitEnt(cls, kitSpot(at, f[1] * (i * 48 - 72), -f[0] * (i * 48 - 72))));
		} },
	// misc_cube with the keys it is placed with: its box is 32 wide, so they lie on each other with a gap of 2 and
	// settle
	{ name: 'Cube stack', about: 'three misc_cube on top of each other', classes: ['misc_cube'],
		make: at => [18, 52, 86].map(up => placedAt('misc_cube', [at[0], at[1], at[2] + up])) },
];

const kitNamed = name => KITS.find(k => k.name.toLowerCase() === name.trim().toLowerCase());
// the kits this game module has every class of: all of them until its classes are known
function kitsOffered() {
	if (!classesLoaded) return KITS;
	const has = new Set(allClasses.map(c => c.toLowerCase()));
	return KITS.filter(k => k.classes.every(c => has.has(c.toLowerCase())));
}
