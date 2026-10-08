/* ------------------------------------------------------------------ */
/* the game's HUD, from ui/hud.menu and CG_DrawHUD                     */
/* ------------------------------------------------------------------ */

const hudImages = new Map();
function hudImage(name) {
	if (hudImages.has(name)) return hudImages.get(name).img;
	const entry = { img: null };
	hudImages.set(name, entry);
	(async () => {
		for (const ext of ['.tga', '.png', '.jpg']) {
			try {
				const data = await fetchGameFile(name + ext);
				if (ext === '.tga') {
					const t = decodeTga(data);
					if (!t) continue;
					entry.img = await createImageBitmap(new ImageData(new Uint8ClampedArray(t.data.buffer), t.w, t.h));
				} else {
					entry.img = await createImageBitmap(new Blob([data]));
				}
				return;
			} catch (e) { /* next extension */ }
		}
	})();
	return null;
}

// the number glyphs are white, R_SetColor tints them
const tinted = new Map();
function tintedImage(name, color) {
	const key = name + color.join(',');
	if (tinted.has(key)) return tinted.get(key);
	const img = hudImage(name);
	if (!img) return null;
	const c = document.createElement('canvas');
	c.width = img.width; c.height = img.height;
	const x = c.getContext('2d');
	x.drawImage(img, 0, 0);
	x.globalCompositeOperation = 'multiply';
	x.fillStyle = 'rgb(' + color.map(v => Math.round(v * 255)).join(',') + ')';
	x.fillRect(0, 0, c.width, c.height);
	x.globalCompositeOperation = 'destination-in';
	x.drawImage(img, 0, 0);
	tinted.set(key, c);
	return c;
}

const HUD = {
	healthTics: [[20, 24, 28, 28], [38, 33, 28, 28], [52, 47, 28, 28], [60, 65, 28, 28]],
	armorTics: [[9, -6, 56, 56], [34, 3, 56, 56], [54, 24, 56, 56], [63, 48, 56, 56]],
	forceTics: [[-65, -5, 56, 56], [-89, 4, 56, 56], [-109, 24, 56, 56], [-119, 48, 56, 56]],
	ammoTics: [[-48, 25, 28, 28], [-66, 33, 28, 28], [-80, 47, 28, 28], [-88, 65, 28, 28]],
	saberStyles: { fast: [-85, 69, 24, 24, 'saber_fast'], medium: [-70, 43, 26, 26, 'saber_med'], strong: [-49, 24, 26, 26, 'saber_strong'] },
};
const DIGITS = ['t_zero', 't_one', 't_two', 't_three', 't_four', 't_five', 't_six', 't_seven', 't_eight', 't_nine', 't_minus'];

