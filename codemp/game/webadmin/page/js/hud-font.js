// ---- the fonts of the game (rd-common/tr_font.cpp of the renderer): a .fontdat with the measures of its glyphs
// and the image they are in, and the 2D text of the client game on top of it (cgame/cg_draw.c, cg_drawtools.c)
//
// A string is drawn glyph by glyph with RE_StretchPic (hudStretchPic in js/hud.js), in the 640 x 480 screen of
// the 2D. The numbers are worked out as floats of 32 bits where the engine has them (Math.fround), because the
// width of a string is rounded up to a whole number and must come out the same.

const HUD_FONTDAT_SIZE = 7180;	// sizeof( dfontdat_t ): 256 glyphs of 28 bytes and five shorts, padded to 4 bytes
const HUD_STYLE_DROPSHADOW = 0x80000000 | 0, HUD_STYLE_BLINK = 0x40000000, HUD_SET_MASK = 0x00ffffff;	// qfiles.h
// menudef.h and q_shared.h
const HUD_TEXTSTYLE = { NORMAL: 0, BLINK: 1, PULSE: 2, SHADOWED: 3, OUTLINED: 4, OUTLINESHADOWED: 5, SHADOWEDMORE: 6 };
const HUD_FONT_SMALL = 1, HUD_FONT_MEDIUM = 2, HUD_FONT_LARGE = 3, HUD_FONT_SMALL2 = 4;
const HUD_UI_CENTER = 1, HUD_UI_RIGHT = 2, HUD_UI_FORMATMASK = 7, HUD_UI_SMALLFONT = 0x10, HUD_UI_BIGFONT = 0x20, HUD_UI_DROPSHADOW = 0x800, HUD_UI_BLINK = 0x1000, HUD_UI_PULSE = 0x4000;
const HUD_BIGCHAR_WIDTH = 16, HUD_BIGCHAR_HEIGHT = 16, HUD_SMALLCHAR_WIDTH = 8, HUD_SMALLCHAR_HEIGHT = 16;
// g_color_table of q_color.c: what ^0 to ^9 set. ColorIndex masks with 15, the entries after the tenth are 0.
const HUD_COLOR_TABLE = [[0, 0, 0, 1], [1, 0, 0, 1], [0, 1, 0, 1], [1, 1, 0, 1], [0, 0, 1, 1], [0, 1, 1, 1], [1, 0, 1, 1], [1, 1, 1, 1], [1, 0.5, 0, 1], [0.5, 0.5, 0.5, 1]];

const hudF32 = Math.fround;
// Round of qcommon.h
const hudRound = v => Math.floor(hudF32(v + 0.5));

// entry 0 is the font that is not there, as in the engine (g_vFontArray)
const hudFonts = [null], hudFontIndex = new Map();
// the fonts of the client game (cgDC.Assets): CG_Init registers ocr_a and ergoec, the assetGlobalDef of the HUD
// menu file names more (CG_Asset_Parse, hudLoadMenus in js/hud.js)
const hudAssets = { qhSmallFont: 0, qhMediumFont: 0, qhSmall2Font: 0, qhBigFont: 0 };

// What is on its way of the 2D counts as an image on its way (imageBusy of js/scripts-parse.js): a picture for
// the comparison with the game is taken when nothing is.
function hudBusy(promise) {
	imageBusy++;
	return promise.finally(() => { imageBusy--; pumpImages(); });
}

// CFontInfo::CFontInfo: the glyph table and the measures of a font, as the file has them
function hudParseFontdat(buf) {
	if (buf.byteLength !== HUD_FONTDAT_SIZE) return null;
	const v = new DataView(buf), glyphs = [];
	for (let i = 0; i < 256; i++) {
		const o = i * 28;
		glyphs.push({ width: v.getInt16(o, true), height: v.getInt16(o + 2, true), horizAdvance: v.getInt16(o + 4, true), horizOffset: v.getInt16(o + 6, true),
			baseline: v.getInt32(o + 8, true), s: v.getFloat32(o + 12, true), t: v.getFloat32(o + 16, true), s2: v.getFloat32(o + 20, true), t2: v.getFloat32(o + 24, true) });
	}
	const f = { glyphs, pointSize: v.getInt16(7168, true), height: v.getInt16(7170, true), ascender: v.getInt16(7172, true), descender: v.getInt16(7174, true) };
	// "cope with bad fontdat headers"
	if (f.height === 0) {
		f.height = f.pointSize;
		f.ascender = f.pointSize - hudRound(f.pointSize / 10 + 2);
		f.descender = f.height - f.ascender;
	}
	return f;
}

