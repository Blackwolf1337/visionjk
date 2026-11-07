// The last lines of the console over the picture (CL_ConsolePrint and Con_DrawNotify of the engine,
// client/cl_console.cpp), and what puts lines there for a client that watches: what the server prints for
// everybody ("print", the centre prints "cp" come the same way) and the line of a kill (CG_Obituary of cgame).
//
// The engine keeps its console in rows of characters with a colour each. The last four rows (NUM_CON_TIMES) are
// drawn at the top left for three seconds (con_notifytime) from when they were written, in the fixed font at
// 8 x 16 pixels of the picture (con_scale 1), not of the 640 x 480 screen. The row that is being written counts
// as one of the four, so three lines of text show at most.
// Not here: what the engine and the client game print by themselves besides the kills (a client that connects,
// a file that is missing), and what the server tells one client alone: a client that follows a player is not
// told what that player is told.

const CON_TIMES = 4, CON_NOTIFY_MSEC = 3000, CON_CHAR_WIDTH = 8, CON_CHAR_HEIGHT = 16;
// rows: the last of the console, each { chars: [[colour, character code], ...], time: when it was written or 0 }.
// The last one is the row that is being written (con.current), x where the next character goes (con.x).
const con = { rows: [{ chars: [], time: 0 }], x: 0, notifyMsec: CON_NOTIFY_MSEC };

// con.linewidth: the characters of a row, two less than fit into the width of the picture, 20 at least
const conLineWidth = () => Math.max(20, Math.floor(canvas.width / CON_CHAR_WIDTH) - 2);

// CL_ConsolePrint: a text into the console. A row is full at the line width, there is no break at a blank. A
// text that starts with a star or with [skipnotify] is not shown over the picture (the chat lines are such).
function conPrint(txt) {
	let skip = false;
	if (txt.startsWith('[skipnotify]')) { skip = true; txt = txt.slice(12); }
	if (txt[0] === '*') { skip = true; txt = txt.slice(1); }
	const now = performance.now(), width = conLineWidth();
	// Con_Linefeed
	const feed = () => {
		con.rows[con.rows.length - 1].time = skip ? 0 : now;
		con.x = 0;
		con.rows.push({ chars: [], time: 0 });
		if (con.rows.length > CON_TIMES * 2) con.rows.shift();
	};
	let color = 7;
	for (let i = 0; i < txt.length; i++) {
		const c = txt.charCodeAt(i);
		// Q_IsColorString
		if (c === 94 && i + 1 < txt.length && txt.charCodeAt(i + 1) !== 94 && hudIsDigit(txt.charCodeAt(i + 1))) { color = (txt.charCodeAt(++i) - 48) & 15; continue; }
		if (c === 10) feed();
		else if (c === 13) con.x = 0;
		else {
			if (con.x === width) feed();
			con.rows[con.rows.length - 1].chars[con.x++] = [color, c & 255];
		}
	}
	// the time of the row that is being written, for the lines over the picture
	if (skip) { if (con.rows.length > 1) con.rows[con.rows.length - 2].time = 0; }
	else con.rows[con.rows.length - 1].time = now;
}

// Con_DrawNotify: the last four rows that are not older than three seconds, from the top of the picture down.
// SCR_DrawSmallChar places a character in pixels of the picture; the 2D of the page is the 640 x 480 screen.
function conDrawNotify() {
	const now = performance.now(), xadjust = 640 / canvas.width, yadjust = 480 / canvas.height;
	let v = 0, color = 7;
	hudSetColor(HUD_COLOR_TABLE[color]);
	for (let i = Math.max(0, con.rows.length - CON_TIMES); i < con.rows.length; i++) {
		const row = con.rows[i];
		if (!row.time || now - row.time > con.notifyMsec) continue;
		for (let x = 0; x < row.chars.length; x++) {
			const ch = row.chars[x];
			if (!ch || ch[1] === 32) continue;
			if (ch[0] !== color) { color = ch[0]; hudSetColor(HUD_COLOR_TABLE[color] || [0, 0, 0, 0]); }
			hudDrawChar((x + 1) * CON_CHAR_WIDTH * xadjust, v * yadjust, CON_CHAR_WIDTH * xadjust, CON_CHAR_HEIGHT * yadjust, ch[1]);
		}
		v += CON_CHAR_HEIGHT;
	}
	hudSetColor(null);
}

// ---- what the server tells everybody

