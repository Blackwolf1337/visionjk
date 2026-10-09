// ---- the weather of a map (rd-vanilla/tr_WorldEffects.cpp): snow, rain, dust, sand and the fog that drifts
//
// The game names the weather of a map among its effects, with a * in front (fx_snow, fx_rain, fx_wind and
// fx_spacedust of g_misc.c put them there, and amweather of JA+ while the map runs), and the client hands each to
// the renderer as a command (CG_ParseWeatherEffect). The boxes of misc_weather_zone say where it is looked up
// whether a point is under the sky (SP_misc_weather_zone of cg_spawn.c). The renderer keeps up to five clouds of
// particles in a box around the eye, moves them once in every frame and draws them when all else of the scene is
// drawn (RB_RenderWorldEffects), without fog. Every number here is the one of that file.

const WE_MAX_WIND_ZONES = 10, WE_MAX_WEATHER_ZONES = 10, WE_MAX_PARTICLE_CLOUDS = 5;
const WE_CELL = 96;		// POINTCACHE_CELL_SIZE
// what a brush is made of, as far as the weather asks (surfaceflags.h)
const WE_SOLID = 0x1, WE_WATER = 0x4, WE_OUTSIDE = 0x10000, WE_INSIDE = 0x10000000;
// CWeatherParticle::mFlags
const WE_RENDER = 1, WE_FADEIN = 2, WE_FADEOUT = 4;
const WE_ASK_MS = 3000;	// how often the names of the effects are read again, for weather that comes while the map runs
const WE_CACHE_MS = 6;		// how long a frame works on the cache of what is under the sky

// The globals of the renderer: mFrozen, mSecondsElapsed, the wind of all zones together (mGlobalWindVelocity,
// its direction and speed), mParticlesRendered, mWindZones and mParticleClouds. last: the time of the frame before.
const we = { frozen: false, sec: 0, wind: [0, 0, 0], windDir: [0, 0, 0], windSpeed: 0, rendered: 0, winds: [], clouds: [], last: 0, level: 0 };
// COutside: mOutsideShake, mOutsidePain, mCacheInit, SWeatherZone::mMarkedOutside and mWeatherZones.
// error: the map has brushes of both kinds, inside and outside, where the game ends with an error. scan: the
// cache while it is being made.
const weOut = { shake: false, pain: 0, init: false, marked: false, zones: [], error: false, scan: null };

// WE_flrand and Q_irand
const weRand = (min, max) => min + Math.random() * (max - min);
const weIrand = (min, max) => min + Math.floor(Math.random() * (max - min + 1));

/* where the sky is open */

// CM_PointContents of qcommon/cm_test.cpp for the map itself: what the brushes are made of that a point is in.
// A point on a plane of the tree is in front of it (CM_PointLeafnum_r), one on a side of a brush is in the brush.
function wePointContents(x, y, z) {
	const t = world.tree, N = t.nodes, P = t.planes, L = t.leafs, LB = t.leafBrushes, B = world.brushes.brushes, S = world.brushes.sides;
	if (!N.length) return 0;
	let n = 0;
	while (n >= 0) { const p = N[n * 9] * 4; n = N[n * 9 + (P[p] * x + P[p + 1] * y + P[p + 2] * z - P[p + 3] < 0 ? 2 : 1)]; }
	const leaf = (-1 - n) * 12;
	let contents = 0;
	for (let k = L[leaf + 10], end = k + L[leaf + 11]; k < end; k++) {
		const b = LB[k] * 3, first = B[b], sides = B[b + 1];
		let i = 0;
		for (; i < sides; i++) { const p = S[(first + i) * 3] * 4; if (P[p] * x + P[p + 1] * y + P[p + 2] * z > P[p + 3]) break; }
		if (i === sides) contents |= (world.shaders[B[b + 2]] || {}).contents | 0;
	}
	return contents;
}

// SnapFloatToGrid: to the nearest whole cell, the half way one towards zero
function weSnap(f, grid) {
	f = Math.trunc(f);
	const neg = f < 0;
	if (neg) f = -f;
	let offset = f % grid;
	if (offset > (grid >> 1)) offset = -(grid - offset);
	f -= offset;
	return neg ? -f : f;
}

