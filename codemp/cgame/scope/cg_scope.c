/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// cg_scope.c -- the scope of the disruptor. Not from JA+.
//
// The lens: while the disruptor is zoomed the renderer draws the view through a lens (trap->ext.R_SetScopeEffect,
// rd-vulkan-vision): large and clear, bent and coloured a little towards its rim, sharp around what the crosshair is
// on, a fine reticle with a lit dot, the ammo, the zoom and the charge as arcs around the rim, the level soft and
// dimmed outside. The mask of the game and its art are left out then (CG_DrawZoomMask). v_scope 0, or rd-vanilla:
// the mask as it always was.
//
// The wheel: while the zoom is locked the mouse wheel zooms in and out in steps (v_scopezoom). The engine sends
// the wheel here instead of its binding while the cvar v_scopeWheel is 1 (CL_ParseBinding), so a weapnext on the
// wheel still works outside of the scope. Only the view changes: the server takes nothing of the zoom but whether
// there is one (WP_FireDisruptor), so it works on every server.
//
// The tracker: while the disruptor is zoomed the key of the saber styles (saberAttackCycle, the middle mouse button in
// the defaults of the game) turns it on and off. It stays on for the next zoom and the next start of the game
// (v_scopeMode). The lens is tinted dim and red with the edges of the level drawn, and every player in it who
// moves faster than v_motionThreshold gets a box that locks on and follows him, with his distance. Inside the boxes
// the level keeps its colours (the renderer gets the boxes, VK_ScopeBoxes). A ping repeats while something is
// tracked, faster the nearer it is, and every ping leaves the boxes behind as echoes that fade where they were. A
// mover counts for a second after he stopped. Only what can be seen: nothing through walls, cloaked, dead and the
// own player never. Now and then the tracker takes an edge or a corner of the level for a mover for a moment, as a
// real one would. rd-vanilla: the boxes without the tint.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"

#define SCOPE_RADIUS		0.45f		// of the height of the view
#define SCOPE_STEP			1.25f		// the fov changes by this for a notch of the wheel
#define SCOPE_MIN_FOV		MAX_ZOOM_FOV
#define SCOPE_MAX_FOV		50.0f		// where the zoom starts (CG_CalcFov)
#define SCOPE_EASE_MSEC		80
#define MODE_FADE_MSEC		150
#define MOTION_RISE			6.0f		// how fast a mover lights up, a second
#define MOTION_FALL			1.0f		// and fades out after he stopped
#define MOTION_MAX_PIPS		8			// contacts shown under the lens
#define TRACK_LOST_MSEC		450			// a box stays this long where its mover was last seen
#define TRACK_LOCK_MSEC		220			// how long a new box takes to close in on its mover
#define TRACK_PHANTOMS		3
#define TRACK_ECHOES		32			// boxes a ping left behind
#define TRACK_ECHO_MSEC		1500		// and how long they take to fade
#define UNITS_PER_METER		36.0f		// a player is 64 units, about 1.8 m

typedef enum {
	SCOPE_MODE_OFF,
	SCOPE_MODE_TRACKER,
	SCOPE_NUM_MODES
} scopeMode_t;

typedef struct {
	int			seenTime;				// 0: no box
	int			lockTime;				// when the box was new
	vec3_t		origin, mins, maxs;		// where the mover was last seen
	float		rect[4];				// the box on the 2D grid as it is drawn: left, top, right, bottom
	qboolean	rectValid;
} scopeTrack_t;

typedef struct {
	float		rect[4];				// on the 2D grid, as the brackets stand: left, top, right, bottom
	float		level[4];				// and where its mover is in the picture before the lens bends it, which is
										// where scope.frag reads the mask
	float		distance;				// 0: no text under it
	float		alpha;
} scopeBox_t;

#define SCOPE_MAX_BOXES		SCOPEFX_MAX_BOXES

typedef struct {
	int			birth;					// 0: none
	vec3_t		origin, mins, maxs;		// where a box stood when the ping went
} scopeEcho_t;

typedef struct {
	int			birth, life;			// 0 life: none
	vec3_t		origin;
	float		halfWidth, height;
	float		rect[4];
	qboolean	rectValid;
	int			seed;
} scopePhantom_t;

extern float zoomFov;					// cg_view.c, the fov of the disruptor zoom