// CG_CheckSVStringEdRef: a text of the server with its references to the string package of the server texts,
// @@@NAME up to a blank, a colon, a full stop or the end of a line. Three at signs at the very end are no
// reference.
function hudSvString(str) {
	return str.replace(/@@@(?=[\s\S])@*([^ :.\n]*)/g, (all, ref) => hudString('MP_SVGAME', ref));
}

let prSeq = -1;		// the number of the last print the page has, -1 until the server said where it is
const prHeard = new Map();
const prQuery = () => '&prs=' + prSeq;

// "print" and "cp" of the server for all clients, as CG_Print_f and CG_CenterPrint_f take them. A text whose
// string package is not read yet waits for it.
function takePrints(s) {
	if (s.prSeq === undefined) return;
	for (const p of s.pr || []) {
		// known by its number and the time of the map it was sent at, as an event is (takeEvents)
		const key = p[0] + ' ' + (s.time - p[1]);
		if (prHeard.has(key)) continue;
		prHeard.set(key, s.time - p[1]);
		const kind = p[2], text = String(p[3]), show = () => { const t = hudSvString(text); if (kind === 1) hudCenterPrint(t, 480 * 0.30); else conPrint(t); };
		if (text.includes('@@@') && !(hudStrings.get('MP_SVGAME') instanceof Map)) hudStringsRead('MP_SVGAME').then(show); else show();
	}
	heardForget(prHeard, s.time, 10000);
	prSeq = s.prSeq;
}

// ---- the line of a kill

let hudMods = null;		// the names of the means of death by their numbers, there once the list is read
const hudGenders = new Map();	// model and skin -> a promise of 'f' or 'm'

// The gender of the model of a player, as cgame takes it from the sound file of the model (CG_LoadCISounds): an f
// in what is after the last line break of the file makes it female, everything else male.
function hudGender(d) {
	const name = d.model || 'kyle', slash = name.indexOf('/'), model = (slash < 0 ? name : name.slice(0, slash)) || 'kyle', skin = slash >= 0 ? name.slice(slash + 1) : '';
	const key = model + '/' + skin;
	if (!hudGenders.has(key)) {
		const dir = 'models/players/' + model + '/', plain = !skin || skin.toLowerCase() === 'default';
		const read = path => fetchGameFile(path, true).then(text => (typeof text === 'string' ? text : null), () => null);
		hudGenders.set(key, read(dir + (plain ? 'sounds.cfg' : 'sounds_' + skin + '.cfg')).then(text => (text !== null ? text : read(dir + (plain ? 'sounds_default.cfg' : 'sounds.cfg')))).then(text => {
			let female = false;
			for (let i = (text || '').length; text && i >= 0 && text[i] !== '\n'; i--) if (text[i] === 'f') female = true;
			return female ? 'f' : 'm';
		}));
	}
	return hudGenders.get(key);
}

// The name of the text of CG_Obituary for a death (MP_INGAME), by the means of death and who did it.
// self: he did it himself, world: nobody did.
const HUD_MOD_SHOT = ['MOD_BRYAR_PISTOL', 'MOD_BRYAR_PISTOL_ALT', 'MOD_BLASTER', 'MOD_TURBLAST', 'MOD_DISRUPTOR', 'MOD_DISRUPTOR_SPLASH', 'MOD_DISRUPTOR_SNIPER', 'MOD_BOWCASTER', 'MOD_REPEATER', 'MOD_REPEATER_ALT', 'MOD_FLECHETTE'];
const HUD_MOD_EXPLOSIVES = ['MOD_REPEATER_ALT_SPLASH', 'MOD_FLECHETTE_ALT_SPLASH', 'MOD_ROCKET', 'MOD_ROCKET_SPLASH', 'MOD_ROCKET_HOMING', 'MOD_ROCKET_HOMING_SPLASH', 'MOD_THERMAL', 'MOD_THERMAL_SPLASH', 'MOD_TRIP_MINE_SPLASH',
	'MOD_TIMED_MINE_SPLASH', 'MOD_DET_PACK_SPLASH', 'MOD_VEHICLE', 'MOD_CONC', 'MOD_CONC_ALT'];