// COutside::AddWeatherZone: a box in which it is looked up cell by cell what is under the sky. size: its lower
// corner in cells. Its cache has a bit for every cell, 32 of them above each other in a number.
function weAddZone(mins, maxs) {
	if (weOut.zones.length >= WE_MAX_WEATHER_ZONES) return;
	const lo = mins.map(v => weSnap(v, WE_CELL)), hi = maxs.map(v => weSnap(v, WE_CELL)), size = lo.map(v => v / WE_CELL);
	const width = Math.trunc(hi[0] / WE_CELL - size[0]), height = Math.trunc(hi[1] / WE_CELL - size[1]), depth = (Math.trunc(hi[2] / WE_CELL - size[2]) + 31) >> 5;
	weOut.zones.push({ mins: lo, maxs: hi, size, width, height, depth, cache: new Uint32Array(Math.max(0, width * height * depth)) });
}

// COutside::Cache: every cell of every zone is asked what the map is made of at its middle. The cells in a brush
// that says inside or outside are marked, and which of the two the map uses is kept (mMarkedOutside). A map has
// only one of them. Without a zone the box of the whole map is one.
// The engine does all of it in one frame, which stops the game for a moment on a large map without zones. Here
// it is done a few milliseconds at a time (weCache), so this stops after every row of cells.
function* weCacheScan() {
	const o = weOut;
	if (!o.zones.length) weAddZone(world.models[0].mins, world.models[0].maxs);
	let found = false, marked = false;
	for (const wz of o.zones) {
		const mx = wz.mins[0] + WE_CELL / 2, my = wz.mins[1] + WE_CELL / 2, mz = wz.mins[2] + WE_CELL / 2;
		for (let z = 0; z < wz.depth; z++) for (let q = 0; q < 32; q++) {
			const bit = 1 << q, zbase = z << 5;
			for (let x = 0; x < wz.width; x++) {
				for (let y = 0; y < wz.height; y++) {
					const contents = wePointContents(x * WE_CELL + mx, y * WE_CELL + my, (zbase + q) * WE_CELL + mz);
					if (!(contents & (WE_INSIDE | WE_OUTSIDE))) continue;
					const outside = (contents & WE_OUTSIDE) !== 0;
					if (!found) { found = true; marked = outside; }
					else if (marked !== outside) {
						// "Weather Effect: Both Indoor and Outdoor brushs encountered in map": the game drops the client
						o.error = o.init = true;
						console.warn('weather: the map has brushes that say inside and brushes that say outside, the game ends with an error here');
						return;
					}
					wz.cache[z * wz.width * wz.height + y * wz.width + x] |= bit;
				}
				yield;
			}
		}
	}
	// no such brush at all: everything is under the sky
	o.marked = found && marked;
	o.init = true;
}

// the cache, as far as WE_CACHE_MS go in this frame. Nothing of the weather moves or is drawn until it is whole.
function weCache() {
	if (!world || weOut.init) return;
	const scan = weOut.scan || (weOut.scan = weCacheScan()), until = performance.now() + WE_CACHE_MS;
	while (!scan.next().done && performance.now() < until);
}

// SWeatherZone::CellOutside: a cell that is marked is what the map marks, one beyond the zone is the other
function weCellOutside(wz, x, y, z, bit) {
	if (x < 0 || x >= wz.width || y < 0 || y >= wz.height || z < 0 || z >= wz.depth || bit < 0 || bit >= 32) return !weOut.marked;
	return weOut.marked === !!(wz.cache[z * wz.width * wz.height + y * wz.width + x] & (1 << bit));
}

// COutside::PointOutside for something that is width wide and height high: the zone it is in says it, and what
// is as large as a cell or larger needs all the cells around it to be outside. In no zone: what the map does not mark.
function weOutsideAt(x, y, z, width, height) {
	for (const wz of weOut.zones) {
		// SVecRange::In: inside on all three axes, the faces of the box are not in it
		if (!(x > wz.mins[0] && y > wz.mins[1] && z > wz.mins[2] && x < wz.maxs[0] && y < wz.maxs[1] && z < wz.maxs[2])) continue;
		// SWeatherZone::ConvertToCell, in the floats of the engine
		const cx = Math.trunc(Math.fround(Math.fround(x / WE_CELL) - wz.size[0])), cy = Math.trunc(Math.fround(Math.fround(y / WE_CELL) - wz.size[1]));
		const cz = Math.trunc(Math.fround(Math.fround(z / WE_CELL) - wz.size[2])), bit = cz & 31, row = cz >> 5;
		if (width < WE_CELL || height < WE_CELL) return weCellOutside(wz, cx, cy, row, bit);
		const w = Math.trunc(Math.trunc(width) / WE_CELL), h = Math.trunc(Math.trunc(height) / WE_CELL);
		for (let ix = cx - w; ix <= cx + w; ix++) for (let iy = cy - w; iy <= cy + w; iy++) for (let ib = bit - h; ib <= bit + h; ib++) {
			if (!weCellOutside(wz, ix, iy, row, ib)) return false;
		}
		return true;
	}
	return !weOut.marked;
}

