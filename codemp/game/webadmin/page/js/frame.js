/* ------------------------------------------------------------------ */
/* render loop                                                         */
/* ------------------------------------------------------------------ */

// how many pixels of the picture a pixel of the page is: one for the comparison with the game
const canvasScale = () => REF ? 1 : Math.min(window.devicePixelRatio || 1, 2);
function resize() {
	const dpr = canvasScale();
	canvas.width = Math.round(canvas.clientWidth * dpr);
	canvas.height = Math.round(canvas.clientHeight * dpr);
}
window.addEventListener('resize', resize);

function drawMesh(mesh, x, y, z, yaw, scale, color) {
	const c = Math.cos(yaw), s = Math.sin(yaw);
	gl.uniformMatrix4fv(flatProg.u.uModel, false, new Float32Array([c * scale, s * scale, 0, 0, -s * scale, c * scale, 0, 0, 0, 0, scale, 0, x, y, z, 1]));
	gl.uniform4fv(flatProg.u.uColor, color);
	gl.bindVertexArray(mesh.vao);
	gl.drawElements(gl.TRIANGLES, mesh.count, gl.UNSIGNED_SHORT, 0);
}

let lastFrame = performance.now();
// the keys that move the camera
function moveCamera(dt) {
	// keyboard camera
	const speed = cam.dist * 1.2 * dt, y = cam.yaw * Math.PI / 180;
	const fwd = (keys.has('w') || keys.has('arrowup') ? 1 : 0) - (keys.has('s') || keys.has('arrowdown') ? 1 : 0);
	const right = (keys.has('d') || keys.has('arrowright') ? 1 : 0) - (keys.has('a') || keys.has('arrowleft') ? 1 : 0);
	// up and down: a map has floors
	const rise = (keys.has('pageup') ? 1 : 0) - (keys.has('pagedown') ? 1 : 0);
	if (cam.fly) {
		// along the line of sight and across it, Q and E down and up, Shift faster
		const d = camDir(), v = cam.flySpeed * (keys.has('shift') ? 3 : 1) * dt, up = rise + (keys.has('e') ? 1 : 0) - (keys.has('q') ? 1 : 0);
		cam.pos[0] += (d[0] * fwd + Math.sin(y) * right) * v;
		cam.pos[1] += (d[1] * fwd - Math.cos(y) * right) * v;
		cam.pos[2] += (d[2] * fwd + up) * v;
	} else {
		if (fwd || right) {
			cam.follow = -1;
			cam.target[0] += (Math.cos(y) * fwd + Math.sin(y) * right) * speed;
			cam.target[1] += (Math.sin(y) * fwd - Math.cos(y) * right) * speed;
		}
		if (keys.has('q')) cam.yaw += 90 * dt;
		if (keys.has('e')) cam.yaw -= 90 * dt;
		if (rise) { cam.follow = -1; cam.target[2] += rise * speed; }
	}
}

// The plain view of the map: an image times the light for every surface. The view of the editor, and of the
// administration until the shader scripts are there.
function drawWorldPlain(now, editor) {
	const wire = editor && $('dbgWire').checked;
	gl.useProgram(mapProg.p);
	gl.uniformMatrix4fv(mapProg.u.uMvp, false, viewProj);
	gl.uniform3fv(mapProg.u.uEye, eye);
	gl.uniform1f(mapProg.u.uFogFar, Math.max(4000, cam.dist * 3));
	gl.uniform1i(mapProg.u.uTex, 0);
	gl.uniform1i(mapProg.u.uLm, 1);
	gl.uniform1i(mapProg.u.uView, editor ? +$('dbgLight').value : 0);
	gl.disable(gl.CULL_FACE);
	const open = viewCulls();
	// the lines lie in the surfaces: those are pushed back a little so the lines win
	if (wire) { gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(1, 1); }
	let placed = null;
	for (const b of world.batches) {
		const places = moverPlaces(b.model, now);
		if (!places.length) continue;
		// the side the game draws, which the script of the shader says once the scripts are read
		if (open) setCull(scripts.state === 2 ? materialFor(b.shaderName, !!b.lm).cull : 0);
		let t = world.textures.get(b.shaderName);
		if (t.failed) t = b.editorTex || (b.editorTex = editorImage(b.shaderName)) || t;
		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, t.tex);
		gl.activeTexture(gl.TEXTURE1);
		gl.bindTexture(gl.TEXTURE_2D, b.lm || grayTex);
		gl.uniform1i(mapProg.u.uUseLm, b.lm ? 1 : 0);
		gl.uniform1i(mapProg.u.uAlphaTest, t.alpha ? 1 : 0);
		gl.bindVertexArray(b.vao);
		for (const place of places) {
			if (place !== placed) {
				placed = place;
				gl.uniformMatrix4fv(mapProg.u.uMvp, false, place ? M4.mul(viewProj, place) : viewProj);
				gl.uniform3fv(mapProg.u.uEye, place ? eyeAt(place) : eye);
			}
			gl.drawElements(gl.TRIANGLES, b.count, gl.UNSIGNED_INT, 0);
		}
	}
	gl.disable(gl.CULL_FACE);
	if (wire) { gl.disable(gl.POLYGON_OFFSET_FILL); drawWorldWire(now); }
}

