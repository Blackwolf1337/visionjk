/*
===========================================================================
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

// cl_imgui.cpp: the backend of Dear ImGui (lib/imgui, version 1.93.0) for the debug menus of the client. Input
// comes from the key events of the engine and the mouse of the window (IN_MousePosition: the mouse is freed while a
// menu is open, see IN_Frame), pictures go to the renderer as UI textures and the lists as triangles in pixels of
// the window (ext.UiTexture, ext.DrawTriangles2D). The layout of the windows is kept in debugmenus.ini of the
// home folder.

#include "client.h"
#include "cl_imgui.h"
#include "imgui/imgui.h"

#define MAX_DEBUG_WINDOWS	8
#define LAYOUT_FILE			"debugmenus.ini"

static debugWindow_t	*windows[MAX_DEBUG_WINDOWS];
static int				numWindows;
static qboolean			contextMade;
static qboolean			textureSlots[UI_TEXTURES];	// taken
static cvar_t			*v_debugMenuScale;

void CL_ImGui_Register( debugWindow_t *window ) {
	if ( numWindows < MAX_DEBUG_WINDOWS ) {
		windows[numWindows++] = window;
	}
}

qboolean CL_ImGui_Active( void ) {
	for ( int i = 0; i < numWindows; i++ ) {
		if ( windows[i]->open ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
===============
Clipboard and layout
===============
*/
static const char *ImGui_GetClipboard( ImGuiContext * ) {
	static char	text[8192];
	char		*data = Sys_GetClipboardData();

	text[0] = '\0';
	if ( data ) {
		Q_strncpyz( text, data, sizeof( text ) );
		Z_Free( data );
	}
	return text;
}

static void ImGui_SetClipboard( ImGuiContext *, const char *text ) {
	Sys_SetClipboardData( text );
}

static void ImGui_LoadLayout( void ) {
	void		*buffer = NULL;
	const long	length = FS_ReadFile( LAYOUT_FILE, &buffer );

	if ( length > 0 && buffer ) {
		ImGui::LoadIniSettingsFromMemory( (const char *)buffer, (size_t)length );
	}
	if ( buffer ) {
		FS_FreeFile( buffer );
	}
}

static void ImGui_SaveLayout( void ) {
	size_t		length = 0;
	const char	*text = ImGui::SaveIniSettingsToMemory( &length );

	if ( text && length ) {
		FS_WriteFile( LAYOUT_FILE, text, (int)length );
	}
	ImGui::GetIO().WantSaveIniSettings = false;
}

/*
===============
The context, made when the first menu opens (the fonts come from the file system)
===============
*/
static void ImGui_Style( void ) {
	ImGuiStyle &style = ImGui::GetStyle();

	ImGui::StyleColorsDark( &style );
	style.WindowRounding = 4.0f;
	style.FrameRounding = 3.0f;
	style.GrabRounding = 3.0f;
	style.Colors[ImGuiCol_WindowBg].w = 0.92f;
}

static qboolean ImGui_MakeContext( void ) {
	if ( contextMade ) {
		return qtrue;
	}
	if ( !re || !re->ext.DrawTriangles2D || !re->ext.UiTexture ) {
		Com_Printf( S_COLOR_YELLOW "The debug menus need the renderer rd-vulkan-vision\n" );
		return qfalse;
	}
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO &io = ImGui::GetIO();

	io.IniFilename = NULL;
	io.LogFilename = NULL;
	io.BackendPlatformName = "VisionJK client";
	io.BackendRendererName = "VisionJK renderer";
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGuiPlatformIO &platform = ImGui::GetPlatformIO();

	platform.Platform_GetClipboardTextFn = ImGui_GetClipboard;
	platform.Platform_SetClipboardTextFn = ImGui_SetClipboard;

	// the text font of the menus and the fixed width one of the console, both of the files this build shipped
	// (CL_FontFile); they stay until the program ends, so the atlas does not own them
	ImFontConfig config;
	int length = 0;
	const byte *data = CL_FontFile( 0, &length );

	config.FontDataOwnedByAtlas = false;
	if ( data ) {
		io.Fonts->AddFontFromMemoryTTF( (void *)data, length, 0.0f, &config );
	} else {
		io.Fonts->AddFontDefault();
	}
	data = CL_FontFile( 1, &length );
	if ( data ) {
		io.Fonts->AddFontFromMemoryTTF( (void *)data, length, 0.0f, &config );
	}

	ImGui_Style();
	ImGui_LoadLayout();
	memset( textureSlots, 0, sizeof( textureSlots ) );
	contextMade = qtrue;
	return qtrue;
}

