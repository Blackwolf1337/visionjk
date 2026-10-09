/* ------------------------------------------------------------------ */
/* the 2D of a client: CG_Draw2D of cgame/cg_draw.c and what it calls  */
/* ------------------------------------------------------------------ */

// The game draws its 2D with the renderer: pictures with a shader of the game in a screen of 640 x 480 that is
// stretched over the whole picture, whatever its shape (RB_SetGL2D, this fork has no correction for a wide
// picture). So it is drawn into the 3D canvas here too, after the scene, with the stages of the script renderer
// (drawStage): a shader that adds to the picture or multiplies it (the zoom mask, the tints) can not be laid
// over it as a canvas of its own. The canvas #hudCanvas of the page stays empty.

// ---- the 2D of the renderer (RE_SetColor, RE_StretchPic, RE_RotatePic2 of rd-vanilla/tr_cmds.cpp and their
// RB_ functions in tr_backend.cpp)

// qglOrtho( 0, 640, 480, 0, 0, 1 ) of RB_SetGL2D
const HUD_ORTHO = new Float32Array([2 / 640, 0, 0, 0, 0, -2 / 480, 0, 0, 0, 0, -2, 0, -1, 1, -1, 1]);
const HUD_MAX_QUADS = 4096, HUD_STRIDE = 24;	// a vertex as the effects have it: place, texture coordinates, colour
const hud2d = {
	data: new ArrayBuffer(HUD_MAX_QUADS * 4 * HUD_STRIDE), verts: 0, batches: [], color: new Uint8Array([255, 255, 255, 255]),
	vao: gl.createVertexArray(), vbo: gl.createBuffer(),
	// for the checks: with record on, every picture of a frame is listed in pics
	record: false, pics: [], frames: 0
};
hud2d.floats = new Float32Array(hud2d.data);
hud2d.bytes = new Uint8Array(hud2d.data);
gl.bindVertexArray(hud2d.vao);
gl.bindBuffer(gl.ARRAY_BUFFER, hud2d.vbo);
gl.bufferData(gl.ARRAY_BUFFER, hud2d.data.byteLength, gl.DYNAMIC_DRAW);
gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, HUD_STRIDE, 0);
gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, HUD_STRIDE, 12);
gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 4, gl.UNSIGNED_BYTE, true, HUD_STRIDE, 20);
// the two triangles of a picture as RB_StretchPic numbers them: 3 0 2 and 2 0 1
gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer());
gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, Uint16Array.from({ length: HUD_MAX_QUADS * 6 }, (_, i) => (i / 6 | 0) * 4 + [3, 0, 2, 2, 0, 1][i % 6]), gl.STATIC_DRAW);
gl.bindVertexArray(null);

// Where a place of the 640 x 480 screen is in a picture of w x h pixels, by the projection the 2D is drawn with
function hud2dToPixels(x, y, w, h) {
	const m = HUD_ORTHO, nx = m[0] * x + m[12], ny = m[5] * y + m[13];
	return [(nx * 0.5 + 0.5) * w, (0.5 - ny * 0.5) * h];
}

// RE_SetColor and RB_SetColor: null is white. The colour of the 2D is four bytes, a float times 255 cut off.
function hudSetColor(rgba) {
	const c = hud2d.color;
	if (!rgba) { c[0] = c[1] = c[2] = c[3] = 255; return; }
	for (let i = 0; i < 4; i++) c[i] = Math.max(0, Math.min(255, Math.trunc(hudF32(rgba[i]) * 255)));
}

function hud2dVertex(x, y, s, t) {
	const o = hud2d.verts * 6, c = hud2d.verts * HUD_STRIDE + 20, f = hud2d.floats, b = hud2d.bytes, col = hud2d.color;
	f[o] = x; f[o + 1] = y; f[o + 2] = 0; f[o + 3] = s; f[o + 4] = t;
	b[c] = col[0]; b[c + 1] = col[1]; b[c + 2] = col[2]; b[c + 3] = col[3];
	hud2d.verts++;
}

// a picture more for the shader that is drawn, or the first one of another shader (RB_BeginSurface)
function hud2dQuad(shader, x, y, w, h) {
	if (!shader || shader.state !== 1 || hud2d.verts + 4 > HUD_MAX_QUADS * 4) return false;
	const last = hud2d.batches[hud2d.batches.length - 1];
	if (last && last.shader === shader) last.count += 6; else hud2d.batches.push({ shader, first: hud2d.verts / 4 * 6, count: 6 });
	if (hud2d.record) hud2d.pics.push({ name: shader.name, x, y, w, h, color: Array.from(hud2d.color) });
	return true;
}

// RE_StretchPic. A shader that is not there (yet) draws nothing.
function hudStretchPic(x, y, w, h, s1, t1, s2, t2, shader) {
	x = hudF32(x); y = hudF32(y); w = hudF32(w); h = hudF32(h);
	if (!hud2dQuad(shader, x, y, w, h)) return;
	hud2dVertex(x, y, s1, t1);
	hud2dVertex(x + w, y, s2, t1);
	hud2dVertex(x + w, y + h, s2, t2);
	hud2dVertex(x, y + h, s1, t2);
}

// RE_RotatePic2: a picture turned by a degrees around its middle, which is at x y
function hudRotatePic2(x, y, w, h, s1, t1, s2, t2, a, shader) {
	if (!hud2dQuad(shader, x, y, w, h)) return;
	const angle = a * Math.PI / 180, s = Math.sin(angle), c = Math.cos(angle), hw = w * 0.5, hh = h * 0.5;
	const at = (px, py, u, v) => hud2dVertex(c * px - s * py + x, s * px + c * py + y, u, v);
	at(-hw, -hh, s1, t1); at(hw, -hh, s2, t1); at(hw, hh, s2, t2); at(-hw, hh, s1, t2);
}

// what was gathered of a frame drawn (RB_EndSurface for each run of pictures with one shader)
function hud2dFlush() {
	const batches = hud2d.batches;
	if (!batches.length) { hud2d.verts = 0; return; }
	// the time of the shaders of the 2D is the clock of the engine, not the time of the level (RB_SetGL2D)
	const t = frameNow / 1000, program = gl.getParameter(gl.CURRENT_PROGRAM);
	gl.useProgram(stageProg.p);
	gl.uniform1i(stageProg.u.uTex, 0);
	gl.uniform3f(stageProg.u.uEye, 320, 240, 1);
	gl.uniformMatrix4fv(stageProg.u.uMvp, false, HUD_ORTHO);
	gl.uniform1i(stageProg.u.uSprite, 0);
	stageBegin(stageProg, null);
	gl.viewport(0, 0, canvas.width, canvas.height);
	// the alpha of the canvas stays 1 whatever a stage blends
	gl.colorMask(true, true, true, false);
	gl.bindBuffer(gl.ARRAY_BUFFER, hud2d.vbo);
	gl.bufferSubData(gl.ARRAY_BUFFER, 0, hud2d.bytes, 0, hud2d.verts * HUD_STRIDE);
	// no side is left out in 2D (GL_Cull), and everything is in front: the depth is 0
	gl.disable(gl.CULL_FACE);
	gl.disable(gl.DEPTH_TEST);
	for (const b of batches) {
		const mat = b.shader.mat;
		setDeform(stageProg, mat, t);
		for (const st of mat.stages) drawStage(st, t, null, hud2d.vao, b.count, gl.UNSIGNED_SHORT, b.first * 2, stageProg, null);
	}
	setDeform(stageProg, NO_DEFORM, 0);
	gl.bindVertexArray(null);
	gl.enable(gl.DEPTH_TEST);
	gl.disable(gl.BLEND);
	gl.depthMask(true);
	gl.depthFunc(gl.LESS);
	gl.colorMask(true, true, true, true);
	gl.useProgram(program);
	batches.length = 0;
	hud2d.verts = 0;
	hud2d.frames++;
}

// ---- the shaders of the 2D (RE_RegisterShaderNoMip and RE_RegisterShader: R_FindShader with LIGHTMAP_2D)

// A name with a script: its stages (materialFor as for an effect: the colour and the alpha of the vertices where
// a stage asks for them). A name without: the image of that name, blended by its alpha, with the colour of the
// 2D. mip false is RE_RegisterShaderNoMip: such an image has no mipmaps and does not repeat (GL_CLAMP).
// state: 0 on its way, 1 there, 2 there is none (the engine draws its default image then, the page nothing).
const hudShaders = new Map(), hudShaderNames = new Map(), hudUnsettled = [];
let hudShaderMap = '';

