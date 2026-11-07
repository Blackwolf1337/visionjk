/* ------------------------------------------------------------------ */
/* command panel                                                       */
/* ------------------------------------------------------------------ */

let editing = false;	// don't overwrite inputs the user is typing in

function select(id) {
	if (id >= 0) selectEnt(-1);
	selected = id;
	editing = false;
	if (id < 0) cam.follow = -1;
	disarm();
	renderCommand(true);
	if (state) renderTable();
}

// The size of a player in percent. The field #setScale holds what Set sends, the slider under it only moves that
// field. The slider is two halves, so that 100 percent is its middle: it runs from 0 to 1000, left of the middle
// is 10 to 100 percent and right of it 100 to 500. The 30 on either side of the middle (3 percent of its way)
// are 100 percent too: there it snaps. What it gives is rounded to 5.
const SCALE_MIN = 10, SCALE_MAX = 500, SCALE_BAR_MID = 500, SCALE_BAR_SNAP = 30, SCALE_BAR_HALF = SCALE_BAR_MID - SCALE_BAR_SNAP;
function scaleOfBar(pos) {
	if (Math.abs(pos - SCALE_BAR_MID) <= SCALE_BAR_SNAP) return 100;
	const pct = pos < SCALE_BAR_MID ? SCALE_MIN + pos / SCALE_BAR_HALF * (100 - SCALE_MIN) : 100 + (pos - SCALE_BAR_MID - SCALE_BAR_SNAP) / SCALE_BAR_HALF * (SCALE_MAX - 100);
	// and so is what comes out within 3 of 100
	return Math.abs(pct - 100) <= 3 ? 100 : Math.round(pct / 5) * 5;
}
function scaleToBar(pct) {
	pct = Math.min(SCALE_MAX, Math.max(SCALE_MIN, pct));
	if (pct === 100) return SCALE_BAR_MID;
	return pct < 100 ? (pct - SCALE_MIN) / (100 - SCALE_MIN) * SCALE_BAR_HALF : SCALE_BAR_MID + SCALE_BAR_SNAP + (pct - 100) / (SCALE_MAX - 100) * SCALE_BAR_HALF;
}
// the slider and the label follow the field
function scaleFollow() {
	const text = $('setScale').value;
	$('scaleVal').textContent = text === '' ? '' : text + '%';
	if (text !== '') $('setScaleBar').value = scaleToBar(+text);
}

function renderCommand(fillInputs) {
	const p = selected >= 0 && players.get(selected);
	$('command').classList.toggle('open', !!p);
	if (!p) return;
	const d = p.data;
	$('cmdName').innerHTML = colorName(d.name);
	$('cmdSub').innerHTML = '<span class="team ' + d.team + '"></span>' + d.team + (d.bot ? ' &middot; bot' : '') + ' &middot; ' +
		escapeHtml(WEAPONS[d.weapon] || 'weapon ' + d.weapon) + ' &middot; score ' + d.score + (d.dead ? ' &middot; <b style="color:var(--accent2)">dead</b>' : '');
	$('cmdHp').textContent = Math.max(d.health, 0) + ' / ' + d.maxHealth;
	$('cmdAr').textContent = d.armor;
	$('cmdHpBar').style.width = Math.min(100, Math.max(0, d.health) / Math.max(1, d.maxHealth) * 100) + '%';
	$('cmdArBar').style.width = Math.min(100, d.armor / Math.max(1, d.maxHealth) * 100) + '%';
	$('godBtn').classList.toggle('primary', d.god);
	$('noclipBtn').classList.toggle('primary', !!d.noclip);
	$('schizoBtn').classList.toggle('primary', !!d.schizo);
	$('flatulenceBtn').classList.toggle('primary', !!d.flatulence);
	$('disorientBtn').classList.toggle('primary', !!d.disorient);
	$('followBtn').classList.toggle('primary', cam.follow === d.id);
	// who that is: since when he is there, from where, and with which client plugin of JA+
	if (d.minutes !== undefined) {
		$('cmdSub').insertAdjacentHTML('beforeend', '<br>' + (d.minutes < 60 ? d.minutes + ' min' : Math.floor(d.minutes / 60) + ' h ' + d.minutes % 60 + ' min') + ' in the game' +
			(d.address ? ' &middot; ' + escapeHtml(d.address) : '') + (d.bot ? '' : ' &middot; ' + (d.plugin ? 'plugin ' + d.plugin : 'no plugin')) +
			// what lag compensation (tay_unlagged) shifts the others back by for his shots
			(d.bot || d.lag === undefined ? '' : ' &middot; lag ' + d.lag + ' ms'));
	}
	if (d.jp) {
		document.querySelectorAll('[data-jpstate]').forEach(b => b.classList.toggle('primary', jpState(d.jp, b.dataset.jpstate)));
		if (d.jp.admin) $('cmdSub').insertAdjacentHTML('beforeend', ' &middot; admin ' + (JP_RANKS[d.jp.admin] || d.jp.admin));
		if (d.jp.altDim) $('cmdSub').insertAdjacentHTML('beforeend', ' &middot; alternate dimension');
		if (fillInputs || !editing) $('jpRank').value = d.jp.admin >= 2 ? JP_RANKS[d.jp.admin] : 'no';
	}
	if (fillInputs || !editing) {
		$('setHp').value = Math.max(d.health, 1);
		$('setAr').value = d.armor;
		$('setScale').value = d.scale;
		scaleFollow();
		$('setTeam').value = d.team;
		// empty while the player has the gravity of the server
		$('setGrav').value = d.ownGravity ? d.gravity : '';
	}
}