static struct {
	qboolean		checked;			// whether the engine and the renderer have R_SetScopeEffect is known
	qboolean		has;
	qboolean		drawn;				// the renderer draws the lens this frame
	float			easeFrom, easeTo;	// the wheel: the zoom moves from to, starting at easeTime
	int				easeTime;
	qboolean		easing;
	qboolean		wheel;				// what v_scopeWheel was set to
	scopeMode_t		mode;				// the one in use: v_scopeMode while zoomed, else off
	float			trackerFade;		// and the tracker
	int				contacts;			// movers seen in the lens this frame
	sfxHandle_t		pingSound;
	int				nextPing;			// 0: nothing is tracked
	int				nextPhantom;
	float			nearest;			// units to the nearest box this frame
	scopePhantom_t	phantoms[TRACK_PHANTOMS];
	scopeBox_t		boxes[SCOPE_MAX_BOXES];	// the boxes of this frame
	int				numBoxes;
	scopeEcho_t		echoes[TRACK_ECHOES];	// what the pings left behind, a ring
	int				nextEcho;
	scopeBox_t		echoBoxes[TRACK_ECHOES];	// and where they are drawn this frame
	int				numEchoBoxes;
	float			heat[MAX_GENTITIES];	// how strongly an entity shows as a mover
	int				heatTime[MAX_GENTITIES];
	scopeTrack_t	tracks[MAX_GENTITIES];
} scope;

static void CG_ScopeUpdateTracker( void );

static qboolean Scope_Engine( void ) {
	char value[8];

	trap->Cvar_VariableStringBuffer( "v_cgameScope", value, sizeof( value ) );
	return (qboolean)( atoi( value ) == 1 );
}

void CG_ScopeInit( void ) {
	memset( &scope, 0, sizeof( scope ) );
	// an engine without the cvar has no such entry in trap->ext, it may not be read
	scope.has = (qboolean)( Scope_Engine() && trap->ext.R_SetScopeEffect && trap->ext.R_SetScopeEffect( NULL ) );
	scope.checked = qtrue;
	scope.pingSound = trap->S_RegisterSound( "sound/vision/tracker_ping.wav" );
	trap->Cvar_Set( "v_scopeWheel", "0" );
}

// the disruptor is zoomed in, by the player or the one he follows
static qboolean CG_Scoped( void ) {
	return (qboolean)( cg.snap && cg.predictedPlayerState.zoomMode == 1 && cg.predictedPlayerState.weapon == WP_DISRUPTOR
		&& cg.predictedPlayerState.stats[STAT_HEALTH] > 0 );
}

// and the zoom is locked: the alt fire was let go, from now on the zoom stays where it is
static qboolean CG_ScopeLocked( void ) {
	return (qboolean)( CG_Scoped() && cg.predictedPlayerState.zoomLocked );
}

/*
=================
CG_ScopeZoom_f

v_scopezoom in|out: a notch of the wheel while the zoom is locked
=================
*/
void CG_ScopeZoom_f( void ) {
	const char	*dir = CG_Argv( 1 );
	float		from;

	if ( !CG_ScopeLocked() ) {
		return;
	}
	from = scope.easing ? scope.easeTo : zoomFov;
	if ( !Q_stricmp( dir, "in" ) ) {
		scope.easeTo = from / SCOPE_STEP;
	} else if ( !Q_stricmp( dir, "out" ) ) {
		scope.easeTo = from * SCOPE_STEP;
	} else {
		trap->Print( "usage: v_scopezoom in|out\n" );
		return;
	}
	scope.easeTo = Com_Clamp( SCOPE_MIN_FOV, SCOPE_MAX_FOV, scope.easeTo );
	if ( scope.easeTo == from ) {
		return;
	}
	scope.easeFrom = zoomFov;
	scope.easeTime = cg.time;
	scope.easing = qtrue;
	trap->S_StartLocalSound( cgs.media.disruptorZoomLoop, CHAN_LOCAL_SOUND );
}

/*
=================
CG_ScopeKeyCommand

What a key is bound to, before it is run (CG_IncomingConsoleCommand): while the disruptor is zoomed the key of the
saber styles (saberAttackCycle, the middle mouse button in the defaults of the game) turns the tracker on and off,
the styles do nothing then anyway. qtrue: taken, the command is not run.
=================
*/
qboolean CG_ScopeKeyCommand( const char *command ) {
	char	word[32];
	int		i;

	while ( *command == ' ' ) {
		command++;
	}
	for ( i = 0; i < (int)sizeof( word ) - 1 && command[i] && command[i] != ' '; i++ ) {
		word[i] = command[i];
	}
	word[i] = 0;
	if ( Q_stricmp( word, "saberAttackCycle" ) || !CG_Scoped() ) {
		return qfalse;
	}
	scope.mode = (scopeMode_t)( ( Com_Clampi( 0, SCOPE_NUM_MODES - 1, v_scopeMode.integer ) + 1 ) % SCOPE_NUM_MODES );
	trap->Cvar_Set( "v_scopeMode", va( "%i", scope.mode ) );
	trap->Cvar_Update( &v_scopeMode );
	scope.nextPing = 0;
	scope.nextPhantom = cg.time + Q_irand( 1500, 4000 );
	trap->S_StartLocalSound( scope.mode != SCOPE_MODE_OFF ? cgs.media.zoomStart : cgs.media.zoomEnd, CHAN_LOCAL_SOUND );
	return qtrue;
}

