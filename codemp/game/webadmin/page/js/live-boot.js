// ---- the live view of a website (live.js, wa_live.c): the map of the server and what moves on it, with a camera
// that flies by itself
//
// The website has a canvas #gl and a box #labels for the names, and hands over window.VJK_LIVE:
//   base      where the questions go ('/live/' when it is not given)
//   paused    true while the view is not seen: nothing is drawn, and the server is asked once in two seconds
//   onState   called after every answer of the server with { map, players, ready, error }: players is the number
//             of those who play (no spectators), ready says that the map is drawn as the game draws it
//
// The camera goes round a spot where players are, at the height of a man and a half, and keeps in front of the
// walls. Every LIVE_STAY msec, or when the one it watches is gone, it looks for the spot where the most players are
// close together. A spot far from the last one is a cut, not a flight through the walls.

const LIVE_STAY = 15000, LIVE_NEAR = 600, LIVE_RADIUS = 320, LIVE_HEIGHT = 110, LIVE_TURN = 7, LIVE_CUT = 1500;
const liveHost = window.VJK_LIVE || {};
const live = { focus: -1, until: 0, spot: null, at: null, angle: Math.random() * 360, reach: LIVE_RADIUS, drawn: false };

// those who play: not the spectators, and not the dead for the choice of a spot
function livePlayers(alive) {
	return [...players.values()].filter(p => p.data.team !== 'spectator' && p.data.connected !== false && !(alive && p.data.dead));
}

// the one with the most others close to him, a little chance in it so the camera does not stay with one pair
function liveChoose(now) {
	const all = livePlayers(true);
	let best = null, bestScore = -1;
	for (const p of all) {
		let score = Math.random() * 0.5;
		for (const q of all) if (q !== p && Math.hypot(q.draw[0] - p.draw[0], q.draw[1] - p.draw[1], q.draw[2] - p.draw[2]) < LIVE_NEAR) score++;
		if (score > bestScore) { best = p; bestScore = score; }
	}
	live.focus = best ? best.data.id : -1;
	live.until = now + LIVE_STAY;
}

// the middle of the one watched and of those close to him, at the height of the chest
function liveSpot() {
	const p = players.get(live.focus);
	if (!p) return null;
	const sum = [0, 0, 0];
	let n = 0;
	for (const q of livePlayers(false)) {
		if (q !== p && Math.hypot(q.draw[0] - p.draw[0], q.draw[1] - p.draw[1]) > LIVE_NEAR / 2) continue;
		sum[0] += q.draw[0]; sum[1] += q.draw[1]; sum[2] += q.draw[2]; n++;
	}
	return n ? [sum[0] / n, sum[1] / n, sum[2] / n + 16] : null;
}

function liveCamera(now, dt) {
	if (!world) return;
	if (live.focus < 0 || now > live.until || !players.has(live.focus) || players.get(live.focus).data.team === 'spectator') liveChoose(now);
	const want = liveSpot();
	cam.fly = true;
	cam.follow = -1;
	if (!want) {
		// nobody plays: once round the middle of the map, from high above
		const b = world.bounds, mid = [(b.mins[0] + b.maxs[0]) / 2, (b.mins[1] + b.maxs[1]) / 2, (b.mins[2] + b.maxs[2]) / 2];
		const r = Math.max(b.maxs[0] - b.mins[0], b.maxs[1] - b.mins[1]) * 0.6;
		live.angle += LIVE_TURN * dt;
		const a = live.angle * Math.PI / 180;
		cam.pos = [mid[0] + Math.cos(a) * r, mid[1] + Math.sin(a) * r, b.maxs[2]];
		liveLookAt(mid);
		live.spot = null;
		return;
	}
	// a cut to a spot that is far, else on its way to it
	if (!live.spot || Math.hypot(want[0] - live.spot[0], want[1] - live.spot[1], want[2] - live.spot[2]) > LIVE_CUT) {
		live.spot = want.slice();
		live.reach = LIVE_RADIUS;
	} else {
		const k = 1 - Math.exp(-dt * 1.5);
		for (let i = 0; i < 3; i++) live.spot[i] += (want[i] - live.spot[i]) * k;
	}
	live.angle += LIVE_TURN * dt;
	const a = live.angle * Math.PI / 180, ideal = [live.spot[0] + Math.cos(a) * LIVE_RADIUS, live.spot[1] + Math.sin(a) * LIVE_RADIUS, live.spot[2] + LIVE_HEIGHT];
	// in front of a wall that is between the spot and where the camera would be: closer at once, back out slowly
	const tr = traceBox(live.spot, ideal, [-8, -8, -8], [8, 8, 8]);
	const reach = LIVE_RADIUS * tr.fraction;
	live.reach = reach < live.reach ? reach : live.reach + (reach - live.reach) * (1 - Math.exp(-dt * 0.8));
	const f = live.reach / LIVE_RADIUS;
	cam.pos = live.spot.map((v, i) => v + (ideal[i] - v) * f);
	liveLookAt(live.spot);
}

function liveLookAt(p) {
	const d = [p[0] - cam.pos[0], p[1] - cam.pos[1], p[2] - cam.pos[2]];
	cam.yaw = Math.atan2(d[1], d[0]) * 180 / Math.PI;
	cam.pitch = -Math.atan2(d[2], Math.hypot(d[0], d[1])) * 180 / Math.PI;
}

function liveTell(error) {
	if (typeof liveHost.onState !== 'function') return;
	try {
		liveHost.onState({
			map: state ? state.map : '',
			players: livePlayers(false).length,
			ready: !!(world && world.scripted && scripts.state === 2 && live.drawn),
			error: !!error
		});
	} catch (e) { /* the website's own business */ }
}

if (!gl) {
	liveTell(true);
} else {
	resize();
	let before = performance.now();
	const draw = now => {
		const dt = Math.min(0.1, (now - before) / 1000);
		before = now;
		frameHold = !!liveHost.paused;
		if (!frameHold) {
			liveCamera(now, dt);
			if (world && world.scripted) live.drawn = true;
		}
		requestAnimationFrame(draw);
	};
	// the camera before the frame of the panel, which draws from where it is
	requestAnimationFrame(draw);
	requestAnimationFrame(frame);
	const poll = async () => {
		const away = !!liveHost.paused || document.hidden;
		await pollState();
		liveTell($('status').className === 'err');
		pollInterval = away ? 2000 : 100;
		setTimeout(poll, pollInterval);
	};
	poll();
}