for (const id of ['setHp', 'setAr', 'setScale', 'setScaleBar', 'setTeam', 'setGrav', 'jpRank']) {
	$(id).addEventListener('focus', () => editing = true);
	$(id).addEventListener('input', () => editing = true);
}
// The field and the slider of the size follow each other. The slider is put where its value is, which is how it
// stops in the middle. Nothing of this is sent: Set does that, or Enter in the field.
$('setScale').addEventListener('input', scaleFollow);
$('setScaleBar').addEventListener('input', () => { $('setScale').value = scaleOfBar(+$('setScaleBar').value); scaleFollow(); });
$('setScaleBar').addEventListener('dblclick', () => { editing = true; $('setScale').value = 100; scaleFollow(); });

document.querySelectorAll('[data-set]').forEach(b => b.addEventListener('click', async () => {
	await action({ action: b.dataset.set, id: selected, value: $(b.dataset.input).value });
	editing = false;
}));

// dangerous actions need a second click
let armed = null;
function disarm() {
	if (armed) { armed.classList.remove('armed'); armed.textContent = armed.dataset.label; armed = null; }
}
// false on the first click of a button, which arms it for three seconds, true on the second one
function confirmed(b) {
	const again = armed === b;
	disarm();
	if (again) return true;
	armed = b;
	b.dataset.label = b.textContent;
	b.classList.add('armed');
	b.textContent = 'Sure? ' + b.dataset.label;
	setTimeout(() => { if (armed === b) disarm(); }, 3000);
	return false;
}
document.querySelectorAll('[data-act]').forEach(b => {
	b.addEventListener('click', () => {
		if (!b.hasAttribute('data-confirm')) disarm();
		else if (!confirmed(b)) return;
		action({ action: b.dataset.act, id: selected });
	});
});
// JA+ admin commands on the selected player (not in JKAdmin). A button with data-jpoff sends that command
// while its state is on: the pairs amsleep/amwake, amsilence/amunsilence, amForceAltDim/amUnForceAltDim.
const JP_RANKS = { 1: 'rcon', 2: 'council', 3: 'knight', 4: 'instructor' };
function jpState(jp, key) { return key === 'altForced' ? jp.altDim === 2 : !!jp[key]; }
document.querySelectorAll('[data-jp]').forEach(b => b.addEventListener('click', () => {
	const p = selected >= 0 && players.get(selected);
	const on = p && p.data.jp && b.dataset.jpstate && jpState(p.data.jp, b.dataset.jpstate);
	disarm();
	action({ action: on && b.dataset.jpoff ? b.dataset.jpoff : b.dataset.jp, id: selected });
}));
document.querySelectorAll('[data-jptext]').forEach(b => b.addEventListener('click', async () => {
	const input = $(b.dataset.input);
	const ok = await action({ action: b.dataset.jptext, id: selected, value: input.value });
	editing = false;
	if (ok && input.tagName === 'INPUT') input.value = '';
}));
// Enter in a field is a click on the button that names it (data-input). Not in a list, which Enter opens.
document.querySelectorAll('button[data-input]').forEach(b => {
	const field = $(b.dataset.input);
	if (field.tagName !== 'SELECT') field.addEventListener('keydown', e => {
		if (e.key === 'Enter' && !e.isComposing) { e.preventDefault(); b.click(); }
	});
});
$('followBtn').addEventListener('click', () => { cam.follow = cam.follow === selected ? -1 : selected; renderCommand(); });
$('cmdClose').addEventListener('click', () => select(-1));
$('fpBtn').addEventListener('click', () => setFirstPerson(view.id >= 0 ? -1 : selected));
$('fpExit').addEventListener('click', () => setFirstPerson(-1));

$('sayForm').addEventListener('submit', e => { e.preventDefault(); if ($('sayText').value) action({ action: 'say', value: $('sayText').value }).then(ok => { if (ok) { $('sayText').value = ''; pollChat(); } }); });
// gravity for everybody (not in JKAdmin), the field shows the current one as its hint
$('toggleSide').addEventListener('click', () => {
	document.body.classList.toggle('sideClosed');
	try { localStorage.setItem('wa.sideClosed', document.body.classList.contains('sideClosed') ? '1' : ''); } catch (e) { /* no storage */ }
	setTimeout(resize, 200);
});
try { if (localStorage.getItem('wa.sideClosed')) document.body.classList.add('sideClosed'); } catch (e) { /* no storage */ }


// The groups of the panel of a player: Watch and steer stays open at the top, the others are tabs of which one is
// open. The page keeps which (wa.cmdTab).
const cmdSects = [...document.querySelectorAll('#command details.sect:not([data-sect="watch"])')], cmdTabs = document.createElement('nav');
cmdTabs.id = 'cmdTabs';
function cmdShowTab(name) {
	if (!cmdSects.some(d => d.dataset.sect === name)) name = cmdSects[0].dataset.sect;
	cmdSects.forEach(d => { d.open = d.dataset.sect === name; });
	cmdTabs.querySelectorAll('button').forEach(b => b.classList.toggle('on', b.dataset.sect === name));
	try { localStorage.setItem('wa.cmdTab', name); } catch (e) { /* not kept */ }
}
cmdSects.forEach(d => {
	const b = document.createElement('button');
	b.type = 'button';
	b.dataset.sect = d.dataset.sect;
	b.textContent = d.querySelector('summary').textContent;
	b.addEventListener('click', () => cmdShowTab(d.dataset.sect));
	cmdTabs.appendChild(b);
});
cmdSects[0].before(cmdTabs);
document.querySelector('#command details[data-sect="watch"]').open = true;
{ let kept = ''; try { kept = localStorage.getItem('wa.cmdTab') || ''; } catch (e) { /* the first tab */ } cmdShowTab(kept); }