// RE_RegisterFont_Real. name is "ergoec" or "fonts/ergoec": the file is fonts/<name without its path>.fontdat and
// the image of the glyphs the shader of that name without the ending. The engine gives 0 for a font that is not
// there; here the number is given at once and the font draws nothing until its file has come, or for good.
function hudRegisterFontReal(name) {
	if (hudFontIndex.has(name)) return hudFontIndex.get(name);
	const base = 'fonts/' + name.replace(/^.*[\/\\]/, ''), font = { name, base, state: 0, glyphs: null, pointSize: 0, height: 0, ascender: 0, descender: 0, roundCalcs: false, shader: null, variants: [], handle: hudFonts.length };
	hudFonts.push(font);
	hudFontIndex.set(name, font.handle);
	font.loaded = hudBusy(fetchGameFile(base + '.fontdat').then(buf => {
		const f = hudParseFontdat(buf);
		if (!f || f.pointSize <= 0) { font.state = 2; return false; }
		Object.assign(font, f);
		font.shader = hudShader(base, false);
		font.state = 1;
		return true;
	}, () => { font.state = 2; return false; }));
	return font.handle;
}

// RE_RegisterFont: the font, and the sharper ones of this fork that stand in for it when it is drawn large
// (<name>_sharp1 to _sharp8, as far as their files are there)
function hudRegisterFont(name) {
	const known = hudFontIndex.has(name), handle = hudRegisterFontReal(name), font = hudFonts[handle];
	if (!known) {
		font.loaded.then(async ok => {
			for (let i = 1; ok && i <= 8; i++) {
				const variant = hudFonts[hudRegisterFontReal(name + '_sharp' + i)];
				if (!(await variant.loaded)) break;
				font.variants.push(variant);
			}
		});
	}
	return handle;
}

const hudGetFont = handle => { const f = hudFonts[handle & HUD_SET_MASK]; return f && f.state === 1 ? f : null; };

// RE_Font_GetVariant: [the font that is drawn, the scale it is drawn with]. glConfig.vidHeight / SCREEN_HEIGHT
// is a division of whole numbers in the engine.
function hudFontVariant(font, scale) {
	const n = font.variants.length;
	if (!n) return [font, scale];
	const requested = Math.trunc(font.pointSize * scale * 1 /* r_fontSharpness */ * Math.trunc(canvas.height / 480));
	if (requested <= font.pointSize) return [font, scale];
	let variant = font.variants[n - 1];
	for (let i = 0; i < n; i++) if (requested <= font.variants[i].pointSize) { variant = font.variants[i]; break; }
	return [variant, hudF32(scale * (font.pointSize / variant.pointSize))];
}

// CFontInfo::GetLetter and GetLetterHorizAdvance (the western fonts: no Asian glyphs, no other alphabet)
const hudLetter = (font, c) => font.glyphs[c & 255];
const hudLetterAdvance = (font, c) => font.glyphs[c & 255].horizAdvance || font.glyphs[46].horizAdvance;
const hudIsDigit = c => c >= 48 && c <= 57;

// RE_Font_StrLenPixelsNew and RE_Font_StrLenPixels: the width of the longest line of a string in a font, in
// units of the 640 screen. A ^ and a digit after it are a colour and have no width.
function hudFontStrLenPixelsNew(text, handle, scaleIn) {
	let font = hudGetFont(handle), scale = hudF32(scaleIn);
	if (!font) return 0;
	[font, scale] = hudFontVariant(font, scale);
	let most = 0, line = 0;
	for (let i = 0; i < text.length; ) {
		const c = text.charCodeAt(i++) & 255;
		if (c === 94 && hudIsDigit(text.charCodeAt(i))) { i++; continue; }
		if (c === 10) { line = 0; continue; }
		// r_aspectCorrectFonts is 0
		const value = hudF32(hudLetterAdvance(font, c) * scale);
		line = hudF32(line + (font.roundCalcs ? Math.round(value) : value));
		if (line > most) most = line;
	}
	return most;
}
const hudFontStrLenPixels = (text, handle, scale) => Math.ceil(hudFontStrLenPixelsNew(text, handle, scale));

// RE_Font_HeightPixels (it returns an int)
function hudFontHeightPixels(handle, scaleIn) {
	let font = hudGetFont(handle), scale = hudF32(scaleIn);
	if (!font) return 0;
	[font, scale] = hudFontVariant(font, scale);
	const value = hudF32(font.pointSize * scale);
	return font.roundCalcs ? hudRound(value) : Math.trunc(value);
}

