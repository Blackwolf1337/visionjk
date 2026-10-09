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

// The projection of the game, for every camera of the page: R_SetupProjection with what CG_CalcFov hands it.
// fovX is the angle across (cg_fov, 80) whatever the shape of the picture (cg_fovAspectAdjust is 0), the angle
// up and down follows from that shape, the near plane is r_znear and the far plane the one of SetFarClip.
// The far plane is worked out for where the eye is: eye is set before this is called.
const ZNEAR = 4;
const gameFovY = (w, h, fovX = 80) => 2 * Math.atan2(h, w / Math.tan(fovX * Math.PI / 360));
function gameProjection(w, h, fovX = 80, far = farClip()) {
	return M4.perspective(gameFovY(w, h, fovX), w / h, ZNEAR, far);
}

// The eye is where an eye of the game can be: the client the page is compared with, the fly camera, the eyes of
// a player. Around the map from above it is not, and what the game leaves out for being far away is left in
// there, as the fog is: it would take the map away that this view is there to show.
const camInMap = () => !!refCam || cam.fly || view.id >= 0;

// The far plane (SetFarClip in tr_main.cpp): as far as the farthest corner of the box around what the view draws
// of the map, at least 2048, and at most the distanceCull of the worldspawn times 1.732. The engine leaves no
// surface out for being far away, the map is cut where this plane is. Its box is the one around the leafs it
// draws; the page draws all of the map and takes the box around all of it. That is never nearer than the plane
// of the engine, and the same plane wherever distanceCull cuts the map.
function farClip(box = world && world.bounds, at = eye, cut = camInMap()) {
	if (!box) return 2048;
	let far = 0;
	for (let i = 0; i < 8; i++) {
		const x = (i & 1 ? box.mins : box.maxs)[0] - at[0], y = (i & 2 ? box.mins : box.maxs)[1] - at[1], z = (i & 4 ? box.mins : box.maxs)[2] - at[2];
		far = Math.max(far, x * x + y * y + z * z);
	}
	far = Math.sqrt(far);
	const most = mapEnts().distanceCull * 1.732;
	// Com_Clamp( 2048, most, far ), in its order
	return far < 2048 ? 2048 : cut && far > most ? most : far;
}

// A view from another place: the matrix of a view (projection times view) for an eye that is by d further on.
function viewMoved(vp, d, out) {
	out.set(vp);
	for (let r = 0; r < 4; r++) out[12 + r] = vp[12 + r] - vp[r] * d[0] - vp[4 + r] * d[1] - vp[8 + r] * d[2];
	return out;
}

// The same view with another far plane. Only the row of a projection that makes the depth has the far plane in
// it (R_SetupProjection), and the row below it is the depth before the far plane comes into it.
function viewWithFar(vp, far, out) {
	const a = (far + ZNEAR) / (ZNEAR - far), b = 2 * far * ZNEAR / (ZNEAR - far);
	out.set(vp);
	out[2] = -a * vp[3]; out[6] = -a * vp[7]; out[10] = -a * vp[11]; out[14] = b - a * vp[15];
	return out;
}

// The camera of the comparison with the game: the eye and the angles of a client, as /api/state?view= tells
// them (origin, pitch yaw roll, viewheight, pm_type, time of the level). null: the camera of the page again.
let refCam = null;
function refView(v) {
	refCam = v ? { eye: [v[0], v[1], v[2] + v[6]], pitch: v[3], yaw: v[4], time: v[8] } : null;
}

function updateCamera() {
	let at;
	if (refCam) {
		// the way a client looks: AngleVectors, a pitch above zero looks down
		const p = refCam.pitch * Math.PI / 180, y = refCam.yaw * Math.PI / 180, d = [Math.cos(p) * Math.cos(y), Math.cos(p) * Math.sin(y), -Math.sin(p)];
		eye = refCam.eye.slice();
		at = eye.map((v, i) => v + d[i]);
	} else {
		// following a player is done around him
		if (cam.fly && cam.follow >= 0) setFly(false);
		if (cam.fly) {
			const d = camDir();
			eye = cam.pos.slice();
			at = eye.map((v, i) => v + d[i]);
		} else {
			if (cam.follow >= 0) {
				const p = players.get(cam.follow);
				if (p) cam.target = p.draw.slice();
			}
			const y = cam.yaw * Math.PI / 180, pt = cam.pitch * Math.PI / 180;
			eye = [cam.target[0] - Math.cos(y) * Math.cos(pt) * cam.dist, cam.target[1] - Math.sin(y) * Math.cos(pt) * cam.dist, cam.target[2] + Math.sin(pt) * cam.dist];
			at = cam.target;
		}
	}
	viewProj = M4.mul(gameProjection(canvas.width, canvas.height), M4.lookAt(eye, at, [0, 0, 1]));
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

