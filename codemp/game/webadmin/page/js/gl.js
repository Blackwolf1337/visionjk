/* ------------------------------------------------------------------ */
/* WebGL                                                               */
/* ------------------------------------------------------------------ */

const canvas = $('gl');
const gl = canvas.getContext('webgl2', { antialias: true });

function compile(vs, fs, g = gl) {
	const gl = g;
	const p = gl.createProgram();
	for (const [type, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
		const s = gl.createShader(type);
		gl.shaderSource(s, src);
		gl.compileShader(s);
		if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
		gl.attachShader(p, s);
	}
	gl.linkProgram(p);
	if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
	const u = {};
	for (let i = 0, n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS); i < n; i++) {
		const name = gl.getActiveUniform(p, i).name;
		u[name] = gl.getUniformLocation(p, name);
	}
	return { p, u };
}

const mapProg = compile(`#version 300 es
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aSt; layout(location=2) in vec2 aLm; layout(location=3) in vec4 aCol;
uniform mat4 uMvp; uniform vec3 uEye;
out vec2 vSt; out vec2 vLm; out vec4 vCol; out float vDist;
void main() { vSt = aSt; vLm = aLm; vCol = aCol; vDist = distance(aPos, uEye); gl_Position = uMvp * vec4(aPos, 1.0); }`,
`#version 300 es
precision mediump float;
in vec2 vSt; in vec2 vLm; in vec4 vCol; in float vDist;
uniform sampler2D uTex; uniform sampler2D uLm; uniform int uUseLm; uniform int uAlphaTest; uniform float uFogFar; uniform int uView;
out vec4 o;
void main() {
	vec4 t = texture(uTex, vSt);
	if (uAlphaTest == 1 && t.a < 0.5) discard;
	// The plain view has twice the light of the map, so that dark corners can be worked in. 3 is "As in the game"
	// until the shader scripts are read and that view takes over: the light as it is, and no haze.
	vec3 lm = texture(uLm, vLm).rgb;
	vec3 light = uUseLm == 1 ? (uView == 3 ? lm : min(lm * 2.0, vec3(1.6))) : vCol.rgb;
	// the debug views of the editor: 1 the light alone, 2 the texture alone
	vec3 c = uView == 1 ? light : uView == 2 ? t.rgb : t.rgb * light;
	float fog = uView == 3 ? 0.0 : clamp(vDist / uFogFar, 0.0, 1.0) * 0.55;
	o = vec4(mix(c, vec3(0.05, 0.05, 0.06), fog), 1.0);
}`);

const flatProg = compile(`#version 300 es
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNorm;
uniform mat4 uMvp; uniform mat4 uModel;
out vec3 vNorm;
void main() { vNorm = mat3(uModel) * aNorm; gl_Position = uMvp * uModel * vec4(aPos, 1.0); }`,
`#version 300 es
precision mediump float;
in vec3 vNorm; uniform vec4 uColor; out vec4 o;
void main() { float l = 0.55 + 0.45 * max(dot(normalize(vNorm), normalize(vec3(0.4, 0.3, 0.85))), 0.0); o = vec4(uColor.rgb * l, uColor.a); }`);

function makeTexture(w, h, data, opts = {}) {
	const t = gl.createTexture();
	gl.bindTexture(gl.TEXTURE_2D, t);
	gl.texImage2D(gl.TEXTURE_2D, 0, opts.rgb ? gl.RGB8 : gl.RGBA8, w, h, 0, opts.rgb ? gl.RGB : gl.RGBA, gl.UNSIGNED_BYTE, data);
	if (opts.mip !== false) {
		gl.generateMipmap(gl.TEXTURE_2D);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
	} else {
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
	}
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
	if (opts.clamp) {
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
	}
	return t;
}

const grayTex = makeTexture(1, 1, new Uint8Array([150, 150, 150, 255]), { mip: false });