void CL_ImGui_Shutdown( void ) {
	if ( !contextMade ) {
		return;
	}
	ImGui_SaveLayout();
	ImGui::DestroyContext();
	contextMade = qfalse;
}

/*
===============
Textures: the font atlas, which ImGui asks to be made, filled again and given up (ImGuiBackendFlags_RendererHasTextures)
===============
*/
static void ImGui_UpdateTexture( ImTextureData *tex ) {
	int slot = (int)(intptr_t)tex->BackendUserData - 1;

	if ( tex->Status == ImTextureStatus_WantDestroy ) {
		if ( tex->UnusedFrames <= 0 ) {
			return;
		}
		if ( slot >= 0 && slot < UI_TEXTURES ) {
			textureSlots[slot] = qfalse;
		}
		tex->BackendUserData = NULL;
		tex->SetTexID( ImTextureID_Invalid );
		tex->SetStatus( ImTextureStatus_Destroyed );
		return;
	}
	if ( tex->Status != ImTextureStatus_WantCreate && tex->Status != ImTextureStatus_WantUpdates ) {
		return;
	}
	if ( slot < 0 ) {
		for ( slot = 0; slot < UI_TEXTURES && textureSlots[slot]; slot++ ) {
		}
		if ( slot == UI_TEXTURES ) {
			return;
		}
		textureSlots[slot] = qtrue;
		tex->BackendUserData = (void *)(intptr_t)( slot + 1 );
	}
	// the whole picture again, also for an update of a part: it changes only when glyphs are added
	const qhandle_t shader = re->ext.UiTexture( slot, (const byte *)tex->GetPixels(), tex->Width, tex->Height );

	if ( !shader ) {
		Com_Printf( S_COLOR_YELLOW "Debug menus: the picture of %d x %d could not be made\n", tex->Width, tex->Height );
	}
	tex->SetTexID( (ImTextureID)shader );
	tex->SetStatus( ImTextureStatus_OK );
}

void CL_ImGui_RendererStarted( void ) {
	if ( !contextMade ) {
		return;
	}
	// the pictures went with the renderer: all are made again in their slots
	for ( ImTextureData *tex : ImGui::GetPlatformIO().Textures ) {
		if ( tex->Status == ImTextureStatus_OK ) {
			tex->SetStatus( ImTextureStatus_WantCreate );
		}
	}
}

/*
===============
Drawing: every command of a list as triangles with the vertices it uses
===============
*/
static polyVert_t	drawVerts[65536];
static int			drawIndexes[65536 * 3];

static void ImGui_DrawCommand( const ImDrawList *list, const ImDrawCmd *cmd ) {
	const ImDrawIdx	*indexes = list->IdxBuffer.Data + cmd->IdxOffset;
	const ImDrawVert *verts = list->VtxBuffer.Data + cmd->VtxOffset;
	const int		count = (int)cmd->ElemCount;
	int				low = INT_MAX, high = -1;

	if ( count <= 0 || count > (int)ARRAY_LEN( drawIndexes ) ) {
		return;
	}
	for ( int i = 0; i < count; i++ ) {
		low = Q_min( low, (int)indexes[i] );
		high = Q_max( high, (int)indexes[i] );
	}
	if ( high - low + 1 > (int)ARRAY_LEN( drawVerts ) ) {
		return;
	}
	for ( int i = low; i <= high; i++ ) {
		polyVert_t *v = &drawVerts[i - low];

		v->xyz[0] = verts[i].pos.x;
		v->xyz[1] = verts[i].pos.y;
		v->xyz[2] = 0.0f;
		v->st[0] = verts[i].uv.x;
		v->st[1] = verts[i].uv.y;
		// IM_COL32 is R in the low byte: in memory R, G, B, A on every little endian machine this builds for
		memcpy( v->modulate, &verts[i].col, sizeof( v->modulate ) );
	}
	for ( int i = 0; i < count; i++ ) {
		drawIndexes[i] = (int)indexes[i] - low;
	}

	const int clip[4] = { (int)cmd->ClipRect.x, (int)cmd->ClipRect.y, (int)ceilf( cmd->ClipRect.z ), (int)ceilf( cmd->ClipRect.w ) };

	if ( clip[2] <= clip[0] || clip[3] <= clip[1] ) {
		return;
	}
	re->ext.DrawTriangles2D( drawVerts, high - low + 1, drawIndexes, count, (qhandle_t)cmd->GetTexID(), clip );
}

