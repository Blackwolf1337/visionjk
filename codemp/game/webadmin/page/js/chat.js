/* ------------------------------------------------------------------ */
/* chat (not in JKAdmin)                                               */
/* ------------------------------------------------------------------ */

const CHAT_MAX_LINES = 400;
// the colors the game gives the text of each kind of chat
const CHAT_COLOR = { all: 2, team: 5, tell: 6, clan: 1, admin: 3 };
let chatSeq = 0, chatEpoch = 0, chatBusy = false, chatUnread = 0;

function chatLine(m) {
	const kind = m.mode === 'tell' ? 'to ' + plainName(m.to) : (m.mode === 'team' || m.mode === 'clan' || m.mode === 'admin') ? m.mode : '';
	return '<div><span class="t">' + escapeHtml(m.time) + '</span>' +
		(kind ? '<span class="k">' + escapeHtml(kind) + '</span>' : '') +
		(m.team ? '<span class="team ' + escapeHtml(m.team) + '"></span>' : '') +
		colorName(m.name) + ': ' + colorName('^' + (CHAT_COLOR[m.mode] || 7) + m.text) + '</div>';
}

async function pollChat() {
	if (chatBusy) return;
	chatBusy = true;
	try {
		const res = await fetch('api/chat?since=' + chatSeq + '&epoch=' + chatEpoch, { cache: 'no-store' });
		if (!res.ok) return;
		const json = await res.json();
		const log = $('chatLog');
		const atEnd = log.scrollHeight - log.scrollTop - log.clientHeight < 24;
		let html = '';
		// a new map loads the game again, its chat numbers start over
		if (chatEpoch && json.epoch !== chatEpoch) {
			html = '<div class="sep">new map</div>';
			chatSeq = 0;
		}
		const fresh = json.messages.filter(m => m.seq > chatSeq);
		html += fresh.map(chatLine).join('');
		chatEpoch = json.epoch;
		chatSeq = json.seq;
		if (!html) return;
		const none = log.querySelector('.none');
		if (none) none.remove();
		log.insertAdjacentHTML('beforeend', html);
		while (log.children.length > CHAT_MAX_LINES) log.firstChild.remove();
		if (atEnd) log.scrollTop = log.scrollHeight;
		if (document.body.classList.contains('chatClosed')) {
			chatUnread += fresh.length;
			$('chatNew').textContent = chatUnread ? chatUnread + ' new' : '';
		}
	} catch (e) {
		/* the state poll shows a lost connection */
	} finally {
		chatBusy = false;
	}
}

$('chatLog').innerHTML = '<div class="none">Nothing said yet.</div>';
$('chatHead').addEventListener('click', () => {
	const closed = document.body.classList.toggle('chatClosed');
	try { localStorage.setItem('wa.chatClosed', closed ? '1' : ''); } catch (e) { /* no storage */ }
	chatUnread = 0;
	$('chatNew').textContent = '';
	if (!closed) $('chatLog').scrollTop = $('chatLog').scrollHeight;
});
try { if (localStorage.getItem('wa.chatClosed')) document.body.classList.add('chatClosed'); } catch (e) { /* no storage */ }
setInterval(pollChat, 1000);
pollChat();