/*
=================
CG_ScopeFrame

Once a frame before the view is worked out: the zoom of the wheel, the wheel for the engine, the modes
=================
*/
void CG_ScopeFrame( void ) {
	const qboolean	locked = CG_ScopeLocked();
	const qboolean	wheel = (qboolean)( locked && v_scopeWheelZoom.integer );
	const float		step = cg.frametime / (float)MODE_FADE_MSEC;

	if ( !scope.checked ) {
		CG_ScopeInit();
	}
	if ( wheel != scope.wheel ) {
		scope.wheel = wheel;
		trap->Cvar_Set( "v_scopeWheel", wheel ? "1" : "0" );
	}

	if ( scope.easing ) {
		const float f = ( cg.time - scope.easeTime ) / (float)SCOPE_EASE_MSEC;

		if ( !locked ) {
			scope.easing = qfalse;
		} else if ( f >= 1.0f || f < 0.0f ) {
			zoomFov = scope.easeTo;
			scope.easing = qfalse;
		} else {
			// eased out: fast at first, settling at the end
			zoomFov = scope.easeFrom + ( scope.easeTo - scope.easeFrom ) * ( 1.0f - ( 1.0f - f ) * ( 1.0f - f ) );
		}
	}

	// what was chosen last, while zoomed
	scope.mode = CG_Scoped() ? (scopeMode_t)Com_Clampi( 0, SCOPE_NUM_MODES - 1, v_scopeMode.integer ) : SCOPE_MODE_OFF;
	scope.trackerFade = Com_Clamp( 0.0f, 1.0f, scope.trackerFade + ( scope.mode == SCOPE_MODE_TRACKER ? step : -step ) );
	scope.contacts = 0;
	scope.nearest = 0.0f;
	scope.drawn = qfalse;
}

// the size of the lens in the plane one unit in front of the eye
static float CG_ScopeLensTan( void ) {
	return tanf( DEG2RAD( cg.refdef.fov_y * 0.5f ) ) * SCOPE_RADIUS * 2.0f;
}

// whether a point is in the lens: its angle off the middle of the view against the radius of the lens
static qboolean CG_ScopeInLens( const vec3_t point ) {
	const float	lens = CG_ScopeLensTan();
	vec3_t		dir;
	float		ahead;

	VectorSubtract( point, cg.refdef.vieworg, dir );
	ahead = DotProduct( dir, cg.refdef.viewaxis[0] );
	if ( ahead <= 0.0f ) {
		return qfalse;
	}
	return (qboolean)( DotProduct( dir, dir ) - ahead * ahead <= Square( lens * ahead ) );
}

// the distance to the first wall along dir, 0: nothing within reach. normal: of the wall.
static float CG_ScopeWall( const vec3_t dir, vec3_t end, vec3_t normal ) {
	trace_t	tr;
	vec3_t	to;

	VectorMA( cg.refdef.vieworg, 8192.0f, dir, to );
	trap->CM_Trace( &tr, cg.refdef.vieworg, to, NULL, NULL, 0, MASK_OPAQUE, 0 );
	if ( tr.fraction >= 1.0f || tr.startsolid || ( tr.surfaceFlags & SURF_SKY ) ) {
		return 0.0f;
	}
	if ( end ) {
		VectorCopy( tr.endpos, end );
	}
	if ( normal ) {
		VectorCopy( tr.plane.normal, normal );
	}
	return tr.fraction * 8192.0f;
}