// RE_Font_DrawString. ox and oy are ints in the engine: what a caller hands over as a float loses its fraction.
// maxPixelWidth is -1 for all of the string. handle carries the style bits (STYLE_DROPSHADOW, STYLE_BLINK).
let hudFontInShadow = false;
function hudFontDrawString(ox, oy, text, rgba, handle, maxPixelWidth, scaleIn) {
	ox = Math.trunc(ox); oy = Math.trunc(oy);
	// the engine blinks by its own clock
	if ((handle & HUD_STYLE_BLINK) && ((Math.floor(performance.now()) >> 7) & 1)) return;
	let font = hudGetFont(handle), scale = hudF32(scaleIn);
	if (!font) return;
	[font, scale] = hudFontVariant(font, scale);
	if (!font.glyphs) return;

	if (handle & HUD_STYLE_DROPSHADOW) {
		const offset = hudRound(hudF32(hudF32(font.pointSize * scale) * hudF32(0.075)));
		hudFontInShadow = true;
		hudFontDrawString(ox + offset, oy + offset, text, [0.15, 0.15, 0.15, rgba ? rgba[3] : 1], font.handle, maxPixelWidth, scale);
		hudFontInShadow = false;
	}
	hudSetColor(rgba);

	const fox = ox, r = v => font.roundCalcs ? hudRound(v) : hudF32(v);
	let fx = fox, foy = hudF32(oy + r((font.height - (font.descender >> 1)) * scale)), overflow = false;
	for (let i = 0; i < text.length && !overflow; ) {
		const c = text.charCodeAt(i++) & 255;
		if (c === 10) { fx = fox; foy = hudF32(foy + r(font.pointSize * scale)); continue; }
		if (c === 13) continue;
		if (c === 32) {
			fx = hudF32(fx + r(hudLetter(font, 32).horizAdvance * scale));
			overflow = maxPixelWidth !== -1 && fx - fox > maxPixelWidth;
			continue;
		}
		if (c === 94 && hudIsDigit(text.charCodeAt(i))) {
			const color = HUD_COLOR_TABLE[(text.charCodeAt(i++) - 48) & 15] || [0, 0, 0, 0];
			if (!hudFontInShadow) hudSetColor([color[0], color[1], color[2], rgba ? rgba[3] : 1]);
			continue;
		}
		let g = hudLetter(font, c);
		if (!g.width) g = hudLetter(font, 46);
		const advance = r(g.horizAdvance * scale);
		overflow = maxPixelWidth !== -1 && hudF32(fx + advance) - fox > maxPixelWidth;
		if (overflow) break;
		const fy = hudF32(foy - r(g.baseline * scale));
		hudStretchPic(hudF32(fx + r(g.horizOffset * scale)), fy, r(g.width * scale), r(g.height * scale), g.s, g.t, g.s2, g.t2, font.shader);
		fx = hudF32(fx + advance);
	}
	// the colour stays as it is, as in the engine
}

// ---- the text of the client game

// MenuFontToHandle of cg_draw.c
function hudMenuFont(menuFont) {
	if (menuFont === HUD_FONT_SMALL) return hudAssets.qhSmallFont;
	if (menuFont === HUD_FONT_SMALL2) return hudAssets.qhSmall2Font;
	return hudAssets.qhMediumFont;
}

// CG_Text_Width, CG_Text_Height and CG_Text_Paint of cg_draw.c
const hudTextWidth = (text, scale, menuFont) => hudFontStrLenPixels(text, hudMenuFont(menuFont), scale);
const hudTextHeight = (text, scale, menuFont) => hudFontHeightPixels(hudMenuFont(menuFont), scale);
function hudTextPaint(x, y, scale, color, text, adjust, limit, style, menuFont) {
	const or = style === HUD_TEXTSTYLE.BLINK || style === HUD_TEXTSTYLE.PULSE ? HUD_STYLE_BLINK : style >= HUD_TEXTSTYLE.SHADOWED && style <= HUD_TEXTSTYLE.SHADOWEDMORE ? HUD_STYLE_DROPSHADOW : 0;
	hudFontDrawString(x, y, text, color, or | hudMenuFont(menuFont), !limit ? -1 : limit, scale);
}