function drawHud(d) {
	const c = $('hudCanvas'), dpr = Math.min(window.devicePixelRatio || 1, 2);
	const W = Math.round(c.clientWidth * dpr), H = Math.round(c.clientHeight * dpr);
	if (c.width !== W || c.height !== H) { c.width = W; c.height = H; }
	const g = c.getContext('2d');
	g.clearRect(0, 0, W, H);
	if (!d || d.dead || d.team === 'spectator') return;

	// 640x480 virtual screen: left HUD sticks to the left edge, right HUD to the right edge
	const s = H / 480;
	const left = (x, y) => [x * s, y * s];
	const right = (x, y) => [W - (640 - x) * s, y * s];
	const pic = (pos, x, y, w, h, name, alpha = 1) => {
		const img = hudImage(name);
		if (!img) return;
		const [px, py] = pos(x, y);
		g.globalAlpha = alpha;
		if (w < 0) {
			g.save();
			g.translate(px, py);
			g.scale(-1, 1);
			g.drawImage(img, 0, 0, -w * s, h * s);
			g.restore();
		} else {
			g.drawImage(img, px, py, w * s, h * s);
		}
		g.globalAlpha = 1;
	};
	// CG_DrawNumField with NUM_FONT_SMALL, 3 digits right aligned
	const num = (pos, x, y, value, color) => {
		let str = String(Math.max(-99, Math.min(999, Math.round(value))));
		x += 2 + 6 * (3 - str.length);
		for (const ch of str) {
			const img = tintedImage('gfx/2d/numbers/' + DIGITS[ch === '-' ? 10 : +ch], color);
			if (img) { const [px, py] = pos(x, y); g.drawImage(img, px, py, 6 * s, 12 * s); }
			x += 2 + 6;
		}
	};
	// tics fill from the last one, the partial one fades out
	const tics = (pos, ox, oy, rects, prefix, value, max) => {
		const inc = max / 4;
		let cur = Math.min(value, max);
		for (let i = 3; i >= 0; i--) {
			if (cur <= 0) break;
			const r = rects[i];
			pic(pos, ox + r[0], oy + r[1], r[2], r[3], 'gfx/hud/' + prefix + (i + 1), cur < inc ? cur / inc : 1);
			cur -= inc;
		}
	};

	// lefthud at 0 368
	pic(left, 0, 368, 112, 112, 'gfx/hud/hudleft');
	tics(left, 0, 368, HUD.armorTics, 'armor_tic_', d.armor, d.maxHealth || 100);
	num(left, 85, 368 + 98, d.armor, [0, 0.613, 0.097]);
	tics(left, 0, 368, HUD.healthTics, 'health_tic_', Math.max(0, d.health), d.maxHealth || 100);
	num(left, 59, 368 + 98, d.health, [0.835, 0.015, 0.015]);

	// righthud at 640 368, the frame is the left one mirrored
	g.font = 'bold ' + Math.round(13 * s) + 'px -apple-system, Helvetica, Arial, sans-serif';
	g.textAlign = 'right';
	g.textBaseline = 'top';
	const [sx, sy] = right(640 - 150, 368 + 92);
	g.fillStyle = 'rgba(0,0,0,.8)';
	g.fillText('Score: ' + d.score, sx + s, sy + s);
	g.fillStyle = '#fff';
	g.fillText('Score: ' + d.score, sx, sy);
	pic(right, 640, 368, -112, 112, 'gfx/hud/hudleft');
	if (d.weapon === 3) {
		// one icon for the saber style, like CG_DrawSaberStyle
		const style = { 1: 'fast', 5: 'fast', 2: 'medium', 6: 'medium', 7: 'medium', 3: 'strong', 4: 'strong' }[d.saberStyle];
		if (style) { const r = HUD.saberStyles[style]; pic(right, 640 + r[0], 368 + r[1], r[2], r[3], 'gfx/hud/' + r[4]); }
	} else if (d.ammo >= 0 && d.weapon > 0) {
		if (d.ammoInfinite) {
			g.fillStyle = 'rgb(255,168,16)';
			g.textAlign = 'left';
			const [ix, iy] = right(640 - 75, 368 + 87);
			g.fillText('--', ix, iy);
			tics(right, 640, 368, HUD.ammoTics, 'ammo_tic_', 8, 8);
		} else {
			num(right, 640 - 83, 368 + 98, d.ammo, d.ammo > 0 ? [1, 0.658, 0.062] : [1, 0, 0]);
			tics(right, 640, 368, HUD.ammoTics, 'ammo_tic_', d.ammo, d.maxAmmo || 1);
		}
	}
	tics(right, 640, 368, HUD.forceTics, 'force_tic_', d.forcePower, 100);
	num(right, 640 - 109, 368 + 98, d.forcePower, [0.359, 0.524, 0.722]);

	// cg_drawCrosshair 1, cg_crosshairSize 24, in the middle of the 3D view
	const ch = hudImage('gfx/2d/crosshaira');
	if (ch) {
		const size = 24 * s;
		g.drawImage(ch, (W - size) / 2, (H - size) / 2, size, size);
	}
}