/*
=================
CG_ScopePhantoms

The tracker is not perfect: now and then it takes an edge or a corner of the level in the lens for a mover, for a
moment. A spot in the lens is tried, and two beside it: a jump of the distance is an edge, walls that face other
ways are a corner.
=================
*/
static void CG_ScopePhantoms( void ) {
	const float	lens = CG_ScopeLensTan();
	int			i, attempt;

	if ( scope.mode != SCOPE_MODE_TRACKER || scope.trackerFade < 1.0f || cg.time < scope.nextPhantom ) {
		return;
	}
	for ( attempt = 0; attempt < 4; attempt++ ) {
		const float		angle = Q_flrand( 0.0f, 2.0f * M_PI );
		const float		out = sqrtf( Q_flrand( 0.05f, 0.85f ) );
		const float		x = cosf( angle ) * out * lens, y = sinf( angle ) * out * lens;
		const float		aside = lens * 0.03f;
		vec3_t			dir, dirRight, dirUp, hit, normal, normalRight, normalUp;
		float			dist, distRight, distUp;
		scopePhantom_t	*ph = NULL;

		VectorMA( cg.refdef.viewaxis[0], x, cg.refdef.viewaxis[1], dir );
		VectorMA( dir, y, cg.refdef.viewaxis[2], dir );
		VectorNormalize( dir );
		VectorMA( dir, aside, cg.refdef.viewaxis[1], dirRight );
		VectorNormalize( dirRight );
		VectorMA( dir, aside, cg.refdef.viewaxis[2], dirUp );
		VectorNormalize( dirUp );
		dist = CG_ScopeWall( dir, hit, normal );
		distRight = CG_ScopeWall( dirRight, NULL, normalRight );
		distUp = CG_ScopeWall( dirUp, NULL, normalUp );
		if ( dist < 256.0f || !distRight || !distUp ) {
			continue;
		}
		if ( fabsf( distRight - dist ) < dist * 0.12f && fabsf( distUp - dist ) < dist * 0.12f
			&& DotProduct( normal, normalRight ) > 0.7f && DotProduct( normal, normalUp ) > 0.7f ) {
			// a plain wall: nothing a tracker could mistake
			continue;
		}
		for ( i = 0; i < TRACK_PHANTOMS; i++ ) {
			if ( cg.time - scope.phantoms[i].birth >= scope.phantoms[i].life ) {
				ph = &scope.phantoms[i];
				break;
			}
		}
		if ( !ph ) {
			break;
		}
		memset( ph, 0, sizeof( *ph ) );
		// a little off the wall, towards the eye, about the size of a player
		VectorMA( hit, -8.0f, dir, ph->origin );
		ph->halfWidth = Q_flrand( 10.0f, 22.0f );
		ph->height = Q_flrand( 36.0f, 70.0f );
		ph->birth = cg.time;
		ph->life = Q_irand( 250, 900 );
		ph->seed = Q_irand( 0, 1023 );
		scope.nextPhantom = cg.time + Q_irand( 2500, 8000 );
		return;
	}
	scope.nextPhantom = cg.time + 400;
}

/*
=================
CG_ScopeScene

Before the view of the level is rendered: what the renderer is to make of it
=================
*/
void CG_ScopeScene( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	scopeEffect_t		fx;
	float				ammo;

	int					i;

	CG_ScopePhantoms();
	CG_ScopeUpdateTracker();
	if ( !scope.has || !CG_Scoped() || ( !v_scope.integer && scope.trackerFade <= 0.0f ) ) {
		return;
	}
	memset( &fx, 0, sizeof( fx ) );
	fx.flags = ( v_scope.integer ? SCOPEFX_LENS | SCOPEFX_RETICLE : 0 ) | ( scope.trackerFade > 0.0f ? SCOPEFX_TRACKER : 0 );
	fx.center[0] = fx.center[1] = 0.5f;
	fx.radius = SCOPE_RADIUS;
	fx.zoom = Com_Clamp( 0.0f, 1.0f, ( SCOPE_MAX_FOV - zoomFov ) / ( SCOPE_MAX_FOV - SCOPE_MIN_FOV ) );
	fx.motion = scope.trackerFade;
	// the boxes of the tracker keep the colours of the level: fractions of the view, the 2D grid is as wide as it
	for ( i = 0; i < scope.numBoxes && fx.numBoxes < SCOPEFX_MAX_BOXES; i++ ) {
		const float	width = SCREEN_WIDTH + 2.0f * cgs.bias2D;
		float		*box = fx.boxes[fx.numBoxes++];

		box[0] = ( scope.boxes[i].level[0] + cgs.bias2D ) / width;
		box[1] = scope.boxes[i].level[1] / SCREEN_HEIGHT;
		box[2] = ( scope.boxes[i].level[2] + cgs.bias2D ) / width;
		box[3] = scope.boxes[i].level[3] / SCREEN_HEIGHT;
	}
	fx.time = ( cg.time & 0xfffff ) * 0.001f;
	// as the art of the mask had them (CG_DrawZoomMask): 50 is the charge unit of the disruptor, 30 units at most
	fx.charge = ps->weaponstate == WEAPON_CHARGING_ALT ? Com_Clamp( 0.0f, 1.0f, ( cg.time - ps->weaponChargeTime ) / ( 50.0f * 30.0f ) ) : -1.0f;
	ammo = ps->ammo[weaponData[WP_DISRUPTOR].ammoIndex] / (float)ammoData[weaponData[WP_DISRUPTOR].ammoIndex].max;
	if ( ps->eFlags & EF_DOUBLE_AMMO ) {
		ammo *= 0.5f;
	}
	fx.ammo = Com_Clamp( 0.0f, 1.0f, ammo );
	scope.drawn = trap->ext.R_SetScopeEffect( &fx ) && v_scope.integer;
}

