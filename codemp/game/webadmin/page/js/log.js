// ---- the log of the game and the console of the server: /api/log and /api/console give their text from a place on,
// the view asks on from where the last answer ended. A line typed under it goes to /api/command.

// The three sources, each with its own lines, its place in the text and the line that has not ended yet. first: nothing
// of it was shown yet, again: it was asked for while a request was on its way.
const logSource = (api, none) => ({ api, none, next: -1, lines: [], partial: '', first: true, busy: false, again: false, state: '' });
const LOG_SOURCES = {
	log: logSource('api/log', 'This game module has no log view.'),
	console: logSource('api/console', 'This game module does not show its console.'),
	ids: logSource('api/ids', 'This game module does not show what the intrusion detection wrote down.')
};
// the one that is shown: the filter, "follow the end" and "Clear view" (their listeners are in js/load.js) work on it
let logView = LOG_SOURCES.log;
const LOG_KEEP = 4000;	// lines the view holds of each

// What a line is, for its colour: a kill, chat, a warning or an error, noise of the game (an item picked up, a
// client that begins, his userinfo), or the rest. The game log puts the time of the game before each line, the
// console the date and the time of day (twice on a line of the game log it echoes).
const LOG_TIME = /^\s*(?:\d{4}-\d\d-\d\d )?(\d+:\d\d(?::\d\d)?)\s(.*)$/, LOG_STAMP = /^(?:\d{4}-\d\d-\d\d )?\d+:\d\d(?::\d\d)?\s/;
function logKind(text) {
	if (/^Kill:/.test(text)) return 'kill';
	if (/^(say|sayteam|tell|vsay|vsay_team):/i.test(text)) return 'chat';
	if (/\b(error|failed|refused)\b|\bblocked\b/i.test(text)) return 'err';
	if (/^warning|\bwarning\b|^SERVER:/i.test(text)) return 'warn';
	if (/^(Item|ClientBegin|ClientUserinfoChanged|InitGame|ShutdownGame|Exit|score|red:|-{8,})/.test(text)) return 'dim';
	return '';
}
function logLine(line) {
	const m = LOG_TIME.exec(line), text = m ? m[2] : line, kind = logKind(text.replace(LOG_STAMP, ''));
	// (the space in the column: a copied line keeps it between the time and the text)
	return '<span class="ll' + (kind ? ' ' + kind : '') + '">' + (m ? '<i>' + m[1] + ' </i>' : '') + escapeHtml(text) + '</span>';
}

function logRender() {
	const f = $('logFilter').value.trim().toLowerCase(), box = $('logText');
	const atEnd = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
	// a line each, in its colour; copied out of the view they are plain lines again
	box.innerHTML = (f ? logView.lines.filter(l => l.toLowerCase().includes(f)) : logView.lines).map(logLine).join('');
	$('logSize').textContent = logView.lines.length + (logView.lines.length === 1 ? ' line' : ' lines');
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
	if (mode !== 'log') { streamDrop('log'); return; }
	// over the socket of the page when it is open (js/stream.js), pushed when the text grew
	if (streamGet('log', v.api + (v.next >= 0 ? '?from=' + v.next : ''), (json, status) => logTake(v, json, status), logPoll)) return;
	if (v.busy) { v.again = true; return; }
	v.busy = true;
	try {
		const res = await fetch(v.api + (v.next >= 0 ? '?from=' + v.next : ''), { cache: 'no-store' });
		logTake(v, res.ok ? await res.json() : null, res.status);
	} catch (e) {
		v.state = 'Request failed: ' + e.message;
		if (v === logView) $('logState').textContent = v.state;
	} finally {
		v.busy = false;
		if (v.again) { v.again = false; setTimeout(logPoll, 50); }
	}
}

// An answer for the view v: its text from v.next on. With more of it there already the view asks again at once.
function logTake(v, json, status) {
	const tell = text => { v.state = text; if (v === logView) $('logState').textContent = text; };
	if (!json) { tell(status === 404 ? v.none : 'HTTP ' + status); return; }
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
}
setInterval(logPoll, 2000);

document.querySelectorAll('#logSource button').forEach(b => b.addEventListener('click', () => logShow(b.dataset.log)));
// the ?: what the three logs are
$('logHelpBtn').addEventListener('click', () => { $('logHelp').hidden = !$('logHelp').hidden; $('logHelpBtn').classList.toggle('on', !$('logHelp').hidden); });

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
	const json = await apiPost('api/command', { cmd }, 'This game module does not take console commands.');
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
