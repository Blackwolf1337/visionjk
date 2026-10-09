// ---- taking control of a player: /api/control. The page steers him as his client would, with the keys and the
// mouse from behind him or through his eyes, or sends him to a spot on the map.

// the keys and the mouse buttons that steer him, and what each of them stands for
const CONTROL_KEYS = { w: 'fwd', s: 'back', a: 'left', d: 'right', ' ': 'up', c: 'down', control: 'down', shift: 'walk', e: 'use', f: 'force', mouse0: 'attack', mouse2: 'alt' };
// the buttons of a user command: BUTTON_ATTACK, BUTTON_WALKING, BUTTON_USE, BUTTON_ALT_ATTACK, BUTTON_FORCEPOWER
const CONTROL_BUTTONS = { attack: 1, walk: 16, use: 32, alt: 128, force: 512 };
const CONTROL_TURN = 0.12;		// degrees a step of the mouse turns him by
const CONTROL_DIST = 120;		// how far behind him the camera of the third person starts
// The server lets him go after 5 seconds without a request. The Map view sends nothing by itself, so it says
// again this often where he is to be.
const CONTROL_ALIVE = 2000;

// id: who is controlled, -1 for nobody. view: from where he is looked at, 'map', 'third' or 'first'. yaw, pitch: the
// way he is turned, as the game has its view angles (a pitch below zero looks up). held: the keys that are down.
// tap: what was pressed since the last request, so that a key that was let go before it went is still in it.
// goal: where the Map view sent him last. cam: the camera of the map as it was before the third person took it.
const control = { id: -1, view: 'map', yaw: 0, pitch: 0, held: new Set(), tap: new Set(), busy: false, changed: false, starting: false, goal: null, asked: 0, cam: null };

const controlWrap = a => ((a + 180) % 360 + 360) % 360 - 180;

// A request. The answer is { ok, message, id }, id being who the server holds now, -1 for nobody.
// controlHolds: whether an answer says that the server still holds him (one that does not say counts as that).
const controlHolds = (json, id) => typeof json.id !== 'number' || json.id === id;
async function controlPost(params) {
	control.asked = performance.now();
	try {
		const res = await fetch('api/control', { method: 'POST', body: new URLSearchParams(params) });
		if (res.status === 404) return { ok: false, message: 'This game module can not control a player.', id: -1 };
		return await res.json();
	} catch (e) {
		return { ok: false, message: 'Request failed: ' + e.message };
	}
}

async function controlStart(id) {
	if (control.starting || control.id >= 0) return;
	control.starting = true;
	const json = await controlPost({ op: 'start', id });
	control.starting = false;
	toast(json.message, json.ok);
	if (!json.ok) return;
	// (another one may be selected by now: the next frame gives him back then)
	control.id = id;
	control.view = 'map';
	control.goal = null;
	controlTakeAngles();
	controlRender();
}

// He is given back. tell: the server is told, which it is not when it said itself that it does not hold him (it
// may hold one for another page by then, and letting go names nobody).
function controlStop(tell = true) {
	if (control.id < 0) return;
	control.held.clear();
	control.tap.clear();
	controlSetView('map');
	control.id = -1;
	control.goal = null;
	if (tell) controlPost({ op: 'stop' });
	controlRender();
}

// the way he looks now is where the mouse turns him on from
function controlTakeAngles() {
	const p = players.get(control.id);
	if (!p) return;
	control.yaw = controlWrap(p.data.yaw || 0);
	control.pitch = Math.max(-89, Math.min(89, controlWrap(p.data.pitch || 0)));
}

// From where he is looked at. A view that is left puts back what it took: the camera of the map as it was before
// the third person, the view of the map after the one through his eyes.
function controlSetView(v) {
	if (control.id < 0 || v === control.view) return;
	const was = control.view;
	// (while the view is still the one with the keys: he stops)
	controlRelease();
	control.view = v;
	if (was === 'third') controlCamBack();
	if (was === 'first' && view.id === control.id) setFirstPerson(-1);
	if (v === 'map') {
		if (document.pointerLockElement === canvas) document.exitPointerLock();
		// he stays where he is until a right-click sends him somewhere
		control.goal = null;
	} else {
		// The keys are his now: what the camera of the map had of them is let go, and no button keeps the focus
		// for Space to press it.
		keys.clear();
		if (document.activeElement && document.activeElement !== document.body) document.activeElement.blur();
		if (was === 'map') controlTakeAngles();
		if (v === 'third') controlCamTake(); else setFirstPerson(control.id);
	}
	controlRender();
}

