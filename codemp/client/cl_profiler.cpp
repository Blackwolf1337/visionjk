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

// cl_profiler.cpp: the GPU profiler (v_profiler), a panel over the game in the manner of the Metal performance HUD:
// frames a second, the frame on the CPU and on the device, a graph of the last frames, what each part of the frame
// took on the device (ext.GpuProfile, the parts v_gpuTimes prints) and what the frame was made of. It takes neither
// keys nor the mouse.

#include "client.h"
#include "cl_imgui.h"
#include "imgui/imgui.h"

#define PROF_HISTORY	GPU_PROFILE_HISTORY
#define PROF_MSEC		500			// as the window of the renderer (vk_cmd.cpp VK_PROFILE_MSEC)

static debugWindow_t	profWindow;
static cvar_t			*v_profiler;

// the frames of the client: the window of about half a second, and the time of each of the last frames
static struct {
	int		frames, sumMs, maxMs, start;
	float	fps, frameMs, frameMaxMs;
	float	history[PROF_HISTORY];
	int		historyAt;
	int		lastTime;
} cpu;

static const ImU32	colGood = IM_COL32( 76, 217, 100, 255 );
static const ImU32	colFair = IM_COL32( 255, 204, 0, 255 );
static const ImU32	colBad = IM_COL32( 255, 59, 48, 255 );
static const ImU32	colDim = IM_COL32( 154, 154, 154, 255 );
static const ImU32	colText = IM_COL32( 232, 232, 232, 255 );
static const ImU32	colGpu = IM_COL32( 90, 200, 250, 255 );

// a frame time: within 60 frames a second, within 30, slower
static ImU32 Prof_FrameColor( float ms ) {
	return ms <= 17.5f ? colGood : ms <= 34.0f ? colFair : colBad;
}

static ImU32 Prof_FpsColor( float fps ) {
	return fps >= 57.0f ? colGood : fps >= 29.0f ? colFair : colBad;
}

// the client frame that just ended into the window (called every frame, also while the panel is closed it costs
// nothing but this)
static void Prof_CpuFrame( void ) {
	const int now = Sys_Milliseconds();
	int ms;

	if ( !cpu.lastTime ) {
		cpu.lastTime = cpu.start = now;
		return;
	}
	ms = now - cpu.lastTime;
	cpu.lastTime = now;
	cpu.history[cpu.historyAt] = (float)ms;
	cpu.historyAt = ( cpu.historyAt + 1 ) % PROF_HISTORY;
	cpu.frames++;
	cpu.sumMs += ms;
	cpu.maxMs = Q_max( cpu.maxMs, ms );
	if ( now - cpu.start >= PROF_MSEC && cpu.frames ) {
		cpu.fps = cpu.frames * 1000.0f / Q_max( cpu.sumMs, 1 );
		cpu.frameMs = (float)cpu.sumMs / cpu.frames;
		cpu.frameMaxMs = (float)cpu.maxMs;
		cpu.frames = cpu.sumMs = cpu.maxMs = 0;
		cpu.start = now;
	}
}

// a row: label, value, and a remark after it, at the columns of the panel
static void Prof_Row( const char *label, ImU32 valueColor, const char *value, const char *remark ) {
	const float ch = ImGui::CalcTextSize( "0" ).x;

	ImGui::PushStyleColor( ImGuiCol_Text, colDim );
	ImGui::TextUnformatted( label );
	ImGui::PopStyleColor();
	ImGui::SameLine( ch * 12.0f );
	ImGui::PushStyleColor( ImGuiCol_Text, valueColor );
	ImGui::TextUnformatted( value );
	ImGui::PopStyleColor();
	if ( remark && remark[0] ) {
		ImGui::SameLine( ch * 23.0f );
		ImGui::PushStyleColor( ImGuiCol_Text, colDim );
		ImGui::TextUnformatted( remark );
		ImGui::PopStyleColor();
	}
}

static void Prof_Separator( void ) {
	const ImVec2	at = ImGui::GetCursorScreenPos();
	const float		width = ImGui::GetContentRegionAvail().x;
	const float		gap = ImGui::GetFontSize() * 0.35f;

	ImGui::GetWindowDrawList()->AddLine( ImVec2( at.x, at.y + gap ), ImVec2( at.x + width, at.y + gap ), IM_COL32( 255, 255, 255, 40 ) );
	ImGui::Dummy( ImVec2( width, gap * 2.0f ) );
}

// a count short: 180259 -> 180k
static const char *Prof_Count( int n ) {
	if ( n >= 10000000 ) {
		return va( "%iM", n / 1000000 );
	}
	if ( n >= 10000 ) {
		return va( "%ik", n / 1000 );
	}
	return va( "%i", n );
}