function hudShader(name, mip = false) {
	let h = hudShaderNames.get(name);
	if (h) return h;
	const key = scKey(name);
	h = hudShaders.get(key);
	if (!h) {
		h = { name: key, mat: null, state: 0, mip: !!mip, script: false };
		hudShaders.set(key, h);
		hudUnsettled.push(h);
	}
	hudShaderNames.set(name, h);
	return h;
}

// the stage R_FindShader makes for an image of the 2D that has no script
function hudImageMaterial(tex) {
	const st = finishStage(Object.assign(newStage(), { tex, rgbGen: CGEN.vertex, alphaGen: AGEN.vertex, src: gl.SRC_ALPHA, dst: gl.ONE_MINUS_SRC_ALPHA }));
	const mat = { stages: [st], nodraw: false, polygonOffset: false, sky: null, fog: null, contentsFog: false, cull: 0, deforms: [], portalRange: 0, sort: 0, fogPass: 0, order: 0 };
	finishMaterial(mat, null);
	return mat;
}

// An image for the 2D. With mipmaps it is an image like any other of the game (fetchImage), without it is made
// here: the endings in the order of the engine, as it is in the file.
async function hudLoadImage(name, mip) {
	if (mip) return fetchImage(name, false);
	const m = /\.(tga|jpg|png)$/i.exec(name), base = m ? name.slice(0, m.index) : name;
	for (const ext of [m ? m[0].toLowerCase() : '', '.jpg', '.png', '.tga'].filter((e, i, all) => e && all.indexOf(e) === i)) {
		let data;
		try {
			const res = await fetch('api/file?name=' + encodeURIComponent(base + ext));
			if (!res.ok) continue;
			data = await res.arrayBuffer();
		} catch (e) { continue; }
		try {
			const opts = { clamp: true, mip: false };
			if (ext === '.tga') {
				const img = decodeTga(data);
				if (img) return { tex: makeTexture(img.w, img.h, img.data, opts), w: img.w, h: img.h };
			} else {
				const img = await createImageBitmap(new Blob([data]), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' }), tex = gl.createTexture();
				gl.bindTexture(gl.TEXTURE_2D, tex);
				gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, img);
				finishTexture(img.width, img.height, opts);
				return { tex, w: img.width, h: img.height };
			}
		} catch (e) { /* a file that is no image: the next ending */ }
	}
	return null;
}

// Once the shader scripts are there (or there are none to come) every shader that was asked for is settled. The
// materials are those of a map (loadMap lets them go): after another map came the ones with a script are made anew.
function hudSettleShaders() {
	if (scripts.state < 2) return false;
	if (hudShaderMap !== loadedMap) {
		hudShaderMap = loadedMap;
		for (const h of hudShaders.values()) if (h.script) { h.state = 0; h.mat = null; hudUnsettled.push(h); }
	}
	while (hudUnsettled.length) {
		const h = hudUnsettled.pop();
		if (h.state) continue;
		if (scripts.state === 2 && scripts.bodies.has(h.name)) {
			h.script = true;
			h.mat = materialFor(h.name, 'fx');
			h.state = 1;
		} else {
			h.state = -1;	// on its way
			hudBusy(hudLoadImage(h.name, h.mip).then(img => {
				if (img) { h.mat = hudImageMaterial(img.tex); h.w = img.w; h.h = img.h; h.state = 1; } else h.state = 2;
			}, () => { h.state = 2; }));
		}
	}
	return true;
}

// CG_DrawPic, CG_FillRect and CG_DrawRect (with CG_DrawSides and CG_DrawTopBottom) of cg_drawtools.c. The sides of
// a frame take their width times cgs.screenXScale: that is what the code of the game does.
const hudPic = (x, y, w, h, shader) => hudStretchPic(x, y, w, h, 0, 0, 1, 1, shader);
function hudFillRect(x, y, w, h, color) {
	hudSetColor(color);
	hudStretchPic(x, y, w, h, 0, 0, 0, 0, hudShader('white', true));
	hudSetColor(null);
}
function hudDrawRect(x, y, w, h, size, color) {
	const white = hudShader('white', true), sx = size * (canvas.width / 640), sy = size * (canvas.height / 480);
	hudSetColor(color);
	hudStretchPic(x, y, w, sy, 0, 0, 0, 0, white);
	hudStretchPic(x, y + h - sy, w, sy, 0, 0, 0, 0, white);
	hudStretchPic(x, y, sx, h, 0, 0, 0, 0, white);
	hudStretchPic(x + w - sx, y, sx, h, 0, 0, 0, 0, white);
	hudSetColor(null);
}

// The media of the client game (cgs.media, CG_RegisterGraphics) are asked for by name where they are drawn, so
// that only what a frame shows is fetched. The digits: the large ones are registered with mipmaps first, and that
// is the shader the second call finds; the thick ones have the minus of the small ones.
const HUD_NUMBERS = ['zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'minus'];
let hudMediaSet = null;
function hudMedia() {
	return hudMediaSet || (hudMediaSet = { charset: hudShader('gfx/2d/charsgrid_med') });
}

// ---- the menu files of the HUD (CG_LoadHudMenu, CG_LoadMenus, CG_ParseMenu, CG_Asset_Parse of cg_main.c and
// Menu_Parse, Item_Parse, Menu_PostParse of ui/ui_shared.c)

// What the client game takes from them: the fonts of the assetGlobalDef, and for the menus it looks up by name
// (Menus_FindByName, Menu_FindItemByName) the place, size, colour and image of their items. Menu_Paint paints
// none of the items of the HUD: they have no style and no border (Window_Paint), and no text.
const hudMenus = { state: 0, byName: new Map() };

function hudMenuTokens(text) {
	const out = [], re = /\/\/[^\n]*|\/\*[\s\S]*?\*\/|"([^"\n]*)"|[{}]|[^\s{}"]+/g;
	for (let m; (m = re.exec(text)); ) {
		if (m[0][0] === '/' && (m[0][1] === '/' || m[0][1] === '*')) continue;
		out.push(m[1] !== undefined ? { s: m[1], quoted: true } : { s: m[0], quoted: false });
	}
	return out;
}

// a keyword and what belongs to it: the word after it, and the numbers that follow that
function hudMenuKey(tok, i) {
	const key = tok[i++].s.toLowerCase(), args = [];
	// an item: its block is read by the one who asked
	if (key === 'itemdef') return [key, args, i];
	if (i < tok.length && tok[i].s === '{' && !tok[i].quoted) {
		// a script in braces: skipped
		for (let depth = 0; i < tok.length; i++) { if (!tok[i].quoted && tok[i].s === '{') depth++; else if (!tok[i].quoted && tok[i].s === '}' && --depth === 0) { i++; break; } }
		return [key, args, i];
	}
	if (i < tok.length && (tok[i].quoted || !/^[{}]$/.test(tok[i].s))) args.push(tok[i++].s);
	while (i < tok.length && !tok[i].quoted && /^[-+]?(\d+\.?\d*|\.\d+)$/.test(tok[i].s)) args.push(tok[i++].s);
	return [key, args, i];
}

function hudParseMenuFile(text) {
	const tok = hudMenuTokens(text), num = a => a.map(v => parseFloat(v) || 0);
	const block = (i, each) => {
		if (!tok[i] || tok[i].s !== '{') return i;
		for (i++; i < tok.length && !(tok[i].s === '}' && !tok[i].quoted); ) {
			const [key, args, next] = hudMenuKey(tok, i);
			i = each(key, args, next);
		}
		return i + 1;
	};
	for (let i = 0; i < tok.length; ) {
		const word = tok[i++].s.toLowerCase();
		if (word === '}') break;
		if (word === 'assetglobaldef') {
			i = block(i, (key, args, next) => {
				if (key === 'font') hudAssets.qhMediumFont = hudRegisterFont(args[0]);
				else if (key === 'smallfont') hudAssets.qhSmallFont = hudRegisterFont(args[0]);
				else if (key === 'small2font') hudAssets.qhSmall2Font = hudRegisterFont(args[0]);
				else if (key === 'bigfont') hudAssets.qhBigFont = hudRegisterFont(args[0]);
				return next;
			});
		} else if (word === 'menudef') {
			// Window_Init: the colour of a window is white until the file says another
			const menu = { name: '', rect: [0, 0, 0, 0], fullScreen: false, visible: false, items: [] };
			i = block(i, (key, args, next) => {
				if (key === 'name') menu.name = args[0];
				else if (key === 'rect') menu.rect = num(args).slice(0, 4);
				else if (key === 'fullscreen') menu.fullScreen = !!parseInt(args[0]);
				else if (key === 'visible') menu.visible = !!parseInt(args[0]);
				else if (key === 'itemdef') {
					const item = { name: '', group: '', rectClient: [0, 0, 0, 0], rect: [0, 0, 0, 0], foreColor: [1, 1, 1, 1], background: '', visible: false };
					next = block(next, (k, a, n) => {
						if (k === 'name') item.name = a[0];
						else if (k === 'group') item.group = a[0];
						else if (k === 'rect') item.rectClient = num(a).slice(0, 4);
						else if (k === 'forecolor') item.foreColor = num(a).slice(0, 4);
						else if (k === 'background') item.background = a[0];
						else if (k === 'visible') item.visible = !!parseInt(a[0]);
						return n;
					});
					menu.items.push(item);
				}
				return next;
			});
			// Menu_PostParse and Item_SetScreenCoords: an item is placed from the corner of its menu
			if (menu.fullScreen) menu.rect = [0, 0, 640, 480];
			for (const it of menu.items) it.rect = [menu.rect[0] + it.rectClient[0], menu.rect[1] + it.rectClient[1], it.rectClient[2], it.rectClient[3]];
			const key = menu.name.toLowerCase();
			if (!hudMenus.byName.has(key)) hudMenus.byName.set(key, menu);
		}
	}
}