static void ImGui_Draw( ImDrawData *data ) {
	if ( data->Textures ) {
		for ( ImTextureData *tex : *data->Textures ) {
			if ( tex->Status != ImTextureStatus_OK ) {
				ImGui_UpdateTexture( tex );
			}
		}
	}
	for ( const ImDrawList *list : data->CmdLists ) {
		for ( const ImDrawCmd &cmd : list->CmdBuffer ) {
			if ( cmd.UserCallback ) {
				if ( cmd.UserCallback != ImGui::GetPlatformIO().DrawCallback_ResetRenderState ) {
					cmd.UserCallback( list, &cmd );
				}
				continue;
			}
			ImGui_DrawCommand( list, &cmd );
		}
	}
}

/*
===============
CL_ImGui_Frame
===============
*/
void CL_ImGui_Frame( void ) {
	int x, y;

	if ( !CL_ImGui_Active() || !contextMade || !cls.glconfig.vidWidth ) {
		return;
	}

	ImGuiIO &io = ImGui::GetIO();

	io.DisplaySize = ImVec2( (float)cls.glconfig.vidWidth, (float)cls.glconfig.vidHeight );
	io.DeltaTime = Q_max( cls.realFrametime, 1 ) * 0.001f;
	if ( IN_MousePosition( &x, &y ) ) {
		io.AddMousePosEvent( (float)x, (float)y );
	}
	// text as large on a big window as on a 1080 one, and what v_debugMenuScale adds
	ImGui::GetStyle().FontSizeBase = 16.0f;
	ImGui::GetStyle().FontScaleMain = Com_Clamp( 1.0f, 3.0f, cls.glconfig.vidHeight / 1080.0f ) * Com_Clamp( 0.5f, 3.0f, v_debugMenuScale->value );

	ImGui::NewFrame();
	for ( int i = 0; i < numWindows; i++ ) {
		if ( windows[i]->open ) {
			windows[i]->draw( windows[i] );
		}
	}
	ImGui::Render();
	ImGui_Draw( ImGui::GetDrawData() );

	if ( io.WantSaveIniSettings ) {
		ImGui_SaveLayout();
	}
}

void CL_ImGui_Toggle( debugWindow_t *window ) {
	if ( !window->open && !ImGui_MakeContext() ) {
		return;
	}
	if ( !v_debugMenuScale ) {
		v_debugMenuScale = Cvar_Get( "v_debugMenuScale", "1", CVAR_ARCHIVE_ND, "Size of the debug menus of the client (v_matdebug), 1 as on a window of 1080 rows" );
	}
	window->open = (qboolean)!window->open;
	// what was held stops (the menu takes the keys), and the menu gets no half pressed keys either
	Key_ClearStates();
	if ( contextMade ) {
		ImGui::GetIO().ClearInputKeys();
	}
}

