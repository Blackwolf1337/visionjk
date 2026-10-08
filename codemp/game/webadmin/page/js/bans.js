// ---- the bans: /api/bans lists the addresses, the actions banip and unban change the list

async function banLoad() {
	try {
		const bans = (await (await fetch('api/bans', { cache: 'no-store' })).json()).bans;
		$('banCount').textContent = bans.length;
		$('banRows').innerHTML = bans.map(a => '<tr><td>' + escapeHtml(a) + '</td><td class="num"><button data-ban="' + escapeHtml(a) + '" title="Lets this address connect again">Lift</button></td></tr>').join('') ||
			'<tr><td class="hint">Nobody is banned.</td></tr>';
	} catch (e) {
		$('banRows').innerHTML = '<tr><td class="hint">This game module has no list of bans.</td></tr>';
	}
}
// the game takes the command with its next frame: the list is asked for a moment later
async function banChange(params) {
	await action(params);
	setTimeout(banLoad, 400);
}
$('banForm').addEventListener('submit', e => {
	e.preventDefault();
	const address = $('banAddress').value.trim();
	if (!address) return;
	$('banAddress').value = '';
	banChange({ action: 'banip', value: address });
});
$('banRows').addEventListener('click', e => {
	const b = e.target.closest('button[data-ban]');
	if (b) banChange({ action: 'unban', value: b.dataset.ban });
});