// what else the game draws as a model: the props and the models of the map that are entities
// WP_TRIP_MINE, EF_FIRING and the effect of cgs.effects.tripmineLaserFX (cg_weaponinit.c)
const MINE_WEAPON = 13, MINE_EF_FIRING = 1 << 9, MINE_BEAM = 'tripMine/laserMP';
let mineBeams = null;
function drawModelEntities(now, k) {
	if (state) {
		// A saber that was thrown is one of them, as its hilt. The game lays it flat and lets it spin (the client
		// tilts it there, the server only turns it), and its blade is on: whose saber an entity is, the players say.
		const thrown = new Map();
		for (const p of players.values()) if (p.data.saberInFlight && p.data.saberEnt > 0 && !p.data.dead) thrown.set(p.data.saberEnt, p.data);
		// the beam of a trip mine is an effect the game plays anew in every frame of a client (fxFramePlays): how
		// often in this frame of the page is asked with the first mine
		let beams = null;
		for (const m of state.models || []) {
			// what a client that looks from here is not sent, it does not draw
			if (!clientSees(m.pos)) continue;
			const model = staticNow(m.model), pos = m.prev.map((v, i) => v + (m.pos[i] - v) * k), owner = thrown.get(m.id);
			if (!owner) {
				const place = anglesMatrix(pos, m.angles, m.scale / 100);
				if (model) drawStatic(model, place);
				// A trip mine that sticks and is armed (CG_General: time -1, EF_FIRING): its beam starts 6.6 in front of
				// it and runs the way the server says (pos.trDelta) until it meets the map.
				if (m.weapon === MINE_WEAPON && m.time === -1 && (m.eFlags & MINE_EF_FIRING) && m.delta) {
					if (beams === null) beams = fxFramePlays(mineBeams || (mineBeams = fxFrames()), frameNow, fxOn() && fxReady(fxGet(MINE_BEAM)));
					const s = m.scale / 100, l = Math.hypot(m.delta[0], m.delta[1], m.delta[2]) || 1;
					fxFramePlay(mineBeams, m.id, beams, MINE_BEAM, [pos[0] + place[0] / s * 6.6, pos[1] + place[1] / s * 6.6, pos[2] + place[2] / s * 6.6], [m.delta[0] / l, m.delta[1] / l, m.delta[2] / l]);
				}
				continue;
			}
			// 800 degrees a second is how the game spins it, on from where it was at the last poll
			const yaw = m.angles[1] + SABER_SPIN * Math.min(pollInterval * 2, now - lastPoll) / 1000, r = yaw * Math.PI / 180;
			const at = anglesMatrix(pos, [90, yaw, 0], 1);
			if (model) drawStatic(model, at);
			// Its blades start at the bolts of the hilt, as in a hand (CG_AddSaberBlade from the saber entity): *blade1
			// and on, *flash for a hilt of the old kind. The second blade of a staff is off with saberHolstered 1.
			// A hilt that is not read yet: the first blade along the way the hilt lies.
			const saber = (owner.sabers || [])[0];
			(saber ? saber.blades : []).forEach((blade, bi) => {
				if (bi > 0 && owner.saberHolstered === 1) return;
				const tag = model && (model.tags['*blade' + (bi + 1)] || (bi ? null : model.tags['*flash']));
				let org, dir;
				if (tag) { const f = M4.mul(at, to44(tag)); org = [f[12], f[13], f[14]]; dir = fxUnit([-f[0], -f[1], -f[2]]); }
				else if (!bi && !model) { dir = [Math.cos(r), Math.sin(r), 0]; org = [pos[0] + dir[0] * 6, pos[1] + dir[1] * 6, pos[2]]; }
				else return;
				// lenMax and packed: its full length and the colour of an RGB saber, for the blade as the game draws it (js/sabers.js)
				blades.push({ org, dir, len: blade.length, lenMax: blade.length, radius: blade.radius, color: blade.color, packed: blade.packed || 0 });
			});
		}
	}
}