function hudLoadMenus() {
	if (hudMenus.state) return;
	hudMenus.state = 1;
	// CG_Init: the fonts the client game has before the menu files name theirs
	hudAssets.qhSmallFont = hudRegisterFont('ocr_a');
	hudAssets.qhMediumFont = hudRegisterFont('ergoec');
	hudAssets.qhBigFont = hudAssets.qhMediumFont;
	hudBusy((async () => {
		// cg_hudFiles is "ui/jahud.txt": the list of the menu files, loadMenu { "file" }
		const tok = hudMenuTokens(await fetchGameFile('ui/jahud.txt', true));
		for (let i = 0; i < tok.length; i++) {
			if (tok[i].s.toLowerCase() !== 'loadmenu' || !tok[i + 1] || tok[i + 1].s !== '{') continue;
			for (i += 2; i < tok.length && tok[i].s !== '}'; i++) hudParseMenuFile(await fetchGameFile(tok[i].s, true));
		}
		hudMenus.state = 2;
	})().catch(() => { hudMenus.state = 3; }));
}

// Menus_FindByName and Menu_FindItemByName (the first of that name, whatever its case)
const hudFindMenu = name => hudMenus.byName.get(name.toLowerCase()) || null;
function hudFindItem(menu, name) {
	if (!menu) return null;
	name = name.toLowerCase();
	return menu.items.find(it => it.name.toLowerCase() === name) || null;
}
// an item drawn as the client game draws one: its image in its rectangle (a width below 0 mirrors it)
const hudItemPic = it => hudPic(it.rect[0], it.rect[1], it.rect[2], it.rect[3], it.background ? hudShader(it.background) : null);

// ---- what a client has of the player it looks through the eyes of

const HUD_STAT = { HEALTH: 0, HOLDABLE_ITEM: 1, HOLDABLE_ITEMS: 2, PERSISTANT_POWERUP: 3, WEAPONS: 4, ARMOR: 5, DEAD_YAW: 6, CLIENTS_READY: 7, MAX_HEALTH: 8 };
const HUD_PERS = { SCORE: 0, HITS: 1, RANK: 2, TEAM: 3, SPAWN_COUNT: 4, PLAYEREVENTS: 5, ATTACKER: 6 };
const HUD_PM_SPECTATOR = 4, HUD_PM_INTERMISSION = 7, HUD_PMF_FOLLOW = 4096;
const HUD_TEAM_FREE = 0, HUD_TEAM_RED = 1, HUD_TEAM_BLUE = 2, HUD_TEAM_SPECTATOR = 3;
const HUD_GT = { FFA: 0, HOLOCRON: 1, JEDIMASTER: 2, DUEL: 3, POWERDUEL: 4, SINGLE_PLAYER: 5, TEAM: 6, SIEGE: 7, CTF: 8, CTY: 9 };
const HUD_EF_ALT_DIM = 1 << 15, HUD_EF_DOUBLE_AMMO = 1 << 20;
const HUD_WP = { NONE: 0, STUN_BATON: 1, MELEE: 2, SABER: 3, BRYAR_PISTOL: 4, BLASTER: 5, DISRUPTOR: 6, BOWCASTER: 7, REPEATER: 8, DEMP2: 9, FLECHETTE: 10, ROCKET_LAUNCHER: 11, THERMAL: 12, TRIP_MINE: 13, DET_PACK: 14, CONCUSSION: 15, BRYAR_OLD: 16, EMPLACED_GUN: 17, TURRET: 18, NUM: 19 };
const HUD_LAST_USEABLE_WEAPON = 16;
const HUD_WEAPON_FIRING = 3, HUD_WEAPON_CHARGING_ALT = 5;
const HUD_FP = { HEAL: 0, LEVITATION: 1, SPEED: 2, PUSH: 3, PULL: 4, TELEPATHY: 5, GRIP: 6, LIGHTNING: 7, RAGE: 8, PROTECT: 9, ABSORB: 10, TEAM_HEAL: 11, TEAM_FORCE: 12, DRAIN: 13, SEE: 14, SABER_OFFENSE: 15, SABER_DEFENSE: 16, SABERTHROW: 17, NUM: 18 };
const HUD_PW = { REDFLAG: 4, BLUEFLAG: 5, NEUTRALFLAG: 6, YSALAMIRI: 15, NUM: 16 };
const HUD_ENTITYNUM_NONE = 1023, HUD_ENTITYNUM_WORLD = 1022, HUD_MAX_CLIENTS = 32;
// weaponData and ammoData of game/bg_weapons.c, by the number of the weapon: [ammoIndex, energyPerShot, altEnergyPerShot]
const HUD_WEAPON_DATA = [[0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0], [2, 0, 0], [2, 2, 3], [3, 5, 6], [3, 5, 5], [4, 1, 15], [3, 8, 6], [4, 10, 15], [5, 1, 2], [7, 1, 1], [8, 1, 1], [9, 1, 0], [4, 40, 50], [2, 2, 2], [0, 0, 0], [0, 0, 0]];
const HUD_AMMO_MAX = [0, 100, 300, 300, 300, 25, 800, 10, 10, 10];
// colorTable of q_color.c, the entries the 2D uses
const HUD_CT = { BLACK: [0, 0, 0, 1], RED: [1, 0, 0, 1], GREEN: [0, 1, 0, 1], BLUE: [0, 0, 1, 1], YELLOW: [1, 1, 0, 1], WHITE: [1, 1, 1, 1], LTGREY: [0.75, 0.75, 0.75, 1],
	HUD_GREEN: [0, 0.613, 0.097, 1], HUD_RED: [0.835, 0.015, 0.015, 1], ICON_BLUE: [0.567, 0.685, 1, 0.75], HUD_ORANGE: [1, 0.658, 0.062, 1] };

// What the client game keeps from frame to frame by itself (fields of cg and the statics of cg_draw.c)
const hudLocal = { client: -1, since: 0, HUDTickFlashTime: 0, HUDArmorFlag: false, oldammo: 0, oldAmmoTime: 0,
	forceHUDTotalFlashTime: 0, forceHUDNextFlashTime: 0, forceHUDActive: true,
	weaponSelect: 0, weaponSelectTime: 0, spawnCount: -1, itemPickupTime: 0,
	crosshairClientNum: HUD_ENTITYNUM_NONE, crosshairClientTime: 0,
	rageTime: 0, rageFadeTime: 0, rageFadeVal: 0, rageRecTime: 0, rageRecFadeTime: 0, rageRecFadeVal: 0,
	absorbTime: 0, absorbFadeTime: 0, absorbFadeVal: 0, protectTime: 0, protectFadeTime: 0, protectFadeVal: 0, ysalTime: 0, ysalFadeTime: 0, ysalFadeVal: 0 };

// The page looks through the eyes of a player as a client that follows him does: the server hands such a client
// the player state of the one it follows, with PMF_FOLLOW set (SpectatorClientEndFrame). false: as he sees it himself.
let hudFollows = true;

// cg.time: the time of the level the times of the player state are measured against. Between two polls it runs
// on, but not in the comparison with the game, where the game nearly stands.
function hudTime() {
	const h = state && state.hud;
	if (!h || h.now === undefined) return state ? state.time || 0 : 0;
	return refCam ? h.now : h.now + Math.max(0, Math.min(2 * pollInterval, performance.now() - lastPoll));
}

