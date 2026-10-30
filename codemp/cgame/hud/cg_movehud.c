/*
===========================================================================
Copyright (C) 2015 - 2023, TaystJK contributors
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

// cg_movehud.c -- the movement HUD (2026-10-17), after TaystJK's hud_strafehelper.c (from jaPRO and the CGaz of
// DeFRaG), for the physics of the game only (bg_pmove.c), not for the movement styles of jaPRO:
// - tay_speedometer: the speed along the ground under the crosshair, and the speeds of the last jumps
// - tay_strafeHelper: a bar under the crosshair with where to turn to gain speed for the keys that are held (CGaz):
//   from the angle at which speed starts to come, over the best one, to the one past which it goes
// - tay_snapHud: the zones of the angle in which the rounding of the velocity at the end of every move (the
//   SnapVector at the end of PmoveSingle) gives a little more speed, alternating colours
// - tay_pitchHud: a mark at the pitch tay_pitchHudAngle
// - tay_movementKeys: the keys of moving that are held, as jaPRO and TaystJK draw them (cg_movementKeys)
// - tay_startGoal, tay_jumpGoal: the speed and the speeds of the jumps in gold once they reach these
// The angles of the bars are where the view has to point: the middle of the screen is where it points now.

#include "cgame/cg_local.h"
#include "game/bg/bg_local.h"
#include "ui/menudef.h"

static struct {
	qboolean	wasOnGround;
	int			jumps[16];			// horizontal speed when the ground was left, the newest first
	int			numJumps;
	int			lastTime;
} moveHud;

// the colours a cvar of "r g b a" gives, def where it gives less
static void MoveHud_Color( const vmCvar_t *cv, const vec4_t def, vec4_t out ) {
	if ( sscanf( cv->string, "%f %f %f %f", &out[0], &out[1], &out[2], &out[3] ) != 4 ) {
		VectorCopy4( def, out );
	}
}

// fills an angle interval of the view's yaw (degrees, + left) on a bar from y on
static void MoveHud_FillYaw( float from, float to, float y, float h, const vec4_t color ) {
	const float half = cg.refdef.fov_x * 0.5f;
	const float scale = tanf( DEG2RAD( half ) );
	const float halfW = 0.5f * SCREEN_WIDTH + cgs.bias2D;		// the window from its middle, on the grid of the 2D
	float x1, x2;

	from = AngleNormalize180( from );
	to = AngleNormalize180( to );
	if ( from > to ) {
		const float t = from;

		from = to;
		to = t;
	}
	if ( to - from > 180.0f || to < -half || from > half ) {
		return;		// past the edge, or round the back
	}
	from = Q_max( from, -half + 0.01f );
	to = Q_min( to, half - 0.01f );

	// a yaw to the left is to the left of the middle
	x1 = 0.5f * SCREEN_WIDTH - tanf( DEG2RAD( to ) ) / scale * halfW;
	x2 = 0.5f * SCREEN_WIDTH - tanf( DEG2RAD( from ) ) / scale * halfW;
	trap->R_SetColor( color );
	trap->R_DrawStretchPic( x1, y, Q_max( 1.0f, x2 - x1 ), h, 0, 0, 0, 0, cgs.media.whiteShader );
	trap->R_SetColor( NULL );
}

// seconds of a move
static float MoveHud_FrameTime( void ) {
	static vmCvar_t	maxFps;
	static qboolean	registered;
	float			fps;

	if ( !registered ) {
		trap->Cvar_Register( &maxFps, "com_maxfps", "125", 0 );
		registered = qtrue;
	}
	trap->Cvar_Update( &maxFps );
	fps = maxFps.value;

	if ( pmove_fixed.integer && pmove_msec.integer > 0 ) {
		return pmove_msec.integer * 0.001f;
	}
	if ( fps > 0.0f ) {
		return 1.0f / fps;
	}
	return Q_max( 1, cg.frametime ) * 0.001f;
}

// the keys: where the wished direction is to the view (degrees, + left), and the wished speed. qfalse with no key
static qboolean MoveHud_Wish( const playerState_t *ps, float *keyYaw, float *wishSpeed ) {
	usercmd_t	cmd;
	float		fmove, rmove, umove, most, total;

	if ( ps->clientNum != cg.clientNum || cg.demoPlayback ) {
		// someone followed: what the server says of his keys
		*keyYaw = ps->movementDir * 45.0f;
		*wishSpeed = ps->speed;
		return (qboolean)( VectorLength( ps->velocity ) > 0.0f );
	}
	if ( !trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd ) ) {
		return qfalse;
	}
	fmove = cmd.forwardmove;
	rmove = cmd.rightmove;
	umove = cmd.upmove;
	if ( !fmove && !rmove ) {
		return qfalse;
	}
	*keyYaw = RAD2DEG( atan2f( -rmove, fmove ) );

	// PM_CmdScale
	most = Q_max( fabsf( fmove ), Q_max( fabsf( rmove ), fabsf( umove ) ) );
	total = sqrtf( fmove * fmove + rmove * rmove + umove * umove );
	*wishSpeed = total > 0.0f ? ps->speed * most / ( 127.0f * total ) : 0.0f;
	if ( ps->pm_flags & PMF_DUCKED ) {
		*wishSpeed *= pm_duckScale;
	}
	return qtrue;
}

static float MoveHud_Acos( float x ) {
	return RAD2DEG( acosf( Com_Clamp( -1.0f, 1.0f, x ) ) );
}

static void MoveHud_StrafeHelper( const playerState_t *ps, float keyYaw, float s, qboolean ground ) {
	static const vec4_t defBest = { 0.0f, 1.0f, 0.4f, 0.75f };
	static const vec4_t defLoss = { 1.0f, 0.75f, 0.0f, 0.5f };
	const float	ft = MoveHud_FrameTime();
	const float	v = sqrtf( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
	const float	y = SCREEN_HEIGHT * 0.5f + 16.0f + tay_strafeHelperY.value, h = 6.0f;
	const qboolean friction = ground && moveHud.wasOnGround;
	vec4_t		slow, best, loss;
	float		vf = v, a, dMin, dOpt, dCos, dMax, velYaw;
	int			side;

	if ( v < 1.0f || s <= 0.0f ) {
		return;
	}
	MoveHud_Color( &tay_strafeHelperColor, defBest, best );
	VectorCopy4( best, slow );
	slow[3] *= 0.5f;
	VectorCopy4( defLoss, loss );

	// the speed after friction (PM_Friction), on the ground for more than a frame
	if ( friction ) {
		const float control = v < pm_stopspeed ? pm_stopspeed : v;

		vf = Q_max( 0.0f, v - control * pm_friction * ft );
	}
	a = ( ground ? pm_accelerate : pm_airaccelerate ) * s * ft;
	if ( vf <= 0.0f || a <= 0.0f ) {
		return;
	}

	// the angles between velocity and wished direction (CGaz): no gain below dMin, the most at dOpt, the speed
	// stays at dCos, past dMax it goes down
	if ( friction ) {
		dMin = s * s - v * v + vf * vf >= 0.0f ? MoveHud_Acos( sqrtf( s * s - v * v + vf * vf ) / vf ) : 0.0f;
		dCos = MoveHud_Acos( ( v - vf ) / a );
		dMax = MoveHud_Acos( ( v * v - vf * vf - a * a ) / ( 2.0f * a * vf ) );
	} else {
		dMin = MoveHud_Acos( s / v );
		dCos = 90.0f;
		dMax = MoveHud_Acos( -a / ( 2.0f * v ) );
	}
	dOpt = MoveHud_Acos( ( s - a ) / vf );

	velYaw = RAD2DEG( atan2f( ps->velocity[1], ps->velocity[0] ) );
	for ( side = -1; side <= 1; side += 2 ) {
		// the view yaw that puts the wished direction at velYaw + side * d, from the view now
		const float base = velYaw - keyYaw - cg.refdef.viewangles[YAW];

		MoveHud_FillYaw( base + side * dMin, base + side * dOpt, y, h, slow );
		MoveHud_FillYaw( base + side * dOpt, base + side * dCos, y, h, best );
		MoveHud_FillYaw( base + side * dCos, base + side * dMax, y, h, loss );
	}
	// where the view points
	trap->R_SetColor( colorWhite );
	trap->R_DrawStretchPic( SCREEN_WIDTH * 0.5f - 0.5f, y - 2.0f, 1.0f, h + 4.0f, 0, 0, 0, 0, cgs.media.whiteShader );
	trap->R_SetColor( NULL );
}

static void MoveHud_SnapHud( float keyYaw, float s ) {
	static const vec4_t def1 = { 0.0f, 0.6f, 1.0f, 0.35f }, def2 = { 1.0f, 1.0f, 1.0f, 0.15f };
	const float	speed = s * pm_airaccelerate * MoveHud_FrameTime();		// what a move in the air adds
	const float	y = SCREEN_HEIGHT * 0.5f + 26.0f + tay_snapHudY.value, h = 4.0f;
	float		zones[66], step;
	vec4_t		color[2];
	int			count = 0, i, quarter;

	if ( speed < 0.5f ) {
		return;
	}
	MoveHud_Color( &tay_snapHudColor1, def1, color[0] );
	MoveHud_Color( &tay_snapHudColor2, def2, color[1] );

	// a velocity component rounds up once speed * cos (or sin) of the angle passes n + 0.5
	for ( step = floorf( speed + 0.5f ) - 0.5f; step > 0.0f && count < (int)ARRAY_LEN( zones ) - 2; step -= 1.0f ) {
		zones[count++] = MoveHud_Acos( step / speed );
		zones[count++] = 90.0f - MoveHud_Acos( step / speed );
	}
	if ( !count ) {
		return;
	}
	for ( i = 1; i < count; i++ ) {		// sort, a handful of values
		const float z = zones[i];
		int j = i - 1;

		while ( j >= 0 && zones[j] > z ) {
			zones[j + 1] = zones[j];
			j--;
		}
		zones[j + 1] = z;
	}
	zones[count] = zones[0] + 90.0f;

	// the zones lie on the axes of the world, for the wished direction
	for ( quarter = 0; quarter < 4; quarter++ ) {
		for ( i = 0; i < count; i++ ) {
			const float base = quarter * 90.0f - keyYaw - cg.refdef.viewangles[YAW];

			MoveHud_FillYaw( base + zones[i], base + zones[i + 1], y, h, color[i & 1] );
		}
	}
}

static void MoveHud_PitchHud( void ) {
	static const vec4_t def = { 1.0f, 0.4f, 0.0f, 0.8f };
	const float	half = cg.refdef.fov_y * 0.5f;
	const float	d = tay_pitchHudAngle.value - cg.refdef.viewangles[PITCH];
	vec4_t		color;
	float		y;

	if ( fabsf( d ) >= half ) {
		return;
	}
	MoveHud_Color( &tay_pitchHudColor, def, color );
	y = 0.5f * SCREEN_HEIGHT + tanf( DEG2RAD( d ) ) / tanf( DEG2RAD( half ) ) * ( 0.5f * SCREEN_HEIGHT );
	trap->R_SetColor( color );
	trap->R_DrawStretchPic( SCREEN_WIDTH * 0.5f - 12.0f, y - 1.0f, 24.0f, 2.0f, 0, 0, 0, 0, cgs.media.whiteShader );
	trap->R_SetColor( NULL );
}

static void MoveHud_Speedometer( const playerState_t *ps ) {
	const float	size = Com_Clamp( 0.5f, 2.0f, tay_speedometerSize.value );
	const float	v = sqrtf( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] );
	const float	x = SCREEN_WIDTH * 0.5f + tay_speedometerX.value;
	const float	y = SCREEN_HEIGHT * 0.5f + 36.0f + tay_speedometerY.value;
	vec4_t		color = { 1.0f, 1.0f, 1.0f, 0.9f };
	const char	*text = va( "%i", (int)( v + 0.5f ) );
	char		jumps[128];
	int			i;

	// faster than walking is green, much faster is gold; with tay_startGoal gold from that speed on
	if ( tay_startGoal.integer > 0 ) {
		if ( v >= tay_startGoal.value ) {
			VectorSet( color, 1.0f, 0.8f, 0.2f );
		}
	} else if ( v > ps->speed * 1.6f ) {
		VectorSet( color, 1.0f, 0.8f, 0.2f );
	} else if ( v > ps->speed * 1.05f ) {
		VectorSet( color, 0.4f, 1.0f, 0.5f );
	}
	CG_Text_Paint( x - CG_Text_Width( text, 0.8f * size, FONT_SMALL ) * 0.5f, y, 0.8f * size, color, text, 0, 0,
		ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );

	if ( tay_speedometerJumps.integer > 0 && moveHud.numJumps ) {
		jumps[0] = '\0';
		for ( i = 0; i < moveHud.numJumps && i < tay_speedometerJumps.integer; i++ ) {
			// tay_jumpGoal: a jump that reached it in gold
			const char *mark = ( tay_jumpGoal.integer > 0 && moveHud.jumps[i] >= tay_jumpGoal.integer ) ? S_COLOR_YELLOW : S_COLOR_WHITE;

			Q_strcat( jumps, sizeof( jumps ), va( i ? "  %s%i" : "%s%i", mark, moveHud.jumps[i] ) );
		}
		VectorSet( color, 0.8f, 0.85f, 0.9f );
		CG_Text_Paint( x - CG_Text_Width( jumps, 0.55f * size, FONT_SMALL ) * 0.5f, y + 14.0f * size, 0.55f * size,
			color, jumps, 0, 0, ITEM_TEXTSTYLE_SHADOWED, FONT_SMALL );
	}
}

// One key of tay_movementKeys: lit while it is held
static void MoveHud_Key( float x, float y, float w, float h, const char *label, qboolean held ) {
	const vec4_t	litFill = { 0.43f, 0.83f, 1.0f, 0.75f }, dimFill = { 0.0f, 0.0f, 0.0f, 0.35f };
	const vec4_t	edge = { 1.0f, 1.0f, 1.0f, 0.35f };
	const vec4_t	litText = { 0.02f, 0.06f, 0.1f, 1.0f }, dimText = { 1.0f, 1.0f, 1.0f, 0.55f };
	const float		scale = h / 34.0f;
	vec4_t			text;

	VectorCopy4( held ? litText : dimText, text );
	CG_FillRect( x, y, w, h, held ? litFill : dimFill );
	CG_DrawRect( x, y, w, h, 1.0f, edge );
	CG_Text_Paint( x + ( w - CG_Text_Width( label, scale, FONT_SMALL ) ) * 0.5f, y + ( h - CG_Text_Height( label, scale, FONT_SMALL ) ) * 0.5f - 1.0f,
		scale, text, label, 0, 0, ITEM_TEXTSTYLE_NORMAL, FONT_SMALL );
}

/*
=================
MoveHud_MovementKeys

Crouch, forward and jump above, left, back and right below. The own keys from the last command; for someone who
is followed what the server says: the direction of his last move (movementDir, 0 forward, counted to the left
in eighths) while he moves, ducked, and the jump held.
=================
*/
static void MoveHud_MovementKeys( const playerState_t *ps ) {
	const float	size = Com_Clamp( 0.5f, 2.0f, tay_movementKeysSize.value );
	const float	k = 16.0f * size, gap = 2.0f * size;
	const float	x = SCREEN_WIDTH * 0.5f - 1.5f * k - gap + tay_movementKeysX.value;
	const float	y = SCREEN_HEIGHT * 0.5f + 70.0f + tay_movementKeysY.value;
	qboolean	forward = qfalse, back = qfalse, left = qfalse, right = qfalse, jump = qfalse, crouch = qfalse;
	usercmd_t	cmd;

	if ( ps->clientNum == cg.clientNum && !cg.demoPlayback ) {
		if ( !trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd ) ) {
			return;
		}
		forward = (qboolean)( cmd.forwardmove > 0 );
		back = (qboolean)( cmd.forwardmove < 0 );
		left = (qboolean)( cmd.rightmove < 0 );
		right = (qboolean)( cmd.rightmove > 0 );
		jump = (qboolean)( cmd.upmove > 0 );
		crouch = (qboolean)( cmd.upmove < 0 );
	} else {
		static const qboolean dirs[8][4] = {	// forward, left, back, right for movementDir 0 to 7
			{ qtrue, qfalse, qfalse, qfalse }, { qtrue, qtrue, qfalse, qfalse }, { qfalse, qtrue, qfalse, qfalse },
			{ qfalse, qtrue, qtrue, qfalse }, { qfalse, qfalse, qtrue, qfalse }, { qfalse, qfalse, qtrue, qtrue },
			{ qfalse, qfalse, qfalse, qtrue }, { qtrue, qfalse, qfalse, qtrue } };

		if ( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] > 100.0f ) {
			const int d = ps->movementDir & 7;

			forward = dirs[d][0];
			left = dirs[d][1];
			back = dirs[d][2];
			right = dirs[d][3];
		}
		jump = (qboolean)( ( ps->pm_flags & PMF_JUMP_HELD ) != 0 );
		crouch = (qboolean)( ( ps->pm_flags & PMF_DUCKED ) != 0 );
	}

	MoveHud_Key( x, y, k, k, "C", crouch );
	MoveHud_Key( x + k + gap, y, k, k, "W", forward );
	MoveHud_Key( x + 2.0f * ( k + gap ), y, k, k, "J", jump );
	MoveHud_Key( x, y + k + gap, k, k, "A", left );
	MoveHud_Key( x + k + gap, y + k + gap, k, k, "S", back );
	MoveHud_Key( x + 2.0f * ( k + gap ), y + k + gap, k, k, "D", right );
}

