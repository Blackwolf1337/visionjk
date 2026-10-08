/* ------------------------------------------------------------------ */
/* camera (RTS style, Z up like the game)                              */
/* ------------------------------------------------------------------ */

// Two ways to look at the map. Around a point on it, from above (target, dist), as JKAdmin has it. Or flying:
// the camera is at pos and looks along yaw and pitch, which is how the inside of a map is looked at. A pitch
// above zero looks down in both.
const cam = { target: [0, 0, 0], yaw: 45, pitch: 55, dist: 1500, follow: -1, fly: false, pos: [0, 0, 0], flySpeed: 600 };

function camDir() {
	const y = cam.yaw * Math.PI / 180, p = cam.pitch * Math.PI / 180;
	return [Math.cos(y) * Math.cos(p), Math.sin(y) * Math.cos(p), -Math.sin(p)];
}

// into flying from where the camera is, and out of it to the point it looks at
function setFly(on) {
	if (on === cam.fly) return;
	if (on) {
		cam.pos = eye.slice();
		cam.follow = -1;
	} else {
		cam.pitch = Math.min(89, Math.max(10, cam.pitch));
		const d = camDir();
		cam.target = cam.pos.map((v, i) => v + d[i] * cam.dist);
	}
	cam.fly = on;
	$('flyBtn').classList.toggle('armedPlace', on);
}
let viewProj = null, invViewProj = null, eye = [0, 0, 0];

// Up to nine views of a map, kept by the browser: Shift and a digit stores where the camera is, the digit alone
// goes back there.
function camBookmark(n, store) {
	const key = 'views:' + loadedMap;
	let all = {};
	try { all = JSON.parse(localStorage.getItem(key) || '{}'); } catch (e) { /* none then */ }
	if (store) {
		all[n] = cam.fly ? { fly: true, pos: cam.pos, yaw: cam.yaw, pitch: cam.pitch } : { fly: false, target: cam.target, dist: cam.dist, yaw: cam.yaw, pitch: cam.pitch };
		try { localStorage.setItem(key, JSON.stringify(all)); toast('View ' + n + ' stored'); } catch (e) { toast('The browser does not keep it', false); }
		return;
	}
	const v = all[n];
	if (!v) { toast('No view under ' + n + ', Shift+' + n + ' stores one', false); return; }
	cam.follow = -1;
	setFly(!!v.fly);
	if (v.fly) cam.pos = v.pos.slice(); else { cam.target = v.target.slice(); cam.dist = v.dist; }
	cam.yaw = v.yaw;
	cam.pitch = v.pitch;
}

function resetCamera() {
	if (!world) return;
	setFly(false);
	const b = world.bounds;
	cam.target = [(b.mins[0] + b.maxs[0]) / 2, (b.mins[1] + b.maxs[1]) / 2, (b.mins[2] + b.maxs[2]) / 2];
	cam.dist = Math.max(b.maxs[0] - b.mins[0], b.maxs[1] - b.mins[1]) * 0.75;
}

function updateCamera() {
	// following a player is done around him
	if (cam.fly && cam.follow >= 0) setFly(false);
	if (cam.fly) {
		const d = camDir();
		eye = cam.pos.slice();
		viewProj = M4.mul(M4.perspective(55 * Math.PI / 180, canvas.width / canvas.height, 4, 65536), M4.lookAt(eye, eye.map((v, i) => v + d[i]), [0, 0, 1]));
		invViewProj = M4.invert(viewProj);
		return;
	}
	if (cam.follow >= 0) {
		const p = players.get(cam.follow);
		if (p) cam.target = p.draw.slice();
	}
	const y = cam.yaw * Math.PI / 180, pt = cam.pitch * Math.PI / 180;
	eye = [cam.target[0] - Math.cos(y) * Math.cos(pt) * cam.dist, cam.target[1] - Math.sin(y) * Math.cos(pt) * cam.dist, cam.target[2] + Math.sin(pt) * cam.dist];
	const proj = M4.perspective(55 * Math.PI / 180, canvas.width / canvas.height, 8, 65536);
	viewProj = M4.mul(proj, M4.lookAt(eye, cam.target, [0, 0, 1]));
	invViewProj = M4.invert(viewProj);
}

function screenRay(x, y) {
	const nx = x / canvas.clientWidth * 2 - 1, ny = 1 - y / canvas.clientHeight * 2;
	const a = M4.transform(invViewProj, [nx, ny, -1]), b = M4.transform(invViewProj, [nx, ny, 1]);
	const p0 = [a[0] / a[3], a[1] / a[3], a[2] / a[3]], p1 = [b[0] / b[3], b[1] / b[3], b[2] / b[3]];
	const d = [p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]], l = Math.hypot(...d);
	return { origin: p0, dir: d.map(v => v / l) };
}

function project(p) {
	const c = M4.transform(viewProj, p);
	if (c[3] <= 0) return null;
	return [(c[0] / c[3] * 0.5 + 0.5) * canvas.clientWidth, (0.5 - c[1] / c[3] * 0.5) * canvas.clientHeight];
}