// level.sortedClients[0] and what CalculateRanks puts into CS_SCORES1 (the leader the top right shows). The feed
// does not have the two config strings, so the order of SortRanks is made here from what the feed has of the
// players: who plays before who watches, then by score, and who was first stays first among equals.
function hudRanks() {
	const list = [...players.values()].map(p => p.data).sort((a, b) => a.id - b.id);
	const rank = d => (d.connected === false ? 2 : d.team === 'spectator' ? 1 : 0);
	list.sort((a, b) => rank(a) - rank(b) || (rank(a) === 0 ? b.score - a.score : 0));
	const team = state.gametype >= HUD_GT.TEAM;
	return { duelWinner: state.leader !== undefined ? state.leader : list.length ? list[0].id : -1,
		scores1: team ? state.scores.red : state.scores1 !== undefined ? state.scores1 : list.length ? list[0].score : -9999,
		scores2: team ? state.scores.blue : state.scores2 !== undefined ? state.scores2 : list.length > 1 ? list[1].score : -9999 };
}

// cg.snap->ps of a client that looks through the eyes of the player d, and the rest of cg and cgs the 2D reads.
// null: the player state of the feed has not come yet.
function hudClientState(d) {
	const now = performance.now(), L = hudLocal;
	if (L.client !== d.id) Object.assign(L, { client: d.id, since: now, spawnCount: -1, weaponSelectTime: 0, oldammo: 0, oldAmmoTime: 0, crosshairClientNum: HUD_ENTITYNUM_NONE, crosshairClientTime: 0 });
	const h = state.hud && state.hud.client === d.id ? state.hud : null;
	// a game module that does not send the player state: what the row of the player has, after a moment
	if (!h && now - L.since < 1000) return null;
	const stats = h ? h.stats : [d.health, 0, 0, 0, d.weapon > 0 ? 1 << d.weapon : 0, d.armor, 0, 0, d.maxHealth || 100];
	const pers = h ? h.pers : [d.score, 0, 0, d.team === 'red' ? 1 : d.team === 'blue' ? 2 : d.team === 'spectator' ? 3 : 0, 0];
	const ammo = h ? h.ammo : HUD_AMMO_MAX.map((_, i) => (i === HUD_WEAPON_DATA[d.weapon || 0][0] && d.ammo >= 0 ? d.ammo : 0));
	const ps = {
		clientNum: h ? h.psClient : d.id, pm_type: h ? h.pm[0] : 0, pm_flags: (h ? h.pm[1] : 0) | (hudFollows ? HUD_PMF_FOLLOW : 0),
		stats, persistant: pers, ammo, powerups: h ? h.powerups : [],
		weapon: d.weapon, weaponstate: d.weaponstate || 0, weaponTime: h ? h.weapon[0] : 0, weaponChargeTime: h ? h.weapon[1] : 0,
		eFlags: d.eFlags || 0, viewheight: d.viewheight || 0, origin: d.pos, viewangles: h ? h.angles : [d.pitch, d.yaw, 0],
		fd: { forcePower: d.forcePower, forcePowersKnown: h ? h.force[0] : 0, forcePowerSelected: h ? h.force[1] : 0, forceSide: h ? h.force[2] : 0,
			forceRageRecoveryTime: h ? h.force[4] : 0, forcePowersActive: d.forceActive || 0, saberDrawAnimLevel: d.saberStyle, forcePowerLevel: h ? h.forceLevel : [] },
		zoomMode: h ? h.zoom[0] : 0, zoomFov: h ? h.zoom[1] : 0, zoomTime: h ? h.zoom[2] : 0, zoomLocked: h ? !!h.zoom[3] : false,
		rocketLockIndex: h ? h.lock[0] : HUD_ENTITYNUM_NONE, rocketLockTime: h ? h.lock[1] : 0, rocketLastValidTime: h ? h.lock[2] : 0, rocketTargetTime: h ? h.lock[3] : 0,
		jetpackFuel: h ? h.fuel[0] : 100, cloakFuel: h ? h.fuel[1] : 100,
		duelInProgress: h ? !!h.duel[0] : false, duelIndex: h ? h.duel[1] : HUD_ENTITYNUM_NONE, duelTime: h ? h.duel[2] : 0,
		fallingToDeath: h ? h.fallingToDeath : 0, holocronBits: h ? h.holocronBits : 0, hackingTime: h ? h.hackingTime : 0, hackingBaseTime: h ? h.hackingBaseTime : 0,
		emplacedIndex: h ? h.emplacedIndex : 0, m_iVehicleNum: h ? h.vehicle : 0, hasDetPackPlanted: h ? !!h.detPack : false, isJediMaster: h ? !!h.jediMaster : false
	};
	const cg = { time: hudTime(), ps, d, L, renderingThirdPerson: !!view.thirdPerson, refYaw: view.yaw || 0,
		cgs: Object.assign({ gametype: state.gametype || 0, fraglimit: state.fraglimit || 0 }, hudRanks()) };
	// CG_TransitionPlayerState: he has come into the map again (CG_Respawn shows his weapons and takes the one he has)
	const spawns = pers[HUD_PERS.SPAWN_COUNT] || 0;
	if (L.spawnCount >= 0 && spawns !== L.spawnCount) L.weaponSelectTime = cg.time;
	if (L.spawnCount !== spawns || !L.weaponSelect) L.weaponSelect = ps.weapon;
	L.spawnCount = spawns;
	return cg;
}

// cgs.clientinfo of a client: what the page has of a player
const hudClientInfo = id => { const p = players.get(id); return p ? p.data : null; };
const hudTeamOf = d => (!d ? HUD_TEAM_FREE : d.team === 'red' ? HUD_TEAM_RED : d.team === 'blue' ? HUD_TEAM_BLUE : d.team === 'spectator' ? HUD_TEAM_SPECTATOR : HUD_TEAM_FREE);

// ci->modelIcon (CG_NewClientInfo and CG_RegisterClientModelname): the icon of the model and its skin, the skin
// of his team in a team game, and for a skin of three parts the icon of the first
function hudModelIcon(d) {
	const name = d.model || 'kyle', slash = name.indexOf('/'), model = (slash < 0 ? name : name.slice(0, slash)) || 'kyle';
	let skin = slash >= 0 && name.slice(slash + 1) ? name.slice(slash + 1) : 'default';
	if (state.gametype >= HUD_GT.TEAM && state.gametype !== HUD_GT.SIEGE && (d.team === 'red' || d.team === 'blue')) skin = d.team;
	const bar = skin.indexOf('|');
	return hudShader('models/players/' + model + '/icon_' + (bar >= 0 ? skin.slice(0, bar) : skin));
}

// ---- the numbers (CG_DrawNumField of cg_drawtools.c): x, y and the sizes are ints there
const HUD_NUM_FONT_BIG = 1, HUD_NUM_FONT_SMALL = 2, HUD_NUM_FONT_CHUNKY = 3;
function hudDrawNumField(x, y, width, value, charWidth, charHeight, style, zeroFill) {
	x = Math.trunc(x); y = Math.trunc(y); value = Math.trunc(value); charWidth = Math.trunc(charWidth); charHeight = Math.trunc(charHeight);
	if (width < 1) return;
	if (width > 5) width = 5;
	const lim = [0, [9, 0], [99, -9], [999, -99], [9999, -999]][width];
	if (lim) value = Math.max(lim[1], Math.min(lim[0], value));
	const num = String(value);
	let l = Math.min(num.length, width);
	const digit = i => style === HUD_NUM_FONT_SMALL ? hudShader('gfx/2d/numbers/t_' + HUD_NUMBERS[i]) : style === HUD_NUM_FONT_CHUNKY ? hudShader('gfx/2d/numbers/' + (i === 10 ? 't_' : 'c_') + HUD_NUMBERS[i]) : hudShader('gfx/2d/numbers/' + HUD_NUMBERS[i], true);
	const xWidth = style === HUD_NUM_FONT_SMALL ? charWidth : style === HUD_NUM_FONT_CHUNKY ? Math.trunc(charWidth / 1.2 + 2) : Math.trunc(charWidth / 2) + 7;
	if (zeroFill) {
		for (let i = 0; i < width - l; i++) { hudPic(x, y, charWidth, charHeight, digit(0)); x += 2 + xWidth; }
	} else {
		x += 2 + xWidth * (width - l);
	}
	for (let i = 0; i < num.length && l; i++, l--) {
		hudPic(x, y, charWidth, charHeight, digit(num[i] === '-' ? 10 : +num[i]));
		if (style === HUD_NUM_FONT_SMALL) x++;	// "For a one line gap"
		x += xWidth;
	}
}

// ---- the HUD (CG_DrawHUD and its parts in cg_draw.c)

