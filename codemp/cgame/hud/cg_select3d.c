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

// cg_select3d.c -- VisionJK (issue #17): the selection of a weapon or an item of the inventory with the new HUD as a
// carousel of frosted glass cards, each with the 3D model of what it is: the cards stand on a ring, the selected one
// in front, biggest, its model turning, its name and ammo under it; the ring turns to a new selection the shortest way
// round. In the order of the stock row (CG_DrawWeaponSelect, CG_DrawInvenSelect), which stays for the HUD of the game
// (v_hud 0). The models are loaded with the level (CG_Select3DPreload). Not from JA+.

#include "cgame/cg_local.h"
#include "ui/ui_shared.h"
#include "ui/ui_skin.h"

#define SELECT_SMALL		40.0f		// a card beside the front one, in units of the 640 x 480 screen
#define SELECT_BIG			70.0f		// the card in front
#define SELECT_MIDDLE_Y		376.0f		// the middle of the row: over the panels of the HUD (ui/vision/hud.menu, y 432)
#define SELECT_RADIUS		7.0f
#define SELECT_RING			150.0f		// half the width of the ring, in units of the screen
#define SELECT_STEP			27.0f		// degrees of the ring between two cards
#define SELECT_REACH		3.4f		// cards shown at most this many places from the front
#define SELECT_SWIPE_MSEC	110.0f		// how fast the ring turns to the selection (time constant)
#define SELECT_FOV			30.0f
#define SELECT_TURN_MSEC	6000.0f		// one turn of the model of the selected card
#define SELECT_IN_MSEC		120.0f
#define SELECT_OUT_MSEC		200.0f

static const vec4_t selectGlass = { 0.035f, 0.07f, 0.12f, 0.78f };
static const vec4_t selectRim = { 0.43f, 0.83f, 1.0f, 0.25f };
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

/*
=================
Select3D_DrawModel

The model in the rect (units of the screen) as a scene of its own, as CG_Draw3DModel does, but always (not only
with cg_draw3DIcons) and framed by its bounds: its middle in front of the camera, as far away as fills the rect.
A white key light and one in the colour of the accent from behind.
=================
*/
static void Select3D_DrawModel( const selectModel_t *m, float x, float y, float w, float h, float yaw, float pitch ) {
	refdef_t	refdef;
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
	VectorSet( angles, pitch, yaw, 0.0f );
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

	trap->R_ClearScene();
	trap->R_AddRefEntityToScene( &ent );
	// a white key light in front and above, the accent from behind its left side
	VectorSet( light, dist - 2.5f * radius, -radius, 1.5f * radius );
	trap->R_AddLightToScene( light, radius * 12.0f, 1.0f, 1.0f, 1.0f );
	VectorSet( light, dist + radius, 1.5f * radius, radius );
	trap->R_AddLightToScene( light, radius * 8.0f, selectAccent[0], selectAccent[1], selectAccent[2] );
	trap->R_RenderScene( &refdef );
}

// the name of an item as the string package of the game has it (SP_INGAME_<CLASSNAME>), as the stock row shows it
static const char *Select3D_Name( const gitem_t *item ) {
	static char	text[256];
	char		key[256];

	Q_strncpyz( key, item->classname, sizeof( key ) );
	if ( trap->SE_GetStringTextString( va( "SP_INGAME_%s", Q_strupr( key ) ), text, sizeof( text ) ) ) {
		return text;
	}
	return item->classname;
}

// ringYaw: how far round the ring the card stands, in degrees (its model turns with it)
static void Select3D_Card( const select3dItem_t *it, float x, float y, float size, qboolean selected, float alpha, float ringYaw ) {
	const selectModel_t	*m = Select3D_Model( it->item );
	const float			inset = size * 0.08f;
	vec4_t				glass, rim;

	memcpy( glass, selectGlass, sizeof( glass ) );
	memcpy( rim, selected ? selectAccent : selectRim, sizeof( rim ) );
	// frosted glass behind it once it is there; the glass thinner over it
	if ( alpha > 0.6f && CG_DrawBackdrop( x, y, size, size, SELECT_RADIUS ) ) {
		glass[3] = 0.45f;
	}
	glass[3] *= alpha;
	rim[3] *= alpha * ( selected ? 0.9f : 1.0f );
	if ( selected ) {
		const vec4_t glow = { selectAccent[0], selectAccent[1], selectAccent[2], 0.3f * alpha };

		Skin_Soft( x, y, size, size, 10.0f, glow );
	}
	Skin_Panel( x, y, size, size, SELECT_RADIUS, glass, rim );

	if ( it->icon && alpha > 0.2f ) {
		// no model of its own (melee, which the item list gives the model of the stun baton): its picture
		const float s = size * 0.62f;

		trap->R_SetColor( NULL );
		CG_DrawPic( x + ( size - s ) * 0.5f, y + ( size - s ) * 0.5f, s, s, it->icon );
	} else if ( m && alpha > 0.2f ) {
		// the selected one turns, the others are shown from the side (the models lie along their y axis, the muzzle
		// to -y: at yaw 0 it points to the right), turned a little towards the viewer
		const float yaw = selected ? 360.0f * fmodf( cg.time / SELECT_TURN_MSEC, 1.0f ) : 20.0f - ringYaw;

		Select3D_DrawModel( m, x + inset, y + inset, size - 2.0f * inset, size - 2.0f * inset, yaw, selected ? 12.0f : 10.0f );
	}
	if ( it->dim ) {
		// nothing to shoot with: the card darkened over its model
		const vec4_t shade = { 0.02f, 0.03f, 0.06f, 0.6f * alpha };

		Skin_Panel( x, y, size, size, SELECT_RADIUS, shade, NULL );
	}
}

