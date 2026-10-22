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
The clone: a bot that plays like the player whose movement logs it was trained on (tools/trainer/train_clone.py).
Once a server frame it looks at itself and the player it fights (the features of trainerlib.FEATURES, the same
numbers in the same order) and asks one small random forest per decision what that player did in such a moment:
how he moved, jumped, hooked, attacked and where he looked. It draws each decision by the shares the forest gives,
not the most likely one, so the rare presses (a kick, a hook) come as often as with the player.

The model is the file of vs_cloneModel (default trainer/clone.dat), read into fixed arrays when the cvar changes.
Its layout is in train_clone.py write_model(), the class tables in clone_tables.h (gen_tables.py).

Not from JA+.
*/

#include "game/g_local.h"
#include "g_clone.h"
#include "clone_tables.h"

#define CLONE_FEATURES		26			// trainerlib.FEATURES
#define CLONE_MAX_CLASSES	16
#define CLONE_MAX_TREES		256
#define CLONE_MAX_NODES		262144
#define CLONE_MAX_VALUES	524288
#define CLONE_MAX_FILE		(8 * 1024 * 1024)
#define CLONE_MAGIC			"VJKCLN01"

typedef struct {
	int		feature;		// -1: a leaf
	float	threshold;		// x <= threshold goes left
	int		left, right;	// node numbers in cloneNodes
	int		value;			// a leaf: its class shares in cloneValues
} cloneNode_t;

typedef struct {
	int		classes;
	int		firstTree, trees;	// in cloneRoots
} cloneHead_t;

static cloneNode_t	cloneNodes[CLONE_MAX_NODES];
static float		cloneValues[CLONE_MAX_VALUES];
static int			cloneRoots[CLONE_MAX_TREES];
static cloneHead_t	cloneHeads[CLONE_HEADS];
static byte			cloneFile[CLONE_MAX_FILE];
static qboolean		cloneLoaded;
static int			cloneModified = -1;

// little endian numbers out of the file, qfalse past its end
static qboolean Clone_Int( const byte **p, const byte *end, int *out ) {
	if ( *p + 4 > end )
		return qfalse;
	*out = (int)( (unsigned)(*p)[0] | ((unsigned)(*p)[1] << 8) | ((unsigned)(*p)[2] << 16) | ((unsigned)(*p)[3] << 24) );
	*p += 4;
	return qtrue;
}

static qboolean Clone_Float( const byte **p, const byte *end, float *out ) {
	int bits;

	if ( !Clone_Int( p, end, &bits ) )
		return qfalse;
	memcpy( out, &bits, sizeof( *out ) );
	return qtrue;
}

static qboolean Clone_Parse( const byte *p, const byte *end ) {
	int features, heads, h, nodes = 0, values = 0, trees = 0;

	if ( end - p < 8 || memcmp( p, CLONE_MAGIC, 8 ) )
		return qfalse;
	p += 8;
	if ( !Clone_Int( &p, end, &features ) || !Clone_Int( &p, end, &heads ) || features != CLONE_FEATURES || heads != CLONE_HEADS )
		return qfalse;

	for ( h = 0; h < CLONE_HEADS; h++ ) {
		cloneHead_t	*head = &cloneHeads[h];
		const int	firstValue = values;
		int			i, n, count;

		if ( !Clone_Int( &p, end, &head->classes ) || !Clone_Int( &p, end, &head->trees )
			|| head->classes < 1 || head->classes > CLONE_MAX_CLASSES || head->trees < 1 || trees + head->trees > CLONE_MAX_TREES )
			return qfalse;
		head->firstTree = trees;
		for ( i = 0; i < head->trees; i++ ) {
			const int base = nodes;

			if ( !Clone_Int( &p, end, &count ) || count < 1 || nodes + count > CLONE_MAX_NODES )
				return qfalse;
			cloneRoots[trees++] = base;
			for ( n = 0; n < count; n++ ) {
				cloneNode_t *node = &cloneNodes[nodes++];

				if ( !Clone_Int( &p, end, &node->feature ) || !Clone_Float( &p, end, &node->threshold )
					|| !Clone_Int( &p, end, &node->left ) || !Clone_Int( &p, end, &node->right ) || !Clone_Int( &p, end, &node->value ) )
					return qfalse;
				if ( node->feature >= CLONE_FEATURES )
					return qfalse;
				if ( node->feature >= 0 ) {
					if ( node->left < 0 || node->left >= count || node->right < 0 || node->right >= count )
						return qfalse;
					node->left += base;
					node->right += base;
				}
				else
					node->value += firstValue;
			}
		}
		if ( !Clone_Int( &p, end, &count ) || count < 0 || values + count > CLONE_MAX_VALUES )
			return qfalse;
		for ( n = 0; n < count; n++ ) {
			if ( !Clone_Float( &p, end, &cloneValues[values++] ) )
				return qfalse;
		}
		// every leaf has its classes in the file
		for ( i = cloneRoots[head->firstTree]; i < nodes; i++ ) {
			if ( cloneNodes[i].feature < 0 && (cloneNodes[i].value < firstValue || cloneNodes[i].value + head->classes > values) )
				return qfalse;
		}
	}
	return (qboolean)( p == end );
}

