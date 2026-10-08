// ---- the load of the server: a value a second from /api/perf, the last five minutes

const perf = { seq: -1, samples: [], info: null, busy: false };
// a sample: frames, what a frame took on average and at most (microseconds), the longest wait (ms), entities, players,
// and what of a frame was this panel, on average and at most
const PERF_ROWS = [
	{ title: 'A frame of the game', of: s => s[1] / 1000, top: s => s[2] / 1000, least: 0.2, unit: ' ms',
		text: s => (s[1] / 1000).toFixed(2) + ' ms, at most ' + (s[2] / 1000).toFixed(2), bad: (s, i) => s[2] / 1000 > 1000 / i.svFps },
	{ title: 'Of it, this panel', of: s => s[6] / 1000, top: s => s[7] / 1000, least: 0.2, unit: ' ms', needs: 7,
		text: s => (s[6] / 1000).toFixed(2) + ' ms, at most ' + (s[7] / 1000).toFixed(2), bad: (s, i) => s[7] / 1000 > 500 / i.svFps },
	{ title: 'Frames in a second', of: s => s[0], least: i => i.svFps * 1.25, unit: '',
		text: (s, i) => s[0] + ' of ' + i.svFps, bad: (s, i) => s[0] < i.svFps * 0.9 },
	{ title: 'Longest wait for a frame', of: s => s[3], least: i => 2000 / i.svFps, unit: ' ms',
		text: (s, i) => s[3] + ' ms, ' + Math.round(1000 / i.svFps) + ' is the rule', bad: (s, i) => s[3] > 2000 / i.svFps },
	{ title: 'Entities', of: s => s[4], least: 64, unit: '', text: (s, i) => s[4] + ' of ' + i.entityMax, bad: (s, i) => s[4] > i.entityMax * 0.9 },
	{ title: 'Players', of: s => s[5], least: i => i.maxClients, unit: '', text: (s, i) => s[5] + ' of ' + i.maxClients, bad: () => false }
];

async function perfPoll() {
	if (mode !== 'load' || perf.busy) return;
	perf.busy = true;
	try {
		const res = await fetch('api/perf' + (perf.seq >= 0 ? '?since=' + perf.seq : ''), { cache: 'no-store' });
		if (!res.ok) { $('perfState').textContent = res.status === 404 ? 'This game module does not measure itself.' : 'HTTP ' + res.status; return; }
		const json = await res.json();
		// not the ones that follow what is here: a new map, or the view was away for too long
		if (json.from !== perf.seq) perf.samples = [];
		perf.samples.push(...json.samples);
		if (perf.samples.length > json.keep) perf.samples.splice(0, perf.samples.length - json.keep);
		perf.seq = json.seq;
		perf.info = json;
		$('perfState').textContent = '';
		perfRender();
	} catch (e) {
		$('perfState').textContent = 'No answer from the server';
	} finally {
		perf.busy = false;
	}
}
setInterval(perfPoll, 1000);

function perfRender() {
	const box = $('perfRows'), info = perf.info, all = perf.samples, last = all[all.length - 1];
	if (!box.children.length) {
		box.innerHTML = PERF_ROWS.map(r => '<div class="perf"><div class="head"><span>' + r.title + '</span><b></b></div><canvas></canvas></div>').join('');
	}
	const m = Math.floor(all.length / 60), sec = all.length % 60;
	$('perfSpan').textContent = all.length ? (m ? m + ' min ' : '') + sec + ' s' : '';
	if (!last) { $('perfState').textContent = 'The first value comes after a second.'; return; }
	PERF_ROWS.forEach((row, n) => {
		const el = box.children[n], text = el.querySelector('b'), cv = el.querySelector('canvas');
		// a value an older game module does not send
		el.style.display = row.needs !== undefined && last[row.needs] === undefined ? 'none' : '';
		if (el.style.display === 'none') return;
		text.textContent = row.text(last, info);
		text.classList.toggle('bad', row.bad(last, info));
		// drawn as wide as it is shown, for a sharp line
		const ratio = Math.min(window.devicePixelRatio || 1, 2), w = Math.round(cv.clientWidth * ratio), h = Math.round(cv.clientHeight * ratio);
		if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
		const g = cv.getContext('2d'), least = typeof row.least === 'function' ? row.least(info) : row.least;
		// The scale: what the values reach. Of the highest of a second (the grey line) the few that stand out are
		// left to run over the top, or one long frame at the start of the map would flatten five minutes.
		let most = least;
		for (const s of all) most = Math.max(most, row.of(s));
		if (row.top) {
			const tops = all.map(row.top).sort((a, b) => a - b);
			most = Math.max(most, tops[Math.floor((tops.length - 1) * 0.95)] * 1.25);
		}
		g.clearRect(0, 0, w, h);
		g.strokeStyle = '#2a2a31';
		g.lineWidth = 1;
		g.strokeRect(0.5, 0.5, w - 1, h - 1);
		// the newest value at the right, a second a step, the whole width is what the server keeps
		const x = i => w - 1 - (all.length - 1 - i) * (w - 2) / (info.keep - 1), y = v => h - 2 - Math.min(1, v / most) * (h - 4);
		const line = (of, colour) => {
			g.strokeStyle = colour;
			g.beginPath();
			all.forEach((s, i) => { if (i) g.lineTo(x(i), y(of(s))); else g.moveTo(x(i), y(of(s))); });
			g.stroke();
		};
		if (row.top) line(row.top, '#6b6b74');
		line(row.of, '#ff2f4f');
		g.fillStyle = '#8b8b94';
		g.font = Math.round(10 * ratio) + 'px sans-serif';
		g.textBaseline = 'top';
		g.fillText((most >= 10 ? Math.round(most) : most.toFixed(most < 1 ? 2 : 1)) + row.unit, 4 * ratio, 3 * ratio);
	});
}
$('logFilter').addEventListener('input', logRender);
$('logClear').addEventListener('click', () => { logView.lines = []; logRender(); });

