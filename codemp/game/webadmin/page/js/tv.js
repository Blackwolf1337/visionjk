/* ------------------------------------------------------------------ */
/* the page TV: the TV of the server in the client for the browser     */
/* ------------------------------------------------------------------ */

// The web host of the engine (vs_clientWebPort) has the page of the client for the browser, and with ?tv it plays the
// TV of the server: a spectator in no slot who follows the players (server/sv_tv.cpp). The page loads the game files
// first (more than a GB the first time, then from the storage of the browser), so it starts on a click, and the
// frame is emptied when the page TV is left, which stops the game in it.

function tvAddress() {
	if (!me.tv || !me.tv.port) return '';
	return me.tv.page || location.protocol + '//' + location.hostname + ':' + me.tv.port + '/?tv';
}

// the button is there when the server has a web host with the TV on (wa_auth.c, WA_TVPort)
function tvApply() {
	document.body.classList.toggle('hasTV', !!tvAddress());
}

function tvOpen() {
	const address = tvAddress();
	$('tvNote').textContent = address
		? 'Watch opens the client for the browser from ' + address + '. It loads the game files first, more than a GB the first time, after that from the storage of this browser.'
		: 'This server has no TV: vs_clientWebPort is 0, or vs_tv is 0.';
	$('tvGo').hidden = !address;
	if ($('tvFrame').hidden) $('tvStart').hidden = false;
}

function tvLeave() {
	if ($('tvFrame').hidden) return;
	$('tvFrame').src = 'about:blank';
	$('tvFrame').hidden = true;
}

$('tvGo').addEventListener('click', () => {
	$('tvStart').hidden = true;
	$('tvFrame').hidden = false;
	$('tvFrame').src = tvAddress();
	$('tvFrame').focus();
});