// R_IsOutside: whether a point is under the sky. Before the cache is made the map is asked (ContentsOutside).
function weatherOutside(pos) {
	if (weOut.init) return weOutsideAt(pos[0], pos[1], pos[2], 0, 0);
	const contents = world ? wePointContents(pos[0], pos[1], pos[2]) : 0;
	return !(contents & (WE_WATER | WE_SOLID)) && !!(contents & WE_OUTSIDE);
}

/* the wind */

// CWindZone::Initialize: a wind that changes by itself
function weWind() {
	return { global: true, velMin: [-1500, -1500, -10], velMax: [1500, 1500, 10], maxDelta: 10, duration: [1000, 2000], chanceOfDead: 0.3, dead: [1000, 3000],
		cur: [0, 0, 0], target: [0, 0, 0], remaining: 0 };
}

// CWindZone::Update, once in every frame: the wind turns towards its target by at most maxDelta, and picks a new
// target (or a calm) when the old one has run out. What runs out is counted in frames. -1: it never changes.
function weWindUpdate(w) {
	if (w.remaining === 0) {
		if (Math.random() < w.chanceOfDead) {
			w.remaining = weIrand(w.dead[0], w.dead[1]);
			w.target = [0, 0, 0];
		} else {
			w.remaining = weIrand(w.duration[0], w.duration[1]);
			w.target = w.velMin.map((v, i) => weRand(v, w.velMax[i]));
		}
	} else if (w.remaining !== -1) {
		w.remaining--;
		const d = w.target.map((v, i) => v - w.cur[i]), len = Math.hypot(d[0], d[1], d[2]), step = Math.min(len, w.maxDelta);
		if (len) for (let i = 0; i < 3; i++) w.cur[i] += d[i] / len * step;
	}
}

/* a cloud of particles */

// CWeatherParticleCloud::Reset and Initialize: a cloud with the values it has before a command changes them.
// verts: 3 a particle is a triangle, 4 a quad. Every particle picks its mass here, between 5 and 10: the commands
// for dust, sand and fog set another range (10 to 30) only after that, which changes nothing and is left out.
function weCloud(count, image, verts) {
	count = Math.max(0, count | 0);
	const c = { count, verts, populated: false, rendering: 0,
		// R_FindImageFile with GL_CLAMP. The loader of the engine has none for a .bmp and goes on to the other endings.
		image: gameImage(image.replace(/\.bmp$/i, ''), true), imageName: image,
		orient: false, water: false, planeDist: 500, planeSize: 500, spawnMins: [-625, -625, -625], spawnMaxs: [625, 625, 625],
		gravity: 300, color: [1, 1, 1, 1], width: 1, height: 1, blend: 0, filter: 0, fade: 10,
		rotation: [-0.7, 0.7], rotDelta: 0, rotTarget: 0, rotCurrent: 0, rotTimer: [500, 2000], rotNext: -1, friction: 0.7,
		pos: new Float32Array(count * 3), vel: new Float32Array(count * 3), alpha: new Float32Array(count), mass: new Float32Array(count), flags: new Uint8Array(count),
		mins: [0, 0, 0], maxs: [0, 0, 0], norm: [0, 0, 0], right: [0, 0, 0], up: [0, 0, 0], speed: 0, corners: new Float32Array(12) };
	for (let i = 0; i < count; i++) c.mass[i] = weRand(5, 10);
	return c;
}

// SVecRange::Wrap: what left the box on one side comes back in on the other, as far in as it was out
function weWrap(P, o, mins, maxs) {
	for (let i = 0; i < 3; i++) {
		const size = maxs[i] - mins[i];
		if (P[o + i] < mins[i]) P[o + i] = maxs[i] - (mins[i] - P[o + i]) % size;
		if (P[o + i] > maxs[i]) P[o + i] = mins[i] + (P[o + i] - maxs[i]) % size;
	}
}

