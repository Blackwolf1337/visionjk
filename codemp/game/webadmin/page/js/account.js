// ---- the tab Account: who is logged in, his password, the second step of his login, a token for scripts,
// one-time codes for the admin login in the game, and for an owner the accounts of the panel (wa_auth.c)

let me = { name: '', role: 'moderator', account: false };
const ROLE_RANK = { moderator: 1, admin: 2, owner: 3 };
const ACCOUNT_WHY = {
	wrong: 'That is not your password.', busy: 'Another password is being checked. Try again in a second.',
	short: 'A password has ten characters at least.', badcode: 'That code is not right.',
	noaccount: 'You are logged in with a key, there is no account to change.', role: 'Your account may not do that.',
	name: 'A name is made of letters, digits, _ - and . and has 23 characters at most.', exists: 'There is an account of that name already.',
	full: 'There is no room for another account.', lastowner: 'The panel needs one owner at least.', nouser: 'There is no such account.',
	rank: 'There is no such rank.', random: 'The server has no random numbers.', form: 'Something is missing.',
};

function roleAtLeast(role) { return ROLE_RANK[me.role] >= ROLE_RANK[role]; }

async function accountPost(path, fields) {
	const json = await apiPost(path, fields);
	if (!json.ok) toast(ACCOUNT_WHY[json.why] || (json.why ? 'Refused (' + json.why + ').' : json.message), false);
	return json;
}

// what the role of the account may not use is not shown
function accountApplyRole() {
	document.body.classList.toggle('role-moderator', me.role === 'moderator');
	document.body.classList.toggle('role-admin', me.role === 'admin');
	document.body.classList.toggle('role-owner', me.role === 'owner');
	if (!roleAtLeast('admin') && appMode === 'editor') setAppMode('admin');
}

async function accountLoad() {
	try {
		me = await (await fetch('api/me', { cache: 'no-store' })).json();
	} catch (e) {
		return;
	}
	accountApplyRole();
	$('acctWho').textContent = me.name + ' (' + me.role + (me.account ? '' : ', by key') + ')';
	$('acctOwn').hidden = !me.account;
	$('acctTotpState').textContent = me.totp ? 'On. ' + me.recovery + ' recovery codes are left.' : 'Off: the password alone opens this account.';
	$('acctTotpStart').hidden = me.totp;
	$('acctTotpOff').hidden = !me.totp;
	$('acctTokenState').textContent = me.token ? 'This account has a token.' : 'This account has no token.';
	$('acctTokenOff').hidden = !me.token;
	$('acctCodes').hidden = !roleAtLeast('admin');
	$('acctCodeRank').querySelector('option[value="rcon"]').hidden = !roleAtLeast('owner');
	$('acctCodeHint').textContent = me.passwords
		? 'For an admin in the game whose client can not log in with a key. The rank passwords are on as well (jp_adminPasswords 1).'
		: 'For an admin in the game whose client can not log in with a key (jamp.exe, the JA+ plugin). He types the code as "amlogin <code>" within a minute, it works once.';
	$('acctOwner').hidden = !roleAtLeast('owner');
	if (roleAtLeast('owner')) {
		$('acctUsers').innerHTML = (me.users || []).map(u => '<tr><td>' + escapeHtml(u.name) + (u.totp ? ' <span class="hint" title="Has the second step of the login">2</span>' : '') + '</td><td><select data-role="' + escapeHtml(u.name) + '">' +
			['moderator', 'admin', 'owner'].map(r => '<option' + (r === u.role ? ' selected' : '') + '>' + r + '</option>').join('') + '</select></td><td class="num">' +
			'<button data-reset="' + escapeHtml(u.name) + '" title="A new password from the server, the second step and the token off, every session of the account closed">Reset</button> ' +
			'<button class="danger" data-remove="' + escapeHtml(u.name) + '">Remove</button></td></tr>').join('');
		$('acctKeys').innerHTML = (me.adminKeys || []).map(k => '<tr><td>' + escapeHtml(k.rank) + '</td><td title="' + escapeHtml(k.fingerprint) + '">' + escapeHtml(k.comment || k.fingerprint) + '</td></tr>').join('') ||
			'<tr><td class="hint">admin_keys.txt has no key.</td></tr>';
	}
}

// something that is shown once: a password, a token, the recovery codes
function accountShow(title, text) {
	$('acctOnceTitle').textContent = title;
	$('acctOnceText').value = text;
	$('acctOnce').hidden = false;
	$('acctOnceText').focus();
	$('acctOnceText').select();
}
$('acctOnceClose').addEventListener('click', () => { $('acctOnceText').value = ''; $('acctOnce').hidden = true; });

