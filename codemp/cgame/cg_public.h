/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
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

#pragma once

#include "qcommon/spatial_debug.h"

#define	CGAME_API_VERSION		2

// the videos on surfaces of the level (ext.Video_*, client/cl_video.cpp): streams, and what one does
#define VIDEO_STREAMS		8		// 0 to 3 for the TVs of the boombox, 4 to 7 for the drawing boards (Video_Picture)
#define VIDEO_NONE			0
#define VIDEO_FETCHING		1		// opened, nothing to show yet
#define VIDEO_PLAYING		2
#define VIDEO_ENDED			3		// all of it came and was shown
#define VIDEO_FAILED		4		// nothing came, or what came was refused

#define	CMD_BACKUP			64
#define	CMD_MASK			(CMD_BACKUP - 1)
// allow a lot of command backups for very fast systems
// multiple commands may be combined into a single packet, so this
// needs to be larger than PACKET_BACKUP


#define	MAX_ENTITIES_IN_SNAPSHOT	256

// snapshots are a view of the server at a given time

// Snapshots are generated at regular time intervals by the server,
// but they may not be sent if a client's rate level is exceeded, or
// they may be dropped by the network.
typedef struct snapshot_s {
	int				snapFlags;			// SNAPFLAG_RATE_DELAYED, etc
	int				ping;

	int				serverTime;		// server time the message is valid for (in msec)

	byte			areamask[MAX_MAP_AREA_BYTES];		// portalarea visibility bits

	playerState_t	ps;						// complete information about the current player at this time
	playerState_t	vps; //vehicle I'm riding's playerstate (if applicable) -rww

	int				numEntities;			// all of the entities that need to be presented
	entityState_t	entities[MAX_ENTITIES_IN_SNAPSHOT];	// at the time of this snapshot

	int				numServerCommands;		// text based server commands to execute when this
	int				serverCommandSequence;	// snapshot becomes current
} snapshot_t;

typedef enum cgameEvent_e {
	CGAME_EVENT_NONE=0,
	CGAME_EVENT_TEAMMENU,
	CGAME_EVENT_SCOREBOARD,
	CGAME_EVENT_EDITHUD
} cgameEvent_t;

typedef struct autoMapInput_s {
	float		up;
	float		down;
	float		yaw;
	float		pitch;
	qboolean	goToDefaults;
} autoMapInput_t;

// CG_POINT_CONTENTS
typedef struct TCGPointContents_s {
	vec3_t		mPoint;			// input
	int			mPassEntityNum;	// input
} TCGPointContents;

// CG_GET_BOLT_POS
typedef struct TCGGetBoltData_s {
	vec3_t		mOrigin;		// output
	vec3_t		mAngles;		// output
	vec3_t		mScale;			// output
	int			mEntityNum;		// input
} TCGGetBoltData;

// CG_IMPACT_MARK
typedef struct TCGImpactMark_s {
	int		mHandle;
	vec3_t	mPoint;
	vec3_t	mAngle;
	float	mRotation;
	float	mRed;
	float	mGreen;
	float	mBlue;
	float	mAlphaStart;
	float	mSizeStart;
} TCGImpactMark;

// CG_GET_LERP_ORIGIN
// CG_GET_LERP_ANGLES
// CG_GET_MODEL_SCALE
typedef struct TCGVectorData_s {
	int			mEntityNum;		// input
	vec3_t		mPoint;			// output
} TCGVectorData;

// CG_TRACE/CG_G2TRACE
typedef struct TCGTrace_s {
	trace_t mResult;					// output
	vec3_t	mStart, mMins, mMaxs, mEnd;	// input
	int		mSkipNumber, mMask;			// input
} TCGTrace;

// CG_G2MARK
typedef struct TCGG2Mark_s {
	int			shader;
	float		size;
	vec3_t		start, dir;
} TCGG2Mark;

// CG_INCOMING_CONSOLE_COMMAND
typedef struct TCGIncomingConsoleCommand_s {
	char conCommand[1024];
} TCGIncomingConsoleCommand;

// CG_FX_CAMERASHAKE
typedef struct TCGCameraShake_s {
	vec3_t	mOrigin;					// input
	float	mIntensity;					// input
	int		mRadius;					// input
	int		mTime;						// input
} TCGCameraShake;

// CG_MISC_ENT
typedef struct TCGMiscEnt_s {
	char	mModel[MAX_QPATH];			// input
	vec3_t	mOrigin, mAngles, mScale;	// input
} TCGMiscEnt;

typedef struct TCGPositionOnBolt_s {
	refEntity_t		ent;				// output
	void			*ghoul2;			// input
	int				modelIndex;			// input
	int				boltIndex;			// input
	vec3_t			origin;				// input
	vec3_t			angles;				// input
	vec3_t			modelScale;			// input
} TCGPositionOnBolt;

//ragdoll callback structs -rww
#define RAG_CALLBACK_NONE				0
#define RAG_CALLBACK_DEBUGBOX			1
typedef struct ragCallbackDebugBox_s {
	vec3_t			mins;
	vec3_t			maxs;
	int				duration;
} ragCallbackDebugBox_t;

#define RAG_CALLBACK_DEBUGLINE			2
typedef struct ragCallbackDebugLine_s {
	vec3_t			start;
	vec3_t			end;
	int				time;
	int				color;
	int				radius;
} ragCallbackDebugLine_t;

#define RAG_CALLBACK_BONESNAP			3
typedef struct ragCallbackBoneSnap_s {
	char			boneName[128]; //name of the bone in question
	int				entNum; //index of entity who owns the bone in question
} ragCallbackBoneSnap_t;

#define RAG_CALLBACK_BONEIMPACT			4
typedef struct ragCallbackBoneImpact_s {
	char			boneName[128]; //name of the bone in question
	int				entNum; //index of entity who owns the bone in question
} ragCallbackBoneImpact_t;

#define RAG_CALLBACK_BONEINSOLID		5
typedef struct ragCallbackBoneInSolid_s {
	vec3_t			bonePos; //world coordinate position of the bone
	int				entNum; //index of entity who owns the bone in question
	int				solidCount; //higher the count, the longer we've been in solid (the worse off we are)
} ragCallbackBoneInSolid_t;

#define RAG_CALLBACK_TRACELINE			6
typedef struct ragCallbackTraceLine_s {
	trace_t			tr;
	vec3_t			start;
	vec3_t			end;
	vec3_t			mins;
	vec3_t			maxs;
	int				ignore;
	int				mask;
} ragCallbackTraceLine_t;