// CWeatherParticleCloud::Update, once in every frame. at and fwd: the eye and the way it looks. left and up: the
// axes of the view the particles are turned to. still: nothing moves (mFrozen), only the corners are worked out.
function weCloudUpdate(c, at, fwd, left, up, still) {
	const fade = c.fade * we.sec;
	// the two half sides of a particle (mCameraLeft and mCameraDown, which points up), turned by its rotation
	let l = left.map(v => v * c.width), d = up.map(v => v * c.height);
	if (c.rotNext !== -1) {
		if (c.rotNext === 0) {
			c.rotTarget = weRand(c.rotation[0], c.rotation[1]);
			c.rotNext = weIrand(c.rotTimer[0], c.rotTimer[1]);
			if (c.rotNext <= 0) c.rotNext = 1;
		}
		c.rotNext--;
		const diff = c.rotTarget - c.rotDelta;
		if (Math.abs(diff) > 0.01) c.rotDelta += diff;
		c.rotCurrent += c.rotDelta * we.sec;
		const s = Math.sin(c.rotCurrent), co = Math.cos(c.rotCurrent);
		l = left.map((v, i) => v * co * c.width - up[i] * s * c.width);
		d = up.map((v, i) => v * co * c.height + left[i] * s * c.height);
	}

	// gravity and the wind
	const force = [we.wind[0], we.wind[1], we.wind[2] - c.gravity], plane = c.gravity !== 0;	// UseSpawnPlane
	// the box the particles live in. With a plane they come from it is longer on the side they come from.
	for (let i = 0; i < 3; i++) { c.mins[i] = at[i] + c.spawnMins[i]; c.maxs[i] = at[i] + c.spawnMaxs[i]; }
	if (plane) {
		for (let i = 0; i < 3; i++) {
			if (force[i] > 0.01) c.mins[i] -= c.planeDist / 2;
			else if (force[i] < -0.01) c.maxs[i] += c.planeDist / 2;
		}
		c.speed = Math.hypot(force[0], force[1], force[2]);
		c.norm = c.speed ? force.map(v => v / c.speed) : force.slice();
		[c.right, c.up] = fxNormals(c.norm);
		// rain: a streak along the way it falls
		if (c.orient) d = c.norm.map(v => v * -c.height);
	}
	// the corners in the order they are drawn: a triangle at the particle, left of it and left below it, a quad
	// around it (mCameraLeftPlusUp is left minus down, mCameraLeftMinusUp left plus down)
	const K = c.corners;
	for (let i = 0; i < 3; i++) {
		if (c.verts === 4) { K[i] = -(l[i] + d[i]); K[3 + i] = -(l[i] - d[i]); K[6 + i] = l[i] + d[i]; K[9 + i] = l[i] - d[i]; }
		else { K[i] = 0; K[3 + i] = l[i]; K[6 + i] = d[i] + l[i]; K[9 + i] = 0; }
	}
	if (still) return;

	const P = c.pos, V = c.vel, mins = c.mins, maxs = c.maxs, sec = we.sec, top = c.color[3];
	c.rendering = 0;
	for (let n = 0, o = 0; n < c.count; n++, o += 3) {
		// the first time: anywhere in the box
		if (!c.populated) for (let i = 0; i < 3; i++) P[o + i] = weRand(mins[i], maxs[i]);
		// the force by its mass onto its speed, once a frame however long the frame is, and the air slows it
		const m = c.mass[n];
		V[o] = (V[o] + force[0] / m) * c.friction; V[o + 1] = (V[o + 1] + force[1] / m) * c.friction; V[o + 2] = (V[o + 2] + force[2] / m) * c.friction;
		P[o] += V[o] * sec; P[o + 1] += V[o + 1] * sec; P[o + 2] += V[o + 2] * sec;
		const x = P[o], y = P[o + 1], z = P[o + 2];
		let flags = c.flags[n], shown = (flags & WE_RENDER) !== 0;
		const inRange = x > mins[0] && y > mins[1] && z > mins[2] && x < maxs[0] && y < maxs[1] && z < maxs[2];
		// seen: under the sky, in the box and in front of the eye
		const inView = inRange && (x - at[0]) * fwd[0] + (y - at[1]) * fwd[1] + (z - at[2]) * fwd[2] > 0 && weOutsideAt(x, y, z, c.width, c.height);

		// out of the box and faded away: it starts again, on the plane the wind carries it from, or on the other side of the box
		if (!inRange && !shown) {
			V[o] = V[o + 1] = V[o + 2] = 0;
			if (plane) {
				const r = weRand(-c.planeSize, c.planeSize), u = weRand(-c.planeSize, c.planeSize);
				for (let i = 0; i < 3; i++) P[o + i] = at[i] - c.norm[i] * c.planeDist + c.right[i] * r + c.up[i] * u;
			} else weWrap(P, o, mins, maxs);
		}

		// it fades out when it is not seen any more and in when it is seen again, by mFade a second
		if (shown && !inView) flags = (flags & ~WE_FADEIN) | WE_FADEOUT;
		else if (shown && inView && (flags & WE_FADEOUT)) flags = (flags & ~WE_FADEOUT) | WE_FADEIN;
		else if (!shown && inView) { shown = true; c.alpha[n] = 0; flags = WE_RENDER | WE_FADEIN; }
		if (shown) {
			if (flags & WE_FADEOUT) {
				c.alpha[n] -= fade;
				if (c.alpha[n] <= 0) { c.alpha[n] = 0; flags = 0; }
			} else if (flags & WE_FADEIN) {
				c.alpha[n] += fade;
				if (c.alpha[n] >= top) { flags &= ~WE_FADEIN; c.alpha[n] = top; }
			}
		}
		c.flags[n] = flags;
		if (flags & WE_RENDER) c.rendering++;
	}
	c.populated = true;
}