const HUD_TICS = 4;
// a tic that is partly used is faded: the last four tics of a value, from the fourth down
function hudDrawTics(menu, prefix, value, inc, color, partial) {
	for (let i = HUD_TICS - 1; i >= 0; i--) {
		const it = hudFindItem(menu, prefix + (i + 1));
		if (!it) continue;
		if (value <= 0) break;
		const c = color.slice();
		if (value < inc) c[3] = partial(c[3], value / inc);
		hudSetColor(c);
		hudItemPic(it);
		value -= inc;
	}
}

// CG_DrawHealth. inc is an int in the game: a quarter of the most health, cut off.
function hudDrawHealth(cg, menu) {
	const ps = cg.ps, inc = Math.trunc(ps.stats[HUD_STAT.MAX_HEALTH] / HUD_TICS);
	hudDrawTics(menu, 'health_tic', Math.min(ps.stats[HUD_STAT.HEALTH], ps.stats[HUD_STAT.MAX_HEALTH]), inc, HUD_CT.WHITE, (a, part) => a * part);
	const it = hudFindItem(menu, 'healthamount');
	if (it) {
		hudSetColor(it.foreColor);
		hudDrawNumField(it.rect[0], it.rect[1], 3, ps.stats[HUD_STAT.HEALTH], it.rect[2], it.rect[3], HUD_NUM_FONT_SMALL, false);
	}
}

// CG_DrawArmor: the last tic flashes while the armor is below a quarter
function hudDrawArmor(cg, menu) {
	const ps = cg.ps, L = cg.L, armor = ps.stats[HUD_STAT.ARMOR], inc = Math.trunc(ps.stats[HUD_STAT.MAX_HEALTH] / HUD_TICS);
	let value = armor;
	for (let i = HUD_TICS - 1; i >= 0; i--) {
		const it = hudFindItem(menu, 'armor_tic' + (i + 1));
		if (!it) continue;
		if (value <= 0) break;
		const c = HUD_CT.WHITE.slice();
		if (value < inc) c[3] *= value / inc;
		hudSetColor(c);
		if (!(i === HUD_TICS - 1 && value < inc) || L.HUDArmorFlag) hudItemPic(it);
		value -= inc;
	}
	const it = hudFindItem(menu, 'armoramount');
	if (it) {
		hudSetColor(it.foreColor);
		hudDrawNumField(it.rect[0], it.rect[1], 3, armor, it.rect[2], it.rect[3], HUD_NUM_FONT_SMALL, false);
	}
	if (armor) {
		if (armor < ps.stats[HUD_STAT.MAX_HEALTH] / 4) {
			if (L.HUDTickFlashTime < cg.time) { L.HUDTickFlashTime = cg.time + 400; L.HUDArmorFlag = !L.HUDArmorFlag; }
		} else {
			L.HUDArmorFlag = true;
		}
	} else {
		L.HUDArmorFlag = false;
	}
}

// CG_DrawSaberStyle: the picture of the style the saber is drawn with
function hudDrawSaberStyle(cg, menu) {
	if (!cg.d.weapon || cg.d.weapon !== HUD_WP.SABER) return;
	const name = { 1: 'saberstyle_fast', 5: 'saberstyle_fast', 2: 'saberstyle_medium', 6: 'saberstyle_medium', 7: 'saberstyle_medium', 3: 'saberstyle_strong', 4: 'saberstyle_strong' }[cg.ps.fd.saberDrawAnimLevel];
	const it = name ? hudFindItem(menu, name) : null;
	if (it) { hudSetColor(HUD_CT.WHITE); hudItemPic(it); }
}

// CG_DrawAmmo
function hudDrawAmmo(cg, menu) {
	const ps = cg.ps, L = cg.L, weapon = cg.d.weapon;
	if (!weapon) return;
	const data = HUD_WEAPON_DATA[weapon] || [0, 0, 0];
	let value = ps.ammo[data[0]] || 0, inc = 0;
	if (value < 0) return;
	if (L.oldammo < value) L.oldAmmoTime = cg.time + 200;
	L.oldammo = value;
	if (data[1] === 0 && data[2] === 0) {
		// "just draw infinite". NUM_FONT_SMALL is handed over as the style of the string, where it means UI_RIGHT.
		inc = Math.trunc(8 / HUD_TICS);
		value = 8;
		const it = hudFindItem(menu, 'ammoinfinite');
		hudSetColor(HUD_CT.YELLOW);
		if (it) hudDrawProportionalString(it.rect[0], it.rect[1], '--', HUD_NUM_FONT_SMALL, it.foreColor);
	} else {
		const it = hudFindItem(menu, 'ammoamount');
		let color;
		if (ps.weaponstate === HUD_WEAPON_FIRING && ps.weaponTime > 100) color = HUD_CT.LTGREY;
		else if (value > 0) color = L.oldAmmoTime > cg.time ? HUD_CT.YELLOW : it ? it.foreColor : HUD_CT.WHITE;
		else color = HUD_CT.RED;
		hudSetColor(color);
		if (it) {
			inc = HUD_AMMO_MAX[data[0]] * ((ps.eFlags & HUD_EF_DOUBLE_AMMO) ? 2 : 1) / HUD_TICS;
			hudDrawNumField(it.rect[0], it.rect[1], 3, value, it.rect[2], it.rect[3], HUD_NUM_FONT_SMALL, false);
		}
	}
	hudSetColor(HUD_CT.WHITE);
	hudDrawTics(menu, 'ammo_tic', value, inc, HUD_CT.WHITE, (a, part) => part);
}

// CG_DrawForcePower. The flashing when a power is asked for that there is no force for comes with an event of
// the player, which the feed does not have: forceHUDTotalFlashTime is never set.
function hudDrawForcePower(cg, menu) {
	const L = cg.L, power = cg.ps.fd.forcePower;
	let flash = false;
	if (L.forceHUDTotalFlashTime > cg.time) {
		flash = true;
		if (L.forceHUDNextFlashTime < cg.time) { L.forceHUDNextFlashTime = cg.time + 400; L.forceHUDActive = !L.forceHUDActive; }
	} else {
		L.forceHUDNextFlashTime = 0;
		L.forceHUDActive = true;
	}
	hudDrawTics(menu, 'force_tic', power, 100 / HUD_TICS, flash ? HUD_CT.RED : HUD_CT.WHITE, (a, part) => part);
	const it = hudFindItem(menu, 'forceamount');
	if (it) {
		hudSetColor(flash ? HUD_CT.RED : it.foreColor);
		hudDrawNumField(it.rect[0], it.rect[1], 3, power, it.rect[2], it.rect[3], HUD_NUM_FONT_SMALL, false);
	}
}

// CG_DrawHUD (cg_hudFiles is "ui/jahud.txt", which is 0 as a number: the HUD of the menu files)
function hudDrawHUD(cg) {
	const ps = cg.ps, cgs = cg.cgs;
	if (ps.pm_type === HUD_PM_SPECTATOR) return;
	let menu = hudFindMenu('lefthud');
	if (menu) {
		for (const name of ['scanline', 'frame']) {
			const it = hudFindItem(menu, name);
			if (it) { hudSetColor(HUD_CT.WHITE); hudItemPic(it); }
		}
		hudDrawArmor(cg, menu);
		hudDrawHealth(cg, menu);
		// JA+ client plugin: the alternate dimension (FFA only)
		if (cgs.gametype === HUD_GT.FFA && (ps.eFlags & HUD_EF_ALT_DIM)) {
			const it = hudFindItem(menu, 'altdiminfo');
			if (it) hudDrawScaledProportionalString(it.rect[0], it.rect[1], '[Alt-Dim]', HUD_UI_RIGHT | HUD_UI_DROPSHADOW, it.foreColor, 0.7);
		}
	}
	const score = hudString('MP_INGAME', 'SCORE') + ': ' + ps.persistant[HUD_PERS.SCORE] + (cgs.gametype === HUD_GT.DUEL ? '/' + cgs.fraglimit : '');
	menu = hudFindMenu('righthud');
	if (menu) {
		if (cgs.gametype !== HUD_GT.POWERDUEL) {
			const it = hudFindItem(menu, 'score_line');
			if (it) hudDrawScaledProportionalString(it.rect[0], it.rect[1], score, HUD_UI_RIGHT | HUD_UI_DROPSHADOW, it.foreColor, 0.7);
		}
		for (const name of ['scanline', 'frame']) {
			const it = hudFindItem(menu, name);
			if (it) { hudSetColor(HUD_CT.WHITE); hudItemPic(it); }
		}
		hudDrawForcePower(cg, menu);
		if (cg.d.weapon === HUD_WP.SABER) hudDrawSaberStyle(cg, menu); else hudDrawAmmo(cg, menu);
	}
}

