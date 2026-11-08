// Saber blades in 3D (vk_post.cpp VK_SaberBlades, ext.AddSaberBladeToScene), for shaders/saber.frag (the view of the
// level), shaders/saberportal.frag (a mirror or portal in it, PORTAL_VIEW) and shaders/saberglow.frag (into the buffer
// of r_DynamicGlow, GLOW_PASS, which blurs it into the soft halo the flat blades get). Drawn after the solid level and
// before what is transparent, so sparks and flashes come in front. Each pixel takes the nearest the ray from the eye
// through it comes to each blade, in the space of the level and not on the screen: a white hot core where it passes
// within the core, a glow in the colour of the blade falling off round it. From the side a blade is a line, seen
// along it a round glowing point; the cgame had drawn two flat strips turned to the eye that fell apart seen along
// the blade (2026-11-08, movie style on their choice). The core is brighter than white for the bloom of the HDR frame.
//
// A blade that moved has swept the place between where it is and where it was a moment ago: the pixel takes the
// nearest of the blades in between (the hilt and the way to the tip turned from one to the other), glowing less the
// older it is, as the SFX blade smears its glow over the swing. What lies in front of the blade (by the depth of the
// scene) hides it; where a wall or the floor hides it, the glow goes on on the wall from the part of the blade in
// front of it, so a blade that strikes a wall does not end in a hard edge.
//
// globals.blades: 0-10 the view as VK_ViewRows puts it, 11 x how many blades; five rows a blade (VK_BLADE_ROWS): its
// start and the radius of the core, its end and the radius of the glow, where its start was a moment ago and how
// bright the swept place is, where its end was, its colour and in w the views it is in (1 the level, 2 a mirror or
// portal). After the blades (VK_BLADE_PORTAL) the view of the mirror or portal as rows 0-10 have it for the level.

layout( set = 0, binding = 0 ) uniform Globals {
	vec4	gamma;
	vec4	screen;
	vec4	screenFx[160];
	vec4	styles[64];
	vec4	wet[779];
	vec4	paint[459];
	vec4	rt[319];
	vec4	lights[34];
	vec4	ssr[7];
	vec4	blades[172];
	vec4	bladeView[11];
} globals;
layout( set = 2, binding = 0 ) uniform sampler2D u_depth;	// the depth of the scene, read pixel for pixel

#define B		globals.blades
#ifdef PORTAL_VIEW
#define W		globals.bladeView
#define VIEW	2
#else
#define W		globals.blades
#define VIEW	1
#endif
#define BLADES	32
#define ROWS	5

layout( location = 0 ) out vec4 o_color;

#include "scene.glsl"

// The nearest points of the ray eye + dir t (t from 0) and the blade a + seg u (u 0 to 1): u, t, how far apart, and
// how far along the blade the nearest point of its line is before the clamp (below 0: behind the hilt).
void Nearest( vec3 eye, vec3 dir, vec3 a, vec3 seg, out float u, out float t, out float d, out float along ) {
	float len2 = max( dot( seg, seg ), 1e-4 );
	vec3 r = eye - a;
	float bd = dot( dir, seg ), dd = dot( dir, r ), ee = dot( seg, r );
	float denom = len2 - bd * bd;

	along = denom > 1e-4 ? ( ee - bd * dd ) / denom : 0.5;
	u = clamp( along, 0.0, 1.0 );
	t = max( dot( a + seg * u - eye, dir ), 0.0 );
	u = clamp( dot( eye + dir * t - a, seg ) / len2, 0.0, 1.0 );
	t = max( dot( a + seg * u - eye, dir ), 0.0 );
	d = distance( eye + dir * t, a + seg * u );
}

// the blade at s between now (0) and a moment ago (1): its hilt moved straight, its way to the tip turned
void Swept( vec3 a, vec3 seg, vec3 oa, vec3 oseg, float s, out vec3 sa, out vec3 sseg ) {
	float len = length( seg ), olen = length( oseg );
	vec3 way = mix( seg / max( len, 1e-4 ), oseg / max( olen, 1e-4 ), s );

	sa = mix( a, oa, s );
	sseg = way * ( mix( len, olen, s ) / max( length( way ), 1e-4 ) );
}

// how far the point p is from the part of the blade in front of the surface (normal n) at it, 1e9: none of it is
float SurfaceDistance( vec3 p, vec3 n, vec3 a, vec3 seg ) {
	float da = dot( a - p, n ), db = dot( a + seg - p, n );

	if ( da <= 0.0 && db <= 0.0 ) {
		return 1e9;
	}
	// (the part where the blade passes into the surface is cut off)
	float u0 = 0.0, u1 = 1.0;
	if ( da < 0.0 ) {
		u0 = da / ( da - db );
	} else if ( db < 0.0 ) {
		u1 = da / ( da - db );
	}
	float u = clamp( dot( p - a, seg ) / max( dot( seg, seg ), 1e-4 ), u0, u1 );
	return distance( p, a + seg * u );
}

