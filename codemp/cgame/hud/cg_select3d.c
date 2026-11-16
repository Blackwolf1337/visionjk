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

// cg_select3d.c -- VisionJK (issue #17): the selection of a weapon, an item of the inventory or a force power with the
// new HUD as a carousel of frosted glass cards, each with the 3D model of what it is: the cards stand on a ring, the
// selected one in front, biggest, its model turning, its name and ammo under it; the ring turns to a new selection the
// shortest way round. A force power has no model: its picture on a disc that turns, the card in the colour of its
// side, with its rank as pips, what it costs under it, darkened while the force is too low for it and glowing while
// it runs. In the order of the stock row (CG_DrawWeaponSelect, CG_DrawInvenSelect, CG_DrawForceSelect), which stays
// for the HUD of the game (v_hud 0). The models are loaded with the level (CG_Select3DPreload). Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"

#define SELECT_SMALL		28.0f		// a card beside the front one, in units of the 640 x 480 screen
#define SELECT_BIG			50.0f		// the card in front
#define SELECT_MIDDLE_Y		420.0f		// the middle of the row, at the bottom right of the screen (2026-11-16: the HUD is
										// one block at the left, ui/vision/hud.menu), the name and ammo under it
#define SELECT_RADIUS		6.0f
#define SELECT_CENTER_X		( SCREEN_WIDTH - 10.0f - 110.0f - 25.0f )	// the middle of the ring (PLACE_RIGHT): its
										// edge, half its width and half the front card from the right edge
#define SELECT_RING			110.0f		// half the width of the ring, in units of the screen
#define SELECT_STEP			27.0f		// degrees of the ring between two cards
#define SELECT_REACH		3.4f		// cards shown at most this many places from the front
#define SELECT_SWIPE_MSEC	110.0f		// how fast the ring turns to the selection (time constant)
#define SELECT_FOV			30.0f
#define SELECT_TURN_MSEC	6000.0f		// one turn of the model of the selected card
#define SELECT_SLOT_TURN_MSEC	14000.0f	//	and of the one in a slot of the HUD, which is always there
#define SELECT_IN_MSEC		120.0f
#define SELECT_OUT_MSEC		200.0f
#define SELECT_DISC_RADIUS	10.0f		// the disc of a force power, in units of its scene
#define SELECT_DISC_THICK	1.2f
#define SELECT_DISC_SIDES	24
#define SELECT_PULSE_MSEC	1100.0f		// one pulse of the glow of a power that runs

static const vec4_t selectGlass = { SKIN_RGB_GLASS, 0.78f };
static const vec4_t selectRim = { SKIN_RGB_ACCENT, 0.25f };
static const vec4_t selectAccent = { 0.36f, 0.78f, 0.96f, 1.0f };

// the models of the cards: a ghoul2 instance of its own for a .glm (the instances of CG_InitG2Weapons are made to be
// bolted to a player), the handle for the bounds and for an .md3
typedef struct {
	const gitem_t	*item;
	qhandle_t		model;
	void			*ghoul2;
	vec3_t			mins, maxs;
} selectModel_t;

static selectModel_t	selectModels[64];
static float			selectShown;		// how far a row was shown in the frame selectShownTime (CG_Select3DShown)
static int				selectShownTime;
static int				selectNumModels;

static const selectModel_t *Select3D_Model( const gitem_t *item ) {
	selectModel_t	*m;
	const char		*path;
	int				i;

	for ( i = 0; i < selectNumModels; i++ ) {
		if ( selectModels[i].item == item ) {
			return &selectModels[i];
		}
	}
	if ( selectNumModels == (int)ARRAY_LEN( selectModels ) || !item->world_model[0] || !item->world_model[0][0] ) {
		return NULL;
	}
	m = &selectModels[selectNumModels++];
	memset( m, 0, sizeof( *m ) );
	m->item = item;
	path = item->world_model[0];
	m->model = trap->R_RegisterModel( path );
	if ( m->model ) {
		trap->R_ModelBounds( m->model, m->mins, m->maxs );
	}
	if ( Q_stristr( path, ".glm" ) ) {
		trap->G2API_InitGhoul2Model( &m->ghoul2, path, 0, 0, 0, 0, 0 );
	}
	return m;
}