// the model of vs_cloneModel, read again when the cvar changes
static void Clone_Load( void ) {
	fileHandle_t	f;
	int				len;

	if ( cloneModified == vs_cloneModel.modificationCount )
		return;
	cloneModified = vs_cloneModel.modificationCount;
	cloneLoaded = qfalse;
	if ( !vs_cloneModel.string[0] )
		return;

	len = trap->FS_Open( vs_cloneModel.string, &f, FS_READ );
	if ( !f ) {
		trap->Print( "vs_cloneModel: no file %s\n", vs_cloneModel.string );
		return;
	}
	if ( len <= 0 || len > CLONE_MAX_FILE ) {
		trap->FS_Close( f );
		trap->Print( "vs_cloneModel: %s is empty or larger than %d bytes\n", vs_cloneModel.string, CLONE_MAX_FILE );
		return;
	}
	trap->FS_Read( cloneFile, len, f );
	trap->FS_Close( f );
	cloneLoaded = Clone_Parse( cloneFile, cloneFile + len );
	trap->Print( cloneLoaded ? "vs_cloneModel: %s loaded\n" : "vs_cloneModel: %s is no clone model of this version\n",
		vs_cloneModel.string );
}

qboolean Clone_Ready( void ) {
	Clone_Load();
	return cloneLoaded;
}

static int Clone_Table( const byte *table, int size, int index ) {
	return ( index >= 0 && index < size ) ? table[index] : 0;
}

// trainerlib.features: the same numbers in the same order
static void Clone_Features( const gentity_t *self, const gentity_t *enemy, const int prev[CLONE_HEADS], float *x ) {
	const playerState_t	*ps = &self->client->ps, *ops = &enemy->client->ps;
	vec3_t				d, angles, eye;
	float				flat, ux, uy;

	VectorSubtract( ops->origin, ps->origin, d );
	flat = sqrtf( d[0] * d[0] + d[1] * d[1] );
	if ( flat > 0.001f ) {
		ux = d[0] / flat;
		uy = d[1] / flat;
	}
	else {
		ux = 1.0f;
		uy = 0.0f;
	}

	x[0] = Q_min( flat, 2000.0f );
	x[1] = d[2];
	x[2] = ps->velocity[0] * ux + ps->velocity[1] * uy;
	x[3] = -ps->velocity[0] * uy + ps->velocity[1] * ux;
	x[4] = ps->velocity[2];
	x[5] = ops->velocity[0] * ux + ops->velocity[1] * uy;
	x[6] = -ops->velocity[0] * uy + ops->velocity[1] * ux;
	x[7] = ops->velocity[2];
	x[8] = ps->groundEntityNum != ENTITYNUM_NONE;
	x[9] = ops->groundEntityNum != ENTITYNUM_NONE;
	x[10] = !self->client->hook ? 0.0f : ( self->client->pers.jp.hooked ? 2.0f : 1.0f );
	x[11] = enemy->client->hook != NULL;
	x[12] = fabsf( AngleSubtract( RAD2DEG( atan2f( -d[1], -d[0] ) ), ops->viewangles[YAW] ) );
	x[13] = Clone_Table( cloneSaber, CLONE_SABER_MOVES, ops->saberMove );
	x[14] = Clone_Table( cloneLegs, CLONE_ANIMS, ops->legsAnim );
	x[15] = Clone_Table( cloneLegs, CLONE_ANIMS, ps->legsAnim );
	x[16] = Q_min( self->health, 200 );
	x[17] = Q_min( enemy->health, 200 );
	x[18] = ps->fd.forcePower;
	x[19] = prev[0];
	x[20] = prev[1];
	x[21] = prev[2];
	x[22] = prev[3];
	x[23] = prev[4];
	// the logs have the yaw to him from the eye, as g_movelog.c
	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	VectorSubtract( ops->origin, eye, d );
	vectoangles( d, angles );
	x[24] = AngleSubtract( angles[YAW], ps->viewangles[YAW] );
	x[25] = ps->viewangles[PITCH];
}