/* drawing */

// A particle is drawn by where it is and its alpha alone: its corners and its colour are the same for all of a
// cloud. The image times the colour, as the fixed pipeline of the engine does it (qglColor4f and GL_MODULATE).
let weGl = null;
function weGlNow() {
	if (weGl) return weGl;
	const prog = compile(`#version 300 es
layout(location=0) in vec4 aPart;	// where a particle is, and its alpha
uniform mat4 uMvp; uniform vec3 uCorner[4]; uniform vec2 uSt[4]; uniform vec4 uColor; uniform int uBlend;
out vec2 vSt; out vec4 vCol;
void main() {
	vSt = uSt[gl_VertexID];
	// blended by its alpha the alpha of the particle is the alpha alone, added it darkens all of the colour
	vCol = uBlend == 0 ? vec4(uColor.rgb, aPart.w) : uColor * aPart.w;
	gl_Position = uMvp * vec4(aPart.xyz + uCorner[gl_VertexID], 1.0);
}`, `#version 300 es
precision highp float;
in vec2 vSt; in vec4 vCol; uniform sampler2D uTex; out vec4 o;
void main() { o = vCol * texture(uTex, vSt); }`);
	const vao = gl.createVertexArray(), vbo = gl.createBuffer();
	gl.bindVertexArray(vao);
	gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
	gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 4, gl.FLOAT, false, 16, 0);
	gl.vertexAttribDivisor(0, 1);
	gl.bindVertexArray(null);
	// The engine sets the filter of the image when it draws: GL_LINEAR or GL_NEAREST, no mipmaps, clamped. Here
	// that is a sampler, so the image keeps what its other users set.
	const samplers = [gl.LINEAR, gl.NEAREST].map(filter => {
		const s = gl.createSampler();
		gl.samplerParameteri(s, gl.TEXTURE_MIN_FILTER, filter); gl.samplerParameteri(s, gl.TEXTURE_MAG_FILTER, filter);
		gl.samplerParameteri(s, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.samplerParameteri(s, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		return s;
	});
	return (weGl = { prog, vao, vbo, samplers, data: new Float32Array(4096), room: 0 });
}

// the texture coordinates of the corners: of a triangle and of a quad (CWeatherParticleCloud::Render)
const WE_ST = [null, null, null, new Float32Array([1, 0, 0, 1, 0, 0, 0, 0]), new Float32Array([0, 0, 1, 0, 1, 1, 0, 1])];

// CWeatherParticleCloud::Render: the particles that are shown. Behind what is nearer, they write no depth, both
// sides are drawn. mBlendMode 0 blends by the alpha, 1 adds.
function weCloudRender(c) {
	if (!c.rendering || !c.image.ready) return;
	const g = weGlNow();
	if (g.data.length < c.count * 4) g.data = new Float32Array(c.count * 4);
	const D = g.data, P = c.pos;
	let n = 0;
	for (let i = 0; i < c.count; i++) {
		if (!(c.flags[i] & WE_RENDER)) continue;
		D[n] = P[i * 3]; D[n + 1] = P[i * 3 + 1]; D[n + 2] = P[i * 3 + 2]; D[n + 3] = c.alpha[i];
		n += 4;
	}
	gl.bindBuffer(gl.ARRAY_BUFFER, g.vbo);
	if (g.room < n) { gl.bufferData(gl.ARRAY_BUFFER, D.byteLength, gl.DYNAMIC_DRAW); g.room = D.length; }
	gl.bufferSubData(gl.ARRAY_BUFFER, 0, D, 0, n);
	gl.uniform3fv(g.prog.u['uCorner[0]'], c.corners);
	gl.uniform2fv(g.prog.u['uSt[0]'], WE_ST[c.verts]);
	gl.uniform4fv(g.prog.u.uColor, c.color);
	gl.uniform1i(g.prog.u.uBlend, c.blend);
	if (c.blend === 0) gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); else gl.blendFunc(gl.ONE, gl.ONE);
	gl.bindTexture(gl.TEXTURE_2D, c.image.tex);
	gl.bindSampler(0, g.samplers[c.filter]);
	gl.drawArraysInstanced(gl.TRIANGLE_FAN, 0, c.verts, n / 4);
	we.rendered += c.rendering;
}