// CG_DrawProportionalString and CG_DrawScaledProportionalString of cg_drawtools.c. x and y are ints there. UI_RIGHT
// moves the string by half its width, like UI_CENTER: that is what the code of the game does.
function hudDrawProportionalString(x, y, str, style, color) {
	hudDrawScaledProportionalString(x, y, str, style, color, 1.0, (style & HUD_UI_SMALLFONT) ? HUD_FONT_SMALL : HUD_FONT_MEDIUM);
}
function hudDrawScaledProportionalString(x, y, str, style, color, scale, menuFont = HUD_FONT_MEDIUM) {
	x = Math.trunc(x); y = Math.trunc(y);
	const format = style & (HUD_UI_CENTER | HUD_UI_RIGHT);
	if (format === HUD_UI_CENTER || format === HUD_UI_RIGHT) x -= Math.trunc(hudTextWidth(str, scale, menuFont) / 2);
	const textStyle = (style & HUD_UI_DROPSHADOW) ? HUD_TEXTSTYLE.SHADOWED : (style & (HUD_UI_BLINK | HUD_UI_PULSE)) ? HUD_TEXTSTYLE.BLINK : 0;
	hudTextPaint(x, y, scale, color, str, 0, 0, textStyle, menuFont);
}

// CG_DrawChar: a character of the image of the fixed font (gfx/2d/charsgrid_med, 16 x 16 of them)
function hudDrawChar(x, y, width, height, ch) {
	ch &= 255;
	if (ch === 32) return;
	const frow = (ch >> 4) * 0.0625, fcol = (ch & 15) * 0.0625;
	hudStretchPic(x, y, width, height, fcol, frow, fcol + 0.03125, frow + 0.0625, hudMedia().charset);
}

// CG_DrawStringExt (the language is not an Asian one): a string in the fixed font, with its colour codes and a
// shadow 2 to the right and down
function hudDrawStringExt(x, y, string, setColor, forceColor, shadow, charWidth, charHeight) {
	const isColor = i => string.charCodeAt(i) === 94 && i + 1 < string.length && string.charCodeAt(i + 1) !== 94 && hudIsDigit(string.charCodeAt(i + 1));
	if (shadow) {
		hudSetColor([0, 0, 0, setColor[3]]);
		for (let i = 0, xx = x; i < string.length; i++) {
			if (isColor(i)) { i++; continue; }
			hudDrawChar(xx + 2, y + 2, charWidth, charHeight, string.charCodeAt(i));
			xx += charWidth;
		}
	}
	hudSetColor(setColor);
	for (let i = 0, xx = x; i < string.length; i++) {
		if (isColor(i)) {
			if (!forceColor) { const c = HUD_COLOR_TABLE[(string.charCodeAt(i + 1) - 48) & 15]; hudSetColor([c[0], c[1], c[2], setColor[3]]); }
			i++;
			continue;
		}
		hudDrawChar(xx, y, charWidth, charHeight, string.charCodeAt(i));
		xx += charWidth;
	}
	hudSetColor(null);
}
const hudDrawBigString = (x, y, s, alpha) => hudDrawStringExt(x, y, s, [1, 1, 1, alpha], false, true, HUD_BIGCHAR_WIDTH, HUD_BIGCHAR_HEIGHT);
const hudDrawSmallString = (x, y, s, alpha) => hudDrawStringExt(x, y, s, [1, 1, 1, alpha], false, false, HUD_SMALLCHAR_WIDTH, HUD_SMALLCHAR_HEIGHT);
// CG_DrawStrlen: how many characters a string shows, without its colour codes
const hudDrawStrlen = s => s.replace(/\^(?!\^)[0-9]/g, '').length;

// ---- the texts of the game (strings/English/<file>.str, the string packages of the engine)

const hudStrings = new Map();	// file -> Map reference -> text, null while it is on its way
// CG_GetStringEdString( "MP_INGAME", "FOLLOWING" ): the English text, "" while its file is not there
function hudString(section, name) {
	let file = hudStrings.get(section);
	if (file === undefined) {
		hudStrings.set(section, null);
		hudBusy(fetchGameFile('strings/English/' + section + '.str', true).then(text => {
			const refs = new Map();
			let ref = null;
			for (const line of text.split(/\r?\n/)) {
				const m = /^\s*(REFERENCE|LANG_ENGLISH)\s+(.*?)\s*$/.exec(line);
				if (!m) continue;
				if (m[1] === 'REFERENCE') ref = m[2].replace(/^"|"$/g, '');
				else if (ref !== null && !refs.has(ref)) refs.set(ref, m[2].replace(/^"|"$/g, '').replace(/\\n/g, '\n'));
			}
			hudStrings.set(section, refs);
		}, () => { hudStrings.set(section, new Map()); }));
		file = null;
	}
	return (file && file.get(name)) || '';
}
