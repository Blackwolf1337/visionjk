// ---- the log of the game and the console of the server: /api/log and /api/console give their text from a place on,
// the view asks on from where the last answer ended. A line typed under it goes to /api/command.

// The two sources, each with its own lines, its place in the text and the line that has not ended yet. first: nothing
// of it was shown yet, again: it was asked for while a request was on its way.
const logSource = (api, none) => ({ api, none, next: -1, lines: [], partial: '', first: true, busy: false, again: false, state: '' });
const LOG_SOURCES = {
	log: logSource('api/log', 'This game module has no log view.'),
	console: logSource('api/console', 'This game module does not show its console.')
};
// the one that is shown: the filter, "follow the end" and "Clear view" (their listeners are in js/load.js) work on it
let logView = LOG_SOURCES.log;
const LOG_KEEP = 4000;	// lines the view holds of each

function logRender() {
	const f = $('logFilter').value.trim().toLowerCase(), box = $('logText');
	const atEnd = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
	box.textContent = (f ? logView.lines.filter(l => l.toLowerCase().includes(f)) : logView.lines).join('\n');
	$('logSize').textContent = logView.lines.length + ' lines';
	if ($('logFollow').checked && (atEnd || logView.first)) box.scrollTop = box.scrollHeight;
}

function logShow(name) {
	logView = LOG_SOURCES[name];
	document.querySelectorAll('#logSource button').forEach(b => b.classList.toggle('on', b.dataset.log === name));
	$('logState').textContent = logView.state;
	logRender();
	// its end, wherever the other one was looked at
	if ($('logFollow').checked) $('logText').scrollTop = $('logText').scrollHeight;
	logPoll();
}

async function logPoll() {
	const v = logView;
	if (mode !== 'log') return;
	if (v.busy) { v.again = true; return; }
	v.busy = true;
	const tell = text => { v.state = text; if (v === logView) $('logState').textContent = text; };
	try {
		const res = await fetch(v.api + (v.next >= 0 ? '?from=' + v.next : ''), { cache: 'no-store' });
		if (!res.ok) { tell(res.status === 404 ? v.none : 'HTTP ' + res.status); return; }
		const json = await res.json();
		// a new file, or the view was behind its end: what is shown is of the old one
		if (v.next >= 0 && json.from !== v.next) { v.lines = []; v.partial = ''; v.first = true; }
		let text = v.partial + json.text;
		// a view that starts in the middle of the text starts in the middle of a line
		if (v.first && json.from > 0) text = text.slice(text.indexOf('\n') + 1);
		const lines = text.split('\n');
		v.partial = lines.pop();
		const before = v.lines.length;
		v.lines.push(...lines.map(l => l.replace(/\r$/, '').replace(/\^[0-9]/g, '')));
		if (v.lines.length > LOG_KEEP) v.lines.splice(0, v.lines.length - LOG_KEEP);
		v.next = json.next;
		tell('');
		if (v === logView && (lines.length || v.first)) logRender();
		v.first = false;
		// more of it is there already
		if (json.next < json.size && lines.length + before > before) v.again = true;
	} catch (e) {
		tell('Request failed: ' + e.message);
	} finally {
		v.busy = false;
		if (v.again) { v.again = false; setTimeout(logPoll, 50); }
	}
}
setInterval(logPoll, 2000);

document.querySelectorAll('#logSource button').forEach(b => b.addEventListener('click', () => logShow(b.dataset.log)));

// ---- a line for the console of the server, as if it was typed there. The arrow keys walk the lines that were
// sent before, which the browser keeps.

const LOG_HISTORY = 50, LOG_HISTORY_KEY = 'wa.console';
// logBack: how far back in them the field is, -1 for the line that is being typed (logTyped)
let logHistory = [], logBack = -1, logTyped = '';
try { logHistory = JSON.parse(localStorage.getItem(LOG_HISTORY_KEY) || '[]'); } catch (e) { /* none then */ }
if (!Array.isArray(logHistory)) logHistory = [];

function logRemember(cmd) {
	if (logHistory[logHistory.length - 1] !== cmd) logHistory.push(cmd);
	if (logHistory.length > LOG_HISTORY) logHistory.splice(0, logHistory.length - LOG_HISTORY);
	logBack = -1;
	// a line that sets a password is walked back to until the page is loaded again, the browser does not keep it
	try { localStorage.setItem(LOG_HISTORY_KEY, JSON.stringify(logHistory.filter(l => !/password/i.test(l)))); } catch (e) { /* not kept */ }
}

$('logCmd').addEventListener('submit', async e => {
	e.preventDefault();
	const cmd = $('logCmdText').value.trim();
	if (!cmd) return;
	logRemember(cmd);
	$('logCmdText').value = '';
	// where the answer is printed
	logShow('console');
	let json;
	try {
		const res = await fetch('api/command', { method: 'POST', body: new URLSearchParams({ cmd }) });
		json = res.status === 404 ? { ok: false, message: 'This game module does not take console commands.' } : await res.json();
	} catch (err) {
		json = { ok: false, message: 'Request failed: ' + err.message };
	}
	if (!json.ok) { toast(json.message, false); return; }
	// The command runs in the next frame of the server: what the console has now, and again when its answer is in it.
	logPoll();
	setTimeout(logPoll, 250);
});
$('logCmdText').addEventListener('keydown', e => {
	if (e.key !== 'ArrowUp' && e.key !== 'ArrowDown') return;
	e.preventDefault();
	if (logBack < 0) logTyped = $('logCmdText').value;
	logBack = Math.max(-1, Math.min(logHistory.length - 1, logBack + (e.key === 'ArrowUp' ? 1 : -1)));
	$('logCmdText').value = logBack < 0 ? logTyped : logHistory[logHistory.length - 1 - logBack];
});