/* what the game says */

// R_InitWorldEffects: no clouds, no wind, no zones and no cache
function weInit() {
	we.clouds.length = 0;
	we.winds.length = 0;
	Object.assign(weOut, { shake: false, pain: 0, init: false, marked: false, zones: [], error: false, scan: null });
}

// RE_WorldEffectCommand: a line of the game for the weather. Its words are what stands between blanks
// (COM_ParseExt), and a vector is "( x y z )" with the brackets as words of their own (WE_ParseVector).
function weCommand(command) {
	const tok = String(command).split(/[\0-\x20]+/).filter(t => t), name = (tok.shift() || '').toLowerCase();
	const vector = () => {
		if (tok.shift() !== '(') return null;
		const v = [tok.shift(), tok.shift(), tok.shift()];
		return v[2] === undefined || tok.shift() !== ')' ? null : v.map(t => parseFloat(t) || 0);
	};
	const wind = set => { if (we.winds.length < WE_MAX_WIND_ZONES) we.winds.push(Object.assign(weWind(), set)); };
	const cloud = (count, image, verts, set) => {
		if (we.clouds.length >= WE_MAX_PARTICLE_CLOUDS) return false;
		we.clouds.push(Object.assign(weCloud(count, image, verts), set));
		return true;
	};
	const rain = (count, set) => cloud(count, 'gfx/world/rain.jpg', 3, Object.assign({ height: 80, width: 1.2, gravity: 2000, filter: 1, blend: 1, fade: 100, color: [0.5, 0.5, 0.5, 0.5], orient: true, water: true }, set));
	const smoke = 'gfx/effects/alpha_smoke2b.tga', flat = { spawnMins: [-625, -625, -150], spawnMaxs: [625, 625, 150] };
	switch (name) {
	case 'die': weInit(); break;
	case 'clear': we.clouds.length = 0; we.winds.length = 0; break;
	case 'freeze': we.frozen = !we.frozen; break;
	case 'zone': { const mins = vector(), maxs = mins && vector(); if (maxs) weAddZone(mins, maxs); break; }
	case 'wind': wind({}); break;
	// a vector that can not be read is 800 along y
	case 'constantwind': wind({ cur: vector() || [0, 800, 0], remaining: -1 }); break;
	case 'gustingwind': wind({ velMin: [-3000, -3000, -100], velMax: [3000, 3000, 100], maxDelta: 10, duration: [1000, 3000], chanceOfDead: 0.5, dead: [2000, 4000] }); break;
	case 'lightrain': rain(500, {}); break;
	case 'rain': rain(1000, {}); break;
	case 'acidrain': if (rain(1000, { width: 2, color: [0.34, 0.70, 0.34, 0.70] })) weOut.pain = 0.1; break;
	case 'heavyrain': rain(1000, { gravity: 2800, fade: 15 }); break;
	case 'snow': cloud(1000, 'gfx/effects/snowflake1.bmp', 4, { blend: 1, rotNext: 0, color: [0.75, 0.75, 0.75, 0.75], water: true }); break;
	case 'spacedust':
		if (we.clouds.length < WE_MAX_PARTICLE_CLOUDS) cloud(parseInt(tok.shift(), 10) || 0, 'gfx/effects/snowpuff1.tga', 4, { height: 1.2, width: 1.2, gravity: 0, blend: 1, rotNext: 0, color: [0.75, 0.75, 0.75, 0.75], water: true,
			spawnMins: [-1500, -1500, -1500], spawnMaxs: [1500, 1500, 1500] });
		break;
	case 'sand': cloud(400, smoke, 4, Object.assign({ gravity: 0, width: 70, height: 70, color: [0.9, 0.6, 0.0, 0.5], fade: 5, rotNext: 0 }, flat)); break;
	case 'fog': cloud(60, smoke, 4, Object.assign({ blend: 1, gravity: 0, width: 70, height: 70, color: [0.2, 0.2, 0.2, 0.2], fade: 5, rotNext: 0 }, flat)); break;
	case 'heavyrainfog': cloud(70, smoke, 4, Object.assign({ blend: 1, gravity: 0, width: 100, height: 100, color: [0.3, 0.3, 0.3, 0.3], fade: 1, rotNext: 0 }, flat)); break;
	case 'light_fog': cloud(40, smoke, 4, Object.assign({ blend: 1, gravity: 0, width: 100, height: 100, color: [0.19, 0.6, 0.7, 0.12], fade: 0.10, rotNext: 0 }, flat)); break;
	case 'outsideshake': weOut.shake = !weOut.shake; break;
	case 'outsidepain': weOut.pain = weOut.pain ? 0 : 1; break;
	default: break;		// the engine prints the list of the commands
	}
}