// ---- the upper right corner (CG_DrawUpperRight with CG_DrawEnemyInfo, CG_DrawMiniScoreboard, CG_DrawPowerupIcons)

const HUD_ICON_SIZE = 48;
// CG_DrawEnemyInfo: who he fights a duel with, else who leads, with his picture, his name and the score
function hudDrawEnemyInfo(cg, y) {
	const ps = cg.ps, cgs = cg.cgs;
	if (ps.stats[HUD_STAT.HEALTH] <= 0 || cgs.gametype === HUD_GT.POWERDUEL) return y;
	let title, clientNum;
	if (cgs.gametype === HUD_GT.JEDIMASTER) {
		// who the Jedi Master is comes in a config string the feed does not have
		return y;
	} else if (ps.duelInProgress) {
		title = hudString('MP_INGAME', 'DUELING');
		clientNum = ps.duelIndex;
	} else if (cgs.gametype === HUD_GT.DUEL) {
		// the two who fight come in a config string the feed does not have
		return y;
	} else {
		if (cgs.duelWinner < 0 || cgs.duelWinner >= HUD_MAX_CLIENTS) return y;
		title = hudString('MP_INGAME', 'LEADER') + ': ' + cgs.scores1;
		clientNum = cgs.duelWinner;
	}
	const ci = clientNum >= 0 && clientNum < HUD_MAX_CLIENTS ? hudClientInfo(clientNum) : null;
	if (!ci) return y;
	const size = HUD_ICON_SIZE * 1.25, icon = hudModelIcon(ci);
	y += 5;
	hudPic(640 - size - 5, y, size, size, icon);
	y += size;
	hudTextPaint(630 - hudTextWidth(ci.name, 1.0, HUD_FONT_SMALL2), y, 1.0, HUD_CT.WHITE, ci.name, 0, 0, 0, HUD_FONT_SMALL2);
	y += 15;
	hudTextPaint(630 - hudTextWidth(title, 1.0, HUD_FONT_SMALL2), y, 1.0, HUD_CT.WHITE, title, 0, 0, 0, HUD_FONT_SMALL2);
	return y + HUD_BIGCHAR_HEIGHT + 2;
}

// CG_DrawMiniScoreboard: the scores of the two teams
function hudDrawMiniScoreboard(cg, y) {
	const cgs = cg.cgs;
	if (cgs.gametype === HUD_GT.SIEGE || cgs.gametype < HUD_GT.TEAM) return y;
	const temp = hudString('MP_INGAME', 'RED') + ': ' + (cgs.scores1 === -9999 ? '-' : cgs.scores1) + ' ' + hudString('MP_INGAME', 'BLUE') + ': ' + (cgs.scores2 === -9999 ? '-' : cgs.scores2);
	hudTextPaint(630 - hudTextWidth(temp, 0.7, HUD_FONT_MEDIUM), y, 0.7, HUD_CT.WHITE, temp, 0, 0, HUD_TEXTSTYLE.SHADOWEDMORE, HUD_FONT_MEDIUM);
	return y + 15;
}

// CG_DrawPowerupIcons: what he carries that runs out, with the seconds it has left. y is an int there.
function hudDrawPowerupIcons(cg, y) {
	const ps = cg.ps, size = 64;
	hudSetColor(null);
	y = Math.trunc(y) + 16;
	for (let j = 0; j < HUD_PW.NUM; j++) {
		if (!(ps.powerups[j] > cg.time)) continue;
		// BG_FindItemForPowerup: giType IT_POWERUP (5), IT_PERSISTANT_POWERUP (7) or IT_TEAM (8)
		const item = itemDefs.find(it => it && (it.type === 5 || it.type === 7 || it.type === 8) && it.tag === j);
		if (!item) continue;
		const flag = j === HUD_PW.REDFLAG || j === HUD_PW.BLUEFLAG, left = Math.trunc((ps.powerups[j] - cg.time) / 1000);
		const icon = cg.cgs.gametype === HUD_GT.CTY && flag ? hudShader(j === HUD_PW.REDFLAG ? 'gfx/hud/mpi_rflag_ys' : 'gfx/hud/mpi_bflag_ys') : item.icon ? hudShader(item.icon, true) : null;
		hudPic(640 - size * 1.1, y, size, size, icon);
		y += size;
		if (!flag && left < 999) hudDrawProportionalString(640 - size * 1.1 + size / 2, y - 8, String(left), HUD_UI_CENTER | HUD_UI_BIGFONT | HUD_UI_DROPSHADOW, HUD_CT.WHITE);
		y += Math.trunc(size / 3);
	}
}

// CG_DrawUpperRight. With the cvars at their defaults there is no team overlay, no snapshot number, no frame
// rate and no timer. The radar (a team game, or in a vehicle) is in js/hud-world.js.
function hudDrawUpperRight(cg) {
	let y = 0;
	hudSetColor(HUD_CT.WHITE);
	if (cg.cgs.gametype >= HUD_GT.TEAM || cg.ps.m_iVehicleNum) y = hudDrawRadar(cg, y);
	y = hudDrawEnemyInfo(cg, y);
	y = hudDrawMiniScoreboard(cg, y);
	hudDrawPowerupIcons(cg, y);
}

// CG_DrawFollow: "Following" and the name of the one who is followed, for a client that follows
function hudDrawFollow(cg) {
	if (!(cg.ps.pm_flags & HUD_PMF_FOLLOW)) return false;
	// the two halves of a team in a power duel come in a config string the feed does not have
	let s = hudString('MP_INGAME', 'FOLLOWING');
	hudTextPaint(320 - Math.trunc(hudTextWidth(s, 1.0, HUD_FONT_MEDIUM) / 2), 60, 1.0, HUD_CT.WHITE, s, 0, 0, 0, HUD_FONT_MEDIUM);
	const ci = hudClientInfo(cg.ps.clientNum);
	s = ci ? ci.name : '';
	hudTextPaint(320 - Math.trunc(hudTextWidth(s, 2.0, HUD_FONT_MEDIUM) / 2), 80, 2.0, HUD_CT.WHITE, s, 0, 0, 0, HUD_FONT_MEDIUM);
	return true;
}

// ---- what lies over the whole picture and at its edges

// The tint of a force power: it comes up over 9 seconds while the power is on and goes when it ends
// (the same lines four times in CG_Draw2DScreenTints: rage, absorb, protect, ysalamiri)
function hudTintPower(cg, on, key, rgb, half) {
	const L = cg.L, T = key + 'Time', F = key + 'FadeTime', V = key + 'FadeVal';
	if (on) {
		if (!L[T]) L[T] = cg.time;
		const v = Math.max(0, Math.min(0.15, (cg.time - L[T]) / 9000));
		if (!cg.renderingThirdPerson) hudFillRect(0, 0, 640, 480, [rgb[0], rgb[1], rgb[2], half ? v / 2 : v]);
		L[F] = 0; L[V] = 0;
		return true;
	}
	if (L[T]) {
		if (!L[F]) { L[F] = cg.time; L[V] = 0.15; }
		const v = Math.max(0, Math.min(0.15, L[V]));
		L[V] -= (cg.time - L[F]) * 0.000005;
		if (!cg.renderingThirdPerson && v) hudFillRect(0, 0, 640, 480, [rgb[0], rgb[1], rgb[2], half ? v / 2 : v]);
		else L[T] = 0;
	}
	return false;
}

// CG_Draw2DScreenTints. Not here: the tint under water, in slime and in lava (what the eye is in, viewContents,
// needs the brushes of the map), and the one after rage where it differs from the others.
function hudDraw2DScreenTints(cg) {
	const ps = cg.ps, L = cg.L, active = ps.fd.forcePowersActive;
	if (hudTeamOf(hudClientInfo(ps.clientNum)) !== HUD_TEAM_SPECTATOR) {
		// Plugin_NoAltDim_effect (cp_pluginDisable bit 8) is off by default: no overlay in the alternate dimension
		if (!hudTintPower(cg, !!(active & (1 << HUD_FP.RAGE)), 'rage', [0.7, 0, 0], false) && !L.rageTime) {
			// the grey of the time after rage
			if (ps.fd.forceRageRecoveryTime > cg.time) {
				if (!L.rageRecTime) L.rageRecTime = cg.time;
				if (!cg.renderingThirdPerson) hudFillRect(0, 0, 640, 480, [0.2, 0.2, 0.2, 0.15]);
				L.rageRecFadeTime = 0; L.rageRecFadeVal = 0;
			} else if (L.rageRecTime) {
				if (!L.rageRecFadeTime) { L.rageRecFadeTime = cg.time; L.rageRecFadeVal = 0.15; }
				const v = Math.max(0, Math.min(0.15, L.rageRecFadeVal));
				L.rageRecFadeVal -= (cg.time - L.rageRecFadeTime) * 0.000005;
				if (!cg.renderingThirdPerson && v) hudFillRect(0, 0, 640, 480, [0.2, 0.2, 0.2, v]); else L.rageRecTime = 0;
			}
		}
		hudTintPower(cg, !!(active & (1 << HUD_FP.ABSORB)), 'absorb', [0, 0, 0.7], true);
		hudTintPower(cg, !!(active & (1 << HUD_FP.PROTECT)), 'protect', [0, 0.7, 0], true);
		if (ps.rocketLockIndex !== HUD_ENTITYNUM_NONE && cg.time - ps.rocketLockTime > 0) hudDrawRocketLocking(cg, ps.rocketLockIndex, ps.rocketLockTime);
		// BG_HasYsalamiri: the powerup, or in Capture the Ysalamiri one of the flags
		const ysal = ps.powerups[HUD_PW.YSALAMIRI] > cg.time || (cg.cgs.gametype === HUD_GT.CTY && (ps.powerups[HUD_PW.REDFLAG] > 0 || ps.powerups[HUD_PW.BLUEFLAG] > 0));
		hudTintPower(cg, !!ysal, 'ysal', [0.7, 0.7, 0], true);
	}
}

