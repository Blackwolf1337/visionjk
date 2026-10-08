// ---- the log of the game: /api/log gives the file from a place on, the view asks on from where the last answer ended

const logView = { next: -1, lines: [], partial: '', first: true, busy: false };
const LOG_KEEP = 4000;	// lines the view holds

function logRender() {
	const f = $('logFilter').value.trim().toLowerCase(), box = $('logText');
	const atEnd = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
	box.textContent = (f ? logView.lines.filter(l => l.toLowerCase().includes(f)) : logView.lines).join('\n');
	if ($('logFollow').checked && (atEnd || logView.first)) box.scrollTop = box.scrollHeight;
}

async function logPoll() {
	if (mode !== 'log' || logView.busy) return;
	logView.busy = true;
	try {
		const res = await fetch('api/log' + (logView.next >= 0 ? '?from=' + logView.next : ''), { cache: 'no-store' });
		if (!res.ok) { $('logState').textContent = res.status === 404 ? 'This game module has no log view.' : 'HTTP ' + res.status; return; }
		const json = await res.json();
		// a new file, or the view was behind its end: what is shown is of the old one
		if (logView.next >= 0 && json.from !== logView.next) { logView.lines = []; logView.partial = ''; logView.first = true; }
		let text = logView.partial + json.text;
		// a view that starts in the middle of the file starts in the middle of a line
		if (logView.first && json.from > 0) text = text.slice(text.indexOf('\n') + 1);
		const lines = text.split('\n');
		logView.partial = lines.pop();
		const before = logView.lines.length;
		logView.lines.push(...lines.map(l => l.replace(/\r$/, '').replace(/\^[0-9]/g, '')));
		if (logView.lines.length > LOG_KEEP) logView.lines.splice(0, logView.lines.length - LOG_KEEP);
		logView.next = json.next;
		$('logSize').textContent = logView.lines.length + ' lines';
		$('logState').textContent = '';
		if (lines.length || logView.first) logRender();
		logView.first = false;
		// more of it is there already
		if (json.next < json.size && lines.length + before > before) setTimeout(logPoll, 50);
	} catch (e) {
		$('logState').textContent = 'Request failed: ' + e.message;
	} finally {
		logView.busy = false;
	}
}
setInterval(logPoll, 2000);