// one decision: the class shares of all trees of the head, one class drawn by them
static int Clone_Head( int h, const float *x ) {
	const cloneHead_t	*head = &cloneHeads[h];
	float				share[CLONE_MAX_CLASSES] = { 0 }, sum = 0.0f, r;
	int					i, c;

	for ( i = 0; i < head->trees; i++ ) {
		const cloneNode_t *node = &cloneNodes[cloneRoots[head->firstTree + i]];

		while ( node->feature >= 0 )
			node = &cloneNodes[ x[node->feature] <= node->threshold ? node->left : node->right ];
		for ( c = 0; c < head->classes; c++ )
			share[c] += cloneValues[node->value + c];
	}
	for ( c = 0; c < head->classes; c++ )
		sum += share[c];
	r = Q_flrand( 0.0f, sum );
	for ( c = 0; c < head->classes - 1; c++ ) {
		r -= share[c];
		if ( r < 0.0f )
			break;
	}
	return c;
}

// what the clone does this frame; prev has the decisions of the frame before and gets the new ones
void Clone_Decide( const gentity_t *self, const gentity_t *enemy, int prev[CLONE_HEADS], cloneCmd_t *out ) {
	float	x[CLONE_FEATURES];
	int		act[CLONE_HEADS], h;

	memset( out, 0, sizeof( *out ) );
	if ( !Clone_Ready() )
		return;
	Clone_Features( self, enemy, prev, x );
	for ( h = 0; h < CLONE_HEADS; h++ )
		act[h] = Clone_Head( h, x );

	// move: forward and right of trainerlib.actions, -1 to 1 each
	out->forwardmove = ( act[0] / 3 - 1 ) * 127;
	out->rightmove = ( act[0] % 3 - 1 ) * 127;
	// jump: a press needs a frame without jump before it (PMF_JUMP_HELD)
	switch ( act[1] ) {
	case 1:		out->upmove = ( prev[1] == 1 || prev[1] == 2 ) ? 0 : 127;	break;
	case 2:		out->upmove = 127;											break;
	case 3:		out->upmove = -127;											break;
	default:	break;
	}
	// hook: press and hold the button, use lets go
	if ( act[2] == 1 || act[2] == 2 )
		out->buttons |= BUTTON_HOOK;
	else if ( act[2] == 3 )
		out->buttons |= BUTTON_USE;
	if ( act[3] )
		out->buttons |= BUTTON_ATTACK;
	out->yawOff = cloneYaw[Com_Clampi( 0, ARRAY_LEN( cloneYaw ) - 1, act[4] )];
	out->pitch = clonePitch[Com_Clampi( 0, ARRAY_LEN( clonePitch ) - 1, act[5] )];

	for ( h = 0; h < CLONE_HEADS; h++ )
		prev[h] = act[h];
}
