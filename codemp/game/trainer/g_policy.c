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

/*
The learned policy (vs_trainer ai): a small neural network that makes the usercmd of a training bot once a server
frame. It sees itself and the player it fights (the observation of tools/trainer/rl/spec.py, laid out in
policy_spec.h) and picks one action per head as the clone has them: move, jump, hook, attack, where to aim.

tools/trainer/rl trains it: first from the movement logs of a player (behaviour cloning), then in matches of bots
on servers of the simulation, where it plays and learns from what it got for it (PPO). For that the matches write
every frame of the policy, what it saw, did and got, into rollouts/bot<slot>.bin (vs_rlRollouts 1). The network
that runs here is the same in training and on a live server.

Two models can be loaded: slot 0 from vs_policyModel ("ai"), slot 1 from vs_policyOpponent ("ai2", an older one
to fight). The files are written by tools/trainer/rl/export.py, the layout is in Policy_Parse().

Not from JA+.
*/

#include "game/g_local.h"
#include "g_policy.h"
#include "clone_tables.h"
#include "policy_spec.h"

#if POLICY_PROBES != POLICY_PROBES_LOGGED
#error the movement log has another number of probes than the policy
#endif

#define POLICY_MAGIC		"VJKPOL01"
#define POLICY_MAX_HIDDEN	512
#define POLICY_MAX_FILE		(4 * 1024 * 1024)
#define POLICY_AIM_SEC		0.1f		// the aim follows the picked bin with this time constant, as a mouse
#define ROLLOUT_MAGIC		"VJKRL001"
#define ROLLOUT_FOOTER		"END!"
#define ROLLOUT_RECORD		( POLICY_OBS * 4 + POLICY_HEADS + 2 + 4 + 4 )

typedef struct {
	qboolean	loaded;
	int			modified;		// modificationCount of its cvar when it was read
	int			version;		// given by tools/trainer/rl, goes into the rollouts
	int			hidden[2];
	float		mean[POLICY_OBS], scale[POLICY_OBS];	// the observation is (x - mean) * scale
	float		w1[POLICY_MAX_HIDDEN * POLICY_OBS], b1[POLICY_MAX_HIDDEN];
	float		w2[POLICY_MAX_HIDDEN * POLICY_MAX_HIDDEN], b2[POLICY_MAX_HIDDEN];
	float		w3[POLICY_LOGITS * POLICY_MAX_HIDDEN], b3[POLICY_LOGITS];
} policyModel_t;

typedef struct {
	qboolean		on;				// a policy bot
	int				slot;			// its model
	int				prev[POLICY_HEADS];	// the actions of the frame before
	float			yawOff, pitch;	// the aim, smoothed
	float			reward;			// since the last decision
	// rollouts
	fileHandle_t	f;
	int				records;
	qboolean		pending;		// the last decision waits for its reward
	byte			record[ROLLOUT_RECORD];
} policyBot_t;

static policyModel_t	policyModels[POLICY_SLOTS];
static policyBot_t		policyBots[MAX_CLIENTS];
static byte				policyFile[POLICY_MAX_FILE];

static struct {
	int		modified;
	float	damage, kill, death, kick;
} policyReward = { -1, 0.01f, 1.0f, 1.0f, 0.3f };

// little endian numbers out of the file, qfalse past its end
static qboolean Policy_Ints( const byte **p, const byte *end, int *out, int count ) {
	int i;

	if ( end - *p < 4 * count )
		return qfalse;
	for ( i = 0; i < count; i++, *p += 4 )
		out[i] = (int)( (unsigned)(*p)[0] | ((unsigned)(*p)[1] << 8) | ((unsigned)(*p)[2] << 16) | ((unsigned)(*p)[3] << 24) );
	return qtrue;
}

static qboolean Policy_Floats( const byte **p, const byte *end, float *out, int count ) {
	int i, bits;

	for ( i = 0; i < count; i++ ) {
		if ( !Policy_Ints( p, end, &bits, 1 ) )
			return qfalse;
		memcpy( &out[i], &bits, sizeof( out[i] ) );
	}
	return qtrue;
}