// what the editor lays over the map: boxes and links, arrows, the outline of a surface, the route, the markers
function drawEditorMarks(now, editor) {
	if (editor) drawEditorLines();
	if (editor && $('dbgMarkers').checked && (mode === 'world' || mode === 'mapinfo')) drawFacing();
	if (editor && mode === 'mapinfo' && surfShown && world.surfs[surfShown.surface]) drawSurfaceWire(now);
	if (editor && mode === 'routes') drawRoute();
	if (editor && $('dbgMarkers').checked) {
		for (const ent of ents.values()) {
			if (!entVisible(ent)) continue;
			const c = classColor(ent.classname), sel = ent.id === selectedEnt || picked.has(ent.id);
			const pos = ghost && ghost.id === ent.id ? ghost.pos : ent.pos;
			// what is not on the server yet has a marker of its own
			if (ent.draft) { drawDraftMark(pos, c, sel, now); continue; }
			drawMesh(cubeMesh, pos[0], pos[1], pos[2], 0, sel ? 1.6 : 1, sel ? [1, 0.2, 0.3, 1] : [c[0], c[1], c[2], 1]);
		}
		if (selectedEnt >= 0 && ents.has(selectedEnt)) {
			const pos = ghost && ghost.id === selectedEnt ? ghost.pos : ents.get(selectedEnt).pos;
			drawMesh(ringMesh, pos[0], pos[1], pos[2] - 8, now / 900, 0.6, [1, 0.15, 0.25, 1]);
		}
	}
	if (editor) drawBoxHandles();
}

// the ring where a right-click went, for a moment
function drawMarker(now) {
	if (marker) {
		const age = (now - marker.t) / 1000;
		if (age > 1.2) marker = null;
		else drawMesh(ringMesh, marker.pos[0], marker.pos[1], marker.pos[2], 0, 1 - age * 0.6, [0.3, 1, 0.4, 1]);
	}
}

// name labels above the heads
function placeLabels() {
	for (const [id, p] of players) {
		const d = p.data;
		let el = labels.get(id);
		const s = (d.scale || 100) / 100;
		const sp = d.team === 'spectator' || id === view.id ? null : project([p.draw[0], p.draw[1], p.draw[2] - 24 * s + 80 * s]);
		if (!sp || sp[0] < -50 || sp[1] < -20 || sp[0] > canvas.clientWidth + 50 || sp[1] > canvas.clientHeight + 40) {
			if (el) el.style.display = 'none';
			continue;
		}
		if (!el) {
			el = document.createElement('div');
			el.className = 'label';
			$('labels').appendChild(el);
			labels.set(id, el);
		}
		const key = d.name + '|' + d.health + '|' + d.armor + '|' + d.maxHealth + '|' + (id === selected) + '|' + d.dead;
		if (el.dataset.key !== key) {
			el.dataset.key = key;
			el.className = 'label' + (id === selected ? ' sel' : '');
			const hp = Math.max(0, Math.min(100, d.health / Math.max(1, d.maxHealth) * 100));
			el.innerHTML = '<span class="nm">' + colorName(d.name) + '</span>' + (d.dead ? '' :
				'<div class="bar"><i style="width:' + hp + '%"></i></div>' + (d.armor > 0 ? '<div class="bar ar"><i style="width:' + Math.min(100, d.armor) + '%"></i></div>' : ''));
		}
		el.style.display = '';
		el.style.left = sp[0] + 'px';
		el.style.top = sp[1] + 'px';
	}
}