#define	MAX_CG_SHARED_BUFFER_SIZE		2048

typedef enum cgameImportLegacy_e {
	CG_PRINT = 0,
	CG_ERROR,
	CG_MILLISECONDS,
	CG_PRECISIONTIMER_START,
	CG_PRECISIONTIMER_END,
	CG_CVAR_REGISTER,
	CG_CVAR_UPDATE,
	CG_CVAR_SET,
	CG_CVAR_VARIABLESTRINGBUFFER,
	CG_CVAR_GETHIDDENVALUE,
	CG_ARGC,
	CG_ARGV,
	CG_ARGS,
	CG_FS_FOPENFILE,
	CG_FS_READ,
	CG_FS_WRITE,
	CG_FS_FCLOSEFILE,
	CG_FS_GETFILELIST,
	CG_SENDCONSOLECOMMAND,
	CG_ADDCOMMAND,
	CG_REMOVECOMMAND,
	CG_SENDCLIENTCOMMAND,
	CG_UPDATESCREEN,
	CG_CM_LOADMAP,
	CG_CM_NUMINLINEMODELS,
	CG_CM_INLINEMODEL,
	CG_CM_TEMPBOXMODEL,
	CG_CM_TEMPCAPSULEMODEL,
	CG_CM_POINTCONTENTS,
	CG_CM_TRANSFORMEDPOINTCONTENTS,
	CG_CM_BOXTRACE,
	CG_CM_CAPSULETRACE,
	CG_CM_TRANSFORMEDBOXTRACE,
	CG_CM_TRANSFORMEDCAPSULETRACE,
	CG_CM_MARKFRAGMENTS,
	CG_S_GETVOICEVOLUME,
	CG_S_MUTESOUND,
	CG_S_STARTSOUND,
	CG_S_STARTLOCALSOUND,
	CG_S_CLEARLOOPINGSOUNDS,
	CG_S_ADDLOOPINGSOUND,
	CG_S_UPDATEENTITYPOSITION,
	CG_S_ADDREALLOOPINGSOUND,
	CG_S_STOPLOOPINGSOUND,
	CG_S_RESPATIALIZE,
	CG_S_SHUTUP,
	CG_S_REGISTERSOUND,
	CG_S_STARTBACKGROUNDTRACK,
	CG_S_UPDATEAMBIENTSET,
	CG_AS_PARSESETS,
	CG_AS_ADDPRECACHEENTRY,
	CG_S_ADDLOCALSET,
	CG_AS_GETBMODELSOUND,
	CG_R_LOADWORLDMAP,
	CG_R_REGISTERMODEL,
	CG_R_REGISTERSKIN,
	CG_R_REGISTERSHADER,
	CG_R_REGISTERSHADERNOMIP,
	CG_R_REGISTERFONT,
	CG_R_FONT_STRLENPIXELS,
	CG_R_FONT_STRLENCHARS,
	CG_R_FONT_STRHEIGHTPIXELS,
	CG_R_FONT_DRAWSTRING,
	CG_LANGUAGE_ISASIAN,
	CG_LANGUAGE_USESSPACES,
	CG_ANYLANGUAGE_READCHARFROMSTRING,

	CGAME_MEMSET = 100,
	CGAME_MEMCPY,
	CGAME_STRNCPY,
	CGAME_SIN,
	CGAME_COS,
	CGAME_ATAN2,
	CGAME_SQRT,
	CGAME_MATRIXMULTIPLY,
	CGAME_ANGLEVECTORS,
	CGAME_PERPENDICULARVECTOR,
	CGAME_FLOOR,
	CGAME_CEIL,
	CGAME_TESTPRINTINT,
	CGAME_TESTPRINTFLOAT,
	CGAME_ACOS,
	CGAME_ASIN,

	CG_R_CLEARSCENE = 200,
	CG_R_CLEARDECALS,
	CG_R_ADDREFENTITYTOSCENE,
	CG_R_ADDPOLYTOSCENE,
	CG_R_ADDPOLYSTOSCENE,
	CG_R_ADDDECALTOSCENE,
	CG_R_LIGHTFORPOINT,
	CG_R_ADDLIGHTTOSCENE,
	CG_R_ADDADDITIVELIGHTTOSCENE,
	CG_R_RENDERSCENE,
	CG_R_SETCOLOR,
	CG_R_DRAWSTRETCHPIC,
	CG_R_MODELBOUNDS,
	CG_R_LERPTAG,
	CG_R_DRAWROTATEPIC,
	CG_R_DRAWROTATEPIC2,
	CG_R_SETRANGEFOG,
	CG_R_SETREFRACTIONPROP,
	CG_R_REMAP_SHADER,
	CG_R_GET_LIGHT_STYLE,
	CG_R_SET_LIGHT_STYLE,
	CG_R_GET_BMODEL_VERTS,
	CG_R_GETDISTANCECULL,
	CG_R_GETREALRES,
	CG_R_AUTOMAPELEVADJ,
	CG_R_INITWIREFRAMEAUTO,
	CG_FX_ADDLINE,
	CG_GETGLCONFIG,
	CG_GETGAMESTATE,
	CG_GETCURRENTSNAPSHOTNUMBER,
	CG_GETSNAPSHOT,
	CG_GETDEFAULTSTATE,
	CG_GETSERVERCOMMAND,
	CG_GETCURRENTCMDNUMBER,
	CG_GETUSERCMD,
	CG_SETUSERCMDVALUE,
	CG_SETCLIENTFORCEANGLE,
	CG_SETCLIENTTURNEXTENT,
	CG_OPENUIMENU,
	CG_TESTPRINTINT,
	CG_TESTPRINTFLOAT,
	CG_MEMORY_REMAINING,
	CG_KEY_ISDOWN,
	CG_KEY_GETCATCHER,
	CG_KEY_SETCATCHER,
	CG_KEY_GETKEY,
 	CG_PC_ADD_GLOBAL_DEFINE,
	CG_PC_LOAD_SOURCE,
	CG_PC_FREE_SOURCE,
	CG_PC_READ_TOKEN,
	CG_PC_SOURCE_FILE_AND_LINE,
	CG_PC_LOAD_GLOBAL_DEFINES,
	CG_PC_REMOVE_ALL_GLOBAL_DEFINES,
	CG_S_STOPBACKGROUNDTRACK,
	CG_REAL_TIME,
	CG_SNAPVECTOR,
	CG_CIN_PLAYCINEMATIC,
	CG_CIN_STOPCINEMATIC,
	CG_CIN_RUNCINEMATIC,
	CG_CIN_DRAWCINEMATIC,
	CG_CIN_SETEXTENTS,
	CG_GET_ENTITY_TOKEN,
	CG_R_INPVS,
	CG_FX_REGISTER_EFFECT,
	CG_FX_PLAY_EFFECT,
	CG_FX_PLAY_ENTITY_EFFECT,
	CG_FX_PLAY_EFFECT_ID,
	CG_FX_PLAY_PORTAL_EFFECT_ID,
	CG_FX_PLAY_ENTITY_EFFECT_ID,
	CG_FX_PLAY_BOLTED_EFFECT_ID,
	CG_FX_ADD_SCHEDULED_EFFECTS,
	CG_FX_INIT_SYSTEM,
	CG_FX_SET_REFDEF,
	CG_FX_FREE_SYSTEM,
	CG_FX_ADJUST_TIME,
	CG_FX_DRAW_2D_EFFECTS,
	CG_FX_RESET,
	CG_FX_ADDPOLY,
	CG_FX_ADDBEZIER,
	CG_FX_ADDPRIMITIVE,
	CG_FX_ADDSPRITE,
	CG_FX_ADDELECTRICITY,
	CG_SP_GETSTRINGTEXTSTRING,
	CG_ROFF_CLEAN,
	CG_ROFF_UPDATE_ENTITIES,
	CG_ROFF_CACHE,
	CG_ROFF_PLAY,
	CG_ROFF_PURGE_ENT,
	CG_TRUEMALLOC,
	CG_TRUEFREE,
	CG_G2_LISTSURFACES,
	CG_G2_LISTBONES,
	CG_G2_SETMODELS,
	CG_G2_HAVEWEGHOULMODELS,
	CG_G2_GETBOLT,
	CG_G2_GETBOLT_NOREC,
	CG_G2_GETBOLT_NOREC_NOROT,
	CG_G2_INITGHOUL2MODEL,
	CG_G2_SETSKIN,
	CG_G2_COLLISIONDETECT,
	CG_G2_COLLISIONDETECTCACHE,
	CG_G2_CLEANMODELS,
	CG_G2_ANGLEOVERRIDE,
	CG_G2_PLAYANIM,
	CG_G2_GETBONEANIM,
	CG_G2_GETBONEFRAME,
	CG_G2_GETGLANAME,
	CG_G2_COPYGHOUL2INSTANCE,
	CG_G2_COPYSPECIFICGHOUL2MODEL,
	CG_G2_DUPLICATEGHOUL2INSTANCE,
	CG_G2_HASGHOUL2MODELONINDEX,
	CG_G2_REMOVEGHOUL2MODEL,
	CG_G2_SKINLESSMODEL,
	CG_G2_GETNUMGOREMARKS,
	CG_G2_ADDSKINGORE,
	CG_G2_CLEARSKINGORE,
	CG_G2_SIZE,
	CG_G2_ADDBOLT,
	CG_G2_ATTACHENT,
	CG_G2_SETBOLTON,
	CG_G2_SETROOTSURFACE,
	CG_G2_SETSURFACEONOFF,
	CG_G2_SETNEWORIGIN,
	CG_G2_DOESBONEEXIST,
	CG_G2_GETSURFACERENDERSTATUS,
	CG_G2_GETTIME,
	CG_G2_SETTIME,
	CG_G2_ABSURDSMOOTHING,
	CG_G2_SETRAGDOLL,
	CG_G2_ANIMATEG2MODELS,
	CG_G2_RAGPCJCONSTRAINT,
	CG_G2_RAGPCJGRADIENTSPEED,
	CG_G2_RAGEFFECTORGOAL,
	CG_G2_GETRAGBONEPOS,
	CG_G2_RAGEFFECTORKICK,
	CG_G2_RAGFORCESOLVE,
	CG_G2_SETBONEIKSTATE,
	CG_G2_IKMOVE,
	CG_G2_REMOVEBONE,
	CG_G2_ATTACHINSTANCETOENTNUM,
	CG_G2_CLEARATTACHEDINSTANCE,
	CG_G2_CLEANENTATTACHMENTS,
	CG_G2_OVERRIDESERVER,
	CG_G2_GETSURFACENAME,
	CG_SET_SHARED_BUFFER,
	CG_CM_REGISTER_TERRAIN,
	CG_RMG_INIT,
	CG_RE_INIT_RENDERER_TERRAIN,
	CG_R_WEATHER_CONTENTS_OVERRIDE,
	CG_R_WORLDEFFECTCOMMAND,
	CG_WE_ADDWEATHERZONE
} cgameImportLegacy_t;

