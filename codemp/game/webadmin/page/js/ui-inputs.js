/* ------------------------------------------------------------------ */
/* the fields of the page: keys, focus, the wheel                      */
/* ------------------------------------------------------------------ */

// The keys of the map (js/input.js) are dropped while the focus is on a field that is typed into. A box to tick,
// a button or a slider is not one: W A S D move the camera with the focus on it. A slider keeps the keys that
// move it.
const UI_NOT_TYPED = new Set(['checkbox', 'radio', 'range', 'button', 'submit', 'reset', 'image', 'file', 'color']);
const UI_RANGE_KEYS = new Set(['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home', 'End', 'PageUp', 'PageDown']);
function uiKeyIsTyped(e) {
	const el = e.target, tag = el.tagName;
	if (tag === 'TEXTAREA' || tag === 'SELECT' || el.isContentEditable) return true;
	if (tag !== 'INPUT') return false;
	if (el.type === 'range') return UI_RANGE_KEYS.has(e.key);
	return !UI_NOT_TYPED.has(el.type);
}

// A box that was ticked and a list that was chosen from keep the focus, and a list takes every key then: they
// give it back once they have changed. A slider gives it back when the mouse lets go of it, not when it changes,
// or the arrow keys would move it one step only.
document.addEventListener('change', e => {
	const el = e.target;
	if (el.tagName === 'SELECT' || (el.tagName === 'INPUT' && (el.type === 'checkbox' || el.type === 'radio'))) el.blur();
});
// (the slider that has the focus, not the target: the mouse may let go beside it)
document.addEventListener('pointerup', () => {
	const el = document.activeElement;
	if (el && el.tagName === 'INPUT' && el.type === 'range') el.blur();
});

// ---- the wheel over a number field or a slider

// the panels and lists a field can be in that scroll by themselves
const UI_SCROLLERS = '#entPanel, #command, .scroll';
function uiInScroller(el) {
	for (let box = el.closest(UI_SCROLLERS); box; box = box.parentElement && box.parentElement.closest(UI_SCROLLERS)) {
		if (box.scrollHeight > box.clientHeight) return true;
	}
	return false;
}
// how many digits a number is written with behind its point
function uiDecimals(text) {
	const frac = /\.(\d+)/.exec(text), exp = /e([+-]?\d+)/i.exec(text);
	return Math.max(0, (frac ? frac[1].length : 0) - (exp ? +exp[1] : 0));
}
function uiLimit(text, none) { return text === '' || isNaN(+text) ? none : +text; }

// A notch is one step of the field, five with Shift, and up is more. The value stays between min and max and is
// written with the digits of the step, or 0.1 three times would be 0.30000000000000004. The handlers of the
// field run as if the value had been typed: input at once, change when the wheel rests, for a field of the
// entity panel sends its entity to the server with every change. Where the panel around the field has something
// to scroll, the wheel is the panel's until the field has the focus. Not with Ctrl, which is the zoom of the
// browser.
const uiWheelRest = new WeakMap();	// field -> the timer of its change
document.addEventListener('wheel', e => {
	const over = e.target;
	if (over.tagName !== 'INPUT' || (over.type !== 'number' && over.type !== 'range') || e.ctrlKey || e.metaKey) return;
	// a slider that only shows the value of a field names it, and the wheel turns that field
	const el = (over.dataset.wheel && $(over.dataset.wheel)) || over;
	if (over.disabled || el.disabled || el.readOnly) return;
	// with Shift some platforms turn the wheel sideways
	const turn = e.deltaY || (e.shiftKey ? e.deltaX : 0);
	if (!turn) return;
	if (uiInScroller(over) && document.activeElement !== over && document.activeElement !== el) return;
	e.preventDefault();
	const stepText = el.getAttribute('step') || '', step = +stepText > 0 ? +stepText : 1, was = el.value;
	// an empty field starts from what it shows as its hint
	const from = was !== '' ? was : el.placeholder.trim() !== '' && isFinite(+el.placeholder) ? el.placeholder : '0';
	const value = +from + (turn < 0 ? 1 : -1) * (e.shiftKey ? 5 : 1) * step;
	const digits = Math.min(12, Math.max(uiDecimals(stepText), uiDecimals(from)));
	el.value = String(+Math.min(uiLimit(el.max, Infinity), Math.max(uiLimit(el.min, -Infinity), value)).toFixed(digits));
	if (el.value === was) return;
	el.dispatchEvent(new Event('input', { bubbles: true }));
	clearTimeout(uiWheelRest.get(el));
	uiWheelRest.set(el, setTimeout(() => el.dispatchEvent(new Event('change', { bubbles: true })), 250));
}, { passive: false });