/*
===============
Input
===============
*/
static ImGuiKey ImGui_KeyOf( int key ) {
	if ( key >= A_CAP_A && key <= A_CAP_Z ) {
		return (ImGuiKey)( ImGuiKey_A + ( key - A_CAP_A ) );
	}
	if ( key >= A_LOW_A && key <= A_LOW_Z ) {
		return (ImGuiKey)( ImGuiKey_A + ( key - A_LOW_A ) );
	}
	if ( key >= A_0 && key <= A_9 ) {
		return (ImGuiKey)( ImGuiKey_0 + ( key - A_0 ) );
	}
	switch ( key ) {
	case A_TAB:				return ImGuiKey_Tab;
	case A_ENTER:			return ImGuiKey_Enter;
	case A_KP_ENTER:		return ImGuiKey_KeypadEnter;
	case A_ESCAPE:			return ImGuiKey_Escape;
	case A_SPACE:			return ImGuiKey_Space;
	case A_BACKSPACE:		return ImGuiKey_Backspace;
	case A_DELETE:			return ImGuiKey_Delete;
	case A_INSERT:			return ImGuiKey_Insert;
	case A_HOME:			return ImGuiKey_Home;
	case A_END:				return ImGuiKey_End;
	case A_PAGE_UP:			return ImGuiKey_PageUp;
	case A_PAGE_DOWN:		return ImGuiKey_PageDown;
	case A_CURSOR_LEFT:		return ImGuiKey_LeftArrow;
	case A_CURSOR_RIGHT:	return ImGuiKey_RightArrow;
	case A_CURSOR_UP:		return ImGuiKey_UpArrow;
	case A_CURSOR_DOWN:		return ImGuiKey_DownArrow;
	case A_CTRL:			return ImGuiKey_LeftCtrl;
	case A_SHIFT:			return ImGuiKey_LeftShift;
	case A_ALT:				return ImGuiKey_LeftAlt;
	case A_F1:				return ImGuiKey_F1;
	case A_F2:				return ImGuiKey_F2;
	case A_F3:				return ImGuiKey_F3;
	case A_F4:				return ImGuiKey_F4;
	case A_F5:				return ImGuiKey_F5;
	case A_F6:				return ImGuiKey_F6;
	case A_F7:				return ImGuiKey_F7;
	case A_F8:				return ImGuiKey_F8;
	case A_F9:				return ImGuiKey_F9;
	case A_F10:				return ImGuiKey_F10;
	case A_F11:				return ImGuiKey_F11;
	case A_F12:				return ImGuiKey_F12;
	default:				return ImGuiKey_None;
	}
}

qboolean CL_ImGui_KeyEvent( int key, qboolean down ) {
	if ( !CL_ImGui_Active() || !contextMade ) {
		return qfalse;
	}

	ImGuiIO &io = ImGui::GetIO();

	switch ( key ) {
	case A_MOUSE1:
		io.AddMouseButtonEvent( 0, down ? true : false );
		return qtrue;
	case A_MOUSE2:
		io.AddMouseButtonEvent( 1, down ? true : false );
		return qtrue;
	case A_MOUSE3:
		io.AddMouseButtonEvent( 2, down ? true : false );
		return qtrue;
	case A_MWHEELUP:
		if ( down ) {
			io.AddMouseWheelEvent( 0.0f, 1.0f );
		}
		return qtrue;
	case A_MWHEELDOWN:
		if ( down ) {
			io.AddMouseWheelEvent( 0.0f, -1.0f );
		}
		return qtrue;
	default:
		break;
	}

	// the key that opened a menu closes it again, and Escape closes all while no text is typed
	if ( down ) {
		const char *binding = Key_GetBinding( key );

		if ( binding && !io.WantTextInput && ( !Q_stricmp( binding, "v_matdebug" ) ) ) {
			Cbuf_ExecuteText( EXEC_APPEND, va( "%s\n", binding ) );
			return qtrue;
		}
		if ( key == A_ESCAPE && !io.WantTextInput ) {
			for ( int i = 0; i < numWindows; i++ ) {
				if ( windows[i]->open ) {
					CL_ImGui_Toggle( windows[i] );
				}
			}
			return qtrue;
		}
	}

	if ( key == A_CTRL ) {
		io.AddKeyEvent( ImGuiMod_Ctrl, down ? true : false );
	} else if ( key == A_SHIFT ) {
		io.AddKeyEvent( ImGuiMod_Shift, down ? true : false );
	} else if ( key == A_ALT ) {
		io.AddKeyEvent( ImGuiMod_Alt, down ? true : false );
	}

	const ImGuiKey imKey = ImGui_KeyOf( key );

	if ( imKey != ImGuiKey_None ) {
		io.AddKeyEvent( imKey, down ? true : false );
	}
	return qtrue;
}

qboolean CL_ImGui_CharEvent( int ch ) {
	if ( !CL_ImGui_Active() || !contextMade ) {
		return qfalse;
	}
	if ( ch >= 32 ) {
		ImGui::GetIO().AddInputCharacter( (unsigned int)ch );
	}
	return qtrue;
}