/*
=================
CG_Select3D

The carousel of row (SELECT3D_WEAPONS, SELECT3D_ITEMS): count items, selected the index of the one that is
selected, since the time the row was asked for (cg.weaponSelectTime, cg.invenSelectTime). Each card stands on a ring
at its place from the front; where the front is follows the selection (pos of the row), turning the shortest way
round, also when the row comes up again; a list that changed starts at the selection. Drawn from the back to the
front.
=================
*/
void CG_Select3D( int row, const select3dItem_t *items, int count, int selected, int since ) {
	static float	pos[SELECT3D_ROWS];
	static int		lastCount[SELECT3D_ROWS], lastDraw[SELECT3D_ROWS];
	const float		shown = (float)( cg.time - since );
	const float		left = (float)( since + WEAPON_SELECT_TIME - cg.time );
	float			alpha = Com_Clamp( 0.0f, 1.0f, shown / SELECT_IN_MSEC );
	float			offsets[64], delta, rise;
	int				order[64], n = 0, i, j;

	if ( count <= 0 || count > (int)ARRAY_LEN( order ) || selected < 0 || selected >= count || row < 0 || row >= SELECT3D_ROWS ) {
		return;
	}
	if ( left < SELECT_OUT_MSEC ) {
		alpha *= Com_Clamp( 0.0f, 1.0f, left / SELECT_OUT_MSEC );
	}
	if ( alpha <= 0.0f ) {
		return;
	}
	rise = ( 1.0f - alpha ) * 8.0f;

	// where the front of the ring is: it stays where it was while the row is away, so the next switch swipes from the
	// weapon before to the new one; a list that changed starts at the selection
	if ( count != lastCount[row] || cg.time < lastDraw[row] ) {
		pos[row] = (float)selected;
	}
	lastCount[row] = count;
	lastDraw[row] = cg.time;
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
		const float	x = 320.0f + SELECT_RING * sinf( angle ) - size * 0.5f;
		// (the ring seen a little from above: what is farther back stands higher)
		const float	y = SELECT_MIDDLE_Y - size * 0.5f - ( 1.0f - depth ) * 14.0f + rise;
		const float	fade = alpha * Com_Clamp( 0.0f, 1.0f, ( SELECT_REACH - fabsf( o ) ) * 1.2f ) * ( 0.55f + 0.45f * depth );

		// (not a card too faint for its model: that was an empty frame at the ends of the ring)
		if ( depth <= 0.0f || fade <= 0.25f ) {
			continue;
		}
		Select3D_Card( &items[order[i]], x, y, size, (qboolean)( order[i] == selected ), fade, o * SELECT_STEP );
	}

	// the name and the ammo of the selection under the front of the ring
	{
		const char	*name = Select3D_Name( items[selected].item );
		const float	scale = 0.36f;
		vec4_t		text = { 0.92f, 0.95f, 1.0f, alpha };
		float		ty = SELECT_MIDDLE_Y + SELECT_BIG * 0.5f + 4.0f + rise;

		CG_Text_Paint( 320.0f - CG_Text_Width( name, scale, SKIN_FONT_SANS ) * 0.5f, ty, scale, text, name, 0, 0, 0, SKIN_FONT_SANS );
		if ( items[selected].count >= 0 ) {
			const char *ammo = va( "%i", items[selected].count );

			memcpy( text, selectAccent, sizeof( text ) );
			text[3] = alpha;
			ty += 11.0f;
			CG_Text_Paint( 320.0f - CG_Text_Width( ammo, scale, SKIN_FONT_SANS ) * 0.5f, ty, scale, text, ammo, 0, 0, 0, SKIN_FONT_SANS );
		}
	}
	trap->R_SetColor( NULL );
}