// how strongly an entity shows as a mover, worked out once a frame
static float CG_ScopeHeat( centity_t *cent ) {
	const int	num = cent->currentState.number;
	const float	seconds = ( cg.time - scope.heatTime[num] ) * 0.001f;
	float		speed;

	if ( scope.heatTime[num] == cg.time ) {
		return scope.heat[num];
	}
	scope.heatTime[num] = cg.time;
	if ( seconds < 0.0f || seconds > 0.5f ) {
		// not seen for a while, or a demo went back: from nothing again
		scope.heat[num] = 0.0f;
		return 0.0f;
	}
	speed = VectorLength( cent->currentState.pos.trDelta );
	if ( speed > v_motionThreshold.value ) {
		scope.heat[num] = Q_min( 1.0f, scope.heat[num] + MOTION_RISE * seconds );
	} else {
		scope.heat[num] = Q_max( 0.0f, scope.heat[num] - MOTION_FALL * seconds );
	}
	return scope.heat[num];
}

// a player a mode may show: not the one who looks, not cloaked, not dead
static float CG_ScopeMover( centity_t *cent, float fade ) {
	if ( fade <= 0.0f || cent->currentState.number == cg.predictedPlayerState.clientNum
		|| ( cent->currentState.powerups & ( 1 << PW_CLOAKED ) ) || ( cent->currentState.eFlags & EF_DEAD ) ) {
		return 0.0f;
	}
	return CG_ScopeHeat( cent ) * fade;
}

// the eye sees him: his middle or his head, past the level alone (he himself would be in the way of the entities)
static qboolean CG_ScopeSees( centity_t *cent ) {
	trace_t	tr;
	vec3_t	head;

	trap->CM_Trace( &tr, cg.refdef.vieworg, cent->lerpOrigin, NULL, NULL, 0, MASK_OPAQUE, 0 );
	if ( tr.fraction >= 1.0f ) {
		return qtrue;
	}
	VectorSet( head, cent->lerpOrigin[0], cent->lerpOrigin[1], cent->lerpOrigin[2] + 30.0f );
	trap->CM_Trace( &tr, cg.refdef.vieworg, head, NULL, NULL, 0, MASK_OPAQUE, 0 );
	return (qboolean)( tr.fraction >= 1.0f );
}

// the tracker saw a mover this frame: his box follows him
static void CG_ScopeTrack( centity_t *cent ) {
	scopeTrack_t	*track = &scope.tracks[cent->currentState.number];
	const int		solid = cent->currentState.solid;

	if ( !track->seenTime || cg.time - track->seenTime > TRACK_LOST_MSEC ) {
		track->lockTime = cg.time;
		track->rectValid = qfalse;
	}
	track->seenTime = cg.time;
	VectorCopy( cent->lerpOrigin, track->origin );
	if ( solid && solid != SOLID_BMODEL ) {
		// the box the server gave him, packed as CG_ClipMoveToEntities() reads it: crouched is lower
		const float x = (float)( solid & 255 ), zd = (float)( ( solid >> 8 ) & 255 ), zu = (float)( ( ( solid >> 16 ) & 255 ) - 32 );

		VectorSet( track->mins, -x, -x, -zd );
		VectorSet( track->maxs, x, x, zu );
	} else {
		VectorSet( track->mins, -15.0f, -15.0f, DEFAULT_MINS_2 );
		VectorSet( track->maxs, 15.0f, 15.0f, DEFAULT_MAXS_2 );
	}
}

/*
=================
CG_ScopeEntity

After the body of a player was added: the tracker follows him while he moves and can be seen in the lens
=================
*/
void CG_ScopeEntity( centity_t *cent ) {
	if ( CG_ScopeMover( cent, scope.trackerFade ) > 0.3f && CG_ScopeInLens( cent->lerpOrigin ) && CG_ScopeSees( cent ) ) {
		CG_ScopeTrack( cent );
		scope.contacts++;
	}
}

// a box of the level on the 2D grid: the corners of mins and maxs around origin. qfalse: behind the eye.
static qboolean CG_ScopeProject( const vec3_t origin, const vec3_t mins, const vec3_t maxs, float *rect ) {
	int i;

	rect[0] = rect[1] = 99999.0f;
	rect[2] = rect[3] = -99999.0f;
	for ( i = 0; i < 8; i++ ) {
		vec3_t	corner;
		float	x, y;

		corner[0] = origin[0] + ( ( i & 1 ) ? maxs[0] : mins[0] );
		corner[1] = origin[1] + ( ( i & 2 ) ? maxs[1] : mins[1] );
		corner[2] = origin[2] + ( ( i & 4 ) ? maxs[2] : mins[2] );
		if ( !CG_WorldCoordToScreenCoordFloat( corner, &x, &y ) ) {
			return qfalse;
		}
		rect[0] = Q_min( rect[0], x );
		rect[1] = Q_min( rect[1], y );
		rect[2] = Q_max( rect[2], x );
		rect[3] = Q_max( rect[3], y );
	}
	return qtrue;
}