function controlCamTake() {
	control.cam = { fly: cam.fly, target: cam.target.slice(), pos: cam.pos.slice(), yaw: cam.yaw, pitch: cam.pitch, dist: cam.dist, follow: cam.follow };
	setFly(false);
	cam.follow = -1;
	cam.dist = CONTROL_DIST;
}
function controlCamBack() {
	const c = control.cam;
	control.cam = null;
	if (!c) return;
	setFly(c.fly);
	Object.assign(cam, { target: c.target, pos: c.pos, yaw: c.yaw, pitch: c.pitch, dist: c.dist, follow: c.follow });
}

// Every frame, before the camera is set (js/frame.js). He is given back when another one is selected or he is gone.
// The view follows what the key V and the button Exit do with the view through his eyes. The camera of the third
// person sits behind him: around his head, turned the way he is, as far away as the wheel has set it.
function controlFrame() {
	if (control.id < 0) return;
	const p = players.get(control.id);
	if (!p || selected !== control.id) { controlStop(!!p); return; }
	const eyes = view.id === control.id;
	if (eyes !== (control.view === 'first')) controlSetView(eyes ? 'first' : 'map');
	if (control.view !== 'third') return;
	if (cam.fly) setFly(false);
	cam.follow = -1;
	cam.target = [p.draw[0], p.draw[1], p.draw[2] + (p.data.viewheight ?? 36)];
	cam.yaw = control.yaw;
	cam.pitch = control.pitch;
}

function controlRender() {
	const on = control.id >= 0, p = on && players.get(control.id);
	$('controlBar').hidden = !on;
	$('controlBtn').classList.toggle('primary', on);
	if (!on) return;
	$('controlName').innerHTML = p ? colorName(p.data.name) : '';
	document.querySelectorAll('#controlBar [data-view]').forEach(b => b.classList.toggle('primary', b.dataset.view === control.view));
	$('controlHint').textContent = control.view === 'map' ? 'A right-click on the map: he walks there' :
		document.pointerLockElement === canvas ? 'Esc frees the mouse' : 'Click the map: the mouse turns him';
}

// ---- the Map view: a right-click sends him to a spot

// He walks to where the last right-click sent him. Without one he is to be where he stands, which is what the
// request says that only keeps him. tell: the answer is shown.
async function controlWalk(tell) {
	const id = control.id, p = players.get(id);
	const at = control.goal || (p && [p.data.pos[0], p.data.pos[1], p.data.pos[2] - 24 * (p.data.scale || 100) / 100]);
	if (!at) return;
	const json = await controlPost({ op: 'goal', id, x: at[0].toFixed(1), y: at[1].toFixed(1), z: at[2].toFixed(1) });
	if (control.id !== id) return;
	// the server holds another one or nobody: he is not controlled from here any more
	if (!controlHolds(json, id)) { toast(json.message, false); controlStop(false); }
	else if (tell) toast(json.message, json.ok);
}
// a right-click on the map at hit (js/input.js). false: nobody is controlled, the click is the teleport
function controlGoal(hit) {
	if (control.id < 0) return false;
	control.goal = hit.slice();
	controlWalk(true);
	return true;
}

// ---- the two views with the keys: what is held goes to the server twenty times a second, and at once when it
// changes. The server holds an input for half a second. One request at a time.