// the frames of the client as bars and those of the device as a line over them, with lines at 60 and 30 a second
static void Prof_Graph( const gpuProfile_t *gpu, float width, float height ) {
	ImDrawList		*draw = ImGui::GetWindowDrawList();
	const ImVec2	at = ImGui::GetCursorScreenPos();
	float			top = 5.0f;
	int				i;

	// (as high as the slowest frame shown, so the line of the device stands apart from the bars at any rate)
	for ( i = 0; i < PROF_HISTORY; i++ ) {
		top = Q_max( top, cpu.history[i] * 1.3f );
		if ( gpu ) {
			top = Q_max( top, gpu->history[i] * 1.3f );
		}
	}
	top = Q_min( top, 50.0f );
	draw->AddRectFilled( at, ImVec2( at.x + width, at.y + height ), IM_COL32( 255, 255, 255, 13 ), 3.0f );
	// the guides, dashed
	for ( i = 0; i < 2; i++ ) {
		const float ms = i ? 1000.0f / 30.0f : 1000.0f / 60.0f;
		const float y = at.y + height - height * Q_min( ms, top ) / top;

		if ( ms > top ) {
			continue;
		}
		for ( float x = 0.0f; x < width; x += 6.0f ) {
			draw->AddLine( ImVec2( at.x + x, y ), ImVec2( at.x + Q_min( x + 3.0f, width ), y ), IM_COL32( 255, 255, 255, 56 ) );
		}
	}
	const float step = width / PROF_HISTORY;

	for ( i = 0; i < PROF_HISTORY; i++ ) {
		const float ms = cpu.history[( cpu.historyAt + i ) % PROF_HISTORY];
		const float h = height * Q_min( ms, top ) / top;

		if ( h > 0.0f ) {
			draw->AddRectFilled( ImVec2( at.x + i * step, at.y + height - h ), ImVec2( at.x + i * step + step * 0.8f, at.y + height ),
				( Prof_FrameColor( ms ) & ~IM_COL32_A_MASK ) | IM_COL32( 0, 0, 0, 150 ) );
		}
	}
	if ( gpu ) {
		ImVec2 line[PROF_HISTORY];

		for ( i = 0; i < PROF_HISTORY; i++ ) {
			line[i] = ImVec2( at.x + ( i + 0.4f ) * step, at.y + height - height * Q_min( gpu->history[i], top ) / top );
		}
		draw->AddPolyline( line, PROF_HISTORY, colGpu, Q_max( 1.5f, height * 0.05f ) );
	}
	ImGui::Dummy( ImVec2( width, height ) );
}

// what each part of the frame took on the device, the most first, with a bar of its share of the whole frame
static void Prof_Passes( const gpuProfile_t *gpu, float width ) {
	int		order[GPU_PROFILE_PASSES];
	int		i, j;
	const float	ch = ImGui::CalcTextSize( "0" ).x;
	const float	barX = ch * 26.0f;

	for ( i = 0; i < gpu->numPasses; i++ ) {
		order[i] = i;
	}
	for ( i = 1; i < gpu->numPasses; i++ ) {
		const int k = order[i];

		for ( j = i; j > 0 && gpu->passMs[order[j - 1]] < gpu->passMs[k]; j-- ) {
			order[j] = order[j - 1];
		}
		order[j] = k;
	}
	for ( i = 0; i < gpu->numPasses; i++ ) {
		const int		n = order[i];
		const float		ms = gpu->passMs[n];
		const qboolean	tiny = (qboolean)( ms < 0.01f );
		const float		share = gpu->frameMs > 0.0f ? Com_Clamp( 0.0f, 1.0f, ms / gpu->frameMs ) : 0.0f;

		ImGui::PushStyleColor( ImGuiCol_Text, tiny ? colDim : colText );
		ImGui::TextUnformatted( gpu->passNames[n] );
		ImGui::SameLine( ch * 18.0f );
		ImGui::Text( "%6.3f", ms );
		ImGui::PopStyleColor();
		ImGui::SameLine( barX );

		const ImVec2	at = ImGui::GetCursorScreenPos();
		const float		lineH = ImGui::GetTextLineHeight();
		const float		barW = width - barX;

		ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( at.x, at.y + lineH * 0.25f ),
			ImVec2( at.x + Q_max( barW * share, tiny ? 0.0f : 1.0f ), at.y + lineH * 0.75f ), share > 0.2f ? colFair : colGpu, 1.0f );
		ImGui::Dummy( ImVec2( barW, lineH ) );
	}
}

