/* ------------------------------------------------------------------ */
/* the fields of the page: which keys are theirs, who keeps the focus  */
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
