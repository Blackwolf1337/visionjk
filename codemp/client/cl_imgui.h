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

// cl_imgui.h: Dear ImGui (lib/imgui) in the client, for its debug menus. They are drawn by the engine through the
// renderer (ext.DrawTriangles2D), not by the ui or the cgame, so they work in any mod and on any server.

#pragma once

// a debug menu: draw is called every frame while *open, between ImGui::NewFrame and ImGui::Render
typedef struct debugWindow_s {
	const char	*name;
	qboolean	open;
	qboolean	overlay;		// a panel over the game: drawn while open, takes neither keys nor the mouse
	void		( *draw )( struct debugWindow_s *window );
} debugWindow_t;

void		CL_ImGui_Register( debugWindow_t *window );
// opens or closes it; open takes the keys and frees the mouse (not an overlay)
void		CL_ImGui_Toggle( debugWindow_t *window );
void		CL_ImGui_SetOpen( debugWindow_t *window, qboolean open );
// a debug menu that takes the keys and the mouse is open
qboolean	CL_ImGui_Active( void );
// after the renderer started again (a level, vid_restart): its pictures are made again
void		CL_ImGui_RendererStarted( void );
// in the 2D of the frame, after the menus of the engine, under the console
void		CL_ImGui_Frame( void );
// qtrue: a debug menu is open and took the key
qboolean	CL_ImGui_KeyEvent( int key, qboolean down );
qboolean	CL_ImGui_CharEvent( int ch );
void		CL_ImGui_Shutdown( void );

// cl_profiler.cpp: the GPU profiler (v_profiler)
void		CL_Profiler_Init( void );
void		CL_Profiler_Frame( void );

// cl_matdebug.cpp, cl_matlight.cpp
void		CL_MatDebug_Init( void );
void		CL_MatDebug_Shutdown( void );
void		CL_MatLight_Init( void );
void		CL_MatLight_Toggle( void );