/*
=================
CG_MoveHudDraw

From CG_Draw2D, with the 2D in the middle (PLACE_CENTER)
=================
*/
void CG_MoveHudDraw( void ) {
	const playerState_t	*ps = &cg.predictedPlayerState;
	const qboolean		ground = (qboolean)( ps->groundEntityNum != ENTITYNUM_NONE );
	float				keyYaw = 0.0f, s = ps->speed;
	qboolean			keys;

	if ( !tay_speedometer.integer && !tay_strafeHelper.integer && !tay_snapHud.integer && !tay_pitchHud.integer && !tay_movementKeys.integer ) {
		return;
	}
	if ( !cg.snap || cg.snap->ps.pm_type == PM_INTERMISSION || ps->stats[STAT_HEALTH] <= 0 ) {
		return;
	}
	if ( !( ps->pm_flags & PMF_FOLLOW ) && ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}
	if ( cg.time < moveHud.lastTime ) {
		memset( &moveHud, 0, sizeof( moveHud ) );		// a new level
	}

	// a jump: the ground was left
	if ( cg.time != moveHud.lastTime && moveHud.wasOnGround && !ground && ps->velocity[2] > 0.0f ) {
		memmove( moveHud.jumps + 1, moveHud.jumps, sizeof( moveHud.jumps ) - sizeof( moveHud.jumps[0] ) );
		moveHud.jumps[0] = (int)( sqrtf( ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] ) + 0.5f );
		moveHud.numJumps = Q_min( moveHud.numJumps + 1, (int)ARRAY_LEN( moveHud.jumps ) );
	}

	keys = MoveHud_Wish( ps, &keyYaw, &s );
	if ( tay_strafeHelper.integer && keys ) {
		MoveHud_StrafeHelper( ps, keyYaw, s, ground );
	}
	if ( tay_snapHud.integer && keys && !ground ) {
		MoveHud_SnapHud( keyYaw, s );
	}
	if ( tay_pitchHud.integer ) {
		MoveHud_PitchHud();
	}
	if ( tay_speedometer.integer ) {
		MoveHud_Speedometer( ps );
	}
	if ( tay_movementKeys.integer ) {
		MoveHud_MovementKeys( ps );
	}

	if ( cg.time != moveHud.lastTime ) {
		moveHud.wasOnGround = ground;
		moveHud.lastTime = cg.time;
	}
}