// the box follows the mover softly, not pixel for pixel
static void CG_ScopeEase( float *rect, qboolean *valid, const float *target ) {
	const float	f = 1.0f - expf( -cg.frametime / 45.0f );
	int			i;

	if ( !*valid ) {
		memcpy( rect, target, sizeof( float ) * 4 );
		*valid = qtrue;
		return;
	}
	for ( i = 0; i < 4; i++ ) {
		rect[i] += ( target[i] - rect[i] ) * f;
	}
}

// the four corners of a box, a little bracket each, and the distance under it
static void CG_ScopeBox( const scopeBox_t *box ) {
	const float	left = box->rect[0], top = box->rect[1], right = box->rect[2], bottom = box->rect[3];
	const float	hw = ( right - left ) * 0.5f, hh = ( bottom - top ) * 0.5f;
	const float	alpha = box->alpha;
	const float	arm = Com_Clamp( 3.0f, 14.0f, Q_min( hw, hh ) * 0.45f );
	const float	line = 0.75f;
	vec4_t		color;
	vec4_t		shade;

	VectorSet4( shade, 0.0f, 0.0f, 0.0f, 0.45f * alpha );
	VectorSet4( color, 1.0f, 0.8f, 0.3f, alpha );
	// a thin shade under every line, so a box stays to be seen on a bright wall
	CG_FillRect( left - line, top - line, arm + line * 2.0f, line * 3.0f, shade );
	CG_FillRect( right - arm - line, top - line, arm + line * 2.0f, line * 3.0f, shade );
	CG_FillRect( left - line, bottom - line * 2.0f, arm + line * 2.0f, line * 3.0f, shade );
	CG_FillRect( right - arm - line, bottom - line * 2.0f, arm + line * 2.0f, line * 3.0f, shade );

	CG_FillRect( left, top, arm, line, color );
	CG_FillRect( left, top, line, arm, color );
	CG_FillRect( right - arm, top, arm, line, color );
	CG_FillRect( right - line, top, line, arm, color );
	CG_FillRect( left, bottom - line, arm, line, color );
	CG_FillRect( left, bottom - arm, line, arm, color );
	CG_FillRect( right - arm, bottom - line, arm, line, color );
	CG_FillRect( right - line, bottom - arm, line, arm, color );

	if ( box->distance > 0.0f ) {
		const char *text = va( "%im", (int)( box->distance / UNITS_PER_METER + 0.5f ) );

		CG_Text_Paint( left, bottom + 2.0f, 0.4f, color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );
	}
}

// how far the lens bends the picture at a place of the 2D grid, as scope.frag does it: the picture there comes
// from farther out, so a point of the level is seen nearer to the middle
static void CG_ScopeBend( float *x, float *y ) {
	const float	radius = SCOPE_RADIUS * SCREEN_HEIGHT;
	const float	zoom = Com_Clamp( 0.0f, 1.0f, ( SCOPE_MAX_FOV - zoomFov ) / ( SCOPE_MAX_FOV - SCOPE_MIN_FOV ) );
	const float	bend = 0.025f + 0.035f * zoom;
	const float	dx = *x - 320.0f, dy = *y - 240.0f;
	const float	r2 = ( dx * dx + dy * dy ) / ( radius * radius );

	*x = 320.0f + dx / ( 1.0f + bend * r2 );
	*y = 240.0f + dy / ( 1.0f + bend * r2 );
}

// a box for this frame in a list: its brackets around rect, grow: how far it still stands out while it locks on
static void CG_ScopeAddBox( scopeBox_t *list, int *count, int max, const float *rect, float grow, float distance, float alpha ) {
	const float	cx = ( rect[0] + rect[2] ) * 0.5f, cy = ( rect[1] + rect[3] ) * 0.5f;
	const float	hw = ( rect[2] - rect[0] ) * 0.5f * grow + 2.0f, hh = ( rect[3] - rect[1] ) * 0.5f * grow + 2.0f;
	scopeBox_t	*box;

	if ( *count >= max || alpha <= 0.0f ) {
		return;
	}
	box = &list[( *count )++];
	box->rect[0] = cx - hw;
	box->rect[1] = cy - hh;
	box->rect[2] = cx + hw;
	box->rect[3] = cy + hh;
	memcpy( box->level, box->rect, sizeof( box->level ) );
	if ( scope.has && v_scope.integer ) {
		// where the lens shows it: the corners bent as the picture is
		CG_ScopeBend( &box->rect[0], &box->rect[1] );
		CG_ScopeBend( &box->rect[2], &box->rect[3] );
	}
	box->distance = grow <= 1.0f ? distance : 0.0f;
	box->alpha = alpha;
}

