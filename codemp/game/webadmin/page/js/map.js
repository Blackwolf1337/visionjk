/* ------------------------------------------------------------------ */
/* map                                                                 */
/* ------------------------------------------------------------------ */

let world = null;		// { batches: [{vao, count, tex, lm, alpha}], bounds, pick }
let loadedMap = '';

async function loadMap(name) {
	loadedMap = name;
	$('loading').style.display = '';
	$('loading').textContent = 'Loading ' + name + '...';
	world = null;
	// the images and the stages that hold them are those of a map: the ones of the last map are let go
	gameImages.clear();
	imageQueue.length = 0;
	scripts.materials.clear();
	try {
		const res = await fetch('api/map?name=' + encodeURIComponent(name));
		if (!res.ok) throw new Error('HTTP ' + res.status);
		const bsp = parseBsp(await res.arrayBuffer());
		if (loadedMap !== name) return;

		const lightmapTex = bsp.lightmaps.map(d => makeTexture(128, 128, d, { rgb: true, clamp: true }));
		const textures = new Map();
		const batches = bsp.batches.map(b => {
			const vao = gl.createVertexArray();
			gl.bindVertexArray(vao);
			const attr = (loc, data, size, type, normalized) => {
				const buf = gl.createBuffer();
				gl.bindBuffer(gl.ARRAY_BUFFER, buf);
				gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
				gl.enableVertexAttribArray(loc);
				gl.vertexAttribPointer(loc, size, type, normalized, 0, 0);
				return buf;
			};
			const posBuf = attr(0, new Float32Array(b.pos), 3, gl.FLOAT, false);
			const stBuf = attr(1, new Float32Array(b.st), 2, gl.FLOAT, false);
			attr(2, new Float32Array(b.lm), 2, gl.FLOAT, false);
			const colBuf = attr(3, new Uint8Array(b.col), 4, gl.UNSIGNED_BYTE, true);
			attr(4, new Float32Array(b.norm), 3, gl.FLOAT, false);
			const tri = new Uint32Array(b.idx), ib = gl.createBuffer();
			gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
			gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, tri, gl.STATIC_DRAW);
			gl.bindVertexArray(null);
			const shaderName = bsp.shaders[b.shader].name;
			if (!textures.has(shaderName)) textures.set(shaderName, gameImage(shaderName, false));
			// posBuf and tri: the wireframe of the editor makes its lines of them when it is asked for. With stBuf,
			// colBuf and verts they are also what the quads that turn to the viewer are made of (buildSprites).
			return { vao, count: b.idx.length, shaderName, fog: b.fog, model: b.model, lm: b.lightmap >= 0 ? lightmapTex[b.lightmap] : null, posBuf, stBuf, colBuf, verts: b.pos.length / 3, tri, wire: null, sprites: undefined };
		});

		world = { entityText: bsp.entityText, batches, textures, bounds: bsp.bounds, pick: bsp.pick, sky: bsp.sky, models: bsp.models, brushes: bsp.brushes, shaders: bsp.shaders, surfs: bsp.surfs, stats: bsp.stats, grid: bsp.grid, fogs: bsp.fogs, fogInfo: [], globalFog: null, wires: new Map(), scripted: null, skyDraw: null };
		prepareScripted(world);
		resetCamera();
		$('loading').style.display = 'none';
	} catch (e) {
		$('loading').textContent = 'Could not load the map: ' + e.message;
	}
}

// An image of the game as a texture, by its name with or without an ending: the ending the name has first, as
// the engine does, then the others. null: there is none, or the server does not hand out its folder.
async function fetchImage(name, clamp) {
	const m = /\.(tga|jpg|png)$/i.exec(name), base = m ? name.slice(0, m.index) : name;
	const endings = [m ? m[0].toLowerCase() : '', '.jpg', '.tga', '.png'].filter((e, i, all) => e && all.indexOf(e) === i);
	let unanswered = false;
	for (const ext of endings) {
		try {
			let res, data;
			try {
				res = await fetch('api/file?name=' + encodeURIComponent(base + ext));
				if (!res.ok) { if (res.status !== 404 && res.status !== 400) unanswered = true; continue; }
				data = await res.arrayBuffer();
			} catch (e) {
				unanswered = true;
				continue;
			}
			if (ext === '.tga') {
				const img = decodeTga(data);
				if (!img) continue;
				return { tex: makeTexture(img.w, img.h, img.data, { clamp }), alpha: img.alpha };
			}
			// as it is in the file: the stages of a shader blend with the alpha themselves
			const bmp = await createImageBitmap(new Blob([data]), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
			const tex = gl.createTexture();
			gl.bindTexture(gl.TEXTURE_2D, tex);
			gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, bmp);
			gl.generateMipmap(gl.TEXTURE_2D);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
			if (clamp) {
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
			}
			return { tex, alpha: false };
		} catch (e) { /* a file that is no image: the next ending */ }
	}
	// the server did not answer (it changes the map, say): that is not "there is none", the caller may ask again
	if (unanswered) throw new Error('no answer for ' + name);
	return null;
}