/*
=================
Policy_Parse

magic, version, observations, heads, the size of each head, the two hidden layers; then floats: the mean and the
standard deviation of each observation, layer 1 (weights by output, then input; biases), layer 2, the logits. Both
hidden layers are tanh. Little endian.
=================
*/
static qboolean Policy_Parse( policyModel_t *m, const byte *p, const byte *end ) {
	int		head[4 + POLICY_HEADS + 2], i;
	float	std[POLICY_OBS];

	if ( end - p < 8 || memcmp( p, POLICY_MAGIC, 8 ) )
		return qfalse;
	p += 8;
	if ( !Policy_Ints( &p, end, head, 3 ) || head[1] != POLICY_OBS || head[2] != POLICY_HEADS
		|| !Policy_Ints( &p, end, head + 3, POLICY_HEADS + 2 ) )
		return qfalse;
	for ( i = 0; i < POLICY_HEADS; i++ ) {
		if ( head[3 + i] != policyHeadSize[i] )
			return qfalse;
	}
	m->version = head[0];
	m->hidden[0] = head[3 + POLICY_HEADS];
	m->hidden[1] = head[4 + POLICY_HEADS];
	if ( m->hidden[0] < 1 || m->hidden[0] > POLICY_MAX_HIDDEN || m->hidden[1] < 1 || m->hidden[1] > POLICY_MAX_HIDDEN )
		return qfalse;
	if ( !Policy_Floats( &p, end, m->mean, POLICY_OBS ) || !Policy_Floats( &p, end, std, POLICY_OBS )
		|| !Policy_Floats( &p, end, m->w1, m->hidden[0] * POLICY_OBS ) || !Policy_Floats( &p, end, m->b1, m->hidden[0] )
		|| !Policy_Floats( &p, end, m->w2, m->hidden[1] * m->hidden[0] ) || !Policy_Floats( &p, end, m->b2, m->hidden[1] )
		|| !Policy_Floats( &p, end, m->w3, POLICY_LOGITS * m->hidden[1] ) || !Policy_Floats( &p, end, m->b3, POLICY_LOGITS ) )
		return qfalse;
	for ( i = 0; i < POLICY_OBS; i++ )
		m->scale[i] = ( std[i] > 1e-6f ) ? 1.0f / std[i] : 0.0f;
	return (qboolean)( p == end );
}

// the model of a slot, read again when its cvar changes; NULL if there is none
static const policyModel_t *Policy_Model( int slot ) {
	const vmCvar_t	*cv = slot ? &vs_policyOpponent : &vs_policyModel;
	policyModel_t	*m;
	fileHandle_t	f;
	int				len;

	if ( slot < 0 || slot >= POLICY_SLOTS )
		return NULL;
	m = &policyModels[slot];
	if ( m->modified != cv->modificationCount ) {
		m->modified = cv->modificationCount;
		m->loaded = qfalse;
		if ( cv->string[0] ) {
			len = trap->FS_Open( cv->string, &f, FS_READ );
			if ( !f )
				trap->Print( "%s: no file %s\n", slot ? "vs_policyOpponent" : "vs_policyModel", cv->string );
			else if ( len <= 0 || len > POLICY_MAX_FILE ) {
				trap->FS_Close( f );
				trap->Print( "%s: %s is empty or larger than %d bytes\n", slot ? "vs_policyOpponent" : "vs_policyModel",
					cv->string, POLICY_MAX_FILE );
			}
			else {
				trap->FS_Read( policyFile, len, f );
				trap->FS_Close( f );
				m->loaded = Policy_Parse( m, policyFile, policyFile + len );
				trap->Print( m->loaded ? "%s: %s loaded (version %d)\n" : "%s: %s is no policy of this version\n",
					slot ? "vs_policyOpponent" : "vs_policyModel", cv->string, m->version );
			}
		}
	}
	return m->loaded ? m : NULL;
}