// simple meshes with normals: player body (octagonal prism + cap), facing wedge, item diamond, ring
function buildMesh(pos, norm, idx) {
	const vao = gl.createVertexArray();
	gl.bindVertexArray(vao);
	const pb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, pb); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(pos), gl.STATIC_DRAW);
	gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
	const nb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, nb); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(norm), gl.STATIC_DRAW);
	gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
	const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(idx), gl.STATIC_DRAW);
	gl.bindVertexArray(null);
	return { vao, count: idx.length };
}

function prismMesh(sides, r, z0, z1) {
	const pos = [], norm = [], idx = [];
	for (let i = 0; i < sides; i++) {
		const a0 = i / sides * Math.PI * 2, a1 = (i + 1) / sides * Math.PI * 2, am = (a0 + a1) / 2;
		const b = pos.length / 3, nx = Math.cos(am), ny = Math.sin(am);
		pos.push(Math.cos(a0) * r, Math.sin(a0) * r, z0, Math.cos(a1) * r, Math.sin(a1) * r, z0, Math.cos(a1) * r, Math.sin(a1) * r, z1, Math.cos(a0) * r, Math.sin(a0) * r, z1);
		norm.push(nx, ny, 0, nx, ny, 0, nx, ny, 0, nx, ny, 0);
		idx.push(b, b + 1, b + 2, b, b + 2, b + 3);
		const t = pos.length / 3;
		pos.push(0, 0, z1, Math.cos(a0) * r, Math.sin(a0) * r, z1, Math.cos(a1) * r, Math.sin(a1) * r, z1);
		norm.push(0, 0, 1, 0, 0, 1, 0, 0, 1);
		idx.push(t, t + 1, t + 2);
	}
	return buildMesh(pos, norm, idx);
}

const bodyMesh = prismMesh(8, 14, 0, 58);
const headMesh = prismMesh(8, 8, 58, 72);
const noseMesh = buildMesh(
	[22, 0, 46, 10, 6, 46, 10, -6, 46, 22, 0, 52, 10, 6, 52, 10, -6, 52],
	[1, 0, 0, 0, 1, 0, 0, -1, 0, 1, 0, 0, 0, 1, 0, 0, -1, 0],
	[0, 1, 4, 0, 4, 3, 0, 2, 5, 0, 5, 3, 3, 4, 5, 0, 1, 2]);
const diamondMesh = buildMesh(
	[0, 0, 12, 8, 0, 0, 0, 8, 0, -8, 0, 0, 0, -8, 0, 0, 0, -12],
	[0, 0, 1, 1, 0, 0, 0, 1, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1],
	[0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1, 5, 2, 1, 5, 3, 2, 5, 4, 3, 5, 1, 4]);
const ringMesh = (() => {
	const pos = [], norm = [], idx = [], n = 32;
	for (let i = 0; i <= n; i++) {
		const a = i / n * Math.PI * 2;
		pos.push(Math.cos(a) * 26, Math.sin(a) * 26, 1, Math.cos(a) * 32, Math.sin(a) * 32, 1);
		norm.push(0, 0, 1, 0, 0, 1);
		if (i < n) { const b = i * 2; idx.push(b, b + 1, b + 3, b, b + 3, b + 2); }
	}
	return buildMesh(pos, norm, idx);
})();

const cubeMesh = (() => {
	const pos = [], norm = [], idx = [], r = 7;
	const faces = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]];
	for (const n of faces) {
		const u = n[2] ? [1, 0, 0] : [0, 0, 1], v = [n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]];
		const b = pos.length / 3;
		for (const [a, c] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {
			pos.push((n[0] + u[0] * a + v[0] * c) * r, (n[1] + u[1] * a + v[1] * c) * r, (n[2] + u[2] * a + v[2] * c) * r);
			norm.push(n[0], n[1], n[2]);
		}
		idx.push(b, b + 1, b + 2, b, b + 2, b + 3);
	}
	return buildMesh(pos, norm, idx);
})();

