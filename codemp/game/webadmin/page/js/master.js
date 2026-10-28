/* ------------------------------------------------------------------ */
/* the masterserver                                                    */
/* ------------------------------------------------------------------ */

// The masterserver runs this panel too (wa_settings.c, WA_MasterRoute): no map and no players, the settings, the
// log, the files, and what its relay does.

let masterTimer = 0;

// the pollers of the game ask for nothing on the masterserver
function onMaster() { return document.body.classList.contains('master'); }

function masterStart() {
	document.body.classList.add('master', 'app-admin');
	$('loading').hidden = true;
	setMode('master');
}

async function masterPoll() {
	clearTimeout(masterTimer);
	if (mode !== 'master') return;
	try {
		const s = await (await fetch('api/master', { cache: 'no-store' })).json();
		$('msSessions').textContent = s.sessions;
		$('msRooms').textContent = s.rooms;
		$('msKbps').textContent = s.kbps;
		$('msFlagged').textContent = s.rep ? s.flagged : 'off';
		$('msRep').textContent = s.rep ? 'A server is flagged at a score of ' + s.threshold + '.' : 'The reports of the clients are not taken (ms_rep 0).';
	} catch (e) { /* asked again */ }
	masterTimer = setTimeout(masterPoll, 2000);
}

async function masterCommand(cmd) {
	const json = await apiPost('api/command', { cmd });
	toast(json.ok ? 'Sent, the answer is in Log, Console' : json.message, json.ok);
}

$('msRepStatus').addEventListener('click', () => masterCommand('ms_repstatus'));
$('msRepClear').addEventListener('click', () => {
	if (confirm('Forget every report of every server?')) masterCommand('ms_repclear all');
});