void main() {
	vec3 eye = W[4].xyz;
	vec3 p = ScenePoint( gl_FragCoord.xy );
	float sceneDist = distance( p, eye );
	vec3 dir = ( p - eye ) / max( sceneDist, 1e-4 );
	// how large a pixel is one unit before the eye (for the soft edge of the core)
	float pixel = 2.0 * W[5].w / W[8].w;
	int count = min( int( B[11].x ), BLADES );
	vec3 light = vec3( 0.0 );
	vec3 normal = vec3( 0.0 );
	bool haveNormal = false;

	for ( int i = 0; i < count; i++ ) {
		vec4 a = B[12 + i * ROWS], b = B[13 + i * ROWS], oa = B[14 + i * ROWS], ob = B[15 + i * ROWS], c = B[16 + i * ROWS];
		if ( ( int( c.w ) & VIEW ) == 0 ) {
			continue;
		}
		vec3 seg = b.xyz - a.xyz, oseg = ob.xyz - oa.xyz;
		vec3 hotColour = mix( c.rgb, vec3( 1.0 ), 0.85 );
#ifdef GLOW_PASS
		// (into the glow buffer in its colour: the halo round a blade is coloured, not white)
		hotColour = c.rgb * 0.6;
#endif
		float u, t, d, along;

		// the blade now
		Nearest( eye, dir, a.xyz, seg, u, t, d, along );
		// (at least a pixel of core and a little glow, so a blade far away does not get lost)
		float px = pixel * max( t, 1.0 );
		float core = max( a.w, px * 0.4 ), glow = max( b.w, px * 1.5 );
#ifdef GLOW_PASS
		glow *= 2.0;		// (the blur spreads it further, as the wide glow sprites of the flat blade)
#endif
		// the glow ends at the hilt, it does not spill back over it
		float hilt = smoothstep( -glow * 0.6, 0.0, along * length( seg ) );
		// hidden where that point lies behind what the pixel shows
		float seen = 1.0 - smoothstep( sceneDist - 1.0, sceneDist + 2.0, t );
		vec3 blade = vec3( 0.0 );
		if ( seen > 0.0 ) {
			float hot = 1.0 - smoothstep( core - px, core + px, d );
			float inner = exp( -3.0 * ( d * d ) / ( glow * glow ) );
			float outer = exp( -d / ( glow * 1.5 ) ) * 0.2;
			blade = ( c.rgb * ( inner * 1.1 + outer ) + hotColour * hot * 2.5 ) * hilt * seen;
		}

		// The place it swept: the nearest of the blades between, seven of them, then closer in round the best. It
		// glows fainter the older the blade, its core smears less the slower it swept; the blade now stays as it
		// is, the swept place only adds where it is brighter.
		vec3 sweep = vec3( 0.0 );
		if ( oa.w > 0.0 && distance( a.xyz, oa.xyz ) + distance( b.xyz, ob.xyz ) > 0.5 ) {
			float best = 1e9, s = 1.0 / 6.0;
			vec3 sa, sseg;
			float su, st, sd, salong;

			for ( int k = 1; k <= 6; k++ ) {
				Swept( a.xyz, seg, oa.xyz, oseg, float( k ) / 6.0, sa, sseg );
				Nearest( eye, dir, sa, sseg, su, st, sd, salong );
				if ( sd < best ) {
					best = sd;
					s = float( k ) / 6.0;
				}
			}
			for ( float step = 1.0 / 12.0; step > 0.01; step *= 0.5 ) {
				float centre = s;
				for ( int side = -1; side <= 1; side += 2 ) {
					float ts = clamp( centre + step * float( side ), 0.01, 1.0 );
					Swept( a.xyz, seg, oa.xyz, oseg, ts, sa, sseg );
					Nearest( eye, dir, sa, sseg, su, st, sd, salong );
					if ( sd < best ) {
						best = sd;
						s = ts;
					}
				}
			}
			Swept( a.xyz, seg, oa.xyz, oseg, s, sa, sseg );
			Nearest( eye, dir, sa, sseg, su, st, sd, salong );
			float sseen = 1.0 - smoothstep( sceneDist - 1.0, sceneDist + 2.0, st );
			if ( sseen > 0.0 ) {
				float spx = pixel * max( st, 1.0 );
				float sglow = max( b.w, spx * 1.5 ), score = max( a.w, spx * 0.4 );
#ifdef GLOW_PASS
				sglow *= 2.0;
#endif
				float shilt = smoothstep( -sglow * 0.6, 0.0, salong * length( sseg ) );
				float hot = 1.0 - smoothstep( score - spx, score + spx, sd );
				float inner = exp( -3.0 * ( sd * sd ) / ( sglow * sglow ) );
				float glowWeight = ( 1.0 - s ) * ( 0.35 + 0.4 * oa.w );
				float hotWeight = pow( 1.0 - s, 3.0 ) * 0.4 * oa.w;
				sweep = ( c.rgb * inner * 1.1 * glowWeight + hotColour * hot * 2.5 * hotWeight ) * shilt * sseen;
			}
		}
		light += max( blade, sweep );

		// on the surface that hides it: the glow of the part of the blade in front of it
		if ( seen < 1.0 && sceneDist < 8192.0 ) {
			if ( !haveNormal ) {
				normal = SceneSurface( gl_FragCoord.xy, p, -dir );
				haveNormal = true;
			}
			float ds = SurfaceDistance( p, normal, a.xyz, seg );
			if ( ds < glow * 4.0 ) {
				float inner = exp( -3.0 * ( ds * ds ) / ( glow * glow ) );
				float outer = exp( -ds / ( glow * 1.5 ) ) * 0.2;
				light += c.rgb * ( inner * 1.1 + outer ) * 0.6 * ( 1.0 - seen );
			}
		}
	}
	o_color = vec4( light, 0.0 );
}