// a ping: every box of a mover that is seen now, and every false one, stays behind as an echo
static void CG_ScopeLeaveEchoes( void ) {
	int i;

	for ( i = 0; i < MAX_GENTITIES + TRACK_PHANTOMS; i++ ) {
		scopeEcho_t *echo;

		if ( i < MAX_GENTITIES ) {
			const scopeTrack_t *track = &scope.tracks[i];

			if ( !track->seenTime || track->seenTime != cg.time ) {
				continue;
			}
			echo = &scope.echoes[scope.nextEcho];
			VectorCopy( track->origin, echo->origin );
			VectorCopy( track->mins, echo->mins );
			VectorCopy( track->maxs, echo->maxs );
		} else {
			const scopePhantom_t	*ph = &scope.phantoms[i - MAX_GENTITIES];
			const int				age = cg.time - ph->birth;

			if ( !ph->life || age < 0 || age >= ph->life ) {
				continue;
			}
			echo = &scope.echoes[scope.nextEcho];
			VectorCopy( ph->origin, echo->origin );
			VectorSet( echo->mins, -ph->halfWidth, -ph->halfWidth, -ph->height * 0.5f );
			VectorSet( echo->maxs, ph->halfWidth, ph->halfWidth, ph->height * 0.5f );
		}
		echo->birth = cg.time;
		scope.nextEcho = ( scope.nextEcho + 1 ) % TRACK_ECHOES;
	}
}

/*
=================
CG_ScopeUpdateTracker

Before the view is rendered: the boxes of the tracker this frame, the false ones among them, and its ping
=================
*/
static void CG_ScopeUpdateTracker( void ) {
	const float	alpha = scope.trackerFade;
	int			i, tracked = 0;

	scope.numBoxes = 0;
	scope.numEchoBoxes = 0;
	if ( alpha <= 0.0f ) {
		scope.nextPing = 0;
		return;
	}
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		scopeTrack_t	*track = &scope.tracks[i];
		const int		lost = cg.time - track->seenTime;
		const int		age = cg.time - track->lockTime;
		float			target[4], grow, fade, distance;

		if ( !track->seenTime || lost > TRACK_LOST_MSEC || lost < 0 ) {
			continue;
		}
		if ( !CG_ScopeProject( track->origin, track->mins, track->maxs, target ) ) {
			continue;
		}
		CG_ScopeEase( track->rect, &track->rectValid, target );
		distance = Distance( track->origin, cg.refdef.vieworg );
		// a new box closes in on its mover and blinks while it does, a lost one fades where he was last seen
		grow = age < TRACK_LOCK_MSEC ? 1.0f + 0.9f * ( 1.0f - age / (float)TRACK_LOCK_MSEC ) : 1.0f;
		fade = lost > 0 ? 1.0f - lost / (float)TRACK_LOST_MSEC : 1.0f;
		if ( age < TRACK_LOCK_MSEC && ( ( age / 55 ) & 1 ) ) {
			fade *= 0.35f;
		}
		CG_ScopeAddBox( scope.boxes, &scope.numBoxes, SCOPE_MAX_BOXES, track->rect, grow, distance, alpha * fade );
		if ( !lost ) {
			tracked++;
			scope.nearest = scope.nearest ? Q_min( scope.nearest, distance ) : distance;
		}
	}
	for ( i = 0; i < TRACK_PHANTOMS; i++ ) {
		scopePhantom_t	*ph = &scope.phantoms[i];
		const int		age = cg.time - ph->birth;
		vec3_t			mins, maxs;
		float			target[4], distance;

		if ( !ph->life || age < 0 || age >= ph->life ) {
			ph->rectValid = qfalse;
			continue;
		}
		VectorSet( mins, -ph->halfWidth, -ph->halfWidth, -ph->height * 0.5f );
		VectorSet( maxs, ph->halfWidth, ph->halfWidth, ph->height * 0.5f );
		if ( !CG_ScopeInLens( ph->origin ) || !CG_ScopeProject( ph->origin, mins, maxs, target ) ) {
			continue;
		}
		CG_ScopeEase( ph->rect, &ph->rectValid, target );
		// it flickers: the tracker is not sure of it
		if ( ( ( age / 45 ) * 7 + ph->seed ) % 5 == 0 ) {
			continue;
		}
		distance = Distance( ph->origin, cg.refdef.vieworg );
		CG_ScopeAddBox( scope.boxes, &scope.numBoxes, SCOPE_MAX_BOXES, ph->rect, age < TRACK_LOCK_MSEC ? 1.0f + 0.9f * ( 1.0f - age / (float)TRACK_LOCK_MSEC ) : 1.0f, distance,
			alpha * ( 1.0f - age / (float)ph->life * 0.5f ) );
		tracked++;
		scope.nearest = scope.nearest ? Q_min( scope.nearest, distance ) : distance;
	}

	// the ping, faster the nearer the nearest box is. It leaves the boxes behind where they are.
	if ( !tracked || scope.mode != SCOPE_MODE_TRACKER ) {
		scope.nextPing = 0;
	} else if ( cg.time >= scope.nextPing ) {
		trap->S_StartLocalSound( scope.pingSound, CHAN_LOCAL_SOUND );
		scope.nextPing = cg.time + (int)Com_Clamp( 180.0f, 950.0f, scope.nearest * 0.4f );
		CG_ScopeLeaveEchoes();
	}

	// the echoes fade where they were left, a little smaller each time they are drawn
	scope.numEchoBoxes = 0;
	for ( i = 0; i < TRACK_ECHOES; i++ ) {
		const scopeEcho_t	*echo = &scope.echoes[i];
		const int			age = cg.time - echo->birth;
		float				rect[4], left;

		if ( !echo->birth || age < 0 || age >= TRACK_ECHO_MSEC || !CG_ScopeInLens( echo->origin )
			|| !CG_ScopeProject( echo->origin, echo->mins, echo->maxs, rect ) ) {
			continue;
		}
		left = 1.0f - age / (float)TRACK_ECHO_MSEC;
		CG_ScopeAddBox( scope.echoBoxes, &scope.numEchoBoxes, TRACK_ECHOES, rect, 0.85f + 0.15f * left, 0.0f,
			alpha * 0.55f * left * left );
	}
}

