/* ------------------------------------------------------------------ */
/* the shader scripts of the game                                      */
/* ------------------------------------------------------------------ */

// The view of the administration draws the map the way the game does: every surface with the stages its shader
// script names (images, blending, moving texture coordinates, colours that pulse), in the order of the game,
// under the sky of the map. What the engine makes of a script is in rd-vanilla/tr_shader.cpp and tr_shade.cpp,
// the rules here are taken from there. Not done: glow, sprites on surfaces, portals.
//
// The editor keeps the plain view (an image times the light), which needs nothing but the map.

const whiteTex = makeTexture(1, 1, new Uint8Array([255, 255, 255, 255]), { mip: false });

// deformVertexes wave, bulge and move, as RB_DeformTessGeometry does them: a wave and a bulge move a vertex along
// its normal, by how far depends on where it is, and a move shifts all of them.
const DEFORM_GLSL = `uniform int uWaveFunc;	// 0 no wave, 1 sin, 2 triangle, 3 square, 4 sawtooth, 5 the same falling
uniform vec4 uWave;		// how fast it changes along the surface, its base, how far it goes, where it is in its wave
uniform vec4 uBulge;	// width, height, where it is, and 1 when it only pushes outwards. Height 0: none
uniform vec3 uMove;
float waveOf(int f, float x) {
	float r = fract(x);
	if (f == 2) return r < 0.25 ? r * 4.0 : r < 0.75 ? 2.0 - r * 4.0 : r * 4.0 - 4.0;
	if (f == 3) return r < 0.5 ? 1.0 : -1.0;
	if (f == 4) return r;
	if (f == 5) return 1.0 - r;
	return sin(r * 6.2831853);
}
vec3 deformed(vec3 p, vec3 n, vec2 st) {
	if (uWaveFunc != 0) p += n * (uWave.y + uWave.z * waveOf(uWaveFunc, (p.x + p.y + p.z) * uWave.x + uWave.w));
	if (uBulge.y != 0.0) p += n * (uBulge.w > 0.5 ? uBulge.y : sin(st.x * uBulge.x + uBulge.z) * uBulge.y);
	return p + uMove;
}`;

const stageProg = compile(`#version 300 es
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aSt; layout(location=2) in vec2 aLm; layout(location=3) in vec4 aCol; layout(location=4) in vec3 aNorm;
uniform mat4 uMvp; uniform vec3 uEye;
uniform int uTcGen;		// 0 the texture coordinates, 1 the ones of the lightmap, 2 the environment
uniform vec3 uTcA; uniform vec3 uTcB;	// the tcMods of the stage as one matrix
uniform vec2 uTurb;		// tcMod turb: how far, and where in its wave it is
uniform mediump int uAlphaVertex;	// the precision the fragment shader has for it: it is in both
${DEFORM_GLSL}
// deformVertexes autosprite (1) and autosprite2 (2): a quad that turns to the viewer. Its vertices come as the
// middle it turns around, and aLm says how far from it they are (see buildSprites).
uniform int uSprite; uniform vec3 uViewLeft; uniform vec3 uViewUp; uniform vec3 uViewFwd;
out vec2 vTc; out vec4 vCol; out float vSpec; out float vDepth;
void main() {
	vec3 pos = aPos, norm = aNorm;
	if (uSprite == 1) { pos += uViewLeft * aLm.x + uViewUp * aLm.y; norm = -uViewFwd; }
	else if (uSprite == 2) { pos += normalize(cross(aNorm, uViewFwd)) * aLm.x; norm = -uViewFwd; }
	pos = deformed(pos, norm, aSt);
	// alphaGen lightingSpecular, as RB_CalcSpecularAlpha: how much of a light is mirrored into the eye, to the
	// fourth power. For the map the engine has one light at a fixed place.
	vSpec = 1.0;
	if (uAlphaVertex == 3) {
		vec3 l = normalize(vec3(-960.0, 1980.0, 96.0) - pos);
		float s = max(dot(norm * 2.0 * dot(norm, l) - l, normalize(uEye - pos)), 0.0);
		vSpec = s * s * s * s;
	}
	vec2 tc = aSt;
	if (uTcGen == 1) tc = aLm;
	else if (uTcGen == 2) {
		vec3 v = normalize(uEye - pos);
		vec3 r = norm * 2.0 * dot(norm, v) - v;
		tc = vec2(0.5 + r.y * 0.5, 0.5 - r.z * 0.5);
	}
	if (uTurb.x != 0.0) {
		tc.x += sin(((pos.x + pos.z) * 0.0009765625 + uTurb.y) * 6.2831853) * uTurb.x;
		tc.y += sin((pos.y * 0.0009765625 + uTurb.y) * 6.2831853) * uTurb.x;
	}
	vTc = vec2(dot(uTcA.xy, tc) + uTcA.z, dot(uTcB.xy, tc) + uTcB.z);
	vCol = aCol;
	gl_Position = uMvp * vec4(pos, 1.0);
	vDepth = gl_Position.w;	// how far in front of the eye
}`,
`#version 300 es
precision mediump float;
in vec2 vTc; in vec4 vCol; in float vSpec; in float vDepth;
uniform sampler2D uTex; uniform vec4 uColor;
uniform vec4 uFog;	// the fog of the whole map: what the colour fades to, and how thick it is. 0: none
uniform int uRgbVertex; uniform int uAlphaVertex;	// 1 times the colour of the vertex, 2 times one minus it, 3 the shine
uniform int uAlphaFunc;	// 1 GT0, 2 LT128, 3 GE128, 4 GE192
out vec4 o;
void main() {
	vec4 c = uColor;
	if (uRgbVertex == 1) c.rgb *= vCol.rgb; else if (uRgbVertex == 2) c.rgb *= 1.0 - vCol.rgb;
	if (uAlphaVertex == 1) c.a *= vCol.a; else if (uAlphaVertex == 2) c.a *= 1.0 - vCol.a; else if (uAlphaVertex == 3) c.a *= vSpec;
	c *= texture(uTex, vTc);
	if (uAlphaFunc == 1 && c.a <= 0.0) discard;
	if (uAlphaFunc == 2 && c.a >= 0.5) discard;
	if (uAlphaFunc == 3 && c.a < 0.5) discard;
	if (uAlphaFunc == 4 && c.a < 0.75) discard;
	// as the fog of OpenGL with GL_EXP2, which the engine uses for it
	if (uFog.a > 0.0) { float f = uFog.a * vDepth; c.rgb = mix(uFog.rgb, c.rgb, exp(-f * f)); }
	o = c;
}`);