// the button Screenshot: what the view shows, as a file
function savePicture() {
	if (wantShot) {
		// now, while the frame is still in the canvas
		wantShot = false;
		canvas.toBlob(blob => {
			if (!blob) return;
			const a = document.createElement('a');
			a.href = URL.createObjectURL(blob);
			a.download = (loadedMap || 'map').replace(/\//g, '_') + '_' + new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-') + '.png';
			a.click();
			setTimeout(() => URL.revokeObjectURL(a.href), 5000);
		});
	}
}

// A frame: the camera, the map, what stands and moves on it, what the editor lays over it, the labels.
// the time of the frame that is drawn: for what animates by a shader and is not handed the time
let frameNow = 0;
// One part of a frame that fails (a model the page reads wrongly) must not end the picture for good: it did,
// because the frame that threw never asked for the next one. The error is told once on the console, what the
// frame had set of the state of the graphics card is put back, and the next frame comes all the same.
const frameErrors = new Set();
function frame(now) {
	try {
		drawFrame(now);
	} catch (e) {
		const text = String(e && e.stack || e);
		if (!frameErrors.has(text)) { frameErrors.add(text); console.error('a frame failed: ' + text); }
		gl.bindVertexArray(null);
		gl.depthMask(true); gl.depthFunc(gl.LESS); gl.depthRange(0, 1);
		gl.colorMask(true, true, true, true);
		gl.disable(gl.BLEND); gl.disable(gl.CULL_FACE); gl.disable(gl.POLYGON_OFFSET_FILL);
	}
	requestAnimationFrame(frame);
}
function drawFrame(now) {
	const dt = Math.min(0.1, (now - lastFrame) / 1000);
	lastFrame = now;

	moveCamera(dt);

	// smooth player movement between polls
	const k = Math.min(1, (now - lastPoll) / pollInterval);
	for (const p of players.values()) {
		for (let i = 0; i < 3; i++) p.draw[i] = p.prev[i] + (p.data.pos[i] - p.prev[i]) * k;
	}
	// a player who is controlled from the page, and the camera behind him (js/control.js)
	controlFrame();

	if (canvas.width !== Math.round(canvas.clientWidth * canvasScale())) resize();
	if (view.id < 0 || !firstPersonCamera(now)) updateCamera();
	// the comparison: the time of the game for what a shader animates, and the picture of what animates on its own
	if (refStands()) now = refCam.time;
	frameNow = now;
	// The sky of a map that was there before the shader scripts is settled once they have come, and the curved
	// surfaces take the level of detail of where the camera is now (RB_SurfaceGrid).
	if (world) { if (settleSky(world)) prepareScripted(world); updateGridLod(now, gameLook() && !!world.scripted); }
	if (refMasking && world) { drawWorldMask(now); return; }
	// the lights of this frame, before anything they fall on is drawn
	gatherDlights(now);

	gl.viewport(0, 0, canvas.width, canvas.height);
	gl.clearColor(0.05, 0.05, 0.06, 1);
	gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
	gl.enable(gl.DEPTH_TEST);

	const editor = appMode === 'editor';
	const scripted = world && gameLook() && world.scripted;
	// what was left over from a frame that did not get to its end
	lateModels.length = 0;
	deferLate = !!scripted;
	if (scripted) {
		// the room of a sky portal first, from its own spot: what the map then draws of its sky is that picture
		skyPortalOn = drawSkyPortal(now);
		drawWorldScripted(now, false);
		// what the client game puts into the map by itself (cg_spawn.c): the models of misc_model_static
		drawWorldEnts(now);
		// the models of the movers, the light of blades and shots on the map, the marks and shadows on it
		drawMoverModels(now);
		drawDlights(now);
		drawMarks(now);
		// The lines of the triangles lie in the surfaces. The plain view pushes its surfaces back for them, here
		// the lines come forward by a hair of the depth instead: the stages of a shader set that offset themselves.
		if (editor && $('dbgWire').checked) { gl.depthRange(0, 0.9997); drawWorldWire(now); gl.depthRange(0, 1); }
	} else if (world) {
		drawWorldPlain(now, editor);
	}

	gl.useProgram(flatProg.p);
	gl.uniformMatrix4fv(flatProg.u.uMvp, false, viewProj);

	drawItems(now);

	drawModelEntities(now, k);
	drawEditorMarks(now, editor);

	drawPlayers(now);

	drawNpcs(now);

	drawMarker(now);

	if (state && ((state.shots && state.shots.length) || flashes.length)) drawShots(k);
	if (scripted) drawWorldScripted(now, true);
	// what a client draws above and around the players (sprites, shells)
	if (scripted) drawPlayerSprites(now);
	// the flares of the map (the engine draws them when everything else of the scene is drawn)
	if (scripted) drawFlares(now);
	// the effects of the game, over what is see-through of the map and under the glow of blades and bolts
	drawEffects(now);
	drawBlades();
	// the weather comes when all of the scene is drawn (RB_RenderWorldEffects)
	if (scripted) drawWeather(now);
	if (view.id >= 0 && players.has(view.id)) {
		const d = players.get(view.id).data;
		drawViewWeapon(d);
		// what an event of the game shows by itself for a moment: after the weapon in front of the camera, whose
		// muzzle a flash of this frame is played at, and before the 2D
		drawEvents(now);
		renderHud(d);
	} else {
		// (the plain view of the editor plays effects too)
		drawEvents(now);
	}

	placeLabels();
	placeEntLabels();

	savePicture();
}
let wantShot = false;
$('shotBtn').addEventListener('click', () => { wantShot = true; });