// The misc_weather_zone of the map file that are there in a gametype (CG_ParseEntityFromSpawnVars), as the box
// of their brush model each (SP_misc_weather_zone, R_ModelBounds). The words of the text as mapEnts reads them.
function weMapZones(gametype) {
	const out = [], re = /"([^"]*)"|[{}]|[^\s{}"]+/g;
	for (let m, e = null, key = null; (m = re.exec(world.entityText || ''));) {
		if (m[0] === '{') { e = []; key = null; }
		else if (m[0] === '}') {
			if (e && (spawnKey(e, 'classname') || '').toLowerCase() === 'misc_weather_zone' && spawnsIn(e, gametype)) {
				const model = spawnKey(e, 'model') || '', b = model[0] === '*' ? world.models[+model.slice(1)] : null;
				// without a model the game ends with an error, and a model that is none has no size
				if (model) out.push(b ? [b.mins, b.maxs] : [[0, 0, 0], [0, 0, 0]]);
			}
			e = null;
		} else if (e) {
			const word = m[1] !== undefined ? m[1] : m[0];
			if (key === null) key = word; else { e.push(key, word); key = null; }
		}
	}
	return out;
}

// What the client game hands the renderer, in its order: when it comes into a map the names of the effects that
// begin with a *, by their numbers (CG_RegisterSounds), then the zones of the map file (CG_ParseEntitiesFromString),
// and after that every such name the game adds (CG_ConfigStringModified). The game never takes a name back while
// a map runs, so the list is read again from time to time and what is new of it is taken.
const weFed = { world: null, time: 0, names: 0, zoned: false, asked: 0, busy: false, run: 0 };
function weFeed() {
	const f = weFed;
	// the map that runs is not the one that is shown yet
	if (!world || !state || state.map !== loadedMap) return false;
	if (f.world !== world || state.time < f.time) {
		// another map, or this one from its start: the client game ends the weather when it goes (CG_Shutdown)
		weCommand('die');
		Object.assign(f, { world, names: 0, zoned: false, asked: 0, run: f.run + 1 });
	}
	f.time = state.time;
	const now = performance.now(), run = f.run;
	if (f.busy || (f.asked && now - f.asked < WE_ASK_MS)) return f.zoned;
	f.busy = true;
	f.asked = now;
	fetch('api/list?kind=fxindex', { cache: 'no-store' }).then(res => res.json()).then(json => {
		if (run !== f.run || !Array.isArray(json.names)) return;
		// CG_ParseWeatherEffect: the name without its *
		for (const name of json.names.slice(f.names)) if (name[0] === '*') weCommand(name.slice(1));
		f.names = Math.max(f.names, json.names.length);
		if (!f.zoned) {
			f.zoned = true;
			for (const [mins, maxs] of weMapZones(state ? state.gametype : 0)) weAddZone(mins, maxs);
		}
	}).catch(() => { /* it is asked for again */ }).finally(() => { f.busy = false; });
	return f.zoned;
}