// The same stages on a model: its vertices are placed by a matrix, it has no colours and no lightmap of its own,
// and where a stage asks for the light of the place (rgbGen lightingDiffuse, and every model without a script)
// it gets the light the page gives its models. The environment is worked out in the space of the model, as
// the engine does it.
const modelStageProg = compile(`#version 300 es
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNorm; layout(location=2) in vec2 aUv;
uniform mat4 uMvp; uniform mat4 uModel; uniform vec3 uEyeLocal;
uniform vec3 uAmbient; uniform vec3 uDirected; uniform vec3 uLightDir;	// the light of the place, and where it comes from
uniform int uTcGen; uniform vec3 uTcA; uniform vec3 uTcB; uniform vec2 uTurb;
uniform mediump int uAlphaVertex;	// the precision the fragment shader has for it: it is in both
${DEFORM_GLSL}
out vec2 vTc; out vec3 vLight; out float vSpec; out float vDepth;
void main() {
	vec3 pos = deformed(aPos, aNorm, aUv);
	vec4 world = uModel * vec4(pos, 1.0);
	// alphaGen lightingSpecular: for a model the light of the place is the one that is mirrored
	vSpec = 1.0;
	if (uAlphaVertex == 3) {
		vec3 l = normalize(transpose(mat3(uModel)) * uLightDir);
		float s = max(dot(aNorm * 2.0 * dot(aNorm, l) - l, normalize(uEyeLocal - pos)), 0.0);
		vSpec = s * s * s * s;
	}
	vec2 tc = aUv;
	if (uTcGen == 2) {
		vec3 v = normalize(uEyeLocal - pos);
		vec3 r = aNorm * 2.0 * dot(aNorm, v) - v;
		tc = vec2(0.5 + r.y * 0.5, 0.5 - r.z * 0.5);
	}
	if (uTurb.x != 0.0) {
		tc.x += sin(((world.x + world.z) * 0.0009765625 + uTurb.y) * 6.2831853) * uTurb.x;
		tc.y += sin((world.y * 0.0009765625 + uTurb.y) * 6.2831853) * uTurb.x;
	}
	vTc = vec2(dot(uTcA.xy, tc) + uTcA.z, dot(uTcB.xy, tc) + uTcB.z);
	// as RB_CalcDiffuseColor: the ambient light, and the directed one by how much the surface faces it
	vLight = min(uAmbient + max(dot(normalize(mat3(uModel) * aNorm), uLightDir), 0.0) * uDirected, vec3(1.0));
	gl_Position = uMvp * world;
	vDepth = gl_Position.w;
}`,
`#version 300 es
precision mediump float;
in vec2 vTc; in vec3 vLight; in float vSpec; in float vDepth;
uniform sampler2D uTex; uniform vec4 uColor;
uniform vec4 uFog;
uniform int uRgbVertex;	// 3: times the light of the place
uniform int uAlphaVertex;	// 3: times the shine
uniform int uAlphaFunc;
out vec4 o;
void main() {
	vec4 c = uColor;
	if (uRgbVertex == 3) c.rgb *= vLight;
	if (uAlphaVertex == 3) c.a *= vSpec;
	c *= texture(uTex, vTc);
	if (uAlphaFunc == 1 && c.a <= 0.0) discard;
	if (uAlphaFunc == 2 && c.a >= 0.5) discard;
	if (uAlphaFunc == 3 && c.a < 0.5) discard;
	if (uAlphaFunc == 4 && c.a < 0.75) discard;
	if (uFog.a > 0.0) { float f = uFog.a * vDepth; c.rgb = mix(uFog.rgb, c.rgb, exp(-f * f)); }
	o = c;
}`);

// The fog of a brush (RB_FogPass): the surfaces in it are drawn once more in the colour of the fog, the thicker
// the further the line of sight runs through it. uPlane is the side the fog ends at, looking into it: what
// counts is how far behind it a place is (vT), and where the eye is (uEyeT, below zero outside of the fog).
const fogProg = compile(`#version 300 es
layout(location=0) in vec3 aPos;
uniform mat4 uMvp; uniform vec4 uPlane; uniform float uScale;
out float vS; out float vT;
void main() {
	gl_Position = uMvp * vec4(aPos, 1.0);
	vS = gl_Position.w * uScale;
	vT = dot(aPos, uPlane.xyz) - uPlane.w;
}`,
`#version 300 es
precision mediump float;
in float vS; in float vT;
uniform vec3 uColor; uniform float uEyeT;
out vec4 o;
void main() {
	// the part of the way to the eye that is in the fog
	float part = uEyeT < 0.0 ? (vT < 1.0 ? 0.0 : vT / (vT - uEyeT)) : (vT < 0.0 ? 0.0 : 1.0);
	o = vec4(uColor, sqrt(clamp(vS * part, 0.0, 1.0)));
}`);