typedef enum cgameExportLegacy_e {
	CG_INIT,
	CG_SHUTDOWN,
	CG_CONSOLE_COMMAND,
	CG_DRAW_ACTIVE_FRAME,
	CG_CROSSHAIR_PLAYER,
	CG_LAST_ATTACKER,
	CG_KEY_EVENT,
	CG_MOUSE_EVENT,
	CG_EVENT_HANDLING,
	CG_POINT_CONTENTS,
	CG_GET_LERP_ORIGIN,
	CG_GET_LERP_DATA,
	CG_GET_GHOUL2,
	CG_GET_MODEL_LIST,
	CG_CALC_LERP_POSITIONS,
	CG_TRACE,
	CG_G2TRACE,
	CG_G2MARK,
	CG_RAG_CALLBACK,
	CG_INCOMING_CONSOLE_COMMAND,
	CG_GET_USEABLE_FORCE,
	CG_GET_ORIGIN,
	CG_GET_ANGLES,
	CG_GET_ORIGIN_TRAJECTORY,
	CG_GET_ANGLE_TRAJECTORY,
	CG_ROFF_NOTETRACK_CALLBACK,
	CG_IMPACT_MARK,
	CG_MAP_CHANGE,
	CG_AUTOMAP_INPUT,
	CG_MISC_ENT,
	CG_GET_SORTED_FORCE_POWER,
	CG_FX_CAMERASHAKE,
} cgameExportLegacy_t;

