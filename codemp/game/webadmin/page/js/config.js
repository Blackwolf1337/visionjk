/* ------------------------------------------------------------------ */
/* the server config                                                   */
/* ------------------------------------------------------------------ */

// the server takes a request of 64 KB at most, head and body
const CFG_MAX_BYTES = 60000;
let cfgLoaded = false, cfgSaved = '';

function cfgBytes(text) { return new TextEncoder().encode(text).length; }

// Colors for the text of a config. A line is statements with ; between them, a statement is words with blanks
// between them, "..." is one word, // begins a comment. Neither ; nor // count inside quotes.
// Words that are commands of the console. Any other first word is a cvar that is set by its name.
const CFG_COMMANDS = new Set(['set', 'seta', 'sets', 'setu', 'reset', 'toggle', 'bind', 'unbind', 'unbindall', 'exec', 'vstr', 'wait', 'echo',
	'map', 'devmap', 'map_restart', 'addbot', 'kick', 'clientkick', 'banuser', 'addip', 'removeip', 'say', 'svsay', 'writeconfig', 'killserver', 'rehashbans']);
const CFG_SETTERS = new Set(['set', 'seta', 'sets', 'setu', 'reset', 'toggle']);

// a quoted word. The color codes of the game stay to be read and color what follows them as the game would.
function cfgString(s) {
	let out = '', cls = 'cfg-s', chunk = '';
	const flush = () => { if (chunk) out += '<span class="' + cls + '">' + escapeHtml(chunk) + '</span>'; chunk = ''; };
	for (let i = 0; i < s.length; i++) {
		if (s[i] === '^' && s[i + 1] >= '0' && s[i + 1] <= '9') {
			flush();
			out += '<span class="cfg-code">' + s[i] + s[i + 1] + '</span>';
			cls = 'c' + s[i + 1];
			i++;
		} else {
			chunk += s[i];
		}
	}
	flush();
	return out;
}
function cfgLine(line) {
	let out = '', i = 0, word = 0, setter = false;
	const span = (cls, s) => '<span class="' + cls + '">' + escapeHtml(s) + '</span>';
	while (i < line.length) {
		const ch = line[i];
		if (ch === '/' && line[i + 1] === '/') { out += span('cfg-c', line.slice(i)); break; }
		if (ch === ' ' || ch === '\t') { out += ch; i++; continue; }
		if (ch === ';') { out += span('cfg-p', ch); i++; word = 0; setter = false; continue; }
		let j = i;
		if (ch === '"') {
			j = line.indexOf('"', i + 1);
			j = j < 0 ? line.length : j + 1;
			out += cfgString(line.slice(i, j));
		} else {
			while (j < line.length && !' \t;"'.includes(line[j]) && !(line[j] === '/' && line[j + 1] === '/')) j++;
			const tok = line.slice(i, j), low = tok.toLowerCase();
			let cls = '';
			if (word === 0) { cls = CFG_COMMANDS.has(low) ? 'cfg-k' : 'cfg-v'; setter = CFG_SETTERS.has(low); }
			else if (word === 1 && setter) cls = 'cfg-v';
			else if (/^-?(\d+\.?\d*|\.\d+)$/.test(tok)) cls = 'cfg-n';
			out += cls ? span(cls, tok) : escapeHtml(tok);
		}
		i = j;
		word++;
	}
	return out;
}
// the text of a field into the layer under it, with the colors of a config or plain
function codePaint(textId, hiId, colored) {
	// the line after the last line break has to be there too, or the last line of the field has nothing under it
	$(hiId).innerHTML = $(textId).value.split('\n').map(colored ? cfgLine : escapeHtml).join('\n') + '\n';
	codeScroll(textId, hiId);
}
function codeScroll(textId, hiId) {
	$(hiId).scrollTop = $(textId).scrollTop;
	$(hiId).scrollLeft = $(textId).scrollLeft;
}
function cfgPaint() { codePaint('cfgText', 'cfgHi', true); }
function cfgScroll() { codeScroll('cfgText', 'cfgHi'); }
function cfgShowState() {
	const text = $('cfgText').value, dirty = cfgLoaded && text !== cfgSaved;
	cfgPaint();
	$('cfgSize').textContent = cfgLoaded ? cfgBytes(text) + ' bytes' : '';
	$('cfgState').textContent = !cfgLoaded ? 'not loaded' : dirty ? 'changed, not saved' : 'as on the server';
	$('cfgState').classList.toggle('dirty', dirty);
}
async function loadConfig() {
	try {
		const res = await fetch('api/config', { cache: 'no-store' });
		if (!res.ok) throw new Error('HTTP ' + res.status);
		cfgSaved = $('cfgText').value = await res.text();
		cfgLoaded = true;
	} catch (e) {
		toast('Could not read the config: ' + e.message, false);
	}
	cfgShowState();
}
async function saveConfig(run) {
	// the server stores the lines with LF only
	const text = $('cfgText').value.replace(/\r/g, '');
	if (!cfgLoaded) { toast('The config was never loaded, nothing to save', false); return; }
	if (cfgBytes(text) > CFG_MAX_BYTES) { toast('The config is larger than ' + CFG_MAX_BYTES + ' bytes, the server does not take it', false); return; }
	if (run && !confirm('Save server.cfg and execute it on the server now?')) return;
	try {
		const res = await fetch('api/config' + (run ? '?exec=1' : ''), { method: 'POST', body: text });
		const json = await res.json();
		toast(json.message, json.ok);
		if (json.ok) cfgSaved = text;
	} catch (e) {
		toast('Request failed: ' + e.message, false);
	}
	cfgShowState();
}
$('cfgText').addEventListener('input', cfgShowState);
$('cfgText').addEventListener('scroll', cfgScroll);
$('cfgSave').addEventListener('click', () => saveConfig(false));
$('cfgRun').addEventListener('click', () => saveConfig(true));
$('cfgLoad').addEventListener('click', () => {
	if ($('cfgText').value === cfgSaved || confirm('Drop what was changed here and read the file again?')) loadConfig();
});
window.addEventListener('beforeunload', e => {
	if ((cfgLoaded && $('cfgText').value !== cfgSaved) || (fsEdit && $('fsText').value !== fsEdit.saved)) e.preventDefault();
});