// forcePowerSorted of bg_misc.c and HolocronIcons of ui_shared.c
const HUD_FORCE_SORTED = [5, 0, 10, 9, 11, 1, 2, 3, 4, 14, 7, 13, 8, 6, 12, 15, 16, 17];
const HUD_FORCE_ICONS = ['lt_heal', 'levitation', 'speed', 'push', 'pull', 'lt_telepathy', 'dk_grip', 'dk_l1', 'dk_rage', 'lt_protect', 'lt_absorb', 'lt_healother', 'dk_forceother', 'dk_drain', 'sight', 'saber_attack', 'saber_defend', 'saber_throw'];
const hudForceIcon = fp => hudShader('gfx/mp/f_icon_' + HUD_FORCE_ICONS[fp]);

// CG_DrawHolocronIcons: the holocrons he carries, down the left edge
function hudDrawHolocronIcons(cg) {
	const ps = cg.ps, size = 40;
	if (ps.zoomMode || hudTeamOf(hudClientInfo(ps.clientNum)) === HUD_TEAM_SPECTATOR) return;
	let x = 10, y = 10;
	for (const fp of HUD_FORCE_SORTED) {
		if (!(ps.holocronBits & (1 << fp))) continue;
		hudPic(x, y, size, size, hudForceIcon(fp));
		y += size + 2;
		if (y + size >= 480 - 80) { y = 10; x += size + 2; }
	}
}

// CG_DrawActivePowers: the powers that are on for a while, and the time after rage. With the sight of the Force
// the JA+ client plugin lays gfx/2d/jsense over the picture (Plugin_NewForceSight_effect, on by default).
function hudDrawActivePowers(cg) {
	const ps = cg.ps, size = 40, duration = [HUD_FP.HEAL, HUD_FP.SPEED, HUD_FP.TELEPATHY, HUD_FP.RAGE, HUD_FP.PROTECT, HUD_FP.ABSORB, HUD_FP.SEE];
	if (ps.zoomMode || hudTeamOf(hudClientInfo(ps.clientNum)) === HUD_TEAM_SPECTATOR) return;
	let x = size * 2 + 16, y = 480 - size * 2;
	hudSetColor(null);
	for (const fp of HUD_FORCE_SORTED) {
		if (!(ps.fd.forcePowersActive & (1 << fp)) || !duration.includes(fp)) continue;
		if (fp === HUD_FP.SEE) hudPic(0, 0, 640, 480, hudShader('gfx/2d/jsense'));
		hudPic(x, y, size, size, hudForceIcon(fp));
		x += size + 2;
		if (x + size >= 640 - 80) { x = size * 2 + 16; y += size + 2; }
	}
	if (ps.fd.forceRageRecoveryTime > cg.time) hudPic(x, y, size, size, hudShader('gfx/mp/f_icon_ragerec'));
}

// CG_DrawJetpackFuel and CG_DrawCloakFuel: a bar at the right edge while the fuel is not full
function hudDrawFuelBar(x, fuel, color, missing) {
	const H = 100, W = 20, y = 260;
	let percent = fuel / 100 * H;
	if (percent > H) return;
	if (percent < 0.1) percent = 0.1;
	hudDrawRect(x, y, W, H, 1, HUD_CT.BLACK);
	hudFillRect(x + 1, y + 1 + (H - percent), W - 1, H - 1 - (H - percent), color);
	hudFillRect(x + 1, y + 1, W - 1, H - percent, missing);
}

// CG_DrawZoomMask: the mask of the binoculars (zoomMode 2) and of the disruptor scope (any other zoom)
let hudZoomFlip = true;
function hudDrawZoomMask(cg) {
	const ps = cg.ps, sh = name => hudShader(name, true);
	if (ps.zoomMode === 2) {
		const level = Math.max(0, Math.min(1, (80 - ps.zoomFov) / 80)) * 162;
		hudSetColor(HUD_CT.WHITE);
		hudPic(34, 48, 570, 362, sh('gfx/2d/binocularWindow'));
		hudSetColor(HUD_CT.BLACK);
		hudPic(212, 367, 200, 40, hudShader('white', true));
		hudSetColor([0.2, 0.4, 0.2, 0.3]);
		// the numbers of the compass, ten degrees apart
		const yaw = cg.refYaw + 180;
		let val = Math.trunc(yaw / 10) * 10;
		const off = yaw - val;
		for (let i = -10; i < 30; i += 10) {
			val -= 10;
			if (val < 0) val += 360;
			if ((off > 3 && i === -10) || i > -10) {
				hudDrawNumField(155 + i * 10 + off * 10, 374, 3, val + 200, 24, 14, HUD_NUM_FONT_CHUNKY, true);
				hudPic(245 + (i - 1) * 10 + off * 10, 376, 6, 6, hudShader('white', true));
			}
		}
		hudPic(212, 367, 200, 28, sh('gfx/2d/binocularNumOverlay'));
		let c = Math.sin(cg.time * 0.01) * 0.5 + 0.5;
		c *= c;
		hudSetColor([c, c, c, 1]);
		hudPic(82, 94, 16, 16, sh('gfx/2d/binCircle'));
		const rnd = () => Math.random() * 0.2 - 0.1;
		hudSetColor([0.7 + rnd(), 0.8 + rnd(), 0.7 + rnd(), 1]);
		hudPic(0, 0, 640, 480, sh('gfx/2d/binMask'));
		hudPic(4, 282 - level, 16, 16, sh('gfx/2d/binSideArrow'));
		if (hudZoomFlip) hudPic(330, 60, -26, -30, sh('gfx/2d/binTopTri')); else hudPic(307, 40, 26, 30, sh('gfx/2d/binTopTri'));
		if (Math.random() > 0.98 && (cg.time & 1024)) hudZoomFlip = !hudZoomFlip;
	} else if (ps.zoomMode) {
		// zoomFov is a value of the client game that follows the zoom of the player state
		const level = Math.max(0, Math.min(1, (50 - (ps.zoomFov || 80)) / 50)) * 103;
		hudSetColor(HUD_CT.WHITE);
		hudPic(0, 0, 640, 480, sh('gfx/2d/cropCircle2'));
		if (level >= 99) hudSetColor([1, 1, 1, 0.7 + Math.sin(cg.time * 0.01) * 0.3]);
		hudRotatePic2(320, 240, 640, 480, 0, 0, 1, 1, -level, sh('gfx/2d/cropCircle'));
		const index = HUD_WEAPON_DATA[HUD_WP.DISRUPTOR][0];
		let max = Math.min(1, ps.ammo[index] / (HUD_AMMO_MAX[index] * ((ps.eFlags & HUD_EF_DOUBLE_AMMO) ? 2 : 1)));
		let color = [Math.min(1, (1 - max) * 2), Math.min(1, max * 1.5), 0, 1];
		if (max < 0.15 && (cg.time & 512)) color = [0, 0, 0, 1];
		hudSetColor(color);
		max *= 58;
		for (let fi = 18.5; fi <= 18.5 + max; fi += 3) {
			const cx = 320 + Math.sin((fi + 90) / 57.296) * 190, cy = 240 + Math.cos((fi + 90) / 57.296) * 190;
			hudRotatePic2(cx, cy, 12, 24, 0, 0, 1, 1, 90 - fi, sh('gfx/2d/insertTick'));
		}
		if (ps.weaponstate === HUD_WEAPON_CHARGING_ALT) {
			hudSetColor(HUD_CT.WHITE);
			const charge = Math.min(1, (cg.time - ps.weaponChargeTime) / (50 * 30));
			hudStretchPic(257, 435, 134 * charge, 34, 0, 0, charge, 1, hudShader('gfx/2d/crop_charge'));
		}
	}
}