/* a frame */

// RB_RenderWorldEffects without the drawing: one frame of msec milliseconds for the wind and the clouds.
// still: no time has passed, nothing moves.
function weFrame(msec, at, fwd, left, up, still) {
	// the time of a frame is 1 to 1000 msec for the weather
	we.sec = Math.min(1000, Math.max(1, msec)) / 1000;
	if (!we.frozen && !still) {
		we.wind = [0, 0, 0];
		for (const w of we.winds) {
			weWindUpdate(w);
			if (w.global) for (let i = 0; i < 3; i++) we.wind[i] += w.cur[i];
		}
		we.windSpeed = Math.hypot(we.wind[0], we.wind[1], we.wind[2]);
		we.windDir = we.windSpeed ? we.wind.map(v => v / we.windSpeed) : we.wind.slice();
	}
	for (const c of we.clouds) weCloudUpdate(c, at, fwd, left, up, we.frozen || still);
}

// At the end of the scene, after everything else of it is drawn. The time of a frame is the one of the renderer:
// whole milliseconds since the frame before, at most 500 (RE_RenderScene).
//
// In the comparison with the game (the page opened with ?ref=) the weather is not around the eye of the page but
// around the eye of the client of the game, which the feed has as its first player, and it runs by the clock of
// the level in frames of 8 msec, the 125 a second of that client. So it has been through what the weather of
// the client has: where its eye was put, and how long ago. The page itself is told that only for the picture.
function drawWeather(now) {
	const fed = weFeed();
	let msec = Math.min(500, Math.max(0, Math.trunc(now) - Math.trunc(we.last))), frames = 1;
	we.last = now;
	if (REF && state) {
		msec = Math.min(4000, Math.max(0, state.time - we.level));
		we.level = state.time;
		frames = Math.ceil(msec / 8);
	}
	if (!fed || !we.clouds.length) return;
	// the frame that finds no cache makes it and does nothing else
	if (!weOut.init) { weCache(); return; }
	if (weOut.error) return;
	const axes = cameraAxes();
	let at = eye, fwd = axes.fwd;
	const client = REF ? players.get(0) : null;
	if (client) {
		const d = client.data, p = d.pitch * Math.PI / 180, y = d.yaw * Math.PI / 180;
		at = [d.pos[0], d.pos[1], d.pos[2] + (d.viewheight || 0)];
		fwd = [Math.cos(p) * Math.cos(y), Math.cos(p) * Math.sin(y), -Math.sin(p)];
	}
	we.rendered = 0;
	if (!frames) weFrame(0, at, fwd, axes.left, axes.up, true);
	for (let i = 0; i < frames; i++) weFrame(REF ? Math.min(8, msec - i * 8) : msec, at, fwd, axes.left, axes.up, false);

	const g = weGlNow(), program = gl.getParameter(gl.CURRENT_PROGRAM);
	gl.useProgram(g.prog.p);
	gl.uniformMatrix4fv(g.prog.u.uMvp, false, viewProj);
	gl.uniform1i(g.prog.u.uTex, 0);
	gl.activeTexture(gl.TEXTURE0);
	gl.bindVertexArray(g.vao);
	gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL); gl.depthMask(false);
	gl.disable(gl.CULL_FACE);
	gl.enable(gl.BLEND);
	// the alpha of the canvas stays 1 whatever is blended, as in the map
	gl.colorMask(true, true, true, false);
	for (const c of we.clouds) weCloudRender(c);
	gl.bindSampler(0, null);
	gl.bindVertexArray(null);
	gl.disable(gl.BLEND);
	gl.depthMask(true); gl.depthFunc(gl.LESS);
	gl.colorMask(true, true, true, true);
	gl.useProgram(program);
}
