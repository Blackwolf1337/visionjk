if (!gl) {
	$('loading').textContent = 'This browser has no WebGL 2.';
} else {
	resize();
	requestAnimationFrame(frame);
	// faster updates while watching through a player's eyes
	// Ten times a second while the map is looked at, twenty through the eyes of a player. Once a second while it
	// is not: the tab of the browser is in the back, or the config, the files or the log lie over the map.
	const unseen = () => document.hidden || ['cfgOpen', 'fsOpen', 'logOpen'].some(c => document.body.classList.contains(c));
	const poll = () => { pollState(); pollInterval = view.id >= 0 ? 50 : unseen() ? 1000 : 100; setTimeout(poll, pollInterval); };
	poll();
	loadClasses();
	let wanted = 'admin';
	try { wanted = localStorage.getItem('appMode') || 'admin'; } catch (e) { /* the administration then */ }
	setAppMode(wanted);
}