const HUD_MOD_KILLED = { MOD_STUN_BATON: 'KILLED_STUN', MOD_MELEE: 'KILLED_MELEE', MOD_SABER: 'KILLED_SABER', MOD_BRYAR_PISTOL: 'KILLED_BRYAR', MOD_BRYAR_PISTOL_ALT: 'KILLED_BRYAR', MOD_BLASTER: 'KILLED_BLASTER', MOD_TURBLAST: 'KILLED_BLASTER',
	MOD_DISRUPTOR: 'KILLED_DISRUPTOR', MOD_DISRUPTOR_SPLASH: 'KILLED_DISRUPTOR', MOD_DISRUPTOR_SNIPER: 'KILLED_DISRUPTORSNIPE', MOD_BOWCASTER: 'KILLED_BOWCASTER', MOD_REPEATER: 'KILLED_REPEATER', MOD_REPEATER_ALT: 'KILLED_REPEATERALT',
	MOD_REPEATER_ALT_SPLASH: 'KILLED_REPEATERALT', MOD_DEMP2: 'KILLED_DEMP2', MOD_DEMP2_ALT: 'KILLED_DEMP2', MOD_FLECHETTE: 'KILLED_FLECHETTE', MOD_FLECHETTE_ALT_SPLASH: 'KILLED_FLECHETTE_MINE', MOD_ROCKET: 'KILLED_ROCKET',
	MOD_ROCKET_SPLASH: 'KILLED_ROCKET', MOD_ROCKET_HOMING: 'KILLED_ROCKET_HOMING', MOD_ROCKET_HOMING_SPLASH: 'KILLED_ROCKET_HOMING', MOD_THERMAL: 'KILLED_THERMAL', MOD_THERMAL_SPLASH: 'KILLED_THERMAL', MOD_TRIP_MINE_SPLASH: 'KILLED_TRIPMINE',
	MOD_TIMED_MINE_SPLASH: 'KILLED_TRIPMINE_TIMED', MOD_DET_PACK_SPLASH: 'KILLED_DETPACK', MOD_VEHICLE: 'KILLED_GENERIC', MOD_CONC: 'KILLED_GENERIC', MOD_CONC_ALT: 'KILLED_GENERIC', MOD_FORCE_DARK: 'KILLED_DARKFORCE', MOD_SENTRY: 'KILLED_SENTRY',
	MOD_TELEFRAG: 'KILLED_TELEFRAG', MOD_CRUSH: 'KILLED_GENERIC', MOD_FALLING: 'KILLED_FORCETOSS', MOD_TRIGGER_HURT: 'KILLED_GENERIC' };
function hudObituaryText(mod, self, world, gender) {
	const suffix = gender === 'f' ? '_FEMALE' : gender === 'n' ? '_GENDERLESS' : '_MALE';
	// the deaths that have one text whoever did it
	let message = ['MOD_SUICIDE', 'MOD_FALLING', 'MOD_CRUSH', 'MOD_WATER', 'MOD_SLIME', 'MOD_LAVA', 'MOD_TRIGGER_HURT'].includes(mod) ? 'DIED_GENERIC' : mod === 'MOD_TARGET_LASER' ? 'DIED_LASER' : null;
	if (self) {
		message = 'SUICIDE_' + (HUD_MOD_SHOT.includes(mod) ? 'SHOT' : HUD_MOD_EXPLOSIVES.includes(mod) ? 'EXPLOSIVES' : mod === 'MOD_DEMP2' ? 'ELECTROCUTED' : mod === 'MOD_FALLING' ? 'FALLDEATH' : 'GENERICDEATH') + suffix;
	}
	if (!self && !world) return { killed: HUD_MOD_KILLED[mod] || 'KILLED_GENERIC' };
	return { died: message || 'DIED_GENERIC' };
}

// CG_Obituary, the line it prints: "<target> <text>" for a death by himself or by the world, "<target> <text>
// <attacker>" for a kill. e: the event (other: the target, other2: the attacker, parm: the means of death).
function hudObituaryPrint(e) {
	const target = e.other, t = target >= 0 && target < 32 ? hudClientInfo(target) : null;
	if (!t) return;
	const a = e.other2 >= 0 && e.other2 < 32 ? hudClientInfo(e.other2) : null, self = e.other2 === target;
	if (!hudMods) {
		// the first one waits for the names of the means of death
		hudMods = fetch('api/list?kind=mods').then(r => r.json()).then(j => j.names || [], () => []);
	}
	const targetName = t.name + '^7', attackerName = a ? a.name + '^7' : 'noname^7';
	Promise.all([hudMods, hudGender(t), hudStringsRead('MP_INGAME')]).then(([names, gender]) => {
		const text = hudObituaryText(names[e.parm] || '', self, !a, gender);
		if (text.killed) conPrint(targetName + ' ' + hudString('MP_INGAME', text.killed) + ' ' + attackerName + '\n');
		else conPrint(targetName + ' ' + hudString('MP_INGAME', text.died) + '\n');
	});
}