// the models of every weapon and item, when the level loads: a card shows at once, the first time too
void CG_Select3DPreload( void ) {
	const gitem_t *item;

	for ( item = bg_itemlist + 1; item->classname; item++ ) {
		if ( item->giType == IT_WEAPON || item->giType == IT_HOLDABLE ) {
			Select3D_Model( item );
		}
	}
}

void CG_Select3DShutdown( void ) {
	int i;

	for ( i = 0; i < selectNumModels; i++ ) {
		if ( selectModels[i].ghoul2 && trap->G2_HaveWeGhoul2Models( selectModels[i].ghoul2 ) ) {
			trap->G2API_CleanGhoul2Models( &selectModels[i].ghoul2 );
		}
	}
	memset( selectModels, 0, sizeof( selectModels ) );
	selectNumModels = 0;
}

// the scene that was put together, in the rect (pixels): the camera at the origin looking along x
static void Select3D_Render( float x, float y, float w, float h ) {
	refdef_t refdef;

	memset( &refdef, 0, sizeof( refdef ) );
	refdef.rdflags = RDF_NOWORLDMODEL;
	AxisClear( refdef.viewaxis );
	refdef.fov_x = SELECT_FOV;
	refdef.fov_y = SELECT_FOV * h / w;
	refdef.x = (int)x;
	refdef.y = (int)y;
	refdef.width = (int)w;
	refdef.height = (int)h;
	refdef.time = cg.time;
	trap->R_RenderScene( &refdef );
}

/*
=================
Select3D_DrawModel

The model in the rect (units of the screen) as a scene of its own, as CG_Draw3DModel does, but always (not only
with cg_draw3DIcons) and framed by its bounds: its middle in front of the camera, as far away as fills the rect.
A white key light and one in the colour of the accent from behind.
=================
*/
// roll: turned about the line of sight (a hilt that stands upright lies down); alpha: of what it stands on
static void Select3D_DrawModel( const selectModel_t *m, float x, float y, float w, float h, float yaw, float pitch, float roll, float alpha ) {
	refEntity_t	ent;
	vec3_t		angles, mid, light, axis[3];
	float		radius, dist;
	int			i;

	if ( !m->model && !m->ghoul2 ) {
		return;
	}
	CG_AdjustFrom640( &x, &y, &w, &h );
	radius = 0.5f * Distance( m->mins, m->maxs );
	if ( radius < 1.0f ) {
		radius = 12.0f;
	}
	// (the radius is half the diagonal of the bounds: the model fits the rect turned any way)
	dist = radius / tanf( DEG2RAD( SELECT_FOV * 0.5f ) ) * 0.98f;

	memset( &ent, 0, sizeof( ent ) );
	VectorSet( angles, pitch, yaw, roll );
	AnglesToAxis( angles, axis );
	// the middle of the model, turned with it, on the axis of the camera
	VectorAdd( m->mins, m->maxs, mid );
	VectorScale( mid, 0.5f, mid );
	for ( i = 0; i < 3; i++ ) {
		ent.origin[i] = ( i == 0 ? dist : 0.0f ) - ( mid[0] * axis[0][i] + mid[1] * axis[1][i] + mid[2] * axis[2][i] );
	}
	if ( m->ghoul2 ) {
		// A Ghoul II mesh is drawn a quarter turn from the space of its vertices, which the bounds are in (its y
		// axis lies along x when drawn): the entity turned back by that much, so the bounds are where it is drawn.
		VectorNegate( axis[1], ent.axis[0] );
		VectorCopy( axis[0], ent.axis[1] );
		VectorCopy( axis[2], ent.axis[2] );
	} else {
		AxisCopy( axis, ent.axis );
	}
	ent.hModel = m->ghoul2 ? 0 : m->model;
	ent.ghoul2 = m->ghoul2;
	ent.radius = (int)( radius * 2.0f );
	ent.renderfx = RF_NOSHADOW;
	// (a model of a scene of its own can not fade: forced to an alpha it is drawn black. It stays as it is until
	// what it stands on is nearly gone, and goes then.)
	if ( alpha < 0.2f ) {
		return;
	}

	trap->R_ClearScene();
	trap->R_AddRefEntityToScene( &ent );
	// a white key light in front and above, the accent from behind its left side
	VectorSet( light, dist - 2.5f * radius, -radius, 1.5f * radius );
	trap->R_AddLightToScene( light, radius * 12.0f, 1.0f, 1.0f, 1.0f );
	VectorSet( light, dist + radius, 1.5f * radius, radius );
	trap->R_AddLightToScene( light, radius * 8.0f, selectAccent[0], selectAccent[1], selectAccent[2] );
	Select3D_Render( x, y, w, h );
}