// ---- the bar of the weapons (CG_DrawWeaponSelect of cg_weapons.c)

// The client that follows shows it when the player comes into the map again (CG_Respawn). When he picks a weapon
// up it shows too, by an event the feed does not have.
const HUD_WEAPON_SELECT_TIME = 1400;
// bg_itemlist: the icon of a weapon is the one of its item (IT_WEAPON is 1)
function hudWeaponIcon(weapon, noAmmo) {
	const item = itemDefs.find(it => it && it.type === 1 && it.tag === weapon);
	return item && item.icon ? hudShader(item.icon + (noAmmo ? '_na' : '')) : null;
}
function hudDrawWeaponSelect(cg) {
	const ps = cg.ps, L = cg.L;
	if (ps.emplacedIndex) L.weaponSelectTime = 0;
	if (L.weaponSelectTime + HUD_WEAPON_SELECT_TIME < cg.time || ps.stats[HUD_STAT.HEALTH] <= 0) return;
	const bits = ps.stats[HUD_STAT.WEAPONS], sel = L.weaponSelect;
	// CG_WeaponCheck and CG_WeaponSelectable
	const check = w => !(ps.ammo[HUD_WEAPON_DATA[w][0]] < HUD_WEAPON_DATA[w][1] && ps.ammo[HUD_WEAPON_DATA[w][0]] < HUD_WEAPON_DATA[w][2]);
	const selectable = w => !!w && check(w) && !(w === HUD_WP.DET_PACK && ps.ammo[HUD_WEAPON_DATA[w][0]] < 1 && !ps.hasDetPackPlanted) && !!(bits & (1 << w));
	const explosive = w => w === HUD_WP.THERMAL || w === HUD_WP.TRIP_MINE;
	let count = !selectable(sel) && explosive(sel) ? 1 : 0;
	for (let i = 1; i < HUD_WP.NUM; i++) if ((bits & (1 << i)) && (selectable(i) || !explosive(i))) count++;
	if (!count) return;
	const sideMax = 3, hold = count - 1;
	const left = hold === 0 ? 0 : count > 2 * sideMax ? sideMax : Math.trunc(hold / 2), right = hold === 0 ? 0 : count > 2 * sideMax ? sideMax : hold - Math.trunc(hold / 2);
	const small = 40, big = 80, pad = 12, x = 320, y = 410;
	const icon = (w, px, py, size) => { hudSetColor(HUD_CT.WHITE); hudPic(px, py, size, size, hudWeaponIcon(w, !check(w))); };
	hudSetColor(HUD_CT.WHITE);
	// the icons to the left, backwards from the one in the middle: the concussion rifle sits between the
	// flechette and the rocket launcher
	let i = sel === HUD_WP.CONCUSSION ? HUD_WP.FLECHETTE : sel - 1, holdX = x - (big / 2 + pad + small), drewConc = false;
	if (i < 1) i = HUD_LAST_USEABLE_WEAPON;
	for (let n = 1, guard = 0; n < left + 1 && guard < 64; i--, guard++) {
		if (i === HUD_WP.CONCUSSION) i--;
		else if (i === HUD_WP.FLECHETTE && !drewConc && sel !== HUD_WP.CONCUSSION) i = HUD_WP.CONCUSSION;
		if (i < 1) i = HUD_LAST_USEABLE_WEAPON;
		if (!(bits & (1 << i))) { if (i === HUD_WP.CONCUSSION) { drewConc = true; i = HUD_WP.ROCKET_LAUNCHER; } continue; }
		if (!selectable(i) && explosive(i)) continue;
		n++;
		if (hudWeaponIcon(i, false)) { icon(i, holdX, y + 10, small); holdX -= small + pad; }
		if (i === HUD_WP.CONCUSSION) { drewConc = true; i = HUD_WP.ROCKET_LAUNCHER; }
	}
	if (hudWeaponIcon(sel, false)) icon(sel, x - big / 2, y - (big - small) / 2 + 10, big);
	i = sel === HUD_WP.CONCUSSION ? HUD_WP.ROCKET_LAUNCHER : sel + 1;
	if (i > HUD_LAST_USEABLE_WEAPON) i = 1;
	holdX = x + big / 2 + pad;
	for (let n = 1, guard = 0; n < right + 1 && guard < 64; i++, guard++) {
		if (i === HUD_WP.CONCUSSION) i++;
		else if (i === HUD_WP.ROCKET_LAUNCHER && !drewConc && sel !== HUD_WP.CONCUSSION) i = HUD_WP.CONCUSSION;
		if (i > HUD_LAST_USEABLE_WEAPON) i = 1;
		if (!(bits & (1 << i))) { if (i === HUD_WP.CONCUSSION) { drewConc = true; i = HUD_WP.FLECHETTE; } continue; }
		if (!selectable(i) && explosive(i)) continue;
		n++;
		if (hudWeaponIcon(i, false)) { icon(i, holdX, y + 10, small); holdX += small + pad; }
		if (i === HUD_WP.CONCUSSION) { drewConc = true; i = HUD_WP.FLECHETTE; }
	}
}

// ---- CG_Draw2D, in the order it draws

// What a client that follows a player sees with the cvars at their defaults. Left out, each for what it needs
// that the feed does not have: the scoreboard and the intermission, the vehicle HUD, the health of an e-web, the
// flags of a CTF game (CG_DrawFlagStatus), the flare where sabers clash and the icon of an item that was picked
// up (events), votes, the centre prints, the chat lines, the warmup and the siege.
function hudDraw2D(cg) {
	const ps = cg.ps;
	if (ps.pm_type === HUD_PM_INTERMISSION) return;
	hudDraw2DScreenTints(cg);
	if (ps.rocketLockIndex !== HUD_ENTITYNUM_NONE && cg.time - ps.rocketLockTime > 0) hudDrawRocketLocking(cg, ps.rocketLockIndex, ps.rocketLockTime);
	if (ps.holocronBits) hudDrawHolocronIcons(cg);
	if (ps.fd.forcePowersActive || ps.fd.forceRageRecoveryTime > cg.time) hudDrawActivePowers(cg);
	if (ps.jetpackFuel < 100) hudDrawFuelBar(640 - 20 - 8, ps.jetpackFuel, [0.5, 0, 0, 0.8], [0.5, 0.5, 0.5, 0.1]);
	if (ps.cloakFuel < 100) hudDrawFuelBar(640 - 20 - 8 - (ps.jetpackFuel < 100 ? 20 + 8 : 0), ps.cloakFuel, [0, 0, 0.6, 0.8], [0.1, 0.1, 0.3, 0.1]);
	hudDrawZoomMask(cg);

	if (ps.persistant[HUD_PERS.TEAM] === HUD_TEAM_SPECTATOR) {
		// a spectator who flies by himself: the page does not look through the eyes of one
		hudDrawCrosshair(cg, null, 0);
		hudDrawCrosshairNames(cg);
	} else if (ps.stats[HUD_STAT.HEALTH] > 0) {
		hudDrawCrosshairNames(cg);
		// inventory and force powers are chosen with keys of the one who plays: their bars never show for a follower
		hudDrawWeaponSelect(cg);
		// CG_DrawStats
		if (!ps.m_iVehicleNum) hudDrawHUD(cg);
	}

	if (ps.fallingToDeath) {
		// FALL_FADE_TIME is 3000
		hudFillRect(0, 0, 640, 480, [0, 0, 0, Math.max(0, Math.min(1, (cg.time - ps.fallingToDeath) / 1500))]);
	}
	hudDrawBracketedEntities(cg);
	hudDrawUpperRight(cg);
	hudDrawFollow(cg);
}

// The 2D of the frame, for the player the page looks through the eyes of (renderHud in js/weapons.js)
function drawHud(d) {
	// the picture of the comparison that is taken without the HUD (cg_draw2D 0 in the game)
	if (!d || !state || document.body.classList.contains('nohud')) return;
	hudLoadMenus();
	if (!hudSettleShaders() || hudMenus.state < 2) return;
	const cg = hudClientState(d);
	if (!cg) return;
	if (hud2d.record) hud2d.pics.length = 0;
	hudSetColor(null);
	// what was gathered is drawn also when a part of the 2D fails, so that nothing of it is left for the next frame
	try { hudDraw2D(cg); } finally { hud2dFlush(); }
}
