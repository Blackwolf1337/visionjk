// ---- the WebSocket of the page (wa_stream.c): the state pushed twenty times a second, and the answers of what the
// page subscribed to whenever they change
//
// A message is a line with its name and the HTTP status of the answer, then the answer as a GET of the same address
// has it. Without the socket (a game module from before it, a proxy that does not pass it on) every part asks over
// HTTP as before: streamGet() says whether a part went over the socket, and the poll of the state asks only while
// stream.open is false. A socket that ends is tried again, later each time it fails.
const stream = { ws: null, open: false, subs: new Map(), wants: null, tries: 0, came: 0, gap: 0, held: false, path: '', onState: null };

// path: 'api/stream' of the panel, '/live/stream' of the live view. onState gets the state as /api/state has it.
function streamStart(path, onState) {
	let ws;
	stream.path = path;
	stream.onState = onState;
	if (stream.held || stream.ws) return;
	try {
		const u = new URL(path, location.href);
		u.protocol = u.protocol === 'https:' ? 'wss:' : 'ws:';
		ws = new WebSocket(u.href);
	} catch (e) {
		return;
	}
	stream.ws = ws;
	ws.onopen = () => {
		stream.open = true;
		stream.tries = 0;
		stream.wants = null;
		stream.came = 0;
		streamWants();
		// what was subscribed to before is asked for again, with what each part has now
		for (const sub of stream.subs.values()) { sub.sent = null; sub.again(); }
	};
	ws.onmessage = e => {
		const text = String(e.data), nl = text.indexOf('\n');
		const head = (nl < 0 ? text : text.slice(0, nl)).split(' '), body = nl < 0 ? '' : text.slice(nl + 1);
		const name = head[0], status = +head[1] || 0;
		if (name === 'state') {
			if (status !== 200) return;
			// how far apart the pushes come (a long wait counts as 400 msec at most): what is drawn moves over that
			const now = performance.now();
			if (stream.came) { const was = Math.min(400, now - stream.came); stream.gap = stream.gap ? stream.gap * 0.8 + was * 0.2 : was; }
			stream.came = now;
			onState(JSON.parse(body));
			return;
		}
		const sub = stream.subs.get(name);
		if (!sub) return;
		let json = null;
		if (status === 200) { try { json = JSON.parse(body); } catch (err) { /* no JSON, as an answer that failed */ } }
		sub.take(json, status);
		// the part asks anew with what it has now: the server keeps that question and answers when it changes
		sub.again();
	};
	ws.onclose = () => {
		stream.open = false;
		stream.ws = null;
		for (const sub of stream.subs.values()) sub.sent = null;
		if (stream.held) return;
		stream.tries++;
		setTimeout(() => streamStart(path, onState), Math.min(30000, 1000 * 2 ** Math.min(stream.tries, 5)));
	};
}

// A page that is not seen for a while lets its socket go, so the server does not push to nobody, and opens it again
// when it is seen
function streamHold(on) {
	if (on === stream.held) return;
	stream.held = on;
	if (on && stream.ws) stream.ws.close();
	if (!on && stream.path) { stream.tries = 0; streamStart(stream.path, stream.onState); }
}

// What the state of the panel is asked with besides what the page has: the HUD of the player it looks through.
// The poll of the state calls it as often as it would ask.
function streamWants() {
	const wants = typeof view !== 'undefined' && view.id >= 0 ? 'hud=' + view.id : '';
	if (stream.open && wants !== stream.wants) {
		stream.ws.send('state ' + wants);
		stream.wants = wants;
	}
}

// How far apart the state comes over the socket, a little more: what moves goes on over that until the next one
const streamInterval = () => Math.min(1000, Math.max(50, (stream.gap || 50) * 1.25));

// A GET a part of the page asks for again and again (url as fetch has it, 'api/chat?since=3'). With the socket
// open it is a subscription under name: take(json, status) gets the answer whenever it changes (json null when it
// is no 200 with JSON), and again() runs after it, so that the part asks anew with what it has then. true: it went
// over the socket, the part does not fetch.
function streamGet(name, url, take, again) {
	if (!stream.open) return false;
	let sub = stream.subs.get(name);
	if (!sub) stream.subs.set(name, sub = { sent: null });
	sub.take = take;
	sub.again = again;
	if (sub.sent !== url) {
		stream.ws.send('sub ' + name + ' /' + url);
		sub.sent = url;
	}
	return true;
}

// a part that does not want its answers any more
function streamDrop(name) {
	if (!stream.subs.has(name)) return;
	stream.subs.delete(name);
	if (stream.open) stream.ws.send('unsub ' + name);
}