qboolean Policy_Ready( int slot ) {
	return (qboolean)( Policy_Model( slot ) != NULL );
}

static void Policy_LoadReward( void ) {
	if ( policyReward.modified == vs_rlReward.modificationCount )
		return;
	policyReward.modified = vs_rlReward.modificationCount;
	if ( sscanf( vs_rlReward.string, "%f %f %f %f", &policyReward.damage, &policyReward.kill, &policyReward.death,
		&policyReward.kick ) != 4 )
		trap->Print( "vs_rlReward: \"%s\" is not four numbers (damage kill death kick)\n", vs_rlReward.string );
}

// a policy bot of that slot from now on, with nothing done before; -1: no policy bot any more
void Policy_Reset( int clientNum, int slot ) {
	policyBot_t	*pb;
	int			h;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS )
		return;
	pb = &policyBots[clientNum];
	if ( pb->f )
		Policy_MatchEnd( clientNum );
	memset( pb, 0, sizeof( *pb ) );
	pb->on = (qboolean)( slot >= 0 );
	pb->slot = slot;
	for ( h = 0; h < CLONE_HEADS; h++ )
		pb->prev[h] = cloneNoAction[h];
	pb->pitch = clonePitch[cloneNoAction[5]];
}

/*
=================
Policy_Probes

how far there is a wall in 8 directions around a player (a share of POLICY_WALL_RANGE) and how far the floor is
down POLICY_DROP_OUT out there (a share of POLICY_DROP_RANGE: 1 is a pit), the first direction toward, then to the
left of it
=================
*/
void Policy_Probes( const gentity_t *ent, const vec3_t toward, float *wall, float *drop ) {
	const float	*origin = ent->client->ps.origin;
	vec3_t		dir, end, out;
	trace_t		tr;
	int			i;

	for ( i = 0; i < POLICY_PROBES; i++ ) {
		const float a = 2.0f * M_PI * i / POLICY_PROBES;

		dir[0] = toward[0] * cosf( a ) - toward[1] * sinf( a );
		dir[1] = toward[1] * cosf( a ) + toward[0] * sinf( a );
		dir[2] = 0.0f;
		VectorMA( origin, POLICY_WALL_RANGE, dir, end );
		trap->Trace( &tr, origin, NULL, NULL, end, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
		wall[i] = tr.fraction;
		// the floor out there, short of a wall nearer than that
		VectorMA( origin, Q_min( POLICY_DROP_OUT, tr.fraction * POLICY_WALL_RANGE - 8.0f ), dir, out );
		VectorCopy( out, end );
		end[2] -= POLICY_DROP_RANGE;
		trap->Trace( &tr, out, NULL, NULL, end, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
		drop[i] = tr.fraction;
	}
}

static void Policy_OneHot( float *x, int at, int count, int i ) {
	if ( i >= 0 && i < count )
		x[at + i] = 1.0f;
}

// a point relative to the player along the line to the opponent (ux, uy) and across it, and up
static void Policy_Along( float *x, const vec3_t point, const vec3_t origin, float ux, float uy ) {
	const float dx = point[0] - origin[0], dy = point[1] - origin[1];

	x[0] = ( dx * ux + dy * uy ) / 500.0f;
	x[1] = ( -dx * uy + dy * ux ) / 500.0f;
	x[2] = ( point[2] - origin[2] ) / 500.0f;
}

// tools/trainer/rl/spec.py observe(): the same numbers in the same places
static void Policy_Observe( const gentity_t *self, const gentity_t *enemy, const policyBot_t *pb, float *x ) {
	const playerState_t	*ps = &self->client->ps, *ops = &enemy->client->ps;
	const gclient_t		*cl = self->client, *ocl = enemy->client;
	vec3_t				d, eye, angles, toward;
	float				flat, ux, uy, a;

	memset( x, 0, sizeof( float ) * POLICY_OBS );
	VectorSubtract( ops->origin, ps->origin, d );
	flat = sqrtf( d[0] * d[0] + d[1] * d[1] );
	ux = ( flat > 0.001f ) ? d[0] / flat : 1.0f;
	uy = ( flat > 0.001f ) ? d[1] / flat : 0.0f;

	x[PO_FLAT] = Com_Clamp( 0.0f, 3.0f, flat / 1000.0f );
	x[PO_DZ] = Com_Clamp( -3.0f, 3.0f, d[2] / 500.0f );
	x[PO_VEL + 0] = ( ps->velocity[0] * ux + ps->velocity[1] * uy ) / 1000.0f;
	x[PO_VEL + 1] = ( -ps->velocity[0] * uy + ps->velocity[1] * ux ) / 1000.0f;
	x[PO_VEL + 2] = ps->velocity[2] / 1000.0f;
	x[PO_OVEL + 0] = ( ops->velocity[0] * ux + ops->velocity[1] * uy ) / 1000.0f;
	x[PO_OVEL + 1] = ( -ops->velocity[0] * uy + ops->velocity[1] * ux ) / 1000.0f;
	x[PO_OVEL + 2] = ops->velocity[2] / 1000.0f;
	x[PO_GROUND] = ps->groundEntityNum != ENTITYNUM_NONE;
	x[PO_OGROUND] = ops->groundEntityNum != ENTITYNUM_NONE;
	x[PO_FLOOR] = G_FloorHeight( self ) / POLICY_DROP_RANGE;
	x[PO_OFLOOR] = G_FloorHeight( enemy ) / POLICY_DROP_RANGE;

	// the yaw and pitch to him from the eye, as g_movelog.c logs them
	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	VectorSubtract( ops->origin, eye, angles );
	vectoangles( angles, angles );
	a = DEG2RAD( AngleSubtract( angles[YAW], ps->viewangles[YAW] ) );
	x[PO_YAWERR + 0] = sinf( a );
	x[PO_YAWERR + 1] = cosf( a );
	x[PO_PITCH] = AngleNormalize180( ps->viewangles[PITCH] ) / 90.0f;
	x[PO_PITCHERR] = AngleSubtract( angles[PITCH], ps->viewangles[PITCH] ) / 90.0f;
	a = DEG2RAD( AngleSubtract( RAD2DEG( atan2f( -d[1], -d[0] ) ), ops->viewangles[YAW] ) );
	x[PO_OFACING + 0] = sinf( a );
	x[PO_OFACING + 1] = cosf( a );
	x[PO_OPITCH] = AngleNormalize180( ops->viewangles[PITCH] ) / 90.0f;

	x[PO_HEALTH] = Com_Clamp( -1.0f, 2.0f, self->health / 100.0f );
	x[PO_OHEALTH] = Com_Clamp( -1.0f, 2.0f, enemy->health / 100.0f );
	x[PO_FORCE] = ps->fd.forcePower / 100.0f;
	x[PO_WEAPONTIME] = Com_Clamp( 0.0f, 2.0f, ps->weaponTime / 1000.0f );
	x[PO_LEGSTIMER] = Com_Clamp( 0.0f, 2.0f, ps->legsTimer / 1000.0f );
	x[PO_TORSOTIMER] = Com_Clamp( 0.0f, 2.0f, ps->torsoTimer / 1000.0f );
	x[PO_JUMPHELD] = !!( ps->pm_flags & PMF_JUMP_HELD );
	x[PO_OJUMPHELD] = !!( ops->pm_flags & PMF_JUMP_HELD );
	x[PO_KICKFLIP] = ps->forceKickFlip != 0;

	x[PO_HOOKOUT] = cl->hook != NULL;
	x[PO_HOOKED] = cl->hook && cl->pers.jp.hooked;
	if ( cl->hook )
		Policy_Along( x + PO_HOOKAT, cl->hook->r.currentOrigin, ps->origin, ux, uy );
	x[PO_OHOOKOUT] = ocl->hook != NULL;
	x[PO_OHOOKED] = ocl->hook && ocl->pers.jp.hooked;
	if ( ocl->hook )
		Policy_Along( x + PO_OHOOKAT, ocl->hook->r.currentOrigin, ps->origin, ux, uy );

	x[PO_HANDEXT] = ps->forceHandExtend != HANDEXTEND_NONE;
	x[PO_OHANDEXT] = ops->forceHandExtend != HANDEXTEND_NONE;
	x[PO_STYLE] = ps->fd.saberAnimLevel == SS_STRONG;
	x[PO_OSTYLE] = ops->fd.saberAnimLevel == SS_STRONG;
	x[PO_HOLSTERED] = ps->saberHolstered != 0;
	x[PO_OHOLSTERED] = ops->saberHolstered != 0;
	x[PO_OATTACK] = !!( ocl->pers.cmd.buttons & BUTTON_ATTACK );
	x[PO_OHOOKBUTTON] = !!( ocl->pers.cmd.buttons & BUTTON_HOOK );
	x[PO_OMOVES + 0] = ocl->pers.cmd.forwardmove / 127.0f;
	x[PO_OMOVES + 1] = ocl->pers.cmd.rightmove / 127.0f;
	x[PO_OMOVES + 2] = ocl->pers.cmd.upmove / 127.0f;

	Policy_OneHot( x, PO_LEGS, 6, Clone_Class( cloneLegs, CLONE_ANIMS, ps->legsAnim ) );
	Policy_OneHot( x, PO_OLEGS, 6, Clone_Class( cloneLegs, CLONE_ANIMS, ops->legsAnim ) );
	Policy_OneHot( x, PO_OSABER, 6, Clone_Class( cloneSaber, CLONE_SABER_MOVES, ops->saberMove ) );
	Policy_OneHot( x, PO_SABER, 6, Clone_Class( cloneSaber, CLONE_SABER_MOVES, ps->saberMove ) );
	Policy_OneHot( x, PO_PMOVE, 9, pb->prev[0] );
	Policy_OneHot( x, PO_PJUMP, 4, pb->prev[1] );
	Policy_OneHot( x, PO_PHOOK, 4, pb->prev[2] );
	x[PO_PATTACK] = (float)pb->prev[3];
	x[PO_PSABER] = (float)pb->prev[6];
	x[PO_AIM + 0] = pb->yawOff / 180.0f;
	x[PO_AIM + 1] = pb->pitch / 90.0f;

	x[PO_PROBED] = 1.0f;
	VectorSet( toward, ux, uy, 0.0f );
	Policy_Probes( self, toward, x + PO_WALL, x + PO_DROP );
}

// the logits of all heads
static void Policy_Forward( const policyModel_t *m, const float *obs, float *logits ) {
	float	x[POLICY_OBS], h1[POLICY_MAX_HIDDEN], h2[POLICY_MAX_HIDDEN];
	int		i, j;

	for ( i = 0; i < POLICY_OBS; i++ )
		x[i] = ( obs[i] - m->mean[i] ) * m->scale[i];
	for ( i = 0; i < m->hidden[0]; i++ ) {
		const float *w = &m->w1[i * POLICY_OBS];
		float sum = m->b1[i];

		for ( j = 0; j < POLICY_OBS; j++ )
			sum += w[j] * x[j];
		h1[i] = tanhf( sum );
	}
	for ( i = 0; i < m->hidden[1]; i++ ) {
		const float *w = &m->w2[i * m->hidden[0]];
		float sum = m->b2[i];

		for ( j = 0; j < m->hidden[0]; j++ )
			sum += w[j] * h1[j];
		h2[i] = tanhf( sum );
	}
	for ( i = 0; i < POLICY_LOGITS; i++ ) {
		const float *w = &m->w3[i * m->hidden[1]];
		float sum = m->b3[i];

		for ( j = 0; j < m->hidden[1]; j++ )
			sum += w[j] * h2[j];
		logits[i] = sum;
	}
}

// one class of a head drawn by its softmax at temperature temp; adds its log-probability to logp
static int Policy_Draw( const float *logits, int count, float temp, float *logp ) {
	float	p[POLICY_LOGITS], top = logits[0], sum = 0.0f, r;
	int		i;

	for ( i = 1; i < count; i++ )
		top = Q_max( top, logits[i] );
	for ( i = 0; i < count; i++ ) {
		p[i] = expf( ( logits[i] - top ) / temp );
		sum += p[i];
	}
	r = Q_flrand( 0.0f, sum );
	for ( i = 0; i < count - 1; i++ ) {
		r -= p[i];
		if ( r < 0.0f )
			break;
	}
	*logp += logf( Q_max( p[i] / sum, 1e-30f ) );
	return i;
}

static void Policy_Write( policyBot_t *pb, int done ) {
	pb->record[POLICY_OBS * 4 + POLICY_HEADS] = (byte)done;
	memcpy( &pb->record[POLICY_OBS * 4 + POLICY_HEADS + 2 + 4], &pb->reward, 4 );
	trap->FS_Write( pb->record, ROLLOUT_RECORD, pb->f );
	pb->records++;
	pb->pending = qfalse;
	pb->reward = 0.0f;
}

/*
=================
Policy_Decide

the actions of a policy bot for this server frame. A rollout record is: the observation (floats), the action of
each head (bytes), how the episode ended with it (POLICY_DONE_*, 0 not), a byte unused, the log-probability of the
actions and the reward until the next decision (floats). It waits for that reward and is written with the next
decision or the end of the episode.
=================
*/
void Policy_Decide( const gentity_t *self, const gentity_t *enemy, int elapsed, cloneCmd_t *out ) {
	policyBot_t				*pb = &policyBots[self->s.number];
	const policyModel_t		*m = Policy_Model( pb->slot );
	float					obs[POLICY_OBS], logits[POLICY_LOGITS], logp = 0.0f, k;
	const float				temp = Q_max( vs_policyTemp.value, 0.05f );
	int						act[POLICY_HEADS], h, at;

	memset( out, 0, sizeof( *out ) );
	if ( !m )
		return;
	Policy_Observe( self, enemy, pb, obs );
	Policy_Forward( m, obs, logits );
	for ( h = 0, at = 0; h < POLICY_HEADS; at += policyHeadSize[h], h++ )
		act[h] = Policy_Draw( logits + at, policyHeadSize[h], temp, &logp );

	if ( pb->f ) {
		if ( pb->pending )
			Policy_Write( pb, 0 );
		memcpy( pb->record, obs, POLICY_OBS * 4 );
		for ( h = 0; h < POLICY_HEADS; h++ )
			pb->record[POLICY_OBS * 4 + h] = (byte)act[h];
		pb->record[POLICY_OBS * 4 + POLICY_HEADS + 1] = 0;
		memcpy( &pb->record[POLICY_OBS * 4 + POLICY_HEADS + 2], &logp, 4 );
		pb->pending = qtrue;
	}

	// as the clone: move, jump (a press needs a frame without jump before it), hook (use lets go), attack; then the
	// saber key
	out->forwardmove = ( act[0] / 3 - 1 ) * 127;
	out->rightmove = ( act[0] % 3 - 1 ) * 127;
	switch ( act[1] ) {
	case 1:		out->upmove = ( pb->prev[1] == 1 || pb->prev[1] == 2 ) ? 0 : 127;	break;
	case 2:		out->upmove = 127;												break;
	case 3:		out->upmove = -127;												break;
	default:	break;
	}
	if ( act[2] == 1 || act[2] == 2 )
		out->buttons |= BUTTON_HOOK;
	else if ( act[2] == 3 )
		out->buttons |= BUTTON_USE;
	if ( act[3] )
		out->buttons |= BUTTON_ATTACK;
	out->saber = (qboolean)( act[6] == 1 );

	// the aim moves toward the picked bins
	k = 1.0f - expf( -elapsed / ( 1000.0f * POLICY_AIM_SEC ) );
	pb->yawOff = AngleNormalize180( pb->yawOff + AngleSubtract( cloneYaw[act[4]], pb->yawOff ) * k );
	pb->pitch += ( clonePitch[act[5]] - pb->pitch ) * k;
	out->yawOff = pb->yawOff;
	out->pitch = pb->pitch;

	for ( h = 0; h < POLICY_HEADS; h++ )
		pb->prev[h] = act[h];
}

static policyBot_t *Policy_Bot( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !policyBots[clientNum].on )
		return NULL;
	return &policyBots[clientNum];
}

// from G_TrainerDamage: damage dealt (a flip kick lands as MOD_MELEE, g_active.c) and taken
void Policy_Damage( const gentity_t *targ, const gentity_t *attacker, int take, int mod ) {
	policyBot_t *pb;

	Policy_LoadReward();
	if ( attacker && attacker != targ && targ->client && (pb = Policy_Bot( attacker->s.number )) != NULL && pb->f )
		pb->reward += policyReward.damage * take + ( mod == MOD_MELEE ? policyReward.kick : 0.0f );
	if ( (pb = Policy_Bot( targ->s.number )) != NULL && pb->f )
		pb->reward -= policyReward.damage * take;
}

// from the match: kills and deaths of a policy bot
void Policy_Reward( int clientNum, float kills, float deaths ) {
	policyBot_t *pb = Policy_Bot( clientNum );

	Policy_LoadReward();
	if ( pb && pb->f )
		pb->reward += policyReward.kill * kills - policyReward.death * deaths;
}

void Policy_EndEpisode( int clientNum, int done ) {
	policyBot_t *pb = Policy_Bot( clientNum );

	if ( pb && pb->f && pb->pending )
		Policy_Write( pb, done );
}

/*
=================
Policy_MatchStart

with vs_rlRollouts on, a policy bot of slot 0 writes its frames into rollouts/bot<client>.bin: magic, the sizes of
the observation and the heads, the version of the model and the level of the opponent (ints), the map (64 bytes),
then the records of Policy_Decide, then a footer: "END!" and how many records there are
=================
*/
void Policy_MatchStart( int clientNum, int opponentLevel ) {
	policyBot_t				*pb = Policy_Bot( clientNum );
	const policyModel_t		*m;
	char					map[64] = { 0 };
	int						head[4];

	// the cvar itself: vs_rlRollouts set in the same config as vs_trainermatch is not in the vmCvar_t yet
	if ( !pb || pb->slot != 0 || trap->Cvar_VariableIntegerValue( "vs_rlRollouts" ) <= 0 || !(m = Policy_Model( 0 )) )
		return;
	if ( pb->f )
		Policy_MatchEnd( clientNum );
	trap->FS_Open( va( "rollouts/bot%d.bin", clientNum ), &pb->f, FS_WRITE );
	if ( !pb->f ) {
		trap->Print( "vs_rlRollouts: can not write rollouts/bot%d.bin\n", clientNum );
		return;
	}
	head[0] = POLICY_OBS;
	head[1] = POLICY_HEADS;
	head[2] = m->version;
	head[3] = opponentLevel;
	Q_strncpyz( map, level.mapname, sizeof( map ) );
	trap->FS_Write( ROLLOUT_MAGIC, 8, pb->f );
	trap->FS_Write( head, sizeof( head ), pb->f );
	trap->FS_Write( map, sizeof( map ), pb->f );
	pb->records = 0;
	pb->pending = qfalse;
	pb->reward = 0.0f;
}

void Policy_MatchEnd( int clientNum ) {
	policyBot_t *pb = Policy_Bot( clientNum );

	if ( !pb || !pb->f )
		return;
	if ( pb->pending )
		Policy_Write( pb, POLICY_DONE_TIME );
	trap->FS_Write( ROLLOUT_FOOTER, 4, pb->f );
	trap->FS_Write( &pb->records, 4, pb->f );
	trap->FS_Close( pb->f );
	pb->f = 0;
}