// the brackets of the boxes of this frame
static void CG_ScopeDrawTracker( void ) {
	int i;

	for ( i = 0; i < scope.numEchoBoxes; i++ ) {
		CG_ScopeBox( &scope.echoBoxes[i] );
	}
	for ( i = 0; i < scope.numBoxes; i++ ) {
		CG_ScopeBox( &scope.boxes[i] );
	}
	trap->R_SetColor( NULL );
}

/*
=================
CG_ScopeLensDrawn

qtrue when the renderer draws the lens this frame: CG_DrawZoomMask() leaves out the mask and its art then
=================
*/
qboolean CG_ScopeLensDrawn( void ) {
	return scope.drawn;
}

/*
=================
CG_ScopeDrawHUD

In CG_DrawZoomMask(), for the disruptor, after the mask if there is one: the zoom under the lens, the light of a
mode and the pips of its contacts, the boxes of the tracker
=================
*/
void CG_ScopeDrawHUD( void ) {
	const float	radius = SCOPE_RADIUS * SCREEN_HEIGHT;
	vec4_t		color;
	int			i;

	if ( scope.drawn ) {
		// how many times larger than without the zoom, next to the arc of the zoom on the left
		const float	times = tanf( DEG2RAD( cg_fov.value * 0.5f ) ) / tanf( DEG2RAD( Q_max( zoomFov, 1.0f ) * 0.5f ) );
		const char	*text = va( "%.1fx", times );

		VectorSet4( color, 0.9f, 0.95f, 1.0f, 0.9f );
		CG_Text_Paint( 320.0f - radius * 1.1f - CG_Text_Width( text, 0.5f, FONT_SMALL ) - 4.0f, 232.0f, 0.5f, color, text, 0, 0,
			ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );
	}
	CG_ScopeDrawTracker();

	if ( scope.mode != SCOPE_MODE_OFF ) {
		const float	pulse = 0.6f + 0.4f * sinf( cg.time * 0.008f );
		const float	fade = scope.trackerFade;
		const float	x = 320.0f - radius * 0.5f, y = 240.0f + radius * 0.82f;

		// the light of the tracker under the lens to the left
		VectorSet4( color, 1.0f, 0.8f, 0.3f, pulse * fade );
		CG_FillRect( x, y, 5.0f, 5.0f, color );
		// a pip a contact, to the right of it
		color[3] = 0.9f * fade;
		for ( i = 0; i < scope.contacts && i < MOTION_MAX_PIPS; i++ ) {
			CG_FillRect( x + 12.0f + i * 8.0f, y + 1.0f, 5.0f, 3.0f, color );
		}
	}
	trap->R_SetColor( NULL );
}