$('acctLogout').addEventListener('click', async () => {
	await accountPost('api/logout');
	location.reload();
});

$('acctPassForm').addEventListener('submit', async e => {
	e.preventDefault();
	if ($('acctPassNew').value !== $('acctPassAgain').value) { toast('The two new passwords are not the same.', false); return; }
	const json = await accountPost('api/account/password', { password: $('acctPassOld').value, new: $('acctPassNew').value });
	if (json.ok) {
		e.target.reset();
		toast('The password is changed. Every other session of this account is closed.');
	}
});

// the second step: the server makes a secret, the app scans it, and a code of the app shows that it has it
$('acctTotpStart').addEventListener('click', async () => {
	const password = prompt('Your password, to set the second step up:');
	if (!password) return;
	const json = await accountPost('api/account/totp/start', { password });
	if (!json.ok) return;
	$('acctTotpSetup').hidden = false;
	$('acctTotpSecret').textContent = json.secret.replace(/(.{4})/g, '$1 ').trim();
	$('acctTotpUri').value = json.uri;
	if (!qrDraw($('acctTotpQr'), json.uri, 4)) $('acctTotpQr').hidden = true;
	$('acctTotpCode').focus();
});
$('acctTotpForm').addEventListener('submit', async e => {
	e.preventDefault();
	const json = await accountPost('api/account/totp/confirm', { code: $('acctTotpCode').value.trim() });
	if (!json.ok) return;
	e.target.reset();
	$('acctTotpSetup').hidden = true;
	$('acctTotpUri').value = '';
	accountShow('The second step is on. These recovery codes are shown this once: each opens the account one time when the app is not at hand.', json.recovery.join('\n'));
	accountLoad();
});
$('acctTotpOff').addEventListener('click', async () => {
	const password = prompt('Your password, to switch the second step off:');
	if (!password) return;
	if ((await accountPost('api/account/totp/off', { password })).ok) { toast('The second step is off.'); accountLoad(); }
});

$('acctTokenNew').addEventListener('click', async () => {
	const password = prompt('Your password, to make a token for scripts' + (me.token ? ' (the one there is stops working)' : '') + ':');
	if (!password) return;
	const json = await accountPost('api/account/token', { password });
	if (!json.ok) return;
	accountShow('The token of this account, shown this once. A script sends it with every request, and may do what this account may:',
		json.token + '\n\ncurl -H "Authorization: Bearer ' + json.token + '" ' + location.origin + '/api/state');
	accountLoad();
});
$('acctTokenOff').addEventListener('click', async () => {
	if ((await accountPost('api/account/token/off')).ok) { toast('The token is removed.'); accountLoad(); }
});

let acctCodeTimer = 0;
$('acctCodeForm').addEventListener('submit', async e => {
	e.preventDefault();
	const json = await accountPost('api/admincode', { rank: $('acctCodeRank').value });
	if (!json.ok) return;
	let left = json.seconds;
	clearInterval(acctCodeTimer);
	const show = () => {
		$('acctCode').textContent = left > 0 ? 'amlogin ' + json.code + '   (' + left + ' s)' : 'The code has run out.';
		if (left-- <= 0) clearInterval(acctCodeTimer);
	};
	show();
	acctCodeTimer = setInterval(show, 1000);
});

$('acctAddForm').addEventListener('submit', async e => {
	e.preventDefault();
	const name = $('acctAddName').value.trim();
	const json = await accountPost('api/users/add', { name, role: $('acctAddRole').value });
	if (!json.ok) return;
	e.target.reset();
	accountShow('The account ' + name + ' is made. Its password, shown this once: hand it on, he changes it on this tab.', json.password);
	accountLoad();
});
$('acctUsers').addEventListener('change', async e => {
	const select = e.target.closest('select[data-role]');
	if (!select) return;
	await accountPost('api/users/role', { name: select.dataset.role, role: select.value });
	accountLoad();
});
$('acctUsers').addEventListener('click', async e => {
	const reset = e.target.closest('button[data-reset]'), remove = e.target.closest('button[data-remove]');
	if (reset) {
		if (!confirm('Give the account ' + reset.dataset.reset + ' a new password? Its second step and its token go, its sessions are closed.')) return;
		const json = await accountPost('api/users/reset', { name: reset.dataset.reset });
		if (json.ok) accountShow('The new password of ' + reset.dataset.reset + ', shown this once:', json.password);
	} else if (remove) {
		if (!confirm('Remove the account ' + remove.dataset.remove + '?')) return;
		await accountPost('api/users/remove', { name: remove.dataset.remove });
	} else {
		return;
	}
	accountLoad();
});
