/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026, VisionJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

//
// string allocation/managment

#ifdef _CGAME
	#include "cgame/cg_local.h"
#elif UI_BUILD
	#include "ui/ui_local.h"
#endif

#include "ui_shared.h"
#include "ui_skin.h"
#include "game/bg/bg_public.h"
#include "game/anims.h"
#include "ghoul2/G2.h"
extern stringID_table_t animTable [MAX_ANIMATIONS+1];
extern void UI_UpdateCharacterSkin( void );

const char *HolocronIcons[NUM_FORCE_POWERS] = {
	"gfx/mp/f_icon_lt_heal",		//FP_HEAL,
	"gfx/mp/f_icon_levitation",		//FP_LEVITATION,
	"gfx/mp/f_icon_speed",			//FP_SPEED,
	"gfx/mp/f_icon_push",			//FP_PUSH,
	"gfx/mp/f_icon_pull",			//FP_PULL,
	"gfx/mp/f_icon_lt_telepathy",	//FP_TELEPATHY,
	"gfx/mp/f_icon_dk_grip",		//FP_GRIP,
	"gfx/mp/f_icon_dk_l1",			//FP_LIGHTNING,
	"gfx/mp/f_icon_dk_rage",		//FP_RAGE,
	"gfx/mp/f_icon_lt_protect",		//FP_PROTECT,
	"gfx/mp/f_icon_lt_absorb",		//FP_ABSORB,
	"gfx/mp/f_icon_lt_healother",	//FP_TEAM_HEAL,
	"gfx/mp/f_icon_dk_forceother",	//FP_TEAM_FORCE,
	"gfx/mp/f_icon_dk_drain",		//FP_DRAIN,
	"gfx/mp/f_icon_sight",			//FP_SEE,
	"gfx/mp/f_icon_saber_attack",	//FP_SABER_OFFENSE,
	"gfx/mp/f_icon_saber_defend",	//FP_SABER_DEFENSE,
	"gfx/mp/f_icon_saber_throw"		//FP_SABERTHROW
};

#define SCROLL_TIME_START					500
#define SCROLL_TIME_ADJUST					150
#define SCROLL_TIME_ADJUSTOFFSET			40
#define SCROLL_TIME_FLOOR					20

typedef struct scrollInfo_s {
	int nextScrollTime;
	int nextAdjustTime;
	int adjustValue;
	int scrollKey;
	float xStart;
	float yStart;
	itemDef_t *item;
	qboolean scrollDir;
} scrollInfo_t;

#ifdef UI_BUILD // Defined in ui_main.c, not in the namespace
	// Some extern functions hoisted from the middle of this file to get all the non-cgame,
	// non-namespace stuff together
	extern void UI_SaberDrawBlades( itemDef_t *item, vec3_t origin, vec3_t angles );
	extern qboolean UI_SaberFrame( itemDef_t *item, vec3_t middle, float *length );

	extern void UI_SaberLoadParms( void );
	extern qboolean ui_saber_parms_parsed;
	extern void UI_CacheSaberGlowGraphics( void );
#endif //

qboolean Item_SetFocus(itemDef_t *item, float x, float y);

static scrollInfo_t scrollInfo;

static void (*captureFunc) (void *p) = NULL;
static void *captureData = NULL;
static itemDef_t *itemCapture = NULL;   // item that has the mouse captured ( if any )

displayContextDef_t *DC = NULL;

static qboolean g_waitingForKey = qfalse;
static qboolean g_editingField = qfalse;

static itemDef_t *g_bindItem = NULL;
static int g_bindSlot = 0;		// the key of the two of a row in a menu with a canvas that the next key pressed replaces
static itemDef_t *g_editItem = NULL;

menuDef_t Menus[MAX_MENUS];      // defined menus
int menuCount = 0;               // how many

menuDef_t *menuStack[MAX_OPEN_MENUS];
int openMenuCount = 0;

static qboolean debugMode = qfalse;

#define DOUBLE_CLICK_DELAY 300
static int lastListBoxClickTime = 0;

void Item_RunScript(itemDef_t *item, const char *s);
void Item_SetupKeywordHash(void);
void Menu_SetupKeywordHash(void);
int BindingIDFromName(const char *name);
qboolean Item_Bind_HandleKey(itemDef_t *item, int key, qboolean down);
itemDef_t *Menu_SetPrevCursorItem(menuDef_t *menu);
itemDef_t *Menu_SetNextCursorItem(menuDef_t *menu);
static qboolean Menu_OverActiveItem(menuDef_t *menu, float x, float y);
static void Item_TextScroll_BuildLines ( itemDef_t* item );
void Menu_SetItemText(const menuDef_t *menu,const char *itemName, const char *text);
extern qboolean ItemParse_asset_model_go( itemDef_t *item, const char *name,int *runTimeLength );
extern qboolean ItemParse_model_g2anim_go( itemDef_t *item, const char *animName );


#ifdef _CGAME
#define MEM_POOL_SIZE  (128 * 1024)
#else
#define MEM_POOL_SIZE  (4 * 1024 * 1024)
#endif

static char memoryPool[MEM_POOL_SIZE];
static int allocPoint;
static qboolean outOfMemory;

typedef struct  itemFlagsDef_s {
	const char *string;
	int value;
} itemFlagsDef_t;

itemFlagsDef_t itemFlags[] = {
	{ "WINDOW_INACTIVE",		WINDOW_INACTIVE },
	{ NULL,					0 }
};

extern int MenuFontToHandle(int iMenuFont);

/*
===============
UI_Alloc
===============
*/
void *UI_Alloc( int size ) {
	char	*p;

	if ( allocPoint + size > MEM_POOL_SIZE ) {
		outOfMemory = qtrue;
		if (DC->Print) {
			DC->Print("UI_Alloc: Failure. Out of memory!\n");
		}
		return NULL;
	}

	p = &memoryPool[allocPoint];

	allocPoint += ( size + 15 ) & ~15;

	return p;
}

/*
===============
UI_InitMemory
===============
*/
void UI_InitMemory( void ) {
	allocPoint = 0;
	outOfMemory = qfalse;
}

qboolean UI_OutOfMemory( void ) {
	return outOfMemory;
}

#define HASH_TABLE_SIZE 2048
/*
================
return a hash value for the string
================
*/
static unsigned hashForString(const char *str) {
	int		i;
	unsigned	hash;
	char	letter;

	hash = 0;
	i = 0;
	while (str[i] != '\0') {
		letter = tolower((unsigned char)str[i]);
		hash+=(unsigned)(letter)*(i+119);
		i++;
	}
	hash &= (HASH_TABLE_SIZE-1);
	return hash;
}

typedef struct stringDef_s {
	struct stringDef_s *next;
	const char *str;
} stringDef_t;

static int strPoolIndex = 0;
static char strPool[STRING_POOL_SIZE];

static int strHandleCount = 0;
static stringDef_t *strHandle[HASH_TABLE_SIZE];


const char *String_Alloc(const char *p) {
	int len;
	unsigned hash;
	stringDef_t *str, *last;
	static const char *staticNULL = "";

	if (p == NULL) {
		return NULL;
	}

	if (*p == 0) {
		return staticNULL;
	}

	hash = hashForString(p);

	str = strHandle[hash];
	while (str) {
		if (strcmp(p, str->str) == 0) {
			return str->str;
		}
		str = str->next;
	}

	len = strlen(p);
	if (len + strPoolIndex + 1 < STRING_POOL_SIZE) {
		int ph = strPoolIndex;
		strcpy(&strPool[strPoolIndex], p);
		strPoolIndex += len + 1;

		str = strHandle[hash];
		last = str;
		while (last && last->next)
		{
			last = last->next;
		}

		str  = (stringDef_t *) UI_Alloc(sizeof(stringDef_t));
		str->next = NULL;
		str->str = &strPool[ph];
		if (last) {
			last->next = str;
		} else {
			strHandle[hash] = str;
		}
		return &strPool[ph];
	}

	//Increase STRING_POOL_SIZE.
	Com_Printf( S_COLOR_RED, "String pool has been exhausted.\n" );
	return NULL;
}

void String_Report() {
	float f;
	Com_Printf("Memory/String Pool Info\n");
	Com_Printf("----------------\n");
	f = strPoolIndex;
	f /= STRING_POOL_SIZE;
	f *= 100;
	Com_Printf("String Pool is %.1f%% full, %i bytes out of %i used.\n", f, strPoolIndex, STRING_POOL_SIZE);
	f = allocPoint;
	f /= MEM_POOL_SIZE;
	f *= 100;
	Com_Printf("Memory Pool is %.1f%% full, %i bytes out of %i used.\n", f, allocPoint, MEM_POOL_SIZE);
}

/*
=================
String_Init
=================
*/
void String_Init() {
	int i;
	for (i = 0; i < HASH_TABLE_SIZE; i++) {
		strHandle[i] = 0;
	}
	strHandleCount = 0;
	strPoolIndex = 0;
	menuCount = 0;
	openMenuCount = 0;
	UI_InitMemory();
	Item_SetupKeywordHash();
	Menu_SetupKeywordHash();
	if (DC && DC->getBindingBuf) {
		Controls_GetConfig();
	}
}

#if 0
/*
=================
PC_SourceWarning
=================
*/
void PC_SourceWarning(int handle, char *format, ...) {
	int line;
	char filename[128];
	va_list argptr;
	static char string[4096];

	va_start (argptr, format);
	Q_vsnprintf (string, sizeof( string ), format, argptr);
	va_end (argptr);

	filename[0] = '\0';
	line = 0;
	trap->PC_SourceFileAndLine(handle, filename, &line);

	Com_Printf(S_COLOR_YELLOW "WARNING: %s, line %d: %s\n", filename, line, string);
}
#endif

/*
=================
PC_SourceError
=================
*/
void PC_SourceError(int handle, char *format, ...) {
	int line;
	char filename[128];
	va_list argptr;
	static char string[4096];

	va_start (argptr, format);
	Q_vsnprintf (string, sizeof( string ), format, argptr);
	va_end (argptr);

	filename[0] = '\0';
	line = 0;
	trap->PC_SourceFileAndLine(handle, filename, &line);

	Com_Printf(S_COLOR_RED "ERROR: %s, line %d: %s\n", filename, line, string);
}

/*
=================
LerpColor
=================
*/
void LerpColor(vec4_t a, vec4_t b, vec4_t c, float t)
{
	int i;

	// lerp and clamp each component
	for (i=0; i<4; i++)
	{
		c[i] = a[i] + t*(b[i]-a[i]);
		if (c[i] < 0)
			c[i] = 0;
		else if (c[i] > 1.0)
			c[i] = 1.0;
	}
}

/*
=================
Float_Parse
=================
*/
qboolean Float_Parse(char **p, float *f) {
	char	*token;
	token = COM_ParseExt((const char **)p, qfalse);
	if (token && token[0] != 0) {
		*f = atof(token);
		return qtrue;
	} else {
		return qfalse;
	}
}

/*
=================
PC_Float_Parse
=================
*/
qboolean PC_Float_Parse(int handle, float *f) {
	pc_token_t token;
	int negative = qfalse;

	if ( !trap->PC_ReadToken( handle, &token ) )
		return qfalse;

	if ( token.string[0] == '-' ) {
		if ( !trap->PC_ReadToken( handle, &token ) )
			return qfalse;
		negative = qtrue;
	}
	if (token.type != TT_NUMBER) {
		PC_SourceError(handle, "expected float but found %s", token.string);
		return qfalse;
	}
	if (negative)
		*f = -token.floatvalue;
	else
		*f = token.floatvalue;
	return qtrue;
}

/*
=================
Color_Parse
=================
*/
qboolean Color_Parse(char **p, vec4_t *c) {
	int i;
	float f;

	for (i = 0; i < 4; i++) {
		if (!Float_Parse(p, &f)) {
			return qfalse;
		}
		(*c)[i] = f;
	}
	return qtrue;
}

/*
=================
PC_Color_Parse
=================
*/
qboolean PC_Color_Parse(int handle, vec4_t *c) {
	int i;
	float f;

	for (i = 0; i < 4; i++) {
		if (!PC_Float_Parse(handle, &f)) {
			return qfalse;
		}
		(*c)[i] = f;
	}
	return qtrue;
}

/*
=================
Int_Parse
=================
*/
qboolean Int_Parse(char **p, int *i) {
	char	*token;
	token = COM_ParseExt((const char **)p, qfalse);

	if (token && token[0] != 0) {
		*i = atoi(token);
		return qtrue;
	} else {
		return qfalse;
	}
}

/*
=================
PC_Int_Parse
=================
*/
qboolean PC_Int_Parse(int handle, int *i) {
	pc_token_t token;
	int negative = qfalse;

	if (!trap->PC_ReadToken(handle, &token))
		return qfalse;
	if (token.string[0] == '-') {
		if (!trap->PC_ReadToken(handle, &token))
			return qfalse;
		negative = qtrue;
	}
	if (token.type != TT_NUMBER) {
		PC_SourceError(handle, "expected integer but found %s", token.string);
		return qfalse;
	}
	*i = token.intvalue;
	if (negative)
		*i = - *i;
	return qtrue;
}

/*
=================
Rect_Parse
=================
*/
qboolean Rect_Parse(char **p, rectDef_t *r) {
	if (Float_Parse(p, &r->x)) {
		if (Float_Parse(p, &r->y)) {
			if (Float_Parse(p, &r->w)) {
				if (Float_Parse(p, &r->h)) {
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

/*
=================
PC_Rect_Parse
=================
*/
qboolean PC_Rect_Parse(int handle, rectDef_t *r) {
	if (PC_Float_Parse(handle, &r->x)) {
		if (PC_Float_Parse(handle, &r->y)) {
			if (PC_Float_Parse(handle, &r->w)) {
				if (PC_Float_Parse(handle, &r->h)) {
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

/*
=================
String_Parse
=================
*/
qboolean String_Parse(char **p, const char **out) {
	char *token;

	token = COM_ParseExt((const char **)p, qfalse);
	if (token && token[0] != 0) {
		*(out) = String_Alloc(token);
		return *(out)!=NULL;
	}
	return qfalse;
}

/*
=================
PC_String_Parse
=================
*/
qboolean PC_String_Parse(int handle, const char **out)
{
	static char*	squiggy = "}";
	pc_token_t		token;

	if (!trap->PC_ReadToken(handle, &token))
	{
		return qfalse;
	}

	// Save some memory by not return the end squiggy as an allocated string
	if ( !Q_stricmp ( token.string, "}" ) )
	{
		*(out) = squiggy;
	}
	else
	{
		*(out) = String_Alloc(token.string);
	}
	return qtrue;
}

/*
=================
PC_Script_Parse
=================
*/
// The longest script of a menu, as it is kept: every word of it in quotes. The game had 2048, and what ran over
// was dropped without a word. The script that opens its siege menu fills 2033 of them, so the same script with the
// colours of the new look (more digits) lost its end: the commands that show the team and the class of somebody
// who is on a team already. tools/menus/build.py refuses a script that does not fit.
#define MAX_SCRIPT_CHARS		4096

qboolean PC_Script_Parse(int handle, const char **out) {
	char script[MAX_SCRIPT_CHARS];
	pc_token_t token;
	qboolean cut = qfalse;

	script[0] = 0;
	// scripts start with { and have ; separated command lists.. commands are command, arg..
	// basically we want everything between the { } as it will be interpreted at run time

	if (!trap->PC_ReadToken(handle, &token))
		return qfalse;
	if (Q_stricmp(token.string, "{") != 0) {
	    return qfalse;
	}

	while ( 1 ) {
		if (!trap->PC_ReadToken(handle, &token))
			return qfalse;

		if (Q_stricmp(token.string, "}") == 0) {
			if ( cut ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: a menu script is longer than %i characters, its end is lost\n", (int)sizeof( script ) - 1 );
			}
			*out = String_Alloc(script);
			return qtrue;
		}

		// Q_strcat leaves out a word that does not fit and takes the shorter ones after it: the script ends at
		// the first word that does not fit, not with a hole in it
		if ( cut || strlen( script ) + strlen( token.string ) + 4 > sizeof( script ) ) {
			cut = qtrue;
			continue;
		}

		if (token.string[1] != '\0') {
			Q_strcat(script, sizeof(script), va("\"%s\"", token.string));
		} else {
			Q_strcat(script, sizeof(script), token.string);
		}
		Q_strcat(script, sizeof(script), " ");
	}
	return qfalse;
}

// display, window, menu, item code
//

/*
==================
Init_Display

Initializes the display with a structure to all the drawing routines
 ==================
*/
void Init_Display(displayContextDef_t *dc) {
	DC = dc;
}

// type and style painting

void GradientBar_Paint(rectDef_t *rect, vec4_t color) {
	// gradient bar takes two paints
	DC->setColor( color );
	DC->drawHandlePic(rect->x, rect->y, rect->w, rect->h, DC->Assets.gradientBar);
	DC->setColor( NULL );
}

/*
==================
Window_Init

Initializes a window structure ( windowDef_t ) with defaults

==================
*/
void Window_Init(windowDef_t *w) {
	memset(w, 0, sizeof(windowDef_t));
	w->borderSize = 1;
	w->foreColor[0] = w->foreColor[1] = w->foreColor[2] = w->foreColor[3] = 1.0;
	w->cinematic = -1;
}

void Fade(int *flags, float *f, float clamp, int *nextTime, int offsetTime, qboolean bFlags, float fadeAmount) {
	if ( *flags & ( WINDOW_FADINGOUT | WINDOW_FADINGIN ) ) {
		if ( DC->realTime > *nextTime ) {
			*nextTime = DC->realTime + offsetTime;
			if ( *flags & WINDOW_FADINGOUT ) {
				*f -= fadeAmount;
				if ( bFlags && *f <= 0.0 )
					*flags &= ~( WINDOW_FADINGOUT | WINDOW_VISIBLE );
			}
			else {
				*f += fadeAmount;
				if ( *f >= clamp ) {
					*f = clamp;
					if ( bFlags )
						*flags &= ~WINDOW_FADINGIN;
				}
			}
		}
	}
}

#define MENU_OPEN_MSEC	180		// how long a menu with "openSlide" takes to come up

// Where the line of text of an item is, for the widget that stands in it (a switch, a slider, the box of a
// field): the place of its label, or the item itself when it has none
static void Item_SkinLine( const itemDef_t *item, float *y, float *h ) {
	if ( item->text && item->textRect.h > 0.0f ) {
		*h = DC->textHeight( "A", item->textscale, item->iMenuFont );
		*y = item->textRect.y;
	} else {
		*h = item->window.rect.h;
		*y = item->window.rect.y;
	}
}

// Is this item drawn in the look of ui_skin.c? Its menu asks for it ("skin 1") and the engine can.
textCatch_t textCatch;

static qboolean Item_Skinned( const itemDef_t *item ) {
	return (qboolean)( item->parent && ( (const menuDef_t *)item->parent )->skin && Skin_Ready() );
}

// A window over the whole 640 x 480 with a picture or a fill is a background: Window_Paint() makes it as wide as
// a wide screen is
static qboolean Window_IsBackground( const windowDef_t *w ) {
	return (qboolean)( w->style != 0 && w->border == 0
		&& w->rect.x == 0 && w->rect.y == 0 && w->rect.w == SCREEN_WIDTH && w->rect.h == SCREEN_HEIGHT );
}

// while Menu_Paint() paints the items that come before the background of their menu, which lie under it
static qboolean window_underBackground;

void Window_Paint(windowDef_t *w, float fadeAmount, float fadeClamp, float fadeCycle)
{
	//float bordersize = 0;
	vec4_t color;
	rectDef_t fillRect;

	if ( w == NULL )
		return;

	if ( debugMode ) {
		color[0] = color[1] = color[2] = color[3] = 1;
		DC->drawRect(w->rect.x, w->rect.y, w->rect.w, w->rect.h, 1, color);
	}

	if ( w->style == 0 && w->border == 0 )
		return;

	fillRect = w->rect;
	if ( w->border != 0 ) {
		fillRect.x += w->borderSize;
		fillRect.y += w->borderSize;
		fillRect.w -= w->borderSize + 1;
		fillRect.h -= w->borderSize + 1;
	}
	// a window over the whole 640 x 480 is a background: it covers a wide screen too, where the grid is wider
	// than 640 by DC->wide on either side
	// ("wide 1" says the same of a window that is not that high: the bar at the top of a page. With the menu on
	// its panel in the level there is nothing beside the 640 units, DC->wide is 0 and the window ends with them.)
	// On a window narrower than 4:3 a background also reaches above and below the 480 by DC->tall.
	else if ( ( DC->wide > 0.0f || DC->tall > 0.0f ) && ( Window_IsBackground( w ) || w->wide ) ) {
		fillRect.x -= DC->wide;
		fillRect.w += 2.0f * DC->wide;
		if ( Window_IsBackground( w ) ) {
			fillRect.y -= DC->tall;
			fillRect.h += 2.0f * DC->tall;
		}
	}
	// What lies under the left or the right edge of such a background and is seen through it (the columns of
	// text of the main menus, under the holes of gfx/menus/main_background) is stretched as the background is.
	// Left where it was, its hole in the wider picture stood over the base of the menu, which is white.
	else if ( DC->wide > 0.0f && window_underBackground && ( w->rect.x <= 0.0f || w->rect.x + w->rect.w >= SCREEN_WIDTH ) ) {
		const float scale = ( SCREEN_WIDTH + 2.0f * DC->wide ) / SCREEN_WIDTH;

		fillRect.x = fillRect.x * scale - DC->wide;
		fillRect.w *= scale;
	}

	if ( w->style == WINDOW_STYLE_FILLED ) {
		// box, but possible a shader that needs filled
		if ( w->background ) {
			Fade( &w->flags, &w->backColor[3], fadeClamp, &w->nextTime, fadeCycle, qtrue, fadeAmount );
			DC->setColor( w->backColor );
			DC->drawHandlePic( fillRect.x, fillRect.y, fillRect.w, fillRect.h, w->background );
			DC->setColor( NULL );
		}
		else if ( fillRect.h < 1.0f && fillRect.w > fillRect.h && Skin_Ready() ) {
			// a line of the new look: one pixel, and whole on a menu that stands in the level (Skin_Line)
			Skin_Line( fillRect.x, fillRect.y, fillRect.w, w->backColor );
		}
		else {
			DC->fillRect( fillRect.x, fillRect.y, fillRect.w, fillRect.h, w->backColor );
		}
	}
	else if ( w->style == WINDOW_STYLE_GRADIENT ) {
		GradientBar_Paint(&fillRect, w->backColor);
	}
	else if ( w->style == WINDOW_STYLE_PANEL ) {
		Fade( &w->flags, &w->backColor[3], fadeClamp, &w->nextTime, fadeCycle, qtrue, fadeAmount );
		if ( Skin_Ready() ) {
			if ( w->shadow > 0.0f ) {
				const vec4_t dark = { 0.0f, 0.0f, 0.0f, 0.45f * w->backColor[3] };

				Skin_Soft( w->rect.x, w->rect.y + w->shadow * 0.25f, w->rect.w, w->rect.h, w->shadow, dark );
			}
			if ( w->glowColor[3] > 0.0f ) {
				Skin_Soft( w->rect.x, w->rect.y, w->rect.w, w->rect.h, 10.0f, w->glowColor );
			}
			Skin_Panel( w->rect.x, w->rect.y, w->rect.w, w->rect.h, w->radius, w->backColor, w->borderColor );
			if ( w->sheen > 0.0f && w->rect.w > 2.0f * w->radius ) {
				const vec4_t light = { 1.0f, 1.0f, 1.0f, w->sheen };

				DC->fillRect( w->rect.x + w->radius, w->rect.y + 1.0f / DC->yscale, w->rect.w - 2.0f * w->radius, 1.0f / DC->yscale, light );
			}
		} else {
			// an engine that can not make the pictures: the same panel with square corners
			DC->fillRect( w->rect.x, w->rect.y, w->rect.w, w->rect.h, w->backColor );
			if ( w->borderColor[3] > 0.0f ) {
				DC->drawRect( w->rect.x, w->rect.y, w->rect.w, w->rect.h, 1, w->borderColor );
			}
		}
		return;
	}
	else if ( w->style == WINDOW_STYLE_SHADER ) {
#ifndef _CGAME
		if ( w->flags & WINDOW_PLAYERCOLOR ) {
			vec4_t	color;
			color[0] = ui_char_color_red.integer/255.0f;
			color[1] = ui_char_color_green.integer/255.0f;
			color[2] = ui_char_color_blue.integer/255.0f;
			color[3] = 1;
			DC->setColor( color );
		}
#endif //

		if ( w->flags & WINDOW_FORECOLORSET )
			DC->setColor(w->foreColor);
		// No picture (the name came from a cvar that is empty, or the file is not there): the game drew the default
		// image of the renderer, a grey box in a white frame. Seen in the siege objectives, whose main picture no
		// level of the game sets.
		if ( w->background || !Skin_Ready() ) {
			DC->drawHandlePic( fillRect.x, fillRect.y, fillRect.w, fillRect.h, w->background );
		}
		DC->setColor( NULL );
	}
	else if ( w->style == WINDOW_STYLE_TEAMCOLOR ) {
		if ( DC->getTeamColor ) {
			DC->getTeamColor( &color );
			DC->fillRect( fillRect.x, fillRect.y, fillRect.w, fillRect.h, color );
		}
	}
	else if ( w->style == WINDOW_STYLE_CINEMATIC ) {
		if ( w->cinematic == -1 ) {
			w->cinematic = DC->playCinematic( w->cinematicName, fillRect.x, fillRect.y, fillRect.w, fillRect.h );
			if ( w->cinematic == -1 )
				w->cinematic = -2;
		}
		if ( w->cinematic >= 0 ) {
			DC->runCinematicFrame( w->cinematic );
			DC->drawCinematic( w->cinematic, fillRect.x, fillRect.y, fillRect.w, fillRect.h );
		}
	}

	if ( w->border == WINDOW_BORDER_FULL ) {
		// full
		// HACK HACK HACK
		if ( w->style == WINDOW_STYLE_TEAMCOLOR ) {
			if ( color[0] > 0 ) {
				// red
				color[0] = 1;
				color[1] = color[2] = .5;
			}
			else {
				color[2] = 1;
				color[0] = color[1] = .5;
			}
			color[3] = 1;
			DC->drawRect( w->rect.x, w->rect.y, w->rect.w, w->rect.h, w->borderSize, color );
		}
		else
			DC->drawRect( w->rect.x, w->rect.y, w->rect.w, w->rect.h, w->borderSize, w->borderColor );
	}
	else if (w->border == WINDOW_BORDER_HORZ) {
		// top/bottom
		DC->setColor( w->borderColor );
		DC->drawTopBottom( w->rect.x, w->rect.y, w->rect.w, w->rect.h, w->borderSize );
		DC->setColor( NULL );
	}
	else if (w->border == WINDOW_BORDER_VERT) {
		// left right
		DC->setColor( w->borderColor );
		DC->drawSides( w->rect.x, w->rect.y, w->rect.w, w->rect.h, w->borderSize );
		DC->setColor( NULL );
	}
	else if ( w->border == WINDOW_BORDER_KCGRADIENT ) {
		// this is just two gradient bars along each horz edge
		rectDef_t r = w->rect;
		r.h = w->borderSize;
		GradientBar_Paint(&r, w->borderColor);
		r.y = w->rect.y + w->rect.h - 1;
		GradientBar_Paint(&r, w->borderColor);
	}
}

void Item_SetScreenCoords(itemDef_t *item, float x, float y)
{
	if (item == NULL)
		return;

	if (item->window.border != 0) {
		x += item->window.borderSize;
		y += item->window.borderSize;
	}

	item->window.rect.x = x + item->window.rectClient.x;
	item->window.rect.y = y + item->window.rectClient.y;
	item->window.rect.w = item->window.rectClient.w;
	item->window.rect.h = item->window.rectClient.h;
	// a row of the list of a page ("scroll 1") stands where the list has been moved to
	if ( item->scroll && item->parent ) {
		item->window.rect.y -= ( (const menuDef_t *)item->parent )->scrollShown;
	}

	// force the text rects to recompute
	item->textRect.w = 0;
	item->textRect.h = 0;

	if ( item->type == ITEM_TYPE_TEXTSCROLL ) {
		textScrollDef_t *scrollPtr = item->typeData.textscroll;
		if ( scrollPtr ) {
			scrollPtr->startPos = 0;
			scrollPtr->endPos = 0;
		}

		Item_TextScroll_BuildLines ( item );
	}
}

// FIXME: consolidate this with nearby stuff
void Item_UpdatePosition(itemDef_t *item)
{
	float x, y;
	menuDef_t *menu;

	if (item == NULL || item->parent == NULL)
		return;

	menu = (menuDef_t *) item->parent;

	x = menu->window.rect.x;
	y = menu->window.rect.y;

	if ( menu->window.border != 0 ) {
		x += menu->window.borderSize;
		y += menu->window.borderSize;
	}

	Item_SetScreenCoords(item, x, y);
}

// menus
void Menu_UpdatePosition(menuDef_t *menu) {
	int i;
	float x, y;

	if (menu == NULL) {
		return;
	}

	x = menu->window.rect.x;
	y = menu->window.rect.y;
	if ( menu->window.border != 0 ) {
		x += menu->window.borderSize;
		y += menu->window.borderSize;
	}

	for ( i=0; i<menu->itemCount; i++ ) {
		Item_SetScreenCoords( menu->items[i], x, y );
	}
}

/*
===============================================================================

The canvas of a menu (VisionJK, "canvas <w> <h>")

A menu file with a canvas is written in the pixels of a picture of that size, the way a design is drawn, and
not in the 640 x 480 units of the game. The picture is put on the screen as large as it fits with the same
scale across and down, in the middle: on a wide window it fills the height, on a window of 4:3 it fills the
width. So a page keeps the proportions it was drawn with on every window, also where the 2D of the game is
stretched (an engine without v_2dBias), and on the panel in the level, where it is put on the 640 x 480 of the panel.

What the file gave is kept in item->canvas and the rectangles, the sizes of text and the rows of lists are
worked out from it again whenever the window or the panel is not the one the layout was made for.

===============================================================================
*/

// the menu was laid out for: the units beside the grid across and down, and the pixels a unit has
static void Menu_CanvasNow( float out[4] ) {
	out[0] = DC->wide;
	out[1] = DC->tall;
	out[2] = DC->xscale;
	out[3] = DC->yscale;
}

static void Menu_CanvasKeep( menuDef_t *menu ) {
	int i, j;

	menu->canvasRect = menu->window.rect;
	menu->canvasScroll = menu->scrollRect;
	menu->canvasScrollStep = menu->scrollStep;
	menu->canvasDescX = menu->descX;
	menu->canvasDescY = menu->descY;
	menu->canvasDescScale = menu->descScale;
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *item = menu->items[i];

		item->canvas.rect = item->window.rectClient;
		item->canvas.textscale = item->textscale;
		item->canvas.textalignx = item->textalignx;
		item->canvas.textaligny = item->textaligny;
		item->canvas.text2alignx = item->text2alignx;
		item->canvas.text2aligny = item->text2aligny;
		item->canvas.textRoom = item->textRoom;
		item->canvas.radius = item->window.radius;
		item->canvas.shadow = item->window.shadow;
		item->canvas.borderSize = item->window.borderSize;
		if ( item->type == ITEM_TYPE_LISTBOX && item->typeData.listbox ) {
			listBoxDef_t *list = item->typeData.listbox;

			item->canvas.elementWidth = list->elementWidth;
			item->canvas.elementHeight = list->elementHeight;
			for ( j = 0; j < list->numColumns && j < MAX_LB_COLUMNS; j++ ) {
				item->canvas.columnPos[j] = list->columnInfo[j].pos;
				item->canvas.columnWidth[j] = list->columnInfo[j].width;
			}
		}
	}
}

// the units of the grid one pixel of the canvas has, or 1 for a menu without a canvas. The widgets of ui_shared.c
// that have sizes of their own (a row, its arrows, the bar of a list) take them times Menu_Unit(): those sizes were
// made for a canvas of 900 pixels high on a wide window, where a pixel of it is 480 / 900 of a unit.
float Menu_Unit( const menuDef_t *menu ) {
	if ( !menu || menu->canvasW <= 0.0f || menu->canvasUnit <= 0.0f ) {
		return 1.0f;
	}
	return menu->canvasUnit * ( 900.0f / 480.0f );
}

/*
=================
Menu_CanvasBars

A page whose canvas is wider than the screen (4:3, 16:10) has room above and below it: spare is that room under
the canvas, the same above it. The page goes up to the top of the screen, as a page is read from the top; the bar
at the bottom ("wide" items from the bottom of the canvas up, one over the other, and what stands in them) goes down
to its bottom. What reached that bar before (a rail, a pane, the window of a list) grows to reach it again.
=================
*/
static void Menu_CanvasBars( menuDef_t *menu, float spare ) {
	float		footTop = menu->canvasH;
	qboolean	grew;
	int			i;

	if ( spare < 0.5f ) {
		return;
	}
	do {
		grew = qfalse;
		for ( i = 0; i < menu->itemCount; i++ ) {
			const rectDef_t *r = &menu->items[i]->canvas.rect;

			if ( menu->items[i]->window.wide && r->h >= 2.0f && fabsf( r->y + r->h - footTop ) < 0.5f && r->y < footTop - 0.5f ) {
				footTop = r->y;
				grew = qtrue;
			}
		}
	} while ( grew );
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t		*item = menu->items[i];
		const rectDef_t	*r = &item->canvas.rect;

		if ( footTop < menu->canvasH && r->y >= footTop - 0.5f ) {
			item->window.rectClient.y += spare;
			continue;
		}
		item->window.rectClient.y -= spare;
		if ( footTop < menu->canvasH && fabsf( r->y + r->h - footTop ) < 0.5f ) {
			item->window.rectClient.h += 2.0f * spare;
		}
	}
	if ( menu->canvasScroll.w > 0.0f ) {
		menu->scrollRect.y -= spare;
		if ( footTop < menu->canvasH && fabsf( menu->canvasScroll.y + menu->canvasScroll.h - footTop ) < 0.5f ) {
			menu->scrollRect.h += 2.0f * spare;
		}
	}
	menu->descY += ( ( footTop < menu->canvasH && menu->canvasDescY >= footTop - 0.5f ) ? spare : -spare );
}

void Menu_CanvasLayout( menuDef_t *menu ) {
	float	now[4], ux, uy, gw, gh, x0, y0, px, cx0, cy0;
	int		i, j;

	if ( !menu || menu->canvasW <= 0.0f || menu->canvasH <= 0.0f ) {
		return;
	}
	Menu_CanvasNow( now );
	gw = SCREEN_WIDTH + 2.0f * now[0];
	gh = SCREEN_HEIGHT + 2.0f * now[1];
	if ( now[0] <= 0.0f && now[1] <= 0.0f && DC->getCVarValue( "v_uiPlane" ) ) {
		// the panel in the level: the units of the grid are square there
		ux = uy = Q_min( gw / menu->canvasW, gh / menu->canvasH );
	} else {
		// on the screen: the same number of pixels across and down for a pixel of the canvas
		px = Q_min( gw * now[2] / menu->canvasW, gh * now[3] / menu->canvasH );
		ux = now[2] > 0.0f ? px / now[2] : 1.0f;
		uy = now[3] > 0.0f ? px / now[3] : 1.0f;
	}
	x0 = -now[0] + ( gw - menu->canvasW * ux ) * 0.5f;
	y0 = -now[1] + ( gh - menu->canvasH * uy ) * 0.5f;
	menu->canvasUnit = uy;
	memcpy( menu->canvasFor, now, sizeof( menu->canvasFor ) );
	cx0 = x0;
	cy0 = y0;

	// a whole page is the canvas; a window keeps its place on the canvas
	if ( menu->fullScreen ) {
		menu->window.rect.x = x0;
		menu->window.rect.y = y0;
		menu->window.rect.w = menu->canvasW * ux;
		menu->window.rect.h = menu->canvasH * uy;
	} else {
		menu->window.rect.x = x0 + menu->canvasRect.x * ux;
		menu->window.rect.y = y0 + menu->canvasRect.y * uy;
		menu->window.rect.w = menu->canvasRect.w * ux;
		menu->window.rect.h = menu->canvasRect.h * uy;
		x0 = menu->window.rect.x;
		y0 = menu->window.rect.y;
	}
	if ( menu->canvasScroll.w > 0.0f ) {
		// (from the corner of the menu, as the rectangles of its items)
		menu->scrollRect.x = menu->canvasScroll.x * ux;
		menu->scrollRect.y = menu->canvasScroll.y * uy;
		menu->scrollRect.w = menu->canvasScroll.w * ux;
		menu->scrollRect.h = menu->canvasScroll.h * uy;
	}
	menu->scrollStep = menu->canvasScrollStep * uy;
	// (the line about the item in focus is placed on the screen, from the corner of the canvas)
	menu->descX = cx0 + menu->canvasDescX * ux;
	menu->descY = cy0 + menu->canvasDescY * uy;
	menu->descScale = menu->canvasDescScale * uy;

	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *item = menu->items[i];

		item->window.rectClient.x = item->canvas.rect.x * ux;
		item->window.rectClient.y = item->canvas.rect.y * uy;
		item->window.rectClient.w = item->canvas.rect.w * ux;
		item->window.rectClient.h = item->canvas.rect.h * uy;
		item->textscale = item->canvas.textscale * uy;
		if ( item->skinScale > 0.0f ) {
			item->skinScale = item->textscale;
		}
		item->textalignx = item->canvas.textalignx * ux;
		item->textaligny = item->canvas.textaligny * uy;
		item->text2alignx = item->canvas.text2alignx * ux;
		item->text2aligny = item->canvas.text2aligny * uy;
		item->textRoom = item->canvas.textRoom * ux;
		item->window.radius = item->canvas.radius * uy;
		item->window.shadow = item->canvas.shadow * uy;
		item->window.borderSize = item->canvas.borderSize * uy;
		if ( item->type == ITEM_TYPE_LISTBOX && item->typeData.listbox ) {
			listBoxDef_t *list = item->typeData.listbox;

			list->elementWidth = item->canvas.elementWidth * ux;
			list->elementHeight = item->canvas.elementHeight * uy;
			for ( j = 0; j < list->numColumns && j < MAX_LB_COLUMNS; j++ ) {
				list->columnInfo[j].pos = (int)( item->canvas.columnPos[j] * ux + 0.5f );
				list->columnInfo[j].width = (int)( item->canvas.columnWidth[j] * ux + 0.5f );
			}
		}
	}
	if ( menu->fullScreen ) {
		Menu_CanvasBars( menu, -now[1] + gh - ( cy0 + menu->canvasH * uy ) );
	}
	// what covers the whole height of the canvas (the veil under a window) covers the whole height of the screen
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t		*item = menu->items[i];
		const float		top = ( menu->fullScreen ? 0.0f : menu->canvasRect.y ) + item->canvas.rect.y;

		if ( item->window.wide && top <= 0.5f && top + item->canvas.rect.h >= menu->canvasH - 0.5f ) {
			item->window.rectClient.y = -now[1] - menu->window.rect.y;
			item->window.rectClient.h = gh;
		}
	}
	// (the rows of a list are where the menu file put them again: the list is laid out anew)
	menu->listKnown = qfalse;
	menu->listSearch[0] = '\1';
	Menu_UpdatePosition( menu );
}

// lays a menu with a canvas out again when the window, its 2D or the panel in the level is not what it was made for
static void Menu_CanvasCheck( menuDef_t *menu ) {
	float now[4];

	if ( !menu || menu->canvasW <= 0.0f ) {
		return;
	}
	Menu_CanvasNow( now );
	if ( memcmp( now, menu->canvasFor, sizeof( now ) ) ) {
		Menu_CanvasLayout( menu );
	}
}

void Menu_PostParse(menuDef_t *menu) {
	if ( menu == NULL )
		return;

	if ( menu->fullScreen ) {
		menu->window.rect.x = 0;
		menu->window.rect.y = 0;
		menu->window.rect.w = 640;
		menu->window.rect.h = 480;
	}
	// a menu with pages opens on its first one, and one with an ITEM_TYPE_DESC explains its items there
	{
		int i, first = 0;

		for ( i = 0; i < menu->itemCount; i++ ) {
			if ( menu->items[i]->tab && ( !first || menu->items[i]->tab < first ) ) {
				first = menu->items[i]->tab;
			}
			if ( menu->items[i]->type == ITEM_TYPE_DESC ) {
				menu->descPane = qtrue;
			}
		}
		if ( !menu->page ) {
			menu->page = first;
		}
	}
	if ( menu->canvasW > 0.0f && menu->canvasH > 0.0f ) {
		Menu_CanvasKeep( menu );
		Menu_CanvasLayout( menu );
		return;
	}
	Menu_UpdatePosition( menu );
}

itemDef_t *Menu_ClearFocus(menuDef_t *menu) {
	int i;
	itemDef_t *ret = NULL;

	if ( menu == NULL )
		return NULL;

	for ( i=0; i<menu->itemCount; i++ ) {
		if ( menu->items[i]->window.flags & WINDOW_HASFOCUS )
			ret = menu->items[i];

		menu->items[i]->window.flags &= ~WINDOW_HASFOCUS;
		if ( menu->items[i]->leaveFocus )
			Item_RunScript( menu->items[i], menu->items[i]->leaveFocus );
	}

	return ret;
}

qboolean IsVisible(int flags) {
	return (flags & WINDOW_VISIBLE && !(flags & WINDOW_FADINGOUT));
}

qboolean Rect_ContainsPoint(rectDef_t *rect, float x, float y) {
	if ( rect ) {
		if ( x > rect->x && x < rect->x + rect->w && y > rect->y && y < rect->y + rect->h )
			return qtrue;
	}
	return qfalse;
}

// whether a point is on a menu. A whole page with a canvas covers the whole screen, as a fullScreen menu does in
// the stock ui: its window.rect is only the canvas, and at 4:3 or 16:10 Menu_CanvasBars puts the bars outside it
// (a click on the Exit button in the bottom bar of the main page counted as a click beside the menu, #15)
static qboolean Menu_ContainsPoint( menuDef_t *menu, float x, float y ) {
	if ( menu->fullScreen && menu->canvasW > 0.0f ) {
		return qtrue;
	}
	return Rect_ContainsPoint( &menu->window.rect, x, y );
}

int Menu_ItemsMatchingGroup(menuDef_t *menu, const char *name) {
	int i;
	int count = 0;

	for ( i=0; i<menu->itemCount; i++ ) {
		if ( !VALIDSTRING( menu->items[i]->window.name ) && !VALIDSTRING( menu->items[i]->window.group ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: item has neither name or group\n" );
			continue;
		}

		if ( !Q_stricmp( menu->items[i]->window.name, name ) || ( VALIDSTRING( menu->items[i]->window.group ) && !Q_stricmp( menu->items[i]->window.group, name ) ) )
			count++;
	}

	return count;
}

itemDef_t *Menu_GetMatchingItemByNumber(menuDef_t *menu, int index, const char *name) {
	int i;
	int count = 0;
	for (i = 0; i < menu->itemCount; i++) {
		if (Q_stricmp(menu->items[i]->window.name, name) == 0 || (menu->items[i]->window.group && Q_stricmp(menu->items[i]->window.group, name) == 0)) {
			if (count == index) {
				return menu->items[i];
			}
			count++;
		}
	}
	return NULL;
}

qboolean Script_SetColor ( itemDef_t *item, char **args )
{
	const char *name;
	int i;
	float f;
	vec4_t *out;

	// expecting type of color to set and 4 args for the color
	if (String_Parse(args, &name))
	{
		out = NULL;
		if (Q_stricmp(name, "backcolor") == 0)
		{
			out = &item->window.backColor;
			item->window.flags |= WINDOW_BACKCOLORSET;
		}
		else if (Q_stricmp(name, "forecolor") == 0)
		{
			out = &item->window.foreColor;
			item->window.flags |= WINDOW_FORECOLORSET;
		}
		else if (Q_stricmp(name, "bordercolor") == 0)
		{
			out = &item->window.borderColor;
		}

		if (out)
		{
			for (i = 0; i < 4; i++)
			{
				if (!Float_Parse(args, &f))
				{
					return qtrue;
				}
				(*out)[i] = f;
			}
		}
	}

	return qtrue;
}

qboolean Script_SetAsset(itemDef_t *item, char **args)
{
	const char *name;
	// expecting name to set asset to
	if (String_Parse(args, &name))
	{
		// check for a model
		if (item->type == ITEM_TYPE_MODEL)
		{
		}
	}
	return qtrue;
}

qboolean Script_SetBackground(itemDef_t *item, char **args)
{
	const char *name;
	// expecting name to set asset to
	if (String_Parse(args, &name))
	{
		item->window.background = DC->registerShaderNoMip(name);
	}
	return qtrue;
}

qboolean Script_SetItemRectCvar(itemDef_t *item, char **args)
{
	const char	*itemName;
	const char	*cvarName;
	char		cvarBuf[1024];
	const char	*holdVal;
	char		*holdBuf;
	itemDef_t	*item2=0;
	menuDef_t	*menu;

	// expecting item group and cvar to get value from
	if (String_Parse(args, &itemName) && String_Parse(args, &cvarName))
	{
		item2 = Menu_FindItemByName((menuDef_t *) item->parent, itemName);

		if (item2)
		{
			// get cvar data
			DC->getCVarString(cvarName, cvarBuf, sizeof(cvarBuf));

			holdBuf = cvarBuf;
			if (String_Parse(&holdBuf,&holdVal))
			{
				menu = (menuDef_t *) item->parent;

				item2->window.rectClient.x = atof(holdVal) + menu->window.rect.x;
				if (String_Parse(&holdBuf,&holdVal))
				{
					item2->window.rectClient.y = atof(holdVal) + menu->window.rect.y;
					if (String_Parse(&holdBuf,&holdVal))
					{
						item2->window.rectClient.w = atof(holdVal);
						if (String_Parse(&holdBuf,&holdVal))
						{
							item2->window.rectClient.h = atof(holdVal);

							item2->window.rect.x = item2->window.rectClient.x;
							item2->window.rect.y = item2->window.rectClient.y;
							item2->window.rect.w = item2->window.rectClient.w;
							item2->window.rect.h = item2->window.rectClient.h;

							return qtrue;
						}
					}
				}
			}
		}
	}

	// Default values in case things screw up
	if (item2)
	{
		item2->window.rectClient.x = 0;
		item2->window.rectClient.y = 0;
		item2->window.rectClient.w = 0;
		item2->window.rectClient.h = 0;
	}

//	Com_Printf(S_COLOR_YELLOW"WARNING: SetItemRectCvar: problems. Set cvar to 0's\n" );

	return qtrue;
}

qboolean Script_SetItemBackground(itemDef_t *item, char **args)
{
	const char *itemName;
	const char *name;

	// expecting name of shader
	if (String_Parse(args, &itemName) && String_Parse(args, &name))
	{
		Menu_SetItemBackground((menuDef_t *) item->parent, itemName, name);
	}
	return qtrue;
}

qboolean Script_SetItemText(itemDef_t *item, char **args)
{
	const char *itemName;
	const char *text;

	// expecting text
	if (String_Parse(args, &itemName) && String_Parse(args, &text))
	{
		Menu_SetItemText((menuDef_t *) item->parent, itemName, text);
	}
	return qtrue;
}

itemDef_t *Menu_FindItemByName(menuDef_t *menu, const char *p) {
	int i;
	if (menu == NULL || p == NULL) {
		return NULL;
	}

	for (i = 0; i < menu->itemCount; i++) {
		if (Q_stricmp(p, menu->items[i]->window.name) == 0) {
			return menu->items[i];
		}
	}

	return NULL;
}

qboolean Script_SetTeamColor(itemDef_t *item, char **args)
{
	if (DC->getTeamColor)
	{
		int i;
		vec4_t color;
		DC->getTeamColor(&color);
		for (i = 0; i < 4; i++)
		{
			item->window.backColor[i] = color[i];
		}
	}
	return qtrue;
}

qboolean Script_SetItemColor(itemDef_t *item, char **args)
{
	const char *itemname;
	const char *name;
	vec4_t color;
	int i;
	vec4_t *out;

	// expecting type of color to set and 4 args for the color
	if (String_Parse(args, &itemname) && String_Parse(args, &name))
	{
		itemDef_t	*item2;
		int			j,count;
		char buff[1024];

		// Is is specifying a cvar to get the item name from?
		if (itemname[0] == '*')
		{
			itemname += 1;
		    DC->getCVarString(itemname, buff, sizeof(buff));
			itemname = buff;
		}

		count = Menu_ItemsMatchingGroup((menuDef_t *) item->parent, itemname);

		if (!Color_Parse(args, &color))
		{
			return qtrue;
		}

		for (j = 0; j < count; j++)
		{
			item2 = Menu_GetMatchingItemByNumber((menuDef_t *) item->parent, j, itemname);
			if (item2 != NULL)
			{
				out = NULL;
				if (Q_stricmp(name, "backcolor") == 0)
				{
					out = &item2->window.backColor;
				}
				else if (Q_stricmp(name, "forecolor") == 0)
				{
					out = &item2->window.foreColor;
					item2->window.flags |= WINDOW_FORECOLORSET;
				}
				else if (Q_stricmp(name, "bordercolor") == 0)
				{
					out = &item2->window.borderColor;
				}

				if (out)
				{
					for (i = 0; i < 4; i++)
					{
						(*out)[i] = color[i];
					}
				}
			}
		}
	}

	return qtrue;
}

qboolean Script_SetItemColorCvar(itemDef_t *item, char **args)
{
	const char *itemname;
	char	*colorCvarName,*holdBuf,*holdVal;
	char cvarBuf[1024];
	const char *name;
	vec4_t color = { 0.0f };
	int i;
	vec4_t *out;

	// expecting type of color to set and 4 args for the color
	if (String_Parse(args, &itemname) && String_Parse(args, &name))
	{
		itemDef_t	*item2;
		int			j,count;
		char buff[1024];

		// Is is specifying a cvar to get the item name from?
		if (itemname[0] == '*')
		{
			itemname += 1;
		    DC->getCVarString(itemname, buff, sizeof(buff));
			itemname = buff;
		}

		count = Menu_ItemsMatchingGroup((menuDef_t *) item->parent, itemname);

		// Get the cvar with the color
		if (!String_Parse(args,(const char **) &colorCvarName))
		{
			return qtrue;
		}
		else
		{
			DC->getCVarString(colorCvarName, cvarBuf, sizeof(cvarBuf));

			holdBuf = cvarBuf;
			if (String_Parse(&holdBuf,(const char **) &holdVal))
			{
				color[0] = atof(holdVal);
				if (String_Parse(&holdBuf,(const char **) &holdVal))
				{
					color[1] = atof(holdVal);
					if (String_Parse(&holdBuf,(const char **) &holdVal))
					{
						color[2] = atof(holdVal);
						if (String_Parse(&holdBuf,(const char **) &holdVal))
						{
							color[3] = atof(holdVal);
						}
					}
				}
			}
		}

		for (j = 0; j < count; j++)
		{
			item2 = Menu_GetMatchingItemByNumber((menuDef_t *) item->parent, j, itemname);
			if (item2 != NULL)
			{
				out = NULL;
				if (Q_stricmp(name, "backcolor") == 0)
				{
					out = &item2->window.backColor;
				}
				else if (Q_stricmp(name, "forecolor") == 0)
				{
					out = &item2->window.foreColor;
					item2->window.flags |= WINDOW_FORECOLORSET;
				}
				else if (Q_stricmp(name, "bordercolor") == 0)
				{
					out = &item2->window.borderColor;
				}

				if (out)
				{
					for (i = 0; i < 4; i++)
					{
						(*out)[i] = color[i];
					}
				}
			}
		}
	}

	return qtrue;
}

qboolean Script_SetItemRect(itemDef_t *item, char **args)
{
	const char *itemname;
	rectDef_t *out;
	rectDef_t rect;
	menuDef_t	*menu;

	// expecting type of color to set and 4 args for the color
	if (String_Parse(args, &itemname))
	{
		itemDef_t *item2;
		int j;
		int count = Menu_ItemsMatchingGroup((menuDef_t *) item->parent, itemname);

		if (!Rect_Parse(args, &rect))
		{
			return qtrue;
		}

		menu = (menuDef_t *) item->parent;

		for (j = 0; j < count; j++)
		{
			item2 = Menu_GetMatchingItemByNumber(menu, j, itemname);
			if (item2 != NULL)
			{
				out = &item2->window.rect;

				if (out)
				{
					item2->window.rect.x = rect.x  + menu->window.rect.x;
					item2->window.rect.y = rect.y  + menu->window.rect.y;
					item2->window.rect.w = rect.w;
					item2->window.rect.h = rect.h;
				}
			}
		}
	}
	return qtrue;
}

void Menu_ShowGroup (menuDef_t *menu, const char *groupName, qboolean showFlag)
{
	itemDef_t *item;
	int count,j;

	count = Menu_ItemsMatchingGroup( menu, groupName);
	for (j = 0; j < count; j++)
	{
		item = Menu_GetMatchingItemByNumber( menu, j, groupName);
		if (item != NULL)
		{
			if (showFlag)
			{
				item->window.flags |= WINDOW_VISIBLE;
			}
			else
			{
				item->window.flags &= ~(WINDOW_VISIBLE | WINDOW_HASFOCUS);
			}
		}
	}
}

void Menu_ShowItemByName(menuDef_t *menu, const char *p, qboolean bShow) {
	itemDef_t *item;
	int i;
	int count = Menu_ItemsMatchingGroup(menu, p);
	for (i = 0; i < count; i++) {
		item = Menu_GetMatchingItemByNumber(menu, i, p);
		if (item != NULL) {
			if (bShow) {
				item->window.flags |= WINDOW_VISIBLE;
			} else {
				item->window.flags &= ~WINDOW_VISIBLE;
				// stop cinematics playing in the window
				if (item->window.cinematic >= 0) {
					DC->stopCinematic(item->window.cinematic);
					item->window.cinematic = -1;
				}
			}
		}
	}
}

void Menu_FadeItemByName(menuDef_t *menu, const char *p, qboolean fadeOut) {
	itemDef_t *item;
	int i;
	int count = Menu_ItemsMatchingGroup(menu, p);
	for (i = 0; i < count; i++) {
		item = Menu_GetMatchingItemByNumber(menu, i, p);
		if (item != NULL) {
			if (fadeOut) {
				item->window.flags |= (WINDOW_FADINGOUT | WINDOW_VISIBLE);
				item->window.flags &= ~WINDOW_FADINGIN;
			} else {
				item->window.flags |= (WINDOW_VISIBLE | WINDOW_FADINGIN);
				item->window.flags &= ~WINDOW_FADINGOUT;
			}
		}
	}
}

menuDef_t *Menus_FindByName(const char *p) {
	int i;
	for (i = 0; i < menuCount; i++) {
		if (Q_stricmp(Menus[i].window.name, p) == 0) {
			return &Menus[i];
		}
	}
	return NULL;
}

void Menus_ShowByName(const char *p) {
	menuDef_t *menu = Menus_FindByName(p);
	if (menu) {
		Menus_Activate(menu);
	}
}

void Menus_OpenByName(const char *p) {
	Menus_ActivateByName(p);
}

static void Menu_RunCloseScript(menuDef_t *menu) {
	if (menu && menu->window.flags & WINDOW_VISIBLE && menu->onClose) {
		itemDef_t item;
		item.parent = menu;
		Item_RunScript(&item, menu->onClose);
	}
}

void Menus_CloseByName ( const char *p )
{
	menuDef_t *menu = Menus_FindByName(p);

	// If the menu wasnt found just exit
	if (menu == NULL)
	{
		return;
	}

	// Run the close script for the menu
	Menu_RunCloseScript(menu);

	// If this window had the focus then take it away
	if ( menu->window.flags & WINDOW_HASFOCUS )
	{
		// If there is something still in the open menu list then
		// set it to have focus now
		if ( openMenuCount )
		{
			// Subtract one from the open menu count to prepare to
			// remove the top menu from the list
			openMenuCount -= 1;

			// Set the top menu to have focus now
			menuStack[openMenuCount]->window.flags |= WINDOW_HASFOCUS;

			// Remove the top menu from the list
			menuStack[openMenuCount] = NULL;
		}
	}

	// Window is now invisible and doenst have focus
	menu->window.flags &= ~(WINDOW_VISIBLE | WINDOW_HASFOCUS);
}

int FPMessageTime = 0;

void Menus_CloseAll()
{
	int i;

	g_waitingForKey = qfalse;

	for (i = 0; i < menuCount; i++)
	{
		Menu_RunCloseScript ( &Menus[i] );
		Menus[i].window.flags &= ~(WINDOW_HASFOCUS | WINDOW_VISIBLE);
	}

	// Clear the menu stack
	openMenuCount = 0;

	FPMessageTime = 0;
}

qboolean Script_Show(itemDef_t *item, char **args)
{
	const char *name;
	if (String_Parse(args, &name))
	{
		Menu_ShowItemByName((menuDef_t *) item->parent, name, qtrue);
	}
	return qtrue;
}

qboolean Script_Hide(itemDef_t *item, char **args)
{
	const char *name;
	if (String_Parse(args, &name))
	{
		Menu_ShowItemByName((menuDef_t *) item->parent, name, qfalse);
	}
	return qtrue;
}

qboolean Script_FadeIn(itemDef_t *item, char **args)
{
	const char *name;
	if (String_Parse(args, &name))
	{
		Menu_FadeItemByName((menuDef_t *) item->parent, name, qfalse);
	}

	return qtrue;
}

qboolean Script_FadeOut(itemDef_t *item, char **args)
{
	const char *name;
	if (String_Parse(args, &name))
	{
		Menu_FadeItemByName((menuDef_t *) item->parent, name, qtrue);
	}
	return qtrue;
}

qboolean Script_Open(itemDef_t *item, char **args)
{
	const char *name;
	if (String_Parse(args, &name))
	{
		Menus_OpenByName(name);
	}
	return qtrue;
}

qboolean Script_Close(itemDef_t *item, char **args)
{
	const char *name;
	if (String_Parse(args, &name))
	{
		if (Q_stricmp(name, "all") == 0)
		{
			Menus_CloseAll();
		}
		else
		{
			Menus_CloseByName(name);
		}
	}
	return qtrue;
}

void Menu_TransitionItemByName(menuDef_t *menu, const char *p, const rectDef_t *rectFrom, const rectDef_t *rectTo, int time, float amt)
{
	itemDef_t *item;
	int i;
	int count = Menu_ItemsMatchingGroup(menu, p);
	for (i = 0; i < count; i++)
	{
		item = Menu_GetMatchingItemByNumber(menu, i, p);
		if (item != NULL)
		{
			if (!rectFrom)
			{
				rectFrom = &item->window.rect;	//if there are more than one of these with the same name, they'll all use the FIRST one's FROM.
			}
			item->window.flags |= (WINDOW_INTRANSITION | WINDOW_VISIBLE);
			item->window.offsetTime = time;
			memcpy(&item->window.rectClient, rectFrom, sizeof(rectDef_t));
			memcpy(&item->window.rectEffects, rectTo, sizeof(rectDef_t));
			item->window.rectEffects2.x = fabs(rectTo->x - rectFrom->x) / amt;
			item->window.rectEffects2.y = fabs(rectTo->y - rectFrom->y) / amt;
			item->window.rectEffects2.w = fabs(rectTo->w - rectFrom->w) / amt;
			item->window.rectEffects2.h = fabs(rectTo->h - rectFrom->h) / amt;

			Item_UpdatePosition(item);
		}
	}
}

/*
=================
Menu_Transition3ItemByName
=================
*/
//JLF
#define _TRANS3
#ifdef _TRANS3
void Menu_Transition3ItemByName(menuDef_t *menu, const char *p, const float minx, const float miny, const float minz,
								const float maxx, const float maxy, const float maxz, const float fovtx, const float fovty,
								const int time, const float amt)
{
	itemDef_t *item;
	int i;
	int count = Menu_ItemsMatchingGroup(menu, p);
	modelDef_t  * modelptr;
	for (i = 0; i < count; i++)
	{
		item = Menu_GetMatchingItemByNumber(menu, i, p);
		if (item != NULL)
		{
			if ( item->type == ITEM_TYPE_MODEL )
			{
				modelptr = item->typeData.model;

				item->window.flags |= (WINDOW_INTRANSITIONMODEL | WINDOW_VISIBLE);
				item->window.offsetTime = time;
				modelptr->fov_x2 = fovtx;
				modelptr->fov_y2 = fovty;
				VectorSet(modelptr->g2maxs2, maxx, maxy, maxz);
				VectorSet(modelptr->g2mins2, minx, miny, minz);

//				//modelptr->g2maxs2.x= maxx;
//				modelptr->g2maxs2.y= maxy;
//				modelptr->g2maxs2.z= maxz;
//				modelptr->g2mins2.x= minx;
//				modelptr->g2mins2.y= miny;
//				modelptr->g2mins2.z= minz;

//				VectorSet(modelptr->g2maxs2, maxx, maxy, maxz);

				modelptr->g2maxsEffect[0] = fabs(modelptr->g2maxs2[0] - modelptr->g2maxs[0]) / amt;
				modelptr->g2maxsEffect[1] = fabs(modelptr->g2maxs2[1] - modelptr->g2maxs[1]) / amt;
				modelptr->g2maxsEffect[2] = fabs(modelptr->g2maxs2[2] - modelptr->g2maxs[2]) / amt;

				modelptr->g2minsEffect[0] = fabs(modelptr->g2mins2[0] - modelptr->g2mins[0]) / amt;
				modelptr->g2minsEffect[1] = fabs(modelptr->g2mins2[1] - modelptr->g2mins[1]) / amt;
				modelptr->g2minsEffect[2] = fabs(modelptr->g2mins2[2] - modelptr->g2mins[2]) / amt;


				modelptr->fov_Effectx = fabs(modelptr->fov_x2 - modelptr->fov_x) / amt;
				modelptr->fov_Effecty = fabs(modelptr->fov_y2 - modelptr->fov_y) / amt;
			}
		}
	}
}

#endif

#define MAX_DEFERRED_SCRIPT		MAX_SCRIPT_CHARS

char		ui_deferredScript [ MAX_DEFERRED_SCRIPT ];
itemDef_t*	ui_deferredScriptItem = NULL;

/*
=================
Script_Defer

Defers the rest of the script based on the defer condition.  The deferred
portion of the script can later be run with the "rundeferred"
=================
*/
qboolean Script_Defer ( itemDef_t* item, char **args )
{
	// Should the script be deferred?
	if ( DC->deferScript ( (char**)args ) )
	{
		// Need the item the script was being run on
		ui_deferredScriptItem = item;

		// Save the rest of the script
		Q_strncpyz ( ui_deferredScript, *args, MAX_DEFERRED_SCRIPT );

		// No more running
		return qfalse;
	}

	// Keep running the script, its ok
	return qtrue;
}

/*
=================
Script_RunDeferred

Runs the last deferred script, there can only be one script deferred at a
time so be careful of recursion
=================
*/
qboolean Script_RunDeferred ( itemDef_t* item, char **args )
{
	// Make sure there is something to run.
	if ( !ui_deferredScript[0] || !ui_deferredScriptItem )
	{
		return qtrue;
	}

	// Run the deferred script now
	Item_RunScript ( ui_deferredScriptItem, ui_deferredScript );

	return qtrue;
}

qboolean Script_Transition(itemDef_t *item, char **args)
{
	const char *name;
	rectDef_t rectFrom, rectTo;
	int time;
	float amt;

	if (String_Parse(args, &name))
	{
	    if ( Rect_Parse(args, &rectFrom) && Rect_Parse(args, &rectTo) && Int_Parse(args, &time) && Float_Parse(args, &amt))
		{
			Menu_TransitionItemByName((menuDef_t *) item->parent, name, &rectFrom, &rectTo, time, amt);
		}
	}

	return qtrue;
}

void Menu_OrbitItemByName(menuDef_t *menu, const char *p, float x, float y, float cx, float cy, int time)
{
	itemDef_t *item;
	int i;
	int count = Menu_ItemsMatchingGroup(menu, p);
	for (i = 0; i < count; i++) {
		item = Menu_GetMatchingItemByNumber(menu, i, p);
		if (item != NULL) {
			item->window.flags |= (WINDOW_ORBITING | WINDOW_VISIBLE);
			item->window.offsetTime = time;
			item->window.rectEffects.x = cx;
			item->window.rectEffects.y = cy;
			item->window.rectClient.x = x;
			item->window.rectClient.y = y;
			Item_UpdatePosition(item);
		}
	}
}

void Menu_ItemDisable(menuDef_t *menu, const char *name, qboolean disableFlag)
{
	int	j,count;
	itemDef_t *itemFound;

	count = Menu_ItemsMatchingGroup(menu, name);
	// Loop through all items that have this name
	for (j = 0; j < count; j++)
	{
		itemFound = Menu_GetMatchingItemByNumber( menu, j, name);
		if (itemFound != NULL)
		{
			itemFound->disabled = disableFlag;
			// Just in case it had focus
			itemFound->window.flags &= ~WINDOW_MOUSEOVER;
		}
	}
}

// Set item disable flags
qboolean Script_Disable(itemDef_t *item, char **args)
{
	char *name;
	int	value;
	menuDef_t *menu;

	if (String_Parse(args, (const char **)&name))
	{
		char buff[1024];

		// Is is specifying a cvar to get the item name from?
		if (name[0] == '*')
		{
			name += 1;
		    DC->getCVarString(name, buff, sizeof(buff));
			name = buff;
		}

		if ( Int_Parse(args, &value))
		{
			menu = Menu_GetFocused();
			Menu_ItemDisable(menu, name,value);
		}
	}

	return qtrue;
}

// Scale the given item instantly.
qboolean Script_Scale(itemDef_t *item, char **args)
{
	const char *name;
	float scale;
	int	j,count;
	itemDef_t *itemFound;
	rectDef_t rectTo;

	if (String_Parse(args, &name))
	{
		char buff[1024];

		// Is is specifying a cvar to get the item name from?
		if (name[0] == '*')
		{
			name += 1;
		    DC->getCVarString(name, buff, sizeof(buff));
			name = buff;
		}

		count = Menu_ItemsMatchingGroup((menuDef_t *) item->parent, name);

		if ( Float_Parse(args, &scale))
		{
			for (j = 0; j < count; j++)
			{
				itemFound = Menu_GetMatchingItemByNumber( (menuDef_t *) item->parent, j, name);
				if (itemFound != NULL)
				{
					rectTo.h = itemFound->window.rect.h * scale;
					rectTo.w = itemFound->window.rect.w * scale;

					rectTo.x = itemFound->window.rect.x + ((itemFound->window.rect.h - rectTo.h)/2);
					rectTo.y = itemFound->window.rect.y + ((itemFound->window.rect.w - rectTo.w)/2);

					Menu_TransitionItemByName((menuDef_t *) item->parent, name, 0, &rectTo, 1, 1);
				}
			}
		}
	}

	return qtrue;
}

qboolean Script_Orbit(itemDef_t *item, char **args)
{
	const char *name;
	float cx, cy, x, y;
	int time;

	if (String_Parse(args, &name))
	{
		if ( Float_Parse(args, &x) && Float_Parse(args, &y) && Float_Parse(args, &cx) && Float_Parse(args, &cy) && Int_Parse(args, &time) )
		{
			Menu_OrbitItemByName((menuDef_t *) item->parent, name, x, y, cx, cy, time);
		}
	}

	return qtrue;
}

qboolean Script_SetFocus(itemDef_t *item, char **args)
{
	const char *name;
	itemDef_t *focusItem;

	if (String_Parse(args, &name)) {
		focusItem = Menu_FindItemByName((menuDef_t *) item->parent, name);
		if (focusItem && !(focusItem->window.flags & WINDOW_DECORATION) && !(focusItem->window.flags & WINDOW_HASFOCUS)) {
			Menu_ClearFocus((menuDef_t *) item->parent);
			focusItem->window.flags |= WINDOW_HASFOCUS;


			if (focusItem->onFocus) {
				Item_RunScript(focusItem, focusItem->onFocus);
			}
			if (DC->Assets.itemFocusSound) {
				DC->startLocalSound( DC->Assets.itemFocusSound, CHAN_LOCAL_SOUND );
			}
		}
	}

	return qtrue;
}

qboolean Script_SetPlayerModel(itemDef_t *item, char **args)
{
	const char *name;
	if (String_Parse(args, &name))
	{
		DC->setCVar("model", name);
	}

	return qtrue;
}

/*
=================
ParseRect
=================
*/
qboolean ParseRect(const char **p, rectDef_t *r)
{
	if (!COM_ParseFloat(p, &r->x))
	{
		if (!COM_ParseFloat(p, &r->y))
		{
			if (!COM_ParseFloat(p, &r->w))
			{
				if (!COM_ParseFloat(p, &r->h))
				{
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

/*
=================
Script_Transition2
uses current origin instead of specifing a starting origin

transition2		lfvscr		25 0 202 264  20 25
=================
*/
qboolean Script_Transition2(itemDef_t *item, char **args)
{
	const char *name;
	rectDef_t rectTo;
	int time;
	float amt;

	if (String_Parse(args, &name))
	{
		if ( ParseRect((const char **) args, &rectTo) && Int_Parse(args, &time) && !COM_ParseFloat((const char **) args, &amt))
		{
			Menu_TransitionItemByName((menuDef_t *) item->parent, name, 0, &rectTo, time, amt);
		}
		else
		{
			Com_Printf(S_COLOR_YELLOW"WARNING: Script_Transition2: error parsing '%s'\n", name );
		}
	}

	return qtrue;
}

#ifdef _TRANS3
/*
JLF
=================
Script_Transition3

used exclusively with model views
uses current origin instead of specifing a starting origin


transition3		lfvscr		(min extent) (max extent) (fovx,y)  20 25
=================
*/
qboolean Script_Transition3(itemDef_t *item, char **args)
{
	const char *name = NULL, *value = NULL;
	float minx, miny, minz, maxx, maxy, maxz, fovtx, fovty;
	int time;
	float amt;

	if (String_Parse(args, &name))
	{
		if (String_Parse( args, &value))
		{
			minx = atof(value);
			if (String_Parse( args, &value))
			{
				miny = atof(value);
				if (String_Parse( args, &value))
				{
					minz = atof(value);
					if (String_Parse( args, &value))
					{
						maxx = atof(value);
						if (String_Parse( args, &value))
						{
							maxy = atof(value);
							if (String_Parse( args, &value))
							{
								maxz = atof(value);
								if (String_Parse( args, &value))
								{
									fovtx = atof(value);
									if (String_Parse( args, &value))
									{
										fovty = atof(value);

										if (String_Parse( args, &value))
										{
											time = atoi(value);
											if (String_Parse( args, &value))
											{
												amt = atof(value);
												//set up the variables
												Menu_Transition3ItemByName((menuDef_t *) item->parent,
														name,
														minx, miny, minz,
														maxx, maxy, maxz,
														fovtx, fovty,
														time, amt);

												return qtrue;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	if ( name ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Script_Transition2: error parsing '%s'\n", name );
	}
	return qtrue;
}
#endif

qboolean Script_SetCvar(itemDef_t *item, char **args)
{
	const char *cvar, *val;
	if (String_Parse(args, &cvar) && String_Parse(args, &val))
	{
		DC->setCVar(cvar, val);
	}
	return qtrue;
}

qboolean Script_SetCvarToCvar(itemDef_t *item, char **args) {
	const char *cvar, *val;
	if (String_Parse(args, &cvar) && String_Parse(args, &val)) {
		char cvarBuf[1024];
		DC->getCVarString(val, cvarBuf, sizeof(cvarBuf));
		DC->setCVar(cvar, cvarBuf);
	}
	return qtrue;
}

qboolean Script_Exec(itemDef_t *item, char **args) {
	const char *val;
	if (String_Parse(args, &val)) {
		DC->executeText(EXEC_APPEND, va("%s ; ", val));
	}
	return qtrue;
}

qboolean Script_Play(itemDef_t *item, char **args) {
	const char *val;
	if (String_Parse(args, &val)) {
		DC->startLocalSound(DC->registerSound(val), CHAN_AUTO);
	}
	return qtrue;
}

qboolean Script_playLooped(itemDef_t *item, char **args) {
	const char *val;
	if (String_Parse(args, &val)) {
		DC->stopBackgroundTrack();
		DC->startBackgroundTrack(val, val, qfalse);
	}
	return qtrue;
}

/*
=================
Vision_Text

A text of this tree in the language of the game, for the cgame (CG_VisionText) and the ui. It is asked for by its
English, tools/menus/texts.py has what it is in the other languages (its list CGAME) and writes it into
strings/<language>/vision.str under a key that it makes from the English, as this does. Without such a string:
the English text itself.
=================
*/
const char *Vision_Text( const char *english ) {
	static char	text[8][256];
	static int	index;
	char		*out = text[index++ & 7];
	char		key[256], found[256], value[8];
	const char	*in;
	qboolean	gap = qfalse;
	int			n = 0;

	// capitals and digits, one _ for whatever is between them, 36 letters at most, no _ at either end
	for ( in = english; *in && n < (int)sizeof( key ) - 2; in++ ) {
		const char c = ( *in >= 'a' && *in <= 'z' ) ? (char)( *in - 'a' + 'A' ) : *in;

		if ( ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ) {
			if ( gap && n > 0 ) {
				key[n++] = '_';
			}
			gap = qfalse;
			key[n++] = c;
		} else {
			gap = qtrue;
		}
	}
	n = Q_min( n, 36 );
	while ( n > 0 && key[n - 1] == '_' ) {
		n--;
	}
	key[n] = '\0';

	// (for a reference it does not have the engine writes "??" and says no: the pack of this client is older
	// than its cgame, or the pack of a mod took its place)
	found[0] = '\0';
	if ( n <= 0 || !trap->SE_GetStringTextString( va( "VISION_%s", key ), found, sizeof( found ) ) || !found[0] ) {
		Q_strncpyz( out, english, sizeof( text[0] ) );
		return out;
	}
	// (the files of the strings are in the code page of the game)
	trap->Cvar_VariableStringBuffer( "v_utf8Text", value, sizeof( value ) );
	if ( atoi( value ) == 1 ) {
		Q_UTF8_FromCP1252( found, out, sizeof( text[0] ) );
	} else {
		Q_strncpyz( out, found, sizeof( text[0] ) );
	}
	return out;
}

// setpage <n>: opens that page of the menu (the pages of an options screen, "tab" and "page" of its items)
qboolean Script_SetPage( itemDef_t *item, char **args ) {
	int page;

	if ( Int_Parse( args, &page ) ) {
		Menu_SetPage( (menuDef_t *)item->parent, page );
	}
	return qtrue;
}

commandDef_t commandList[] =
{
  {"fadein", &Script_FadeIn},                   // group/name
  {"fadeout", &Script_FadeOut},                 // group/name
  {"show", &Script_Show},                       // group/name
  {"hide", &Script_Hide},                       // group/name
  {"setcolor", &Script_SetColor},               // works on this
  {"open", &Script_Open},                       // nenu
  {"close", &Script_Close},                     // menu
  {"setasset", &Script_SetAsset},               // works on this
  {"setbackground", &Script_SetBackground},     // works on this
  {"setitemrectcvar", &Script_SetItemRectCvar},			// group/name
  {"setitembackground", &Script_SetItemBackground},// group/name
  {"setitemtext", &Script_SetItemText},			// group/name
  {"setitemcolor", &Script_SetItemColor},       // group/name
  {"setitemcolorcvar", &Script_SetItemColorCvar},// group/name
  {"setitemrect", &Script_SetItemRect},			// group/name
  {"setteamcolor", &Script_SetTeamColor},       // sets this background color to team color
  {"setfocus", &Script_SetFocus},               // sets focus
  {"setplayermodel", &Script_SetPlayerModel},   // sets model
  {"transition", &Script_Transition},           // group/name
  {"setcvar", &Script_SetCvar},					// name
  {"setcvartocvar", &Script_SetCvarToCvar},     // name
  {"exec", &Script_Exec},						// group/name
  {"play", &Script_Play},						// group/name
  {"playlooped", &Script_playLooped},           // group/name
  {"orbit", &Script_Orbit},                     // group/name
  {"scale",			&Script_Scale},				// group/name
  {"disable",		&Script_Disable},			// group/name
  {"defer",			&Script_Defer},				//
  {"rundeferred",	&Script_RunDeferred},		//
  {"transition2",	&Script_Transition2},			// group/name
  {"setpage",		&Script_SetPage},			// number of the page
};

// Set all the items within a given menu, with the given itemName, to the given shader
void Menu_SetItemBackground(const menuDef_t *menu,const char *itemName, const char *background)
{
	itemDef_t	*item;
	int			j, count;

	if (!menu)	// No menu???
	{
		return;
	}

	count = Menu_ItemsMatchingGroup( (menuDef_t *) menu, itemName);

	for (j = 0; j < count; j++)
	{
		item = Menu_GetMatchingItemByNumber( (menuDef_t *) menu, j, itemName);
		if (item != NULL)
		{
			item->window.background = DC->registerShaderNoMip(background);
		}
	}
}

// Set all the items within a given menu, with the given itemName, to the given text
void Menu_SetItemText(const menuDef_t *menu,const char *itemName, const char *text)
{
	itemDef_t	*item;
	int			j, count;

	if (!menu)	// No menu???
	{
		return;
	}

	count = Menu_ItemsMatchingGroup( (menuDef_t *) menu, itemName);

	for (j = 0; j < count; j++)
	{
		item = Menu_GetMatchingItemByNumber( (menuDef_t *) menu, j, itemName);
		if (item != NULL)
		{
			if (text[0] == '*')
			{
				item->text = NULL;		// Null this out because this would take presidence over cvar text.
				item->cvar = text+1;
				switch ( item->type )
				{
					case ITEM_TYPE_EDITFIELD:
					case ITEM_TYPE_NUMERICFIELD:
					case ITEM_TYPE_YESNO:
					case ITEM_TYPE_BIND:
					case ITEM_TYPE_SLIDER:
					case ITEM_TYPE_TEXT:
					{
						if ( item->typeData.edit )
						{
							item->typeData.edit->minVal = -1;
							item->typeData.edit->maxVal = -1;
							item->typeData.edit->defVal = -1;
						}
						break;
					}
					default:
						break;
				}
			}
			else
			{
				item->text = text;
				if ( item->type == ITEM_TYPE_TEXTSCROLL )
				{
					textScrollDef_t *scrollPtr = item->typeData.textscroll;
					if ( scrollPtr )
					{
						scrollPtr->startPos = 0;
						scrollPtr->endPos = 0;
					}

					Item_TextScroll_BuildLines ( item );
				}
			}
		}
	}
}

int scriptCommandCount = sizeof(commandList) / sizeof(commandDef_t);

void Item_RunScript(itemDef_t *item, const char *s)
{
	char script[MAX_SCRIPT_CHARS], *p;
	int i;
	qboolean bRan;

	script[0] = 0;

	if (item && s && s[0])
	{
		Q_strcat(script, sizeof(script), s);
		p = script;

		while (1)
		{
			const char *command;

			// expect command then arguments, ; ends command, NULL ends script
			if (!String_Parse(&p, &command))
			{
				return;
			}

			if (command[0] == ';' && command[1] == '\0')
			{
				continue;
			}

			bRan = qfalse;
			for (i = 0; i < scriptCommandCount; i++)
			{
				if (Q_stricmp(command, commandList[i].name) == 0)
				{
					// Allow a script command to stop processing the script
					if ( !commandList[i].handler(item, &p) )
					{
						return;
					}

					bRan = qtrue;
					break;
				}
			}

			// not in our auto list, pass to handler
			if (!bRan)
			{
				DC->runScript(&p);
			}
		}
	}
}


qboolean Item_EnableShowViaCvar(itemDef_t *item, int flag) {
  char script[2048], *p;
  if (item && item->enableCvar && *item->enableCvar && item->cvarTest && *item->cvarTest) {
		char buff[2048];
	  DC->getCVarString(item->cvarTest, buff, sizeof(buff));

    Q_strncpyz(script, item->enableCvar, 2048);
    p = script;
    while (1) {
      const char *val;
      // expect value then ; or NULL, NULL ends list
      if (!String_Parse(&p, &val)) {
				return (item->cvarFlags & flag) ? qfalse : qtrue;
      }

      if (val[0] == ';' && val[1] == '\0') {
        continue;
      }

			// enable it if any of the values are true
			if (item->cvarFlags & flag) {
        if (Q_stricmp(buff, val) == 0) {
					return qtrue;
				}
			} else {
				// disable it if any of the values are true
        if (Q_stricmp(buff, val) == 0) {
					return qfalse;
				}
			}

    }
		return (item->cvarFlags & flag) ? qfalse : qtrue;
  }
	return qtrue;
}


/*
===============================================================================

Pages, lists and rows (VisionJK: the options screens of the new look, docs/japlus/ui.md)

A menu file can put its items on pages ("page <n>") that buttons open ("tab <n>"), the settings of a page into a
list ("scroll 1") that may be longer than its window ("scrollRect" of the menu), and have a setting drawn as a
row of that list ("row 1"): its name at the left, its widget at the right. The wheel, the bar beside the list
(ITEM_TYPE_PAGESCROLL) and the keys move the list. A menu file without these words has none of it.

===============================================================================
*/

// a card of a list in a menu with a canvas (tools/menus/canvas.py CARD_*): its line ends 56 pixels under its top, 10
// to its first row, 10 under its last
#define CARD_LINE_PX	55.0f		// where the line under the head of a card is
#define CARD_HEAD_PX	66.0f		// from the top of a card to its first row
#define CARD_BOTTOM_PX	10.0f		// from its last row to its bottom
#define CARD_GAP_PX		24.0f		// between two cards of a column (tools/menus/canvas.py CARD_GAP)
#define ROW_PAD			8.0f		// from the edge of a row to its name and to its widget
#define ROW_WIDGET		0.31f		// the part of the width of a row its widget has
#define ROW_WIDGET_WIDE	0.56f		// "row 3": a line to type a name in
#define ROW_NUMBER		31.0f		// in front of the track of a slider: what it stands at
#define ROW_ARROW		8.0f		// how high the arrows of a choice are
#define ROW_SMALL		0.88f		// the size of the words of a widget, in that of the name of its row (0.76 was hard to
								// read on the menu in the level, 2026-11-02)
#define TAB_PAD			11.0f		// beside the word of the button of a page
#define LIST_GLIDE_MSEC	35.0f		// the list covers most of its way to a new place in about three times this

void Item_MouseLeave( itemDef_t *item );
void Item_SetMouseOver( itemDef_t *item, qboolean focus );

static qboolean Menu_HasList( const menuDef_t *menu ) {
	return (qboolean)( menu->scrollRect.h > 0.0f && menu->page >= 0 && menu->page < MENU_PAGES );
}

static float Menu_ListStep( const menuDef_t *menu ) {
	return menu->scrollStep > 0.0f ? menu->scrollStep : 20.0f;
}

// the window of the list of a menu, on the screen
static void Menu_ListWindow( const menuDef_t *menu, rectDef_t *out ) {
	*out = menu->scrollRect;
	out->x += menu->window.rect.x;
	out->y += menu->window.rect.y;
	if ( menu->window.border != 0 ) {
		out->x += menu->window.borderSize;
		out->y += menu->window.borderSize;
	}
}

// Is this item on a page that is not open. A row the list has folded away or that is no match of the search is
// on none (Menu_ListLayout), and while the search is on, the rows of every page are in its list.

static qboolean Item_OffPage( const itemDef_t *item ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	if ( !menu ) {
		return qfalse;
	}
	if ( item->listOut ) {
		return qtrue;
	}
	if ( item->scroll && menu->searching ) {
		return qfalse;
	}
	if ( item->page == PAGE_SEARCH ) {
		// "page 15", the last one: only while a search is on (its title)
		return (qboolean)!menu->searching;
	}
	return (qboolean)( item->page && item->page != menu->page );
}

// how long the list of the open page is: from the top of its window to the end of its last row
static float Menu_ListLength( const menuDef_t *menu ) {
	float	end = 0.0f;
	int		i;

	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t *item = menu->items[i];

		if ( item->scroll && !Item_OffPage( item ) && ( item->window.flags & WINDOW_VISIBLE ) ) {
			end = Q_max( end, item->window.rectClient.y + item->window.rectClient.h - menu->scrollRect.y );
		}
	}
	return end;
}

// how far the list can be moved up: until its end stands at the end of its window, in whole rows
static float Menu_ListMost( const menuDef_t *menu ) {
	const float over = Menu_ListLength( menu ) - menu->scrollRect.h;

	return over <= 0.5f ? 0.0f : ceilf( over / Menu_ListStep( menu ) - 0.01f ) * Menu_ListStep( menu );
}

// Is this item not there right now: on a page that is not open, a row of the list outside of its window, or the
// bar of a list that needs none
static qboolean Item_Away( const itemDef_t *item ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	if ( !menu || ( !item->page && !item->scroll && item->type != ITEM_TYPE_PAGESCROLL ) ) {
		return qfalse;
	}
	if ( Item_OffPage( item ) ) {
		return qtrue;
	}
	if ( !Menu_HasList( menu ) ) {
		return qfalse;
	}
	if ( item->type == ITEM_TYPE_PAGESCROLL ) {
		return (qboolean)( Menu_ListMost( menu ) <= 0.0f );
	}
	if ( item->scroll ) {
		rectDef_t	list;
		float		top = item->window.rect.y;

		Menu_ListWindow( menu, &list );
		if ( item->fold && menu->canvasW > 0.0f ) {
			// (a heading under another card has the gap above its card in it, see Menu_ListCards: what shows of
			// it begins at the card)
			top = Q_max( top, item->window.rect.y + item->window.rect.h - CARD_HEAD_PX * menu->canvasUnit );
		}
		return (qboolean)( top < list.y - 0.5f || item->window.rect.y + item->window.rect.h > list.y + list.h + 0.5f );
	}
	return qfalse;
}

// the rows of the list to where the list is shown (Item_SetScreenCoords)
static void Menu_ListPlace( menuDef_t *menu ) {
	int i;

	for ( i = 0; i < menu->itemCount; i++ ) {
		if ( menu->items[i]->scroll ) {
			Item_UpdatePosition( menu->items[i] );
		}
	}
}

// Moves the list of the open page up by `to` units from its start, no further than it is long. jump: at once,
// else it glides there (Menu_ScrollFrame).
static void Menu_ScrollTo( menuDef_t *menu, float to, qboolean jump ) {
	if ( !Menu_HasList( menu ) ) {
		return;
	}
	to = Com_Clamp( 0.0f, Menu_ListMost( menu ), to );
	menu->scrollTo[menu->page] = to;
	if ( jump && menu->scrollShown != to ) {
		menu->scrollShown = to;
		Menu_ListPlace( menu );
	}
}

static void Menu_ScrollBy( menuDef_t *menu, float rows ) {
	if ( Menu_HasList( menu ) ) {
		Menu_ScrollTo( menu, menu->scrollTo[menu->page] + rows * Menu_ListStep( menu ), qfalse );
	}
}

// Brings a row into the window of its list, for the keys that walk through all rows. Upwards one row more
// comes with it: the heading of a part stands above its first row.
static void Menu_ScrollIntoView( menuDef_t *menu, const itemDef_t *item ) {
	const float	step = Menu_ListStep( menu );
	const float	top = item->window.rectClient.y - menu->scrollRect.y, bottom = top + item->window.rectClient.h;
	float		to;

	if ( !Menu_HasList( menu ) ) {
		return;
	}
	to = menu->scrollTo[menu->page];
	if ( top < to ) {
		to = ( floorf( top / step + 0.01f ) - 1.0f ) * step;
	} else if ( bottom > to + menu->scrollRect.h ) {
		to = ceilf( ( bottom - menu->scrollRect.h ) / step - 0.01f ) * step;
	}
	Menu_ScrollTo( menu, to, qtrue );
}

// Once a frame: the list of the open page glides to where it is going, fast at first. Where it is going is
// looked at again as well: the list is shorter after a row was hidden.
static void Menu_ScrollFrame( menuDef_t *menu ) {
	float to;

	if ( !Menu_HasList( menu ) ) {
		return;
	}
	Menu_ScrollTo( menu, menu->scrollTo[menu->page], qfalse );
	to = menu->scrollTo[menu->page];
	if ( menu->scrollShown == to ) {
		return;
	}
	if ( DC->frameTime <= 0 || fabsf( to - menu->scrollShown ) < 0.5f ) {
		menu->scrollShown = to;
	} else {
		menu->scrollShown += ( to - menu->scrollShown ) * Com_Clamp( 0.0f, 1.0f, DC->frameTime / LIST_GLIDE_MSEC );
	}
	Menu_ListPlace( menu );
	if ( menu->scrollShown == to && !itemCapture ) {
		// another row is under the mouse now
		Menu_HandleMouseMove( menu, DC->cursorx, DC->cursory );
	}
}

// the bar of the list of a page, where it is and how long its thumb is. False: the list needs none.
static qboolean Item_PageScroll_Thumb( const itemDef_t *item, float *start, float *part ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const float		most = Menu_ListMost( menu );

	if ( !Menu_HasList( menu ) || most <= 0.0f ) {
		return qfalse;
	}
	*part = menu->scrollRect.h / ( menu->scrollRect.h + most );
	*start = ( menu->scrollShown / most ) * ( 1.0f - *part );
	return qtrue;
}

// while the mouse holds the bar: the list follows it
static void Scroll_PageScroll_ThumbFunc( void *p ) {
	scrollInfo_t	*si = (scrollInfo_t *)p;
	menuDef_t		*menu = (menuDef_t *)si->item->parent;
	const rectDef_t	*r = &si->item->window.rect;
	const float		most = Menu_ListMost( menu );
	float			start, part;

	if ( !Item_PageScroll_Thumb( si->item, &start, &part ) || part >= 1.0f ) {
		return;
	}
	// the middle of the thumb under the mouse, in whole rows
	start = Com_Clamp( 0.0f, 1.0f, ( ( DC->cursory - r->y ) / r->h - part * 0.5f ) / ( 1.0f - part ) );
	Menu_ScrollTo( menu, floorf( start * most / Menu_ListStep( menu ) + 0.5f ) * Menu_ListStep( menu ), qfalse );
}

// Opens a page of a menu. What had the mouse or the focus on the page that goes loses it.
// page 0: the one of the search (Menu_ListLayout), which no item names
static void Menu_OpenPage( menuDef_t *menu, int page ) {
	int i;

	menu->page = page;
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *item = menu->items[i];

		if ( !Item_OffPage( item ) ) {
			continue;
		}
		if ( item->window.flags & WINDOW_MOUSEOVER ) {
			Item_MouseLeave( item );
			Item_SetMouseOver( item, qfalse );
		}
		if ( item->window.flags & WINDOW_HASFOCUS ) {
			item->window.flags &= ~WINDOW_HASFOCUS;
			if ( item->leaveFocus ) {
				Item_RunScript( item, item->leaveFocus );
			}
		}
	}
	// (the list of this page is where it was left)
	menu->scrollShown = -1.0f;
	Menu_ScrollTo( menu, menu->scrollTo[page], qtrue );
	Menu_HandleMouseMove( menu, DC->cursorx, DC->cursory );
}

void Menu_SetPage( menuDef_t *menu, int page ) {
	if ( !menu || page <= 0 || page >= MENU_PAGES || menu->page == page ) {
		return;
	}
	// (a button of a page ends the search, Item_Action)
	Menu_OpenPage( menu, page );
}

static qboolean Item_IsRow( const itemDef_t *item ) {
	return (qboolean)( item->row && Item_Skinned( item ) );
}

// can this item not be used right now
static qboolean Item_Off( itemDef_t *item ) {
	return (qboolean)( item->disabled
		|| ( ( item->cvarFlags & ( CVAR_ENABLE | CVAR_DISABLE ) ) && !Item_EnableShowViaCvar( item, CVAR_ENABLE ) ) );
}

// where the widget of a row is: at its right end, on the middle of its height
// a size of a row: in a menu with a canvas the pixels of the design (Setup of the redesign: rows 52 high, widgets
// 220 x 40, 12 from the edge), else the units the rows of the 640 x 480 menus had
static float Item_RowSize( const itemDef_t *item, float pixels, float units ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	return ( menu && menu->canvasW > 0.0f ) ? pixels * menu->canvasUnit : units;
}

#define BIND_SLOT_W		190.0f		// a key of a row of keys in a menu with a canvas, in pixels of the canvas
#define BIND_SLOT_GAP	16.0f

static void Item_Row_Widget( const itemDef_t *item, rectDef_t *out ) {
	const rectDef_t *r = &item->window.rect;
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	if ( menu && menu->canvasW > 0.0f && ( !item->text || !item->text[0] ) ) {
		// (a row without a name is its widget alone: the switch between the settings that matter and all of them)
		*out = *r;
		return;
	}
	if ( menu && menu->canvasW > 0.0f && item->type == ITEM_TYPE_BIND ) {
		// (two keys side by side, KEY and SECOND KEY, see Item_Bind_Slot)
		out->w = floorf( Q_min( ( 2.0f * BIND_SLOT_W + BIND_SLOT_GAP ) * menu->canvasUnit, r->w * 0.6f ) );
		out->h = Q_min( 38.0f * menu->canvasUnit, r->h - 4.0f * menu->canvasUnit );
	} else if ( menu && menu->canvasW > 0.0f ) {
		// (220 x 40 in a row 52 high as the board, in proportion in rows of other heights: the panels in a game)
		out->h = Q_min( r->h * ( 40.0f / 52.0f ), r->h - 4.0f * menu->canvasUnit );
		out->w = floorf( item->row == 3 ? r->w * ROW_WIDGET_WIDE : Q_min( 220.0f / 40.0f * out->h, r->w * 0.5f ) );
	} else {
		out->w = floorf( r->w * ( item->row == 3 ? ROW_WIDGET_WIDE : ROW_WIDGET ) );
		out->h = Q_min( 16.0f, r->h - 6.0f );
	}
	out->x = r->x + r->w - Item_RowSize( item, 12.0f, ROW_PAD ) - out->w;
	// (on the middle between the line over the row and its own line, a unit over its end, as its name: Row_Name)
	out->y = r->y + ( r->h - 1.0f - out->h ) * 0.5f;
}

// A row of keys in a menu with a canvas shows the two keys of its command in two fields, KEY and SECOND KEY, and
// each of them is set on its own.
static qboolean Item_Bind_Slots( const itemDef_t *item ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	return (qboolean)( item->type == ITEM_TYPE_BIND && menu && menu->canvasW > 0.0f && item->text && item->text[0] );
}

// the field of the first (0) or the second key (1) of such a row
static void Item_Bind_Slot( const itemDef_t *item, int slot, rectDef_t *out ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const float		gap = BIND_SLOT_GAP * menu->canvasUnit;
	rectDef_t		widget;

	Item_Row_Widget( item, &widget );
	out->w = ( widget.w - gap ) * 0.5f;
	out->h = widget.h;
	out->x = widget.x + slot * ( out->w + gap );
	out->y = widget.y;
}

// where the track of a slider starts and how long it is: behind its label, or in the widget of a row
static float Item_Slider_Track( itemDef_t *item, float *width ) {
	if ( Item_IsRow( item ) ) {
		rectDef_t widget;

		// (the track first, what it stands at after it, as in the other widgets the value is at the right)
		Item_Row_Widget( item, &widget );
		*width = widget.w - Item_RowSize( item, 70.0f, ROW_NUMBER + 8.0f );
		return widget.x + Item_RowSize( item, 8.0f, 4.5f );
	}
	*width = SLIDER_WIDTH;
	return item->text ? item->textRect.x + item->textRect.w + 8 : item->window.rect.x;
}

// A text in capital letters, in place. Only where that is safe: text in UTF-8, or without a byte above 127 (a
// byte of a letter of two bytes in another encoding may look like a small letter).
static void Text_Caps( char *text ) {
	const char	*c;
	byte		*u;

	for ( c = text; *c && !( *c & 0x80 ); c++ )
		;
	if ( *c && !DC->getCVarValue( "v_utf8Text" ) ) {
		return;
	}
	Q_strupr( text );
	// (And the small letters with marks, a with grave to thorn without the division sign: the capital is 32
	// lower. In UTF-8 they are two bytes, 0xC3 and the letter. The texts of the game itself come in
	// Windows-1252, where they are one byte, which is no start of a UTF-8 letter unless bytes of its inside
	// follow.)
	for ( u = (byte *)text; *u; u++ ) {
		if ( u[0] == 0xC3 && u[1] >= 0xA0 && u[1] <= 0xBE && u[1] != 0xB7 ) {
			u[1] -= 0x20;
			u++;
		} else if ( u[0] >= 0xE0 && u[0] <= 0xFE && u[0] != 0xF7 && ( u[1] & 0xC0 ) != 0x80 ) {
			u[0] -= 0x20;
		}
	}
}

// A text of a menu file as it is drawn, in a buffer: a reference into the texts of the game ("@MENUS_YES") is
// looked up. label: the colon a name of a setting ends with in the old menus goes.
static void Text_Word( const char *text, char *out, int size, qboolean caps, qboolean label ) {
	if ( !text ) {
		text = "";
	}
	if ( *text == '@' ) {
		trap->SE_GetStringTextString( &text[1], out, size );
	} else {
		Q_strncpyz( out, text, size );
	}
	if ( label ) {
		int len = (int)strlen( out );

		// (with the space some languages set in front of it, which may be one that does not break)
		while ( len > 0 && ( out[len - 1] == ':' || out[len - 1] == ' ' || (byte)out[len - 1] == 0xA0 ) ) {
			if ( (byte)out[len - 1] == 0xA0 && len > 1 && (byte)out[len - 2] == 0xC2 ) {
				len--;
			}
			out[--len] = '\0';
		}
	}
	if ( caps ) {
		Text_Caps( out );
	}
}

/*
=================
Menu_ListLayout

The lists of a menu with "searchCvar" or "foldCvar", laid out anew. A list is made of lines: the items the menu
file put at the same height of the same page (a row, or a heading with its line). The lines after a heading, up
to the next one, are its own. Folded, they are out of the list. While a search is on, a row is in the list when
its name or its description has the words, a heading when one of its rows is, and the lines of all pages make
one list on page 0. The lines that stay move up under each other, a step of the list each.
=================
*/
#define LIST_SEARCH_MIN		2		// letters a search needs before it begins

static qboolean Menu_FoldIs( const char *folds, int fold ) {
	const char *p = folds;

	while ( *p ) {
		while ( *p == ' ' ) {
			p++;
		}
		if ( *p && atoi( p ) == fold ) {
			return qtrue;
		}
		while ( *p && *p != ' ' ) {
			p++;
		}
	}
	return qfalse;
}

static qboolean Item_Bind_HasKey( const itemDef_t *item, const char *search );

// the name of a row or its description has the words, as the player reads them; or a row of keys has that key
static qboolean Item_ListMatch( const itemDef_t *item, const char *search ) {
	char text[1024];

	if ( !item->row ) {
		return qfalse;
	}
	Text_Word( item->text, text, sizeof( text ), qfalse, qtrue );
	if ( Q_stristr( text, search ) ) {
		return qtrue;
	}
	if ( item->descText ) {
		Text_Word( item->descText, text, sizeof( text ), qfalse, qfalse );
		if ( Q_stristr( text, search ) ) {
			return qtrue;
		}
	}
	return Item_Bind_HasKey( item, search );
}

#define LIST_COLUMNS	4		// "column <n>": the columns of the list of a page, counted from 0

static void Menu_ListLayout( menuDef_t *menu, const char *search, const char *folds, qboolean newSearch ) {
	static struct {
		int		page;
		int		column;
		float	y;			// where the menu file put it
		int		fold;		// a heading: its number
		int		head;		// the line of the heading it is under, -1: none
		qboolean match;		// the search: one of its rows has the words, or for a heading one of its lines
		qboolean advanced;	// "advanced 1": shown only with all settings (advancedCvar), or by the search
		qboolean in;
		float	bottom;		// a heading: where the last of its lines ends, the end of its card
		float	h;			// in a menu with a canvas: as high as its highest item (else every line is a step high)
		float	gap;		// a heading under another card: the room above its card it has in it (canvas)
		int		dst;		// the column it is shown in: its own, or for a search the one with room (canvas)
	} lines[MAX_MENUITEMS];
	static int	lineOf[MAX_MENUITEMS];
	const float	step = Menu_ListStep( menu );
	const qboolean searching = (qboolean)( strlen( search ) >= LIST_SEARCH_MIN );
	const qboolean all = (qboolean)( !menu->advancedCvar || DC->getCVarValue( menu->advancedCvar ) != 0.0f );
	float		cursor[LIST_COLUMNS] = { 0 };
	float		colX[LIST_COLUMNS];
	qboolean	colSeen[LIST_COLUMNS] = { qfalse };
	int			page[LIST_COLUMNS];
	int			head[LIST_COLUMNS];
	float		firstHead[LIST_COLUMNS];	// the height of the first heading of a column, which has no room above it
	qboolean	started[LIST_COLUMNS];		// a line of the column is shown already
	int			i, count = 0;

	for ( i = 0; i < LIST_COLUMNS; i++ ) {
		page[i] = -1;
		head[i] = -1;
		firstHead[i] = -1.0f;
		started[i] = qfalse;
	}
	menu->listAll = all;
	menu->searchFound = 0;
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t	*item = menu->items[i];
		const int	col = Com_Clampi( 0, LIST_COLUMNS - 1, item->column );

		lineOf[i] = -1;
		if ( !item->scroll ) {
			continue;
		}
		if ( !menu->listKnown ) {
			item->listY = item->window.rectClient.y;
			item->listX = item->window.rectClient.x;
		}
		if ( !colSeen[col] || item->listX < colX[col] ) {
			colX[col] = item->listX;
			colSeen[col] = qtrue;
		}
		// (a line: the items of a step, or in a menu with a canvas the items at the same height, which may be of
		// different heights: rows, the headings of cards)
		if ( !count || item->page != lines[count - 1].page || col != lines[count - 1].column
			|| ( menu->canvasW > 0.0f ? fabsf( item->listY - lines[count - 1].y ) > 0.5f
				: ( item->listY < lines[count - 1].y - 0.5f || item->listY >= lines[count - 1].y + step - 0.5f ) ) ) {
			if ( count && ( item->page != lines[count - 1].page ) ) {
				int k;

				for ( k = 0; k < LIST_COLUMNS; k++ ) {
					head[k] = -1;
					firstHead[k] = -1.0f;
				}
			}
			lines[count].page = item->page;
			lines[count].column = col;
			lines[count].y = item->listY;
			lines[count].fold = 0;
			lines[count].head = head[col];
			lines[count].match = qfalse;
			lines[count].advanced = qfalse;
			lines[count].bottom = 0.0f;
			lines[count].h = 0.0f;
			lines[count].gap = 0.0f;
			count++;
		}
		lines[count - 1].h = Q_max( lines[count - 1].h, item->window.rectClient.h );
		lineOf[i] = count - 1;
		if ( item->fold ) {
			lines[count - 1].fold = item->fold;
			lines[count - 1].head = -1;
			head[col] = count - 1;
			if ( menu->canvasW > 0.0f ) {
				if ( firstHead[col] < 0.0f ) {
					firstHead[col] = item->window.rectClient.h;
				} else {
					lines[count - 1].gap = Q_max( 0.0f, item->window.rectClient.h - firstHead[col] );
				}
			}
		}
		if ( item->advanced ) {
			lines[count - 1].advanced = qtrue;
		}
		if ( searching && Item_ListMatch( item, search ) ) {
			lines[count - 1].match = qtrue;
			menu->searchFound++;
		}
	}
	for ( i = 0; i < count; i++ ) {
		if ( searching && lines[i].match && lines[i].head >= 0 ) {
			lines[lines[i].head].match = qtrue;
		}
	}
	for ( i = 0; i < count; i++ ) {
		if ( searching ) {
			lines[i].in = lines[i].match;
		} else {
			lines[i].in = (qboolean)( ( lines[i].head < 0 || !Menu_FoldIs( folds, lines[lines[i].head].fold ) )
				&& ( all || !lines[i].advanced ) );
		}
	}

	// each page from where its list began, or all of them one after the other for the search; every column of
	// its own from the top
	if ( searching && menu->canvasW > 0.0f && colSeen[0] && colSeen[1] ) {
		// (the cards of a search go into the column with room: both columns start at the top of the list)
		float top = count ? lines[0].y : 0.0f;

		for ( i = 1; i < count; i++ ) {
			top = Q_min( top, lines[i].y );
		}
		for ( i = 0; i < LIST_COLUMNS; i++ ) {
			cursor[i] = top;
			page[i] = 0;
		}
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t	*item = menu->items[i];
		const int	n = lineOf[i];
		int			col;

		if ( n < 0 ) {
			continue;
		}
		col = lines[n].column;
		if ( i == 0 || lineOf[i - 1] != n ) {
			// (a search in a menu with a canvas: each card into the column that is shorter, its lines after it)
			lines[n].dst = col;
			if ( searching && menu->canvasW > 0.0f && colSeen[0] && colSeen[1] && col < 2 && lines[n].in ) {
				if ( lines[n].fold ) {
					lines[n].dst = cursor[1] < cursor[0] ? 1 : 0;
				} else if ( lines[n].head >= 0 ) {
					lines[n].dst = lines[lines[n].head].dst;
				}
			}
		}
		if ( lines[n].dst != col ) {
			const int from = col;

			col = lines[n].dst;
			item->window.rectClient.x = item->listX + colX[col] - colX[from];
		} else {
			item->window.rectClient.x = item->listX;
		}
		if ( page[col] < 0 || ( !searching && lines[n].page != page[col] ) ) {
			cursor[col] = lines[n].y;
			page[col] = lines[n].page;
			started[col] = qfalse;
		}
		if ( ( i == 0 || lineOf[i - 1] != n ) && lines[n].in && !started[col] ) {
			// the first card shown in a column starts at its top: a heading that stood under another card (which
			// is folded away, hidden with the settings that matter, or left out by the search) leaves out the
			// room above it
			if ( lines[n].fold ) {
				cursor[col] -= lines[n].gap;
			}
			started[col] = qtrue;
		} else if ( ( i == 0 || lineOf[i - 1] != n ) && lines[n].in && lines[n].fold && lines[n].gap <= 0.0f && menu->canvasW > 0.0f ) {
			// the other way round: the first heading of a page, which has no room above it, under another card
			// (a search puts the cards of all pages one under the other)
			cursor[col] += ( CARD_GAP_PX + CARD_BOTTOM_PX ) * menu->canvasUnit;
		}
		item->listOut = (qboolean)!lines[n].in;
		item->window.rectClient.y = item->listY - lines[n].y + cursor[col];
		if ( i + 1 >= menu->itemCount || lineOf[i + 1] != n ) {
			if ( lines[n].in ) {
				const int owner = lines[n].fold ? n : lines[n].head;

				cursor[col] += ( menu->canvasW > 0.0f && lines[n].h > 0.0f ) ? lines[n].h : step;
				if ( owner >= 0 ) {
					lines[owner].bottom = cursor[col];
				}
			}
		}
	}
	// where the card of each heading ends (Menu_ListCards)
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *item = menu->items[i];

		if ( lineOf[i] >= 0 && item->fold ) {
			item->cardBottom = lines[lineOf[i]].bottom;
		}
	}
	menu->listKnown = qtrue;

	if ( searching != menu->searching ) {
		menu->searching = searching;
		if ( searching ) {
			menu->searchPage = menu->page > 0 ? menu->page : 1;
			menu->scrollTo[0] = 0.0f;
			Menu_OpenPage( menu, 0 );
		} else if ( menu->page == 0 ) {
			Menu_OpenPage( menu, menu->searchPage > 0 ? menu->searchPage : 1 );
		}
	} else if ( searching && newSearch ) {
		menu->scrollTo[0] = 0.0f;
	}
	menu->scrollShown = -1.0f;
	Menu_ScrollTo( menu, menu->scrollTo[menu->page], qtrue );
}

// Once a frame: the list is laid out again when the search or the folded headings changed
static void Menu_ListCheck( menuDef_t *menu ) {
	char	search[64], folds[256];
	char	*p;

	if ( !menu->searchCvar && !menu->foldCvar && !menu->advancedCvar ) {
		return;
	}
	search[0] = folds[0] = '\0';
	if ( menu->searchCvar ) {
		DC->getCVarString( menu->searchCvar, search, sizeof( search ) );
	}
	if ( menu->foldCvar ) {
		DC->getCVarString( menu->foldCvar, folds, sizeof( folds ) );
	}
	for ( p = search; *p == ' '; p++ ) {
	}
	memmove( search, p, strlen( p ) + 1 );
	for ( p = search + strlen( search ); p > search && p[-1] == ' '; p-- ) {
		p[-1] = '\0';
	}
	if ( menu->listKnown && !strcmp( search, menu->listSearch ) && !strcmp( folds, menu->listFolds )
		&& ( !menu->advancedCvar || menu->listAll == (qboolean)( DC->getCVarValue( menu->advancedCvar ) != 0.0f ) ) ) {
		return;
	}
	Menu_ListLayout( menu, search, folds, (qboolean)strcmp( search, menu->listSearch ) );
	Q_strncpyz( menu->listSearch, search, sizeof( menu->listSearch ) );
	Q_strncpyz( menu->listFolds, folds, sizeof( menu->listFolds ) );
}

// a click on a heading: its rows fold away, or come back
static void Menu_ToggleFold( menuDef_t *menu, int fold ) {
	char		folds[256], out[256];
	const char	*p;
	qboolean	was = qfalse;

	if ( !menu->foldCvar || menu->searching ) {
		return;
	}
	DC->getCVarString( menu->foldCvar, folds, sizeof( folds ) );
	out[0] = '\0';
	for ( p = folds; *p; ) {
		char word[16];
		int len;

		while ( *p == ' ' ) {
			p++;
		}
		len = (int)strcspn( p, " " );
		if ( !len ) {
			break;
		}
		Q_strncpyz( word, p, Q_min( len + 1, (int)sizeof( word ) ) );
		p += len;
		if ( atoi( word ) == fold ) {
			was = qtrue;
			continue;
		}
		Q_strcat( out, sizeof( out ), va( "%s%s", out[0] ? " " : "", word ) );
	}
	if ( !was ) {
		Q_strcat( out, sizeof( out ), va( "%s%i", out[0] ? " " : "", fold ) );
	}
	DC->setCVar( menu->foldCvar, out );
	Menu_ListCheck( menu );
}

// will optionaly set focus to this item
qboolean Item_SetFocus(itemDef_t *item, float x, float y) {
	int i;
	itemDef_t *oldFocus;
	sfxHandle_t *sfx = &DC->Assets.itemFocusSound;
	qboolean playSound = qfalse;
	menuDef_t *parent;
	// sanity check, non-null, not a decoration and does not already have the focus
	if (item == NULL || item->window.flags & WINDOW_DECORATION || item->window.flags & WINDOW_HASFOCUS || !(item->window.flags & WINDOW_VISIBLE)) {
		return qfalse;
	}

	parent = (menuDef_t*)item->parent;

	// items can be enabled and disabled
	if (item->disabled)
	{
		return qfalse;
	}

	// items can be enabled and disabled based on cvars
	if (item->cvarFlags & (CVAR_ENABLE | CVAR_DISABLE) && !Item_EnableShowViaCvar(item, CVAR_ENABLE)) {
		return qfalse;
	}

	if (item->cvarFlags & (CVAR_SHOW | CVAR_HIDE) && !Item_EnableShowViaCvar(item, CVAR_SHOW)) {
		return qfalse;
	}

	// an item of a page that is not open is not there. A row of the list that stands outside of its window is
	// brought into it: the keys walk through all of them (the mouse never gets to one, Menu_HandleMouseMove).
	if ( Item_OffPage( item ) ) {
		return qfalse;
	}
	if ( item->scroll && Item_Away( item ) ) {
		Menu_ScrollIntoView( parent, item );
	}
	if ( Item_Away( item ) ) {
		return qfalse;
	}

	oldFocus = Menu_ClearFocus(parent);

	if (item->type == ITEM_TYPE_TEXT) {
		rectDef_t r;
		r = item->textRect;
		r.y -= r.h;

		if (Rect_ContainsPoint(&r, x, y))
		{
			item->window.flags |= WINDOW_HASFOCUS;
			if (item->focusSound) {
				sfx = &item->focusSound;
			}
			playSound = qtrue;
		}
		else
		{
			if (oldFocus) {
				oldFocus->window.flags |= WINDOW_HASFOCUS;
				if (oldFocus->onFocus) {
					Item_RunScript(oldFocus, oldFocus->onFocus);
				}
			}
		}
	} else {
	    item->window.flags |= WINDOW_HASFOCUS;
		if (item->onFocus) {
			Item_RunScript(item, item->onFocus);
		}
		if (item->focusSound) {
			sfx = &item->focusSound;
		}
		playSound = qtrue;
	}

	if (playSound && sfx) {
		DC->startLocalSound( *sfx, CHAN_LOCAL_SOUND );
	}

	for (i = 0; i < parent->itemCount; i++) {
		if (parent->items[i] == item) {
			parent->cursorItem = i;
			break;
		}
	}

	return qtrue;
}

int Item_TextScroll_MaxScroll ( itemDef_t *item )
{
	textScrollDef_t *scrollPtr = item->typeData.textscroll;

	int count = scrollPtr->iLineCount;
	int max   = count - (int)(item->window.rect.h / scrollPtr->lineHeight) + 1;

	if (max < 0)
	{
		return 0;
	}

	return max;
}

int Item_TextScroll_ThumbPosition ( itemDef_t *item )
{
	float max, pos, size;
	textScrollDef_t *scrollPtr = item->typeData.textscroll;

	max  = Item_TextScroll_MaxScroll ( item );
	size = item->window.rect.h - (SCROLLBAR_SIZE * 2) - 2;

	if (max > 0)
	{
		pos = (size-SCROLLBAR_SIZE) / (float) max;
	}
	else
	{
		pos = 0;
	}

	pos *= scrollPtr->startPos;

	return item->window.rect.y + 1 + SCROLLBAR_SIZE + pos;
}

int Item_TextScroll_ThumbDrawPosition ( itemDef_t *item )
{
	int min, max;

	if (itemCapture == item)
	{
		min = item->window.rect.y + SCROLLBAR_SIZE + 1;
		max = item->window.rect.y + item->window.rect.h - 2*SCROLLBAR_SIZE - 1;

		if (DC->cursory >= min + SCROLLBAR_SIZE/2 && DC->cursory <= max + SCROLLBAR_SIZE/2)
		{
			return DC->cursory - SCROLLBAR_SIZE/2;
		}

		return Item_TextScroll_ThumbPosition(item);
	}

	return Item_TextScroll_ThumbPosition(item);
}

int Item_TextScroll_OverLB ( itemDef_t *item, float x, float y )
{
	rectDef_t		r;
	int				thumbstart;

	r.x = item->window.rect.x + item->window.rect.w - SCROLLBAR_SIZE;
	r.y = item->window.rect.y;
	r.h = r.w = SCROLLBAR_SIZE;
	if (Rect_ContainsPoint(&r, x, y))
	{
		return WINDOW_LB_LEFTARROW;
	}

	r.y = item->window.rect.y + item->window.rect.h - SCROLLBAR_SIZE;
	if (Rect_ContainsPoint(&r, x, y))
	{
		return WINDOW_LB_RIGHTARROW;
	}

	thumbstart = Item_TextScroll_ThumbPosition(item);
	r.y = thumbstart;
	if (Rect_ContainsPoint(&r, x, y))
	{
		return WINDOW_LB_THUMB;
	}

	r.y = item->window.rect.y + SCROLLBAR_SIZE;
	r.h = thumbstart - r.y;
	if (Rect_ContainsPoint(&r, x, y))
	{
		return WINDOW_LB_PGUP;
	}

	r.y = thumbstart + SCROLLBAR_SIZE;
	r.h = item->window.rect.y + item->window.rect.h - SCROLLBAR_SIZE;
	if (Rect_ContainsPoint(&r, x, y))
	{
		return WINDOW_LB_PGDN;
	}

	return 0;
}

void Item_TextScroll_MouseEnter (itemDef_t *item, float x, float y)
{
	item->window.flags &= ~(WINDOW_LB_LEFTARROW | WINDOW_LB_RIGHTARROW | WINDOW_LB_THUMB | WINDOW_LB_PGUP | WINDOW_LB_PGDN);
	item->window.flags |= Item_TextScroll_OverLB(item, x, y);
}

qboolean Item_TextScroll_HandleKey ( itemDef_t *item, int key, qboolean down, qboolean force)
{
	textScrollDef_t *scrollPtr = item->typeData.textscroll;
	int				max;
	int				viewmax;

	if (force || (Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory) && item->window.flags & WINDOW_HASFOCUS))
	{
		max = Item_TextScroll_MaxScroll(item);

		viewmax = (item->window.rect.h / scrollPtr->lineHeight);
		if ( key == A_CURSOR_UP || key == A_KP_8 )
		{
			scrollPtr->startPos--;
			if (scrollPtr->startPos < 0)
			{
				scrollPtr->startPos = 0;
			}
			return qtrue;
		}

		if ( key == A_CURSOR_DOWN || key == A_KP_2 )
		{
			scrollPtr->startPos++;
			if (scrollPtr->startPos > max)
			{
				scrollPtr->startPos = max;
			}

			return qtrue;
		}

		if ( key == A_MWHEELUP )
		{
			int count = trap->Key_IsDown( A_CTRL ) ? 5 : 1;
			scrollPtr->startPos -= count;
			if (scrollPtr->startPos < 0)
			{
				scrollPtr->startPos = 0;
				Display_MouseMove(NULL, DC->cursorx, DC->cursory);
				return qfalse;
			}
			Display_MouseMove(NULL, DC->cursorx, DC->cursory);
			return qtrue;
		}
		if ( key == A_MWHEELDOWN )
		{
			int count = trap->Key_IsDown( A_CTRL ) ? 5 : 1;
			scrollPtr->startPos += count;
			if (scrollPtr->startPos > max)
			{
				scrollPtr->startPos = max;
				Display_MouseMove(NULL, DC->cursorx, DC->cursory);
				return qfalse;
			}
			Display_MouseMove(NULL, DC->cursorx, DC->cursory);
			return qtrue;
		}

		// mouse hit
		if (key == A_MOUSE1 || key == A_MOUSE2)
		{
			if (item->window.flags & WINDOW_LB_LEFTARROW)
			{
				scrollPtr->startPos--;
				if (scrollPtr->startPos < 0)
				{
					scrollPtr->startPos = 0;
				}
			}
			else if (item->window.flags & WINDOW_LB_RIGHTARROW)
			{
				// one down
				scrollPtr->startPos++;
				if (scrollPtr->startPos > max)
				{
					scrollPtr->startPos = max;
				}
			}
			else if (item->window.flags & WINDOW_LB_PGUP)
			{
				// page up
				scrollPtr->startPos -= viewmax;
				if (scrollPtr->startPos < 0)
				{
					scrollPtr->startPos = 0;
				}
			}
			else if (item->window.flags & WINDOW_LB_PGDN)
			{
				// page down
				scrollPtr->startPos += viewmax;
				if (scrollPtr->startPos > max)
				{
					scrollPtr->startPos = max;
				}
			}
			else if (item->window.flags & WINDOW_LB_THUMB)
			{
				// Display_SetCaptureItem(item);
			}

			return qtrue;
		}

		if ( key == A_HOME || key == A_KP_7)
		{
			// home
			scrollPtr->startPos = 0;
			return qtrue;
		}
		if ( key == A_END || key == A_KP_1)
		{
			// end
			scrollPtr->startPos = max;
			return qtrue;
		}
		if (key == A_PAGE_UP || key == A_KP_9 )
		{
			scrollPtr->startPos -= viewmax;
			if (scrollPtr->startPos < 0)
			{
					scrollPtr->startPos = 0;
			}

			return qtrue;
		}
		if ( key == A_PAGE_DOWN || key == A_KP_3 )
		{
			scrollPtr->startPos += viewmax;
			if (scrollPtr->startPos > max)
			{
				scrollPtr->startPos = max;
			}
			return qtrue;
		}
	}

	return qfalse;
}

int Item_ListBox_MaxScroll(itemDef_t *item) {
	listBoxDef_t *listPtr = item->typeData.listbox;
	int count = DC->feederCount(item->special);
	int max;

	if (item->window.flags & WINDOW_HORIZONTAL) {
		max = count - (item->window.rect.w / listPtr->elementWidth) + 1;
	}
	else {
		max = count - (item->window.rect.h / listPtr->elementHeight) + 1;
	}
	if (max < 0) {
		return 0;
	}
	return max;
}

int Item_ListBox_ThumbPosition(itemDef_t *item) {
	float max, pos, size;
	listBoxDef_t *listPtr = item->typeData.listbox;

	max = Item_ListBox_MaxScroll(item);
	if (item->window.flags & WINDOW_HORIZONTAL) {
		size = item->window.rect.w - (SCROLLBAR_SIZE * 2) - 2;
		if (max > 0) {
			pos = (size-SCROLLBAR_SIZE) / (float) max;
		} else {
			pos = 0;
		}
		pos *= listPtr->startPos;
		return item->window.rect.x + 1 + SCROLLBAR_SIZE + pos;
	}
	else {
		size = item->window.rect.h - (SCROLLBAR_SIZE * 2) - 2;
		if (max > 0) {
			pos = (size-SCROLLBAR_SIZE) / (float) max;
		} else {
			pos = 0;
		}
		pos *= listPtr->startPos;
		return item->window.rect.y + 1 + SCROLLBAR_SIZE + pos;
	}
}

int Item_ListBox_ThumbDrawPosition(itemDef_t *item)
{
	int min, max;

	if (itemCapture == item)
	{
		if (item->window.flags & WINDOW_HORIZONTAL)
		{
			min = item->window.rect.x + SCROLLBAR_SIZE + 1;
			max = item->window.rect.x + item->window.rect.w - 2*SCROLLBAR_SIZE - 1;
			if (DC->cursorx >= min + SCROLLBAR_SIZE/2 && DC->cursorx <= max + SCROLLBAR_SIZE/2)
			{
				return DC->cursorx - SCROLLBAR_SIZE/2;
			}
			else
			{
				return Item_ListBox_ThumbPosition(item);
			}
		}
		else
		{
			min = item->window.rect.y + SCROLLBAR_SIZE + 1;
			max = item->window.rect.y + item->window.rect.h - 2*SCROLLBAR_SIZE - 1;
			if (DC->cursory >= min + SCROLLBAR_SIZE/2 && DC->cursory <= max + SCROLLBAR_SIZE/2)
			{
				return DC->cursory - SCROLLBAR_SIZE/2;
			}
			else
			{
				return Item_ListBox_ThumbPosition(item);
			}
		}
	}
	else
	{
		return Item_ListBox_ThumbPosition(item);
	}
}

float Item_Slider_ThumbPosition(itemDef_t *item) {
	float value, range, x, width;
	editFieldDef_t *editDef = item->typeData.edit;

	x = Item_Slider_Track( item, &width );

	if (!editDef || !item->cvar) {
		return x;
	}

	value = DC->getCVarValue(item->cvar);

	if (value < editDef->minVal) {
		value = editDef->minVal;
	} else if (value > editDef->maxVal) {
		value = editDef->maxVal;
	}

	range = editDef->maxVal - editDef->minVal;
	value -= editDef->minVal;
	value /= range;
	//value /= (editDef->maxVal - editDef->minVal);
	value *= width;
	x += value;
	// vm fuckage
	//x = x + (((float)value / editDef->maxVal) * SLIDER_WIDTH);
	return x;
}

int Item_Slider_OverSlider(itemDef_t *item, float x, float y) {
	rectDef_t r;

	if ( Item_IsRow( item ) ) {
		// all of the track of a row takes hold of its handle
		r = item->window.rect;
		r.x = Item_Slider_Track( item, &r.w ) - 4.0f;
		r.w += 8.0f;
		return Rect_ContainsPoint( &r, x, y ) ? WINDOW_LB_THUMB : 0;
	}

	r.x = Item_Slider_ThumbPosition(item) - (SLIDER_THUMB_WIDTH / 2);
	r.y = item->window.rect.y - 2;
	r.w = SLIDER_THUMB_WIDTH;
	r.h = SLIDER_THUMB_HEIGHT;

	if (Rect_ContainsPoint(&r, x, y)) {
		return WINDOW_LB_THUMB;
	}
	return 0;
}

int Item_ListBox_OverLB(itemDef_t *item, float x, float y)
{
	rectDef_t r;
	listBoxDef_t *listPtr;
	int thumbstart;

	listPtr = item->typeData.listbox;
	if (item->window.flags & WINDOW_HORIZONTAL)
	{
		// check if on left arrow
		r.x = item->window.rect.x;
		r.y = item->window.rect.y + item->window.rect.h - SCROLLBAR_SIZE;
		r.h = r.w = SCROLLBAR_SIZE;
		if (Rect_ContainsPoint(&r, x, y))
		{
			return WINDOW_LB_LEFTARROW;
		}

		// check if on right arrow
		r.x = item->window.rect.x + item->window.rect.w - SCROLLBAR_SIZE;
		if (Rect_ContainsPoint(&r, x, y))
		{
			return WINDOW_LB_RIGHTARROW;
		}

		// check if on thumb
		thumbstart = Item_ListBox_ThumbPosition(item);
		r.x = thumbstart;
		if (Rect_ContainsPoint(&r, x, y))
		{
			return WINDOW_LB_THUMB;
		}

		r.x = item->window.rect.x + SCROLLBAR_SIZE;
		r.w = thumbstart - r.x;
		if (Rect_ContainsPoint(&r, x, y))
		{
			return WINDOW_LB_PGUP;
		}

		r.x = thumbstart + SCROLLBAR_SIZE;
		r.w = item->window.rect.x + item->window.rect.w - SCROLLBAR_SIZE;
		if (Rect_ContainsPoint(&r, x, y))
		{
			return WINDOW_LB_PGDN;
		}
	}
	// Vertical Scroll
	else
	{
		// Multiple rows and columns (since it's more than twice as wide as an element)
		if (( item->window.rect.w > (listPtr->elementWidth*2)) &&  (listPtr->elementStyle == LISTBOX_IMAGE))
		{
			r.x = item->window.rect.x + item->window.rect.w - SCROLLBAR_SIZE;
			r.y = item->window.rect.y;
			r.h = r.w = SCROLLBAR_SIZE;
			if (Rect_ContainsPoint(&r, x, y))
			{
				return WINDOW_LB_PGUP;
			}

			r.y = item->window.rect.y + item->window.rect.h - SCROLLBAR_SIZE;
			if (Rect_ContainsPoint(&r, x, y))
			{
				return WINDOW_LB_PGDN;
			}

			thumbstart = Item_ListBox_ThumbPosition(item);
			r.y = thumbstart;
			if (Rect_ContainsPoint(&r, x, y))
			{
				return WINDOW_LB_THUMB;
			}

		}
		else
		{
			r.x = item->window.rect.x + item->window.rect.w - SCROLLBAR_SIZE;
			r.y = item->window.rect.y;
			r.h = r.w = SCROLLBAR_SIZE;
			if (Rect_ContainsPoint(&r, x, y))
			{
				return WINDOW_LB_LEFTARROW;
			}

			r.y = item->window.rect.y + item->window.rect.h - SCROLLBAR_SIZE;
			if (Rect_ContainsPoint(&r, x, y))
			{
				return WINDOW_LB_RIGHTARROW;
			}

			thumbstart = Item_ListBox_ThumbPosition(item);
			r.y = thumbstart;
			if (Rect_ContainsPoint(&r, x, y))
			{
				return WINDOW_LB_THUMB;
			}

			r.y = item->window.rect.y + SCROLLBAR_SIZE;
			r.h = thumbstart - r.y;
			if (Rect_ContainsPoint(&r, x, y))
			{
				return WINDOW_LB_PGUP;
			}

			r.y = thumbstart + SCROLLBAR_SIZE;
			r.h = item->window.rect.y + item->window.rect.h - SCROLLBAR_SIZE;
			if (Rect_ContainsPoint(&r, x, y))
			{
				return WINDOW_LB_PGDN;
			}
		}
	}
	return 0;
}


void Item_ListBox_MouseEnter(itemDef_t *item, float x, float y)
{
	rectDef_t r;
	listBoxDef_t *listPtr = item->typeData.listbox;

	item->window.flags &= ~(WINDOW_LB_LEFTARROW | WINDOW_LB_RIGHTARROW | WINDOW_LB_THUMB | WINDOW_LB_PGUP | WINDOW_LB_PGDN);
	item->window.flags |= Item_ListBox_OverLB(item, x, y);

	if (item->window.flags & WINDOW_HORIZONTAL)
	{
		if (!(item->window.flags & (WINDOW_LB_LEFTARROW | WINDOW_LB_RIGHTARROW | WINDOW_LB_THUMB | WINDOW_LB_PGUP | WINDOW_LB_PGDN)))
		{
			// check for selection hit as we have exausted buttons and thumb
			if (listPtr->elementStyle == LISTBOX_IMAGE)
			{
				r.x = item->window.rect.x;
				r.y = item->window.rect.y;
				r.h = item->window.rect.h - SCROLLBAR_SIZE;
				r.w = item->window.rect.w - listPtr->drawPadding;
				if (Rect_ContainsPoint(&r, x, y))
				{
					listPtr->cursorPos =  (int)((x - r.x) / listPtr->elementWidth)  + listPtr->startPos;
					if (listPtr->cursorPos >= listPtr->endPos)
					{
						listPtr->cursorPos = listPtr->endPos;
					}
				}
			}
			else
			{
				// text hit..
			}
		}
	}
	// Window Vertical Scroll
	else if (!(item->window.flags & (WINDOW_LB_LEFTARROW | WINDOW_LB_RIGHTARROW | WINDOW_LB_THUMB | WINDOW_LB_PGUP | WINDOW_LB_PGDN)))
	{
		// Calc which element the mouse is over
		r.x = item->window.rect.x;
		r.y = item->window.rect.y;
		r.w = item->window.rect.w - SCROLLBAR_SIZE;
		r.h = item->window.rect.h - listPtr->drawPadding;
		if (Rect_ContainsPoint(&r, x, y))
		{
			// Multiple rows and columns (since it's more than twice as wide as an element)
			if (( item->window.rect.w > (listPtr->elementWidth*2)) &&  (listPtr->elementStyle == LISTBOX_IMAGE))
			{
				int row,column,rowLength;

				row = (int)((y - 2 - r.y) / listPtr->elementHeight);
				rowLength = (int) r.w / listPtr->elementWidth;
				column = (int)((x - r.x) / listPtr->elementWidth);

				listPtr->cursorPos = (row * rowLength)+column  + listPtr->startPos;
				if (listPtr->cursorPos >= listPtr->endPos)
				{
					listPtr->cursorPos = listPtr->endPos;
				}
			}
			// single column
			else
			{
				listPtr->cursorPos =  (int)((y - 2 - r.y) / listPtr->elementHeight)  + listPtr->startPos;
				if (listPtr->cursorPos > listPtr->endPos)
				{
                    listPtr->cursorPos = listPtr->endPos;
				}
			}
		}
	}
}

void Item_MouseEnter(itemDef_t *item, float x, float y) {

	rectDef_t r;
	if (item) {
		r = item->textRect;
		r.y -= r.h;
		// in the text rect?

		// items can be enabled and disabled
		if (item->disabled)
		{
			return;
		}

		// items can be enabled and disabled based on cvars
		if (item->cvarFlags & (CVAR_ENABLE | CVAR_DISABLE) && !Item_EnableShowViaCvar(item, CVAR_ENABLE)) {
			return;
		}

		if (item->cvarFlags & (CVAR_SHOW | CVAR_HIDE) && !Item_EnableShowViaCvar(item, CVAR_SHOW)) {
			return;
		}

		if (Rect_ContainsPoint(&r, x, y))
		{
			if (!(item->window.flags & WINDOW_MOUSEOVERTEXT)) {
				Item_RunScript(item, item->mouseEnterText);
				item->window.flags |= WINDOW_MOUSEOVERTEXT;
			}
			if (!(item->window.flags & WINDOW_MOUSEOVER)) {
				Item_RunScript(item, item->mouseEnter);
				item->window.flags |= WINDOW_MOUSEOVER;
			}

		} else {
			// not in the text rect
			if (item->window.flags & WINDOW_MOUSEOVERTEXT) {
				// if we were
				Item_RunScript(item, item->mouseExitText);
				item->window.flags &= ~WINDOW_MOUSEOVERTEXT;
			}
			if (!(item->window.flags & WINDOW_MOUSEOVER)) {
				Item_RunScript(item, item->mouseEnter);
				item->window.flags |= WINDOW_MOUSEOVER;
			}

			if (item->type == ITEM_TYPE_LISTBOX) {
				Item_ListBox_MouseEnter(item, x, y);
			}
			else if ( item->type == ITEM_TYPE_TEXTSCROLL )
			{
				Item_TextScroll_MouseEnter ( item, x, y );
			}
		}
	}
}

void Item_MouseLeave(itemDef_t *item) {
  if (item) {
    if (item->window.flags & WINDOW_MOUSEOVERTEXT) {
      Item_RunScript(item, item->mouseExitText);
      item->window.flags &= ~WINDOW_MOUSEOVERTEXT;
    }
    Item_RunScript(item, item->mouseExit);
    item->window.flags &= ~(WINDOW_LB_RIGHTARROW | WINDOW_LB_LEFTARROW);
  }
}

itemDef_t *Menu_HitTest(menuDef_t *menu, float x, float y) {
  int i;
  for (i = 0; i < menu->itemCount; i++) {
    if (Rect_ContainsPoint(&menu->items[i]->window.rect, x, y)) {
      return menu->items[i];
    }
  }
  return NULL;
}

void Item_SetMouseOver(itemDef_t *item, qboolean focus) {
  if (item) {
    if (focus) {
      item->window.flags |= WINDOW_MOUSEOVER;
    } else {
      item->window.flags &= ~WINDOW_MOUSEOVER;
    }
  }
}


qboolean Item_OwnerDraw_HandleKey(itemDef_t *item, int key) {
  if (item && DC->ownerDrawHandleKey)
  {

	  // yep this is an ugly hack
	  if( key == A_MOUSE1 || key == A_MOUSE2 )
	  {
		switch( item->window.ownerDraw )
		{
			case UI_FORCE_SIDE:
			case UI_FORCE_RANK_HEAL:
			case UI_FORCE_RANK_LEVITATION:
			case UI_FORCE_RANK_SPEED:
			case UI_FORCE_RANK_PUSH:
			case UI_FORCE_RANK_PULL:
			case UI_FORCE_RANK_TELEPATHY:
			case UI_FORCE_RANK_GRIP:
			case UI_FORCE_RANK_LIGHTNING:
			case UI_FORCE_RANK_RAGE:
			case UI_FORCE_RANK_PROTECT:
			case UI_FORCE_RANK_ABSORB:
			case UI_FORCE_RANK_TEAM_HEAL:
			case UI_FORCE_RANK_TEAM_FORCE:
			case UI_FORCE_RANK_DRAIN:
			case UI_FORCE_RANK_SEE:
			case UI_FORCE_RANK_SABERATTACK:
			case UI_FORCE_RANK_SABERDEFEND:
			case UI_FORCE_RANK_SABERTHROW:
	  			if(!Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory) )
				{
					return qfalse;
				}
				break;
		}
	  }


    return DC->ownerDrawHandleKey(item->window.ownerDraw, item->window.ownerDrawFlags, &item->special, key);
  }
  return qfalse;
}


qboolean Item_ListBox_HandleKey(itemDef_t *item, int key, qboolean down, qboolean force) {
	listBoxDef_t *listPtr = item->typeData.listbox;
	int count = DC->feederCount(item->special);
	int max, viewmax;
	if (force || (Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory) && item->window.flags & WINDOW_HASFOCUS))
	{
		max = Item_ListBox_MaxScroll(item);
		if (item->window.flags & WINDOW_HORIZONTAL) {
			viewmax = (item->window.rect.w / listPtr->elementWidth);
			if ( key == A_CURSOR_LEFT || key == A_KP_4 )
			{
				if (!listPtr->notselectable) {
					listPtr->cursorPos--;
					if (listPtr->cursorPos < 0) {
						listPtr->cursorPos = 0;
						return qfalse;
					}
					if (listPtr->cursorPos < listPtr->startPos) {
						listPtr->startPos = listPtr->cursorPos;
						return qfalse;
					}
					if (listPtr->cursorPos >= listPtr->startPos + viewmax) {
						listPtr->startPos = listPtr->cursorPos - viewmax + 1;
					}
					item->cursorPos = listPtr->cursorPos;
					DC->feederSelection(item->special, item->cursorPos, NULL);
				}
				else {
					listPtr->startPos--;
					if (listPtr->startPos < 0)
						listPtr->startPos = 0;
				}
				return qtrue;
			}
			if ( key == A_CURSOR_RIGHT || key == A_KP_6 )
			{
				if (!listPtr->notselectable) {
					listPtr->cursorPos++;
					if (listPtr->cursorPos < listPtr->startPos) {
						listPtr->startPos = listPtr->cursorPos;
						return qfalse;
					}
					if (listPtr->cursorPos >= count) {
						listPtr->cursorPos = count-1;
						return qfalse;
					}
					if (listPtr->cursorPos >= listPtr->startPos + viewmax) {
						listPtr->startPos = listPtr->cursorPos - viewmax + 1;
					}
					item->cursorPos = listPtr->cursorPos;
					DC->feederSelection(item->special, item->cursorPos, NULL);
				}
				else {
					listPtr->startPos++;
					if (listPtr->startPos >= count)
						listPtr->startPos = count-1;
				}
				return qtrue;
			}
		}
		// Vertical scroll
		else {

			// Multiple rows and columns (since it's more than twice as wide as an element)
			if (( item->window.rect.w > (listPtr->elementWidth*2)) &&  (listPtr->elementStyle == LISTBOX_IMAGE))
			{
				viewmax = (item->window.rect.w / listPtr->elementWidth);
			}
			else
			{
				viewmax = (item->window.rect.h / listPtr->elementHeight);
			}

			if ( key == A_CURSOR_UP || key == A_KP_8 )
			{
				if (!listPtr->notselectable) {
					listPtr->cursorPos--;
					if (listPtr->cursorPos < 0) {
						listPtr->cursorPos = 0;
						return qfalse;
					}
					if (listPtr->cursorPos < listPtr->startPos) {
						listPtr->startPos = listPtr->cursorPos;
						return qfalse;
					}
					if (listPtr->cursorPos >= listPtr->startPos + viewmax) {
						listPtr->startPos = listPtr->cursorPos - viewmax + 1;
					}
					item->cursorPos = listPtr->cursorPos;
					DC->feederSelection(item->special, item->cursorPos, NULL);
				}
				else {
					listPtr->startPos--;
					if (listPtr->startPos < 0)
						listPtr->startPos = 0;
				}
				return qtrue;
			}
			if ( key == A_CURSOR_DOWN || key == A_KP_2 )
			{
				if (!listPtr->notselectable) {
					listPtr->cursorPos++;
					if (listPtr->cursorPos < listPtr->startPos) {
						listPtr->startPos = listPtr->cursorPos;
						return qfalse;
					}
					if (listPtr->cursorPos >= count) {
						listPtr->cursorPos = count-1;
						return qfalse;
					}
					if (listPtr->cursorPos >= listPtr->startPos + viewmax) {
						listPtr->startPos = listPtr->cursorPos - viewmax + 1;
					}
					item->cursorPos = listPtr->cursorPos;
					DC->feederSelection(item->special, item->cursorPos, NULL);
				}
				else {
					listPtr->startPos++;
					if (listPtr->startPos > max)
						listPtr->startPos = max;
				}
				return qtrue;
			}

			if ( key == A_MWHEELUP )
			{
				int count = trap->Key_IsDown( A_CTRL ) ? 5 : 1;
				listPtr->startPos -= ((int)item->special == FEEDER_Q3HEADS) ? viewmax : count;
				if (listPtr->startPos < 0)
				{
					listPtr->startPos = 0;
					Display_MouseMove(NULL, DC->cursorx, DC->cursory);
					return qfalse;
				}
				Display_MouseMove(NULL, DC->cursorx, DC->cursory);
				return qtrue;
			}
			if ( key == A_MWHEELDOWN )
			{
				int count = trap->Key_IsDown( A_CTRL ) ? 5 : 1;
				listPtr->startPos += ((int)item->special == FEEDER_Q3HEADS) ? viewmax : count;
				if (listPtr->startPos > max)
				{
					listPtr->startPos = max;
					Display_MouseMove(NULL, DC->cursorx, DC->cursory);
					return qfalse;
				}
				Display_MouseMove(NULL, DC->cursorx, DC->cursory);
				return qtrue;
			}
		}
		// mouse hit
		if (key == A_MOUSE1 || key == A_MOUSE2) {
			if (item->window.flags & WINDOW_LB_LEFTARROW) {
				listPtr->startPos--;
				if (listPtr->startPos < 0) {
					listPtr->startPos = 0;
				}
			} else if (item->window.flags & WINDOW_LB_RIGHTARROW) {
				// one down
				listPtr->startPos++;
				if (listPtr->startPos > max) {
					listPtr->startPos = max;
				}
			} else if (item->window.flags & WINDOW_LB_PGUP) {
				// page up
				listPtr->startPos -= viewmax;
				if (listPtr->startPos < 0) {
					listPtr->startPos = 0;
				}
			} else if (item->window.flags & WINDOW_LB_PGDN) {
				// page down
				listPtr->startPos += viewmax;
				if (listPtr->startPos > (max)) {
					listPtr->startPos = (max);
				}
			} else if (item->window.flags & WINDOW_LB_THUMB) {
				// Display_SetCaptureItem(item);
			} else {
				// select an item
				if (DC->realTime < lastListBoxClickTime && listPtr->doubleClick) {
					Item_RunScript(item, listPtr->doubleClick);
				}
				lastListBoxClickTime = DC->realTime + DOUBLE_CLICK_DELAY;
//				if (item->cursorPos != listPtr->cursorPos)
				{
					int prePos = item->cursorPos;

					item->cursorPos = listPtr->cursorPos;

					if (!DC->feederSelection(item->special, item->cursorPos, item))
					{
						item->cursorPos = listPtr->cursorPos = prePos;
					}
				}
			}
			return qtrue;
		}
		if ( key == A_HOME || key == A_KP_7) {
			// home
			listPtr->startPos = 0;
			return qtrue;
		}
		if ( key == A_END || key == A_KP_1) {
			// end
			listPtr->startPos = max;
			return qtrue;
		}
		if (key == A_PAGE_UP || key == A_KP_9 ) {
			// page up
			if (!listPtr->notselectable) {
				listPtr->cursorPos -= viewmax;
				if (listPtr->cursorPos < 0) {
					listPtr->cursorPos = 0;
				}
				if (listPtr->cursorPos < listPtr->startPos) {
					listPtr->startPos = listPtr->cursorPos;
				}
				if (listPtr->cursorPos >= listPtr->startPos + viewmax) {
					listPtr->startPos = listPtr->cursorPos - viewmax + 1;
				}
				item->cursorPos = listPtr->cursorPos;
				DC->feederSelection(item->special, item->cursorPos, NULL);
			}
			else {
				listPtr->startPos -= viewmax;
				if (listPtr->startPos < 0) {
					listPtr->startPos = 0;
				}
			}
			return qtrue;
		}
		if ( key == A_PAGE_DOWN || key == A_KP_3 ) {
			// page down
			if (!listPtr->notselectable) {
				listPtr->cursorPos += viewmax;
				if (listPtr->cursorPos < listPtr->startPos) {
					listPtr->startPos = listPtr->cursorPos;
				}
				if (listPtr->cursorPos >= count) {
					listPtr->cursorPos = count-1;
				}
				if (listPtr->cursorPos >= listPtr->startPos + viewmax) {
					listPtr->startPos = listPtr->cursorPos - viewmax + 1;
				}
				item->cursorPos = listPtr->cursorPos;
				DC->feederSelection(item->special, item->cursorPos, NULL);
			}
			else {
				listPtr->startPos += viewmax;
				if (listPtr->startPos > max) {
					listPtr->startPos = max;
				}
			}
			return qtrue;
		}
	}
	return qfalse;
}

qboolean Item_YesNo_HandleKey(itemDef_t *item, int key) {
  if (Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory) && item->window.flags & WINDOW_HASFOCUS && item->cvar)
	{
		if (key == A_MOUSE1 || key == A_ENTER || key == A_MOUSE2 || key == A_MOUSE3)
		{
	    DC->setCVar(item->cvar, va("%i", !DC->getCVarValue(item->cvar)));
		  return qtrue;
		}
  }

  return qfalse;

}

int Item_Multi_CountSettings(itemDef_t *item) {
	multiDef_t *multiPtr = item->typeData.multi;
	if (multiPtr == NULL) {
		return 0;
	}
	return multiPtr->count;
}

int Item_Multi_FindCvarByValue(itemDef_t *item) {
	char buff[2048];
	float value = 0;
	int i;
	multiDef_t *multiPtr = item->typeData.multi;
	if (multiPtr) {
		if (multiPtr->strDef) {
	    DC->getCVarString(item->cvar, buff, sizeof(buff));
		} else {
			value = DC->getCVarValue(item->cvar);
		}
		for (i = 0; i < multiPtr->count; i++) {
			if (multiPtr->strDef) {
				if (Q_stricmp(buff, multiPtr->cvarStr[i]) == 0) {
					return i;
				}
			} else {
 				if (multiPtr->cvarValue[i] == value) {
 					return i;
 				}
 			}
 		}
	}
	return 0;
}

const char *Item_Multi_Setting(itemDef_t *item) {
	char buff[2048];
	float value = 0;
	int i;
	multiDef_t *multiPtr = item->typeData.multi;
	if (multiPtr)
	{
		if (multiPtr->strDef)
		{
			if (item->cvar)
			{
			    DC->getCVarString(item->cvar, buff, sizeof(buff));
			}
		}
		else
		{
			if (item->cvar)	// Was a cvar given?
			{
				value = DC->getCVarValue(item->cvar);
			}
		}

		for (i = 0; i < multiPtr->count; i++)
		{
			if (multiPtr->strDef)
			{
				if (Q_stricmp(buff, multiPtr->cvarStr[i]) == 0)
				{
					return multiPtr->cvarList[i];
				}
			}
			else
			{
 				if (multiPtr->cvarValue[i] == value)
				{
					return multiPtr->cvarList[i];
 				}
 			}
 		}
	}
	return ( multiPtr && multiPtr->other ) ? multiPtr->other : "@MENUS_CUSTOM";
}

// sets a choice to one of its settings
static void Item_Multi_Set( itemDef_t *item, int current ) {
	multiDef_t *multiPtr = item->typeData.multi;

	if ( !multiPtr || current < 0 || current >= multiPtr->count ) {
		return;
	}
	if ( multiPtr->strDef ) {
		DC->setCVar( item->cvar, multiPtr->cvarStr[current] );
	} else {
		const float value = multiPtr->cvarValue[current];

		if ( (float)( (int)value ) == value ) {
			DC->setCVar( item->cvar, va( "%i", (int)value ) );
		} else {
			DC->setCVar( item->cvar, va( "%f", value ) );
		}
	}
	if ( item->special ) {
		// its a feeder?
		DC->feederSelection( item->special, current, item );
	}
}

qboolean Item_Multi_HandleKey(itemDef_t *item, int key)
{
	multiDef_t *multiPtr = item->typeData.multi;
	if (multiPtr)
	{
		if (Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory) && item->window.flags & WINDOW_HASFOCUS)
		{
			if (key == A_MOUSE1 || key == A_ENTER || key == A_MOUSE2 || key == A_MOUSE3 || key == A_MWHEELDOWN || key == A_MWHEELUP)
			{
				int current = Item_Multi_FindCvarByValue(item);
				int max = Item_Multi_CountSettings(item);

				if (key == A_MOUSE2 || key == A_CURSOR_LEFT || key == A_MWHEELDOWN)	// Xbox uses CURSOR_LEFT
			//	if (key == A_MOUSE2 || key == A_CURSOR_LEFT)	// Xbox uses CURSOR_LEFT
				{
					current--;
					if ( current < 0 )
					{
						current = max-1;
					}
				}
				else
				{
					current++;
					if ( current >= max )
					{
						current = 0;
					}
				}

				Item_Multi_Set( item, current );

				return qtrue;
			}
		}
	}
  return qfalse;
}

// Leaving an edit field so reset it so it prints from the beginning.
void Leaving_EditField(itemDef_t *item)
{
	// switching fields so reset printed text of edit field
	if ((g_editingField==qtrue) && (item->type==ITEM_TYPE_EDITFIELD))
	{
		editFieldDef_t *editPtr = item->typeData.edit;
		if (editPtr)
		{
			editPtr->paintOffset = 0;
		}
	}
}

#ifdef UI_BUILD
qboolean Item_TextField_HandleKey( itemDef_t *item, int key );
void Item_TextField_Paste( itemDef_t *item ) {
	int		pasteLen, i;
	char	buff[2048] = { 0 };

	trap->GetClipboardData( buff, sizeof(buff) );

	if ( !*buff ) {
		return;
	}

	// send as if typed, so insert / overstrike works properly
	pasteLen = strlen( buff );
	for ( i = 0; i < pasteLen; i++ ) {
		Item_TextField_HandleKey( item, (unsigned char)buff[i]|K_CHAR_FLAG );
	}
}
#endif

// Text is UTF-8 with an engine of this tree that says so (v_utf8Text, CL_UTF8() there): a character has 1 to 4
// bytes then, which arrive as one char event each, and the cursor of a field stands between two characters.
static int Item_TextField_CharLen( const char *buff, int pos ) {
	unsigned int codePoint;

	if ( !buff[pos] ) {
		return 0;
	}
	return DC->getCVarValue( "v_utf8Text" ) ? Q_UTF8_Decode( buff + pos, &codePoint ) : 1;
}

static int Item_TextField_PrevChar( const char *buff, int pos ) {
	if ( pos <= 0 ) {
		return 0;
	}
	return DC->getCVarValue( "v_utf8Text" ) ? Q_UTF8_PrevChar( buff, pos ) : pos - 1;
}

qboolean Item_TextField_HandleKey(itemDef_t *item, int key) {
	char buff[2048];
	int len;
	itemDef_t *newItem = NULL;
	editFieldDef_t *editPtr = item->typeData.edit;

	if (item->cvar) {

		buff[0] = 0;
		DC->getCVarString(item->cvar, buff, sizeof(buff));
		len = strlen(buff);
		if (editPtr->maxChars && len > editPtr->maxChars) {
			len = editPtr->maxChars;
		}
		if ( key & K_CHAR_FLAG ) {
			key &= ~K_CHAR_FLAG;

#ifdef UI_BUILD
			if ( key == 'v' - 'a' + 1 ) {	// ctrl-v is paste
				Item_TextField_Paste( item );
				return qtrue;
			}
#endif

			if (key == 'h' - 'a' + 1 )	{	// ctrl-h is backspace
				if ( item->cursorPos > 0 ) {
					const int prev = Item_TextField_PrevChar( buff, item->cursorPos );

					memmove( &buff[prev], &buff[item->cursorPos], len + 1 - item->cursorPos);
					item->cursorPos = prev;
					if (item->cursorPos < editPtr->paintOffset) {
						editPtr->paintOffset = item->cursorPos;
					}
				}
				DC->setCVar(item->cvar, buff);
	    		return qtrue;
			}

			//
			// ignore any non printable chars
			//
			if ( key < 32 || !item->cvar) {
			    return qtrue;
		    }

			if (item->type == ITEM_TYPE_NUMERICFIELD) {
				if (key < '0' || key > '9') {
					return qfalse;
				}
			}

			if (!DC->getOverstrikeMode()) {
				if (( len == MAX_EDITFIELD - 1 ) || (editPtr->maxChars && len >= editPtr->maxChars)) {
					return qtrue;
				}
				memmove( &buff[item->cursorPos + 1], &buff[item->cursorPos], len + 1 - item->cursorPos );
			} else {
				if (editPtr->maxChars && item->cursorPos >= editPtr->maxChars) {
					return qtrue;
				}
			}

			buff[item->cursorPos] = key;

			//rww - nul-terminate!
			if (item->cursorPos+1 < 2048)
			{
				buff[item->cursorPos+1] = 0;
			}
			else
			{
				buff[item->cursorPos] = 0;
			}

			DC->setCVar(item->cvar, buff);

			if (item->cursorPos < len + 1) {
				item->cursorPos++;
				if (editPtr->maxPaintChars && item->cursorPos > editPtr->maxPaintChars) {
					editPtr->paintOffset++;
				}
			}

		} else {

			if ( key == A_DELETE || key == A_KP_PERIOD ) {
				if ( item->cursorPos < len ) {
					const int charLen = Item_TextField_CharLen( buff, item->cursorPos );

					memmove( buff + item->cursorPos, buff + item->cursorPos + charLen, len - item->cursorPos - charLen + 1);
					DC->setCVar(item->cvar, buff);
				}
				return qtrue;
			}

			if ( key == A_CURSOR_RIGHT || key == A_KP_6 )
			{
				const int charLen = Item_TextField_CharLen( buff, item->cursorPos );

				if (editPtr->maxPaintChars && item->cursorPos >= editPtr->maxPaintChars && item->cursorPos < len) {
					item->cursorPos += charLen;
					editPtr->paintOffset += charLen;
					return qtrue;
				}
				if (item->cursorPos < len) {
					item->cursorPos += charLen;
				}
				return qtrue;
			}

			if ( key == A_CURSOR_LEFT || key == A_KP_4 )
			{
				if ( item->cursorPos > 0 ) {
					item->cursorPos = Item_TextField_PrevChar( buff, item->cursorPos );
				}
				if (item->cursorPos < editPtr->paintOffset) {
					editPtr->paintOffset = item->cursorPos;
				}
				return qtrue;
			}

			if ( key == A_HOME || key == A_KP_7) {// || ( tolower(key) == 'a' && trap->Key_IsDown( K_CTRL ) ) ) {
				item->cursorPos = 0;
				editPtr->paintOffset = 0;
				return qtrue;
			}

			if ( key == A_END || key == A_KP_1)  {// ( tolower(key) == 'e' && trap->Key_IsDown( K_CTRL ) ) ) {
				item->cursorPos = len;
				if(item->cursorPos > editPtr->maxPaintChars) {
					editPtr->paintOffset = len - editPtr->maxPaintChars;
				}
				return qtrue;
			}

			if ( key == A_INSERT || key == A_KP_0 ) {
				DC->setOverstrikeMode(!DC->getOverstrikeMode());
				return qtrue;
			}
		}

		if (key == A_TAB || key == A_CURSOR_DOWN || key == A_KP_2)
		{
			// switching fields so reset printed text of edit field
			Leaving_EditField(item);
			g_editingField = qfalse;
			newItem = Menu_SetNextCursorItem((menuDef_t *) item->parent);
			if (newItem && (newItem->type == ITEM_TYPE_EDITFIELD || newItem->type == ITEM_TYPE_NUMERICFIELD))
			{
				g_editItem = newItem;
				g_editingField = qtrue;
			}
		}

		if (key == A_CURSOR_UP || key == A_KP_8)
		{
			// switching fields so reset printed text of edit field
			Leaving_EditField(item);

			g_editingField = qfalse;
			newItem = Menu_SetPrevCursorItem((menuDef_t *) item->parent);
			if (newItem && (newItem->type == ITEM_TYPE_EDITFIELD || newItem->type == ITEM_TYPE_NUMERICFIELD))
			{
				g_editItem = newItem;
				g_editingField = qtrue;
			}
		}

	//	if ( key == A_ENTER || key == A_KP_ENTER || key == A_ESCAPE)  {
		if ( key == A_ENTER || key == A_KP_ENTER || key == A_ESCAPE || (key == A_MOUSE1 && !Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory) )) {
			DC->setOverstrikeMode( qfalse );
			return qfalse;
		}

		return qtrue;
	}
	return qfalse;

}

static void Scroll_TextScroll_AutoFunc (void *p)
{
	scrollInfo_t *si = (scrollInfo_t*)p;

	if (DC->realTime > si->nextScrollTime)
	{
		// need to scroll which is done by simulating a click to the item
		// this is done a bit sideways as the autoscroll "knows" that the item is a listbox
		// so it calls it directly
		Item_TextScroll_HandleKey(si->item, si->scrollKey, qtrue, qfalse);
		si->nextScrollTime = DC->realTime + si->adjustValue;
	}

	if (DC->realTime > si->nextAdjustTime)
	{
		si->nextAdjustTime = DC->realTime + SCROLL_TIME_ADJUST;
		if (si->adjustValue > SCROLL_TIME_FLOOR)
		{
			si->adjustValue -= SCROLL_TIME_ADJUSTOFFSET;
		}
	}
}

static void Scroll_TextScroll_ThumbFunc(void *p)
{
	scrollInfo_t *si = (scrollInfo_t*)p;
	rectDef_t	 r;
	int			 pos;
	int			 max;

	textScrollDef_t *scrollPtr = si->item->typeData.textscroll;

	if (DC->cursory != si->yStart)
	{
		r.x = si->item->window.rect.x + si->item->window.rect.w - SCROLLBAR_SIZE - 1;
		r.y = si->item->window.rect.y + SCROLLBAR_SIZE + 1;
		r.h = si->item->window.rect.h - (SCROLLBAR_SIZE*2) - 2;
		r.w = SCROLLBAR_SIZE;
		max = Item_TextScroll_MaxScroll(si->item);
		//
		pos = (DC->cursory - r.y - SCROLLBAR_SIZE/2) * max / (r.h - SCROLLBAR_SIZE);
		if (pos < 0)
		{
			pos = 0;
		}
		else if (pos > max)
		{
			pos = max;
		}

		scrollPtr->startPos = pos;
		si->yStart = DC->cursory;
	}

	if (DC->realTime > si->nextScrollTime)
	{
		// need to scroll which is done by simulating a click to the item
		// this is done a bit sideways as the autoscroll "knows" that the item is a listbox
		// so it calls it directly
		Item_TextScroll_HandleKey(si->item, si->scrollKey, qtrue, qfalse);
		si->nextScrollTime = DC->realTime + si->adjustValue;
	}

	if (DC->realTime > si->nextAdjustTime)
	{
		si->nextAdjustTime = DC->realTime + SCROLL_TIME_ADJUST;
		if (si->adjustValue > SCROLL_TIME_FLOOR)
		{
			si->adjustValue -= SCROLL_TIME_ADJUSTOFFSET;
		}
	}
}

static void Scroll_ListBox_AutoFunc(void *p) {
	scrollInfo_t *si = (scrollInfo_t*)p;
	if (DC->realTime > si->nextScrollTime) {
		// need to scroll which is done by simulating a click to the item
		// this is done a bit sideways as the autoscroll "knows" that the item is a listbox
		// so it calls it directly
		Item_ListBox_HandleKey(si->item, si->scrollKey, qtrue, qfalse);
		si->nextScrollTime = DC->realTime + si->adjustValue;
	}

	if (DC->realTime > si->nextAdjustTime) {
		si->nextAdjustTime = DC->realTime + SCROLL_TIME_ADJUST;
		if (si->adjustValue > SCROLL_TIME_FLOOR) {
			si->adjustValue -= SCROLL_TIME_ADJUSTOFFSET;
		}
	}
}

static void Scroll_ListBox_ThumbFunc(void *p) {
	scrollInfo_t *si = (scrollInfo_t*)p;
	rectDef_t r;
	int pos, max;

	listBoxDef_t *listPtr = si->item->typeData.listbox;
	if (si->item->window.flags & WINDOW_HORIZONTAL) {
		if (DC->cursorx == si->xStart) {
			return;
		}
		r.x = si->item->window.rect.x + SCROLLBAR_SIZE + 1;
		r.y = si->item->window.rect.y + si->item->window.rect.h - SCROLLBAR_SIZE - 1;
		r.h = SCROLLBAR_SIZE;
		r.w = si->item->window.rect.w - (SCROLLBAR_SIZE*2) - 2;
		max = Item_ListBox_MaxScroll(si->item);
		//
		pos = (DC->cursorx - r.x - SCROLLBAR_SIZE/2) * max / (r.w - SCROLLBAR_SIZE);
		if (pos < 0) {
			pos = 0;
		}
		else if (pos > max) {
			pos = max;
		}
		listPtr->startPos = pos;
		si->xStart = DC->cursorx;
	}
	else if (DC->cursory != si->yStart) {

		r.x = si->item->window.rect.x + si->item->window.rect.w - SCROLLBAR_SIZE - 1;
		r.y = si->item->window.rect.y + SCROLLBAR_SIZE + 1;
		r.h = si->item->window.rect.h - (SCROLLBAR_SIZE*2) - 2;
		r.w = SCROLLBAR_SIZE;
		max = Item_ListBox_MaxScroll(si->item);

		// Multiple rows and columns (since it's more than twice as wide as an element)
		if (( si->item->window.rect.w > (listPtr->elementWidth*2)) &&  (listPtr->elementStyle == LISTBOX_IMAGE))
		{
			int rowLength, rowMax;
			rowLength = si->item->window.rect.w / listPtr->elementWidth;
			rowMax = max / rowLength;

			pos = (DC->cursory - r.y - SCROLLBAR_SIZE/2) * rowMax / (r.h - SCROLLBAR_SIZE);
			pos *= rowLength;
		}
		else
		{
			pos = (DC->cursory - r.y - SCROLLBAR_SIZE/2) * max / (r.h - SCROLLBAR_SIZE);
		}

		if (pos < 0) {
			pos = 0;
		}
		else if (pos > max) {
			pos = max;
		}
		listPtr->startPos = pos;
		si->yStart = DC->cursory;
	}

	if (DC->realTime > si->nextScrollTime) {
		// need to scroll which is done by simulating a click to the item
		// this is done a bit sideways as the autoscroll "knows" that the item is a listbox
		// so it calls it directly
		Item_ListBox_HandleKey(si->item, si->scrollKey, qtrue, qfalse);
		si->nextScrollTime = DC->realTime + si->adjustValue;
	}

	if (DC->realTime > si->nextAdjustTime) {
		si->nextAdjustTime = DC->realTime + SCROLL_TIME_ADJUST;
		if (si->adjustValue > SCROLL_TIME_FLOOR) {
			si->adjustValue -= SCROLL_TIME_ADJUSTOFFSET;
		}
	}
}

// What a slider of a menu with "skin 1" sets and shows are whole numbers: whole units where it reaches over twelve
// of them or more, whole hundredths on a shorter scale, which is shown as percent (1 is 100 %)
static qboolean Item_Slider_Units( const itemDef_t *item ) {
	const editFieldDef_t *editDef = item->typeData.edit;

	return (qboolean)( editDef && editDef->maxVal - editDef->minVal >= 12.0f );
}

static const char *Item_Slider_String( const itemDef_t *item, float value ) {
	if ( !Item_Skinned( item ) ) {
		return va( "%f", value );
	}
	if ( Item_Slider_Units( item ) ) {
		return va( "%i", (int)floorf( value + 0.5f ) );
	}
	return va( "%g", floorf( value * 100.0f + 0.5f ) / 100.0f );
}

// and what it stands at as it is written beside it: whole units, or percent on a short scale
static const char *Item_Slider_Shown( const itemDef_t *item ) {
	const float value = item->cvar ? DC->getCVarValue( item->cvar ) : 0.0f;

	return Item_Slider_Units( item ) ? va( "%i", (int)floorf( value + 0.5f ) ) : va( "%i %%", (int)floorf( value * 100.0f + 0.5f ) );
}

/*
===============================================================================

What a screen of settings changed (VisionJK, "changesCvar <cvar>" of a menu)

When such a menu opens it keeps what each of its settings is. Every frame it counts the settings that differ from
that and sets <cvar> to the number and <cvar>Restart to 1 while one of them takes effect only after a restart (a
cvar with CVAR_LATCH, or one of the video settings the ui hands to the renderer with the restart: ui_r_*).
An ITEM_TYPE_CHANGES lists them with a button to undo each; uiScript changesRevert sets them all
back, uiScript changesApply keeps them (the menu file restarts the video in the same click where needed).

===============================================================================
*/

#define MENU_CHANGES		384
#define MENU_CHANGE_VALUE	128

static struct {
	menuDef_t	*menu;
	int			count;
	struct {
		itemDef_t	*item;
		char		value[MENU_CHANGE_VALUE];
	} rows[MENU_CHANGES];
	int			changed;
	int			onPage[MENU_PAGES];
	qboolean	restart;
} menuChanges;

// a setting of a screen of settings: an item that sets a cvar by itself
static qboolean Item_IsSetting( const itemDef_t *item ) {
	// (a row without a name is a switch of the screen itself: Essentials / All Settings)
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	if ( !item->cvar || !item->cvar[0] || !item->text || !item->text[0] ) {
		return qfalse;
	}
	// (nor is the search of the screen)
	if ( menu && menu->searchCvar && !Q_stricmp( item->cvar, menu->searchCvar ) ) {
		return qfalse;
	}
	switch ( item->type ) {
	case ITEM_TYPE_YESNO:
	case ITEM_TYPE_MULTI:
	case ITEM_TYPE_SLIDER:
	case ITEM_TYPE_EDITFIELD:
	case ITEM_TYPE_NUMERICFIELD:
		return qtrue;
	default:
		return qfalse;
	}
}

// are two values of a cvar the same setting ("1" and "1.000" are)
static qboolean Value_Same( const char *a, const char *b ) {
	char *endA, *endB;
	double x, y;

	if ( !strcmp( a, b ) ) {
		return qtrue;
	}
	x = strtod( a, &endA );
	y = strtod( b, &endB );
	if ( endA != a && !*endA && endB != b && !*endB ) {
		return (qboolean)( fabs( x - y ) < 0.0005 );
	}
	return qfalse;
}

// does a change of this cvar take effect only after a restart
static qboolean Cvar_NeedsRestart( const char *cvar ) {
	char		value[8];
	uint32_t	flags = 0;

	if ( !Q_stricmpn( cvar, "ui_r_", 5 ) ) {
		return qtrue;
	}
	if ( DC->getCVarInfo && DC->getCVarInfo( cvar, value, sizeof( value ), &flags ) ) {
		return (qboolean)( ( flags & CVAR_LATCH ) != 0 );
	}
	return qfalse;
}

/*
=================
Item_ValueText

A value of the cvar of a setting as the setting shows it: the name of the choice it is, on or off, the number of
a slider as its row writes it.
=================
*/
static void Item_ValueText( const itemDef_t *item, const char *value, char *out, int size ) {
	int i;

	if ( item->type == ITEM_TYPE_MULTI && item->typeData.multi ) {
		const multiDef_t *multi = item->typeData.multi;

		for ( i = 0; i < multi->count; i++ ) {
			if ( multi->strDef ? ( multi->cvarStr[i] && !Q_stricmp( multi->cvarStr[i], value ) )
				: Value_Same( va( "%g", multi->cvarValue[i] ), value ) ) {
				Text_Word( multi->cvarList[i], out, size, qfalse, qfalse );
				return;
			}
		}
	} else if ( item->type == ITEM_TYPE_YESNO ) {
		Q_strncpyz( out, Vision_Text( atof( value ) != 0.0 ? "On" : "Off" ), size );
		return;
	} else if ( item->type == ITEM_TYPE_SLIDER ) {
		const float v = (float)atof( value );

		Q_strncpyz( out, Item_Slider_Units( item ) ? va( "%i", (int)floorf( v + 0.5f ) ) : va( "%i %%", (int)floorf( v * 100.0f + 0.5f ) ),
			size );
		return;
	} else if ( item->row == 2 ) {
		// (a password)
		Q_strncpyz( out, value[0] ? "***" : "", size );
		return;
	}
	Q_strncpyz( out, value, size );
}

void Menu_ChangesStart( menuDef_t *menu ) {
	int i;

	if ( !menu || !menu->changesCvar ) {
		return;
	}
	memset( &menuChanges, 0, sizeof( menuChanges ) );
	menuChanges.menu = menu;
	for ( i = 0; i < menu->itemCount && menuChanges.count < MENU_CHANGES; i++ ) {
		itemDef_t *item = menu->items[i];

		if ( Item_IsSetting( item ) ) {
			menuChanges.rows[menuChanges.count].item = item;
			DC->getCVarString( item->cvar, menuChanges.rows[menuChanges.count].value, MENU_CHANGE_VALUE );
			menuChanges.count++;
		}
	}
	DC->setCVar( menu->changesCvar, "0" );
	DC->setCVar( va( "%sRestart", menu->changesCvar ), "0" );
	DC->setCVar( va( "%sState", menu->changesCvar ), "0" );
}

// is a setting of the open screen changed since it opened
static qboolean Changes_Row( int n ) {
	char now[MENU_CHANGE_VALUE];

	DC->getCVarString( menuChanges.rows[n].item->cvar, now, sizeof( now ) );
	return (qboolean)!Value_Same( now, menuChanges.rows[n].value );
}

static void Menu_ChangesFrame( menuDef_t *menu ) {
	int i, changed = 0;
	qboolean restart = qfalse;

	if ( !menu || menu != menuChanges.menu || !menu->changesCvar ) {
		return;
	}
	memset( menuChanges.onPage, 0, sizeof( menuChanges.onPage ) );
	for ( i = 0; i < menuChanges.count; i++ ) {
		const itemDef_t *item = menuChanges.rows[i].item;

		// (a setting two rows share, as the old menus had for one cvar on two pages, counts once)
		if ( !Changes_Row( i ) ) {
			continue;
		}
		changed++;
		if ( item->page > 0 && item->page < MENU_PAGES ) {
			menuChanges.onPage[item->page]++;
		}
		if ( Cvar_NeedsRestart( item->cvar ) ) {
			restart = qtrue;
		}
	}
	if ( changed != menuChanges.changed || restart != menuChanges.restart ) {
		menuChanges.changed = changed;
		menuChanges.restart = restart;
		DC->setCVar( menu->changesCvar, va( "%i", changed ) );
		DC->setCVar( va( "%sRestart", menu->changesCvar ), restart ? "1" : "0" );
		// (one cvar for the buttons of the menu file: 0 nothing changed, 1 changed, 2 changed and a restart needed)
		DC->setCVar( va( "%sState", menu->changesCvar ), !changed ? "0" : restart ? "2" : "1" );
	}
}

// the settings of a page that are changed, for the mark on the button of the page (Item_Tab_Paint)
static int Menu_ChangesOnPage( const menuDef_t *menu, int page ) {
	return ( menu == menuChanges.menu && page > 0 && page < MENU_PAGES ) ? menuChanges.onPage[page] : 0;
}

static void Changes_Undo( int n ) {
	DC->setCVar( menuChanges.rows[n].item->cvar, menuChanges.rows[n].value );
}

void Menu_ChangesRevert( void ) {
	int i;

	for ( i = 0; i < menuChanges.count; i++ ) {
		if ( Changes_Row( i ) ) {
			Changes_Undo( i );
		}
	}
}

void Menu_ChangesApply( void ) {
	int i;

	for ( i = 0; i < menuChanges.count; i++ ) {
		DC->getCVarString( menuChanges.rows[i].item->cvar, menuChanges.rows[i].value, MENU_CHANGE_VALUE );
	}
}

static void Row_Text( float x, float y, float h, float room, int align, float scale, vec4_t color, const char *text, int font );

// the text scale for a size of letters in pixels of the canvas of a menu (16 pixels: a scale of 16 / 18), or for
// units of the grid in a menu without one
static float Item_Pixels( const itemDef_t *item, float pixels ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;

	return ( menu && menu->canvasW > 0.0f ? pixels * menu->canvasUnit : pixels * ( 480.0f / 900.0f ) ) / 18.0f;
}

// where the rows of an ITEM_TYPE_CHANGES are: below its heading, each as high as two lines
static float Item_Changes_Layout( const itemDef_t *item, float *top, float *rowH, float *undo ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const float		u = ( menu && menu->canvasW > 0.0f ) ? menu->canvasUnit : ( 480.0f / 900.0f );

	*top = item->window.rect.y + 30.0f * u;
	*rowH = 48.0f * u;
	*undo = 36.0f * u;
	return u;
}

/*
=================
Item_Changes_Paint

The settings the screen changed since it opened: a heading with their number, then for each its name and the
value it had and has now, and a button that sets it back. (The pane of Setup of the redesign.)
=================
*/
static void Item_Changes_Paint( itemDef_t *item ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const rectDef_t	*r = &item->window.rect;
	const float		*accent = menu->focusColor;
	const int		font = item->iMenuFont;
	float			top, rowH, undo, y, u;
	vec4_t			muted = { 0.56f, 0.64f, 0.74f, 1.0f }, text = { 0.86f, 0.90f, 0.945f, 1.0f }, faint = { 0.455f, 0.537f, 0.647f, 1.0f };
	char			word[256], from[128], to[128];
	int				i, hidden = 0;

	if ( menu != menuChanges.menu ) {
		return;
	}
	u = Item_Changes_Layout( item, &top, &rowH, &undo );
	Text_Word( Vision_Text( "Changed settings" ), word, sizeof( word ), qtrue, qfalse );
	Row_Text( r->x, r->y, 18.0f * u, 0.0f, ITEM_ALIGN_LEFT, Item_Pixels( item, 12.0f ), muted, word, font );
	Row_Text( r->x + r->w, r->y, 18.0f * u, 0.0f, ITEM_ALIGN_RIGHT, Item_Pixels( item, 14.0f ), (float *)accent,
		va( "%i", menuChanges.changed ), font );
	if ( !menuChanges.changed ) {
		Row_Text( r->x, top, rowH * 0.5f, r->w, ITEM_ALIGN_LEFT, Item_Pixels( item, 15.0f ), faint, Vision_Text( "Nothing changed yet." ), font );
		return;
	}
	y = top;
	for ( i = 0; i < menuChanges.count; i++ ) {
		const itemDef_t	*row = menuChanges.rows[i].item;
		const qboolean	over = (qboolean)( DC->cursorx >= r->x + r->w - undo && DC->cursorx < r->x + r->w
							&& DC->cursory >= y + ( rowH - undo ) * 0.5f && DC->cursory < y + ( rowH + undo ) * 0.5f );
		char			now[MENU_CHANGE_VALUE];
		float			fw;
		vec4_t			edge;

		if ( !Changes_Row( i ) ) {
			continue;
		}
		if ( y + rowH > r->y + r->h ) {
			hidden++;
			continue;
		}
		DC->getCVarString( row->cvar, now, sizeof( now ) );
		Text_Word( row->text, word, sizeof( word ), qfalse, qtrue );
		Item_ValueText( row, menuChanges.rows[i].value, from, sizeof( from ) );
		Item_ValueText( row, now, to, sizeof( to ) );
		Row_Text( r->x, y + rowH * 0.08f, rowH * 0.46f, r->w - undo - 8.0f * u, ITEM_ALIGN_LEFT, Item_Pixels( item, 15.0f ),
			text, word, font );
		Row_Text( r->x, y + rowH * 0.5f, rowH * 0.42f, 0.0f, ITEM_ALIGN_LEFT, Item_Pixels( item, 13.0f ), muted, from, font );
		fw = DC->textWidth( from, Item_Pixels( item, 13.0f ), font );
		Skin_Arrow( r->x + fw + 5.0f * u, y + rowH * 0.71f - 3.0f * u, 3.5f * u, 6.0f * u, qfalse, muted );
		Row_Text( r->x + fw + 13.0f * u, y + rowH * 0.5f, rowH * 0.42f, 0.0f, ITEM_ALIGN_LEFT, Item_Pixels( item, 13.0f ),
			(float *)accent, to, font );
		// the button that sets it back
		VectorCopy4( over ? accent : skinEdge, edge );
		Skin_Panel( r->x + r->w - undo, y + ( rowH - undo ) * 0.5f, undo, undo, 6.0f * u, NULL, edge );
		Skin_Arrow( r->x + r->w - undo * 0.5f - undo * 0.12f, y + rowH * 0.5f - undo * 0.16f, undo * 0.2f, undo * 0.32f, qtrue,
			over ? accent : muted );
		Skin_Line( r->x, y + rowH - 1.0f, r->w, skinRule );
		y += rowH;
	}
	if ( hidden ) {
		Row_Text( r->x, y, rowH * 0.5f, r->w, ITEM_ALIGN_LEFT, Item_Pixels( item, 13.0f ), muted, va( "+%i", hidden ), font );
	}
}

// a click on the button of a row of an ITEM_TYPE_CHANGES sets that setting back
static qboolean Item_Changes_HandleKey( itemDef_t *item, int key ) {
	const rectDef_t	*r = &item->window.rect;
	float			top, rowH, undo, y;
	int				i;

	if ( key != A_MOUSE1 || item->parent != menuChanges.menu ) {
		return qfalse;
	}
	Item_Changes_Layout( item, &top, &rowH, &undo );
	y = top;
	for ( i = 0; i < menuChanges.count; i++ ) {
		if ( !Changes_Row( i ) ) {
			continue;
		}
		if ( y + rowH > r->y + r->h ) {
			break;
		}
		if ( DC->cursorx >= r->x + r->w - undo && DC->cursorx < r->x + r->w
			&& DC->cursory >= y + ( rowH - undo ) * 0.5f && DC->cursory < y + ( rowH + undo ) * 0.5f ) {
			Changes_Undo( i );
			DC->startLocalSound( DC->Assets.itemFocusSound, CHAN_LOCAL_SOUND );
			return qtrue;
		}
		y += rowH;
	}
	return qfalse;
}

static void Scroll_Slider_ThumbFunc(void *p) {
	float x, value, cursorx, width;
	scrollInfo_t *si = (scrollInfo_t*)p;
	editFieldDef_t *editDef = si->item->typeData.edit;

	x = Item_Slider_Track( si->item, &width );

	cursorx = DC->cursorx;

	if (cursorx < x) {
		cursorx = x;
	} else if (cursorx > x + width) {
		cursorx = x + width;
	}
	value = cursorx - x;
	value /= width;
	value *= (editDef->maxVal - editDef->minVal);
	value += editDef->minVal;
	DC->setCVar(si->item->cvar, Item_Slider_String(si->item, value));
}

void Item_StartCapture(itemDef_t *item, int key)
{
	int flags;
	switch (item->type)
	{
	    case ITEM_TYPE_EDITFIELD:
		case ITEM_TYPE_NUMERICFIELD:
		case ITEM_TYPE_LISTBOX:
		{
			flags = Item_ListBox_OverLB(item, DC->cursorx, DC->cursory);
			if (flags & (WINDOW_LB_LEFTARROW | WINDOW_LB_RIGHTARROW)) {
				scrollInfo.nextScrollTime = DC->realTime + SCROLL_TIME_START;
				scrollInfo.nextAdjustTime = DC->realTime + SCROLL_TIME_ADJUST;
				scrollInfo.adjustValue = SCROLL_TIME_START;
				scrollInfo.scrollKey = key;
				scrollInfo.scrollDir = (flags & WINDOW_LB_LEFTARROW) ? qtrue : qfalse;
				scrollInfo.item = item;
				captureData = &scrollInfo;
				captureFunc = &Scroll_ListBox_AutoFunc;
				itemCapture = item;
			} else if (flags & WINDOW_LB_THUMB) {
				scrollInfo.scrollKey = key;
				scrollInfo.item = item;
				scrollInfo.xStart = DC->cursorx;
				scrollInfo.yStart = DC->cursory;
				captureData = &scrollInfo;
				captureFunc = &Scroll_ListBox_ThumbFunc;
				itemCapture = item;
			}
			break;
		}

		case ITEM_TYPE_TEXTSCROLL:
			flags = Item_TextScroll_OverLB (item, DC->cursorx, DC->cursory);
			if (flags & (WINDOW_LB_LEFTARROW | WINDOW_LB_RIGHTARROW))
			{
				scrollInfo.nextScrollTime = DC->realTime + SCROLL_TIME_START;
				scrollInfo.nextAdjustTime = DC->realTime + SCROLL_TIME_ADJUST;
				scrollInfo.adjustValue = SCROLL_TIME_START;
				scrollInfo.scrollKey = key;
				scrollInfo.scrollDir = (flags & WINDOW_LB_LEFTARROW) ? qtrue : qfalse;
				scrollInfo.item = item;
				captureData = &scrollInfo;
				captureFunc = &Scroll_TextScroll_AutoFunc;
				itemCapture = item;
			}
			else if (flags & WINDOW_LB_THUMB)
			{
				scrollInfo.scrollKey = key;
				scrollInfo.item = item;
				scrollInfo.xStart = DC->cursorx;
				scrollInfo.yStart = DC->cursory;
				captureData = &scrollInfo;
				captureFunc = &Scroll_TextScroll_ThumbFunc;
				itemCapture = item;
			}
			break;

		case ITEM_TYPE_PAGESCROLL:
			scrollInfo.scrollKey = key;
			scrollInfo.item = item;
			scrollInfo.xStart = DC->cursorx;
			scrollInfo.yStart = DC->cursory;
			captureData = &scrollInfo;
			captureFunc = &Scroll_PageScroll_ThumbFunc;
			itemCapture = item;
			break;

		case ITEM_TYPE_SLIDER:
		{
			flags = Item_Slider_OverSlider(item, DC->cursorx, DC->cursory);
			if (flags & WINDOW_LB_THUMB) {
				scrollInfo.scrollKey = key;
				scrollInfo.item = item;
				scrollInfo.xStart = DC->cursorx;
				scrollInfo.yStart = DC->cursory;
				captureData = &scrollInfo;
				captureFunc = &Scroll_Slider_ThumbFunc;
				itemCapture = item;
			}
			break;
		}
	}
}

void Item_StopCapture(itemDef_t *item) {

}

qboolean Item_Slider_HandleKey(itemDef_t *item, int key, qboolean down) {
	float x, value, width, work;

	//DC->Print("slider handle key\n");
	if (item->window.flags & WINDOW_HASFOCUS && item->cvar && Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory)) {
		if (key == A_MOUSE1 || key == A_ENTER || key == A_MOUSE2 || key == A_MOUSE3) {
			editFieldDef_t *editDef = item->typeData.edit;
			if (editDef) {
				rectDef_t testRect;
				width = SLIDER_WIDTH;
				if (item->text) {
					x = item->textRect.x + item->textRect.w + 8;
				} else {
					x = item->window.rect.x;
				}

				testRect = item->window.rect;
				testRect.x = x;
				value = (float)SLIDER_THUMB_WIDTH / 2;
				testRect.x -= value;
				//DC->Print("slider x: %f\n", testRect.x);
				testRect.w = (SLIDER_WIDTH + (float)SLIDER_THUMB_WIDTH / 2);
				//DC->Print("slider w: %f\n", testRect.w);
				if (Rect_ContainsPoint(&testRect, DC->cursorx, DC->cursory)) {
					work = DC->cursorx - x;
					value = work / width;
					value *= (editDef->maxVal - editDef->minVal);
					// vm fuckage
					// value = (((float)(DC->cursorx - x)/ SLIDER_WIDTH) * (editDef->maxVal - editDef->minVal));
					value += editDef->minVal;
					DC->setCVar(item->cvar, Item_Slider_String(item, value));
					return qtrue;
				}
			}
		}
	}
	//DC->Print("slider handle key exit\n");
	return qfalse;
}


// moves a slider by a twentieth of its range, for the keys
static void Item_Slider_Nudge( itemDef_t *item, int dir ) {
	const editFieldDef_t	*editDef = item->typeData.edit;
	float					step, value;

	if ( !editDef || !item->cvar ) {
		return;
	}
	step = ( editDef->maxVal - editDef->minVal ) / 20.0f;
	step = Item_Slider_Units( item ) ? Q_max( 1.0f, floorf( step + 0.5f ) ) : Q_max( 0.01f, step );
	value = Com_Clamp( editDef->minVal, editDef->maxVal, DC->getCVarValue( item->cvar ) + dir * step );
	DC->setCVar( item->cvar, Item_Slider_String( item, value ) );
}

/*
=================
Item_Row_Toggle

A row with two states shows both and marks the one that is set: on and off, or the two settings of a choice
whose names both have room. True: this row is one, with the two words (in capital letters), which of them is set
and the scale they fit at.
=================
*/
static qboolean Item_Row_Toggle( itemDef_t *item, const rectDef_t *widget, char names[2][64], int *sel, float *scale ) {
	const float	room = widget->w * 0.5f - 5.0f;
	float		widest;

	if ( item->type == ITEM_TYPE_YESNO ) {
		const qboolean set = (qboolean)( item->cvar && DC->getCVarValue( item->cvar ) != 0 );

		Text_Word( "@MENUS_OFF", names[0], 64, qtrue, qfalse );
		Text_Word( "@MENUS_ON", names[1], 64, qtrue, qfalse );
		*sel = ( item->invertYesNo ? !set : set ) ? 1 : 0;
	} else if ( item->type == ITEM_TYPE_MULTI && item->typeData.multi && item->typeData.multi->count == 2 && !item->special ) {
		Text_Word( item->typeData.multi->cvarList[0], names[0], 64, qtrue, qfalse );
		Text_Word( item->typeData.multi->cvarList[1], names[1], 64, qtrue, qfalse );
		*sel = Item_Multi_FindCvarByValue( item );
	} else {
		return qfalse;
	}
	*scale = item->textscale * ROW_SMALL;
	widest = Q_max( DC->textWidth( names[0], *scale, item->iMenuFont ), DC->textWidth( names[1], *scale, item->iMenuFont ) );
	if ( widest > room ) {
		// a little smaller is fine, much smaller is not: such a pair is shown one at a time, between arrows
		if ( item->type == ITEM_TYPE_MULTI && widest > room * 1.12f ) {
			return qfalse;
		}
		*scale *= room / widest;
	}
	return qtrue;
}

/*
=================
Item_Row_HandleKey

The keys and the clicks of a switch, a choice or a slider that is drawn as a row. A click on a half of a switch
sets that state, one on an arrow of a choice goes that way, and one on the track of a slider sets it. Left and
right do the same from the keys. True: the setting was changed (the caller runs its action script).
=================
*/
static qboolean Item_Row_HandleKey( itemDef_t *item, int key ) {
	const qboolean	mouse = (qboolean)( key == A_MOUSE1 || key == A_MOUSE2 || key == A_MOUSE3 );
	const qboolean	left = (qboolean)( key == A_CURSOR_LEFT || key == A_KP_4 ), right = (qboolean)( key == A_CURSOR_RIGHT || key == A_KP_6 );
	const qboolean	enter = (qboolean)( key == A_ENTER || key == A_KP_ENTER );
	const float		cx = DC->cursorx;
	rectDef_t		widget;
	qboolean		over;
	char			names[2][64];
	int				sel;
	float			scale;

	if ( !item->cvar || !( item->window.flags & WINDOW_HASFOCUS ) || ( !mouse && !left && !right && !enter ) ) {
		return qfalse;
	}
	if ( mouse && !Rect_ContainsPoint( &item->window.rect, DC->cursorx, DC->cursory ) ) {
		return qfalse;
	}
	Item_Row_Widget( item, &widget );
	over = (qboolean)( mouse && cx >= widget.x && cx <= widget.x + widget.w );

	if ( item->type == ITEM_TYPE_SLIDER ) {
		const editFieldDef_t *editDef = item->typeData.edit;

		if ( !editDef ) {
			return qfalse;
		}
		if ( mouse ) {
			float w, x = Item_Slider_Track( item, &w );

			if ( cx < x - 4.0f || cx > x + w + 4.0f ) {
				return qfalse;
			}
			DC->setCVar( item->cvar, Item_Slider_String( item,
				editDef->minVal + Com_Clamp( 0.0f, 1.0f, ( cx - x ) / w ) * ( editDef->maxVal - editDef->minVal ) ) );
			return qtrue;
		}
		if ( left || right ) {
			Item_Slider_Nudge( item, right ? 1 : -1 );
			return qtrue;
		}
		return qfalse;
	}

	if ( Item_Row_Toggle( item, &widget, names, &sel, &scale ) ) {
		int to = !sel;

		if ( over ) {
			to = ( cx >= widget.x + widget.w * 0.5f );
		} else if ( left || right ) {
			to = right;
		}
		if ( to == sel ) {
			return qfalse;
		}
		if ( item->type == ITEM_TYPE_YESNO ) {
			DC->setCVar( item->cvar, va( "%i", item->invertYesNo ? !to : to ) );
		} else {
			Item_Multi_Set( item, to );
		}
		return qtrue;
	}

	if ( item->type == ITEM_TYPE_MULTI ) {
		const int	count = Item_Multi_CountSettings( item );
		int			dir = 1;

		if ( count <= 0 ) {
			return qfalse;
		}
		if ( over ) {
			dir = ( cx < widget.x + widget.w * 0.5f ) ? -1 : 1;
		} else if ( left || key == A_MOUSE2 ) {
			dir = -1;
		}
		Item_Multi_Set( item, ( Item_Multi_FindCvarByValue( item ) + dir + count ) % count );
		return qtrue;
	}
	return qfalse;
}

qboolean Item_HandleKey(itemDef_t *item, int key, qboolean down) {
	if (itemCapture) {
		Item_StopCapture(itemCapture);
		itemCapture = NULL;
		captureFunc = 0;
		captureData = NULL;
	} else {
		if ( down && ( key == A_MOUSE1 || key == A_MOUSE2 || key == A_MOUSE3 ) ) {
			Item_StartCapture(item, key);
		}
	}

	if (!down) {
		return qfalse;
	}

	if ( Item_IsRow( item ) && ( item->type == ITEM_TYPE_YESNO || item->type == ITEM_TYPE_MULTI || item->type == ITEM_TYPE_SLIDER ) ) {
		return Item_Row_HandleKey( item, key );
	}

	switch (item->type) {
	case ITEM_TYPE_CHANGES:
		return Item_Changes_HandleKey( item, key );
	case ITEM_TYPE_BUTTON:
		return qfalse;
		break;
	case ITEM_TYPE_RADIOBUTTON:
		return qfalse;
		break;
	case ITEM_TYPE_CHECKBOX:
		return qfalse;
		break;
	case ITEM_TYPE_EDITFIELD:
	case ITEM_TYPE_NUMERICFIELD:
		if (key == A_MOUSE1 || key == A_MOUSE2 || key == A_ENTER)
		{
			editFieldDef_t *editPtr = item->typeData.edit;

			if (item->cvar && editPtr)
			{
				editPtr->paintOffset = 0;
			}

			//return Item_TextField_HandleKey(item, key);
		}
		return qfalse;
		break;
	case ITEM_TYPE_COMBO:
		return qfalse;
		break;
	case ITEM_TYPE_LISTBOX:
		return Item_ListBox_HandleKey(item, key, down, qfalse);
		break;
	case ITEM_TYPE_TEXTSCROLL:
		return Item_TextScroll_HandleKey(item, key, down, qfalse);
		break;
	case ITEM_TYPE_YESNO:
		return Item_YesNo_HandleKey(item, key);
		break;
	case ITEM_TYPE_MULTI:
		return Item_Multi_HandleKey(item, key);
		break;
	case ITEM_TYPE_OWNERDRAW:
		return Item_OwnerDraw_HandleKey(item, key);
		break;
	case ITEM_TYPE_BIND:
		return Item_Bind_HandleKey(item, key, down);
		break;
	case ITEM_TYPE_SLIDER:
		return Item_Slider_HandleKey(item, key, down);
		break;
		//case ITEM_TYPE_IMAGE:
		//  Item_Image_Paint(item);
		//  break;
	default:
		return qfalse;
		break;
	}
}


//JLFACCEPT MPMOVED
/*
-----------------------------------------
Item_HandleAccept
	If Item has an accept script, run it.
-------------------------------------------
*/
qboolean Item_HandleAccept(itemDef_t * item)
{
	if (item->accept)
	{
		Item_RunScript(item, item->accept);
		return qtrue;
	}
	return qfalse;
}

void Item_Action(itemDef_t *item) {
  if (item) {
    menuDef_t *menu = (menuDef_t *)item->parent;

    // "fold <n>": a heading of a list folds its rows away, or brings them back
    if ( item->fold && menu ) {
      Menu_ToggleFold( menu, item->fold );
      Item_RunScript( item, item->action );
      return;
    }
    // "tab <n>": the button opens that page of its menu, and ends a search
    if ( item->tab ) {
      if ( menu && menu->searchCvar && menu->searching ) {
        DC->setCVar( menu->searchCvar, "" );
        Menu_ListCheck( menu );
      }
      Menu_SetPage( menu, item->tab );
    }
    Item_RunScript(item, item->action);
  }
}



itemDef_t *Menu_SetPrevCursorItem(menuDef_t *menu) {
	qboolean wrapped = qfalse;
	int oldCursor = menu->cursorItem;

	if (menu->cursorItem < 0) {
		menu->cursorItem = menu->itemCount-1;
		wrapped = qtrue;
	}

	while (menu->cursorItem > -1)
	{
		menu->cursorItem--;
		if (menu->cursorItem < 0) {
			if (wrapped)
			{
				break;
			}
			wrapped = qtrue;
			menu->cursorItem = menu->itemCount -1;
		}

		if (Item_SetFocus(menu->items[menu->cursorItem], DC->cursorx, DC->cursory)) {
			Menu_HandleMouseMove(menu, menu->items[menu->cursorItem]->window.rect.x + 1, menu->items[menu->cursorItem]->window.rect.y + 1);
			return menu->items[menu->cursorItem];
		}
	}
	menu->cursorItem = oldCursor;
	return NULL;

}

itemDef_t *Menu_SetNextCursorItem(menuDef_t *menu) {

  qboolean wrapped = qfalse;
	int oldCursor = menu->cursorItem;


  if (menu->cursorItem == -1) {
    menu->cursorItem = 0;
    wrapped = qtrue;
  }

  while (menu->cursorItem < menu->itemCount) {

    menu->cursorItem++;
    if (menu->cursorItem >= menu->itemCount && !wrapped) {
      wrapped = qtrue;
      menu->cursorItem = 0;
    }
		if (Item_SetFocus(menu->items[menu->cursorItem], DC->cursorx, DC->cursory)) {
			Menu_HandleMouseMove(menu, menu->items[menu->cursorItem]->window.rect.x + 1, menu->items[menu->cursorItem]->window.rect.y + 1);
      return menu->items[menu->cursorItem];
    }

  }

	menu->cursorItem = oldCursor;
	return NULL;
}

static void Window_CloseCinematic(windowDef_t *window) {
	if (window->style == WINDOW_STYLE_CINEMATIC && window->cinematic >= 0) {
		DC->stopCinematic(window->cinematic);
		window->cinematic = -1;
	}
}

static void Menu_CloseCinematics(menuDef_t *menu) {
	if (menu) {
		int i;
		Window_CloseCinematic(&menu->window);
	  for (i = 0; i < menu->itemCount; i++) {
		  Window_CloseCinematic(&menu->items[i]->window);
			if (menu->items[i]->type == ITEM_TYPE_OWNERDRAW) {
				DC->stopCinematic(0-menu->items[i]->window.ownerDraw);
			}
	  }
	}
}

static void Display_CloseCinematics() {
	int i;
	for (i = 0; i < menuCount; i++) {
		Menu_CloseCinematics(&Menus[i]);
	}
}

void  Menus_Activate(menuDef_t *menu) {

	menu->window.flags |= (WINDOW_HASFOCUS | WINDOW_VISIBLE);
	// a search is over when its menu opens again ("searchCvar")
	if ( menu->searchCvar ) {
		DC->setCVar( menu->searchCvar, "" );
	}
	if (menu->onOpen) {
		itemDef_t item;
		item.parent = menu;
		Item_RunScript(&item, menu->onOpen);
	}
	// (after onOpen: it may read settings of the engine into the cvars of the screen)
	Menu_ChangesStart( menu );

	if (menu->soundName && *menu->soundName) {
//		DC->stopBackgroundTrack();					// you don't want to do this since it will reset s_rawend
		DC->startBackgroundTrack(menu->soundName, menu->soundName, qfalse);
	}

	menu->appearanceTime = 0;
	menu->openTime = DC->realTime;
	Display_CloseCinematics();

}

int Display_VisibleMenuCount() {
	int i, count;
	count = 0;
	for (i = 0; i < menuCount; i++) {
		if (Menus[i].window.flags & (WINDOW_FORCED | WINDOW_VISIBLE)) {
			count++;
		}
	}
	return count;
}

void Menus_HandleOOBClick(menuDef_t *menu, int key, qboolean down) {
	if (menu) {
		int i;
		// basically the behaviour we are looking for is if there are windows in the stack.. see if
		// the cursor is within any of them.. if not close them otherwise activate them and pass the
		// key on.. force a mouse move to activate focus and script stuff
		if (down && menu->window.flags & WINDOW_OOB_CLICK) {
			Menu_RunCloseScript(menu);
			menu->window.flags &= ~(WINDOW_HASFOCUS | WINDOW_VISIBLE);
		}

		for (i = 0; i < menuCount; i++) {
			if (Menu_OverActiveItem(&Menus[i], DC->cursorx, DC->cursory)) {
				Menu_RunCloseScript(menu);
				menu->window.flags &= ~(WINDOW_HASFOCUS | WINDOW_VISIBLE);
			//	Menus_Activate(&Menus[i]);
				Menu_HandleMouseMove(&Menus[i], DC->cursorx, DC->cursory);
				Menu_HandleKey(&Menus[i], key, down);
			}
		}

		if (Display_VisibleMenuCount() == 0) {
			if (DC->Pause) {
				DC->Pause(qfalse);
			}
		}
		Display_CloseCinematics();
	}
}


void Menu_HandleKey(menuDef_t *menu, int key, qboolean down) {
	int i;
	itemDef_t *item = NULL;

	if (g_waitingForKey && down) {
		Item_Bind_HandleKey(g_bindItem, key, down);
		return;
	}

	if (g_editingField && down)
	{
		if (!Item_TextField_HandleKey(g_editItem, key))
		{
			// (in a menu with the new look a click beside the field also does what it was aimed at, a tab or a
			// heading: one click, not two)
			const qboolean goOn = (qboolean)( key == A_MOUSE1 && menu && menu->skin );

			g_editingField = qfalse;
			g_editItem = NULL;
			if ( !goOn ) {
				return;
			}
			// (what is under the pointer was not looked at while the field took the keys)
			Display_MouseMove( NULL, DC->cursorx, DC->cursory );
		}
		else if (key == A_MOUSE1 || key == A_MOUSE2 || key == A_MOUSE3)
		{
			// switching fields so reset printed text of edit field
			Leaving_EditField(g_editItem);
			g_editingField = qfalse;
			g_editItem = NULL;
			Display_MouseMove(NULL, DC->cursorx, DC->cursory);
		}
		else if (key == A_TAB || key == A_CURSOR_UP || key == A_CURSOR_DOWN)
		{
			return;
		}
	}

	if (menu == NULL) {
		return;
	}
		// see if the mouse is within the window bounds and if so is this a mouse click
	if (down && !(menu->window.flags & WINDOW_POPUP) && !Menu_ContainsPoint(menu, DC->cursorx, DC->cursory))
	{
		static qboolean inHandleKey = qfalse;
		if (!inHandleKey && ( key == A_MOUSE1 || key == A_MOUSE2 || key == A_MOUSE3 ) ) {
			inHandleKey = qtrue;
			Menus_HandleOOBClick(menu, key, down);
			inHandleKey = qfalse;
			return;
		}
	}

	// get the item with focus
	for (i = 0; i < menu->itemCount; i++) {
		if (menu->items[i]->window.flags & WINDOW_HASFOCUS) {
			item = menu->items[i];
		}
	}

	// Ignore if disabled
	if (item && item->disabled)
	{
		return;
	}

	if (item != NULL) {
		if (Item_HandleKey(item, key, down))
		{
			// It is possible for an item to be disable after Item_HandleKey is run (like in Voice Chat)
			if (!item->disabled)
			{
				Item_Action(item);
			}
			return;
		}
	}

	if (!down) {
		return;
	}

	// default handling
	switch ( key ) {

		case A_F11:
			if (DC->getCVarValue("developer")) {
				debugMode ^= 1;
			}
			break;

		case A_F12:
			if (DC->getCVarValue("developer")) {
				switch ( DC->screenshotFormat ) {
					case SSF_JPEG:
						DC->executeText(EXEC_APPEND, "screenshot\n");
						break;
					case SSF_TGA:
						DC->executeText(EXEC_APPEND, "screenshot_tga\n");
						break;
					case SSF_PNG:
						DC->executeText(EXEC_APPEND, "screenshot_png\n");
						break;
					default:
						if (DC->Print) {
							DC->Print(S_COLOR_YELLOW "Menu_HandleKey[F12]: Unknown screenshot format assigned! This should not happen.\n");
						}
						break;
				}
			}
			break;
		case A_KP_8:
		case A_CURSOR_UP:
			Menu_SetPrevCursorItem(menu);
			break;

		case A_ESCAPE:
			if (!g_waitingForKey && menu->onESC) {
				itemDef_t it;
		    it.parent = menu;
		    Item_RunScript(&it, menu->onESC);
			}
		    g_waitingForKey = qfalse;
			break;
		case A_TAB:
		case A_KP_2:
		case A_CURSOR_DOWN:
			Menu_SetNextCursorItem(menu);
			break;

		// the list of a page: three rows for a notch of the wheel, a window less a row for the page keys
		case A_MWHEELUP:
		case A_MWHEELDOWN:
			Menu_ScrollBy( menu, key == A_MWHEELUP ? -3.0f : 3.0f );
			break;
		case A_PAGE_UP:
		case A_PAGE_DOWN:
			if ( Menu_HasList( menu ) ) {
				const float rows = floorf( menu->scrollRect.h / Menu_ListStep( menu ) ) - 1.0f;

				Menu_ScrollBy( menu, key == A_PAGE_UP ? -rows : rows );
			}
			break;

		case A_MOUSE1:
		case A_MOUSE2:
			if (item) {
				if (item->type == ITEM_TYPE_TEXT) {
					if (Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory))
					{
						Item_Action(item);
					}
				} else if (item->type == ITEM_TYPE_EDITFIELD || item->type == ITEM_TYPE_NUMERICFIELD) {
					if (Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory))
					{
						Item_Action(item);
						item->cursorPos = 0;
						g_editingField = qtrue;
						g_editItem = item;
						//DC->setOverstrikeMode(qtrue);
					}
				}

	//JLFACCEPT
// add new types here as needed
/* Notes:
	Most controls will use the dpad to move through the selection possibilies.  Buttons are the only exception.
	Buttons will be assumed to all be on one menu together.  If the start or A button is pressed on a control focus, that
	means that the menu is accepted and move onto the next menu.  If the start or A button is pressed on a button focus it
	should just process the action and not support the accept functionality.
*/

//JLFACCEPT MPMOVED
				else if ( item->type == ITEM_TYPE_MULTI || item->type == ITEM_TYPE_YESNO || item->type == ITEM_TYPE_SLIDER)
				{
					if (Item_HandleAccept(item))
					{
						//Item processed it overriding the menu processing
						return;
					}
					else if (menu->onAccept)
					{
						itemDef_t it;
						it.parent = menu;
						Item_RunScript(&it, menu->onAccept);
					}
				}
//END JLFACCEPT
				else {
					if (Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory))
					{

						Item_Action(item);
					}
				}
			}
			break;

		case A_JOY0:
		case A_JOY1:
		case A_JOY2:
		case A_JOY3:
		case A_JOY4:
		case A_AUX0:
		case A_AUX1:
		case A_AUX2:
		case A_AUX3:
		case A_AUX4:
		case A_AUX5:
		case A_AUX6:
		case A_AUX7:
		case A_AUX8:
		case A_AUX9:
		case A_AUX10:
		case A_AUX11:
		case A_AUX12:
		case A_AUX13:
		case A_AUX14:
		case A_AUX15:
		case A_AUX16:
			break;
		case A_KP_ENTER:
		case A_ENTER:
			if (item) {
				if (item->type == ITEM_TYPE_EDITFIELD || item->type == ITEM_TYPE_NUMERICFIELD) {
					item->cursorPos = 0;
					g_editingField = qtrue;
					g_editItem = item;
					//DC->setOverstrikeMode(qtrue);
				} else {
						Item_Action(item);
				}
			}
			break;
	}
}

void ToWindowCoords(float *x, float *y, windowDef_t *window) {
	if (window->border != 0) {
		*x += window->borderSize;
		*y += window->borderSize;
	}
	*x += window->rect.x;
	*y += window->rect.y;
}

void Rect_ToWindowCoords(rectDef_t *rect, windowDef_t *window) {
	ToWindowCoords(&rect->x, &rect->y, window);
}

void Item_SetTextExtents(itemDef_t *item, int *width, int *height, const char *text) {
	const char *textPtr = (text) ? text : item->text;

	if (textPtr == NULL ) {
		return;
	}

	*width = item->textRect.w;
	*height = item->textRect.h;

	// keeps us from computing the widths and heights more than once
	if (*width == 0 || (item->type == ITEM_TYPE_OWNERDRAW && item->textalignment == ITEM_ALIGN_CENTER)
#ifndef _CGAME
		|| (item->text && item->text[0]=='@' && item->asset != se_language.modificationCount )	//string package language changed
#endif
		)
	{
		int originalWidth;
		float lift = 0.0f;

		// "skin 1": a text that is too long for its place (the word of another language) is set smaller, down to
		// six tenths, and stays on the middle of its line. Its place: the item for a text in its middle or at its
		// left, and for one that ends at a mark (the label of a setting) what lies between the item's left edge
		// and the mark.
		if ( Item_Skinned( item ) && item->type != ITEM_TYPE_OWNERDRAW && item->text && textPtr != NULL ) {
			float room, full;

			if ( item->skinScale <= 0.0f ) {
				item->skinScale = item->textscale;
			}
			item->textscale = item->skinScale;
			full = DC->textWidth( textPtr, item->textscale, item->iMenuFont );
			if ( item->textalignment == ITEM_ALIGN_CENTER ) {
				room = item->window.rect.w - 4.0f;
			} else if ( item->textalignment == ITEM_ALIGN_RIGHT ) {
				room = item->textalignx - 2.0f;
			} else {
				room = item->window.rect.w - item->textalignx - 2.0f;
			}
			if ( room > 16.0f && full > room ) {
				item->textscale *= Q_max( 0.6f, room / full );
				lift = 9.0f * ( item->skinScale - item->textscale );
			}
		}
		originalWidth = DC->textWidth(textPtr, item->textscale, item->iMenuFont);

		if (item->type == ITEM_TYPE_OWNERDRAW && (item->textalignment == ITEM_ALIGN_CENTER || item->textalignment == ITEM_ALIGN_RIGHT))
		{
			originalWidth += DC->ownerDrawWidth(item->window.ownerDraw, item->textscale);
		}
		else if (item->type == ITEM_TYPE_EDITFIELD && item->textalignment == ITEM_ALIGN_CENTER && item->cvar)
		{
			char buff[256];
			DC->getCVarString(item->cvar, buff, 256);
			originalWidth += DC->textWidth(buff, item->textscale, item->iMenuFont);
		}

		*width = DC->textWidth(textPtr, item->textscale, item->iMenuFont);
		*height = DC->textHeight(textPtr, item->textscale, item->iMenuFont);

		item->textRect.w = *width;
		item->textRect.h = *height;
		item->textRect.x = item->textalignx;
		item->textRect.y = item->textaligny + lift;
		if (item->textalignment == ITEM_ALIGN_RIGHT) {
			item->textRect.x = item->textalignx - originalWidth;
		} else if (item->textalignment == ITEM_ALIGN_CENTER) {
			item->textRect.x = item->textalignx - originalWidth / 2;
		}

		ToWindowCoords(&item->textRect.x, &item->textRect.y, &item->window);
#ifndef _CGAME
		if (item->text && item->text[0]=='@' )//string package
		{//mark language
			item->asset = se_language.modificationCount;
		}
#endif
	}
}

void Item_TextColor(itemDef_t *item, vec4_t *newColor) {
	vec4_t lowLight;
	menuDef_t *parent = (menuDef_t*)item->parent;

	Fade(&item->window.flags, &item->window.foreColor[3], parent->fadeClamp, &item->window.nextTime, parent->fadeCycle, qtrue, parent->fadeAmount);

	if ( Item_Skinned( item ) ) {
		// no pulse: the mark behind the item says where the focus is
		Skin_TextColor( item, *newColor );
		if ( ( item->tab || item->flow >= 2 ) && !item->disabled ) {
			// (and the tab whose page is open is as bright as one in focus)
			const float part = Q_max( item->skinFocus, item->skinValue );
			int			i;

			for ( i = 0; i < 3; i++ ) {
				( *newColor )[i] = item->window.foreColor[i] + ( 1.0f - item->window.foreColor[i] ) * part;
			}
		}
		if ( item->enableCvar && *item->enableCvar && item->cvarTest && *item->cvarTest
			&& ( item->cvarFlags & ( CVAR_ENABLE | CVAR_DISABLE ) ) && !Item_EnableShowViaCvar( item, CVAR_ENABLE ) ) {
			memcpy( newColor, &parent->disableColor, sizeof( vec4_t ) );
		}
		return;
	}

	if (item->window.flags & WINDOW_HASFOCUS) {
		lowLight[0] = 0.8 * parent->focusColor[0];
		lowLight[1] = 0.8 * parent->focusColor[1];
		lowLight[2] = 0.8 * parent->focusColor[2];
		lowLight[3] = 0.8 * parent->focusColor[3];
		LerpColor(parent->focusColor,lowLight,*newColor,0.5+0.5*sin((float)(DC->realTime / PULSE_DIVISOR)));
	} else if (item->textStyle == ITEM_TEXTSTYLE_BLINK && !((DC->realTime/BLINK_DIVISOR) & 1)) {
		lowLight[0] = 0.8 * item->window.foreColor[0];
		lowLight[1] = 0.8 * item->window.foreColor[1];
		lowLight[2] = 0.8 * item->window.foreColor[2];
		lowLight[3] = 0.8 * item->window.foreColor[3];
		LerpColor(item->window.foreColor,lowLight,*newColor,0.5+0.5*sin((float)(DC->realTime / PULSE_DIVISOR)));
	} else {
		memcpy(newColor, &item->window.foreColor, sizeof(vec4_t));
		// items can be enabled and disabled based on cvars
	}

	if (item->disabled)
	{
		memcpy(newColor, &parent->disableColor, sizeof(vec4_t));
	}

	if (item->enableCvar && *item->enableCvar && item->cvarTest && *item->cvarTest) {
		if (item->cvarFlags & (CVAR_ENABLE | CVAR_DISABLE) && !Item_EnableShowViaCvar(item, CVAR_ENABLE)) {
			memcpy(newColor, &parent->disableColor, sizeof(vec4_t));
		}
	}
}

static float Text_PaintWrapped( float x, float y, float width, float scale, vec4_t color, const char *text, int font, int maxLines );

void Item_Text_AutoWrapped_Paint(itemDef_t *item) {
	char text[2048];
	const char *p, *textPtr, *newLinePtr;
	char buff[2048];
	int height, len, textWidth, newLine, newLineWidth;	//int width;
	float y;
	vec4_t color;

	textWidth = 0;
	newLinePtr = NULL;

	if (item->text == NULL) {
		if (item->cvar == NULL) {
			return;
		}
		else {
			DC->getCVarString(item->cvar, text, sizeof(text));
			textPtr = text;
		}
	}
	else {
		textPtr = item->text;
	}
	if (*textPtr == '@')	// string reference
	{
		trap->SE_GetStringTextString( &textPtr[1], text, sizeof(text));
		textPtr = text;
	}
	if (*textPtr == '\0') {
		return;
	}
	Item_TextColor(item, &color);
	if ( item->parent && ( (menuDef_t *)item->parent )->canvasW > 0.0f ) {
		// in a menu with a canvas: in the font of the item, broken where the rectangle ends
		Text_PaintWrapped( item->window.rect.x + item->textalignx, item->window.rect.y + item->textaligny, item->window.rect.w - item->textalignx,
			item->textscale, color, textPtr, item->iMenuFont, 12 );
		return;
	}
	//Item_SetTextExtents(item, &width, &height, textPtr);
	//if (item->value == 0)
	//{
	//	item->value = (int)(0.5 + (float)DC->textWidth(textPtr, item->textscale, item->font) / item->window.rect.w);
	//}
	height = DC->textHeight(textPtr, item->textscale, item->iMenuFont);

	y = item->textaligny;
	len = 0;
	buff[0] = '\0';
	newLine = 0;
	newLineWidth = 0;
	p = textPtr;
	while (p) {	//findmeste (this will break widechar languages)!
		if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\0') {
			newLine = len;
			newLinePtr = p+1;
			newLineWidth = textWidth;
		}
		textWidth = DC->textWidth(buff, item->textscale, 0);
		if ( (newLine && textWidth > item->window.rect.w) || *p == '\n' || *p == '\0') {
			if (len) {
				if (item->textalignment == ITEM_ALIGN_LEFT) {
					item->textRect.x = item->textalignx;
				} else if (item->textalignment == ITEM_ALIGN_RIGHT) {
					item->textRect.x = item->textalignx - newLineWidth;
				} else if (item->textalignment == ITEM_ALIGN_CENTER) {
					item->textRect.x = item->textalignx - newLineWidth / 2;
				}
				item->textRect.y = y;
				ToWindowCoords(&item->textRect.x, &item->textRect.y, &item->window);
				//
				buff[newLine] = '\0';
				DC->drawText(item->textRect.x, item->textRect.y, item->textscale, color, buff, 0, 0, item->textStyle, item->iMenuFont);
			}
			if (*p == '\0') {
				break;
			}
			//
			y += height + 5;
			p = newLinePtr;
			len = 0;
			newLine = 0;
			newLineWidth = 0;
			continue;
		}
		buff[len++] = *p++;
		buff[len] = '\0';
	}
}

void Item_Text_Wrapped_Paint(itemDef_t *item) {
	char text[1024];
	const char *p, *start, *textPtr;
	char buff[1024];
	int width, height;
	float x, y;
	vec4_t color;

	// now paint the text and/or any optional images
	// default to left

	if (item->text == NULL) {
		if (item->cvar == NULL) {
			return;
		}
		else {
			DC->getCVarString(item->cvar, text, sizeof(text));
			textPtr = text;
		}
	}
	else {
		textPtr = item->text;
	}
	if (*textPtr == '@')	// string reference
	{
		trap->SE_GetStringTextString( &textPtr[1], text, sizeof(text));
		textPtr = text;
	}
	if (*textPtr == '\0') {
		return;
	}

	Item_TextColor(item, &color);
	Item_SetTextExtents(item, &width, &height, textPtr);

	x = item->textRect.x;
	y = item->textRect.y;
	start = textPtr;
	p = strchr(textPtr, '\r');
	while (p && *p) {
		strncpy(buff, start, p-start+1);
		buff[p-start] = '\0';
		DC->drawText(x, y, item->textscale, color, buff, 0, 0, item->textStyle, item->iMenuFont);
		y += height + 2;
		start += p - start + 1;
		p = strchr(p+1, '\r');
	}
	DC->drawText(x, y, item->textscale, color, start, 0, 0, item->textStyle, item->iMenuFont);
}

/*
===============
Text_InkAt

Where a text of one line is drawn so that its letters, not the box of its font, are placed: x is where the
letters start, their middle or their end (ITEM_ALIGN_*), and the capitals stand on the middle of h below y. The
box of a font is wider than its letters and the advance of a text ends after its last letter, so a text placed
by them is off by a pixel or more. qfalse: the engine does not know the pixels of the font, or the text has none.
===============
*/
// how wide the letters of a text are as they are drawn (the advances of the font are on whole pixels of its size:
// textWidth adds them up unrounded and is short for a long text set small), else its width by the font
static float Text_InkWidth( const char *text, float scale, int font ) {
	float box[4];

	if ( DC->ext.textInk && !strchr( text, '\n' ) && DC->ext.textInk( text, scale, font, box ) && box[2] > box[0] ) {
		return box[2] - box[0];
	}
	return DC->textWidth( text, scale, font );
}

// a text that is wider than room set smaller, no smaller than 0.55 of its size: the scale it is drawn at
static float Text_FitScale( const char *text, float scale, int font, float room ) {
	const float least = scale * 0.55f;
	int			i;

	// (twice: the letters of the smaller size are on other whole pixels)
	for ( i = 0; i < 2 && room > 0.0f; i++ ) {
		const float width = Text_InkWidth( text, scale, font );

		if ( width <= room ) {
			break;
		}
		scale = Q_max( least, scale * room / width * ( i ? 0.98f : 1.0f ) );
	}
	return scale;
}

// a text wider than room cut so that it ends with "..." within it, in its own buffer (size bytes): the longest
// start that fits, found by halves (it is asked for every row of a list in every frame). A cut never parts a ^
// from the digit of its colour.
static const char *Text_Shorten( char *text, int size, float scale, int font, float room ) {
	char	cut[MAX_STRING_CHARS];
	int		lo = 0, hi = (int)strlen( text );

	if ( room <= 0.0f || Text_InkWidth( text, scale, font ) <= room ) {
		return text;
	}
	hi = Q_min( hi, (int)sizeof( cut ) - 4 );
	while ( lo < hi ) {
		const int mid = ( lo + hi + 1 ) / 2;

		Com_sprintf( cut, sizeof( cut ), "%.*s...", mid, text );
		if ( Text_InkWidth( cut, scale, font ) <= room ) {
			lo = mid;
		} else {
			hi = mid - 1;
		}
	}
	while ( lo > 0 && ( text[lo - 1] == ' ' || text[lo - 1] == '^' ) ) {
		lo--;
	}
	Com_sprintf( cut, sizeof( cut ), "%.*s...", lo, text );
	Q_strncpyz( text, cut, size );
	return text;
}

qboolean Text_InkAt( const char *text, float scale, int font, int align, float x, float y, float h, float *tx, float *ty ) {
	float box[4], cap[4];

	if ( !DC->ext.textInk || strchr( text, '\n' ) || !DC->ext.textInk( text, scale, font, box ) || box[2] <= box[0]
		|| !DC->ext.textInk( "H", scale, font, cap ) ) {
		return qfalse;
	}
	if ( align == ITEM_ALIGN_CENTER ) {
		*tx = x - ( box[0] + box[2] ) * 0.5f;
	} else if ( align == ITEM_ALIGN_RIGHT ) {
		*tx = x - box[2];
	} else {
		// (by the pen, as type is set, moved by the room before the stem of an H: texts one under the other start
		// at the same place, and a j or a g that reaches to the left of its pen does so)
		*tx = x - cap[0];
	}
	*ty = y + ( h - cap[1] - cap[3] ) * 0.5f;
	return qtrue;
}

// The head of the card of a heading of a list in a menu with a canvas (or of a word in it, "cardHead 1"): the
// room between the ring of the card and the line under its head (Skin_CardHeadBand). qfalse: not such an item.
static qboolean Item_CardHeadBand( const itemDef_t *item, float *y, float *h ) {
	const menuDef_t *menu = (const menuDef_t *)item->parent;
	float			top;

	if ( !menu || menu->canvasW <= 0.0f || !( item->fold || item->cardHead ) || !Skin_Ready() ) {
		return qfalse;
	}
	top = item->fold ? item->window.rect.y + item->window.rect.h - CARD_HEAD_PX * menu->canvasUnit : item->window.rect.y;
	Skin_CardHeadBand( top, top + CARD_LINE_PX * menu->canvasUnit, y, h );
	return qtrue;
}

// The room of the line about the item in focus at x on the line y: up to the items on that line next to it (what
// spans x, as the bar under it, is not in the way), less a gap. 0: nothing in the way.
static float Menu_DescRoom( const menuDef_t *menu, float x, float y, int align ) {
	const float gap = menu->canvasW > 0.0f ? 12.0f * menu->canvasUnit : 4.0f;
	float		left = -1.0f, right = -1.0f;
	int			i;

	for ( i = 0; i < menu->itemCount; i++ ) {
		const rectDef_t *r = &menu->items[i]->window.rect;

		if ( !( menu->items[i]->window.flags & WINDOW_VISIBLE ) || y < r->y || y > r->y + r->h || ( r->x <= x && r->x + r->w >= x ) ) {
			continue;
		}
		if ( r->x + r->w < x ) {
			left = Q_max( left, r->x + r->w );
		} else if ( right < 0.0f || r->x < right ) {
			right = r->x;
		}
	}
	if ( align == ITEM_ALIGN_LEFT ) {
		return right < 0.0f ? 0.0f : right - x - gap;
	}
	if ( align == ITEM_ALIGN_RIGHT ) {
		return left < 0.0f ? 0.0f : x - left - gap;
	}
	if ( left < 0.0f && right < 0.0f ) {
		return 0.0f;
	}
	return 2.0f * ( Q_min( left < 0.0f ? 1e6f : x - left, right < 0.0f ? 1e6f : right - x ) - gap );
}

void Item_Text_Paint(itemDef_t *item) {
	char text[1024];
	const char *textPtr;
	int height, width;
	vec4_t color;

	if (item->window.flags & WINDOW_WRAPPED) {
		Item_Text_Wrapped_Paint(item);
		return;
	}
	if (item->window.flags & WINDOW_AUTOWRAPPED) {
		Item_Text_AutoWrapped_Paint(item);
		return;
	}

	if (item->text == NULL) {
		if (item->cvar == NULL) {
			return;
		}
		else {
			DC->getCVarString(item->cvar, text, sizeof(text));
			textPtr = text;
		}
	}
	else {
		textPtr = item->text;
	}
	if (*textPtr == '@')	// string reference
	{
		trap->SE_GetStringTextString( &textPtr[1], text, sizeof(text));
		textPtr = text;
	}

	// "caps 1": in capital letters
	if ( item->caps ) {
		if ( textPtr != text ) {
			Q_strncpyz( text, textPtr, sizeof( text ) );
			textPtr = text;
		}
		Text_Caps( text );
	}

	// this needs to go here as it sets extents for cvar types as well
	Item_SetTextExtents(item, &width, &height, textPtr);

	if (*textPtr == '\0') {
		return;
	}


	Item_TextColor(item, &color);

	{
		float x = item->textRect.x, y = item->textRect.y, scale = item->textscale;

		// the new look: its letters where textalignx and textaligny say, the font's box of height (its size)
		// centred on the line (an owner draw or a field adds its value to the width of the text)
		if ( Item_Skinned( item ) && item->type != ITEM_TYPE_OWNERDRAW && item->type != ITEM_TYPE_EDITFIELD
			&& item->type != ITEM_TYPE_NUMERICFIELD ) {
			float			size = DC->textHeight( "A", 1.0f, item->iMenuFont ) * item->textscale;
			float			alignX = item->textalignx, alignY = 0.0f, inkX, inkY;

			ToWindowCoords( &alignX, &alignY, &item->window );
			// (the words of the head of a card: on the middle between its ring and its line, not by textaligny)
			Item_CardHeadBand( item, &y, &size );
			// (set smaller where they are wider than the room the menu file gave them: textRoom)
			if ( item->textRoom > 0.0f ) {
				scale = Text_FitScale( textPtr, item->textscale, item->iMenuFont, item->textRoom );
			}
			if ( Text_InkAt( textPtr, scale, item->iMenuFont, item->textalignment, alignX, y, size, &inkX, &inkY ) ) {
				x = inkX;
				y = inkY;
			}
		}
		DC->drawText(x, y, scale, color, textPtr, 0, 0, item->textStyle, item->iMenuFont);
	}

	if (item->text2)	// Is there a second line of text?
	{
		textPtr = item->text2;
		if (*textPtr == '@')	// string reference
		{
			trap->SE_GetStringTextString( &textPtr[1], text, sizeof(text));
			textPtr = text;
		}
		Item_TextColor(item, &color);
		DC->drawText(item->textRect.x + item->text2alignx, item->textRect.y + item->text2aligny, item->textscale, color, textPtr, 0, 0, item->textStyle,item->iMenuFont);
	}
}

// An edit field with "hint 1" in a menu with the new look: the box over the whole item, and while it is empty and
// nobody types into it, its text dimmed inside it (the search of Setup)
static void Item_HintField_Paint( itemDef_t *item ) {
	const editFieldDef_t	*editPtr = item->typeData.edit;
	const qboolean			editing = (qboolean)( ( item->window.flags & WINDOW_HASFOCUS ) && g_editingField );
	const float				lineH = DC->textHeight( "A", item->textscale, item->iMenuFont );
	const float				x = item->window.rect.x + 5.0f;
	const float				y = item->window.rect.y + ( item->window.rect.h - lineH ) * 0.5f;
	const float				w = item->window.rect.w - 10.0f;
	char					buff[1024];
	vec4_t					color;
	int						paintOffset;

	buff[0] = '\0';
	if ( item->cvar ) {
		DC->getCVarString( item->cvar, buff, sizeof( buff ) );
	}
	item->textRect.x = x;
	item->textRect.y = y;
	item->textRect.w = 0.0f;
	item->textRect.h = lineH;
	Skin_Field( item, item->window.rect.x, item->window.rect.y, item->window.rect.w, item->window.rect.h, editing );
	VectorCopy4( item->window.foreColor, color );
	if ( !buff[0] && !editing ) {
		char hint[256];

		Text_Word( item->text, hint, sizeof( hint ), qfalse, qfalse );
		color[3] *= 0.7f;
		// (set smaller where it is longer than the box, "Alle Einstellungen durchsuchen", not cut off)
		Row_Text( x, item->window.rect.y, item->window.rect.h, w, ITEM_ALIGN_LEFT, item->textscale, color, hint, item->iMenuFont );
		return;
	}
	Item_TextColor( item, &color );
	paintOffset = Com_Clampi( 0, (int)strlen( buff ), editPtr->paintOffset );
	if ( DC->getCVarValue( "v_utf8Text" ) ) {
		while ( ( buff[paintOffset] & 0xC0 ) == 0x80 ) {
			paintOffset++;
		}
	}
	if ( editing ) {
		DC->drawTextWithCursor( x, y, item->textscale, color, buff + paintOffset, Q_max( 0, item->cursorPos - paintOffset ),
			DC->getOverstrikeMode() ? '_' : '|', (int)w, item->textStyle, item->iMenuFont );
	} else {
		DC->drawText( x, y, item->textscale, color, buff + paintOffset, 0, (int)w, item->textStyle, item->iMenuFont );
	}
}

void Item_TextField_Paint(itemDef_t *item) {
	char buff[1024];
	int paintOffset;
	vec4_t newColor, lowLight;
	int offset;
	menuDef_t *parent = (menuDef_t*)item->parent;
	editFieldDef_t *editPtr = item->typeData.edit;

	if ( item->hint && Item_Skinned( item ) ) {
		Item_HintField_Paint( item );
		return;
	}
	Item_Text_Paint(item);

	buff[0] = '\0';

	if (item->cvar) {
		DC->getCVarString(item->cvar, buff, sizeof(buff));
		if (buff[0] == '@')	// string reference
		{
			trap->SE_GetStringTextString( &buff[1], buff, sizeof(buff));
		}
	}

	parent = (menuDef_t*)item->parent;

	if (item->window.flags & WINDOW_HASFOCUS) {
		lowLight[0] = 0.8 * parent->focusColor[0];
		lowLight[1] = 0.8 * parent->focusColor[1];
		lowLight[2] = 0.8 * parent->focusColor[2];
		lowLight[3] = 0.8 * parent->focusColor[3];
		LerpColor(parent->focusColor,lowLight,newColor,0.5+0.5*sin((float)(DC->realTime / PULSE_DIVISOR)));
	} else {
		memcpy(&newColor, &item->window.foreColor, sizeof(vec4_t));
	}

	offset = (item->text && *item->text) ? 8 : 0;
	if ( Item_Skinned( item ) ) {
		// the box the text is typed into, from where the label ends to the end of the item
		const float	boxX = item->textRect.x + item->textRect.w + offset - 3.0f;
		float		lineY, lineH;

		Item_SkinLine( item, &lineY, &lineH );
		Item_TextColor( item, &newColor );
		Skin_Field( item, boxX, lineY - 2.0f, Q_max( 40.0f, item->window.rect.x + item->window.rect.w - boxX - 2.0f ), lineH + 4.0f,
			(qboolean)( ( item->window.flags & WINDOW_HASFOCUS ) && g_editingField ) );
	}
	// the text that is painted does not start in the middle of a character of UTF-8 (Item_TextField_CharLen)
	paintOffset = Com_Clampi( 0, (int)strlen( buff ), editPtr->paintOffset );
	if ( DC->getCVarValue( "v_utf8Text" ) ) {
		while ( ( buff[paintOffset] & 0xC0 ) == 0x80 ) {
			paintOffset++;
		}
	}
	if (item->window.flags & WINDOW_HASFOCUS && g_editingField) {
		char cursor = DC->getOverstrikeMode() ? '_' : '|';
		DC->drawTextWithCursor(item->textRect.x + item->textRect.w + offset, item->textRect.y, item->textscale, newColor, buff + paintOffset, Q_max( 0, item->cursorPos - paintOffset ), cursor, item->window.rect.w, item->textStyle, item->iMenuFont);
	} else {
		DC->drawText(item->textRect.x + item->textRect.w + offset, item->textRect.y, item->textscale, newColor, buff + paintOffset, 0, item->window.rect.w, item->textStyle,item->iMenuFont);
	}
}

void Item_YesNo_Paint(itemDef_t *item) {
	char	sYES[20];
	char	sNO[20];
	vec4_t color;
	float value;
	const char *yesnovalue;

	value = (item->cvar) ? DC->getCVarValue(item->cvar) : 0;

	trap->SE_GetStringTextString("MENUS_YES",sYES, sizeof(sYES));
	trap->SE_GetStringTextString("MENUS_NO", sNO,  sizeof(sNO));

//JLFYESNO MPMOVED
	if (item->invertYesNo)
		yesnovalue = (value == 0) ? sYES : sNO;
	else
		yesnovalue = (value != 0) ? sYES : sNO;

	Item_TextColor(item, &color);
	if ( Item_Skinned( item ) ) {
		// a switch in place of the word
		const qboolean	on = (qboolean)( item->invertYesNo ? ( value == 0 ) : ( value != 0 ) );
		float			lineY, lineH;

		if ( item->text ) {
			Item_Text_Paint( item );
		}
		Item_SkinLine( item, &lineY, &lineH );
		if ( item->text && item->textalignment == ITEM_ALIGN_LEFT ) {
			// the switch at the right end of the item, as far in as the words are from its left: switches of items
			// side by side stand in a row of their own, and the mark under the mouse holds both evenly (Skin_Switch
			// is 1.75 times as wide as it is high, no higher than 12)
			const float sw = Q_min( lineH, 12.0f ) * 1.75f;

			// (on the middle of the item, where its words are)
			Skin_Switch( item, item->window.rect.x + item->window.rect.w - item->textalignx - sw,
				item->window.rect.y + ( item->window.rect.h - lineH ) * 0.5f, lineH, on );
		} else {
			Skin_Switch( item, item->text ? item->textRect.x + item->textRect.w + 8 : item->window.rect.x + 2, lineY, lineH, on );
		}
		return;
	}
	if (item->text)
	{
		Item_Text_Paint(item);
		DC->drawText(item->textRect.x + item->textRect.w + 8, item->textRect.y, item->textscale, color, yesnovalue, 0, 0, item->textStyle, item->iMenuFont);
	}
	else
	{
		DC->drawText(item->textRect.x, item->textRect.y, item->textscale, color, yesnovalue , 0, 0, item->textStyle, item->iMenuFont);
	}

/* JLF ORIGINAL CODE
	if (item->text) {
		Item_Text_Paint(item);
//		DC->drawText(item->textRect.x + item->textRect.w + 8, item->textRect.y, item->textscale, newColor, (value != 0) ? sYES : sNO, 0, 0, item->textStyle);
		DC->drawText(item->textRect.x + item->textRect.w + 8, item->textRect.y, item->textscale, newColor, (value != 0) ? sYES : sNO, 0, 0, item->textStyle,item->iMenuFont);
	} else {
//		DC->drawText(item->textRect.x, item->textRect.y, item->textscale, newColor, (value != 0) ? sYES : sNO, 0, 0, item->textStyle);
		DC->drawText(item->textRect.x, item->textRect.y, item->textscale, newColor, (value != 0) ? sYES : sNO, 0, 0, item->textStyle,item->iMenuFont);
	}
*/
}

void Item_Multi_Paint(itemDef_t *item) {
	vec4_t color;
	const char *text = "";
	char	temp[MAX_STRING_CHARS];
	float	y, scale;

	text = Item_Multi_Setting(item);
	if (*text == '@')	// string reference
	{
		trap->SE_GetStringTextString( &text[1]  , temp, sizeof(temp));
		text = temp;
	}
	// Is is specifying a cvar to get the item name from?
	else if (*text == '*')
	{
		DC->getCVarString(&text[1], temp, sizeof(temp));
		text = temp;
	}

	Item_TextColor(item, &color);
	if (item->text) {
		Item_Text_Paint(item);
	}
	// the new look: the capitals of the value on the line of the name's (Item_Text_Paint places those by Text_InkAt),
	// the value set smaller where it is longer than the room the item has after its name ("Geschlossen")
	y = item->textRect.y;
	scale = item->textscale;
	if ( Item_Skinned( item ) ) {
		const float	size = DC->textHeight( "A", 1.0f, item->iMenuFont ) * item->textscale;
		float		inkX, inkY, width;

		if ( item->text ) {
			const float room = item->window.rect.x + item->window.rect.w - ( item->textRect.x + item->textRect.w + 8 );

			width = DC->textWidth( text, scale, item->iMenuFont );
			if ( room > 0.0f && width > room ) {
				scale *= Q_max( 0.55f, room / width );
			}
		}
		if ( Text_InkAt( text, scale, item->iMenuFont, ITEM_ALIGN_LEFT, 0.0f, y, size, &inkX, &inkY ) ) {
			y = inkY;
		}
	}
	if ( item->text && Item_Skinned( item ) && item->window.style == WINDOW_STYLE_EMPTY && item->textalignment == ITEM_ALIGN_LEFT
		&& item->textalignx > 0.0f ) {
		// (a choice without a box beside others, the data rate beside the switches of the server lists: its value at
		// the right end, as far in as its name is from the left, as the switches stand)
		Row_Text( item->window.rect.x + item->window.rect.w - item->textalignx, item->window.rect.y, item->window.rect.h,
			item->window.rect.w * 0.5f, ITEM_ALIGN_RIGHT, item->textscale, color, text, item->iMenuFont );
	} else if (item->text) {
		DC->drawText(item->textRect.x + item->textRect.w + 8, y, scale, color, text, 0, 0, item->textStyle,item->iMenuFont);
	} else {
		//JLF added xoffset
		DC->drawText(item->textRect.x+item->xoffset, y, item->textscale, color, text, 0, 0, item->textStyle,item->iMenuFont);
	}
}

static const char *g_bindCommands[] = {
	"+altattack",
	"+attack",
	"+back",
	"+button12",				// JA+: grapple hook
	"+button2",
	"+v_cursor",					// VisionJK: the mouse is free while it is held, to point at players
	"+v_radial",					// VisionJK: the quick wheel of the boombox while it is held
	"+force_drain",
	"+force_grip",
	"+force_lightning",
	"+forward",
	"+left",
	"+lookdown",
	"+lookup",
	"+mlook",
	"+movedown",
	"+moveleft",
	"+moveright",
	"+moveup",
	"+right",
	"+scores",
	"+speed",
	"+strafe",
	"+use",
	"+useforce",
	"+v_voice",
	"+v_voiceclan",
	"+v_snowball",				// VisionJK: the toys of #9, held to throw or spray
	"+v_water",
	"automap_button",
	"automap_toggle",
	"bow",
	"centerview",
	"cg_thirdperson !",
	"engage_duel",
	"engage_FullForceDuel",		// JA+
	"flourish",
	"force_absorb",
	"force_distract",
	"force_forcepowerother",
	"force_heal",
	"force_healother",
	"force_protect",
	"force_pull",
	"force_rage",
	"force_seeing",
	"force_speed",
	"force_throw",
	"forcenext",
	"forceprev",
	"gloat",
	"invnext",
	"invprev",
	"jetpack",					// JA+
	"meditate",
	"messagemode",
	"messagemode2",
	"messagemode3",
	"messagemode4",
	"saberAttackCycle",
	"v_scores_pick",				// VisionJK: the scoreboard kept open with the mouse free
	"v_commandmenu",				// VisionJK: Controls, Extras; a bind item whose command is missing here takes no key
	"v_tools",
	"v_modelbrowser",
	"v_photo",
	"v_rccar",
	"v_spray",
	"v_emote",
	"v_pingpong",
	"v_tennis",
	"v_board",
	"taunt",
	"ui_openmenu modsay",		// JA+: the message menu
	"use_bacta",
	"use_electrobinoculars",
	"use_field",
	"use_seeker",
	"use_sentry",
	"voicechat",
	"weapnext",
	"weapon 1",
	"weapon 10",
	"weapon 11",
	"weapon 12",
	"weapon 13",
	"weapon 2",
	"weapon 3",
	"weapon 4",
	"weapon 5",
	"weapon 6",
	"weapon 7",
	"weapon 8",
	"weapon 9",
	"weapprev",
	"zoom"
};

#define g_bindCount ARRAY_LEN(g_bindCommands)

static int g_bindKeys[g_bindCount][2];
static char g_bindDefault[g_bindCount][48];	// the keys of a command in the defaults of the game (Controls_SetDefaultKeys)

/*
=================
Controls_GetKeyAssignment
=================
*/
static void Controls_GetKeyAssignment( const char *command, int *twokeys )
{
	int		count;
	int		j;
	char	b[256];

	twokeys[0] = twokeys[1] = -1;
	count = 0;

	for ( j=0; j<MAX_KEYS; j++ ) {
		DC->getBindingBuf( j, b, sizeof( b ) );
		if ( *b && !Q_stricmp( b, command ) ) {
			twokeys[count] = j;
			count++;
			if ( count == 2 )
				break;
		}
	}
}

/*
=================
Controls_GetConfig
=================
*/
void Controls_GetConfig( void )
{
	size_t	i;

	// iterate each command, get its numeric binding
	for ( i=0; i < g_bindCount; i++ )
		Controls_GetKeyAssignment( g_bindCommands[i], g_bindKeys[i] );
}

/*
=================
Controls_SetConfig
=================
*/
void Controls_SetConfig( void )
{
	size_t	i;

	// iterate each command, get its numeric binding
	for ( i=0; i<g_bindCount; i++ ) {
		if ( g_bindKeys[i][0] != -1 ) {
			DC->setBinding( g_bindKeys[i][0], g_bindCommands[i] );

			if ( g_bindKeys[i][1] != -1 )
				DC->setBinding( g_bindKeys[i][1], g_bindCommands[i] );
		}
	}
}

void Controls_SetDefaults( void )
{
	size_t	i;

	for ( i=0; i<g_bindCount; i++ ) {
		g_bindKeys[i][0] = -1;
		g_bindKeys[i][1] = -1;
	}
}

// a row of keys has a key of this name (the whole name: "shift", "mouse1")
static qboolean Item_Bind_HasKey( const itemDef_t *item, const char *search ) {
	char	name[64];
	int		id, j;

	if ( item->type != ITEM_TYPE_BIND || ( id = BindingIDFromName( item->cvar ) ) < 0 ) {
		return qfalse;
	}
	for ( j = 0; j < 2; j++ ) {
		if ( g_bindKeys[id][j] != -1 ) {
			DC->keynumToStringBuf( g_bindKeys[id][j], name, sizeof( name ) );
			if ( !Q_stricmp( name, search ) ) {
				return qtrue;
			}
		}
	}
	return qfalse;
}

/*
=================
Controls_SetDefaultKeys

The keys a command has in the defaults of the game, as words to show ("W or UP"); the ui reads them from
mpdefault.cfg. Shown beside "Default" for a row of keys in a menu with a canvas.
=================
*/
void Controls_SetDefaultKeys( const char *command, const char *keys ) {
	const int id = BindingIDFromName( command );

	if ( id >= 0 ) {
		Q_strncpyz( g_bindDefault[id], keys, sizeof( g_bindDefault[id] ) );
	}
}

/*
=================
Controls_ClearAll

Takes every key off the commands of the menus, other binds stay (the defaults of the keys, see UI_KeysDefaults).
=================
*/
void Controls_ClearAll( void ) {
	size_t	i;
	int		j;

	for ( i = 0; i < g_bindCount; i++ ) {
		for ( j = 0; j < 2; j++ ) {
			if ( g_bindKeys[i][j] != -1 ) {
				DC->setBinding( g_bindKeys[i][j], "" );
			}
			g_bindKeys[i][j] = -1;
		}
	}
}

int BindingIDFromName( const char *name ) {
	size_t i;

	// iterate each command, set its default binding
	for ( i=0; i < g_bindCount; i++ ) {
		if ( !Q_stricmp( name, g_bindCommands[i] ) )
			return i;
	}
	return -1;
}

char g_nameBind[96];

void BindingFromName( const char *cvar ) {
	size_t	i;
	int		b1, b2;
	char	sOR[32];

	// iterate each command, set its default binding
	for ( i=0; i < g_bindCount; i++ ) {
		if ( !Q_stricmp(cvar, g_bindCommands[i] ) ) {
			b2 = g_bindKeys[i][1];
			b1 = g_bindKeys[i][0];
			if ( b1 == -1 )
				break;

			if ( b2 != -1 ) {
				char keyname[2][32];

				DC->keynumToStringBuf( b1, keyname[0], sizeof( keyname[0] ) );
// do NOT do this or it corrupts asian text!!!					Q_strupr(keyname[0]);
				DC->keynumToStringBuf( b2, keyname[1], sizeof( keyname[1] ) );
// do NOT do this or it corrupts asian text!!!					Q_strupr(keyname[1]);

				trap->SE_GetStringTextString( "MENUS_KEYBIND_OR", sOR, sizeof( sOR ) );

				Com_sprintf( g_nameBind, sizeof( g_nameBind ), "%s %s %s", keyname[0], sOR, keyname[1] );
			}
			else {
				DC->keynumToStringBuf( b1, g_nameBind, sizeof( g_nameBind ) );
// do NOT do this or it corrupts asian text!!!					Q_strupr(g_nameBind);
			}
			return;
		}
	}
	Q_strncpyz( g_nameBind, "???", sizeof( g_nameBind ) );
}

void Item_Slider_Paint(itemDef_t *item) {
	vec4_t newColor, lowLight;
	float x, y;
	menuDef_t *parent = (menuDef_t*)item->parent;

	if (item->window.flags & WINDOW_HASFOCUS) {
		lowLight[0] = 0.8 * parent->focusColor[0];
		lowLight[1] = 0.8 * parent->focusColor[1];
		lowLight[2] = 0.8 * parent->focusColor[2];
		lowLight[3] = 0.8 * parent->focusColor[3];
		LerpColor(parent->focusColor,lowLight,newColor,0.5+0.5*sin((float)(DC->realTime / PULSE_DIVISOR)));
	} else {
		memcpy(&newColor, &item->window.foreColor, sizeof(vec4_t));
	}

	y = item->window.rect.y;
	if (item->text) {
		Item_Text_Paint(item);
		x = item->textRect.x + item->textRect.w + 8;
	} else {
		x = item->window.rect.x;
	}
	if ( Item_Skinned( item ) ) {
		// the same track the mouse works on (SLIDER_WIDTH from x), with what it stands at written behind it
		const char				*number = Item_Slider_Shown( item );
		float					lineY, lineH;

		Item_SkinLine( item, &lineY, &lineH );
		Item_TextColor( item, &newColor );
		Skin_Slider( item, x, lineY, SLIDER_WIDTH, lineH, ( Item_Slider_ThumbPosition( item ) - x ) / SLIDER_WIDTH );
		if ( item->cvar ) {
			DC->drawText( x + SLIDER_WIDTH + 8, item->textRect.y, item->textscale, newColor, number, 0, 0, item->textStyle, item->iMenuFont );
		}
		return;
	}
	DC->setColor(newColor);
	DC->drawHandlePic( x, y, SLIDER_WIDTH, SLIDER_HEIGHT, DC->Assets.sliderBar );

	x = Item_Slider_ThumbPosition(item);
	DC->drawHandlePic( x - (SLIDER_THUMB_WIDTH / 2), y - 2, SLIDER_THUMB_WIDTH, SLIDER_THUMB_HEIGHT, DC->Assets.sliderThumb );
}

void Item_Bind_Paint(itemDef_t *item)
{
	vec4_t newColor, lowLight;
	float value;
	int maxChars = 0;
	float	textScale,textWidth;
	int		textHeight,yAdj,startingXPos;

	menuDef_t *parent = (menuDef_t*)item->parent;
	editFieldDef_t *editPtr = item->typeData.edit;
	if (editPtr)
	{
		maxChars = editPtr->maxPaintChars;
	}

	value = (item->cvar) ? DC->getCVarValue(item->cvar) : 0;

	if (item->window.flags & WINDOW_HASFOCUS)
	{
		if (g_bindItem == item)
		{
			lowLight[0] = 0.8f * 1.0f;
			lowLight[1] = 0.8f * 0.0f;
			lowLight[2] = 0.8f * 0.0f;
			lowLight[3] = 0.8f * 1.0f;
		}
		else
		{
			lowLight[0] = 0.8f * parent->focusColor[0];
			lowLight[1] = 0.8f * parent->focusColor[1];
			lowLight[2] = 0.8f * parent->focusColor[2];
			lowLight[3] = 0.8f * parent->focusColor[3];
		}
		LerpColor(parent->focusColor,lowLight,newColor,0.5+0.5*sin((float)(DC->realTime / PULSE_DIVISOR)));
	}
	else
	{
		memcpy(&newColor, &item->window.foreColor, sizeof(vec4_t));
	}

	if (item->text)
	{
		Item_Text_Paint(item);
		BindingFromName(item->cvar);

		// If the text runs past the limit bring the scale down until it fits.
		textScale = item->textscale;
		textWidth = DC->textWidth(g_nameBind,(float) textScale, item->iMenuFont);
		startingXPos = (item->textRect.x + item->textRect.w + 8);

		while ((startingXPos + textWidth) >= SCREEN_WIDTH)
		{
			textScale -= .05f;
			textWidth = DC->textWidth(g_nameBind,(float) textScale, item->iMenuFont);
		}

		// Try to adjust it's y placement if the scale has changed.
		yAdj = 0;
		if (textScale != item->textscale)
		{
			textHeight = DC->textHeight(g_nameBind, item->textscale, item->iMenuFont);
			yAdj = textHeight - DC->textHeight(g_nameBind, textScale, item->iMenuFont);
		}

		if ( Item_Skinned( item ) ) {
			// the key stands in a box, which is lit while the next key pressed will be taken for it
			float lineY, lineH;

			Item_SkinLine( item, &lineY, &lineH );
			Item_TextColor( item, &newColor );
			Skin_Field( item, startingXPos - 4, lineY - 1.0f, textWidth + 8, lineH + 2.0f, (qboolean)( g_bindItem == item && g_waitingForKey ) );
		}
		DC->drawText(startingXPos, item->textRect.y + yAdj, textScale, newColor, g_nameBind, 0, maxChars, item->textStyle,item->iMenuFont);
	}
	else
	{
		DC->drawText(item->textRect.x, item->textRect.y, item->textscale, newColor, (value != 0) ? "FIXME" : "FIXME", 0, maxChars, item->textStyle,item->iMenuFont);
	}
}

/*
===============================================================================

The rows, the tabs, the bar and the explanation of a menu with pages (VisionJK)

===============================================================================
*/

// A text on the middle of a height: from x on, around x or up to x (ITEM_ALIGN_*). It is set smaller where it
// is longer than `room`.
static void Row_Text( float x, float y, float h, float room, int align, float scale, vec4_t color, const char *text, int font ) {
	float width;

	scale = Text_FitScale( text, scale, font, room );
	width = DC->textWidth( text, scale, font );
	{
		float inkX, inkY;

		if ( Text_InkAt( text, scale, font, align, x, y, h, &inkX, &inkY ) ) {
			DC->drawText( inkX, inkY, scale, color, text, 0, 0, 0, font );
			return;
		}
	}
	if ( align == ITEM_ALIGN_CENTER ) {
		x -= width * 0.5f;
	} else if ( align == ITEM_ALIGN_RIGHT ) {
		x -= width;
	}
	DC->drawText( x, y + ( h - DC->textHeight( "A", scale, font ) ) * 0.5f, scale, color, text, 0, 0, 0, font );
}

// The word of a choice around x, on the middle of a height: on two lines when it does not fit `room` even set as
// small as Row_Text sets it ("Sounds, Effects and Graphics")
static void Row_Choice( float x, float y, float h, float room, float scale, vec4_t color, const char *text, int font ) {
	const char	*mid = NULL, *p;
	char		first[256];
	float		lineH, half;

	if ( room <= 0.0f || DC->textWidth( text, scale * 0.7f, font ) <= room ) {
		Row_Text( x, y, h, room, ITEM_ALIGN_CENTER, scale, color, text, font );
		return;
	}
	// (broken at the space nearest the middle)
	for ( p = text; *p; p++ ) {
		if ( *p == ' ' && ( !mid || abs( (int)( p - text ) * 2 - (int)strlen( text ) ) < abs( (int)( mid - text ) * 2 - (int)strlen( text ) ) ) ) {
			mid = p;
		}
	}
	if ( !mid ) {
		Row_Text( x, y, h, room, ITEM_ALIGN_CENTER, scale, color, text, font );
		return;
	}
	Q_strncpyz( first, text, Q_min( (int)sizeof( first ), (int)( mid - text ) + 1 ) );
	scale *= 0.8f;
	lineH = DC->textHeight( "A", scale, font ) * 1.25f;
	half = h * 0.5f;
	Row_Text( x, y + half - lineH, lineH, room, ITEM_ALIGN_CENTER, scale, color, first, font );
	Row_Text( x, y + half, lineH, room, ITEM_ALIGN_CENTER, scale, color, mid + 1, font );
}

// The name of a row from x, on the middle of a height: on two lines at the same size when it is longer than `room`
// (all the names of a list stay one size), set smaller only when even the two lines do not fit
static void Row_Name( float x, float y, float h, float room, float scale, vec4_t color, const char *text, int font ) {
	const char	*mid = NULL, *p;
	char		first[256];
	float		lineH, widest = 0.0f;

	if ( room <= 0.0f || DC->textWidth( text, scale, font ) <= room ) {
		Row_Text( x, y, h, room, ITEM_ALIGN_LEFT, scale, color, text, font );
		return;
	}
	// (broken at the space that makes the wider of the two lines narrowest: in German a long word often stands
	// where the middle of the letters is. After a hyphen as well, which stays on the first line:
	// "Sprechblasen-Reichweite")
	for ( p = text; *p; p++ ) {
		const int	keep = ( *p == '-' && p > text && p[1] ) ? 1 : 0;
		float		w;

		if ( *p != ' ' && !keep ) {
			continue;
		}
		Q_strncpyz( first, text, Q_min( (int)sizeof( first ), (int)( p - text ) + 1 + keep ) );
		w = Q_max( DC->textWidth( first, scale, font ), DC->textWidth( p + 1, scale, font ) );
		if ( !mid || w < widest ) {
			mid = p;
			widest = w;
		}
	}
	if ( mid ) {
		// two lines, set smaller together only when the wider one does not fit
		const float fit = Q_max( 0.55f, Q_min( 1.0f, room / widest ) );

		Q_strncpyz( first, text, Q_min( (int)sizeof( first ), (int)( mid - text ) + 1 + ( *mid == '-' ? 1 : 0 ) ) );
		lineH = DC->textHeight( "A", scale * fit, font ) * 1.3f;
		if ( 2.0f * lineH <= h ) {
			Row_Text( x, y + h * 0.5f - lineH, lineH, room, ITEM_ALIGN_LEFT, scale * fit, color, first, font );
			Row_Text( x, y + h * 0.5f, lineH, room, ITEM_ALIGN_LEFT, scale * fit, color, mid + 1, font );
			return;
		}
	}
	Row_Text( x, y, h, room, ITEM_ALIGN_LEFT, scale, color, text, font );
}

static void Color_Mix( vec4_t out, const vec4_t from, const vec4_t to, float part ) {
	int i;

	for ( i = 0; i < 4; i++ ) {
		out[i] = from[i] + ( to[i] - from[i] ) * part;
	}
}

/*
=================
Item_Row_Paint

A setting as a row of a list: its name at the left, its widget at the right, a line under it. The row in focus
is filled with the colour of the menu, and what stands on it is drawn dark.
=================
*/
static void Item_Row_Paint( itemDef_t *item ) {
	static const vec4_t	white = { 1.0f, 1.0f, 1.0f, 1.0f };
	menuDef_t			*parent = (menuDef_t *)item->parent;
	const rectDef_t		*r = &item->window.rect;
	const float			*accent = parent->focusColor;
	const qboolean		off = Item_Off( item );
	const int			font = item->iMenuFont;
	rectDef_t			widget;
	vec4_t				ink, text, color, edge;
	char				word[256], names[2][64];
	float				f, small, mid;
	int					sel;
	// (the sizes of the design in a menu with a canvas, see Item_RowSize)
	const float			rad = Item_RowSize( item, 6.0f, SKIN_RADIUS ), radIn = Item_RowSize( item, 4.0f, SKIN_RADIUS_INNER );
	const float			pad = Item_RowSize( item, 12.0f, ROW_PAD ), arrow = Item_RowSize( item, 12.0f, ROW_ARROW );
	const float			number = Item_RowSize( item, 58.0f, ROW_NUMBER ), unit = Menu_Unit( parent );

	item->skinFocus = Skin_Ease( item->skinFocus, (qboolean)( ( item->window.flags & WINDOW_HASFOCUS ) && !off ), SKIN_EASE_MSEC );
	f = item->skinFocus;
	Item_Row_Widget( item, &widget );
	mid = widget.y + widget.h * 0.5f;
	small = item->textscale * ROW_SMALL;

	// the row in focus is lit, its name turns white; the widgets keep their colours
	if ( f > 0.0f ) {
		// (between the line over the row, a unit over its top and a pixel high, and its own line, a unit over its
		// end: as far from the one as from the other)
		const float pixel = ( SCREEN_HEIGHT + 2.0f * DC->biasY ) / DC->glconfig.vidHeight;

		Skin_Color( color, accent, 0.10f * f );
		Skin_Panel( r->x, r->y + pixel, r->w, r->h - 2.0f - pixel, rad, color, NULL );
	}
	Skin_Line( r->x, r->y + r->h - 1.0f, r->w, skinRule );

	VectorCopy4( accent, ink );
	ink[3] = off ? 0.4f : 1.0f;
	if ( off ) {
		VectorCopy4( parent->disableColor, text );
	} else {
		Color_Mix( text, item->window.foreColor, white, f );
		text[3] = item->window.foreColor[3];
	}
	// the outline of a widget: that of a field, a little of the accent in it while the row is in focus
	Color_Mix( edge, skinEdge, accent, 0.4f * f );
	edge[3] = off ? 0.5f : 1.0f;
	Text_Word( item->text, word, sizeof( word ), qfalse, qtrue );
	{
		// in a menu with a canvas: a dot before the name of a setting that is not what it was by default
		float x = r->x + pad;

		if ( parent->canvasW > 0.0f ) {
			char		def[MENU_CHANGE_VALUE], now[MENU_CHANGE_VALUE];
			uint32_t	flags;

			if ( item->cvar && !( item->window.flags & WINDOW_DECORATION ) && DC->getCVarInfo
				&& DC->getCVarInfo( item->cvar, def, sizeof( def ), &flags ) ) {
				DC->getCVarString( item->cvar, now, sizeof( now ) );
				if ( !Value_Same( now, def ) ) {
					Skin_Disc( x + 3.0f * parent->canvasUnit, mid, 3.0f * parent->canvasUnit, accent );
				}
			}
			x += 16.0f * parent->canvasUnit;
		}
		// (the name has the room up to the widget; a row that is a button has only an arrow at its end, unless it
		// picks a value for a cvar, which it shows)
		const qboolean	arrowOnly = (qboolean)( item->type != ITEM_TYPE_YESNO && item->type != ITEM_TYPE_MULTI
							&& item->type != ITEM_TYPE_OWNERDRAW && item->type != ITEM_TYPE_SLIDER && item->type != ITEM_TYPE_BIND
							&& item->type != ITEM_TYPE_EDITFIELD && item->type != ITEM_TYPE_NUMERICFIELD
							&& !( item->type == ITEM_TYPE_BUTTON && item->cvar ) );
		const float		end = arrowOnly ? widget.x + widget.w - arrow : widget.x;

		Row_Name( x, r->y, r->h - 1.0f, end - pad - x, item->textscale, text, word, font );
	}

	if ( ( item->window.flags & WINDOW_DECORATION )
		&& ( item->type == ITEM_TYPE_EDITFIELD || item->type == ITEM_TYPE_NUMERICFIELD || item->type == ITEM_TYPE_MULTI ) ) {
		// a value that is only shown (the server in About): its words at the right end of the row, no field
		if ( item->type == ITEM_TYPE_MULTI ) {
			Text_Word( Item_Multi_Setting( item ), word, sizeof( word ), qfalse, qfalse );
		} else {
			word[0] = '\0';
			if ( item->cvar ) {
				DC->getCVarString( item->cvar, word, sizeof( word ) );
			}
		}
		Color_Mix( color, item->window.foreColor, white, 0.6f );
		Row_Text( widget.x + widget.w, r->y, r->h - 1.0f, widget.w + ( widget.x - r->x ) * 0.5f, ITEM_ALIGN_RIGHT, item->textscale,
			color, word, font );
		return;
	}

	switch ( item->type ) {
	case ITEM_TYPE_YESNO:
	case ITEM_TYPE_MULTI:
		if ( Item_Row_Toggle( item, &widget, names, &sel, &small ) ) {
			// two settings side by side in a dark field: the one that is set has the accent, its word is dark
			const float	in = Item_RowSize( item, 3.0f, 1.6f ), half = ( widget.w - 2.0f * in ) * 0.5f;
			int			i;

			Skin_EaseValue( item, (qboolean)( sel != 0 ) );
			Skin_Panel( widget.x, widget.y, widget.w, widget.h, rad, skinField, edge );
			Skin_Panel( widget.x + in + item->skinValue * half, widget.y + in, half, widget.h - 2.0f * in, radIn, ink, NULL );
			for ( i = 0; i < 2; i++ ) {
				Color_Mix( color, skinMuted, skinOnAccent, i ? item->skinValue : 1.0f - item->skinValue );
				color[3] = off ? 0.5f : 1.0f;
				Row_Text( widget.x + in + half * ( i + 0.5f ), widget.y, widget.h, half - 2.0f, ITEM_ALIGN_CENTER, small, color, names[i], font );
			}
		} else {
			// one of several: its name between two arrows, in a dark field
			const float aw = arrow * 0.55f, apad = Item_RowSize( item, 12.0f, 5.0f );

			Text_Word( Item_Multi_Setting( item ), word, sizeof( word ), qfalse, qfalse );
			if ( word[0] == '*' ) {
				// the name is in a cvar
				DC->getCVarString( &word[1], names[0], sizeof( names[0] ) );
				Q_strncpyz( word, names[0], sizeof( word ) );
			}
			// (in capital letters like the words of a switch, but not a size: 1280x720)
			if ( !strpbrk( word, "0123456789" ) ) {
				Text_Caps( word );
			}
			Skin_Panel( widget.x, widget.y, widget.w, widget.h, rad, skinField, edge );
			Skin_Arrow( widget.x + apad, mid - arrow * 0.4f, aw, arrow * 0.8f, qtrue, ink );
			Skin_Arrow( widget.x + widget.w - apad - aw, mid - arrow * 0.4f, aw, arrow * 0.8f, qfalse, ink );
			Row_Choice( widget.x + widget.w * 0.5f, widget.y, widget.h, widget.w - 2.0f * ( aw + apad ) - 6.0f * unit, small, ink, word, font );
		}
		break;

	case ITEM_TYPE_OWNERDRAW:
		if ( DC->ownerDrawItem ) {
			// a setting the ui writes itself (game type, team colour, jedi or not): its word in a field between two
			// arrows, as one of several; the owner draw still takes the clicks. The owner draw runs as it always
			// did (some show and hide items or grey their word), the word it writes is caught and written here
			// on the middle of the field, in capitals as the word of a choice
			const float aw = arrow * 0.55f, apad = Item_RowSize( item, 12.0f, 5.0f );
			vec4_t		said;

			Skin_Panel( widget.x, widget.y, widget.w, widget.h, rad, skinField, edge );
			Skin_Arrow( widget.x + apad, mid - arrow * 0.4f, aw, arrow * 0.8f, qtrue, ink );
			Skin_Arrow( widget.x + widget.w - apad - aw, mid - arrow * 0.4f, aw, arrow * 0.8f, qfalse, ink );
			word[0] = '\0';
			VectorCopy4( ink, said );
			textCatch.text = word;
			textCatch.size = sizeof( word );
			textCatch.color = said;
			DC->ownerDrawItem( widget.x, widget.y, widget.w, widget.h, 0, 0, item->window.ownerDraw, item->window.ownerDrawFlags,
				item->alignment, item->special, small, said, item->window.background, 0, font );
			textCatch.text = NULL;
			textCatch.color = NULL;
			if ( word[0] ) {
				if ( !strpbrk( word, "0123456789" ) ) {
					Text_Caps( word );
				}
				Row_Choice( widget.x + widget.w * 0.5f, widget.y, widget.h, widget.w - 2.0f * ( aw + apad ) - 6.0f * unit, small, said,
					word, font );
			} else {
				// (nothing caught: a text path that does not catch, the cgame's; drawn by the owner draw itself)
				const float width = DC->ownerDrawWidth ? DC->ownerDrawWidth( item->window.ownerDraw, small ) : 0.0f;

				DC->ownerDrawItem( widget.x + ( widget.w - width ) * 0.5f, mid - DC->textHeight( "A", small, font ) * 0.5f, width,
					widget.h, 0, 0, item->window.ownerDraw, item->window.ownerDrawFlags, item->alignment, item->special, small, ink,
					item->window.background, 0, font );
			}
		}
		break;

	case ITEM_TYPE_SLIDER: {
		// a track filled up to its knob, what it stands at after it
		float w, x = Item_Slider_Track( item, &w );
		const float part = Com_Clamp( 0.0f, 1.0f, ( Item_Slider_ThumbPosition( item ) - x ) / w );

		Skin_SliderTrack( x, mid, w, part, accent, f, off ? 0.4f : 1.0f, unit );
		Row_Text( widget.x + widget.w - 1.0f, widget.y, widget.h, number, ITEM_ALIGN_RIGHT, small, ink, Item_Slider_Shown( item ), font );
		break;
	}

	case ITEM_TYPE_BIND: {
		// the keys in a box like a key of the keyboard, which takes the accent while the next key that is pressed
		// will be taken
		const qboolean waiting = (qboolean)( g_bindItem == item && g_waitingForKey );

		if ( Item_Bind_Slots( item ) ) {
			// in a menu with a canvas: the first and the second key, each in its own field
			const int	id = BindingIDFromName( item->cvar );
			int			slot;

			for ( slot = 0; slot < 2; slot++ ) {
				const int	key = id >= 0 ? g_bindKeys[id][slot] : -1;
				rectDef_t	box;
				char		name[64];

				Item_Bind_Slot( item, slot, &box );
				if ( waiting && g_bindSlot == slot ) {
					Color_Mix( color, skinKeyFill, accent, 0.12f );
					Skin_Panel( box.x, box.y, box.w, box.h, rad, color, accent );
					Skin_Color( color, white, ( ( DC->realTime / BLINK_DIVISOR ) & 1 ) ? 1.0f : 0.6f );
					Row_Text( box.x + box.w * 0.5f, box.y, box.h, box.w - 8.0f, ITEM_ALIGN_CENTER, small, color,
						Vision_Text( "PRESS A KEY" ), font );
				} else if ( key == -1 ) {
					Skin_Panel( box.x, box.y, box.w, box.h, rad, NULL, skinRule );
					VectorCopy4( skinFaint, color );
					Row_Text( box.x + box.w * 0.5f, box.y, box.h, box.w - 8.0f, ITEM_ALIGN_CENTER, small, color,
						Vision_Text( "NOT BOUND" ), font );
				} else {
					Color_Mix( edge, skinKeyEdge, accent, 0.5f * f );
					Skin_Panel( box.x, box.y, box.w, box.h, rad, skinKeyFill, edge );
					Color_Mix( color, item->window.foreColor, white, f );
					color[3] = off ? 0.5f : 1.0f;
					DC->keynumToStringBuf( key, name, sizeof( name ) );
					Row_Text( box.x + box.w * 0.5f, box.y, box.h, box.w - 8.0f, ITEM_ALIGN_CENTER, small, color, name, font );
				}
			}
			break;
		}
		BindingFromName( item->cvar );
		if ( waiting ) {
			Color_Mix( color, skinKeyFill, accent, 0.12f );
			Skin_Panel( widget.x, widget.y, widget.w, widget.h, rad, color, accent );
			Skin_Color( color, white, ( ( DC->realTime / BLINK_DIVISOR ) & 1 ) ? 1.0f : 0.45f );
			Row_Text( widget.x + widget.w * 0.5f, widget.y, widget.h, 0.0f, ITEM_ALIGN_CENTER, small, color, "? ? ?", font );
		} else if ( !Q_stricmp( g_nameBind, "???" ) ) {
			// nothing bound: an empty field with a dash
			Skin_Panel( widget.x, widget.y, widget.w, widget.h, rad, NULL, skinRule );
			Skin_Panel( widget.x + widget.w * 0.5f - 4.0f, mid - 0.5f, 8.0f, 1.0f, 0.5f, f > 0.5f ? accent : skinFaint, NULL );
		} else {
			Color_Mix( edge, skinKeyEdge, accent, 0.5f * f );
			Skin_Panel( widget.x, widget.y, widget.w, widget.h, rad, skinKeyFill, edge );
			Color_Mix( color, item->window.foreColor, white, f );
			color[3] = off ? 0.5f : 1.0f;
			Row_Text( widget.x + widget.w * 0.5f, widget.y, widget.h, widget.w - 8.0f, ITEM_ALIGN_CENTER, small, color, g_nameBind, font );
		}
		break;
	}

	case ITEM_TYPE_EDITFIELD:
	case ITEM_TYPE_NUMERICFIELD: {
		// what was typed, in a dark field. "row 2": it is not shown unless it is being typed (a password)
		const qboolean			editing = (qboolean)( ( item->window.flags & WINDOW_HASFOCUS ) && g_editingField );
		const editFieldDef_t	*editPtr = item->typeData.edit;
		// (what the text calls take as a limit is a width: what does not fit into the box is not drawn)
		const int				limit = (int)( widget.w - 8.0f );
		int						start = 0;

		word[0] = '\0';
		if ( item->cvar ) {
			DC->getCVarString( item->cvar, word, sizeof( word ) );
		}
		if ( item->row == 2 && !editing ) {
			const int len = Q_min( (int)strlen( word ), 12 );

			memset( word, '*', len );
			word[len] = '\0';
		}
		if ( editPtr ) {
			start = Com_Clampi( 0, (int)strlen( word ), editPtr->paintOffset );
			if ( DC->getCVarValue( "v_utf8Text" ) ) {
				while ( ( word[start] & 0xC0 ) == 0x80 ) {
					start++;
				}
			}
		}
		Skin_Panel( widget.x, widget.y, widget.w, widget.h, rad, skinField, editing ? accent : edge );
		Color_Mix( color, item->window.foreColor, white, editing ? 1.0f : f );
		color[3] = off ? 0.5f : 1.0f;
		mid = widget.y + ( widget.h - DC->textHeight( "A", small, font ) ) * 0.5f;
		if ( editing ) {
			DC->drawTextWithCursor( widget.x + 5.0f, mid, small, color, word + start, Q_max( 0, item->cursorPos - start ),
				DC->getOverstrikeMode() ? '_' : '|', limit, 0, font );
		} else if ( word[start] ) {
			DC->drawText( widget.x + 5.0f, mid, small, color, word + start, 0, limit, 0, font );
		}
		break;
	}

	default:
		// something to click: an arrow says so. A button that opens a picker for a cvar (the model rows of Setup,
		// modelpick.menu) shows the value before it, empty is "As They Chose".
		if ( item->type == ITEM_TYPE_BUTTON && item->cvar ) {
			DC->getCVarString( item->cvar, word, sizeof( word ) );
			Color_Mix( color, item->window.foreColor, white, f );
			color[3] = off ? 0.5f : 1.0f;
			Row_Text( widget.x + widget.w - arrow - 4.0f, widget.y, widget.h, widget.w - arrow - 4.0f, ITEM_ALIGN_RIGHT, small, color,
				word[0] ? word : Vision_Text( "As They Chose" ), font );
		}
		Skin_Arrow( widget.x + widget.w - 1.0f - arrow * 0.55f, mid - arrow * 0.4f, arrow * 0.55f, arrow * 0.8f, qfalse, ink );
		break;
	}
}

/*
=================
Item_Tab_Paint

The mark of a button that opens a page ("tab <n>"): a line under the one whose page is open, a faint one under
the one in focus. Its text is drawn by the caller, brighter by item->skinFocus and item->skinValue.
=================
*/
static void Item_Tab_Paint( itemDef_t *item ) {
	const menuDef_t	*parent = (const menuDef_t *)item->parent;
	const rectDef_t	*r = &item->window.rect;
	vec4_t			color;

	item->skinFocus = Skin_Ease( item->skinFocus, (qboolean)( ( item->window.flags & WINDOW_HASFOCUS ) && !item->disabled ), SKIN_EASE_MSEC );
	// ("flow 3": the entry of a strip of menus that stands for the menu that is open)
	Skin_EaseValue( item, (qboolean)( item->flow == 3 || ( item->tab && parent->page == item->tab ) ) );
	if ( ( item->window.flags & WINDOW_DECORATION ) && item->flow != 3 ) {
		return;
	}
	if ( item->tab && item->textalignment == ITEM_ALIGN_LEFT ) {
		// a tab of a list of pages one under the other (the categories of Setup): the open one is filled and has a
		// bar at its left, the one under the mouse is lit
		Skin_Color( color, parent->focusColor, 0.16f * item->skinValue + 0.08f * item->skinFocus * ( 1.0f - item->skinValue ) );
		Skin_Panel( r->x, r->y, r->w, r->h, Item_RowSize( item, 6.0f, SKIN_RADIUS ), color, NULL );
		if ( item->skinValue > 0.0f ) {
			const float bar = Item_RowSize( item, 3.0f, 1.6f );

			Skin_Color( color, parent->focusColor, item->skinValue );
			Skin_Panel( r->x, r->y + 2.0f * bar, bar, r->h - 4.0f * bar, bar * 0.5f, color, NULL );
		}
		{
			// how many settings of its page are changed: a pill at its right (changesCvar)
			const int n = Menu_ChangesOnPage( parent, item->tab );

			if ( n > 0 ) {
				const float	h = Item_RowSize( item, 22.0f, 11.0f ), w = Q_max( h, Item_RowSize( item, 14.0f, 7.0f ) * ( n > 9 ? 2.0f : 1.0f ) + h * 0.6f );
				const float	x = r->x + r->w - Item_RowSize( item, 14.0f, 6.0f ) - w, y = r->y + ( r->h - h ) * 0.5f;

				Skin_Panel( x, y, w, h, h * 0.5f, parent->focusColor, NULL );
				Row_Text( x + w * 0.5f, y, h, 0.0f, ITEM_ALIGN_CENTER, Item_Pixels( item, 12.0f ), (float *)skinOnAccent, va( "%i", n ),
					SKIN_FONT_BOLD );
			}
		}
		return;
	}
	Skin_Color( color, parent->focusColor, Q_max( item->skinValue, 0.35f * item->skinFocus ) );
	Skin_Panel( r->x + TAB_PAD - 3.0f, r->y + r->h - 1.6f, r->w - 2.0f * ( TAB_PAD - 3.0f ), 1.6f, 0.8f, color, NULL );
}

// "flow 1": a short line between this button of a bar and the one in front of it (Menu_TabsLayout says whether
// there is one)
static void Item_Flow_Paint( const itemDef_t *item ) {
	const menuDef_t	*parent = (const menuDef_t *)item->parent;
	const rectDef_t	*r = &item->window.rect;
	vec4_t			color;

	if ( item->cursorPos ) {
		Skin_Color( color, parent->focusColor, 0.25f );
		Skin_Panel( r->x - 0.4f, r->y + r->h * 0.28f, 0.8f, r->h * 0.44f, 0.0f, color, NULL );
	}
}

// is this button one of a row that is laid out by the width of its words
static qboolean Item_Flows( const itemDef_t *item ) {
	return (qboolean)( ( item->tab || item->flow ) && item->textalignment == ITEM_ALIGN_CENTER );
}

/*
=================
Menu_TabsLayout

The buttons of the pages ("tab") and the buttons of a bar ("flow 1") that stand in a row (one after the other
in the menu file, at the same height) are as wide as their words, which are of another length in every
language: the first one stays where the file put it and the others follow it. A button that is hidden takes no
room. Where the row is longer than the menu is wide, they move together.
=================
*/
static void Menu_TabsLayout( menuDef_t *menu ) {
	int i = 0;

	if ( !menu->skin || !Skin_Ready() ) {
		return;
	}
	while ( i < menu->itemCount ) {
		const int	first = i;
		float		widths[MENU_PAGES], sum = 0.0f, pad = TAB_PAD, x, room;
		int			n, k;

		if ( !Item_Flows( menu->items[i] ) ) {
			i++;
			continue;
		}
		for ( n = first, k = 0; n < menu->itemCount && n - first < MENU_PAGES && Item_Flows( menu->items[n] )
			&& menu->items[n]->window.rectClient.y == menu->items[first]->window.rectClient.y; n++ ) {
			itemDef_t	*item = menu->items[n];
			char		word[256];

			Text_Word( item->text, word, sizeof( word ), item->caps, qfalse );
			widths[n - first] = DC->textWidth( word, item->skinScale > 0.0f ? item->skinScale : item->textscale, item->iMenuFont );
			if ( ( item->window.flags & WINDOW_VISIBLE ) && Item_EnableShowViaCvar( item, CVAR_SHOW ) ) {
				sum += widths[n - first];
				k++;
			} else {
				widths[n - first] = -1.0f;
			}
		}
		i = n;
		// (one button of a page alone stays what the menu file made it, one of a bar is as wide as its word)
		if ( n - first < 2 && !menu->items[first]->flow ) {
			continue;
		}
		x = menu->items[first]->window.rectClient.x;
		room = ( menu->fullScreen ? SCREEN_WIDTH : menu->window.rect.w ) - 2.0f * x;
		if ( k > 0 && sum + 2.0f * pad * k > room ) {
			pad = Q_max( 3.0f, ( room - sum ) / ( 2.0f * k ) );
		}
		for ( k = first; k < n; k++ ) {
			itemDef_t	*item = menu->items[k];
			const float	w = Q_max( 0.0f, widths[k - first] ) + 2.0f * pad;

			if ( fabsf( item->window.rectClient.x - x ) > 0.01f || fabsf( item->window.rectClient.w - w ) > 0.01f ) {
				item->window.rectClient.x = x;
				item->window.rectClient.w = w;
				item->textalignx = w * 0.5f;
				Item_UpdatePosition( item );
			}
			// (the first one that can be seen has no line in front of it)
			item->cursorPos = ( x > menu->items[first]->window.rectClient.x );
			if ( widths[k - first] >= 0.0f ) {
				x += w;
			}
		}
	}
}

/*
=================
Text_PaintWrapped

A text in lines that are no longer than `width`, broken at its spaces, at most `maxLines` of them (what is left
then is cut). Returns where the line after the last one would begin.
=================
*/
static float Text_PaintWrapped( float x, float y, float width, float scale, vec4_t color, const char *text, int font, int maxLines ) {
	const float	lineH = DC->textHeight( "A", scale, font ) * 1.18f;
	char		line[512];
	int			len = 0, lastSpace = -1, lines = 0;
	const char	*p = text, *afterSpace = NULL;

	line[0] = '\0';
	while ( lines < maxLines ) {
		const qboolean end = (qboolean)( *p == '\0' || *p == '\n' );

		if ( !end && len < (int)sizeof( line ) - 1 ) {
			if ( *p == ' ' ) {
				lastSpace = len;
				afterSpace = p + 1;
			}
			line[len++] = *p++;
			line[len] = '\0';
			if ( DC->textWidth( line, scale, font ) <= width || lastSpace <= 0 ) {
				continue;
			}
			// too long: back to the last space
			line[lastSpace] = '\0';
			p = afterSpace;
		}
		if ( line[0] ) {
			DC->drawText( x, y, scale, color, line, 0, 0, 0, font );
		}
		y += lineH;
		lines++;
		if ( end && *p == '\0' ) {
			break;
		}
		if ( end ) {
			p++;
		}
		len = 0;
		lastSpace = -1;
		line[0] = '\0';
	}
	return y;
}


// at the right end of a heading of a list ("fold"): down while its rows are there, to the right when they are
// folded away. While a search is on the headings fold nothing and have none.
static void Item_Fold_Paint( itemDef_t *item ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const qboolean	card = (qboolean)( menu && menu->canvasW > 0.0f );
	// (in a menu with a canvas the heading is the head of a card: the arrow on the middle between its ring and its
	// line, as its words, Item_CardHeadBand)
	const float		h = card ? 10.0f * menu->canvasUnit : ROW_ARROW;
	float			mid = item->window.rect.y + item->window.rect.h * 0.5f, bandY, bandH;
	const float		pad = card ? 22.0f * menu->canvasUnit : ROW_PAD;
	vec4_t			ink;

	if ( !menu || menu->searching || ( item->window.flags & WINDOW_DECORATION ) ) {
		return;
	}
	if ( card && Item_CardHeadBand( item, &bandY, &bandH ) ) {
		mid = bandY + bandH * 0.5f;
	}
	VectorCopy4( item->window.foreColor, ink );
	if ( item->window.flags & WINDOW_HASFOCUS ) {
		ink[3] = 1.0f;
	}
	if ( Menu_FoldIs( menu->listFolds, item->fold ) ) {
		Skin_Arrow( item->window.rect.x + item->window.rect.w - pad - h * 0.62f, mid - h * 0.5f, h * 0.62f, h, qfalse, ink );
	} else {
		Skin_ArrowDown( item->window.rect.x + item->window.rect.w - pad - h, mid - h * 0.31f, h, h * 0.62f, ink );
	}
}

// in the window of the list, while a search found nothing
static void Menu_SearchEmpty_Paint( menuDef_t *menu ) {
	vec4_t		color = { 0.60f, 0.69f, 0.78f, 1.0f };
	rectDef_t	list;
	int			i;

	if ( !menu->searching || menu->searchFound ) {
		return;
	}
	Menu_ListWindow( menu, &list );
	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t *item = menu->items[i];

		if ( item->row ) {
			Text_PaintWrapped( list.x + ROW_PAD, list.y + ROW_PAD, list.w - 2.0f * ROW_PAD, item->textscale, color,
				Vision_Text( "No setting has these words." ), item->iMenuFont, 2 );
			return;
		}
	}
}

/*
=================
Item_Desc_Paint

ITEM_TYPE_DESC: what the item in focus is for, in the rectangle of this item: its name in capital letters, a
short line and its descText. While nothing with a descText is in focus, the same for the page that is open (its
tab). A change fades in.
=================
*/
#define DESCPIC_SHOW_MSEC	1400		// one picture of a pair stands this long,
#define DESCPIC_FADE_MSEC	400			//	and goes over into the other in this

// the two pictures of a setting ("descPic"), at the bottom of the pane, one after the other with a fade between
static void Item_DescPic_Paint( const itemDef_t *item, const itemDef_t *about, float fade ) {
	const rectDef_t	*r = &item->window.rect;
	const float		h = r->w * 9.0f / 16.0f, y = r->y + r->h - h;
	const int		cycle = 2 * ( DESCPIC_SHOW_MSEC + DESCPIC_FADE_MSEC ), t = DC->realTime % cycle;
	const qhandle_t	pics[2] = { DC->registerShaderNoMip( va( "%s_a", about->descPic[0] ) ),
								DC->registerShaderNoMip( va( "%s_b", about->descPic[0] ) ) };
	float			second;
	vec4_t			color = { 1.0f, 1.0f, 1.0f, 1.0f };
	char			name[64];
	int				i;

	if ( !pics[0] || !pics[1] ) {
		return;
	}
	// how much of the second one shows: 0 for a while, up to 1, 1 for a while, down to 0
	if ( t < DESCPIC_SHOW_MSEC ) {
		second = 0.0f;
	} else if ( t < DESCPIC_SHOW_MSEC + DESCPIC_FADE_MSEC ) {
		second = ( t - DESCPIC_SHOW_MSEC ) / (float)DESCPIC_FADE_MSEC;
	} else if ( t < 2 * DESCPIC_SHOW_MSEC + DESCPIC_FADE_MSEC ) {
		second = 1.0f;
	} else {
		second = 1.0f - ( t - 2 * DESCPIC_SHOW_MSEC - DESCPIC_FADE_MSEC ) / (float)DESCPIC_FADE_MSEC;
	}
	color[3] = fade;
	DC->setColor( color );
	DC->drawHandlePic( r->x, y, r->w, h, pics[0] );
	color[3] = fade * second;
	DC->setColor( color );
	DC->drawHandlePic( r->x, y, r->w, h, pics[1] );
	DC->setColor( NULL );

	// the value each one shows, on a dark band at its foot
	for ( i = 0; i < 2; i++ ) {
		const float a = fade * ( i ? second : 1.0f - second );
		vec4_t		band = { 0.0f, 0.0f, 0.0f, 0.55f * a }, ink;

		if ( a <= 0.0f ) {
			continue;
		}
		VectorCopy4( item->window.foreColor, ink );
		ink[3] *= a;
		Text_Word( about->descPic[1 + i], name, sizeof( name ), qtrue, qfalse );
		DC->fillRect( r->x, y + h - 13.0f, r->w, 13.0f, band );
		DC->drawText( r->x + 4.0f, y + h - 12.0f, item->textscale * 0.8f, ink, name, 0, 0, 0, item->iMenuFont );
	}
}

static void Item_Desc_Paint( itemDef_t *item ) {
	menuDef_t		*parent = (menuDef_t *)item->parent;
	const rectDef_t	*r = &item->window.rect;
	itemDef_t		*about = NULL;
	char			title[256], body[1024];
	vec4_t			color;
	float			y;
	int				i, index = -1;

	for ( i = 0; i < parent->itemCount; i++ ) {
		itemDef_t *it = parent->items[i];

		if ( ( it->window.flags & WINDOW_HASFOCUS ) && ( it->window.flags & WINDOW_VISIBLE ) && it->descText && !Item_Away( it ) ) {
			about = it;
			index = i;
		}
	}
	for ( i = 0; i < parent->itemCount && !about; i++ ) {
		if ( parent->items[i]->tab && parent->items[i]->tab == parent->page && parent->items[i]->descText ) {
			about = parent->items[i];
			index = i;
		}
	}
	if ( !about ) {
		return;
	}
	if ( item->cursorPos != index + 1 ) {
		item->cursorPos = index + 1;
		item->skinValue = 0.0f;
	}
	item->skinValue = Skin_Ease( item->skinValue, qtrue, SKIN_EASE_MSEC );

	Text_Word( about->text, title, sizeof( title ), qtrue, qtrue );
	if ( !title[0] && about->type == ITEM_TYPE_MULTI ) {
		// (a row without a name, the switch between the settings that matter and all of them: what it is set to)
		Text_Word( Item_Multi_Setting( about ), title, sizeof( title ), qtrue, qtrue );
	}
	Text_Word( about->descText, body, sizeof( body ), qfalse, qfalse );
	y = r->y - 11.0f;
	if ( title[0] ) {
		Skin_Color( color, item->window.foreColor, item->skinValue );
		y = Text_PaintWrapped( r->x, r->y, r->w, item->textscale * 1.45f, color, title, item->iMenuFont, 3 );
		Skin_Color( color, parent->focusColor, item->skinValue );
		Skin_Panel( r->x, y + 3.0f, 26.0f, 1.5f, 0.75f, color, NULL );
	}
	Skin_Color( color, parent->descColor, item->skinValue );
	y = Text_PaintWrapped( r->x, y + 11.0f, r->w, item->textscale, color, body, item->iMenuFont, about->descPic[0] ? 6 : 12 );
	if ( about->descPic[0] ) {
		Item_DescPic_Paint( item, about, item->skinValue );
	} else if ( parent->canvasW > 0.0f && ( about->type == ITEM_TYPE_BIND || ( Item_IsSetting( about ) && DC->getCVarInfo ) ) ) {
		// in a menu with a canvas: what the setting is by default, what it is now and when a change takes effect;
		// for a key, the keys it has, the keys it has by default and its command
		const float	u = parent->canvasUnit, line = 26.0f * u, scale = Item_Pixels( item, 15.0f );
		const char	*names[3] = { Vision_Text( "Default" ), Vision_Text( "Current" ), Vision_Text( "Takes effect" ) };
		char		def[MENU_CHANGE_VALUE], now[MENU_CHANGE_VALUE], values[3][128];
		uint32_t	flags;
		float		labelW = 0.0f;
		int			k, current = 1;
		qboolean	have = qfalse;

		if ( about->type == ITEM_TYPE_BIND ) {
			const int id = BindingIDFromName( about->cvar );

			names[0] = Vision_Text( "Keys" );
			names[1] = Vision_Text( "Default" );
			names[2] = Vision_Text( "Command" );
			current = 0;
			BindingFromName( about->cvar );
			Q_strncpyz( values[0], Q_stricmp( g_nameBind, "???" ) ? g_nameBind : Vision_Text( "None" ), sizeof( values[0] ) );
			Q_strncpyz( values[1], id >= 0 && g_bindDefault[id][0] ? g_bindDefault[id] : Vision_Text( "None" ), sizeof( values[1] ) );
			Q_strncpyz( values[2], about->cvar ? about->cvar : "", sizeof( values[2] ) );
			have = qtrue;
		} else if ( DC->getCVarInfo( about->cvar, def, sizeof( def ), &flags ) ) {
			DC->getCVarString( about->cvar, now, sizeof( now ) );
			Item_ValueText( about, def, values[0], sizeof( values[0] ) );
			Item_ValueText( about, now, values[1], sizeof( values[1] ) );
			Q_strncpyz( values[2], Vision_Text( Cvar_NeedsRestart( about->cvar ) ? "After restart" : "Instantly" ), sizeof( values[2] ) );
			have = qtrue;
		}
		if ( have ) {
			for ( k = 0; k < 3; k++ ) {
				labelW = Q_max( labelW, DC->textWidth( names[k], scale, item->iMenuFont ) );
			}
			y += 18.0f * u;
			for ( k = 0; k < 3; k++ ) {
				vec4_t value;

				Skin_Color( color, parent->descColor, item->skinValue );
				Row_Text( r->x, y, line, 0.0f, ITEM_ALIGN_LEFT, scale, color, names[k], item->iMenuFont );
				if ( k == current ) {
					Skin_Color( value, parent->focusColor, item->skinValue );
				} else {
					Skin_Color( value, item->window.foreColor, item->skinValue );
				}
				Row_Text( r->x + labelW + 20.0f * u, y, line, r->w - labelW - 20.0f * u, ITEM_ALIGN_LEFT, scale, value, values[k],
					item->iMenuFont );
				y += line;
			}
		}
	}
}

static void Item_PageScroll_Paint( itemDef_t *item ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const rectDef_t	*r = &item->window.rect;
	float			start, part;

	if ( Item_PageScroll_Thumb( item, &start, &part ) ) {
		Skin_ScrollBar( r->x, r->y, r->w, r->h, start, part, qfalse, menu->focusColor );
	}
}

qboolean Display_KeyBindPending( void ) {
	return g_waitingForKey;
}

qboolean Item_Bind_HandleKey(itemDef_t *item, int key, qboolean down) {
	int id;

	if ( key == A_MOUSE1 && Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory) && !g_waitingForKey ) {
		if (down) {
			g_waitingForKey = qtrue;
			g_bindItem = item;
			g_bindSlot = 0;
			if ( Item_Bind_Slots( item ) ) {
				rectDef_t second;

				// (the field that was clicked; a click on the name takes the first key)
				Item_Bind_Slot( item, 1, &second );
				g_bindSlot = DC->cursorx >= second.x ? 1 : 0;
			}
		}
		return qtrue;
	}
	else if ( key == A_ENTER && !g_waitingForKey ) {
		if ( down ) {
			g_waitingForKey = qtrue;
			g_bindItem = item;
			g_bindSlot = 0;
		}
		return qtrue;
	}
	else {
		if ( !g_waitingForKey || g_bindItem == NULL ) {
			return qfalse;
		}

		if ( key & K_CHAR_FLAG ) {
			return qtrue;
		}

		switch ( key ) {
			case A_ESCAPE:
				g_waitingForKey = qfalse;
				return qtrue;

			case A_BACKSPACE:
				id = BindingIDFromName(item->cvar);
				if ( id != -1 && Item_Bind_Slots( item ) ) {
					// only the key of the field, the other one moves to the front
					if ( g_bindKeys[id][g_bindSlot] != -1 ) {
						DC->setBinding( g_bindKeys[id][g_bindSlot], "" );
					}
					if ( g_bindSlot == 0 ) {
						g_bindKeys[id][0] = g_bindKeys[id][1];
					}
					g_bindKeys[id][1] = -1;
				}
				else if (id != -1)
				{
					if ( g_bindKeys[id][0] != -1 )
						DC->setBinding(g_bindKeys[id][0], "");

					if ( g_bindKeys[id][1] != -1 )
						DC->setBinding(g_bindKeys[id][1], "");

					g_bindKeys[id][0] = -1;
					g_bindKeys[id][1] = -1;
				}
				Controls_SetConfig();
				g_waitingForKey = qfalse;
				g_bindItem = NULL;
				return qtrue;

			case '`':
				return qtrue;
		}
	}

	id = BindingIDFromName( item->cvar );
	if ( id != -1 && key != -1 && Item_Bind_Slots( item ) ) {
		// the key of the field that was clicked is replaced, the other one stays
		const int	old = g_bindKeys[id][g_bindSlot];
		int			keep = -1, j;
		size_t		i;

		for ( j = 0; j < 2; j++ ) {
			if ( g_bindKeys[id][j] != -1 && g_bindKeys[id][j] != old && g_bindKeys[id][j] != key ) {
				keep = g_bindKeys[id][j];
			}
		}
		// (a key does one thing: it leaves the commands it had)
		for ( i = 0; i < g_bindCount; i++ ) {
			if ( (int)i == id ) {
				continue;
			}
			if ( g_bindKeys[i][1] == key ) {
				g_bindKeys[i][1] = -1;
			}
			if ( g_bindKeys[i][0] == key ) {
				g_bindKeys[i][0] = g_bindKeys[i][1];
				g_bindKeys[i][1] = -1;
			}
		}
		if ( old != -1 && old != key ) {
			DC->setBinding( old, "" );
		}
		// (a second key without a first one becomes the first)
		if ( g_bindSlot == 0 || keep == -1 ) {
			g_bindKeys[id][0] = key;
			g_bindKeys[id][1] = g_bindSlot == 0 ? keep : -1;
		} else {
			g_bindKeys[id][0] = keep;
			g_bindKeys[id][1] = key;
		}
		Controls_SetConfig();
		g_waitingForKey = qfalse;
		return qtrue;
	}

	if ( key != -1 ) {
		size_t	i;

		for ( i=0; i<g_bindCount; i++ )
		{
			if ( g_bindKeys[i][1] == key )
				g_bindKeys[i][1] = -1;

			if ( g_bindKeys[i][0] == key ) {
				g_bindKeys[i][0] = g_bindKeys[i][1];
				g_bindKeys[i][1] = -1;
			}
		}
	}

	id = BindingIDFromName( item->cvar );

	if ( id != -1 ) {
		if ( key == -1 ) {
			if( g_bindKeys[id][0] != -1 ) {
				DC->setBinding( g_bindKeys[id][0], "" );
				g_bindKeys[id][0] = -1;
			}
			if( g_bindKeys[id][1] != -1 ) {
				DC->setBinding( g_bindKeys[id][1], "" );
				g_bindKeys[id][1] = -1;
			}
		}
		else if ( g_bindKeys[id][0] == -1 )
			g_bindKeys[id][0] = key;
		else if ( g_bindKeys[id][0] != key && g_bindKeys[id][1] == -1 )
			g_bindKeys[id][1] = key;
		else {
			DC->setBinding( g_bindKeys[id][0], "" );
			DC->setBinding( g_bindKeys[id][1], "" );
			g_bindKeys[id][0] = key;
			g_bindKeys[id][1] = -1;
		}
	}

	Controls_SetConfig();
	g_waitingForKey = qfalse;

	return qtrue;
}

// scale the model should we need to
void UI_ScaleModelAxis( refEntity_t *ent )
{
	if ( ent->modelScale[0] && ent->modelScale[0] != 1.0f ) {
		VectorScale( ent->axis[0], ent->modelScale[0] , ent->axis[0] );
		ent->nonNormalizedAxes = qtrue;
	}
	if ( ent->modelScale[1] && ent->modelScale[1] != 1.0f ) {
		VectorScale( ent->axis[1], ent->modelScale[1] , ent->axis[1] );
		ent->nonNormalizedAxes = qtrue;
	}
	if ( ent->modelScale[2] && ent->modelScale[2] != 1.0f ) {
		VectorScale( ent->axis[2], ent->modelScale[2] , ent->axis[2] );
		ent->nonNormalizedAxes = qtrue;
	}
}

#ifndef _CGAME
extern void UI_SaberAttachToChar( itemDef_t *item );
#endif

void Item_Model_Paint(itemDef_t *item)
{
	float x, y, w, h;
	refdef_t refdef;
	refEntity_t		ent;
	vec3_t			mins, maxs, origin;
	vec3_t			angles;
	modelDef_t *modelPtr = item->typeData.model;

	if (modelPtr == NULL)
	{
		return;
	}

	// a moves datapad anim is playing
#ifdef UI_BUILD
	if (uiInfo.moveAnimTime && (uiInfo.moveAnimTime < uiInfo.uiDC.realTime))
	{
		if (modelPtr)
		{
			char modelPath[MAX_QPATH];

			Com_sprintf( modelPath, sizeof( modelPath ), "models/players/%s/model.glm", UI_Cvar_VariableString ( "ui_char_model" ) );
			//HACKHACKHACK: check for any multi-part anim sequences, and play the next anim, if needbe
			switch( modelPtr->g2anim )
			{
			case BOTH_FORCEWALLREBOUND_FORWARD:
			case BOTH_FORCEJUMP1:
				ItemParse_model_g2anim_go( item, animTable[BOTH_FORCEINAIR1].name );
				ItemParse_asset_model_go( item, modelPath, &uiInfo.moveAnimTime );
				if ( !uiInfo.moveAnimTime )
				{
					uiInfo.moveAnimTime = 500;
				}
				uiInfo.moveAnimTime += uiInfo.uiDC.realTime;
				break;
			case BOTH_FORCEINAIR1:
				ItemParse_model_g2anim_go( item, animTable[BOTH_FORCELAND1].name );
				ItemParse_asset_model_go( item, modelPath, &uiInfo.moveAnimTime );
				uiInfo.moveAnimTime += uiInfo.uiDC.realTime;
				break;
			case BOTH_FORCEWALLRUNFLIP_START:
				ItemParse_model_g2anim_go( item, animTable[BOTH_FORCEWALLRUNFLIP_END].name );
				ItemParse_asset_model_go( item, modelPath, &uiInfo.moveAnimTime );
				uiInfo.moveAnimTime += uiInfo.uiDC.realTime;
				break;
			case BOTH_FORCELONGLEAP_START:
				ItemParse_model_g2anim_go( item, animTable[BOTH_FORCELONGLEAP_LAND].name );
				ItemParse_asset_model_go( item, modelPath, &uiInfo.moveAnimTime );
				uiInfo.moveAnimTime += uiInfo.uiDC.realTime;
				break;
			case BOTH_A3_TL_BR:	// JA+: melee throw
				ItemParse_model_g2anim_go( item, animTable[BOTH_D3_TL___].name );
				ItemParse_asset_model_go( item, modelPath, &uiInfo.moveAnimTime );
				uiInfo.moveAnimTime += uiInfo.uiDC.realTime;
				break;
			case BOTH_FORCE_DRAIN_GRAB_START:	// JA+: sith kiss
				ItemParse_model_g2anim_go( item, animTable[BOTH_FORCE_DRAIN_GRAB_END].name );
				ItemParse_asset_model_go( item, modelPath, &uiInfo.moveAnimTime );
				uiInfo.moveAnimTime += uiInfo.uiDC.realTime;
				break;
			case BOTH_KNOCKDOWN3://on front - into force getup
				trap->S_StartLocalSound( uiInfo.uiDC.Assets.moveJumpSound, CHAN_LOCAL );
				ItemParse_model_g2anim_go( item, animTable[BOTH_FORCE_GETUP_F1].name );
				ItemParse_asset_model_go( item, modelPath, &uiInfo.moveAnimTime );
				uiInfo.moveAnimTime += uiInfo.uiDC.realTime;
				break;
			case BOTH_KNOCKDOWN2://on back - kick forward getup
				trap->S_StartLocalSound( uiInfo.uiDC.Assets.moveJumpSound, CHAN_LOCAL );
				ItemParse_model_g2anim_go( item, animTable[BOTH_GETUP_BROLL_F].name );
				ItemParse_asset_model_go( item, modelPath, &uiInfo.moveAnimTime );
				uiInfo.moveAnimTime += uiInfo.uiDC.realTime;
				break;
			case BOTH_KNOCKDOWN1://on back - roll-away
				trap->S_StartLocalSound( uiInfo.uiDC.Assets.moveRollSound, CHAN_LOCAL );
				ItemParse_model_g2anim_go( item, animTable[BOTH_GETUP_BROLL_R].name );
				ItemParse_asset_model_go( item, modelPath, &uiInfo.moveAnimTime );
				uiInfo.moveAnimTime += uiInfo.uiDC.realTime;
				break;
			default:
				ItemParse_model_g2anim_go( item,  uiInfo.movesBaseAnim );
				ItemParse_asset_model_go( item, modelPath, &uiInfo.moveAnimTime );
				uiInfo.moveAnimTime = 0;
				break;
			}

			UI_UpdateCharacterSkin();

			//update saber models
			UI_SaberAttachToChar( item );
		}
	}
#endif

	// setup the refdef
	memset( &refdef, 0, sizeof( refdef ) );

	refdef.rdflags = RDF_NOWORLDMODEL;
	AxisClear( refdef.viewaxis );
	x = item->window.rect.x+1;
	y = item->window.rect.y+1;
	w = item->window.rect.w-2;
	h = item->window.rect.h-2;

	refdef.x = ( x + DC->bias ) * DC->xscale;
	refdef.y = ( y + DC->biasY ) * DC->yscale;
	refdef.width = w * DC->xscale;
	refdef.height = h * DC->yscale;

	if (item->ghoul2)
	{ //ghoul2 models don't have bounds, so we have to parse them.
		VectorCopy(modelPtr->g2mins, mins);
		VectorCopy(modelPtr->g2maxs, maxs);

		if (!mins[0] && !mins[1] && !mins[2] &&
			!maxs[0] && !maxs[1] && !maxs[2])
		{ //we'll use defaults then I suppose.
			VectorSet(mins, -16, -16, -24);
			VectorSet(maxs, 16, 16, 32);
		}
	}
	else
	{
		DC->modelBounds( item->asset, mins, maxs );
	}

	origin[2] = -0.5 * ( mins[2] + maxs[2] );
	origin[1] = 0.5 * ( mins[1] + maxs[1] );

	// calculate distance so the model nearly fills the box
	if (qtrue)
	{
		float len = 0.5 * ( maxs[2] - mins[2] );
		origin[0] = len / 0.268;	// len / tan( fov/2 )
		//origin[0] = len / tan(w/2);
	}
	else
	{
		origin[0] = item->textscale;
	}
	refdef.fov_x = (modelPtr->fov_x) ? modelPtr->fov_x : (int)((float)refdef.width / 640.0f * 90.0f);
	refdef.fov_y = (modelPtr->fov_y) ? modelPtr->fov_y : atan2( refdef.height, refdef.width / tan( refdef.fov_x / 360 * M_PI ) ) * ( 360 / M_PI );
	if ( modelPtr->fov_x && modelPtr->fov_y && refdef.width > 0 && refdef.height > 0 ) {
		// The two angles of a menu fit the box it was made for. On a screen of another shape (wide screens, canvas
		// pages) the box has other proportions in pixels, and the model was stretched: keep the height (the model
		// stands in it, its distance follows from it above) and take the width from the pixels.
		refdef.fov_x = RAD2DEG( atan( tan( DEG2RAD( refdef.fov_y * 0.5f ) ) * refdef.width / refdef.height ) ) * 2.0f;
	}

	//refdef.fov_x = (int)((float)refdef.width / 640.0f * 90.0f);
	//refdef.fov_y = atan2( refdef.height, refdef.width / tan( refdef.fov_x / 360 * M_PI ) ) * ( 360 / M_PI );

	DC->clearScene();

	refdef.time = DC->realTime;

	// add the model

	memset( &ent, 0, sizeof(ent) );

	// use item storage to track
	if ( (item->flags&ITF_ISANYSABER) && !(item->flags&ITF_ISCHARACTER) )
	{//hack to put saber on it's side
		if (modelPtr->rotationSpeed)
		{
			VectorSet( angles, modelPtr->angle+(float)refdef.time/modelPtr->rotationSpeed, 0, 90 );
		}
		else
		{
			VectorSet( angles, modelPtr->angle, 0, 90 );
		}
	}
	else if (modelPtr->rotationSpeed)
	{
		VectorSet( angles, 0, modelPtr->angle + (float)refdef.time/modelPtr->rotationSpeed, 0 );
	}
	else
	{
		VectorSet( angles, 0, modelPtr->angle, 0 );
	}

	AnglesToAxis( angles, ent.axis );

#ifndef _CGAME
	if ( Item_Skinned( item ) && ( item->flags & ITF_ISANYSABER ) && !( item->flags & ITF_ISCHARACTER ) ) {
		vec3_t	middle;
		float	length;

		// In the new look a saber turns about its middle, and all of it, blades included, fits the width of its
		// box. The old menus placed it by hand around the end of its hilt, the bounds of the model have no blade.
		if ( UI_SaberFrame( item, middle, &length ) ) {
			// (a line of half length c that turns about its middle at distance d looks at most c / sqrt( d*d - c*c )
			// wide: at d = c * sqrt( 1 + 1 / t*t ) it never leaves a view of half width t)
			const float t = tan( DEG2RAD( refdef.fov_x * 0.5f ) );

			VectorClear( origin );
			origin[0] = length * 0.5f * sqrt( 1.0f + 1.0f / ( t * t ) ) * 1.08f;
			VectorMA( origin, -middle[0], ent.axis[0], origin );
			VectorMA( origin, -middle[1], ent.axis[1], origin );
			VectorMA( origin, -middle[2], ent.axis[2], origin );
		}
	}
#endif

	if (item->ghoul2)
	{
		ent.ghoul2 = item->ghoul2;
		ent.radius = 1000;
		ent.customSkin = modelPtr->g2skin;

		VectorCopy(modelPtr->g2scale, ent.modelScale);
		UI_ScaleModelAxis(&ent);
#ifndef _CGAME
		if ( (item->flags&ITF_ISCHARACTER) )
		{
			ent.shaderRGBA[0] = ui_char_color_red.integer;
			ent.shaderRGBA[1] = ui_char_color_green.integer;
			ent.shaderRGBA[2] = ui_char_color_blue.integer;
			ent.shaderRGBA[3] = 255;
//			UI_TalkingHead(item);
		}
		if ( item->flags&ITF_ISANYSABER )
		{//UGH, draw the saber blade!
			UI_SaberDrawBlades( item, origin, angles );
		}
#endif
	}
	else
	{
		ent.hModel = item->asset;
	}
	VectorCopy( origin, ent.origin );
	VectorCopy( ent.origin, ent.oldorigin );

	// Set up lighting
	VectorCopy( origin, ent.lightingOrigin );
	ent.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW;

	DC->addRefEntityToScene( &ent );
	DC->renderScene( &refdef );

}


void Item_Image_Paint(itemDef_t *item) {
	if (item == NULL) {
		return;
	}
	DC->drawHandlePic(item->window.rect.x+1, item->window.rect.y+1, item->window.rect.w-2, item->window.rect.h-2, item->asset);
}


void Item_TextScroll_Paint(itemDef_t *item)
{
	// (static: item->text points at it after this returns, and Item_SetScreenCoords builds the lines from it again)
	static char cvartext[1024];
	float x, y, size, count, thumb;
	int	  i;
	textScrollDef_t *scrollPtr = item->typeData.textscroll;

	count = scrollPtr->iLineCount;

	// draw scrollbar to right side of the window
	x = item->window.rect.x + item->window.rect.w - SCROLLBAR_SIZE - 1;
	y = item->window.rect.y + 1;
	if ( Item_Skinned( item ) ) {
		// a thin bar over the same track the mouse works on
		const float track = item->window.rect.h - SCROLLBAR_SIZE * 3;

		// (only when there is more text than room, as the lists)
		scrollPtr->endPos = scrollPtr->startPos;
		if ( Item_TextScroll_MaxScroll( item ) > 0 ) {
			thumb = Item_TextScroll_ThumbDrawPosition( item );
			Skin_ScrollBar( x, y + SCROLLBAR_SIZE, SCROLLBAR_SIZE, item->window.rect.h - SCROLLBAR_SIZE * 2,
				( track > 0.0f ) ? ( thumb - y - SCROLLBAR_SIZE ) / ( track + SCROLLBAR_SIZE ) : 0.0f,
				( track > 0.0f ) ? SCROLLBAR_SIZE / ( track + SCROLLBAR_SIZE ) : 1.0f, qfalse, ( (menuDef_t *)item->parent )->focusColor );
		}
	} else {
	DC->drawHandlePic(x, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE, DC->Assets.scrollBarArrowUp);
	y += SCROLLBAR_SIZE - 1;

	scrollPtr->endPos = scrollPtr->startPos;
	size = item->window.rect.h - (SCROLLBAR_SIZE * 2);
	DC->drawHandlePic(x, y, SCROLLBAR_SIZE, size+1, DC->Assets.scrollBar);
	y += size - 1;
	DC->drawHandlePic(x, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE, DC->Assets.scrollBarArrowDown);

	// thumb
	thumb = Item_TextScroll_ThumbDrawPosition(item);
	if (thumb > y - SCROLLBAR_SIZE - 1)
	{
		thumb = y - SCROLLBAR_SIZE - 1;
	}
	DC->drawHandlePic(x, thumb, SCROLLBAR_SIZE, SCROLLBAR_SIZE, DC->Assets.scrollBarThumb);
	}

	if (item->cvar)
	{
		DC->getCVarString(item->cvar, cvartext, sizeof(cvartext));
		item->text = cvartext;
		Item_TextScroll_BuildLines ( item );
	}

	// adjust size for item painting
	size = item->window.rect.h - 2;
	x	 = item->window.rect.x + item->textalignx + 1;
	y	 = item->window.rect.y + item->textaligny + 1;

	for (i = scrollPtr->startPos; i < count; i++)
	{
		const char *text;

		text = scrollPtr->pLines[i];
		if (!text)
		{
			continue;
		}

		DC->drawText(x + 4, y, item->textscale, item->window.foreColor, text, 0, 0, item->textStyle, item->iMenuFont);

		size -= scrollPtr->lineHeight;
		if (size < scrollPtr->lineHeight)
		{
			scrollPtr->drawPadding = scrollPtr->lineHeight - size;
			break;
		}

		scrollPtr->endPos++;
		y += scrollPtr->lineHeight;
	}
}

#define COLOR_MAX 255.0f

// Draw routine for list boxes
// The mark of the chosen picture of a list of pictures, at x, y: a ring in a menu of the new look (and a faint one
// on the picture under the mouse), the rectangle of the border colour in the others
static void Item_ListBox_ImageMark( itemDef_t *item, float x, float y, int i ) {
	const listBoxDef_t *listPtr = item->typeData.listbox;

	if ( Item_Skinned( item ) ) {
		const qboolean under = (qboolean)( ( item->window.flags & WINDOW_MOUSEOVER ) && DC->cursorx >= x && DC->cursorx < x + listPtr->elementWidth
			&& DC->cursory >= y && DC->cursory < y + listPtr->elementHeight );

		Skin_Tile( x, y, listPtr->elementWidth - 1, listPtr->elementHeight - 1, ( (menuDef_t *)item->parent )->focusColor,
			(qboolean)( i == item->cursorPos ), under );
	} else if ( i == item->cursorPos ) {
		DC->drawRect( x, y, listPtr->elementWidth - 1, listPtr->elementHeight - 1, item->window.borderSize, item->window.borderColor );
	}
}

// Where the text of a column of a list in the new look begins, from the left of the list: where the menu file puts it,
// the first one clear of the mark of the chosen row (16 pixels of the canvas, the room the Join table gives its first
// column). Many lists of the menu files start their text 2 units from the edge.
static float List_ColumnX( const itemDef_t *item, int column ) {
	const menuDef_t	*menu = (const menuDef_t *)item->parent;
	const float		pos = item->typeData.listbox->columnInfo[column].pos;

	if ( column ) {
		return pos;
	}
	return Q_max( pos, ( menu && menu->canvasW > 0.0f ) ? 16.0f * menu->canvasUnit : 9.0f );
}

// How wide the filled row of a list in the new look is: from 3 units in to 3 units before the right edge, or before
// the scroll bar while there is one (it is drawn only when there is something to scroll)
static float List_RowWidth( itemDef_t *item ) {
	if ( !item->typeData.listbox->scrollhidden && Item_ListBox_MaxScroll( item ) > 0 ) {
		return item->window.rect.w - SCROLLBAR_SIZE - 4.0f;
	}
	return item->window.rect.w - 6.0f;
}

// How wide the text of a column of a row of a list may be in the new look: up to the next column that has a text in
// this row (the facts of a server leave score and ping empty), or the end of the filled row for the last one. The
// widths of the menu files are not to be trusted.
static float List_ColumnRoom( itemDef_t *item, int row, int column ) {
	const listBoxDef_t	*listPtr = item->typeData.listbox;
	const float			pad = 3.0f;
	int					k;

	for ( k = column + 1; k < listPtr->numColumns; k++ ) {
		qhandle_t	img1, img2, img3;
		const char	*next = DC->feederItemText( item->special, row, k, &img1, &img2, &img3 );

		if ( ( next && next[0] ) || img1 >= 0 || img2 >= 0 || img3 >= 0 ) {
			return List_ColumnX( item, k ) - List_ColumnX( item, column ) - pad;
		}
	}
	return 3.0f + List_RowWidth( item ) - List_ColumnX( item, column ) - pad;
}

// The text of a row of a list in the new look: its letters from x, its capitals on the middle of the row (rowY, h).
// The font's box is used where the engine does not know the letters.
static void List_TextAt( const char *text, float scale, int font, float x, float rowY, float h, float *tx, float *ty ) {
	if ( !Text_InkAt( text, scale, font, ITEM_ALIGN_LEFT, x, rowY, h, tx, ty ) ) {
		*tx = x;
		*ty = rowY + ( h - DC->textHeight( "A", scale, font ) ) * 0.5f;
	}
}

void Item_ListBox_Paint(itemDef_t *item) {
	float x, y, sizeWidth, count, i, i2,sizeHeight, thumb;
	int startPos;
	qhandle_t image;
	qhandle_t optionalImage1, optionalImage2, optionalImage3;
	listBoxDef_t *listPtr = item->typeData.listbox;
//JLF MPMOVED

	// the listbox is horizontal or vertical and has a fixed size scroll bar going either direction
	// elements are enumerated from the DC and either text or image handles are acquired from the DC as well
	// textscale is used to size the text, textalignx and textaligny are used to size image elements
	// there is no clipping available so only the last completely visible item is painted
	count = DC->feederCount(item->special);

	if (listPtr->startPos > (count?count-1:count))
	{//probably changed feeders, so reset
		listPtr->startPos = 0;
	}
//JLF END
	if (item->cursorPos > (count?count-1:count))
	{//probably changed feeders, so reset
		item->cursorPos = (count?count-1:count);
		// NOTE : might consider moving this to any spot in here we change the cursor position
		DC->feederSelection( item->special, item->cursorPos, NULL );
	}



	// default is vertical if horizontal flag is not here
	if (item->window.flags & WINDOW_HORIZONTAL)
	{
#ifdef	_DEBUG
		const char *text;
#endif

//JLF new variable (code just indented) MPMOVED
		if (!listPtr->scrollhidden)
		{
		// draw scrollbar in bottom of the window
		// bar
			if (Item_ListBox_MaxScroll(item) > 0)
			{
				x = item->window.rect.x + 1;
				y = item->window.rect.y + item->window.rect.h - SCROLLBAR_SIZE - 1;
				if ( Item_Skinned( item ) ) {
					// a thin bar over the same track the mouse works on
					const float track = item->window.rect.w - SCROLLBAR_SIZE * 3;

					thumb = Item_ListBox_ThumbDrawPosition( item );
					// (on the lower rim: a strip of pictures is as high as one picture, and the bar lay on them)
					Skin_ScrollBar( x + SCROLLBAR_SIZE, item->window.rect.y + item->window.rect.h - 2.0f, item->window.rect.w - SCROLLBAR_SIZE * 2, 4.0f,
						( track > 0.0f ) ? ( thumb - x - SCROLLBAR_SIZE ) / ( track + SCROLLBAR_SIZE ) : 0.0f,
						( track > 0.0f ) ? SCROLLBAR_SIZE / ( track + SCROLLBAR_SIZE ) : 1.0f, qtrue, ( (menuDef_t *)item->parent )->focusColor );
				} else {
				DC->drawHandlePic(x, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE, DC->Assets.scrollBarArrowLeft);
				x += SCROLLBAR_SIZE - 1;
				sizeWidth = item->window.rect.w - (SCROLLBAR_SIZE * 2);
				DC->drawHandlePic(x, y, sizeWidth+1, SCROLLBAR_SIZE, DC->Assets.scrollBar);
				x += sizeWidth - 1;
				DC->drawHandlePic(x, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE, DC->Assets.scrollBarArrowRight);
				// thumb
				thumb = Item_ListBox_ThumbDrawPosition(item);//Item_ListBox_ThumbPosition(item);
				if (thumb > x - SCROLLBAR_SIZE - 1) {
					thumb = x - SCROLLBAR_SIZE - 1;
				}
				DC->drawHandlePic(thumb, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE, DC->Assets.scrollBarThumb);
				}
			}
			else if (listPtr->startPos > 0)
			{
				listPtr->startPos = 0;
			}
		}
//JLF end
		//
		listPtr->endPos = listPtr->startPos;
		sizeWidth = item->window.rect.w - 2;
		// items
		// size contains max available space
		if (listPtr->elementStyle == LISTBOX_IMAGE)
		{
			// fit = 0;
			x = item->window.rect.x + 1;
			y = item->window.rect.y + 1;
			for (i = listPtr->startPos; i < count; i++)
			{
				// always draw at least one
				// which may overdraw the box if it is too small for the element
				image = DC->feederItemImage(item->special, i);
				if (image)
				{
#ifndef _CGAME
					if (item->window.flags & WINDOW_PLAYERCOLOR)
					{
						vec4_t	color;

						color[0] = ui_char_color_red.integer/COLOR_MAX;
						color[1] = ui_char_color_green.integer/COLOR_MAX;
						color[2] = ui_char_color_blue.integer/COLOR_MAX;
						color[3] = 1.0f;
						DC->setColor(color);
					}
#endif
					DC->drawHandlePic(x+1, y+1, listPtr->elementWidth - 2, listPtr->elementHeight - 2, image);
				}

				Item_ListBox_ImageMark( item, x, y, (int)i );

				sizeWidth -= listPtr->elementWidth;
				if (sizeWidth < listPtr->elementWidth)
				{
					listPtr->drawPadding = sizeWidth; //listPtr->elementWidth - size;
					break;
				}
				x += listPtr->elementWidth;
				listPtr->endPos++;
				// fit++;
			}
		}
		else
		{
			//
		}

#ifdef	_DEBUG
		// Show pic name
		text = DC->feederItemText(item->special, item->cursorPos, 0, &optionalImage1, &optionalImage2, &optionalImage3);
		if (text)
		{
			DC->drawText(item->window.rect.x, item->window.rect.y+item->window.rect.h, item->textscale, item->window.foreColor, text, 0, 0, item->textStyle, item->iMenuFont);
		}
#endif

	}
	// A vertical list box
	else
	{
		//JLF new variable (code idented with if)
		if (!listPtr->scrollhidden)
		{

			// draw scrollbar to right side of the window
			x = item->window.rect.x + item->window.rect.w - SCROLLBAR_SIZE - 1;

			if ( (int)item->special == FEEDER_Q3HEADS )
				x -= 2;

			y = item->window.rect.y + 1;
			if ( Item_Skinned( item ) ) {
				// a thin bar over the same track the mouse works on, and only when there is something to scroll
				const float track = item->window.rect.h - SCROLLBAR_SIZE * 3;

				listPtr->endPos = listPtr->startPos;
				if ( Item_ListBox_MaxScroll( item ) > 0 ) {
					thumb = Item_ListBox_ThumbDrawPosition( item );
					Skin_ScrollBar( x, y + SCROLLBAR_SIZE, SCROLLBAR_SIZE, item->window.rect.h - SCROLLBAR_SIZE * 2,
						( track > 0.0f ) ? ( thumb - y - SCROLLBAR_SIZE ) / ( track + SCROLLBAR_SIZE ) : 0.0f,
						( track > 0.0f ) ? SCROLLBAR_SIZE / ( track + SCROLLBAR_SIZE ) : 1.0f, qfalse, ( (menuDef_t *)item->parent )->focusColor );
				}
			} else {
			DC->drawHandlePic(x, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE, DC->Assets.scrollBarArrowUp);
			y += SCROLLBAR_SIZE - 1;

			listPtr->endPos = listPtr->startPos;
			sizeHeight = item->window.rect.h - (SCROLLBAR_SIZE * 2);
			DC->drawHandlePic(x, y, SCROLLBAR_SIZE, sizeHeight+1, DC->Assets.scrollBar);
			y += sizeHeight - 1;
			DC->drawHandlePic(x, y, SCROLLBAR_SIZE, SCROLLBAR_SIZE, DC->Assets.scrollBarArrowDown);
			// thumb
			thumb = Item_ListBox_ThumbDrawPosition(item);//Item_ListBox_ThumbPosition(item);
			if (thumb > y - SCROLLBAR_SIZE - 1) {
				thumb = y - SCROLLBAR_SIZE - 1;
			}
			DC->drawHandlePic(x, thumb, SCROLLBAR_SIZE, SCROLLBAR_SIZE, DC->Assets.scrollBarThumb);
			}
		}
//JLF end

		// adjust size for item painting
		sizeWidth = item->window.rect.w - 2;
		sizeHeight = item->window.rect.h - 2;

		if (listPtr->elementStyle == LISTBOX_IMAGE)
		{
			// Multiple rows and columns (since it's more than twice as wide as an element)
			if ( item->window.rect.w > (listPtr->elementWidth*2) )
			{
				startPos = listPtr->startPos;
				x = item->window.rect.x + 1;
				y = item->window.rect.y + 1;
				// Next row
				for (i2 = startPos; i2 < count; i2++)
				{
					x = item->window.rect.x + 1;
					sizeWidth = item->window.rect.w - 2;
					// print a row
					for (i = startPos; i < count; i++)
					{
						// always draw at least one
						// which may overdraw the box if it is too small for the element
						image = DC->feederItemImage(item->special, i);
						if (image)
						{
		#ifndef _CGAME
							if (item->window.flags & WINDOW_PLAYERCOLOR)
							{
								vec4_t	color;

								color[0] = ui_char_color_red.integer/COLOR_MAX;
								color[1] = ui_char_color_green.integer/COLOR_MAX;
								color[2] = ui_char_color_blue.integer/COLOR_MAX;
								color[3] = 1.0f;
								DC->setColor(color);
							}
		#endif
							DC->drawHandlePic(x+1, y+1, listPtr->elementWidth - 2, listPtr->elementHeight - 2, image);
						}

						Item_ListBox_ImageMark( item, x, y, (int)i );

						sizeWidth -= listPtr->elementWidth;
						if (sizeWidth < listPtr->elementWidth)
						{
							listPtr->drawPadding = sizeWidth; //listPtr->elementWidth - size;
							break;
						}
						x += listPtr->elementWidth;
						listPtr->endPos++;
					}

					sizeHeight -= listPtr->elementHeight;
					if (sizeHeight < listPtr->elementHeight)
					{
						listPtr->drawPadding = sizeHeight; //listPtr->elementWidth - size;
						break;
					}
					// NOTE : Is endPos supposed to be valid or not? It was being used as a valid entry but I changed those
					// few spots that were causing bugs
					listPtr->endPos++;
					startPos = listPtr->endPos;
					y += listPtr->elementHeight;

				}
			}
			// single column
			else
			{
				x = item->window.rect.x + 1;
				y = item->window.rect.y + 1;
				for (i = listPtr->startPos; i < count; i++)
				{
					// always draw at least one
					// which may overdraw the box if it is too small for the element
					image = DC->feederItemImage(item->special, i);
					if (image)
					{
						DC->drawHandlePic(x+1, y+1, listPtr->elementWidth - 2, listPtr->elementHeight - 2, image);
					}

					Item_ListBox_ImageMark( item, x, y, (int)i );

					listPtr->endPos++;
					sizeHeight -= listPtr->elementHeight;
					if (sizeHeight < listPtr->elementHeight)
					{
						listPtr->drawPadding = listPtr->elementHeight - sizeHeight;
						break;
					}
					y += listPtr->elementHeight;
					// fit++;
				}
			}
		}
		else
		{
			x = item->window.rect.x + 1;
			y = item->window.rect.y + 1;
//JLF MPMOVED
			y = item->window.rect.y + 1 - listPtr->elementHeight;
			i = listPtr->startPos;

			for (; i < count; i++)
//JLF END
			{
				const char *text;
				// In a menu of the new look the text of a row stands on the middle of the row, where the mark of
				// the chosen row is drawn. "textaligny" does not count there: the menu files set it for the
				// letters of the old fonts, and the new ones stood above the middle with it.
				const qboolean	centred = Item_Skinned( item );
				const float		rowY = y + listPtr->elementHeight + 2;
				float			tx, ty, colScale = item->textscale;
				char			colText[MAX_STRING_CHARS];
				// always draw at least one
				// which may overdraw the box if it is too small for the element
				if (listPtr->numColumns > 0) {
					int j;//, subX = listPtr->elementHeight;

					for (j = 0; j < listPtr->numColumns; j++)
					{
						char	temp[MAX_STRING_CHARS];
						int imageStartX = listPtr->columnInfo[j].pos;
						text = DC->feederItemText(item->special, i, j, &optionalImage1, &optionalImage2, &optionalImage3);

						if( !text )
						{
							continue;
						}

						if (text[0]=='@')
						{
							trap->SE_GetStringTextString( &text[1]  , temp, sizeof(temp));
							text = temp;
						}

						/*
						if (optionalImage >= 0) {
							DC->drawHandlePic(x + 4 + listPtr->columnInfo[j].pos, y - 1 + listPtr->elementHeight / 2, listPtr->columnInfo[j].width, listPtr->columnInfo[j].width, optionalImage);
						}
						else
						*/
						if ( text )
						{
//JLF MPMOVED
							int textyOffset;
							textyOffset = 0;
//							DC->drawText(x + 4 + listPtr->columnInfo[j].pos, y + listPtr->elementHeight, item->textscale, item->window.foreColor, text, 0, listPtr->columnInfo[j].maxChars, item->textStyle);
	//WAS LAST						DC->drawText(x + 4 + listPtr->columnInfo[j].pos, y, item->textscale, item->window.foreColor, text, 0, listPtr->columnInfo[j].maxChars, item->textStyle, item->iMenuFont);
							if ( centred ) {
								// (in the new look a column begins where the menu file puts it, from the left of the
								// list: the headings over the columns are made in the same pixels of the canvas. A
								// text wider than its column is set smaller, it does not run into the next one)
								// (copied first: asking the feeder for the next columns takes its buffers again)
								// (and without the spaces some servers put in front of their names: every text of a
								// column starts at its edge)
								while ( *text == ' ' ) {
									text++;
								}
								Q_strncpyz( colText, text, sizeof( colText ) );
								text = colText;
								colScale = Text_FitScale( text, item->textscale, item->iMenuFont, List_ColumnRoom( item, i, j ) );
								List_TextAt( text, colScale, item->iMenuFont, item->window.rect.x + List_ColumnX( item, j ), rowY,
									listPtr->elementHeight, &tx, &ty );
							} else {
								tx = x + 4 + listPtr->columnInfo[j].pos;
								ty = y + listPtr->elementHeight + textyOffset + item->textaligny;
							}
							DC->drawText(tx, ty, centred ? colScale : item->textscale, item->window.foreColor, text, 0,listPtr->columnInfo[j].maxChars, item->textStyle, item->iMenuFont);


//JLF END
						}
						if ( j < listPtr->numColumns - 1 )
						{
							imageStartX = listPtr->columnInfo[j+1].pos;
						}
						DC->setColor( NULL );
						if ( centred ) {
							// The pictures of a row (password, force, sabers of a server) end where the next column
							// begins, as large as the letters. The old code left out where the list starts (near
							// 0 in the old menus) and made them a whole row high: on a canvas page they covered
							// the names of the levels.
							const float	icon = Q_min( listPtr->elementHeight - 4.0f, DC->textHeight( "A", item->textscale, item->iMenuFont ) * 1.6f );
							const float	iconY = y + listPtr->elementHeight + 2 + ( listPtr->elementHeight - icon ) * 0.5f;
							const float	right = x + imageStartX - 4.0f;

							if ( optionalImage3 >= 0 ) {
								DC->drawHandlePic( right - ( icon + 2.0f ) * 3.0f, iconY, icon, icon, optionalImage3 );
							}
							if ( optionalImage2 >= 0 ) {
								DC->drawHandlePic( right - ( icon + 2.0f ) * 2.0f, iconY, icon, icon, optionalImage2 );
							}
							if ( optionalImage1 >= 0 ) {
								DC->drawHandlePic( right - ( icon + 2.0f ), iconY, icon, icon, optionalImage1 );
							}
						} else {
						if (optionalImage3 >= 0)
						{
							DC->drawHandlePic(imageStartX - listPtr->elementHeight*3, y+listPtr->elementHeight+2, listPtr->elementHeight, listPtr->elementHeight, optionalImage3);
						}
						if (optionalImage2 >= 0)
						{
							DC->drawHandlePic(imageStartX - listPtr->elementHeight*2, y+listPtr->elementHeight+2, listPtr->elementHeight, listPtr->elementHeight, optionalImage2);
						}
						if (optionalImage1 >= 0)
						{
							DC->drawHandlePic(imageStartX - listPtr->elementHeight, y+listPtr->elementHeight+2, listPtr->elementHeight, listPtr->elementHeight, optionalImage1);
						}
						}
					}
				}
				else
				{
					text = DC->feederItemText(item->special, i, 0, &optionalImage1, &optionalImage2, &optionalImage3 );
					if ( optionalImage1 >= 0 || optionalImage2 >= 0 || optionalImage3 >= 0)
					{
						//DC->drawHandlePic(x + 4 + listPtr->elementHeight, y, listPtr->columnInfo[j].width, listPtr->columnInfo[j].width, optionalImage);
					}
					else if (text)
					{
//						DC->drawText(x + 4, y + listPtr->elementHeight, item->textscale, item->window.foreColor, text, 0, 0, item->textStyle);
						// (clear of the bar at the left of the chosen row in the new look, and cut with "..." where it
						// would run past the row: the lines of the intrusion log are longer than any dialog)
						if ( centred ) {
							Q_strncpyz( colText, text, sizeof( colText ) );
							text = Text_Shorten( colText, sizeof( colText ), item->textscale, item->iMenuFont,
								item->window.rect.x + 3.0f + List_RowWidth( item ) - ( x + 8 ) - 6.0f );
							List_TextAt( text, item->textscale, item->iMenuFont, x + 8, rowY, listPtr->elementHeight, &tx, &ty );
						} else {
							tx = x + 4;
							ty = y + item->textaligny;
						}
						DC->drawText(tx, ty, item->textscale, item->window.foreColor, text, 0, 0, item->textStyle, item->iMenuFont);
					}
				}

				if ( Item_Skinned( item ) ) {
					// the chosen row is filled, the one under the mouse is lit
					const float		rowW = List_RowWidth( item );
					const qboolean	under = (qboolean)( DC->cursorx >= x + 2 && DC->cursorx < x + 2 + rowW && DC->cursory >= rowY
										&& DC->cursory < rowY + listPtr->elementHeight && ( item->window.flags & WINDOW_MOUSEOVER ) );

					Skin_Row( x + 2, rowY, rowW, listPtr->elementHeight, ( (menuDef_t *)item->parent )->focusColor,
						(qboolean)( i == item->cursorPos && !listPtr->notselectable ), under ? 1.0f : 0.0f );
				}
				else if (i == item->cursorPos)
				{
					DC->fillRect(x + 2, y + listPtr->elementHeight + 2, item->window.rect.w - SCROLLBAR_SIZE - 4, listPtr->elementHeight, item->window.outlineColor);
				}

				sizeHeight -= listPtr->elementHeight;
				if (sizeHeight < listPtr->elementHeight)
				{
					listPtr->drawPadding = listPtr->elementHeight - sizeHeight;
					break;
				}
				listPtr->endPos++;
				y += listPtr->elementHeight;
				// fit++;
			}
		}
	}
}


void Item_OwnerDraw_Paint(itemDef_t *item) {

	if (item == NULL) {
		return;
	}

	if (DC->ownerDrawItem) {
		vec4_t color, lowLight;
		menuDef_t *parent = (menuDef_t*)item->parent;
		Fade(&item->window.flags, &item->window.foreColor[3], parent->fadeClamp, &item->window.nextTime, parent->fadeCycle, qtrue, parent->fadeAmount);
		memcpy(&color, &item->window.foreColor, sizeof(color));
		if (item->numColors > 0 && DC->getValue) {
			// if the value is within one of the ranges then set color to that, otherwise leave at default
			int i;
			float f = DC->getValue(item->window.ownerDraw);
			for (i = 0; i < item->numColors; i++) {
				if (f >= item->colorRanges[i].low && f <= item->colorRanges[i].high) {
					memcpy(&color, &item->colorRanges[i].color, sizeof(color));
					break;
				}
			}
		}

		if (item->window.flags & WINDOW_HASFOCUS) {
			lowLight[0] = 0.8 * parent->focusColor[0];
			lowLight[1] = 0.8 * parent->focusColor[1];
			lowLight[2] = 0.8 * parent->focusColor[2];
			lowLight[3] = 0.8 * parent->focusColor[3];
			LerpColor(parent->focusColor,lowLight,color,0.5+0.5*sin((float)(DC->realTime / PULSE_DIVISOR)));
		} else if (item->textStyle == ITEM_TEXTSTYLE_BLINK && !((DC->realTime/BLINK_DIVISOR) & 1)) {
			lowLight[0] = 0.8 * item->window.foreColor[0];
			lowLight[1] = 0.8 * item->window.foreColor[1];
			lowLight[2] = 0.8 * item->window.foreColor[2];
			lowLight[3] = 0.8 * item->window.foreColor[3];
			LerpColor(item->window.foreColor,lowLight,color,0.5+0.5*sin((float)(DC->realTime / PULSE_DIVISOR)));
		}

		if ( item->disabled )
			memcpy( color, parent->disableColor, sizeof( vec4_t ) );

		if ( item->cvarFlags & (CVAR_ENABLE | CVAR_DISABLE) && !Item_EnableShowViaCvar( item, CVAR_ENABLE ) )
			memcpy( color, parent->disableColor, sizeof( vec4_t ) );

		if (item->text) {
			Item_Text_Paint(item);
				if ( item->text[0] && Item_Skinned( item ) ) {
					// the new look: the word of the owner draw after the name (as below), caught (textCatch) and
					// written here, its capitals on the line of the name's and set smaller where it is longer than
					// the room up to the end of the item (the places of Create a Game: "Geschlossen"); a space after
					// the name, as "Source: Internet" written in one, not the 8 units of the old fonts
					const float	x0 = item->textRect.x + item->textRect.w + DC->textWidth( " ", item->textscale, item->iMenuFont );
					char		word[256];
					vec4_t		said;

					word[0] = '\0';
					VectorCopy4( color, said );
					textCatch.text = word;
					textCatch.size = sizeof( word );
					textCatch.color = said;
					DC->ownerDrawItem( x0, item->window.rect.y, item->window.rect.w, item->window.rect.h, 0, item->textaligny,
						item->window.ownerDraw, item->window.ownerDrawFlags, item->alignment, item->special, item->textscale, said,
						item->window.background, item->textStyle, item->iMenuFont );
					textCatch.text = NULL;
					textCatch.color = NULL;
					if ( word[0] && fabsf( textCatch.x - x0 ) < 1.0f ) {
						Row_Text( x0, item->window.rect.y, item->window.rect.h, item->window.rect.x + item->window.rect.w - x0,
							ITEM_ALIGN_LEFT, item->textscale, said, word, item->iMenuFont );
					} else {
						// (nothing caught, a text path that does not catch (the cgame's), or an owner draw that places
						// its word itself (at the right: the facts of the server): drawn by the owner draw as always)
						DC->ownerDrawItem( x0, item->window.rect.y, item->window.rect.w, item->window.rect.h, 0, item->textaligny,
							item->window.ownerDraw, item->window.ownerDrawFlags, item->alignment, item->special, item->textscale, color,
							item->window.background, item->textStyle, item->iMenuFont );
					}
				} else if (item->text[0]) {
					// +8 is an offset kludge to properly align owner draw items that have text combined with them
					DC->ownerDrawItem(item->textRect.x + item->textRect.w + 8, item->window.rect.y, item->window.rect.w, item->window.rect.h, 0, item->textaligny, item->window.ownerDraw, item->window.ownerDrawFlags, item->alignment, item->special, item->textscale, color, item->window.background, item->textStyle,item->iMenuFont);
				} else {
					DC->ownerDrawItem(item->textRect.x + item->textRect.w, item->window.rect.y, item->window.rect.w, item->window.rect.h, 0, item->textaligny, item->window.ownerDraw, item->window.ownerDrawFlags, item->alignment, item->special, item->textscale, color, item->window.background, item->textStyle,item->iMenuFont);
				}
			} else {
			DC->ownerDrawItem(item->window.rect.x, item->window.rect.y, item->window.rect.w, item->window.rect.h, item->textalignx, item->textaligny, item->window.ownerDraw, item->window.ownerDrawFlags, item->alignment, item->special, item->textscale, color, item->window.background, item->textStyle,item->iMenuFont);
		}
	}
}


/*
=================
Item_Stat_Paint

ITEM_TYPE_BAR and ITEM_TYPE_VALUE: a number of the game ("stat health", given by the cgame through
DC->statValue) as a bar that is filled to its part of the most it can be, or as text. The bar has the forecolor
of the item on its backcolor, or the colour of the first "addColorRange" the number is in. With "align
ITEM_ALIGN_RIGHT" it is filled from the right.
=================
*/
static void Item_Stat_Paint( itemDef_t *item ) {
	const rectDef_t	*r = &item->window.rect;
	float			value, max, part;
	vec4_t			color;
	int				i;

	if ( !item->stat || !DC->statValue || !DC->statValue( item->stat, &value, &max ) ) {
		return;
	}
	VectorCopy4( item->window.foreColor, color );
	for ( i = 0; i < item->numColors; i++ ) {
		if ( value >= item->colorRanges[i].low && value <= item->colorRanges[i].high ) {
			VectorCopy4( item->colorRanges[i].color, color );
			break;
		}
	}
	if ( item->type == ITEM_TYPE_VALUE ) {
		const char	*text = va( "%i", (int)value );
		float		x = item->textalignx;

		if ( item->textalignment == ITEM_ALIGN_RIGHT ) {
			x -= DC->textWidth( text, item->textscale, item->iMenuFont );
		} else if ( item->textalignment == ITEM_ALIGN_CENTER ) {
			x -= DC->textWidth( text, item->textscale, item->iMenuFont ) * 0.5f;
		}
		DC->drawText( r->x + x, r->y + item->textaligny, item->textscale, color, text, 0, 0, item->textStyle, item->iMenuFont );
		return;
	}
	part = ( max > 0.0f ) ? Com_Clamp( 0.0f, 1.0f, value / max ) : 0.0f;
	if ( Skin_Ready() ) {
		if ( item->alignment == ITEM_ALIGN_RIGHT ) {
			const float w = ( part > 0.0f ) ? Q_max( r->h, r->w * part ) : 0.0f;

			Skin_Bar( r->x, r->y, r->w, r->h, 0.0f, color, item->window.backColor );
			Skin_Bar( r->x + r->w - w, r->y, w, r->h, ( w > 0.0f ) ? 1.0f : 0.0f, color, NULL );
		} else {
			Skin_Bar( r->x, r->y, r->w, r->h, part, color, item->window.backColor );
		}
	} else {
		DC->fillRect( r->x, r->y, r->w, r->h, item->window.backColor );
		DC->fillRect( ( item->alignment == ITEM_ALIGN_RIGHT ) ? r->x + r->w * ( 1.0f - part ) : r->x, r->y, r->w * part, r->h, color );
	}
}

void Item_Paint(itemDef_t *item)
{
	vec4_t		red;
	menuDef_t *parent;
	int			xPos,textWidth;
	vec4_t		color = {1, 1, 1, 1};

	red[0] = red[3] = 1;
	red[1] = red[2] = 0;

	if (item == NULL)
	{
		return;
	}

	parent = (menuDef_t*)item->parent;

	if (item->window.flags & WINDOW_ORBITING)
	{
		if (DC->realTime > item->window.nextTime)
		{
			float rx, ry, a, c, s, w, h;

			item->window.nextTime = DC->realTime + item->window.offsetTime;
			// translate
			w = item->window.rectClient.w / 2;
			h = item->window.rectClient.h / 2;
			rx = item->window.rectClient.x + w - item->window.rectEffects.x;
			ry = item->window.rectClient.y + h - item->window.rectEffects.y;
			a = 3 * M_PI / 180;
			c = cos(a);
			s = sin(a);
			item->window.rectClient.x = (rx * c - ry * s) + item->window.rectEffects.x - w;
			item->window.rectClient.y = (rx * s + ry * c) + item->window.rectEffects.y - h;
			Item_UpdatePosition(item);
		}
	}


	if (item->window.flags & WINDOW_INTRANSITION)
	{
		if (DC->realTime > item->window.nextTime)
		{
			int done = 0;
			item->window.nextTime = DC->realTime + item->window.offsetTime;

			// transition the x,y
			if (item->window.rectClient.x == item->window.rectEffects.x)
			{
				done++;
			}
			else
			{
				if (item->window.rectClient.x < item->window.rectEffects.x)
				{
					item->window.rectClient.x += item->window.rectEffects2.x;
					if (item->window.rectClient.x > item->window.rectEffects.x)
					{
						item->window.rectClient.x = item->window.rectEffects.x;
						done++;
					}
				}
				else
				{
					item->window.rectClient.x -= item->window.rectEffects2.x;
					if (item->window.rectClient.x < item->window.rectEffects.x)
					{
						item->window.rectClient.x = item->window.rectEffects.x;
						done++;
					}
				}
			}

			if (item->window.rectClient.y == item->window.rectEffects.y)
			{
				done++;
			}
			else
			{
				if (item->window.rectClient.y < item->window.rectEffects.y)
				{
					item->window.rectClient.y += item->window.rectEffects2.y;
					if (item->window.rectClient.y > item->window.rectEffects.y)
					{
						item->window.rectClient.y = item->window.rectEffects.y;
						done++;
					}
				}
				else
				{
					item->window.rectClient.y -= item->window.rectEffects2.y;
					if (item->window.rectClient.y < item->window.rectEffects.y)
					{
						item->window.rectClient.y = item->window.rectEffects.y;
						done++;
					}
				}
			}

			if (item->window.rectClient.w == item->window.rectEffects.w)
			{
				done++;
			}
			else
			{
				if (item->window.rectClient.w < item->window.rectEffects.w)
				{
					item->window.rectClient.w += item->window.rectEffects2.w;
					if (item->window.rectClient.w > item->window.rectEffects.w)
					{
						item->window.rectClient.w = item->window.rectEffects.w;
						done++;
					}
				}
				else
				{
					item->window.rectClient.w -= item->window.rectEffects2.w;
					if (item->window.rectClient.w < item->window.rectEffects.w)
					{
						item->window.rectClient.w = item->window.rectEffects.w;
						done++;
					}
				}
			}

			if (item->window.rectClient.h == item->window.rectEffects.h)
			{
				done++;
			}
			else
			{
				if (item->window.rectClient.h < item->window.rectEffects.h)
				{
					item->window.rectClient.h += item->window.rectEffects2.h;
					if (item->window.rectClient.h > item->window.rectEffects.h)
					{
						item->window.rectClient.h = item->window.rectEffects.h;
						done++;
					}
				}
				else
				{
					item->window.rectClient.h -= item->window.rectEffects2.h;
					if (item->window.rectClient.h < item->window.rectEffects.h)
					{
						item->window.rectClient.h = item->window.rectEffects.h;
						done++;
					}
				}
			}

			Item_UpdatePosition(item);

			if (done == 4)
			{
				item->window.flags &= ~WINDOW_INTRANSITION;
			}

		}
	}

#ifdef _TRANS3

//JLF begin model transition stuff
	if (item->window.flags & WINDOW_INTRANSITIONMODEL)
	{
		if ( item->type == ITEM_TYPE_MODEL)
		{
//fields ing modelptr
//				vec3_t g2mins2, g2maxs2, g2minsEffect, g2maxsEffect;
//	float fov_x2, fov_y2, fov_Effectx, fov_Effecty;

			modelDef_t  * modelptr = item->typeData.model;

			if (DC->realTime > item->window.nextTime)
			{
				int done = 0;
				item->window.nextTime = DC->realTime + item->window.offsetTime;


// transition the x,y,z max
				if (modelptr->g2maxs[0] == modelptr->g2maxs2[0])
				{
					done++;
				}
				else
				{
					if (modelptr->g2maxs[0] < modelptr->g2maxs2[0])
					{
						modelptr->g2maxs[0] += modelptr->g2maxsEffect[0];
						if (modelptr->g2maxs[0] > modelptr->g2maxs2[0])
						{
							modelptr->g2maxs[0] = modelptr->g2maxs2[0];
							done++;
						}
					}
					else
					{
						modelptr->g2maxs[0] -= modelptr->g2maxsEffect[0];
						if (modelptr->g2maxs[0] < modelptr->g2maxs2[0])
						{
							modelptr->g2maxs[0] = modelptr->g2maxs2[0];
							done++;
						}
					}
				}
//y
				if (modelptr->g2maxs[1] == modelptr->g2maxs2[1])
				{
					done++;
				}
				else
				{
					if (modelptr->g2maxs[1] < modelptr->g2maxs2[1])
					{
						modelptr->g2maxs[1] += modelptr->g2maxsEffect[1];
						if (modelptr->g2maxs[1] > modelptr->g2maxs2[1])
						{
							modelptr->g2maxs[1] = modelptr->g2maxs2[1];
							done++;
						}
					}
					else
					{
						modelptr->g2maxs[1] -= modelptr->g2maxsEffect[1];
						if (modelptr->g2maxs[1] < modelptr->g2maxs2[1])
						{
							modelptr->g2maxs[1] = modelptr->g2maxs2[1];
							done++;
						}
					}
				}


//z

				if (modelptr->g2maxs[2] == modelptr->g2maxs2[2])
				{
					done++;
				}
				else
				{
					if (modelptr->g2maxs[2] < modelptr->g2maxs2[2])
					{
						modelptr->g2maxs[2] += modelptr->g2maxsEffect[2];
						if (modelptr->g2maxs[2] > modelptr->g2maxs2[2])
						{
							modelptr->g2maxs[2] = modelptr->g2maxs2[2];
							done++;
						}
					}
					else
					{
						modelptr->g2maxs[2] -= modelptr->g2maxsEffect[2];
						if (modelptr->g2maxs[2] < modelptr->g2maxs2[2])
						{
							modelptr->g2maxs[2] = modelptr->g2maxs2[2];
							done++;
						}
					}
				}

// transition the x,y,z min
				if (modelptr->g2mins[0] == modelptr->g2mins2[0])
				{
					done++;
				}
				else
				{
					if (modelptr->g2mins[0] < modelptr->g2mins2[0])
					{
						modelptr->g2mins[0] += modelptr->g2minsEffect[0];
						if (modelptr->g2mins[0] > modelptr->g2mins2[0])
						{
							modelptr->g2mins[0] = modelptr->g2mins2[0];
							done++;
						}
					}
					else
					{
						modelptr->g2mins[0] -= modelptr->g2minsEffect[0];
						if (modelptr->g2mins[0] < modelptr->g2mins2[0])
						{
							modelptr->g2mins[0] = modelptr->g2mins2[0];
							done++;
						}
					}
				}
//y
				if (modelptr->g2mins[1] == modelptr->g2mins2[1])
				{
					done++;
				}
				else
				{
					if (modelptr->g2mins[1] < modelptr->g2mins2[1])
					{
						modelptr->g2mins[1] += modelptr->g2minsEffect[1];
						if (modelptr->g2mins[1] > modelptr->g2mins2[1])
						{
							modelptr->g2mins[1] = modelptr->g2mins2[1];
							done++;
						}
					}
					else
					{
						modelptr->g2mins[1] -= modelptr->g2minsEffect[1];
						if (modelptr->g2mins[1] < modelptr->g2mins2[1])
						{
							modelptr->g2mins[1] = modelptr->g2mins2[1];
							done++;
						}
					}
				}


//z

				if (modelptr->g2mins[2] == modelptr->g2mins2[2])
				{
					done++;
				}
				else
				{
					if (modelptr->g2mins[2] < modelptr->g2mins2[2])
					{
						modelptr->g2mins[2] += modelptr->g2minsEffect[2];
						if (modelptr->g2mins[2] > modelptr->g2mins2[2])
						{
							modelptr->g2mins[2] = modelptr->g2mins2[2];
							done++;
						}
					}
					else
					{
						modelptr->g2mins[2] -= modelptr->g2minsEffect[2];
						if (modelptr->g2mins[2] < modelptr->g2mins2[2])
						{
							modelptr->g2mins[2] = modelptr->g2mins2[2];
							done++;
						}
					}
				}



//fovx
				if (modelptr->fov_x == modelptr->fov_x2)
				{
					done++;
				}
				else
				{
					if (modelptr->fov_x < modelptr->fov_x2)
					{
						modelptr->fov_x += modelptr->fov_Effectx;
						if (modelptr->fov_x > modelptr->fov_x2)
						{
							modelptr->fov_x = modelptr->fov_x2;
							done++;
						}
					}
					else
					{
						modelptr->fov_x -= modelptr->fov_Effectx;
						if (modelptr->fov_x < modelptr->fov_x2)
						{
							modelptr->fov_x = modelptr->fov_x2;
							done++;
						}
					}
				}

//fovy
				if (modelptr->fov_y == modelptr->fov_y2)
				{
					done++;
				}
				else
				{
					if (modelptr->fov_y < modelptr->fov_y2)
					{
						modelptr->fov_y += modelptr->fov_Effecty;
						if (modelptr->fov_y > modelptr->fov_y2)
						{
							modelptr->fov_y = modelptr->fov_y2;
							done++;
						}
					}
					else
					{
						modelptr->fov_y -= modelptr->fov_Effecty;
						if (modelptr->fov_y < modelptr->fov_y2)
						{
							modelptr->fov_y = modelptr->fov_y2;
							done++;
						}
					}
				}

				if (done == 5)
				{
					item->window.flags &= ~WINDOW_INTRANSITIONMODEL;
				}

			}
		}
	}
#endif
//JLF end transition stuff for models



	if (item->window.ownerDrawFlags && DC->ownerDrawVisible) {
		if (!DC->ownerDrawVisible(item->window.ownerDrawFlags)) {
			item->window.flags &= ~WINDOW_VISIBLE;
		} else {
			item->window.flags |= WINDOW_VISIBLE;
		}
	}

	if (item->disabled && item->disabledHidden)
	{
		return;
	}

	if (item->cvarFlags & (CVAR_SHOW | CVAR_HIDE)) {
		if (!Item_EnableShowViaCvar(item, CVAR_SHOW)) {
			return;
		}
	}

  if (item->window.flags & WINDOW_TIMEDVISIBLE) {

	}

	if (!(item->window.flags & WINDOW_VISIBLE))
	{
		return;
	}

	// on a page that is not open, or a row of the list outside of its window
	if ( Item_Away( item ) ) {
		return;
	}

	// "showStat": only while that number of the game is above 0
	if ( item->showStat ) {
		float value, max;

		if ( !DC->statValue || !DC->statValue( item->showStat, &value, &max ) || value <= 0.0f ) {
			return;
		}
	}

	// (a menu with an ITEM_TYPE_DESC says there what the item in focus is for)
	if ((item->window.flags & WINDOW_MOUSEOVER) && !parent->descPane)
	{
		if (item->descText && !Display_KeyBindPending())
		{
			// Make DOUBLY sure that this item should have desctext.
			// NOTE : we can't just check the mouse position on this, what if we TABBED
			// to the current menu item -- in that case our mouse isn't over the item.
			// Removing the WINDOW_MOUSEOVER flag just prevents the item's OnExit script from running
	//	    if (!Rect_ContainsPoint(&item->window.rect, DC->cursorx, DC->cursory))
	//		{	// It isn't something that should, because it isn't live anymore.
	//			item->window.flags &= ~WINDOW_MOUSEOVER;
	//		}
	//		else
			{	// Draw the desctext
				const char *textPtr = item->descText;
				char temp[MAX_STRING_CHARS] = {0};
				if (*textPtr == '@')	// string reference
				{
					trap->SE_GetStringTextString( &textPtr[1], temp, sizeof(temp));
					textPtr = temp;
				}

				Item_TextColor(item, &color);

				{// stupid C language
					float fDescScale = parent->descScale ? parent->descScale : 1;
					float fDescScaleCopy = fDescScale;
					int iYadj = 0;
					// in a menu of the new look the explanation is set in its font
					const int descFont = Item_Skinned( item ) ? SKIN_FONT_SANS : FONT_SMALL2;

					if ( Item_Skinned( item ) ) {
						float inkX, inkY, capMid;

						VectorCopy4( parent->descColor, color );
						// placed by its letters, as the texts of the items, and set smaller where it would reach an
						// item on its line (the buttons of the bar at the bottom), the middle of its capitals kept
						if ( Text_InkAt( textPtr, fDescScale, descFont, parent->descAlignment, parent->descX, parent->descY, 0.0f, &inkX, &inkY ) ) {
							capMid = parent->descMiddle ? parent->descY : 2.0f * parent->descY - inkY;
							fDescScale = Text_FitScale( textPtr, fDescScale, descFont,
								Menu_DescRoom( parent, parent->descX, capMid, parent->descAlignment ) );
							if ( Text_InkAt( textPtr, fDescScale, descFont, parent->descAlignment, parent->descX, capMid, 0.0f, &inkX, &inkY ) ) {
								DC->drawText( inkX, inkY, fDescScale, parent->descColor, textPtr, 0, 0, item->textStyle, descFont );
								fDescScale = 0.0f;
							}
						}
					}
					while ( fDescScale > 0.0f )
					{
						textWidth = DC->textWidth(textPtr,fDescScale, descFont);

						if (parent->descAlignment == ITEM_ALIGN_RIGHT)
						{
							xPos = parent->descX - textWidth;	// Right justify
						}
						else if (parent->descAlignment == ITEM_ALIGN_CENTER)
						{
							xPos = parent->descX - (textWidth/2);	// Center justify
						}
						else										// Left justify
						{
							xPos = parent->descX;
						}

						if (parent->descAlignment == ITEM_ALIGN_CENTER)
						{
							// only this one will auto-shrink the scale until we eventually fit...
							//
							if (xPos + textWidth > (SCREEN_WIDTH-4)) {
								fDescScale -= 0.001f;
								continue;
							}
						}

						// Try to adjust it's y placement if the scale has changed...
						//
						if (fDescScale != fDescScaleCopy)
						{
							int iOriginalTextHeight = DC->textHeight(textPtr, fDescScaleCopy, FONT_MEDIUM);
							iYadj = iOriginalTextHeight - DC->textHeight(textPtr, fDescScale, FONT_MEDIUM);
						}

						DC->drawText(xPos, parent->descY + iYadj, fDescScale, parent->descColor, textPtr, 0, 0, item->textStyle, descFont);
						break;
					}
				}
			}
		}
	}

	// paint the rect first..
	Window_Paint(&item->window, parent->fadeAmount , parent->fadeClamp, parent->fadeCycle);

	// a setting that is a row of a list draws all of itself
	if ( Item_IsRow( item ) ) {
		Item_Row_Paint( item );
		DC->setColor( NULL );
		return;
	}

	// the mark of an item that has the focus, in a menu with the new look. A list marks its rows itself, and
	// the button of a page has a mark of its own.
	if ( Item_Skinned( item ) && item->flow == 1 ) {
		Item_Flow_Paint( item );
	}
	if ( Item_Skinned( item ) && ( item->tab || item->flow >= 2 ) ) {
		Item_Tab_Paint( item );
	} else if ( Item_Skinned( item ) && item->type != ITEM_TYPE_LISTBOX && item->type != ITEM_TYPE_TEXTSCROLL && item->type != ITEM_TYPE_MODEL
		&& ( item->type != ITEM_TYPE_OWNERDRAW || item->action ) && item->type != ITEM_TYPE_TEXT && item->type != ITEM_TYPE_DESC
		&& item->type != ITEM_TYPE_PAGESCROLL && item->type != ITEM_TYPE_CHANGES ) {
		if ( item->fold && item->scroll && parent->canvasW > 0.0f ) {
			// the heading of a card (Menu_ListCards): the head of the card from its top to its line, inside its rim
			const float u = parent->canvasUnit;

			Skin_CardHeadBack( item, item->window.rect.x + u, item->window.rect.y + item->window.rect.h - ( CARD_HEAD_PX - 1.0f ) * u,
				item->window.rect.w - 2.0f * u, ( CARD_LINE_PX - 1.0f ) * u, 9.0f * u );
		} else {
			Skin_ItemBack( item );
		}
	}

	// Draw box to show rectangle extents, in debug mode
	if (debugMode)
	{
		vec4_t color;
		color[1] = color[3] = 1;
		color[0] = color[2] = 0;
		DC->drawRect(
			item->window.rect.x,
			item->window.rect.y,
			item->window.rect.w,
			item->window.rect.h,
			1,
			color);
	}

	//DC->drawRect(item->window.rect.x, item->window.rect.y, item->window.rect.w, item->window.rect.h, 1, red);

	switch (item->type) {
	case ITEM_TYPE_OWNERDRAW:
		Item_OwnerDraw_Paint(item);
		break;
	case ITEM_TYPE_BAR:
	case ITEM_TYPE_VALUE:
		Item_Stat_Paint(item);
		break;
	case ITEM_TYPE_CUBE:
		Skin_Cube( item->window.rect.x, item->window.rect.y, item->window.rect.w, item->window.rect.h );
		break;
	case ITEM_TYPE_LOGO:
		Skin_Logo( item->window.rect.x, item->window.rect.y, item->window.rect.w, item->window.rect.h, item->window.foreColor[3] );
		break;
	case ITEM_TYPE_DESC:
		Item_Desc_Paint( item );
		break;
	case ITEM_TYPE_CHANGES:
		Item_Changes_Paint( item );
		break;
	case ITEM_TYPE_PAGESCROLL:
		Item_PageScroll_Paint( item );
		break;
	case ITEM_TYPE_TEXT:
	case ITEM_TYPE_BUTTON:
		Item_Text_Paint(item);
		if ( item->fold ) {
			Item_Fold_Paint( item );
		}
		break;
	case ITEM_TYPE_RADIOBUTTON:
		break;
	case ITEM_TYPE_CHECKBOX:
		break;
	case ITEM_TYPE_EDITFIELD:
	case ITEM_TYPE_NUMERICFIELD:
		Item_TextField_Paint(item);
		break;
	case ITEM_TYPE_COMBO:
		break;
	case ITEM_TYPE_LISTBOX:
		Item_ListBox_Paint(item);
		break;
	case ITEM_TYPE_TEXTSCROLL:
		Item_TextScroll_Paint ( item );
		break;
		//case ITEM_TYPE_IMAGE:
		//  Item_Image_Paint(item);
		//  break;
	case ITEM_TYPE_MODEL:
		Item_Model_Paint(item);
		break;
	case ITEM_TYPE_YESNO:
		Item_YesNo_Paint(item);
		break;
	case ITEM_TYPE_MULTI:
		Item_Multi_Paint(item);
		break;
	case ITEM_TYPE_BIND:
		Item_Bind_Paint(item);
		break;
	case ITEM_TYPE_SLIDER:
		Item_Slider_Paint(item);
		break;
	default:
		break;
	}

	//FIXME: this might be bad
	DC->setColor( NULL );
}

void Menu_Init(menuDef_t *menu) {
	memset(menu, 0, sizeof(menuDef_t));
	menu->cursorItem = -1;
	menu->fadeAmount = DC->Assets.fadeAmount;
	menu->fadeClamp = DC->Assets.fadeClamp;
	menu->fadeCycle = DC->Assets.fadeCycle;
	Window_Init(&menu->window);
}

itemDef_t *Menu_GetFocusedItem(menuDef_t *menu) {
	int i;
	if (menu) {
		for (i = 0; i < menu->itemCount; i++) {
			if (menu->items[i]->window.flags & WINDOW_HASFOCUS) {
				return menu->items[i];
			}
		}
	}
	return NULL;
}

menuDef_t *Menu_GetFocused() {
	int i;
	for (i = 0; i < menuCount; i++) {
		if (Menus[i].window.flags & WINDOW_HASFOCUS && Menus[i].window.flags & WINDOW_VISIBLE) {
			return &Menus[i];
		}
	}
	return NULL;
}

void Menu_ScrollFeeder(menuDef_t *menu, int feeder, qboolean down) {
	if (menu) {
		int i;
		for (i = 0; i < menu->itemCount; i++) {
			if (menu->items[i]->special == feeder) {
				Item_ListBox_HandleKey(menu->items[i], (down) ? A_CURSOR_DOWN : A_CURSOR_UP, qtrue, qtrue);
				return;
			}
		}
	}
}



void Menu_SetFeederSelection(menuDef_t *menu, int feeder, int index, const char *name) {
	if (menu == NULL) {
		if (name == NULL) {
			menu = Menu_GetFocused();
		} else {
			menu = Menus_FindByName(name);
		}
	}

	if (menu) {
		int i;
		for (i = 0; i < menu->itemCount; i++) {
			if (menu->items[i]->special == feeder) {
				if (index == 0) {
					listBoxDef_t *listPtr = menu->items[i]->typeData.listbox;
					listPtr->cursorPos = 0;
					listPtr->startPos = 0;
				}
				menu->items[i]->cursorPos = index;
				DC->feederSelection(menu->items[i]->special, menu->items[i]->cursorPos, NULL);
				return;
			}
		}
	}
}

qboolean Menus_AnyFullScreenVisible( void ) {
  int i;
  for (i = 0; i < menuCount; i++) {
    if (Menus[i].window.flags & WINDOW_VISIBLE && Menus[i].fullScreen) {
			return qtrue;
    }
  }
  return qfalse;
}

menuDef_t *Menus_ActivateByName(const char *p) {
  int i;
  menuDef_t *m = NULL;
	menuDef_t *focus = Menu_GetFocused();
  for (i = 0; i < menuCount; i++) {
    if (Q_stricmp(Menus[i].window.name, p) == 0) {
	    m = &Menus[i];
			Menus_Activate(m);
			if (openMenuCount < MAX_OPEN_MENUS && focus != NULL) {
				menuStack[openMenuCount++] = focus;
			}
    } else {
      Menus[i].window.flags &= ~WINDOW_HASFOCUS;
    }
  }
	Display_CloseCinematics();

	// Want to handle a mouse move on the new menu in case your already over an item
	Menu_HandleMouseMove ( m, DC->cursorx, DC->cursory );

	return m;
}


void Item_Init(itemDef_t *item) {
	memset(item, 0, sizeof(itemDef_t));
	item->textscale = 0.55f;
	Window_Init(&item->window);
}

void Menu_HandleMouseMove(menuDef_t *menu, float x, float y) {
  int i, pass;
  qboolean focusSet = qfalse;

  itemDef_t *overItem;
  if (menu == NULL) {
    return;
  }

  if (!(menu->window.flags & (WINDOW_VISIBLE | WINDOW_FORCED))) {
    return;
  }

	if (itemCapture) {
		//Item_MouseMove(itemCapture, x, y);
		return;
	}

	if (g_waitingForKey || g_editingField) {
		return;
	}

  // FIXME: this is the whole issue of focus vs. mouse over..
  // need a better overall solution as i don't like going through everything twice
  for (pass = 0; pass < 2; pass++) {
    for (i = 0; i < menu->itemCount; i++) {
      // turn off focus each item
      // menu->items[i].window.flags &= ~WINDOW_HASFOCUS;

      if (!(menu->items[i]->window.flags & (WINDOW_VISIBLE | WINDOW_FORCED))) {
        continue;
      }

			// on a page that is not open, or a row of the list outside of its window
			if ( Item_Away( menu->items[i] ) ) {
				if ( menu->items[i]->window.flags & WINDOW_MOUSEOVER ) {
					Item_MouseLeave( menu->items[i] );
					Item_SetMouseOver( menu->items[i], qfalse );
				}
				continue;
			}

			if (menu->items[i]->disabled)
			{
				continue;
			}

			// items can be enabled and disabled based on cvars
			if (menu->items[i]->cvarFlags & (CVAR_ENABLE | CVAR_DISABLE) && !Item_EnableShowViaCvar(menu->items[i], CVAR_ENABLE)) {
				continue;
			}

			if (menu->items[i]->cvarFlags & (CVAR_SHOW | CVAR_HIDE) && !Item_EnableShowViaCvar(menu->items[i], CVAR_SHOW)) {
				continue;
			}



      if (Rect_ContainsPoint(&menu->items[i]->window.rect, x, y)) {
				if (pass == 1) {
					overItem = menu->items[i];
					if (overItem->type == ITEM_TYPE_TEXT && overItem->text) {
						if (!Rect_ContainsPoint(&overItem->window.rect, x, y)) {
							continue;
						}
					}
					// if we are over an item
					if (IsVisible(overItem->window.flags)) {
						// different one
						Item_MouseEnter(overItem, x, y);
						// Item_SetMouseOver(overItem, qtrue);

						// if item is not a decoration see if it can take focus
						if (!focusSet) {
							focusSet = Item_SetFocus(overItem, x, y);
						}
					}
				}
      } else if (menu->items[i]->window.flags & WINDOW_MOUSEOVER) {
          Item_MouseLeave(menu->items[i]);
          Item_SetMouseOver(menu->items[i], qfalse);
      }
    }
  }

}

/*
=================
Menu_ListCards

In a menu with a canvas every heading of a list ("fold <n>") is the top of a card that holds its rows: a panel
from the heading down to where its last row that is shown ends (cardBottom, Menu_ListLayout), as wide as the
heading, cut to the window of the list. (The Setup of the redesign: the parts of a page as cards in two columns.)
=================
*/
static void Menu_ListCards( menuDef_t *menu ) {
	static const vec4_t	fill = { 0.031f, 0.055f, 0.11f, 0.78f }, edge = { 0.11f, 0.18f, 0.29f, 1.0f };
	rectDef_t			list;
	int					i;

	if ( menu->canvasW <= 0.0f || !Menu_HasList( menu ) || !Skin_Ready() ) {
		return;
	}
	Menu_ListWindow( menu, &list );
	for ( i = 0; i < menu->itemCount; i++ ) {
		const itemDef_t	*item = menu->items[i];
		float			top, bottom;

		if ( !item->scroll || !item->fold || item->listOut || item->cardBottom <= item->window.rectClient.y
			|| Item_OffPage( item ) || !( item->window.flags & WINDOW_VISIBLE ) ) {
			continue;
		}
		// (the heading of a card that is not the first of its column has the gap to the one above in it: the card
		// begins CARD_HEAD_PX above the end of the heading, its line CARD_LINE_PX under its top)
		top = item->window.rect.y + item->window.rect.h - CARD_HEAD_PX * menu->canvasUnit;
		bottom = item->window.rect.y + ( item->cardBottom - item->window.rectClient.y ) + CARD_BOTTOM_PX * menu->canvasUnit;
		{
			const float	head = top + CARD_LINE_PX * menu->canvasUnit;
			const float	y0 = Q_max( top, list.y ), y1 = Q_min( bottom, list.y + list.h );

			if ( y1 > y0 ) {
				Skin_Panel( item->window.rect.x, y0, item->window.rect.w, y1 - y0, 10.0f * menu->canvasUnit, fill, edge );
			}
			if ( head >= list.y && head <= list.y + list.h ) {
				Skin_Line( item->window.rect.x, head, item->window.rect.w, edge );
			}
		}
	}
}

/*
=================
Menus_Dump

For tools that test the menus (the jka MCP server, "v_uiDump [file]"): the screen, then each open menu and every item of
it that can be seen right now, with its rectangle in pixels of the window, one line each, so a tool can measure a
layout and point the mouse at an item by its name. Lines begin with "uidump"; the last one is "uidump end".
=================
*/
static fileHandle_t dumpFile;

// can this item be seen right now (what the dump lists, and what v_uiSweep points at)
qboolean Item_Dumped( const itemDef_t *item ) {
	return (qboolean)!( !( item->window.flags & WINDOW_VISIBLE ) || Item_Away( item ) || item->listOut
		|| ( item->cvarFlags & ( CVAR_SHOW | CVAR_HIDE ) && !Item_EnableShowViaCvar( (itemDef_t *)item, CVAR_SHOW ) ) );
}

// a line of the dump: printed, or written to the file "v_uiDump <file>" named
static void QDECL Dump_Line( const char *fmt, ... ) {
	va_list	ap;
	char	line[1024];

	va_start( ap, fmt );
	Q_vsnprintf( line, sizeof( line ), fmt, ap );
	va_end( ap );
	if ( dumpFile ) {
		trap->FS_Write( line, (int)strlen( line ), dumpFile );
	} else {
		Com_Printf( "%s", line );
	}
}

void Menus_Dump( void ) {
	const float	bias = DC->bias, biasY = DC->biasY;
	int			i, j;

	// (a file to write to instead of the console: a tool reads a long dump from it without the console in between)
	dumpFile = 0;
	if ( trap->Cmd_Argc() > 1 ) {
		char name[MAX_QPATH];

		trap->Cmd_Argv( 1, name, sizeof( name ) );
		if ( trap->FS_Open( name, &dumpFile, FS_WRITE ) < 0 ) {
			dumpFile = 0;
		}
	}

	Dump_Line( "uidump screen %d %d %.4f %.4f %.3f %.3f %d %d\n", DC->glconfig.vidWidth, DC->glconfig.vidHeight, DC->xscale,
		DC->yscale, bias, biasY, DC->cursorx, DC->cursory );
	for ( i = 0; i < menuCount; i++ ) {
		const menuDef_t *menu = &Menus[i];

		if ( !( menu->window.flags & WINDOW_VISIBLE ) ) {
			continue;
		}
		Dump_Line( "uidump menu %s focus=%d page=%d canvas=%gx%g px=%.0f,%.0f,%.0f,%.0f desc=%.0f,%.0f,%d\n", menu->window.name ? menu->window.name : "?",
			( menu->window.flags & WINDOW_HASFOCUS ) ? 1 : 0, menu->page, menu->canvasW, menu->canvasH,
			( menu->window.rect.x + bias ) * DC->xscale, ( menu->window.rect.y + biasY ) * DC->yscale,
			menu->window.rect.w * DC->xscale, menu->window.rect.h * DC->yscale,
			( menu->descX + bias ) * DC->xscale, ( menu->descY + biasY ) * DC->yscale, menu->descAlignment );
		for ( j = 0; j < menu->itemCount; j++ ) {
			const itemDef_t	*item = menu->items[j];
			const rectDef_t	*r = &item->window.rect;
			char			text[128], value[64];

			if ( !Item_Dumped( item ) ) {
				continue;
			}
			text[0] = value[0] = '\0';
			if ( item->text && item->text[0] ) {
				Text_Word( item->text, text, sizeof( text ), qfalse, qfalse );
			}
			if ( item->cvar ) {
				DC->getCVarString( item->cvar, value, sizeof( value ) );
			}
			Dump_Line( "uidump item %s %d %s type=%d px=%.0f,%.0f,%.0f,%.0f flags=%s%s%s%s cvar=%s value=%s text=%s\n",
				menu->window.name ? menu->window.name : "?", j, item->window.name ? item->window.name : "-", item->type,
				( r->x + bias ) * DC->xscale, ( r->y + biasY ) * DC->yscale, r->w * DC->xscale, r->h * DC->yscale,
				( item->window.flags & WINDOW_HASFOCUS ) ? "F" : "", ( item->window.flags & WINDOW_MOUSEOVER ) ? "M" : "",
				( item->window.flags & WINDOW_DECORATION ) ? "D" : "", item->disabled ? "X" : "",
				item->cvar ? item->cvar : "-", value, text );
			{
				// what a check of the layout needs (tools/test, the audit of the menus): the style, the corners, the
				// box the text of the item was drawn in, the widget of a row, the list it is in. A line of its own:
				// the text of the item line goes to its end.
				const rectDef_t	*t = &item->textRect;
				rectDef_t		widget = { 0, 0, 0, 0 };

				if ( Item_IsRow( item ) ) {
					Item_Row_Widget( item, &widget );
				}
				Dump_Line( "uidump geom %d style=%d radius=%.1f border=%d text=%.0f,%.0f,%.0f,%.0f align=%d widget=%.0f,%.0f,%.0f,%.0f row=%d page=%d scroll=%d fold=%d column=%d stack=%d card=%.0f\n",
					j, item->window.style, item->window.radius * DC->yscale, item->window.border,
					( t->x + bias ) * DC->xscale, ( t->y + biasY ) * DC->yscale, t->w * DC->xscale, t->h * DC->yscale, item->textalignment,
					widget.w > 0.0f ? ( widget.x + bias ) * DC->xscale : 0.0f, widget.w > 0.0f ? ( widget.y + biasY ) * DC->yscale : 0.0f,
					widget.w * DC->xscale, widget.h * DC->yscale, item->row, item->page, item->scroll, item->fold, item->column, item->stack,
					item->fold ? ( item->window.rect.y + ( item->cardBottom - item->window.rectClient.y ) + biasY ) * DC->yscale : 0.0f );
			}
		}
	}
	Dump_Line( "uidump end\n" );
	if ( dumpFile ) {
		trap->FS_Close( dumpFile );
		dumpFile = 0;
	}
}

/*
=================
Menu_StackFrame

The items of a menu with the same "stack <n>" are a column of places from the top down, as the menu file put
them; the items at one height are one place (the rows of the game types that share it). A place whose items are
all hidden right now by their cvar tests is left out and what is under it moves up by the room it had, so the
rules of Create a Game and the like have no gaps whatever game type is chosen. Once a frame: the cvars can change.
=================
*/
static void Menu_StackFrame( menuDef_t *menu ) {
	static int	order[MAX_MENUITEMS];
	qboolean	done[MAX_MENUITEMS];
	float		baseX, baseY;
	int			i, j, k, n;

	baseX = menu->window.rect.x;
	baseY = menu->window.rect.y;
	if ( menu->window.border != 0 ) {
		baseX += menu->window.borderSize;
		baseY += menu->window.borderSize;
	}
	memset( done, 0, sizeof( done ) );
	for ( i = 0; i < menu->itemCount; i++ ) {
		float shift = 0.0f;

		if ( done[i] || menu->items[i]->stack <= 0 ) {
			continue;
		}
		// the items of this stack, from the top down
		n = 0;
		for ( j = i; j < menu->itemCount; j++ ) {
			if ( menu->items[j]->stack == menu->items[i]->stack ) {
				done[j] = qtrue;
				for ( k = n++; k > 0 && menu->items[order[k - 1]]->window.rectClient.y > menu->items[j]->window.rectClient.y; k-- ) {
					order[k] = order[k - 1];
				}
				order[k] = j;
			}
		}
		for ( j = 0; j < n; ) {
			const float	y = menu->items[order[j]]->window.rectClient.y;
			qboolean	shown = qfalse;
			int			end;

			for ( end = j; end < n && fabsf( menu->items[order[end]]->window.rectClient.y - y ) < 0.5f; end++ ) {
				const itemDef_t *item = menu->items[order[end]];

				if ( ( item->window.flags & WINDOW_VISIBLE )
					&& !( ( item->cvarFlags & ( CVAR_SHOW | CVAR_HIDE ) ) && !Item_EnableShowViaCvar( (itemDef_t *)item, CVAR_SHOW ) ) ) {
					shown = qtrue;
				}
			}
			for ( k = j; k < end; k++ ) {
				itemDef_t	*item = menu->items[order[k]];
				const float	border = item->window.border ? item->window.borderSize : 0.0f;
				const float	to = baseY + border + item->window.rectClient.y - shift;

				if ( item->window.rect.y != to ) {
					item->window.rect.x = baseX + border + item->window.rectClient.x;
					item->window.rect.y = to;
					// (where its words stand is worked out again, Item_SetScreenCoords)
					item->textRect.w = 0;
					item->textRect.h = 0;
				}
			}
			if ( !shown ) {
				// (the last place: the room it had, as high as its highest item)
				float room = 0.0f;

				if ( end < n ) {
					room = menu->items[order[end]]->window.rectClient.y - y;
				} else {
					for ( k = j; k < end; k++ ) {
						room = Q_max( room, menu->items[order[k]]->window.rectClient.h );
					}
				}
				shift += room;
			}
			j = end;
		}
		// "stack -<n>": the frame around the stack is shorter by what was left out
		for ( j = 0; j < menu->itemCount; j++ ) {
			itemDef_t *frame = menu->items[j];

			if ( frame->stack == -menu->items[i]->stack ) {
				frame->window.rect.h = Q_max( 0.0f, frame->window.rectClient.h - shift );
			}
		}
	}
}

void Menu_Paint(menuDef_t *menu, qboolean forcePaint) {
	int i, underBackground;
	float slide;
	qboolean cardsDone = qfalse;

	if (menu == NULL) {
		return;
	}

	if (!(menu->window.flags & WINDOW_VISIBLE) &&  !forcePaint) {
		return;
	}

	if (menu->window.ownerDrawFlags && DC->ownerDrawVisible && !DC->ownerDrawVisible(menu->window.ownerDrawFlags)) {
		return;
	}

	if (forcePaint) {
		menu->window.flags |= WINDOW_FORCED;
	}
	Menu_CanvasCheck( menu );

	// "openSlide": a menu that just opened comes up from below, fast at first and then slower. Its place is
	// moved for this one frame and put back at the end of it.
	slide = 0.0f;
	if ( menu->openSlide && menu->openTime && DC->realTime - menu->openTime < MENU_OPEN_MSEC && DC->realTime >= menu->openTime ) {
		const float left = 1.0f - (float)( DC->realTime - menu->openTime ) / MENU_OPEN_MSEC;

		slide = menu->openSlide * left * left;
		menu->window.rect.y += slide;
		Menu_UpdatePosition( menu );
	}

	// draw the background if necessary
	if ( menu->backdrop && Skin_Ready() ) {
		Skin_Backdrop();
	} else if (menu->fullScreen) {
		// implies a background shader
		// FIXME: make sure we have a default shader if fullscreen is set with no background
		DC->drawHandlePic( -DC->wide, -DC->tall, SCREEN_WIDTH + 2.0f * DC->wide, SCREEN_HEIGHT + 2.0f * DC->tall, menu->window.background );
	} else if (menu->window.background) {
		// this allows a background shader without being full screen
		//UI_DrawHandlePic(menu->window.rect.x, menu->window.rect.y, menu->window.rect.w, menu->window.rect.h, menu->backgroundShader);
	}

	// paint the background and or border
	Window_Paint(&menu->window, menu->fadeAmount, menu->fadeClamp, menu->fadeCycle );

	// the list of the open page glides to where the wheel sent it, after the search or a fold laid it out anew,
	// and the buttons of the pages are as wide as their words are in the language of this frame
	Menu_ListCheck( menu );
	Menu_ScrollFrame( menu );
	Menu_TabsLayout( menu );
	Menu_ChangesFrame( menu );
	Menu_StackFrame( menu );

	// the items that are painted before the background of the menu lie under it
	underBackground = 0;
	if ( DC->wide > 0.0f ) {
		for ( i = 0; i < menu->itemCount; i++ ) {
			if ( ( menu->items[i]->window.flags & WINDOW_VISIBLE ) && Window_IsBackground( &menu->items[i]->window ) ) {
				underBackground = i;
				break;
			}
		}
	}

	// Loop through all items for the menu and paint them
	for (i = 0; i < menu->itemCount; i++)
	{
		window_underBackground = (qboolean)( i < underBackground );
		// (the cards of the parts of a list lie under its first row and over what comes before it)
		if ( menu->items[i]->scroll && !cardsDone ) {
			Menu_ListCards( menu );
			cardsDone = qtrue;
		}
		if (!menu->items[i]->appearanceSlot)
		{
			Item_Paint(menu->items[i]);
		}
		else // Timed order of appearance
		{
			if (menu->appearanceTime < DC->realTime)	// Time to show another item
			{
				menu->appearanceTime = DC->realTime + menu->appearanceIncrement;
				menu->appearanceCnt++;
			}

			if (menu->items[i]->appearanceSlot<=menu->appearanceCnt)
			{
				Item_Paint(menu->items[i]);
			}
		}
	}
	window_underBackground = qfalse;
	Menu_SearchEmpty_Paint( menu );

	if (debugMode) {
		vec4_t color;
		color[0] = color[2] = color[3] = 1;
		color[1] = 0;
		DC->drawRect(menu->window.rect.x, menu->window.rect.y, menu->window.rect.w, menu->window.rect.h, 1, color);
	}

	if ( slide != 0.0f ) {
		menu->window.rect.y -= slide;
		Menu_UpdatePosition( menu );
	}
}

/*
===============
Item_ValidateTypeData
===============
*/
void Item_ValidateTypeData(itemDef_t *item)
{
	if (item->typeData.data)
	{
		return;
	}

	switch ( item->type )
	{
		case ITEM_TYPE_LISTBOX:
		{
			item->typeData.listbox = (listBoxDef_t *)UI_Alloc(sizeof(listBoxDef_t));
			memset(item->typeData.listbox, 0, sizeof(listBoxDef_t));
			break;
		}
		case ITEM_TYPE_TEXT:
		case ITEM_TYPE_EDITFIELD:
		case ITEM_TYPE_NUMERICFIELD:
		case ITEM_TYPE_YESNO:
		case ITEM_TYPE_BIND:
		case ITEM_TYPE_SLIDER:
		{
			item->typeData.edit = (editFieldDef_t *)UI_Alloc(sizeof(editFieldDef_t));
			memset(item->typeData.edit, 0, sizeof(editFieldDef_t));

			if ( item->type == ITEM_TYPE_EDITFIELD || item->type == ITEM_TYPE_NUMERICFIELD )
				item->typeData.edit->maxPaintChars = MAX_EDITFIELD;
			break;
		}
		case ITEM_TYPE_MULTI:
		{
			item->typeData.multi = (multiDef_t *)UI_Alloc(sizeof(multiDef_t));
			memset(item->typeData.multi, 0, sizeof(multiDef_t));
			break;
		}
		case ITEM_TYPE_MODEL:
		{
			item->typeData.model = (modelDef_t *)UI_Alloc(sizeof(modelDef_t));
			memset(item->typeData.model, 0, sizeof(modelDef_t));
			break;
		}
		case ITEM_TYPE_TEXTSCROLL:
		{
			item->typeData.textscroll = (textScrollDef_t *)UI_Alloc(sizeof(textScrollDef_t));
			memset(item->typeData.textscroll, 0, sizeof(textScrollDef_t));
			break;
		}
		default:
			break;
	}
}

/*
===============
Keyword Hash
===============
*/

#define KEYWORDHASH_SIZE	512

typedef struct keywordHash_s
{
	char *keyword;
	qboolean (*func)(itemDef_t *item, int handle);
	struct keywordHash_s *next;
} keywordHash_t;

static int KeywordHash_Key(char *keyword) {
	int register hash, i;

	hash = 0;
	for (i = 0; keyword[i] != '\0'; i++) {
		if (keyword[i] >= 'A' && keyword[i] <= 'Z')
			hash += (keyword[i] + ('a' - 'A')) * (119 + i);
		else
			hash += keyword[i] * (119 + i);
	}
	hash = (hash ^ (hash >> 10) ^ (hash >> 20)) & (KEYWORDHASH_SIZE-1);
	return hash;
}

static void KeywordHash_Add(keywordHash_t *table[], keywordHash_t *key) {
	int hash;

	hash = KeywordHash_Key(key->keyword);
/*
	if (table[hash]) {
		int collision = qtrue;
	}
*/
	key->next = table[hash];
	table[hash] = key;
}

static keywordHash_t *KeywordHash_Find(keywordHash_t *table[], char *keyword)
{
	keywordHash_t *key;
	int hash;

	hash = KeywordHash_Key(keyword);
	for (key = table[hash]; key; key = key->next) {
		if (!Q_stricmp(key->keyword, keyword))
			return key;
	}
	return NULL;
}

/*
===============
Item Keyword Parse functions
===============
*/

// name <string>
qboolean ItemParse_name( itemDef_t *item, int handle ) {
	if (!PC_String_Parse(handle, &item->window.name)) {
		return qfalse;
	}
	return qtrue;
}

// name <string>
qboolean ItemParse_focusSound( itemDef_t *item, int handle ) {
	pc_token_t token;
	if (!trap->PC_ReadToken(handle, &token)) {
		return qfalse;
	}
	item->focusSound = DC->registerSound(token.string);
	return qtrue;
}


// text <string>
qboolean ItemParse_text( itemDef_t *item, int handle ) {
	if (!PC_String_Parse(handle, &item->text)) {
		return qfalse;
	}
	return qtrue;
}

/*
===============
ItemParse_descText
	text <string>
===============
*/
qboolean ItemParse_descText( itemDef_t *item, int handle)
{

	if (!PC_String_Parse(handle, &item->descText))
	{
		return qfalse;
	}

	return qtrue;

}


/*
===============
ItemParse_text
	text <string>
===============
*/
qboolean ItemParse_text2( itemDef_t *item, int handle)
{

	if (!PC_String_Parse(handle, &item->text2))
	{
		return qfalse;
	}

	return qtrue;

}

/*
===============
ItemParse_text2alignx
===============
*/
qboolean ItemParse_text2alignx( itemDef_t *item, int handle)
{
	if (!PC_Float_Parse(handle, &item->text2alignx))
	{
		return qfalse;
	}
	return qtrue;
}

/*
===============
ItemParse_text2aligny
===============
*/
qboolean ItemParse_text2aligny( itemDef_t *item, int handle)
{
	if (!PC_Float_Parse(handle, &item->text2aligny))
	{
		return qfalse;
	}
	return qtrue;
}

// group <string>
qboolean ItemParse_group( itemDef_t *item, int handle ) {
	if (!PC_String_Parse(handle, &item->window.group)) {
		return qfalse;
	}
	return qtrue;
}

typedef struct uiG2PtrTracker_s uiG2PtrTracker_t;

struct uiG2PtrTracker_s
{
	void *ghoul2;
	uiG2PtrTracker_t *next;
};

uiG2PtrTracker_t *ui_G2PtrTracker = NULL;

//rww - UI G2 shared management functions.

//Insert the pointer into our chain so we can keep track of it for freeing.
void UI_InsertG2Pointer(void *ghoul2)
{
	uiG2PtrTracker_t **nextFree = &ui_G2PtrTracker;

	while ((*nextFree) && (*nextFree)->ghoul2)
	{ //check if it has a ghoul2, if not we can reuse it.
		nextFree = &((*nextFree)->next);
	}

	if (!nextFree)
	{ //shouldn't happen
		assert(0);
		return;
	}

	if (!(*nextFree))
	{ //if we aren't reusing a chain then allocate space for it.
		(*nextFree) = (uiG2PtrTracker_t *)BG_Alloc(sizeof(uiG2PtrTracker_t));
		(*nextFree)->next = NULL;
	}

	(*nextFree)->ghoul2 = ghoul2;
}

//Remove a ghoul2 pointer from the chain if it's there.
void UI_ClearG2Pointer(void *ghoul2)
{
	uiG2PtrTracker_t *next = ui_G2PtrTracker;

	if (!ghoul2)
	{
		return;
	}

	while (next)
	{
		if (next->ghoul2 == ghoul2)
		{ //found it, set it to null so we can reuse this link.
			next->ghoul2 = NULL;
			break;
		}

		next = next->next;
	}
}

#ifdef UI_BUILD
/*
A model item that gets another model frees its old ghoul2 instance only in the next frame of the ui. A model item
painted earlier in the same frame has handed that instance to the renderer, which draws it at the end of the frame:
freed at once, its bone cache was read after it was gone, and both renderers crashed (signal 11 in
CBoneCache::EvalRender or G2_BoneMatrices). That happened on the profile page when the list of heads came after the
figure in the menu file and chose a head while it was painted. The instance stays in the tracking list until it is
freed, so UI_CleanupGhoul2 frees it if the ui shuts down first. (VisionJK)
*/
#define UI_G2_DEFERRED	32

static void	*uiG2Deferred[UI_G2_DEFERRED];
static int	uiNumG2Deferred;

static void UI_FreeGhoul2Now( void *ghoul2 ) {
	UI_ClearG2Pointer( ghoul2 );
	if ( trap->G2_HaveWeGhoul2Models( ghoul2 ) ) {
		trap->G2API_CleanGhoul2Models( &ghoul2 );
	}
}

static void UI_FreeGhoul2Later( void *ghoul2 ) {
	if ( uiNumG2Deferred >= UI_G2_DEFERRED ) {
		// (more than a frame can draw: the oldest one is surely done with)
		UI_FreeGhoul2Now( uiG2Deferred[0] );
		memmove( uiG2Deferred, uiG2Deferred + 1, sizeof( uiG2Deferred[0] ) * ( UI_G2_DEFERRED - 1 ) );
		uiNumG2Deferred--;
	}
	uiG2Deferred[uiNumG2Deferred++] = ghoul2;
}

// once a frame, before anything of the menus is drawn: the frame before has been rendered
void UI_FreeDeferredGhoul2( void ) {
	int i;

	for ( i = 0; i < uiNumG2Deferred; i++ ) {
		UI_FreeGhoul2Now( uiG2Deferred[i] );
	}
	uiNumG2Deferred = 0;
}
#endif

//Called on shutdown, cleans up all the ghoul2 instances laying around.
void UI_CleanupGhoul2(void)
{
	uiG2PtrTracker_t *next = ui_G2PtrTracker;

	while (next)
	{
		if (next->ghoul2 && trap->G2_HaveWeGhoul2Models(next->ghoul2)) //found a g2 instance, clean it.
			trap->G2API_CleanGhoul2Models(&next->ghoul2);

		next = next->next;
	}
#ifdef UI_BUILD
	uiNumG2Deferred = 0;	// (those were in the list and are freed now)
#endif
}

// asset_model <string>
int UI_ParseAnimationFile(const char *filename, animation_t *animset, qboolean isHumanoid);

/*
===============
ItemParse_asset_model
	asset_model <string>
===============
*/
qboolean ItemParse_asset_model_go( itemDef_t *item, const char *name,int *runTimeLength )
{
#ifdef UI_BUILD
	int g2Model;
	modelDef_t *modelPtr;
	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;
	*runTimeLength =0.0f;

	if (!Q_stricmp(&name[strlen(name) - 4], ".glm"))
	{ //it's a ghoul2 model then
		if ( item->ghoul2 )
		{
			// (freed in the next frame, see UI_FreeGhoul2Later)
			UI_FreeGhoul2Later( item->ghoul2 );
			item->ghoul2 = NULL;
			item->flags &= ~ITF_G2VALID;
		}

		g2Model = trap->G2API_InitGhoul2Model(&item->ghoul2, name, 0, modelPtr->g2skin, 0, 0, 0);
		if (g2Model >= 0)
		{
			UI_InsertG2Pointer(item->ghoul2); //remember it so we can free it when the ui shuts down.
			item->flags |= ITF_G2VALID;

			if (modelPtr->g2anim)
			{ //does the menu request this model be playing an animation?
//					DC->g2hilev_SetAnim(&item->ghoul2[0], "model_root", modelPtr->g2anim);

				char GLAName[MAX_QPATH];

				GLAName[0] = 0;
				trap->G2API_GetGLAName(item->ghoul2, 0, GLAName);

				if (GLAName[0])
				{
					int animIndex;
					char *slash;

					// JA+: a player model with a skeleton of its own can not play the menu animations
					if ( name[0] && strstr( name, "models/players/" ) && !strstr( GLAName, "players/_humanoid/" ) )
					{
						return qfalse;
					}

					slash = Q_strrchr( GLAName, '/' );

					if ( slash )
					{ //If this isn't true the gla path must be messed up somehow.
						strcpy(slash, "/animation.cfg");

						animIndex = UI_ParseAnimationFile(GLAName, NULL, qfalse);
						if (animIndex != -1)
						{ //We parsed out the animation info for whatever model this is
							animation_t *anim = &bgAllAnims[animIndex].anims[modelPtr->g2anim];

							int sFrame = anim->firstFrame;
							int eFrame = anim->firstFrame + anim->numFrames;
							int flags = BONE_ANIM_OVERRIDE_FREEZE;
							int time = DC->realTime;
							float animSpeed = 50.0f / anim->frameLerp;
							int blendTime = 150;

							if (anim->loopFrames != -1)
							{
								flags |= BONE_ANIM_OVERRIDE_LOOP;
							}

							trap->G2API_SetBoneAnim(item->ghoul2, 0, "model_root", sFrame, eFrame, flags, animSpeed, time, -1, blendTime);
							*runTimeLength =((anim->frameLerp * (anim->numFrames-2)));
						}
					}
				}
			}

			if ( modelPtr->g2skin )
			{
//					DC->g2_SetSkin( &item->ghoul2[0], 0, modelPtr->g2skin );//this is going to set the surfs on/off matching the skin file
				//trap->G2API_InitGhoul2Model(&item->ghoul2, name, 0, modelPtr->g2skin, 0, 0, 0);
				//ahh, what are you doing?!
				trap->G2API_SetSkin(item->ghoul2, 0, modelPtr->g2skin, modelPtr->g2skin);
			}
		}
		/*
		else
		{
			Com_Error(ERR_FATAL, "%s does not exist.", name);
		}
		*/
	}
	else if(!(item->asset))
	{ //guess it's just an md3
		item->asset = DC->registerModel(name);
		item->flags &= ~ITF_G2VALID;
	}
#endif
	return qtrue;
}

qboolean ItemParse_asset_model( itemDef_t *item, int handle ) {
	int animRunLength;
	pc_token_t token;

	Item_ValidateTypeData(item);

	if (!trap->PC_ReadToken(handle, &token)) {
		return qfalse;
	}

#ifdef UI_BUILD
	if (!Q_stricmp(token.string,"ui_char_model") )
	{
		char modelPath[MAX_QPATH] = {0};
		char ui_char_model[MAX_QPATH] = {0};
		trap->Cvar_VariableStringBuffer("ui_char_model", ui_char_model, sizeof(ui_char_model) );
		Com_sprintf( modelPath, sizeof( modelPath ), "models/players/%s/model.glm", ui_char_model );
		return (ItemParse_asset_model_go( item, modelPath, &animRunLength ));
	}
#endif
	return (ItemParse_asset_model_go( item, token.string, &animRunLength ));
}

// asset_shader <string>
qboolean ItemParse_asset_shader( itemDef_t *item, int handle ) {
	pc_token_t token;
	if (!trap->PC_ReadToken(handle, &token)) {
		return qfalse;
	}
	item->asset = DC->registerShaderNoMip(token.string);
	return qtrue;
}

// model_origin <number> <number> <number>
qboolean ItemParse_model_origin( itemDef_t *item, int handle ) {
	modelDef_t *modelPtr;
	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (PC_Float_Parse(handle, &modelPtr->origin[0])) {
		if (PC_Float_Parse(handle, &modelPtr->origin[1])) {
			if (PC_Float_Parse(handle, &modelPtr->origin[2])) {
				return qtrue;
			}
		}
	}
	return qfalse;
}

// model_fovx <number>
qboolean ItemParse_model_fovx( itemDef_t *item, int handle ) {
	modelDef_t *modelPtr;
	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (!PC_Float_Parse(handle, &modelPtr->fov_x)) {
		return qfalse;
	}
	return qtrue;
}

// model_fovy <number>
qboolean ItemParse_model_fovy( itemDef_t *item, int handle ) {
	modelDef_t *modelPtr;
	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (!PC_Float_Parse(handle, &modelPtr->fov_y)) {
		return qfalse;
	}
	return qtrue;
}

// model_rotation <integer>
qboolean ItemParse_model_rotation( itemDef_t *item, int handle ) {
	modelDef_t *modelPtr;
	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (!PC_Int_Parse(handle, &modelPtr->rotationSpeed)) {
		return qfalse;
	}
	return qtrue;
}

// model_angle <integer>
qboolean ItemParse_model_angle( itemDef_t *item, int handle ) {
	modelDef_t *modelPtr;
	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (!PC_Int_Parse(handle, &modelPtr->angle)) {
		return qfalse;
	}
	return qtrue;
}

// model_g2mins <number> <number> <number>
qboolean ItemParse_model_g2mins( itemDef_t *item, int handle ) {
	modelDef_t *modelPtr;
	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (PC_Float_Parse(handle, &modelPtr->g2mins[0])) {
		if (PC_Float_Parse(handle, &modelPtr->g2mins[1])) {
			if (PC_Float_Parse(handle, &modelPtr->g2mins[2])) {
				return qtrue;
			}
		}
	}
	return qfalse;
}

// model_g2maxs <number> <number> <number>
qboolean ItemParse_model_g2maxs( itemDef_t *item, int handle ) {
	modelDef_t *modelPtr;
	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (PC_Float_Parse(handle, &modelPtr->g2maxs[0])) {
		if (PC_Float_Parse(handle, &modelPtr->g2maxs[1])) {
			if (PC_Float_Parse(handle, &modelPtr->g2maxs[2])) {
				return qtrue;
			}
		}
	}
	return qfalse;
}

// model_g2scale <number> <number> <number>
qboolean ItemParse_model_g2scale( itemDef_t *item, int handle ) {
	modelDef_t *modelPtr;
	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (PC_Float_Parse(handle, &modelPtr->g2scale[0])) {
		if (PC_Float_Parse(handle, &modelPtr->g2scale[1])) {
			if (PC_Float_Parse(handle, &modelPtr->g2scale[2])) {
				return qtrue;
			}
		}
	}
	return qfalse;
}

qboolean ItemParse_model_g2skin( itemDef_t *item, int handle ) {
	modelDef_t *modelPtr;
	pc_token_t token;

	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (!trap->PC_ReadToken(handle, &token)) {
		return qfalse;
	}

	if (!token.string[0])
	{ //it was parsed correctly so still return true.
		return qtrue;
	}

	modelPtr->g2skin = trap->R_RegisterSkin(token.string);

	return qtrue;
}

// model_g2anim <number>
qboolean ItemParse_model_g2anim( itemDef_t *item, int handle ) {
	modelDef_t *modelPtr;
	pc_token_t token;
	int i = 0;

	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (!trap->PC_ReadToken(handle, &token)) {
		return qfalse;
	}

	if ( !token.string[0])
	{ //it was parsed correctly so still return true.
		return qtrue;
	}

	while (i < MAX_ANIMATIONS)
	{
		if (!Q_stricmp(token.string, animTable[i].name))
		{ //found it
			modelPtr->g2anim = i;
			return qtrue;
		}
		i++;
	}

	Com_Printf("Could not find '%s' in the anim table\n", token.string);
	return qtrue;
}

// model_g2skin <string>
qboolean ItemParse_model_g2skin_go( itemDef_t *item, const char *skinName )
{
	modelDef_t *modelPtr;
	int defSkin;

	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (!skinName || !skinName[0])
	{ //it was parsed correctly so still return true.
		modelPtr->g2skin = 0;
		trap->G2API_SetSkin(item->ghoul2, 0, 0, 0);

		return qtrue;
	}

	// set skin
	if ( item->ghoul2 )
	{
		defSkin = trap->R_RegisterSkin(skinName);
		trap->G2API_SetSkin(item->ghoul2, 0, defSkin, defSkin);
	}

	return qtrue;
}

// model_g2anim <number>
qboolean ItemParse_model_g2anim_go( itemDef_t *item, const char *animName )
{
	modelDef_t *modelPtr;
	int i = 0;

	Item_ValidateTypeData(item);
	modelPtr = item->typeData.model;

	if (!animName || !animName[0])
	{ //it was parsed correctly so still return true.
		return qtrue;
	}

	while (i < MAX_ANIMATIONS)
	{
		if (!Q_stricmp(animName, animTable[i].name))
		{ //found it
			modelPtr->g2anim = animTable[i].id;
			return qtrue;
		}
		i++;
	}

	Com_Printf("Could not find '%s' in the anim table\n", animName);
	return qtrue;
}

// Get the cvar, get the values and stuff them in the rect structure.
qboolean ItemParse_rectcvar( itemDef_t *item, int handle )
{
	char	cvarBuf[1024];
	const char	*holdVal;
	char	*holdBuf;

	// get Cvar name
	pc_token_t token;
	if (!trap->PC_ReadToken(handle, &token))
	{
		return qfalse;
	}

	// get cvar data
	DC->getCVarString(token.string, cvarBuf, sizeof(cvarBuf));

	holdBuf = cvarBuf;
	if (String_Parse(&holdBuf,&holdVal))
	{
		item->window.rectClient.x = atof(holdVal);
		if (String_Parse(&holdBuf,&holdVal))
		{
			item->window.rectClient.y = atof(holdVal);
			if (String_Parse(&holdBuf,&holdVal))
			{
				item->window.rectClient.w = atof(holdVal);
				if (String_Parse(&holdBuf,&holdVal))
				{
					item->window.rectClient.h = atof(holdVal);
					return qtrue;
				}
			}
		}
	}

	// There may be no cvar built for this, and that's okay. . . I guess.
	return qtrue;
}

// rect <rectangle>
qboolean ItemParse_rect( itemDef_t *item, int handle ) {
	if (!PC_Rect_Parse(handle, &item->window.rectClient)) {
		return qfalse;
	}
	return qtrue;
}

/*
===============
ItemParse_flag
	flag <flag string>
===============
*/
qboolean ItemParse_flag( itemDef_t *item, int handle)
{
	int i;
	pc_token_t token;

	if (!trap->PC_ReadToken(handle, &token))
	{
		return qfalse;
	}

	for ( i=0; itemFlags[i].string; i++ )
	{
		if (Q_stricmp(token.string,itemFlags[i].string)==0)
		{
			item->window.flags |= itemFlags[i].value;
			break;
		}
	}

	if (itemFlags[i].string == NULL)
	{
		Com_Printf( S_COLOR_YELLOW "Unknown item style value '%s'\n", token.string );
	}

	return qtrue;
}

/*
===============
ItemParse_style
	style <integer>
===============
*/
qboolean ItemParse_style( itemDef_t *item, int handle)
{
	if (!PC_Int_Parse(handle, &item->window.style))
	{
		Com_Printf(S_COLOR_YELLOW "Unknown item style value\n");
		return qfalse;
	}

	return qtrue;
}

// decoration
qboolean ItemParse_decoration( itemDef_t *item, int handle ) {
	item->window.flags |= WINDOW_DECORATION;
	return qtrue;
}

// notselectable
qboolean ItemParse_notselectable( itemDef_t *item, int handle ) {
	listBoxDef_t *listPtr;
	Item_ValidateTypeData(item);
	listPtr = item->typeData.listbox;
	if (item->type == ITEM_TYPE_LISTBOX && listPtr) {
		listPtr->notselectable = qtrue;
	}
	return qtrue;
}

/*
===============
ItemParse_scrollhidden
	scrollhidden
===============
*/
qboolean ItemParse_scrollhidden( itemDef_t *item , int handle)
{
	listBoxDef_t *listPtr;
	Item_ValidateTypeData(item);
	listPtr = item->typeData.listbox;

	if (item->type == ITEM_TYPE_LISTBOX && listPtr)
	{
		listPtr->scrollhidden = qtrue;
	}
	return qtrue;
}

// manually wrapped
qboolean ItemParse_wrapped( itemDef_t *item, int handle ) {
	item->window.flags |= WINDOW_WRAPPED;
	return qtrue;
}

// auto wrapped
qboolean ItemParse_autowrapped( itemDef_t *item, int handle ) {
	item->window.flags |= WINDOW_AUTOWRAPPED;
	return qtrue;
}

// horizontalscroll
qboolean ItemParse_horizontalscroll( itemDef_t *item, int handle ) {
	item->window.flags |= WINDOW_HORIZONTAL;
	return qtrue;
}

/*
===============
ItemParse_type
	type <integer>
===============
*/
qboolean ItemParse_type( itemDef_t *item, int handle  )
{
	if (!PC_Int_Parse(handle, &item->type))
	{
		return qfalse;
	}
	Item_ValidateTypeData(item);
	return qtrue;
}

// elementwidth, used for listbox image elements
// uses textalignx for storage
qboolean ItemParse_elementwidth( itemDef_t *item, int handle ) {
	listBoxDef_t *listPtr;

	Item_ValidateTypeData(item);
	listPtr = item->typeData.listbox;
	if (!listPtr || !PC_Float_Parse(handle, &listPtr->elementWidth)) {
		return qfalse;
	}
	return qtrue;
}

// elementheight, used for listbox image elements
// uses textaligny for storage
qboolean ItemParse_elementheight( itemDef_t *item, int handle ) {
	listBoxDef_t *listPtr;

	Item_ValidateTypeData(item);
	listPtr = item->typeData.listbox;
	if (!listPtr || !PC_Float_Parse(handle, &listPtr->elementHeight)) {
		return qfalse;
	}
	return qtrue;
}

// feeder <float>
qboolean ItemParse_feeder( itemDef_t *item, int handle ) {
	if (!PC_Float_Parse(handle, &item->special)) {
		return qfalse;
	}
	return qtrue;
}

// elementtype, used to specify what type of elements a listbox contains
// uses textstyle for storage
qboolean ItemParse_elementtype( itemDef_t *item, int handle ) {
	listBoxDef_t *listPtr;

	Item_ValidateTypeData(item);
	listPtr = item->typeData.listbox;
	if (!listPtr || !PC_Int_Parse(handle, &listPtr->elementStyle)) {
		return qfalse;
	}
	return qtrue;
}

// columns sets a number of columns and an x pos and width per..
qboolean ItemParse_columns( itemDef_t *item, int handle ) {
	int i;
	listBoxDef_t *listPtr;

	Item_ValidateTypeData(item);
	listPtr = item->typeData.listbox;
	if (listPtr && PC_Int_Parse(handle, &listPtr->numColumns)) {
		if (listPtr->numColumns > MAX_LB_COLUMNS) {
			listPtr->numColumns = MAX_LB_COLUMNS;
		}
		for (i = 0; i < listPtr->numColumns; i++) {
			int pos, width, maxChars;

			if (PC_Int_Parse(handle, &pos) && PC_Int_Parse(handle, &width) && PC_Int_Parse(handle, &maxChars)) {
				listPtr->columnInfo[i].pos = pos;
				listPtr->columnInfo[i].width = width;
				listPtr->columnInfo[i].maxChars = maxChars;
			} else {
				return qfalse;
			}
		}
	} else {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_border( itemDef_t *item, int handle ) {
	if (!PC_Int_Parse(handle, &item->window.border)) {
		return qfalse;
	}
	return qtrue;
}

// radius <units>, shadow <units>, sheen <alpha>, glowcolor <r g b a>: of a window with the style
// WINDOW_STYLE_PANEL (ui_skin.c). For an item and for a menu, which both begin with their window.
qboolean ItemParse_radius( itemDef_t *item, int handle ) {
	return PC_Float_Parse( handle, &item->window.radius );
}

qboolean ItemParse_shadow( itemDef_t *item, int handle ) {
	return PC_Float_Parse( handle, &item->window.shadow );
}

qboolean ItemParse_sheen( itemDef_t *item, int handle ) {
	return PC_Float_Parse( handle, &item->window.sheen );
}

qboolean ItemParse_glowcolor( itemDef_t *item, int handle ) {
	return PC_Color_Parse( handle, &item->window.glowColor );
}

// stat <name>: the number of the game an ITEM_TYPE_BAR or ITEM_TYPE_VALUE shows. showStat <name>: the item is
// only there while that number is above 0.
qboolean ItemParse_stat( itemDef_t *item, int handle ) {
	return PC_String_Parse( handle, &item->stat );
}

qboolean ItemParse_showStat( itemDef_t *item, int handle ) {
	return PC_String_Parse( handle, &item->showStat );
}

// caps 1: the text of the item is drawn in capital letters
// wide 1: the window of the item reaches the sides of a wide screen, as the background of a menu does
qboolean ItemParse_wide( itemDef_t *item, int handle ) {
	int i;

	if ( !PC_Int_Parse( handle, &i ) ) {
		return qfalse;
	}
	item->window.wide = (qboolean)( i != 0 );
	return qtrue;
}

qboolean ItemParse_caps( itemDef_t *item, int handle ) {
	int i;

	if ( !PC_Int_Parse( handle, &i ) ) {
		return qfalse;
	}
	item->caps = (qboolean)( i != 0 );
	return qtrue;
}

// page <n>: the item is only there while that page of its menu is open. tab <n>: a button that opens that
// page. scroll 1: the item stands in the list of its page. row 1: a setting drawn as a row of that list (row 2:
// what was typed into it is not shown).
qboolean ItemParse_page( itemDef_t *item, int handle ) {
	return (qboolean)( PC_Int_Parse( handle, &item->page ) && item->page >= 0 && item->page < MENU_PAGES );
}

qboolean ItemParse_tab( itemDef_t *item, int handle ) {
	return (qboolean)( PC_Int_Parse( handle, &item->tab ) && item->tab >= 0 && item->tab < MENU_PAGES );
}

qboolean ItemParse_scroll( itemDef_t *item, int handle ) {
	int i;

	if ( !PC_Int_Parse( handle, &i ) ) {
		return qfalse;
	}
	item->scroll = (qboolean)( i != 0 );
	return qtrue;
}

qboolean ItemParse_row( itemDef_t *item, int handle ) {
	return PC_Int_Parse( handle, &item->row );
}

// fold <n>: a heading of a list that folds the rows under it away when it is clicked (Menu_ListLayout)
qboolean ItemParse_fold( itemDef_t *item, int handle ) {
	return PC_Int_Parse( handle, &item->fold );
}

qboolean ItemParse_textRoom( itemDef_t *item, int handle ) {
	return PC_Float_Parse( handle, &item->textRoom );
}

qboolean ItemParse_cardHead( itemDef_t *item, int handle ) {
	int i;

	if ( !PC_Int_Parse( handle, &i ) ) {
		return qfalse;
	}
	item->cardHead = (qboolean)( i != 0 );
	return qtrue;
}

qboolean ItemParse_column( itemDef_t *item, int handle ) {
	return PC_Int_Parse( handle, &item->column );
}

// stack <n>: one of a column of rows that closes up over the places that are hidden (Menu_StackFrame)
qboolean ItemParse_stack( itemDef_t *item, int handle ) {
	return PC_Int_Parse( handle, &item->stack );
}

qboolean ItemParse_advanced( itemDef_t *item, int handle ) {
	int i;

	if ( !PC_Int_Parse( handle, &i ) ) {
		return qfalse;
	}
	item->advanced = (qboolean)( i != 0 );
	return qtrue;
}

// descPic <picture> <name a> <name b>: before and after, in the pane of the menu (Item_DescPic_Paint)
qboolean ItemParse_descPic( itemDef_t *item, int handle ) {
	int i;

	for ( i = 0; i < 3; i++ ) {
		if ( !PC_String_Parse( handle, &item->descPic[i] ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// hint 1: an edit field whose text stands dimmed in its empty box instead of in front of it (Item_HintField_Paint)
qboolean ItemParse_hint( itemDef_t *item, int handle ) {
	int i;

	if ( !PC_Int_Parse( handle, &i ) ) {
		return qfalse;
	}
	item->hint = (qboolean)( i != 0 );
	return qtrue;
}

// flow 1: a button of a bar. The buttons of a row are as wide as their words and follow each other, with a
// short line between them (Menu_TabsLayout). flow 2: a button of a strip that leads to other menus, laid out the
// same way and drawn as the button of a page is. flow 3: the entry of that strip for the menu that is open.
qboolean ItemParse_flow( itemDef_t *item, int handle ) {
	return PC_Int_Parse( handle, &item->flow );
}

qboolean ItemParse_bordersize( itemDef_t *item, int handle ) {
	if (!PC_Float_Parse(handle, &item->window.borderSize)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_visible( itemDef_t *item, int handle ) {
	int i;

	if (!PC_Int_Parse(handle, &i)) {
		return qfalse;
	}
	if (i) {
		item->window.flags |= WINDOW_VISIBLE;
	}
	return qtrue;
}

qboolean ItemParse_ownerdraw( itemDef_t *item, int handle ) {
	if (!PC_Int_Parse(handle, &item->window.ownerDraw)) {
		return qfalse;
	}
	item->type = ITEM_TYPE_OWNERDRAW;
	return qtrue;
}

qboolean ItemParse_align( itemDef_t *item, int handle ) {
	if (!PC_Int_Parse(handle, &item->alignment)) {
		return qfalse;
	}
	return qtrue;
}

/*
===============
ItemParse_isCharacter

Sets item flag showing this is a character
===============
*/
qboolean ItemParse_isCharacter( itemDef_t *item, int handle  )
{
	int flag;

	if (PC_Int_Parse(handle, &flag))
	{
		if ( flag )
		{
			item->flags |= ITF_ISCHARACTER;
		}
		else
		{
			item->flags &= ~ITF_ISCHARACTER;
		}
		return qtrue;
	}
	return qfalse;
}

/*
===============
ItemParse_textalign
===============
*/
qboolean ItemParse_textalign( itemDef_t *item, int handle )
{
	if (!PC_Int_Parse(handle, &item->textalignment))
	{
		Com_Printf(S_COLOR_YELLOW "Unknown text alignment value\n");

		return qfalse;
	}

	return qtrue;
}

qboolean ItemParse_textalignx( itemDef_t *item, int handle ) {
	if (!PC_Float_Parse(handle, &item->textalignx)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_textaligny( itemDef_t *item, int handle ) {
	if (!PC_Float_Parse(handle, &item->textaligny)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_textscale( itemDef_t *item, int handle ) {
	if (!PC_Float_Parse(handle, &item->textscale)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_textstyle( itemDef_t *item, int handle ) {
	if (!PC_Int_Parse(handle, &item->textStyle)) {
		return qfalse;
	}
	return qtrue;
}

/*
===============
ItemParse_invertyesno
===============
*/
//JLFYESNO MPMOVED
qboolean ItemParse_invertyesno( itemDef_t *item, int handle)
{
	if (!PC_Int_Parse(handle, &item->invertYesNo))
	{
		return qfalse;
	}
	return qtrue;
}

/*
===============
ItemParse_xoffset (used for yes/no and multi)
===============
*/
qboolean ItemParse_xoffset( itemDef_t *item, int handle)
{
	if (PC_Int_Parse(handle, &item->xoffset))
	{
		return qfalse;
	}
	return qtrue;
}


qboolean ItemParse_backcolor( itemDef_t *item, int handle ) {
	int i;
	float f;

	for (i = 0; i < 4; i++) {
		if (!PC_Float_Parse(handle, &f)) {
			return qfalse;
		}
		item->window.backColor[i]  = f;
	}
	return qtrue;
}

qboolean ItemParse_forecolor( itemDef_t *item, int handle ) {
	int i;
	float f;

	for (i = 0; i < 4; i++) {
		if (!PC_Float_Parse(handle, &f)) {
			return qfalse;
		}

		if (f < 0)
		{	//special case for player color
			item->window.flags |= WINDOW_PLAYERCOLOR;
			return qtrue;
		}

		item->window.foreColor[i]  = f;
		item->window.flags |= WINDOW_FORECOLORSET;
	}
	return qtrue;
}

qboolean ItemParse_bordercolor( itemDef_t *item, int handle ) {
	int i;
	float f;

	for (i = 0; i < 4; i++) {
		if (!PC_Float_Parse(handle, &f)) {
			return qfalse;
		}
		item->window.borderColor[i]  = f;
	}
	return qtrue;
}

qboolean ItemParse_outlinecolor( itemDef_t *item, int handle ) {
	if (!PC_Color_Parse(handle, &item->window.outlineColor)){
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_background( itemDef_t *item, int handle ) {
	pc_token_t token;

	if (!trap->PC_ReadToken(handle, &token)) {
		return qfalse;
	}
	item->window.background = DC->registerShaderNoMip(token.string);
	return qtrue;
}

qboolean ItemParse_cinematic( itemDef_t *item, int handle ) {
	if (!PC_String_Parse(handle, &item->window.cinematicName)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_doubleClick( itemDef_t *item, int handle ) {
	listBoxDef_t *listPtr;

	Item_ValidateTypeData(item);
	listPtr = item->typeData.listbox;

	if (!listPtr || !PC_Script_Parse(handle, &listPtr->doubleClick)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_onFocus( itemDef_t *item, int handle ) {
	if (!PC_Script_Parse(handle, &item->onFocus)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_leaveFocus( itemDef_t *item, int handle ) {
	if (!PC_Script_Parse(handle, &item->leaveFocus)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_mouseEnter( itemDef_t *item, int handle ) {
	if (!PC_Script_Parse(handle, &item->mouseEnter)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_mouseExit( itemDef_t *item, int handle ) {
	if (!PC_Script_Parse(handle, &item->mouseExit)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_mouseEnterText( itemDef_t *item, int handle ) {
	if (!PC_Script_Parse(handle, &item->mouseEnterText)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_mouseExitText( itemDef_t *item, int handle ) {
	if (!PC_Script_Parse(handle, &item->mouseExitText)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_action( itemDef_t *item, int handle ) {
	if (!PC_Script_Parse(handle, &item->action)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_special( itemDef_t *item, int handle ) {
	if (!PC_Float_Parse(handle, &item->special)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_cvarTest( itemDef_t *item, int handle ) {
	if (!PC_String_Parse(handle, &item->cvarTest)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_cvar( itemDef_t *item, int handle )
{
	Item_ValidateTypeData(item);
	if (!PC_String_Parse(handle, &item->cvar))
	{
		return qfalse;
	}

	switch ( item->type )
	{
		case ITEM_TYPE_EDITFIELD:
		case ITEM_TYPE_NUMERICFIELD:
		case ITEM_TYPE_YESNO:
		case ITEM_TYPE_BIND:
		case ITEM_TYPE_SLIDER:
		case ITEM_TYPE_TEXT:
		{
			if ( item->typeData.edit )
			{
				item->typeData.edit->minVal = -1;
				item->typeData.edit->maxVal = -1;
				item->typeData.edit->defVal = -1;
			}
			break;
		}
	}
	return qtrue;
}

qboolean ItemParse_font( itemDef_t *item, int handle )
{
	Item_ValidateTypeData(item);
	if (!PC_Int_Parse(handle, &item->iMenuFont))
	{
		return qfalse;
	}
	return qtrue;
}


qboolean ItemParse_maxChars( itemDef_t *item, int handle ) {
	editFieldDef_t *editPtr;

	Item_ValidateTypeData(item);
	editPtr = (editFieldDef_t*)item->typeData.edit;

	if (!editPtr || !PC_Int_Parse(handle, &editPtr->maxChars)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_maxPaintChars( itemDef_t *item, int handle ) {
	editFieldDef_t *editPtr;

	Item_ValidateTypeData(item);
	editPtr = (editFieldDef_t*)item->typeData.edit;

	if (!editPtr || !PC_Int_Parse(handle, &editPtr->maxPaintChars)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_maxLineChars( itemDef_t *item, int handle ) {
	textScrollDef_t *scrollPtr;

	Item_ValidateTypeData(item);
	scrollPtr = item->typeData.textscroll;

	if (!scrollPtr || !PC_Int_Parse(handle, &scrollPtr->maxLineChars)) {
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_lineHeight( itemDef_t *item, int handle ) {
	textScrollDef_t *scrollPtr;
	int height;

	Item_ValidateTypeData(item);
	scrollPtr = item->typeData.textscroll;

	if (!scrollPtr || !PC_Int_Parse(handle, &height)) {
		return qfalse;
	}
	scrollPtr->lineHeight = height;
	return qtrue;
}

qboolean ItemParse_cvarFloat( itemDef_t *item, int handle ) {
	editFieldDef_t *editPtr;

	Item_ValidateTypeData(item);
	editPtr = item->typeData.edit;

	if (!editPtr)
		return qfalse;

	if (PC_String_Parse(handle, &item->cvar) &&
		PC_Float_Parse(handle, &editPtr->defVal) &&
		PC_Float_Parse(handle, &editPtr->minVal) &&
		PC_Float_Parse(handle, &editPtr->maxVal)) {
		return qtrue;
	}
	return qfalse;
}

#ifdef UI_BUILD
char currLanguage[32][128];
static const char languageString[32] = "@MENUS_MYLANGUAGE";
#endif

#ifdef UI_BUILD
static int QDECL Item_Multi_CompareVideoModes( const void *a, const void *b ) {
	const int *sizeA = (const int *)a, *sizeB = (const int *)b;

	if ( sizeA[0] != sizeB[0] ) {
		return sizeA[0] - sizeB[0];
	}
	return sizeA[1] - sizeB[1];
}

static int Item_Multi_AddVideoMode( int sizes[][2], int count, int max, int width, int height ) {
	int i;

	if ( width <= 0 || height <= 0 || count >= max ) {
		return count;
	}
	for ( i = 0; i < count; i++ ) {
		if ( sizes[i][0] == width && sizes[i][1] == height ) {
			return count;
		}
	}
	sizes[count][0] = width;
	sizes[count][1] = height;
	return count + 1;
}

// sizes a window can have, offered on every display they fit on (under Wayland the display lists its own size only)
static const int windowSizes[][2] = {
	{ 1024, 768 }, { 1280, 720 }, { 1280, 800 }, { 1280, 1024 }, { 1366, 768 }, { 1440, 900 }, { 1600, 900 },
	{ 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 }, { 2560, 1080 }, { 2560, 1440 }, { 2560, 1600 }, { 3440, 1440 },
	{ 3840, 2160 },
};

/*
===============
Item_Multi_FeedVideoModes
The entries of the video mode list: "Desktop", the size of the screen (r_mode -2), the sizes the display
offers (r_availableModes of the engine, "1280x720 1920x1080 ...") and the common sizes of a window that fit on
it, smallest first, with the size in use if it is none of them. The value of an entry is "desktop" or
"<width>x<height>": UI_UpdateVideoSetup() turns it into r_mode, r_customwidth and r_customheight.
===============
*/
static void Item_Multi_FeedVideoModes( multiDef_t *multiPtr ) {
	char		modes[MAX_STRING_CHARS];
	int			sizes[MAX_MULTI_CVARS - 1][2];
	int			count, width, height, i;
	int			maxWidth, maxHeight;
	const char	*p;

	// the size in use first: it has a place however many the display offers
	count = Item_Multi_AddVideoMode( sizes, 0, ARRAY_LEN( sizes ), DC->glconfig.vidWidth, DC->glconfig.vidHeight );
	DC->getCVarString( "r_availableModes", modes, sizeof( modes ) );
	p = modes;
	while ( *p == ' ' ) {
		p++;
	}
	while ( *p ) {
		if ( sscanf( p, "%dx%d", &width, &height ) == 2 ) {
			count = Item_Multi_AddVideoMode( sizes, count, ARRAY_LEN( sizes ), width, height );
		}
		while ( *p && *p != ' ' ) {
			p++;
		}
		while ( *p == ' ' ) {
			p++;
		}
	}
	// the largest size the display offers is the one of the desktop
	maxWidth = maxHeight = 0;
	for ( i = 0; i < count; i++ ) {
		maxWidth = Q_max( maxWidth, sizes[i][0] );
		maxHeight = Q_max( maxHeight, sizes[i][1] );
	}
	for ( i = 0; i < (int)ARRAY_LEN( windowSizes ); i++ ) {
		if ( windowSizes[i][0] <= maxWidth && windowSizes[i][1] <= maxHeight ) {
			count = Item_Multi_AddVideoMode( sizes, count, ARRAY_LEN( sizes ), windowSizes[i][0], windowSizes[i][1] );
		}
	}
	qsort( sizes, count, sizeof( sizes[0] ), Item_Multi_CompareVideoModes );

	multiPtr->cvarList[0] = "Desktop";
	multiPtr->cvarStr[0] = "desktop";
	multiPtr->count = 1;
	for ( i = 0; i < count; i++ ) {
		multiPtr->cvarList[multiPtr->count] = multiPtr->cvarStr[multiPtr->count] = String_Alloc( va( "%ix%i", sizes[i][0], sizes[i][1] ) );
		multiPtr->count++;
	}
}
#endif

// otherText <text>: what a choice shows while its cvar has a value that is none of its settings, in place of
// "Custom". (The style of a blade: a blade in one of the fixed colours has none.)
qboolean ItemParse_otherText( itemDef_t *item, int handle ) {
	const char *text;

	if ( item->type != ITEM_TYPE_MULTI || !PC_String_Parse( handle, &text ) ) {
		return qfalse;
	}
	Item_ValidateTypeData( item );
	if ( !item->typeData.multi ) {
		return qfalse;
	}
	item->typeData.multi->other = text;
	return qtrue;
}

qboolean ItemParse_cvarStrList( itemDef_t *item, int handle ) {
	pc_token_t token;
	multiDef_t *multiPtr;
	int pass;

	Item_ValidateTypeData(item);
	multiPtr = item->typeData.multi;

	if (!multiPtr)
		return qfalse;

	multiPtr->count = 0;
	multiPtr->strDef = qtrue;

	if (!trap->PC_ReadToken(handle, &token))
	{
		return qfalse;
	}

	if (!Q_stricmp(token.string,"feeder") && item->special == FEEDER_PLAYER_SPECIES)
	{
#ifndef _CGAME
		// (the species are folders of paks: there are places for MAX_MULTI_CVARS of them)
		for (; multiPtr->count < uiInfo.playerSpeciesCount && multiPtr->count < MAX_MULTI_CVARS; multiPtr->count++)
		{
			multiPtr->cvarList[multiPtr->count] = String_Alloc(Q_strupr(va("@MENUS_%s",uiInfo.playerSpecies[multiPtr->count].Name )));	//look up translation
			multiPtr->cvarStr[multiPtr->count] = uiInfo.playerSpecies[multiPtr->count].Name;	//value
		}
#endif
		return qtrue;
	}
	// languages
	if (!Q_stricmp(token.string,"feeder") && item->special == FEEDER_LANGUAGES)
	{
#ifdef UI_BUILD
		for (; multiPtr->count < uiInfo.languageCount; multiPtr->count++)
		{
			// The displayed text
			trap->SE_GetLanguageName( (const int) multiPtr->count,(char *) currLanguage[multiPtr->count]  );	// eg "English"
			multiPtr->cvarList[multiPtr->count] = languageString;
			// The cvar value that goes into se_language
			trap->SE_GetLanguageName( (const int) multiPtr->count,(char *) currLanguage[multiPtr->count] );
			multiPtr->cvarStr[multiPtr->count] = currLanguage[multiPtr->count];
		}
#endif
		return qtrue;
	}

	// video modes
	if (!Q_stricmp(token.string,"feeder") && item->special == FEEDER_VIDEOMODES)
	{
#ifdef UI_BUILD
		Item_Multi_FeedVideoModes( multiPtr );
#endif
		return qtrue;
	}

	if (*token.string != '{') {
		return qfalse;
	}

	pass = 0;
	while ( 1 ) {
		char* psString;

		if (!PC_String_Parse(handle, (const char **)&psString))
		{
			PC_SourceError(handle, "end of file inside menu item");
			return qfalse;
		}

		//a normal StringAlloc ptr
		if (psString)
		{
			if (*psString == '}') {
				return qtrue;
			}

			if (*psString == ',' || *psString == ';') {
				continue;
			}
		}

		if (pass == 0) {
			multiPtr->cvarList[multiPtr->count] = psString;
			pass = 1;
		} else {
			multiPtr->cvarStr[multiPtr->count] = psString;
			pass = 0;
			multiPtr->count++;
			if (multiPtr->count >= MAX_MULTI_CVARS) {
				return qfalse;
			}
		}

	}
	return qfalse;
}

qboolean ItemParse_cvarFloatList( itemDef_t *item, int handle )
{
	pc_token_t token;
	multiDef_t *multiPtr;

	Item_ValidateTypeData(item);
	multiPtr = item->typeData.multi;

	if (!multiPtr)
		return qfalse;

	multiPtr->count = 0;
	multiPtr->strDef = qfalse;

	if (!trap->PC_ReadToken(handle, &token))
	{
		return qfalse;
	}

	if (*token.string != '{')
	{
		return qfalse;
	}

	while ( 1 )
	{
		char* string;

		if ( !PC_String_Parse ( handle, (const char **)&string ) )
		{
			PC_SourceError(handle, "end of file inside menu item");
			return qfalse;
		}

		//a normal StringAlloc ptr
		if (string)
		{
			if (*string == '}')
			{
				return qtrue;
			}

			if (*string == ',' || *string == ';')
			{
				continue;
			}
		}

		multiPtr->cvarList[multiPtr->count] = string;
		if (!PC_Float_Parse(handle, &multiPtr->cvarValue[multiPtr->count]))
		{
			return qfalse;
		}

		multiPtr->count++;
		if (multiPtr->count >= MAX_MULTI_CVARS)
		{
			return qfalse;
		}

	}
	return qfalse;
}

qboolean ItemParse_addColorRange( itemDef_t *item, int handle ) {
	colorRangeDef_t color;

	if (PC_Float_Parse(handle, &color.low) &&
		PC_Float_Parse(handle, &color.high) &&
		PC_Color_Parse(handle, &color.color) ) {
		if (item->numColors < MAX_COLOR_RANGES) {
			memcpy(&item->colorRanges[item->numColors], &color, sizeof(color));
			item->numColors++;
		}
		return qtrue;
	}
	return qfalse;
}

qboolean ItemParse_ownerdrawFlag( itemDef_t *item, int handle ) {
	int i;
	if (!PC_Int_Parse(handle, &i)) {
		return qfalse;
	}
	item->window.ownerDrawFlags |= i;
	return qtrue;
}

qboolean ItemParse_enableCvar( itemDef_t *item, int handle ) {
	if (PC_Script_Parse(handle, &item->enableCvar)) {
		item->cvarFlags = CVAR_ENABLE;
		return qtrue;
	}
	return qfalse;
}

qboolean ItemParse_disableCvar( itemDef_t *item, int handle ) {
	if (PC_Script_Parse(handle, &item->enableCvar)) {
		item->cvarFlags = CVAR_DISABLE;
		return qtrue;
	}
	return qfalse;
}

qboolean ItemParse_showCvar( itemDef_t *item, int handle ) {
	if (PC_Script_Parse(handle, &item->enableCvar)) {
		item->cvarFlags = CVAR_SHOW;
		return qtrue;
	}
	return qfalse;
}

qboolean ItemParse_hideCvar( itemDef_t *item, int handle ) {
	if (PC_Script_Parse(handle, &item->enableCvar)) {
		item->cvarFlags = CVAR_HIDE;
		return qtrue;
	}
	return qfalse;
}

/*
===============
ItemParse_align
===============
*/
qboolean ItemParse_Appearance_slot( itemDef_t *item, int handle )
{
	if (!PC_Int_Parse(handle, &item->appearanceSlot))
	{
		return qfalse;
	}
	return qtrue;
}

qboolean ItemParse_isSaber( itemDef_t *item, int handle  )
{
#ifndef _CGAME

	int	i;

	if (PC_Int_Parse(handle, &i))
	{
		if ( i )
		{
			item->flags |= ITF_ISSABER;
			UI_CacheSaberGlowGraphics();
			if ( !ui_saber_parms_parsed )
			{
				UI_SaberLoadParms();
			}
		}
		else
		{
			item->flags &= ~ITF_ISSABER;
		}

		return qtrue;
	}
#endif
	return qfalse;
}

qboolean ItemParse_isSaber2( itemDef_t *item, int handle  )
{
#ifndef _CGAME
	int	i;
	if (PC_Int_Parse(handle, &i))
	{
		if ( i )
		{
			item->flags |= ITF_ISSABER2;
			UI_CacheSaberGlowGraphics();
			if ( !ui_saber_parms_parsed )
			{
				UI_SaberLoadParms();
			}
		}
		else
		{
			item->flags &= ~ITF_ISSABER2;
		}

		return qtrue;
	}
#endif

	return qfalse;
}

keywordHash_t itemParseKeywords[] = {
	{"action",			ItemParse_action,			NULL	},
	{"addColorRange",	ItemParse_addColorRange,	NULL	},
	{"align",			ItemParse_align,			NULL	},
	{"autowrapped",		ItemParse_autowrapped,		NULL	},
	{"appearance_slot",	ItemParse_Appearance_slot,	NULL	},
	{"asset_model",		ItemParse_asset_model,		NULL	},
	{"asset_shader",	ItemParse_asset_shader,		NULL	},
	{"backcolor",		ItemParse_backcolor,		NULL	},
	{"background",		ItemParse_background,		NULL	},
	{"border",			ItemParse_border,			NULL	},
	{"bordercolor",		ItemParse_bordercolor,		NULL	},
	{"bordersize",		ItemParse_bordersize,		NULL	},
	{"cinematic",		ItemParse_cinematic,		NULL	},
	{"columns",			ItemParse_columns,			NULL	},
	{"cvar",			ItemParse_cvar,				NULL	},
	{"cvarFloat",		ItemParse_cvarFloat,		NULL	},
	{"cvarFloatList",	ItemParse_cvarFloatList,	NULL	},
	{"cvarStrList",		ItemParse_cvarStrList,		NULL	},
	{"otherText",		ItemParse_otherText,		NULL	},
	{"cvarTest",		ItemParse_cvarTest,			NULL	},
	{"desctext",		ItemParse_descText,			NULL	},
	{"decoration",		ItemParse_decoration,		NULL	},
	{"disableCvar",		ItemParse_disableCvar,		NULL	},
	{"doubleclick",		ItemParse_doubleClick,		NULL	},
	{"elementheight",	ItemParse_elementheight,	NULL	},
	{"elementtype",		ItemParse_elementtype,		NULL	},
	{"elementwidth",	ItemParse_elementwidth,		NULL	},
	{"enableCvar",		ItemParse_enableCvar,		NULL	},
	{"feeder",			ItemParse_feeder,			NULL	},
	{"flag",			ItemParse_flag,				NULL	},
	{"focusSound",		ItemParse_focusSound,		NULL	},
	{"font",			ItemParse_font,				NULL	},
	{"forecolor",		ItemParse_forecolor,		NULL	},
	{"group",			ItemParse_group,			NULL	},
	{"hideCvar",		ItemParse_hideCvar,			NULL	},
	{"horizontalscroll", ItemParse_horizontalscroll, NULL	},
	{"isCharacter",		ItemParse_isCharacter,		NULL	},
	{"isSaber",			ItemParse_isSaber,			NULL	},
	{"isSaber2",		ItemParse_isSaber2,			NULL	},
	{"leaveFocus",		ItemParse_leaveFocus,		NULL	},
	{"maxChars",		ItemParse_maxChars,			NULL	},
	{"maxPaintChars",	ItemParse_maxPaintChars,	NULL	},
	{"model_angle",		ItemParse_model_angle,		NULL	},
	{"model_fovx",		ItemParse_model_fovx,		NULL	},
	{"model_fovy",		ItemParse_model_fovy,		NULL	},
	{"model_origin",	ItemParse_model_origin,		NULL	},
	{"model_rotation",	ItemParse_model_rotation,	NULL	},
	//rww - g2 begin
	{"model_g2mins",	ItemParse_model_g2mins,		NULL	},
	{"model_g2maxs",	ItemParse_model_g2maxs,		NULL	},
	{"model_g2scale",	ItemParse_model_g2scale,	NULL	},
	{"model_g2skin",	ItemParse_model_g2skin,		NULL	},
	{"model_g2anim",	ItemParse_model_g2anim,		NULL	},
	//rww - g2 end
	{"mouseEnter",		ItemParse_mouseEnter,		NULL	},
	{"mouseEnterText",	ItemParse_mouseEnterText,	NULL	},
	{"mouseExit",		ItemParse_mouseExit,		NULL	},
	{"mouseExitText",	ItemParse_mouseExitText,	NULL	},
	{"name",			ItemParse_name,				NULL	},
	{"notselectable",	ItemParse_notselectable,	NULL	},
	{"onFocus",			ItemParse_onFocus,			NULL	},
	{"outlinecolor",	ItemParse_outlinecolor,		NULL	},
	{"ownerdraw",		ItemParse_ownerdraw,		NULL	},
	{"ownerdrawFlag",	ItemParse_ownerdrawFlag,	NULL	},
	{"rect",			ItemParse_rect,				NULL	},
	{"rectcvar",		ItemParse_rectcvar,			NULL	},
	{"showCvar",		ItemParse_showCvar,			NULL	},
	{"special",			ItemParse_special,			NULL	},
	{"radius",			ItemParse_radius,			NULL	},
	{"shadow",			ItemParse_shadow,			NULL	},
	{"sheen",			ItemParse_sheen,			NULL	},
	{"glowcolor",		ItemParse_glowcolor,		NULL	},
	{"stat",			ItemParse_stat,				NULL	},
	{"showStat",		ItemParse_showStat,			NULL	},
	{"caps",			ItemParse_caps,				NULL	},
	{"page",			ItemParse_page,				NULL	},
	{"tab",				ItemParse_tab,				NULL	},
	{"scroll",			ItemParse_scroll,			NULL	},
	{"row",				ItemParse_row,				NULL	},
	{"fold",			ItemParse_fold,				NULL	},
	{"cardHead",		ItemParse_cardHead,			NULL	},
	{"textRoom",		ItemParse_textRoom,			NULL	},
	{"column",			ItemParse_column,			NULL	},
	{"stack",			ItemParse_stack,			NULL	},
	{"advanced",		ItemParse_advanced,			NULL	},
	{"hint",			ItemParse_hint,				NULL	},
	{"descPic",			ItemParse_descPic,			NULL	},
	{"flow",			ItemParse_flow,				NULL	},
	{"wide",			ItemParse_wide,				NULL	},
	{"style",			ItemParse_style,			NULL	},
	{"text",			ItemParse_text,				NULL	},
	{"textalign",		ItemParse_textalign,		NULL	},
	{"textalignx",		ItemParse_textalignx,		NULL	},
	{"textaligny",		ItemParse_textaligny,		NULL	},
	{"textscale",		ItemParse_textscale,		NULL	},
	{"textstyle",		ItemParse_textstyle,		NULL	},
	{"text2",			ItemParse_text2,			NULL	},
	{"text2alignx",		ItemParse_text2alignx,		NULL	},
	{"text2aligny",		ItemParse_text2aligny,		NULL	},
	{"type",			ItemParse_type,				NULL	},
	{"visible",			ItemParse_visible,			NULL	},
	{"wrapped",			ItemParse_wrapped,			NULL	},

	// Text scroll specific
	{"maxLineChars",	ItemParse_maxLineChars,		NULL	},
	{"lineHeight",		ItemParse_lineHeight,		NULL	},
	{"invertyesno",		ItemParse_invertyesno,		NULL	},
	//JLF MPMOVED
	{"scrollhidden",	ItemParse_scrollhidden,		NULL	},
	{"xoffset		",	ItemParse_xoffset,			NULL	},
	//JLF end



	{0,					0,							0		}
};

keywordHash_t *itemParseKeywordHash[KEYWORDHASH_SIZE];

/*
===============
Item_SetupKeywordHash
===============
*/
void Item_SetupKeywordHash(void) {
	int i;

	memset(itemParseKeywordHash, 0, sizeof(itemParseKeywordHash));
	for (i = 0; itemParseKeywords[i].keyword; i++) {
		KeywordHash_Add(itemParseKeywordHash, &itemParseKeywords[i]);
	}
}

/*
===============
Item_ApplyHacks
Hacks to fix issues with Team Arena menu scripts
===============
*/
static void Item_ApplyHacks( itemDef_t *item ) {
	// EAX: no engine this module runs in has it. The menus of this tree do not have the entry, the ones of the
	// base game do (ui/jamp/setup.menu and ingame_setup.menu in assets1.pk3): the switch and its picture are hidden.
	// So is their sound quality entry: it offers 11 and 22 kHz, the engine runs at 44 and v_spatial needs that.
	if ( ( item->type == ITEM_TYPE_MULTI && item->cvar && ( !Q_stricmp( item->cvar, "s_UseOpenAL" ) || !Q_stricmp( item->cvar, "s_khz" ) ) )
		|| ( item->window.name && !Q_stricmp( item->window.name, "eax_icon" ) ) ) {
		item->disabled = item->disabledHidden = qtrue;
		// Just in case it had focus
		item->window.flags &= ~WINDOW_MOUSEOVER;
	}

	// Fix length of favorite address in createfavorite.menu
	if ( item->type == ITEM_TYPE_EDITFIELD && item->cvar && !Q_stricmp( item->cvar, "ui_favoriteAddress" ) ) {
		editFieldDef_t *editField = item->typeData.edit;

		// enough to hold an IPv6 address plus null
		if ( editField->maxChars < 48 ) {
			Com_Printf( "Extended create favorite address edit field length to hold an IPv6 address\n" );
			editField->maxChars = 48;
		}
	}

	if ( item->type == ITEM_TYPE_EDITFIELD && item->cvar && ( !Q_stricmp( item->cvar, "ui_Name" ) || !Q_stricmp( item->cvar, "ui_findplayer" ) ) ) {
		editFieldDef_t *editField = item->typeData.edit;

		// enough to hold a full player name
		if ( editField->maxChars < MAX_NAME_LENGTH ) {
			if ( editField->maxPaintChars > editField->maxChars ) {
				editField->maxPaintChars = editField->maxChars;
			}

			Com_Printf( "Extended player name field using cvar %s to %d characters\n", item->cvar, MAX_NAME_LENGTH );
			editField->maxChars = MAX_NAME_LENGTH;
		}
	}

	if ( item->type == ITEM_TYPE_MULTI && item->window.name && !Q_stricmp( item->window.name, "sound_quality") ) {
		multiDef_t *multiPtr = item->typeData.multi;
		int i;
		qboolean found = qfalse;
		for( i = 0; i < multiPtr->count; i++ )
		{
			if ( multiPtr->cvarValue[i] == 44 )
			{
				found = qtrue;
				break;
			}
		}
		if ( !found && multiPtr->count < MAX_MULTI_CVARS )
		{
			multiPtr->cvarList[multiPtr->count] = String_Alloc("@MENUS_VERY_HIGH");
			multiPtr->cvarValue[multiPtr->count] = 44;
			multiPtr->count++;
			Com_Printf( "Extended sound quality field to contain very high setting.\n");
		}
	}
}

/*
===============
Item_Parse
===============
*/
qboolean Item_Parse(int handle, itemDef_t *item) {
	pc_token_t token;
	keywordHash_t *key;


	if (!trap->PC_ReadToken(handle, &token))
		return qfalse;

	if (*token.string != '{') {
		return qfalse;
	}
	while ( 1 ) {
		if (!trap->PC_ReadToken(handle, &token)) {
			PC_SourceError(handle, "end of file inside menu item");
			return qfalse;
		}

		if (*token.string == '}') {
			return qtrue;
		}

		key = KeywordHash_Find(itemParseKeywordHash, token.string);
		if (!key) {
			PC_SourceError(handle, "unknown menu item keyword %s", token.string);
			continue;
		}
		if ( !key->func(item, handle) ) {
			PC_SourceError(handle, "couldn't parse menu item keyword %s", token.string);
			return qfalse;
		}
	}
	return qfalse;
}

static void Item_TextScroll_BuildLines ( itemDef_t* item )
{
	char text[2048];

#if 1

	// new asian-aware line breaker...  (pasted from elsewhere late @ night, hence aliasing-vars ;-)
	//
	textScrollDef_t* scrollPtr = item->typeData.textscroll;
	const char *psText = item->text;	// for copy/paste ease
	int iBoxWidth = item->window.rect.w - SCROLLBAR_SIZE - 10;

	// this could probably be simplified now, but it was converted from something else I didn't originally write,
	//	and it works anyway so wtf...
	//
	const char *psCurrentTextReadPos;
	const char *psReadPosAtLineStart;
	const char *psBestLineBreakSrcPos;
	const char *psLastGood_s;	// needed if we get a full screen of chars with no punctuation or space (see usage notes)
	qboolean bIsTrailingPunctuation;
	unsigned int uiLetter;

	if (*psText == '@')	// string reference
	{
		trap->SE_GetStringTextString( &psText[1], text, sizeof(text));
		psText = text;
	}

	psCurrentTextReadPos = psText;
	psReadPosAtLineStart = psCurrentTextReadPos;
	psBestLineBreakSrcPos = psCurrentTextReadPos;

	scrollPtr->iLineCount = 0;
	memset((char*)scrollPtr->pLines,0,sizeof(scrollPtr->pLines));

	while (*psCurrentTextReadPos && (scrollPtr->iLineCount < MAX_TEXTSCROLL_LINES) )
	{
		char sLineForDisplay[2048];	// ott

		// construct a line...
		//
		psCurrentTextReadPos = psReadPosAtLineStart;
		sLineForDisplay[0] = '\0';
		while ( *psCurrentTextReadPos )
		{
			int iAdvanceCount;
			psLastGood_s = psCurrentTextReadPos;

			// read letter...
			//
			uiLetter = trap->R_AnyLanguage_ReadCharFromString(psCurrentTextReadPos, &iAdvanceCount, &bIsTrailingPunctuation);
			psCurrentTextReadPos += iAdvanceCount;

			// concat onto string so far...
			//
			if (uiLetter == 32 && sLineForDisplay[0] == '\0')
			{
				psReadPosAtLineStart++;
				continue;	// unless it's a space at the start of a line, in which case ignore it.
			}

			if (uiLetter > 255)
			{
				Q_strcat(sLineForDisplay, sizeof(sLineForDisplay),va("%c%c",uiLetter >> 8, uiLetter & 0xFF));
			}
			else
			{
				Q_strcat(sLineForDisplay, sizeof(sLineForDisplay),va("%c",uiLetter & 0xFF));
			}

			if (uiLetter == '\n')
			{
				// explicit new line...
				//
				sLineForDisplay[ strlen(sLineForDisplay)-1 ] = '\0';	// kill the CR
				psReadPosAtLineStart = psCurrentTextReadPos;
				psBestLineBreakSrcPos = psCurrentTextReadPos;

				// hack it to fit in with this code...
				//
				scrollPtr->pLines[ scrollPtr->iLineCount ] = String_Alloc ( sLineForDisplay );
				break;	// print this line
			}
			else
			if ( DC->textWidth( sLineForDisplay, item->textscale, item->iMenuFont ) >= iBoxWidth )
			{
				// reached screen edge, so cap off string at bytepos after last good position...
				//
				if (uiLetter > 255 && bIsTrailingPunctuation && !trap->R_Language_UsesSpaces())
				{
					// Special case, don't consider line breaking if you're on an asian punctuation char of
					//	a language that doesn't use spaces...
					//
				}
				else
				{
					if (psBestLineBreakSrcPos == psReadPosAtLineStart)
					{
						//  aarrrggh!!!!!   we'll only get here is someone has fed in a (probably) garbage string,
						//		since it doesn't have a single space or punctuation mark right the way across one line
						//		of the screen.  So far, this has only happened in testing when I hardwired a taiwanese
						//		string into this function while the game was running in english (which should NEVER happen
						//		normally).  On the other hand I suppose it's entirely possible that some taiwanese string
						//		might have no punctuation at all, so...
						//
						psBestLineBreakSrcPos = psLastGood_s;	// force a break after last good letter
					}

					sLineForDisplay[ psBestLineBreakSrcPos - psReadPosAtLineStart ] = '\0';
					psReadPosAtLineStart = psCurrentTextReadPos = psBestLineBreakSrcPos;

					// hack it to fit in with this code...
					//
					scrollPtr->pLines[ scrollPtr->iLineCount ] = String_Alloc( sLineForDisplay );
					break;	// print this line
				}
			}

			// record last-good linebreak pos...  (ie if we've just concat'd a punctuation point (western or asian) or space)
			//
			if (bIsTrailingPunctuation || uiLetter == ' ' || (uiLetter > 255 && !trap->R_Language_UsesSpaces()))
			{
				psBestLineBreakSrcPos = psCurrentTextReadPos;
			}
		}

		/// arrgghh, this is gettng horrible now...
		//
		if (scrollPtr->pLines[ scrollPtr->iLineCount ] == NULL && strlen(sLineForDisplay))
		{
			// then this is the last line and we've just run out of text, no CR, no overflow etc...
			//
			scrollPtr->pLines[ scrollPtr->iLineCount ] = String_Alloc( sLineForDisplay );
		}

		scrollPtr->iLineCount++;
	}

#else
	// old version...
	//
	int				 width;
	char*			 lineStart;
	char*			 lineEnd;
	float			 w;
	float			 cw;

	textScrollDef_t* scrollPtr = item->typeData.textscroll;

	scrollPtr->iLineCount = 0;
	width = scrollPtr->maxLineChars;
	lineStart = (char*)item->text;
	lineEnd   = lineStart;
	w		  = 0;

	// Keep going as long as there are more lines
	while ( scrollPtr->iLineCount < MAX_TEXTSCROLL_LINES )
	{
		// End of the road
		if ( *lineEnd == '\0')
		{
			if ( lineStart < lineEnd )
			{
				scrollPtr->pLines[ scrollPtr->iLineCount++ ] = lineStart;
			}

			break;
		}

		// Force a line end if its a '\n'
		else if ( *lineEnd == '\n' )
		{
			*lineEnd = '\0';
			scrollPtr->pLines[ scrollPtr->iLineCount++ ] = lineStart;
			lineStart = lineEnd + 1;
			lineEnd   = lineStart;
			w = 0;
			continue;
		}

		// Get the current character width
		cw = DC->textWidth ( va("%c", *lineEnd), item->textscale, item->iMenuFont );

		// Past the end of the boundary?
		if ( w + cw > (item->window.rect.w - SCROLLBAR_SIZE - 10) )
		{
			// Past the end so backtrack to the word boundary
			while ( *lineEnd != ' ' && *lineEnd != '\t' && lineEnd > lineStart )
			{
				lineEnd--;
			}

			*lineEnd = '\0';
			scrollPtr->pLines[ scrollPtr->iLineCount++ ] = lineStart;

			// Skip any whitespaces
			lineEnd++;
			while ( (*lineEnd == ' ' || *lineEnd == '\t') && *lineEnd )
			{
				lineEnd++;
			}

			lineStart = lineEnd;
			w = 0;
		}
		else
		{
			w += cw;
			lineEnd++;
		}
	}
#endif
}

// Item_InitControls
// init's special control types
void Item_InitControls(itemDef_t *item)
{
	if (item == NULL)
	{
		return;
	}

	if ( item->type == ITEM_TYPE_LISTBOX )
	{
		listBoxDef_t *listPtr = item->typeData.listbox;
		item->cursorPos = 0;
		if (listPtr)
		{
			listPtr->cursorPos = 0;
			listPtr->startPos = 0;
			listPtr->endPos = 0;
			listPtr->cursorPos = 0;
		}
	}
}

/*
===============
Menu Keyword Parse functions
===============
*/

qboolean MenuParse_font( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	if (!PC_String_Parse(handle, &menu->font)) {
		return qfalse;
	}
	if (!DC->Assets.fontRegistered) {
		//DC->registerFont(menu->font, 48, &DC->Assets.textFont);
		DC->Assets.qhMediumFont = DC->RegisterFont(menu->font);
		DC->Assets.fontRegistered = qtrue;
	}
	return qtrue;
}

qboolean MenuParse_name( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	if (!PC_String_Parse(handle, &menu->window.name)) {
		return qfalse;
	}
	if (Q_stricmp(menu->window.name, "main") == 0) {
		// default main as having focus
		//menu->window.flags |= WINDOW_HASFOCUS;
	}
	return qtrue;
}

// skin 1: the items of this menu are drawn by ui_skin.c. backdrop 1: its sky is behind the menu. openSlide
// <units>: the menu comes up from that far below when it opens.
qboolean MenuParse_skin( itemDef_t *item, int handle ) {
	menuDef_t	*menu = (menuDef_t *)item;
	int			i;

	if ( !PC_Int_Parse( handle, &i ) ) {
		return qfalse;
	}
	menu->skin = (qboolean)( i != 0 );
	return qtrue;
}

qboolean MenuParse_backdrop( itemDef_t *item, int handle ) {
	menuDef_t	*menu = (menuDef_t *)item;
	int			i;

	if ( !PC_Int_Parse( handle, &i ) ) {
		return qfalse;
	}
	menu->backdrop = (qboolean)( i != 0 );
	return qtrue;
}

qboolean MenuParse_openSlide( itemDef_t *item, int handle ) {
	return PC_Int_Parse( handle, &( (menuDef_t *)item )->openSlide );
}

// scrollRect <x y w h>: the window of the list of a page (the items with "scroll 1"), in the units of the menu.
// scrollStep <units>: the height of a row of it, which is what the list moves by.
qboolean MenuParse_scrollRect( itemDef_t *item, int handle ) {
	return PC_Rect_Parse( handle, &( (menuDef_t *)item )->scrollRect );
}

qboolean MenuParse_canvas( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t *)item;

	return (qboolean)( PC_Float_Parse( handle, &menu->canvasW ) && PC_Float_Parse( handle, &menu->canvasH ) );
}

qboolean MenuParse_scrollStep( itemDef_t *item, int handle ) {
	return PC_Float_Parse( handle, &( (menuDef_t *)item )->scrollStep );
}

qboolean MenuParse_searchCvar( itemDef_t *item, int handle ) {
	return PC_String_Parse( handle, &( (menuDef_t *)item )->searchCvar );
}

qboolean MenuParse_foldCvar( itemDef_t *item, int handle ) {
	return PC_String_Parse( handle, &( (menuDef_t *)item )->foldCvar );
}

qboolean MenuParse_advancedCvar( itemDef_t *item, int handle ) {
	return PC_String_Parse( handle, &( (menuDef_t *)item )->advancedCvar );
}

qboolean MenuParse_changesCvar( itemDef_t *item, int handle ) {
	return PC_String_Parse( handle, &( (menuDef_t *)item )->changesCvar );
}

qboolean MenuParse_fullscreen( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	byteAlias_t fullscreen;
	if (!PC_Int_Parse(handle, &fullscreen.i)) {
		return qfalse;
	}
	menu->fullScreen = fullscreen.qb;
	return qtrue;
}

qboolean MenuParse_rect( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	if (!PC_Rect_Parse(handle, &menu->window.rect)) {
		return qfalse;
	}
	return qtrue;
}

/*
=================
MenuParse_style
=================
*/
qboolean MenuParse_style( itemDef_t *item, int handle)
{
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Int_Parse(handle, &menu->window.style))
	{
		Com_Printf(S_COLOR_YELLOW "Unknown menu style value\n");
		return qfalse;
	}

	return qtrue;
}

qboolean MenuParse_visible( itemDef_t *item, int handle ) {
	int i;
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Int_Parse(handle, &i)) {
		return qfalse;
	}
	if (i) {
		menu->window.flags |= WINDOW_VISIBLE;
	}
	return qtrue;
}

qboolean MenuParse_onOpen( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	if (!PC_Script_Parse(handle, &menu->onOpen)) {
		return qfalse;
	}
	return qtrue;
}

qboolean MenuParse_onClose( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	if (!PC_Script_Parse(handle, &menu->onClose)) {
		return qfalse;
	}
	return qtrue;
}

//JLFACCEPT MPMOVED
/*
=================
MenuParse_onAccept
=================
*/
qboolean MenuParse_onAccept( itemDef_t *item, int handle )
{
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Script_Parse(handle, &menu->onAccept))
	{
		return qfalse;
	}
	return qtrue;
}

qboolean MenuParse_onESC( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	if (!PC_Script_Parse(handle, &menu->onESC)) {
		return qfalse;
	}
	return qtrue;
}



qboolean MenuParse_border( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	if (!PC_Int_Parse(handle, &menu->window.border)) {
		return qfalse;
	}
	return qtrue;
}

qboolean MenuParse_borderSize( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	if (!PC_Float_Parse(handle, &menu->window.borderSize)) {
		return qfalse;
	}
	return qtrue;
}

qboolean MenuParse_backcolor( itemDef_t *item, int handle ) {
	int i;
	float f;
	menuDef_t *menu = (menuDef_t*)item;

	for (i = 0; i < 4; i++) {
		if (!PC_Float_Parse(handle, &f)) {
			return qfalse;
		}
		menu->window.backColor[i]  = f;
	}
	return qtrue;
}

/*
=================
MenuParse_descAlignment
=================
*/
qboolean MenuParse_descAlignment( itemDef_t *item, int handle )
{
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Int_Parse(handle, &menu->descAlignment))
	{
		Com_Printf(S_COLOR_YELLOW "Unknown desc alignment value\n");
		return qfalse;
	}

	return qtrue;
}

qboolean MenuParse_descMiddle( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t *)item;

	return PC_Int_Parse( handle, &menu->descMiddle ) ? qtrue : qfalse;
}

/*
=================
MenuParse_descX
=================
*/
qboolean MenuParse_descX( itemDef_t *item, int handle )
{
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Float_Parse(handle, &menu->descX))
	{
		return qfalse;
	}
	return qtrue;
}

/*
=================
MenuParse_descY
=================
*/
qboolean MenuParse_descY( itemDef_t *item, int handle )
{
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Float_Parse(handle, &menu->descY))
	{
		return qfalse;
	}
	return qtrue;
}

/*
=================
MenuParse_descScale
=================
*/
qboolean MenuParse_descScale( itemDef_t *item, int handle)
{
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Float_Parse(handle, &menu->descScale))
	{
		return qfalse;
	}
	return qtrue;
}

/*
=================
MenuParse_descColor
=================
*/
qboolean MenuParse_descColor( itemDef_t *item, int handle)
{
	int i;
	float f;
	menuDef_t *menu = (menuDef_t*)item;

	for (i = 0; i < 4; i++)
	{
		if (!PC_Float_Parse(handle, &f))
		{
			return qfalse;
		}
		menu->descColor[i]  = f;
	}
	return qtrue;
}

qboolean MenuParse_forecolor( itemDef_t *item, int handle ) {
	int i;
	float f;
	menuDef_t *menu = (menuDef_t*)item;

	for (i = 0; i < 4; i++) {
		if (!PC_Float_Parse(handle, &f)) {
			return qfalse;
		}
		if (f < 0)
		{	//special case for player color
			menu->window.flags |= WINDOW_PLAYERCOLOR;
			return qtrue;
		}
		menu->window.foreColor[i]  = f;
		menu->window.flags |= WINDOW_FORECOLORSET;
	}
	return qtrue;
}

qboolean MenuParse_bordercolor( itemDef_t *item, int handle ) {
	int i;
	float f;
	menuDef_t *menu = (menuDef_t*)item;

	for (i = 0; i < 4; i++) {
		if (!PC_Float_Parse(handle, &f)) {
			return qfalse;
		}
		menu->window.borderColor[i]  = f;
	}
	return qtrue;
}

qboolean MenuParse_focuscolor( itemDef_t *item, int handle ) {
	int i;
	float f;
	menuDef_t *menu = (menuDef_t*)item;

	for (i = 0; i < 4; i++) {
		if (!PC_Float_Parse(handle, &f)) {
			return qfalse;
		}
		menu->focusColor[i]  = f;
	}
	return qtrue;
}

qboolean MenuParse_disablecolor( itemDef_t *item, int handle ) {
	int i;
	float f;
	menuDef_t *menu = (menuDef_t*)item;
	for (i = 0; i < 4; i++) {
		if (!PC_Float_Parse(handle, &f)) {
			return qfalse;
		}
		menu->disableColor[i]  = f;
	}
	return qtrue;
}


qboolean MenuParse_outlinecolor( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	if (!PC_Color_Parse(handle, &menu->window.outlineColor)){
		return qfalse;
	}
	return qtrue;
}

qboolean MenuParse_background( itemDef_t *item, int handle ) {
	pc_token_t token;
	menuDef_t *menu = (menuDef_t*)item;

	if (!trap->PC_ReadToken(handle, &token)) {
		return qfalse;
	}
	menu->window.background = DC->registerShaderNoMip(token.string);
	return qtrue;
}

qboolean MenuParse_cinematic( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_String_Parse(handle, &menu->window.cinematicName)) {
		return qfalse;
	}
	return qtrue;
}

qboolean MenuParse_ownerdrawFlag( itemDef_t *item, int handle ) {
	int i;
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Int_Parse(handle, &i)) {
		return qfalse;
	}
	menu->window.ownerDrawFlags |= i;
	return qtrue;
}

qboolean MenuParse_ownerdraw( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Int_Parse(handle, &menu->window.ownerDraw)) {
		return qfalse;
	}
	return qtrue;
}


// decoration
qboolean MenuParse_popup( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	menu->window.flags |= WINDOW_POPUP;
	return qtrue;
}


qboolean MenuParse_outOfBounds( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;

	menu->window.flags |= WINDOW_OOB_CLICK;
	return qtrue;
}

qboolean MenuParse_soundLoop( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_String_Parse(handle, &menu->soundName)) {
		return qfalse;
	}
	return qtrue;
}

qboolean MenuParse_fadeClamp( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Float_Parse(handle, &menu->fadeClamp)) {
		return qfalse;
	}
	return qtrue;
}

qboolean MenuParse_fadeAmount( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Float_Parse(handle, &menu->fadeAmount)) {
		return qfalse;
	}
	return qtrue;
}


qboolean MenuParse_fadeCycle( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Int_Parse(handle, &menu->fadeCycle)) {
		return qfalse;
	}
	return qtrue;
}


qboolean MenuParse_itemDef( itemDef_t *item, int handle ) {
	menuDef_t *menu = (menuDef_t*)item;
	if (menu->itemCount < MAX_MENUITEMS) {
		itemDef_t *newItem = menu->items[menu->itemCount] = (itemDef_t *) UI_Alloc(sizeof(itemDef_t));
		Item_Init(newItem);
		if (!Item_Parse(handle, newItem))
		{
			return qfalse;
		}
		Item_InitControls( newItem );
		newItem->parent = menu->items[menu->itemCount]->parent = menu;
		menu->itemCount++;
		Item_ApplyHacks( newItem );
	}
	return qtrue;
}
/*
=================
MenuParse_focuscolor
=================
*/
qboolean MenuParse_appearanceIncrement( itemDef_t *item, int handle )
{
	menuDef_t *menu = (menuDef_t*)item;

	if (!PC_Float_Parse(handle, &menu->appearanceIncrement))
	{
		return qfalse;
	}
	return qtrue;
}

keywordHash_t menuParseKeywords[] = {
	{"appearanceIncrement",	MenuParse_appearanceIncrement,	NULL	},
	{"backcolor",			MenuParse_backcolor,	NULL	},
	{"background",			MenuParse_background,	NULL	},
	{"border",				MenuParse_border,		NULL	},
	{"bordercolor",			MenuParse_bordercolor,	NULL	},
	{"borderSize",			MenuParse_borderSize,	NULL	},
	{"cinematic",			MenuParse_cinematic,	NULL	},
	{"descAlignment",		MenuParse_descAlignment,NULL	},
	{"descMiddle",			MenuParse_descMiddle,	NULL	},
	{"desccolor",			MenuParse_descColor,	NULL	},
	{"descX",				MenuParse_descX,		NULL	},
	{"descY",				MenuParse_descY,		NULL	},
	{"descScale",			MenuParse_descScale,	NULL	},
	{"disablecolor",		MenuParse_disablecolor, NULL	},
	{"fadeAmount",			MenuParse_fadeAmount,	NULL	},
	{"fadeClamp",			MenuParse_fadeClamp,	NULL	},
	{"fadeCycle",			MenuParse_fadeCycle,	NULL	},
	{"focuscolor",			MenuParse_focuscolor,	NULL	},
	{"font",				MenuParse_font,			NULL	},
	{"forecolor",			MenuParse_forecolor,	NULL	},
	{"fullscreen",			MenuParse_fullscreen,	NULL	},
	{"itemDef",				MenuParse_itemDef,		NULL	},
	{"name",				MenuParse_name,			NULL	},
	{"onClose",				MenuParse_onClose,		NULL	},
	//JLFACCEPT MPMOVED
	{"onAccept",			MenuParse_onAccept,		NULL    },

	{"onESC",				MenuParse_onESC,		NULL	},
	{"outOfBoundsClick",	MenuParse_outOfBounds,	NULL	},
	{"onOpen",				MenuParse_onOpen,		NULL	},
	{"outlinecolor",		MenuParse_outlinecolor, NULL	},
	{"ownerdraw",			MenuParse_ownerdraw,	NULL	},
	{"ownerdrawFlag",		MenuParse_ownerdrawFlag,NULL	},
	{"popup",				MenuParse_popup,		NULL	},
	{"rect",				MenuParse_rect,			NULL	},
	{"soundLoop",			MenuParse_soundLoop,	NULL	},
	{"skin",				MenuParse_skin,			NULL	},
	{"backdrop",			MenuParse_backdrop,		NULL	},
	{"openSlide",			MenuParse_openSlide,	NULL	},
	{"scrollRect",			MenuParse_scrollRect,	NULL	},
	{"canvas",				MenuParse_canvas,		NULL	},
	{"advancedCvar",		MenuParse_advancedCvar,	NULL	},
	{"changesCvar",			MenuParse_changesCvar,	NULL	},
	{"searchCvar",			MenuParse_searchCvar,	NULL	},
	{"foldCvar",			MenuParse_foldCvar,		NULL	},
	{"scrollStep",			MenuParse_scrollStep,	NULL	},
	{"radius",				ItemParse_radius,		NULL	},
	{"shadow",				ItemParse_shadow,		NULL	},
	{"sheen",				ItemParse_sheen,		NULL	},
	{"glowcolor",			ItemParse_glowcolor,	NULL	},
	{"style",				MenuParse_style,		NULL	},
	{"visible",				MenuParse_visible,		NULL	},
	{0,						0,						0		}
};

keywordHash_t *menuParseKeywordHash[KEYWORDHASH_SIZE];

/*
===============
Menu_SetupKeywordHash
===============
*/
void Menu_SetupKeywordHash(void) {
	int i;

	memset(menuParseKeywordHash, 0, sizeof(menuParseKeywordHash));
	for (i = 0; menuParseKeywords[i].keyword; i++) {
		KeywordHash_Add(menuParseKeywordHash, &menuParseKeywords[i]);
	}
}

/*
===============
Menu_Parse
===============
*/
qboolean Menu_Parse(int handle, menuDef_t *menu) {
	pc_token_t token;
	keywordHash_t *key;

	if (!trap->PC_ReadToken(handle, &token))
		return qfalse;
	if (*token.string != '{') {
		return qfalse;
	}

	while ( 1 ) {
		if (!trap->PC_ReadToken(handle, &token)) {
			PC_SourceError(handle, "end of file inside menu");
			return qfalse;
		}

		if (*token.string == '}') {
			return qtrue;
		}

		key = KeywordHash_Find(menuParseKeywordHash, token.string);
		if (!key) {
			PC_SourceError(handle, "unknown menu keyword %s", token.string);
			continue;
		}
		if ( !key->func((itemDef_t*)menu, handle) ) {
			PC_SourceError(handle, "couldn't parse menu keyword %s", token.string);
			return qfalse;
		}
	}
	return qfalse;
}

/*
===============
Menu_New
===============
*/
void Menu_New(int handle) {
	menuDef_t *menu = &Menus[menuCount];

	if (menuCount < MAX_MENUS) {
		Menu_Init(menu);
		if (Menu_Parse(handle, menu)) {
			Menu_PostParse(menu);
			menuCount++;
		}
	}
}

int Menu_Count() {
	return menuCount;
}

void Menu_PaintAll() {
	int i;
	if (captureFunc) {
		captureFunc(captureData);
	}

	for (i = 0; i < Menu_Count(); i++) {
		Menu_Paint(&Menus[i], qfalse);
	}

	if (debugMode) {
		vec4_t v = {1, 1, 1, 1};
		DC->drawText(5, 25, .75, v, va("fps: %f", DC->FPS), 0, 0, 0, 0);
		DC->drawText(5, 45, .75, v, va("x: %d  y:%d", DC->cursorx,DC->cursory), 0, 0, 0, 0);
	}
}

/*
=================
Menus_OpenBox

How much of the 640 x 480 the menus that are open take: the rectangle around every menu that paints a window of
its own and around every item of theirs that is shown. qfalse: nothing is open. The panel that stands behind a
menu in the level (cgame/menu3d) is as large as that: the bar at the top alone is a bar and no empty wall.
=================
*/
qboolean Menus_OpenBox( rectDef_t *box ) {
	float		x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
	qboolean	any = qfalse;
	int			i, j;

	for ( i = 0; i < menuCount; i++ ) {
		const menuDef_t *menu = &Menus[i];

		if ( !( menu->window.flags & WINDOW_VISIBLE )
			|| ( menu->window.ownerDrawFlags && DC->ownerDrawVisible && !DC->ownerDrawVisible( menu->window.ownerDrawFlags ) ) ) {
			continue;
		}
		// -1: the window of the menu itself, where it paints one
		for ( j = -1; j < menu->itemCount; j++ ) {
			const rectDef_t *r;

			if ( j < 0 ) {
				if ( !menu->fullScreen && !menu->backdrop && menu->window.style == WINDOW_STYLE_EMPTY && !menu->window.border ) {
					continue;
				}
				r = &menu->window.rect;
			} else {
				itemDef_t *item = menu->items[j];

				// (a row of a list that moves is inside the window of its menu wherever it is shown)
				if ( !( item->window.flags & WINDOW_VISIBLE ) || item->scroll || Item_OffPage( item )
					|| ( ( item->cvarFlags & ( CVAR_SHOW | CVAR_HIDE ) ) && !Item_EnableShowViaCvar( item, CVAR_SHOW ) ) ) {
					continue;
				}
				r = &item->window.rect;
			}
			if ( r->w <= 0.0f || r->h <= 0.0f ) {
				continue;
			}
			if ( !any ) {
				x0 = r->x;	y0 = r->y;	x1 = r->x + r->w;	y1 = r->y + r->h;
				any = qtrue;
			} else {
				x0 = Q_min( x0, r->x );				y0 = Q_min( y0, r->y );
				x1 = Q_max( x1, r->x + r->w );		y1 = Q_max( y1, r->y + r->h );
			}
		}
	}
	if ( !any ) {
		return qfalse;
	}
	box->x = Q_max( 0.0f, x0 );
	box->y = Q_max( 0.0f, y0 );
	box->w = Q_min( (float)SCREEN_WIDTH, x1 ) - box->x;
	box->h = Q_min( (float)SCREEN_HEIGHT, y1 ) - box->y;
	return (qboolean)( box->w > 0.0f && box->h > 0.0f );
}

void Menu_Reset(void) {
	menuCount = 0;
}

displayContextDef_t *Display_GetContext() {
	return DC;
}

void *Display_CaptureItem(int x, int y) {
	int i;

	for (i = 0; i < menuCount; i++) {
		// turn off focus each item
		// menu->items[i].window.flags &= ~WINDOW_HASFOCUS;
		if (Menu_ContainsPoint(&Menus[i], x, y)) {
			return &Menus[i];
		}
	}
	return NULL;
}


// FIXME:
qboolean Display_MouseMove(void *p, int x, int y) {
	int i;
	menuDef_t *menu = (menuDef_t *) p;

	if (menu == NULL) {
    menu = Menu_GetFocused();
		if (menu) {
			if (menu->window.flags & WINDOW_POPUP) {
				Menu_HandleMouseMove(menu, x, y);
				return qtrue;
			}
		}
		for (i = 0; i < menuCount; i++) {
			Menu_HandleMouseMove(&Menus[i], x, y);
		}
	} else {
		menu->window.rect.x += x;
		menu->window.rect.y += y;
		Menu_UpdatePosition(menu);
	}
 	return qtrue;

}

int Display_CursorType(int x, int y) {
	int i;
	for (i = 0; i < menuCount; i++) {
		rectDef_t r2;
		r2.x = Menus[i].window.rect.x - 3;
		r2.y = Menus[i].window.rect.y - 3;
		r2.w = r2.h = 7;
		if (Rect_ContainsPoint(&r2, x, y)) {
			return CURSOR_SIZER;
		}
	}
	return CURSOR_ARROW;
}


void Display_HandleKey(int key, qboolean down, int x, int y) {
	menuDef_t *menu = (menuDef_t *) Display_CaptureItem(x, y);
	if (menu == NULL) {
		menu = Menu_GetFocused();
	}
	if (menu) {
		Menu_HandleKey(menu, key, down );
	}
}

static void Window_CacheContents(windowDef_t *window) {
	if (window) {
		if (window->cinematicName) {
			int cin = DC->playCinematic(window->cinematicName, 0, 0, 0, 0);
			DC->stopCinematic(cin);
		}
	}
}


static void Item_CacheContents(itemDef_t *item) {
	if (item) {
		Window_CacheContents(&item->window);
	}

}

static void Menu_CacheContents(menuDef_t *menu) {
	if (menu) {
		int i;
		Window_CacheContents(&menu->window);
		for (i = 0; i < menu->itemCount; i++) {
			Item_CacheContents(menu->items[i]);
		}

		if (menu->soundName && *menu->soundName) {
			DC->registerSound(menu->soundName);
		}
	}

}

void Display_CacheAll() {
	int i;
	for (i = 0; i < menuCount; i++) {
		Menu_CacheContents(&Menus[i]);
	}
}


static qboolean Menu_OverActiveItem(menuDef_t *menu, float x, float y) {
 	if (menu && menu->window.flags & (WINDOW_VISIBLE | WINDOW_FORCED)) {
		if (Menu_ContainsPoint(menu, x, y)) {
			int i;
			for (i = 0; i < menu->itemCount; i++) {
				// turn off focus each item
				// menu->items[i].window.flags &= ~WINDOW_HASFOCUS;

				if (!(menu->items[i]->window.flags & (WINDOW_VISIBLE | WINDOW_FORCED))) {
					continue;
				}

				if (menu->items[i]->window.flags & WINDOW_DECORATION) {
					continue;
				}

				if ( Item_Away( menu->items[i] ) ) {
					continue;
				}

				if (Rect_ContainsPoint(&menu->items[i]->window.rect, x, y)) {
					itemDef_t *overItem = menu->items[i];
					if (overItem->type == ITEM_TYPE_TEXT && overItem->text) {
						if (Rect_ContainsPoint(&overItem->window.rect, x, y)) {
							return qtrue;
						} else {
							continue;
						}
					} else {
						return qtrue;
					}
				}
			}

		}
	}
	return qfalse;
}