async function controlSend() {
	if (control.id < 0 || control.view === 'map' || control.busy) return;
	const id = control.id, on = act => control.tap.has(act) || [...control.held].some(k => CONTROL_KEYS[k] === act);
	const axis = (plus, minus) => (on(plus) ? 127 : 0) - (on(minus) ? 127 : 0);
	let buttons = 0;
	for (const act in CONTROL_BUTTONS) if (on(act)) buttons |= CONTROL_BUTTONS[act];
	const params = { op: 'input', id, f: axis('fwd', 'back'), r: axis('right', 'left'), u: axis('up', 'down'), buttons, yaw: control.yaw.toFixed(2), pitch: control.pitch.toFixed(2) };
	control.tap.clear();
	control.changed = false;
	control.busy = true;
	const json = await controlPost(params);
	control.busy = false;
	// (given back while the request was on its way)
	if (control.id !== id) return;
	if (!json.ok) { toast(json.message, false); controlStop(controlHolds(json, id)); return; }
	// a key changed meanwhile
	if (control.changed) controlSend();
}
function controlChanged() {
	control.changed = true;
	controlSend();
}
// nothing is held any more: the keys may be let go where the page does not see it
function controlRelease() {
	if (!control.held.size && !control.tap.size) return;
	control.held.clear();
	control.tap.clear();
	controlChanged();
}
setInterval(() => {
	if (control.id < 0) return;
	if (control.view !== 'map') controlSend();
	else if (performance.now() - control.asked > CONTROL_ALIVE) controlWalk(false);
}, 50);

// The keys, before the camera and the shortcuts of js/input.js see them: in the two views with the keys the ones
// that steer him are his alone. Esc gives him back in every view (while the mouse is taken the browser frees it
// with the first Esc and the page gets the second one).
function controlKey(e, down) {
	if (control.id < 0) return;
	if (e.key === 'Escape') {
		if (down) { e.stopImmediatePropagation(); controlStop(); }
		return;
	}
	const k = e.key.toLowerCase(), act = CONTROL_KEYS[k];
	if (!act || control.view === 'map') return;
	if (!down) {
		if (!control.held.delete(k)) return;
		e.stopImmediatePropagation();
		e.preventDefault();
		controlChanged();
		return;
	}
	// not what is typed into a field, and not the shortcuts of the browser with Alt or the command key
	if (e.altKey || e.metaKey || /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
	e.stopImmediatePropagation();
	e.preventDefault();
	// (a key that is held comes again and again)
	if (control.held.has(k)) return;
	control.held.add(k);
	control.tap.add(act);
	controlChanged();
}
window.addEventListener('keydown', e => controlKey(e, true), true);
window.addEventListener('keyup', e => controlKey(e, false), true);

// The mouse on the map in those two views: the first click takes it (pointer lock), from then on it turns him and
// its buttons attack. The handlers of js/input.js do not get these clicks.
window.addEventListener('mousedown', e => {
	if (control.id < 0 || control.view === 'map' || e.target !== canvas) return;
	e.stopImmediatePropagation();
	e.preventDefault();
	if (document.pointerLockElement !== canvas) {
		if (document.activeElement && document.activeElement !== document.body) document.activeElement.blur();
		// (a browser that answers with a promise rejects it when it does not give the mouse, which is nothing to tell)
		const asked = canvas.requestPointerLock();
		if (asked && asked.catch) asked.catch(() => {});
		return;
	}
	const k = 'mouse' + e.button;
	if (!CONTROL_KEYS[k] || control.held.has(k)) return;
	control.held.add(k);
	control.tap.add(CONTROL_KEYS[k]);
	controlChanged();
}, true);
window.addEventListener('mouseup', e => { if (control.held.delete('mouse' + e.button)) controlChanged(); }, true);
// to the left is a higher yaw, down a higher pitch
document.addEventListener('mousemove', e => {
	if (control.id < 0 || control.view === 'map' || document.pointerLockElement !== canvas) return;
	control.yaw = controlWrap(control.yaw - e.movementX * CONTROL_TURN);
	control.pitch = Math.max(-89, Math.min(89, control.pitch + e.movementY * CONTROL_TURN));
});
document.addEventListener('pointerlockchange', () => {
	if (document.pointerLockElement !== canvas) controlRelease();
	controlRender();
});
window.addEventListener('blur', controlRelease);
// Ctrl is one of the keys to crouch, and Ctrl with W closes the tab of the browser, which no page can stop: while
// Ctrl is held for him the browser asks before it goes.
window.addEventListener('beforeunload', e => {
	if (control.id < 0 || !control.held.has('control')) return;
	e.preventDefault();
	controlRelease();
});

$('controlBtn').addEventListener('click', () => {
	disarm();
	if (control.id >= 0) controlStop(); else if (selected >= 0) controlStart(selected);
});
document.querySelectorAll('#controlBar [data-view]').forEach(b => b.addEventListener('click', () => controlSetView(b.dataset.view)));
$('controlEnd').addEventListener('click', () => controlStop());