typedef struct cgameImport_s {
	// common
	void			(*Print)								( const char *msg, ... );
	NORETURN_PTR void (*Error)( int level, const char *fmt, ... );
	void			(*SnapVector)							( float *v );
	int				(*MemoryRemaining)						( void );
	void			(*RegisterSharedMemory)					( char *memory );
	void			(*TrueMalloc)							( void **ptr, int size );
	void			(*TrueFree)								( void **ptr );

	// timing
	int				(*Milliseconds)							( void );
	int				(*RealTime)								( qtime_t *qtime );
	void			(*PrecisionTimerStart)					( void **timer );
	int				(*PrecisionTimerEnd)					( void *timer );

	// cvar
	void			(*Cvar_Register)						( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, uint32_t flags );
	void			(*Cvar_Set)								( const char *var_name, const char *value );
	void			(*Cvar_Update)							( vmCvar_t *vmCvar );
	void			(*Cvar_VariableStringBuffer)			( const char *var_name, char *buffer, int bufsize );

	// command
	void			(*AddCommand)							( const char *cmd_name );
	int				(*Cmd_Argc)								( void );
	void			(*Cmd_Args)								( char *buffer, int bufferLength );
	void			(*Cmd_Argv)								( int arg, char *buffer, int bufferLength );
	void			(*RemoveCommand)						( const char *cmd_name );
	void			(*SendClientCommand)					( const char *cmd );
	void			(*SendConsoleCommand)					( const char *text );

	// filesystem
	void			(*FS_Close)								( fileHandle_t f );
	int				(*FS_GetFileList)						( const char *path, const char *extension, char *listbuf, int bufsize );
	int				(*FS_Open)								( const char *qpath, fileHandle_t *f, fsMode_t mode );
	int				(*FS_Read)								( void *buffer, int len, fileHandle_t f );
	int				(*FS_Write)								( const void *buffer, int len, fileHandle_t f );

	// screen
	void			(*UpdateScreen)							( void );

	// clip model
	clipHandle_t	(*CM_InlineModel)						( int index );
	void			(*CM_LoadMap)							( const char *mapname, qboolean subBSP );
	int				(*CM_NumInlineModels)					( void );
	int				(*CM_PointContents)						( const vec3_t p, clipHandle_t model );
	int				(*CM_RegisterTerrain)					( const char *config );
	clipHandle_t	(*CM_TempModel)							( const vec3_t mins, const vec3_t maxs, int capsule );
	void			(*CM_Trace)								( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, clipHandle_t model, int brushmask, int capsule );
	int				(*CM_TransformedPointContents)			( const vec3_t p, clipHandle_t model, const vec3_t origin, const vec3_t angles );
	void			(*CM_TransformedTrace)					( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, clipHandle_t model, int brushmask, const vec3_t origin, const vec3_t angles, int capsule );
	void			(*RMG_Init)								( int terrainID, const char *terrainInfo );

	// sound
	int				(*S_AddLocalSet)						( const char *name, vec3_t listener_origin, vec3_t origin, int entID, int time );
	void			(*S_AddLoopingSound)					( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
	void			(*S_ClearLoopingSounds)					( void );
	int				(*S_GetVoiceVolume)						( int entID );
	void			(*S_MuteSound)							( int entityNum, int entchannel );
	sfxHandle_t		(*S_RegisterSound)						( const char *sample );
	void			(*S_Respatialize)						( int entityNum, const vec3_t origin, matrix3_t axis, int inwater );
	void			(*S_Shutup)								( qboolean shutup );
	void			(*S_StartBackgroundTrack)				( const char *intro, const char *loop, qboolean bReturnWithoutStarting );
	void			(*S_StartLocalSound)					( sfxHandle_t sfx, int channelNum );
	void			(*S_StartSound)							( const vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx );
	void			(*S_StopBackgroundTrack)				( void );
	void			(*S_StopLoopingSound)					( int entityNum );
	void			(*S_UpdateEntityPosition)				( int entityNum, const vec3_t origin );
	void			(*S_UpdateAmbientSet)					( const char *name, vec3_t origin ) ;

	// ambient sound
	void			(*AS_AddPrecacheEntry)					( const char *name );
	sfxHandle_t		(*AS_GetBModelSound)					( const char *name, int stage );
	void			(*AS_ParseSets)							( void );

	// renderer
	void			(*R_AddAdditiveLightToScene)			( const vec3_t org, float intensity, float r, float g, float b );
	void			(*R_AddDecalToScene)					( qhandle_t shader, const vec3_t origin, const vec3_t dir, float orientation, float r, float g, float b, float a, qboolean alphaFade, float radius, qboolean temporary );
	void			(*R_AddLightToScene)					( const vec3_t org, float intensity, float r, float g, float b );
	void			(*R_AddPolysToScene)					( qhandle_t hShader , int numVerts, const polyVert_t *verts, int num );
	void			(*R_AddRefEntityToScene)				( const refEntity_t *re );
	unsigned int	(*R_AnyLanguage_ReadCharFromString)		( const char *psText, int *piAdvanceCount, qboolean *pbIsTrailingPunctuation );
	void			(*R_AutomapElevationAdjustment)			( float newHeight );
	void			(*R_ClearDecals)						( void );
	void			(*R_ClearScene)							( void );
	void			(*R_DrawStretchPic)						( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader );	// 0 = white
	void			(*R_DrawRotatePic)						( float x, float y, float w, float h, float s1, float t1, float s2, float t2, float a1, qhandle_t hShader );	// 0 = white
	void			(*R_DrawRotatePic2)						( float x, float y, float w, float h, float s1, float t1, float s2, float t2, float a1, qhandle_t hShader );	// 0 = white
	void			(*R_Font_DrawString)					( int ox, int oy, const char *text, const float *rgba, const int setIndex, int iCharLimit, const float scale );
	int				(*R_Font_HeightPixels)					( const int iFontIndex, const float scale );
	int				(*R_Font_StrLenChars)					( const char *text );
	int				(*R_Font_StrLenPixels)					( const char *text, const int iFontIndex, const float scale );
	void			(*R_GetBModelVerts)						( int bmodelIndex, vec3_t *vec, vec3_t normal );
	float			(*R_GetDistanceCull)					( void );
	qboolean		(*R_GetEntityToken)						( char *buffer, int size );
	void			(*R_GetLightStyle)						( int style, color4ub_t color );
	void			(*R_GetRealRes)							( int *w, int *h );
	qboolean		(*R_InitializeWireframeAutomap)			( void );
	qboolean		(*R_InPVS)								( const vec3_t p1, const vec3_t p2, byte *mask );
	qboolean		(*R_Language_IsAsian)					( void );
	qboolean		(*R_Language_UsesSpaces)				( void );
	int				(*R_LerpTag)							( orientation_t *tag,  qhandle_t model, int startFrame, int endFrame, float frac, const char *tagName );
	int				(*R_LightForPoint)						( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir );
	void			(*R_LoadWorld)							( const char *name );
	int				(*R_MarkFragments)						( int numPoints, const vec3_t *points, const vec3_t projection, int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer );
	void			(*R_ModelBounds)						( qhandle_t model, vec3_t mins, vec3_t maxs );
	qhandle_t		(*R_RegisterFont)						( const char *fontName );
	qhandle_t		(*R_RegisterModel)						( const char *name );
	qhandle_t		(*R_RegisterShader)						( const char *name );
	qhandle_t		(*R_RegisterShaderNoMip)				( const char *name );
	qhandle_t		(*R_RegisterSkin)						( const char *name );
	void			(*R_RemapShader)						( const char *oldShader, const char *newShader, const char *offsetTime );
	void			(*R_RenderScene)						( const refdef_t *fd );
	void			(*R_SetColor)							( const float *rgba );	// NULL = 1,1,1,1
	void			(*R_SetLightStyle)						( int style, int color );
	void			(*R_SetRangedFog)						( float range );
	void			(*R_SetRefractionProperties)			( float distortionAlpha, float distortionStretch, qboolean distortionPrePost, qboolean distortionNegate );
	void			(*R_WorldEffectCommand)					( const char *cmd );
	void			(*RE_InitRendererTerrain)				( const char *info );
	void			(*WE_AddWeatherZone)					( vec3_t mins, vec3_t maxs );

	// client
	void			(*GetCurrentSnapshotNumber)				( int *snapshotNumber, int *serverTime );
	int				(*GetCurrentCmdNumber)					( void );
	qboolean		(*GetDefaultState)						( int index, entityState_t *state );
	void			(*GetGameState)							( gameState_t *gs );
	void			(*GetGlconfig)							( glconfig_t *glconfig );
	qboolean		(*GetServerCommand)						( int serverCommandNumber );
	qboolean		(*GetSnapshot)							( int snapshotNumber, snapshot_t *snapshot );
	qboolean		(*GetUserCmd)							( int cmdNumber, usercmd_t *ucmd );
	void			(*OpenUIMenu)							( int menu );
	void			(*SetClientForceAngle)					( int time, vec3_t angle );
	void			(*SetUserCmdValue)						( int stateValue, float sensitivityScale, float mPitchOverride, float mYawOverride, float mSensitivityOverride, int fpSel, int invenSel, qboolean fighterControls );

	// keys
	int				(*Key_GetCatcher)						( void );
	int				(*Key_GetKey)							( const char *binding );
	qboolean		(*Key_IsDown)							( int keynum );
	void			(*Key_SetCatcher)						( int catcher );

	// preprocessor (botlib_export->PC_***)
	int				(*PC_AddGlobalDefine)					( char *string );
	int				(*PC_FreeSource)						( int handle );
	int				(*PC_LoadGlobalDefines)					( const char *filename );
	int				(*PC_LoadSource)						( const char *filename );
	int				(*PC_ReadToken)							( int handle, pc_token_t *pc_token );
	void			(*PC_RemoveAllGlobalDefines)			( void );
	int				(*PC_SourceFileAndLine)					( int handle, char *filename, int *line );

	// cinematics
	void			(*CIN_DrawCinematic)					( int handle );
	int				(*CIN_PlayCinematic)					( const char *arg, int x, int y, int w, int h, int systemBits );
	e_status		(*CIN_RunCinematic)						( int handle );
	void			(*CIN_SetExtents)						( int handle, int x, int y, int w, int h );
	e_status		(*CIN_StopCinematic)					( int handle );

	// FX
	void			(*FX_AddLine)							( vec3_t start, vec3_t end, float size1, float size2, float sizeParm, float alpha1, float alpha2, float alphaParm, vec3_t sRGB, vec3_t eRGB, float rgbParm, int killTime, qhandle_t shader, int flags );
	int				(*FX_RegisterEffect)					( const char *file );
	void			(*FX_PlayEffect)						( const char *file, vec3_t org, vec3_t fwd, int vol, int rad );
	void			(*FX_PlayEffectID)						( int id, vec3_t org, vec3_t fwd, int vol, int rad, qboolean isPortal );
	void			(*FX_PlayEntityEffectID)				( int id, vec3_t org, matrix3_t axis, const int boltInfo, const int entNum, int vol, int rad );
	qboolean		(*FX_PlayBoltedEffectID)				( int id, vec3_t org, void *pGhoul2, const int boltNum, const int entNum, const int modelNum, int iLooptime, qboolean isRelative );
	void			(*FX_AddScheduledEffects)				( qboolean portal );
	int				(*FX_InitSystem)						( refdef_t *refdef );
	void			(*FX_SetRefDef)							( refdef_t *refdef );
	qboolean		(*FX_FreeSystem)						( void );
	void			(*FX_AdjustTime)						( int time );
	void			(*FX_Draw2DEffects)						( float screenXScale, float screenYScale );
	void			(*FX_AddPoly)							( addpolyArgStruct_t *p );
	void			(*FX_AddBezier)							( addbezierArgStruct_t *p );
	void			(*FX_AddPrimitive)						( effectTrailArgStruct_t *p );
	void			(*FX_AddSprite)							( addspriteArgStruct_t *p );
	void			(*FX_AddElectricity)					( addElectricityArgStruct_t *p );

	// stringed
	qboolean		(*SE_GetStringTextString)				( const char *text, char *buffer, int bufferLength );

	// roff
	qboolean		(*ROFF_Clean)							( void );
	void			(*ROFF_UpdateEntities)					( void );
	int				(*ROFF_Cache)							( char *file );
	qboolean		(*ROFF_Play)							( int entID, int roffID, qboolean doTranslation );
	qboolean		(*ROFF_Purge_Ent)						( int entID );

	// ghoul2
	void			(*G2_ListModelSurfaces)					( void *ghlInfo );
	void			(*G2_ListModelBones)					( void *ghlInfo, int frame );
	void			(*G2_SetGhoul2ModelIndexes)				( void *ghoul2, qhandle_t *modelList, qhandle_t *skinList );
	qboolean		(*G2_HaveWeGhoul2Models)				( void *ghoul2 );
	qboolean		(*G2API_GetBoltMatrix)					( void *ghoul2, const int modelIndex, const int boltIndex, mdxaBone_t *matrix, const vec3_t angles, const vec3_t position, const int frameNum, qhandle_t *modelList, vec3_t scale );
	qboolean		(*G2API_GetBoltMatrix_NoReconstruct)	( void *ghoul2, const int modelIndex, const int boltIndex, mdxaBone_t *matrix, const vec3_t angles, const vec3_t position, const int frameNum, qhandle_t *modelList, vec3_t scale );
	qboolean		(*G2API_GetBoltMatrix_NoRecNoRot)		( void *ghoul2, const int modelIndex, const int boltIndex, mdxaBone_t *matrix, const vec3_t angles, const vec3_t position, const int frameNum, qhandle_t *modelList, vec3_t scale );
	int				(*G2API_InitGhoul2Model)				( void **ghoul2Ptr, const char *fileName, int modelIndex, qhandle_t customSkin, qhandle_t customShader, int modelFlags, int lodBias );
	qboolean		(*G2API_SetSkin)						( void *ghoul2, int modelIndex, qhandle_t customSkin, qhandle_t renderSkin );
	void			(*G2API_CollisionDetect)				( CollisionRecord_t *collRecMap, void* ghoul2, const vec3_t angles, const vec3_t position, int frameNumber, int entNum, vec3_t rayStart, vec3_t rayEnd, vec3_t scale, int traceFlags, int useLod, float fRadius );
	void			(*G2API_CollisionDetectCache)			( CollisionRecord_t *collRecMap, void* ghoul2, const vec3_t angles, const vec3_t position,int frameNumber, int entNum, vec3_t rayStart, vec3_t rayEnd, vec3_t scale, int traceFlags, int useLod, float fRadius );
	void			(*G2API_CleanGhoul2Models)				( void **ghoul2Ptr );
	qboolean		(*G2API_SetBoneAngles)					( void *ghoul2, int modelIndex, const char *boneName, const vec3_t angles, const int flags, const int up, const int right, const int forward, qhandle_t *modelList, int blendTime , int currentTime );
	qboolean		(*G2API_SetBoneAnim)					( void *ghoul2, const int modelIndex, const char *boneName, const int startFrame, const int endFrame, const int flags, const float animSpeed, const int currentTime, const float setFrame, const int blendTime );
	qboolean		(*G2API_GetBoneAnim)					( void *ghoul2, const char *boneName, const int currentTime, float *currentFrame, int *startFrame, int *endFrame, int *flags, float *animSpeed, int *modelList, const int modelIndex );
	qboolean		(*G2API_GetBoneFrame)					( void *ghoul2, const char *boneName, const int currentTime, float *currentFrame, int *modelList, const int modelIndex );
	void			(*G2API_GetGLAName)						( void *ghoul2, int modelIndex, char *fillBuf );
	int				(*G2API_CopyGhoul2Instance)				( void *g2From, void *g2To, int modelIndex );
	void			(*G2API_CopySpecificGhoul2Model)		( void *g2From, int modelFrom, void *g2To, int modelTo );
	void			(*G2API_DuplicateGhoul2Instance)		( void *g2From, void **g2To );
	qboolean		(*G2API_HasGhoul2ModelOnIndex)			( void *ghlInfo, int modelIndex );
	qboolean		(*G2API_RemoveGhoul2Model)				( void *ghlInfo, int modelIndex );
	qboolean		(*G2API_SkinlessModel)					( void *ghlInfo, int modelIndex );
	int				(*G2API_GetNumGoreMarks)				( void *ghlInfo, int modelIndex );
	void			(*G2API_AddSkinGore)					( void *ghlInfo, SSkinGoreData *gore );
	void			(*G2API_ClearSkinGore)					( void *ghlInfo );
	int				(*G2API_Ghoul2Size)						( void *ghlInfo );
	int				(*G2API_AddBolt)						( void *ghoul2, int modelIndex, const char *boneName );
	qboolean		(*G2API_AttachEnt)						( int *boltInfo, void *ghlInfoTo, int toBoltIndex, int entNum, int toModelNum );
	void			(*G2API_SetBoltInfo)					( void *ghoul2, int modelIndex, int boltInfo );
	qboolean		(*G2API_SetRootSurface)					( void *ghoul2, const int modelIndex, const char *surfaceName );
	qboolean		(*G2API_SetSurfaceOnOff)				( void *ghoul2, const char *surfaceName, const int flags );
	qboolean		(*G2API_SetNewOrigin)					( void *ghoul2, const int boltIndex );
	qboolean		(*G2API_DoesBoneExist)					( void *ghoul2, int modelIndex, const char *boneName );
	int				(*G2API_GetSurfaceRenderStatus)			( void *ghoul2, const int modelIndex, const char *surfaceName );
	int				(*G2API_GetTime)						( void );
	void			(*G2API_SetTime)						( int time, int clock );
	void			(*G2API_AbsurdSmoothing)				( void *ghoul2, qboolean status );
	void			(*G2API_SetRagDoll)						( void *ghoul2, sharedRagDollParams_t *params );
	void			(*G2API_AnimateG2Models)				( void *ghoul2, int time, sharedRagDollUpdateParams_t *params );
	qboolean		(*G2API_RagPCJConstraint)				( void *ghoul2, const char *boneName, vec3_t min, vec3_t max );
	qboolean		(*G2API_RagPCJGradientSpeed)			( void *ghoul2, const char *boneName, const float speed );
	qboolean		(*G2API_RagEffectorGoal)				( void *ghoul2, const char *boneName, vec3_t pos );
	qboolean		(*G2API_GetRagBonePos)					( void *ghoul2, const char *boneName, vec3_t pos, vec3_t entAngles, vec3_t entPos, vec3_t entScale );
	qboolean		(*G2API_RagEffectorKick)				( void *ghoul2, const char *boneName, vec3_t velocity );
	qboolean		(*G2API_RagForceSolve)					( void *ghoul2, qboolean force );
	qboolean		(*G2API_SetBoneIKState)					( void *ghoul2, int time, const char *boneName, int ikState, sharedSetBoneIKStateParams_t *params );
	qboolean		(*G2API_IKMove)							( void *ghoul2, int time, sharedIKMoveParams_t *params );
	qboolean		(*G2API_RemoveBone)						( void *ghoul2, const char *boneName, int modelIndex );
	void			(*G2API_AttachInstanceToEntNum)			( void *ghoul2, int entityNum, qboolean server );
	void			(*G2API_ClearAttachedInstance)			( int entityNum );
	void			(*G2API_CleanEntAttachments)			( void );
	qboolean		(*G2API_OverrideServer)					( void *serverInstance );
	void			(*G2API_GetSurfaceName)					( void *ghoul2, int surfNumber, int modelIndex, char *fillBuf );

	struct {
		float			(*R_Font_StrLenPixels)					( const char *text, const int iFontIndex, const float scale );
		// Only an engine that sets the cvar v_spatialVoice to 1 has this one, any other ends the structure
		// before it: look at the cvar before touching it. The voice of a player of the voice chat as a sound in
		// the level, see client/sound/spatial/snd_spatial.cpp. Returns the samples of him that wait, or -1. An engine that sets
		// v_spatialBoxes takes that many boxes of the boombox the same way, as clientNum MAX_CLIENTS and up.
		int				(*S_SpatialVoice)						( int clientNum, const vec3_t origin, float volume, const short *samples, int numSamples, int rate );
		// Only an engine that sets the cvar v_spatialOwnVoice to 1 has this one, the same way. What the own
		// player says into the voice chat: he does not hear the voice, the room he stands in rings from it.
		void			(*S_SpatialOwnVoice)					( float volume, const short *samples, int numSamples, int rate );
		// Only an engine that sets the cvar v_cgameChat to 1 has these three, the same way. For the chat box
		// (cgame/chat/cg_chat.c): the clipboard of the system, as the text the client has (UTF-8 where
		// v_utf8Text is 1), and a web address handed to the system to open, http and https only. Such an
		// engine also hands its message mode commands to the cgame command v_chatOpen while the cvar
		// v_chatHandler is 1.
		void			(*GetClipboard)							( char *buf, int size );
		void			(*SetClipboard)							( const char *text );
		qboolean		(*OpenURL)								( const char *url );
		// Only an engine that sets the cvar v_cgameScene to 1 has these three, the same way. For what the cgame
		// writes and paints into the level itself (the chat bubbles, bubble/cg_bubble.c). Text from the
		// TrueType font as polygons that are hidden by what is in front of them: the first letter stands at
		// origin on the line of the text, which runs along right; up and the height of a letter are units of
		// the level, a ^ with a digit colours what follows. Between R_ClearScene and R_RenderScene. A width of
		// 0 and qfalse: there is no such font. And a shader for a picture the cgame made itself, width and
		// height powers of two up to 512, for polygons in the scene; flags: RGBA_SHADER_GLOW, it is added and
		// glows. RGBA_SHADER_OVER, nothing of the scene hides it. RGBA_SHADER_BEHIND, drawn before the others.
		// RGBA_SHADER_HULL (an engine with the cvar v_uiSkin): as the custom shader of a model that is added a
		// second time it draws an outline around it, in the colour of the entity.
		float			(*R_Text3DWidth)						( const char *text, float height );
		qboolean		(*R_Text3D)								( const vec3_t origin, const vec3_t right, const vec3_t up, float height, const char *text, const float *rgba );
		qhandle_t		(*R_RegisterShaderRGBA)					( const char *name, const byte *rgba, int width, int height, int flags );
		// Only an engine that sets the cvar v_cgameUIPlane to 1 has this one, the same way. The menus of this
		// frame are drawn on a plane in the level and not flat on the screen (menu3d/cg_menu3d.c): matrix takes
		// a place of the 2D grid to clip space, 16 values column by column. Called before the menus are drawn,
		// every frame it is wanted.
		void			(*R_SetUIPlane)							( const float *matrix );
		// Only an engine that sets the cvar v_cgameDoF to 1 has this one, the same way. The scene of this
		// frame is blurred where it is nearer or farther than focus (units of the level), amount 0 to 1. Called
		// before R_RenderScene, every frame it is wanted. A renderer without the effect does nothing.
		void			(*R_SetDepthOfField)					( float focus, float amount );
		// Only an engine that sets the cvar v_cgameSpatialDebug to 1 has this one, the same way. What the sound of
		// the level with Steam Audio shows of itself for v_spatialDebug (qcommon/spatial_debug.h), once a frame:
		// eye and forward are where the camera is and looks, for the wall under the crosshair.
		void			(*S_SpatialDebug)						( spatialDebug_t *out, const vec3_t eye, const vec3_t forward );
		// Only an engine that sets the cvar v_cgameSpatialMovers to 1 has this one, the same way. Where the brush
		// models of the snapshot are (doors, lifts, func_static), once a frame, for the sound of the level with
		// Steam Audio: a closed door blocks, an open one lets through. One not named keeps its place.
		void			(*S_SpatialMovers)						( const spatialMover_t *list, int count );
		// Only an engine that sets the cvar v_cgameScope to 1 has this one, the same way. The view of the level of
		// this frame is seen through the scope of a weapon (scopeEffect_t), called before R_RenderScene every
		// frame it is wanted. NULL only asks. qfalse: the renderer does not have it: the cgame draws
		// the scope itself.
		qboolean		(*R_SetScopeEffect)						( const scopeEffect_t *fx );
		// Only an engine that sets the cvar v_cgameScreenFx to 1 has this one, the same way. The view of the level
		// of this frame bends and is coloured as screenEffect_t says, called before R_RenderScene every frame it is
		// wanted. NULL only asks. qfalse: the renderer does not have it.
		qboolean		(*R_SetScreenEffect)					( const screenEffect_t *fx );
		// Only an engine that sets the cvar v_cgameVideo to 1 has these, the same way. Videos on surfaces of the level
		// (client/cl_video.cpp): one of VIDEO_STREAMS streams shows the picture of a video of YouTube (its eleven
		// characters) from a second on, fetched by the engine from the TV of the masterserver. It plays on to msec
		// (from that second) when Video_Advance is called, once a frame, and a shader stage "videoMap *tv<stream>"
		// shows it. Video_Open qfalse: nothing is fetched now, ask again later. Video_Advance returns the time of the
		// frame shown, -1 none. Video_State: VIDEO_*. Video_Color: the mean colour of that frame.
		qboolean		(*Video_Open)							( int stream, const char *video, int second );
		int				(*Video_Advance)						( int stream, int msec );
		int				(*Video_State)							( int stream );
		qboolean		(*Video_Color)							( int stream, vec3_t out );
		void			(*Video_Reset)							( int stream );
		// A picture the cgame made in place of a video: the stream shows it until the next one or a reset. width and
		// height 16 to 512, rgba width * height * 4 bytes, copied.
		void			(*Video_Picture)						( int stream, const byte *rgba, int width, int height );
		// Only an engine that sets the cvar v_cgameImage to 1 has this one, the same way. A picture file of the game
		// (tga, png, jpg) in width x height RGBA (up to 512 each), its sides kept and centred: where it does not reach
		// the alpha is 0. qfalse: there is no such picture.
		qboolean		(*R_LoadImageFit)						( const char *name, byte *rgba, int width, int height );
		// Only an engine that sets the cvar v_cgameLiquid to 1 has this one, the same way. Pieces of clear water for
		// the scene of this frame (liquidSegment_t: a jet is a chain of them, a drop one short piece), between
		// R_ClearScene and R_RenderScene; they bend what lies behind them. NULL only asks. qfalse: the renderer does
		// not draw them: the cgame draws its water itself.
		qboolean		(*R_AddLiquid)							( const liquidSegment_t *segments, int count );
		// Only an engine that sets the cvar v_cgameWet to 1 has this one, the same way. Wet places for the scene of
		// this frame (wetSpot_t: puddles and wet patches), between R_ClearScene and R_RenderScene. NULL only asks.
		// qfalse: the renderer does not draw them: the cgame draws marks.
		qboolean		(*R_AddWet)								( const wetSpot_t *spots, int count );
		// Only an engine that sets the cvar v_cgameLoopVolume to 1 has this one. S_AddLoopingSound at a volume of
		// its own, 0 to 255 of the full one: several loops of one entity crossfade by it.
		void			(*S_AddLoopingSoundVolume)				( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx, int volume );
		// Only an engine that sets the cvar v_cgamePaint to 1 has these, the same way. The sheet of a picture in a slot
		// of the renderer (paintDecal_t, PAINT_SLOTS of tr_types.h, client/cl_sprays.cpp): an emoji (gfx/emoji/..png),
		// a file of sprays/ (png, jpg, tga, an animated gif) or a sheet of the store (sprays/store/<number>.png). out
		// says how its frames lie and how long each is shown. Returns the shader for marks with it, 0: there is no
		// such picture. R_AddPaint: sprayed pictures for the scene of this frame, between R_ClearScene and
		// R_RenderScene. NULL only asks. qfalse: the renderer does not lay them on as paint: the cgame
		// draws marks with the shader.
		qhandle_t		(*R_PaintPicture)						( int slot, const char *name, paintPicture_t *out );
		qboolean		(*R_AddPaint)							( const paintDecal_t *decals, int count );
		// Only an engine that sets the cvar v_cgameLineLight to 1 has this one, the same way. A tube of light from
		// start to end for the scene of this frame (a saber blade), reaching range units from it, between
		// R_ClearScene and R_RenderScene, in place of R_AddLightToScene. NULL: the renderer has none, the cgame
		// adds a point light as before.
		void			(*R_AddLineLight)						( const vec3_t start, const vec3_t end, float range, float r, float g, float b );
		// Only an engine that sets the cvar v_cgameSaberBlade to 1 has this one, the same way. A saber blade drawn by
		// the renderer in 3D for the scene of this frame (saberBlade3D_t: a white core in a coloured glow, and the
		// place it swept), in place of the flat RT_SABER_GLOW and RT_LINE. NULL: the renderer has none.
		void			(*R_AddSaberBlade)						( const saberBlade3D_t *blade );
		// Only an engine that sets the cvar v_cgameCutEntity to 1 has this one, the same way. An entity drawn cut by
		// plane (in the world: normal, distance; the side the normal points to is kept), otherwise as
		// R_AddRefEntityToScene: the halves of a body a saber cut through (cg_slice.c). NULL: the renderer has none.
		void			(*R_AddCutEntity)						( const refEntity_t *ent, const float *plane );
		// Only an engine that sets the cvar v_cgameSetBones to 1 has these two, the same way. G2_BoneNumber: the
		// number of a bone in the skeleton of model modelIndex (-1: none), parent gets its parent's (-1: the root).
		// G2_SetBonesWorld: bones placed whole in the world, count 3x4 matrices in the frame G2API_GetBoltMatrix
		// gives for the same angles, origin and scale; bones not listed follow their parent; count 0 hands them all
		// back to the animation (the ragdoll of the cut bodies, cg_ragdoll.c). NULL: the renderer has none.
		int				(*G2_BoneNumber)						( void *ghoul2, int modelIndex, const char *boneName, int *parent );
		qboolean		(*G2_SetBonesWorld)						( void *ghoul2, int modelIndex, const vec3_t angles, const vec3_t origin, const vec3_t scale, int count, const int *bones, const float *world );
	} ext;
} cgameImport_t;

typedef struct cgameExport_s {
	void			(*Init)						( int serverMessageNum, int serverCommandSequence, int clientNum );
	void			(*Shutdown)					( void );
	qboolean		(*ConsoleCommand)			( void );
	void			(*DrawActiveFrame)			( int serverTime, stereoFrame_t stereoView, qboolean demoPlayback );
	int				(*CrosshairPlayer)			( void );
	int				(*LastAttacker)				( void );
	void			(*KeyEvent)					( int key, qboolean down );
	void			(*MouseEvent)				( int x, int y );
	void			(*EventHandling)			( int type );
	int				(*PointContents)			( void );
	void			(*GetLerpOrigin)			( void );
	void			(*GetLerpData)				( void );
	void			(*Trace)					( void );
	void			(*G2Trace)					( void );
	void			(*G2Mark)					( void );
	int				(*RagCallback)				( int callType );
	qboolean		(*IncomingConsoleCommand)	( void );
	qboolean		(*NoUseableForce)			( void );
	void			(*GetOrigin)				( int entID, vec3_t out );
	void			(*GetAngles)				( int entID, vec3_t out );
	trajectory_t *	(*GetOriginTrajectory)		( int entID );
	trajectory_t *	(*GetAngleTrajectory)		( int entID );
	void			(*ROFF_NotetrackCallback)	( int entID, const char *notetrack );
	void			(*MapChange)				( void );
	void			(*AutomapInput)				( void );
	void			(*MiscEnt)					( void );
	void			(*CameraShake)				( void );
} cgameExport_t;

//linking of cgame library
typedef cgameExport_t* (QDECL *GetCGameAPI_t)( int apiVersion, cgameImport_t *import );
