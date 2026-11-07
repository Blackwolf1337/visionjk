// the panel of a game server: the map and the state of the game
function gameStart() {
	if (!gl) {
		$('loading').textContent = 'This browser has no WebGL 2.';
		return;
	}
	resize();
	requestAnimationFrame(frame);
	// faster updates while watching through a player's eyes
	// Ten times a second while the map is looked at, twenty through the eyes of a player. Once a second while it
	// is not: the tab of the browser is in the back, or the config, the files or the log lie over the map.
	const unseen = () => document.hidden || ['cfgOpen', 'fsOpen', 'logOpen'].some(c => document.body.classList.contains(c));
	// With the socket of the page open (js/stream.js) the state is pushed and nothing is asked: what moves goes on over
	// the time the pushes are apart.
	const poll = () => {
		if (stream.open) {
			streamWants();
			pollInterval = streamInterval();
			setTimeout(poll, 100);
			return;
		}
		pollState();
		pollInterval = view.id >= 0 ? 50 : unseen() ? 1000 : 100;
		setTimeout(poll, pollInterval);
	};
	poll();
	streamStart('api/stream', takeState);
	loadClasses();
	let wanted = 'admin';
	try { wanted = localStorage.getItem('appMode') || 'admin'; } catch (e) { /* the administration then */ }
	setAppMode(wanted);
	// the page the address names (#/config, #/world, ...)
	modeFromHash();
}
// who is logged in: what his role may not use is taken off the page
accountLoad().then(gameStart);