// a quad of the scene, wound both ways: seen from either side whichever side the shader culls
static void Select3D_Quad( qhandle_t shader, vec3_t corners[4], const float s[4], const float t[4], const byte *rgba ) {
	polyVert_t	verts[4];
	int			i, pass;

	for ( pass = 0; pass < 2; pass++ ) {
		for ( i = 0; i < 4; i++ ) {
			const int from = pass ? 3 - i : i;

			VectorCopy( corners[from], verts[i].xyz );
			verts[i].st[0] = s[from];
			verts[i].st[1] = t[from];
			memcpy( verts[i].modulate, rgba, sizeof( verts[i].modulate ) );
		}
		trap->R_AddPolysToScene( shader, 4, verts, 1 );
	}
}

/*
=================
Select3D_DrawDisc

What has no model (a force power): its picture on a disc in the rect (units of the screen), a scene of its own as
Select3D_DrawModel, turned by yaw about its upright axis, at 0 facing the viewer. The picture reads the right way
round from behind too (it is mirrored while the back is seen). A ring round it and the edge of the disc in the tint,
so there is something to see while it stands edge on.
=================
*/
static void Select3D_DrawDisc( qhandle_t icon, const float *tint, float alpha, float x, float y, float w, float h, float yaw ) {
	const float		rad = DEG2RAD( yaw ), cy = cosf( rad ), sy = sinf( rad );
	const float		dist = SELECT_DISC_RADIUS / tanf( DEG2RAD( SELECT_FOV * 0.5f ) ) * 1.08f;
	const float		pic = SELECT_DISC_RADIUS * 0.74f, inner = SELECT_DISC_RADIUS * 0.9f;
	const qboolean	facing = (qboolean)( cy >= 0.0f );
	// the camera looks along x, y is to its left, z up: across the disc (to the right of the viewer at yaw 0), and
	// out of its face
	const vec3_t	across = { sy, -cy, 0.0f }, out = { -cy, -sy, 0.0f }, middle = { dist, 0.0f, 0.0f };
	const float		st0[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	vec3_t			corners[4];
	float			s[4], t[4];
	byte			rgba[4];
	int				i, k;

	CG_AdjustFrom640( &x, &y, &w, &h );
	trap->R_ClearScene();

	// the picture
	for ( i = 0; i < 4; i++ ) {
		const float right = ( i == 1 || i == 2 ) ? 1.0f : -1.0f, up = i < 2 ? 1.0f : -1.0f;

		VectorMA( middle, right * pic, across, corners[i] );
		corners[i][2] += up * pic;
		s[i] = ( right > 0.0f ) == facing ? 1.0f : 0.0f;
		t[i] = up > 0.0f ? 0.0f : 1.0f;
	}
	rgba[0] = rgba[1] = rgba[2] = 255;
	rgba[3] = (byte)( 255.0f * alpha );
	Select3D_Quad( icon, corners, s, t, rgba );

	// the ring on its face and its edge
	for ( k = 0; k < 3; k++ ) {
		rgba[k] = (byte)( 255.0f * Com_Clamp( 0.0f, 1.0f, tint[k] ) );
	}
	rgba[3] = (byte)( 220.0f * alpha );
	for ( i = 0; i < SELECT_DISC_SIDES; i++ ) {
		const float a0 = (float)i * 2.0f * M_PI / SELECT_DISC_SIDES, a1 = (float)( i + 1 ) * 2.0f * M_PI / SELECT_DISC_SIDES;
		const float c0 = cosf( a0 ), s0 = sinf( a0 ), c1 = cosf( a1 ), s1 = sinf( a1 );
		vec3_t		rim0, rim1;

		VectorMA( middle, SELECT_DISC_RADIUS * c0, across, rim0 );
		rim0[2] += SELECT_DISC_RADIUS * s0;
		VectorMA( middle, SELECT_DISC_RADIUS * c1, across, rim1 );
		rim1[2] += SELECT_DISC_RADIUS * s1;

		VectorMA( middle, inner * c0, across, corners[0] );
		corners[0][2] += inner * s0;
		VectorCopy( rim0, corners[1] );
		VectorCopy( rim1, corners[2] );
		VectorMA( middle, inner * c1, across, corners[3] );
		corners[3][2] += inner * s1;
		Select3D_Quad( cgs.media.whiteShader, corners, st0, st0, rgba );

		VectorMA( rim0, SELECT_DISC_THICK * 0.5f, out, corners[0] );
		VectorMA( rim0, -SELECT_DISC_THICK * 0.5f, out, corners[1] );
		VectorMA( rim1, -SELECT_DISC_THICK * 0.5f, out, corners[2] );
		VectorMA( rim1, SELECT_DISC_THICK * 0.5f, out, corners[3] );
		Select3D_Quad( cgs.media.whiteShader, corners, st0, st0, rgba );
	}
	Select3D_Render( x, y, w, h );
}

// the name of an item as the string package of the game has it (SP_INGAME_<CLASSNAME>), as the stock row shows it
const char *CG_ItemName( const gitem_t *item ) {
	static char	text[256];
	char		key[256];

	Q_strncpyz( key, item->classname, sizeof( key ) );
	if ( trap->SE_GetStringTextString( va( "SP_INGAME_%s", Q_strupr( key ) ), text, sizeof( text ) ) ) {
		return text;
	}
	return item->classname;
}

/*
A card. front: how much it is the one in front, 0 to 1 as it comes round the ring, so its rim, its glow and the turn
of its model come and go with the ring and do not jump at a switch. alpha: how much it shows (its place on the ring
and the fading of the row), appear: the fading of the row alone. ringYaw: how far round the ring it stands, in
degrees (its model turns with it). turn: the turn of the model in front, in degrees.
*/
static void Select3D_Card( const select3dItem_t *it, float x, float y, float size, float front, float alpha, float appear, float ringYaw,
		float turn ) {
	const selectModel_t	*m = it->item ? Select3D_Model( it->item ) : NULL;
	const float			*accent = it->tint ? it->tint : selectAccent;
	const float			inset = size * 0.08f;
	vec4_t				glass, rim;
	int					i;

	memcpy( glass, selectGlass, sizeof( glass ) );
	for ( i = 0; i < 3; i++ ) {
		// (a card with a tint has it in its rim beside the front too: the side of a power is seen along the row)
		const float base = it->tint ? it->tint[i] : selectRim[i];

		rim[i] = base + ( accent[i] - base ) * front;
	}
	rim[3] = ( selectRim[3] + ( 0.9f - selectRim[3] ) * front ) * alpha;
	// frosted glass behind it once the row is there (not by its place on the ring: the glass can not fade, it
	// would pop in and out at the ends while the ring turns); the glass thinner over it
	if ( appear > 0.6f && CG_DrawBackdrop( x, y, size, size, SELECT_RADIUS ) ) {
		glass[3] = 0.45f;
	}
	glass[3] *= alpha;
	if ( front > 0.01f ) {
		const vec4_t glow = { accent[0], accent[1], accent[2], 0.3f * alpha * front };

		Skin_Soft( x, y, size, size, 8.0f, glow );
	}
	if ( it->active ) {
		// running right now: a glow of its own that pulses, in front and beside it
		const float		pulse = 0.5f + 0.5f * sinf( (float)cg.time * 2.0f * M_PI / SELECT_PULSE_MSEC );
		const vec4_t	glow = { accent[0], accent[1], accent[2], ( 0.3f + 0.3f * pulse ) * alpha };

		Skin_Soft( x, y, size, size, 6.0f, glow );
		rim[3] = Com_Clamp( 0.0f, 1.0f, rim[3] + ( 0.3f + 0.3f * pulse ) * alpha );
	}
	Skin_Panel( x, y, size, size, SELECT_RADIUS, glass, rim );

	if ( it->icon && it->disc && alpha > 0.2f ) {
		// the one in front turns, the others face the viewer, turned a little with the ring; smaller than a model
		// and a little higher, the pips have their place under it
		const float d = size * 0.13f;

		Select3D_DrawDisc( it->icon, accent, alpha, x + d, y + d - size * 0.04f, size - 2.0f * d, size - 2.0f * d,
			-0.6f * ringYaw + front * turn );
	} else if ( it->icon && alpha > 0.2f ) {
		// no model of its own (melee, which the item list gives the model of the stun baton): its picture
		const float s = size * 0.62f;

		trap->R_SetColor( NULL );
		CG_DrawPic( x + ( size - s ) * 0.5f, y + ( size - s ) * 0.5f, s, s, it->icon );
	} else if ( m && alpha > 0.2f ) {
		// the selected one turns, the others are shown from the side (the models lie along their y axis, the muzzle
		// to -y: at yaw 0 it points to the right), turned a little towards the viewer
		const float yaw = 20.0f - ringYaw + front * turn;

		Select3D_DrawModel( m, x + inset, y + inset, size - 2.0f * inset, size - 2.0f * inset, yaw, 10.0f + 2.0f * front, 0.0f, 1.0f );
	}
	if ( it->dim ) {
		// nothing to shoot with: the card darkened over its model
		const vec4_t shade = { 0.02f, 0.03f, 0.06f, 0.6f * alpha };

		Skin_Panel( x, y, size, size, SELECT_RADIUS, shade, NULL );
	}
	if ( it->pipsMax > 0 ) {
		// pips along the bottom, over the shade: the ones reached in the accent, the rest faint
		const float		pip = size * 0.07f, gap = pip * 0.7f;
		const float		px = x + ( size - ( it->pipsMax * pip + ( it->pipsMax - 1 ) * gap ) ) * 0.5f;
		const float		py = y + size - pip - size * 0.05f;

		for ( i = 0; i < it->pipsMax; i++ ) {
			const vec4_t fill = { accent[0], accent[1], accent[2], ( i < it->pips ? 0.95f : 0.2f ) * alpha };

			Skin_Panel( px + i * ( pip + gap ), py, pip, pip, pip * 0.5f, fill, NULL );
		}
	}
}

/*
=================
CG_Select3DHead

The head of a player in the rect (units of the screen), as he is in the level right now: his model in the pose it
has, in a scene of its own, the camera before his face. For the card of a duel (hud/cg_hudtop.c). qfalse: he is
not in sight or has no model yet, the caller draws his picture.
=================
*/
qboolean CG_Select3DHead( float x, float y, float w, float h, int client ) {
	const float			yawSeen = 155.0f;		// he looks at the camera, turned a little
	centity_t			*cent;
	const clientInfo_t	*ci;
	refEntity_t			ent;
	mdxaBone_t			matrix;
	vec3_t				angles, head, light;
	float				scale, dist;

	if ( client < 0 || client >= MAX_CLIENTS ) {
		return qfalse;
	}
	cent = &cg_entities[client];
	ci = &cgs.clientinfo[client];
	if ( !cent->currentValid || !cent->ghoul2 || !ci->infoValid || ci->bolt_head < 0 || ( cent->currentState.eFlags & EF_NODRAW ) ) {
		return qfalse;
	}
	CG_AdjustFrom640( &x, &y, &w, &h );
	// where the top of his head is with the model at the origin, turned as it is shown
	VectorSet( angles, 0.0f, yawSeen, 0.0f );
	trap->G2API_GetBoltMatrix( cent->ghoul2, 0, ci->bolt_head, &matrix, angles, vec3_origin, cg.time, cgs.gameModels, cent->modelScale );
	BG_GiveMeVectorFromMatrix( &matrix, ORIGIN, head );
	scale = cent->modelScale[2] > 0.0f ? cent->modelScale[2] : 1.0f;
	head[2] -= 5.0f * scale;		// the face, not the crown
	// (a head is about 7 units from its middle to its edge, with a little room)
	dist = 8.5f * scale / tanf( DEG2RAD( SELECT_FOV * 0.5f ) );

	memset( &ent, 0, sizeof( ent ) );
	AnglesToAxis( angles, ent.axis );
	VectorSet( ent.origin, dist - head[0], -head[1], -head[2] );
	VectorCopy( ent.origin, ent.lightingOrigin );
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( cent->modelScale, ent.modelScale );
	ent.ghoul2 = cent->ghoul2;
	ent.radius = 64;
	ent.renderfx = RF_NOSHADOW | RF_LIGHTING_ORIGIN;
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;

	trap->R_ClearScene();
	trap->R_AddRefEntityToScene( &ent );
	// a white key light in front and above, the accent from behind his left side
	VectorSet( light, dist - 30.0f * scale, -12.0f * scale, 16.0f * scale );
	trap->R_AddLightToScene( light, 160.0f * scale, 1.0f, 1.0f, 1.0f );
	VectorSet( light, dist + 14.0f * scale, 18.0f * scale, 10.0f * scale );
	trap->R_AddLightToScene( light, 110.0f * scale, selectAccent[0], selectAccent[1], selectAccent[2] );
	Select3D_Render( x, y, w, h );
	return qtrue;
}

// has the item a model the cards can show
qboolean CG_Select3DModelFor( const gitem_t *item ) {
	const selectModel_t *m = item ? Select3D_Model( item ) : NULL;

	return (qboolean)( m && ( m->model || m->ghoul2 ) );
}

/*
=================
CG_Select3DIcon

One thing in the rect as the cards of the rows show it, for a slot of the HUD (hud/cg_hudpanels.c): the model of
an item, turning slowly, or a picture as the holo disc in the tint, swinging. qfalse: there is no model, the caller
draws the picture. still: it does not move (a line of the feed), a model seen from the side. alpha: it fades.
=================
*/
qboolean CG_Select3DIcon( float x, float y, float w, float h, const gitem_t *item, qhandle_t disc, const float *tint, qboolean still, float alpha ) {
	const float yaw = still ? 0.0f : fmodf( (float)cg.time * 360.0f / SELECT_SLOT_TURN_MSEC, 360.0f ) - 180.0f;

	if ( disc ) {
		// (it swings and never stands edge on: what the slot holds has to be read at any time)
		Select3D_DrawDisc( disc, tint ? tint : selectAccent, alpha, x, y, w, h, 38.0f * sinf( DEG2RAD( yaw ) ) );
		return qtrue;
	}
	if ( item ) {
		const selectModel_t *m = Select3D_Model( item );

		if ( m && ( m->model || m->ghoul2 ) ) {
			// (standing still the saber lies across the rect: its hilt stands upright and would be a thin line)
			const float roll = ( still && item->giType == IT_WEAPON && item->giTag == WP_SABER ) ? 55.0f : 0.0f;

			Select3D_DrawModel( m, x, y, w, h, ( still ? 0.0f : 20.0f ) + yaw, still ? 6.0f : 12.0f, roll, alpha );
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
CG_Select3D

The carousel of row (SELECT3D_WEAPONS, SELECT3D_ITEMS, SELECT3D_FORCE): count items, selected the index of the one that is
selected, since the time the row was asked for (cg.weaponSelectTime, cg.invenSelectTime). Each card stands on a ring
at its place from the front; where the front is follows the selection (pos of the row), turning the shortest way
round, also when the row comes up again; a list that changed starts at the selection. Drawn from the back to the
front.
=================
*/
void CG_Select3D( int row, const select3dItem_t *items, int count, int selected, int since ) {
	static float	pos[SELECT3D_ROWS], turn[SELECT3D_ROWS];
	static int		lastCount[SELECT3D_ROWS], lastDraw[SELECT3D_ROWS], visibleSince[SELECT3D_ROWS];
	const float		left = (float)( since + WEAPON_SELECT_TIME - cg.time );
	float			alpha, offsets[64], delta, rise, appear;
	int				order[64], n = 0, i, j;

	if ( count <= 0 || count > (int)ARRAY_LEN( order ) || selected < 0 || selected >= count || row < 0 || row >= SELECT3D_ROWS ) {
		return;
	}
	// The row fades in when it comes up, not at every switch while it is there (each switch sets since again: the
	// whole row flashed in anew, which looked as if it blinked)
	if ( cg.time - lastDraw[row] > 100 || cg.time < lastDraw[row] ) {
		visibleSince[row] = cg.time;
	}
	// (before anything returns: on its first frame the row is not seen yet, and it would come up anew every frame)
	if ( count != lastCount[row] || cg.time < lastDraw[row] ) {
		pos[row] = (float)selected;
	}
	lastCount[row] = count;
	lastDraw[row] = cg.time;
	appear = Com_Clamp( 0.0f, 1.0f, (float)( cg.time - visibleSince[row] ) / SELECT_IN_MSEC );
	alpha = appear;
	if ( left < SELECT_OUT_MSEC ) {
		alpha *= Com_Clamp( 0.0f, 1.0f, left / SELECT_OUT_MSEC );
	}
	if ( alpha <= 0.0f ) {
		return;
	}
	rise = ( 1.0f - appear ) * 8.0f;
	selectShown = Q_max( selectShownTime == cg.time ? selectShown : 0.0f, alpha );
	selectShownTime = cg.time;
	CG_Place2D( PLACE_RIGHT );
	// the turn of the model in front goes on while the row is there, between -180 and 180 degrees: a card that leaves
	// the front turns back to the side by the shorter way
	turn[row] = fmodf( turn[row] + 360.0f * (float)cg.frametime / SELECT_TURN_MSEC + 180.0f, 360.0f ) - 180.0f;

	// where the front of the ring is: it stays where it was while the row is away, so the next switch swipes from the
	// weapon before to the new one; a list that changed starts at the selection (above)
	delta = (float)selected - pos[row];
	while ( delta > count * 0.5f ) {
		delta -= count;
	}
	while ( delta < -count * 0.5f ) {
		delta += count;
	}
	pos[row] += delta * ( 1.0f - expf( -(float)cg.frametime / SELECT_SWIPE_MSEC ) );
	if ( fabsf( delta ) < 0.002f ) {
		pos[row] = (float)selected;
	}
	pos[row] = fmodf( pos[row] + count, (float)count );

	// the place of each card from the front, the ones far round left out, the farthest first
	for ( i = 0; i < count; i++ ) {
		float o = (float)i - pos[row];

		while ( o > count * 0.5f ) {
			o -= count;
		}
		while ( o < -count * 0.5f ) {
			o += count;
		}
		if ( fabsf( o ) > SELECT_REACH ) {
			continue;
		}
		offsets[n] = o;
		order[n++] = i;
	}
	for ( i = 1; i < n; i++ ) {
		for ( j = i; j > 0 && fabsf( offsets[j - 1] ) < fabsf( offsets[j] ); j-- ) {
			const float	o = offsets[j];
			const int	k = order[j];

			offsets[j] = offsets[j - 1];
			order[j] = order[j - 1];
			offsets[j - 1] = o;
			order[j - 1] = k;
		}
	}

	for ( i = 0; i < n; i++ ) {
		const float	o = offsets[i];
		const float	angle = DEG2RAD( o * SELECT_STEP );
		const float	depth = cosf( angle );		// 1 in front, less round the ring
		const float	front = Com_Clamp( 0.0f, 1.0f, 1.0f - fabsf( o ) );
		const float	size = ( SELECT_SMALL + ( SELECT_BIG - SELECT_SMALL ) * front ) * ( 0.8f + 0.2f * depth );
		const float	x = SELECT_CENTER_X + SELECT_RING * sinf( angle ) - size * 0.5f;
		// (all on one line: the ring is seen from its side, nothing stands higher, their word 2026-11-14)
		const float	y = SELECT_MIDDLE_Y - size * 0.5f + rise;
		const float	fade = alpha * Com_Clamp( 0.0f, 1.0f, ( SELECT_REACH - fabsf( o ) ) * 1.2f ) * ( 0.55f + 0.45f * depth );

		// (not a card too faint for its model: that was an empty frame at the ends of the ring)
		if ( depth <= 0.0f || fade <= 0.25f ) {
			continue;
		}
		Select3D_Card( &items[order[i]], x, y, size, front, fade, appear, o * SELECT_STEP, turn[row] );
	}

	// the name and the ammo of the selection under the front of the ring
	{
		const char	*name = items[selected].name ? items[selected].name
			: items[selected].item ? CG_ItemName( items[selected].item ) : "";
		const float	scale = 0.32f;
		vec4_t		text = { 0.92f, 0.95f, 1.0f, alpha };
		float		ty = SELECT_MIDDLE_Y + SELECT_BIG * 0.5f + 4.0f + rise;
		// (under the front card, as far left as a long name needs to end before the edge)
		const float	nx = Q_min( SELECT_CENTER_X - CG_Text_Width( name, scale, SKIN_FONT_SANS ) * 0.5f,
			SCREEN_WIDTH - 10.0f - CG_Text_Width( name, scale, SKIN_FONT_SANS ) );

		CG_Text_Paint( nx, ty, scale, text, name, 0, 0, 0, SKIN_FONT_SANS );
		if ( items[selected].count >= 0 ) {
			const char *ammo = va( "%i", items[selected].count );

			VectorCopy( items[selected].tint ? items[selected].tint : selectAccent, text );
			text[3] = alpha;
			ty += 10.0f;
			CG_Text_Paint( SELECT_CENTER_X - CG_Text_Width( ammo, scale, SKIN_FONT_SANS ) * 0.5f, ty, scale, text, ammo, 0, 0, 0, SKIN_FONT_SANS );
		}
	}
	trap->R_SetColor( NULL );
	CG_Place2D( PLACE_CENTER );
}

// how far a selection row is shown right now, 0 to 1: the corner of the HUD makes room for it (hud/cg_hudpanels.c)
float CG_Select3DShown( void ) {
	return ( selectShownTime == cg.time || selectShownTime == cg.time - cg.frametime ) ? selectShown : 0.0f;
}