static void Prof_Draw( debugWindow_t *window ) {
	ImGuiIO			&io = ImGui::GetIO();
	gpuProfile_t	gpuStore;
	const qboolean	hasGpu = (qboolean)( re && re->ext.GpuProfile && re->ext.GpuProfile( &gpuStore ) && gpuStore.serial );
	const gpuProfile_t *gpu = hasGpu ? &gpuStore : NULL;
	const ImGuiViewport *view = ImGui::GetMainViewport();
	const qboolean	left = (qboolean)( v_profiler->integer != 2 );	// (the HUD has its clock and the leader top right)
	char			value[32], remark[32];

	ImGui::PushFont( io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : NULL, 0.0f );
	const float		fs = ImGui::GetFontSize();
	const float		ch = ImGui::CalcTextSize( "0" ).x;
	const float		width = ch * 36.0f;
	const float		margin = fs * 0.6f;

	ImGui::SetNextWindowPos( ImVec2( left ? view->Pos.x + margin : view->Pos.x + view->Size.x - margin, view->Pos.y + margin ),
		ImGuiCond_Always, ImVec2( left ? 0.0f : 1.0f, 0.0f ) );
	ImGui::SetNextWindowSize( ImVec2( width + fs * 1.6f, 0.0f ) );
	ImGui::SetNextWindowBgAlpha( 0.62f );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, fs * 0.6f );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( fs * 0.8f, fs * 0.55f ) );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( ch, fs * 0.12f ) );
	ImGui::Begin( "##profiler", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
		| ImGuiWindowFlags_NoBringToFrontOnFocus );

	// what draws, at which size
	ImGui::PushStyleColor( ImGuiCol_Text, colText );
	ImGui::TextUnformatted( gpu ? "Vulkan" : "GPU" );
	ImGui::PopStyleColor();
	ImGui::SameLine();
	ImGui::PushStyleColor( ImGuiCol_Text, colDim );
	if ( gpu ) {
		ImGui::Text( "%i x %i, %ix MSAA", gpu->width, gpu->height, gpu->samples );
		ImGui::TextUnformatted( gpu->device );
	} else {
		ImGui::Text( "%i x %i", cls.glconfig.vidWidth, cls.glconfig.vidHeight );
		ImGui::TextUnformatted( "no timings of the device (rd-vulkan-vision only)" );
	}
	ImGui::PopStyleColor();

	// frames a second, large
	ImGui::PushFont( NULL, ImGui::GetStyle().FontSizeBase * 2.8f );
	ImGui::PushStyleColor( ImGuiCol_Text, Prof_FpsColor( cpu.fps ) );
	ImGui::Text( "%.0f", cpu.fps );
	ImGui::PopStyleColor();
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::PushStyleColor( ImGuiCol_Text, colDim );
	ImGui::SetCursorPosY( ImGui::GetCursorPosY() + fs * 1.3f );
	ImGui::TextUnformatted( "FPS" );
	ImGui::PopStyleColor();

	Com_sprintf( value, sizeof( value ), "%.2f ms", cpu.frameMs );
	Com_sprintf( remark, sizeof( remark ), "max %.1f", cpu.frameMaxMs );
	Prof_Row( "Frame", colText, value, remark );
	if ( gpu ) {
		Com_sprintf( value, sizeof( value ), "%.2f ms", gpu->frameMs );
		Com_sprintf( remark, sizeof( remark ), "max %.1f", gpu->frameMaxMs );
		Prof_Row( "GPU", colGpu, value, remark );
	}
	Prof_Graph( gpu, width, fs * 3.0f );

	if ( gpu ) {
		Prof_Separator();
		Prof_Passes( gpu, width );
		Prof_Separator();
		Com_sprintf( remark, sizeof( remark ), "%s CPU", Prof_Count( gpu->cpuTriangles ) );
		if ( gpu->levelOverdraw > 0.0f ) {
			// (how often the fragment shader ran for the opaque level a pixel: 1 would be each pixel shaded once)
			Com_sprintf( value, sizeof( value ), "%.2fx", gpu->levelOverdraw );
			Prof_Row( "Overdraw", gpu->levelOverdraw > 3.0f ? colBad : gpu->levelOverdraw > 2.0f ? colFair : colGood, value, "level" );
		}
		Prof_Row( "Draws", colText, Prof_Count( gpu->draws ), "" );
		Prof_Row( "Triangles", colText, Prof_Count( gpu->triangles ), remark );
		Com_sprintf( remark, sizeof( remark ), "%i skeletons", gpu->skeletons );
		Prof_Row( "Bones", colText, Prof_Count( gpu->bones ), remark );
		Prof_Row( "Transforms", colText, Prof_Count( gpu->transforms ), "" );
	}

	ImGui::End();
	ImGui::PopStyleVar( 4 );
	ImGui::PopFont();
}

/*
===============
CL_Profiler_Frame

Every frame of the client, before its debug menus are drawn: counts the frame and opens or closes the panel as
v_profiler says
===============
*/
void CL_Profiler_Frame( void ) {
	static int tried = -1;		// (a renderer without the debug menus says so once a change of the cvar)

	Prof_CpuFrame();
	if ( v_profiler && (qboolean)( v_profiler->integer != 0 ) != profWindow.open && tried != v_profiler->modificationCount ) {
		tried = v_profiler->modificationCount;
		CL_ImGui_SetOpen( &profWindow, (qboolean)( v_profiler->integer != 0 ) );
	}
}

void CL_Profiler_Init( void ) {
	profWindow.name = "GPU profiler";
	profWindow.overlay = qtrue;
	profWindow.draw = Prof_Draw;
	CL_ImGui_Register( &profWindow );
	v_profiler = Cvar_Get( "v_profiler", "0", CVAR_ARCHIVE_ND, "GPU profiler over the game: frames a second, the frame on the CPU and on the GPU, a graph of the last frames and what each part of the frame takes on the GPU (rd-vulkan-vision). 1 top left, 2 top right" );
}
