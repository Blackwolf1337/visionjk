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
//
// v_profiler 3 and 4 show more, in two columns: what the frame of the main thread went into (Com_FrameProfile, the
// "frame: ..." pushes of com_load.cpp), what is waited for and so what limits the frames, every thread of the
// program with its share of a processor, the workers of com_jobs.cpp, and of the card its load, clocks, temperature,
// power and memory (cl_profstats.cpp), the present mode and the most of each part of the frame.

#include "client.h"
#include "cl_imgui.h"
#include "cl_profstats.h"
#include "imgui/imgui.h"
#include <thread>

#ifdef JP_SPATIAL
// sound/spatial/snd_spatial_gpu.cpp: the echo of v_spatial traced on the graphics card, by a Vulkan device of its own
// that the timestamps of the renderer do not see
qboolean S_SpatialGpuTimes( double *gpuMs, int *batches );
#endif

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

// the echo of the sound on the graphics card (v_spatialGpu), over the same windows
static struct {
	qboolean	on, primed;
	double		lastMs;
	int			lastBatches;
	float		msPerFrame, tripsPerSecond;
} snd;

// the frame of the main thread for the full panel, over the same windows: what each part took a frame
#define PROF_PARTS		( FRAME_PROFILE_PARTS + 2 )

static struct {
	qboolean		on, valid;
	frameProfile_t	before;					// the sums when the window before closed
	int				numParts;
	const char		*names[PROF_PARTS];
	float			ms[PROF_PARTS];
	float			frameMs, idleMs, waitGpuMs, waitDisplayMs, busyMs;
	int				jobsBefore;
	double			jobsBusyBefore;
	float			jobsPerSecond, jobsBusyPercent;
} parts;

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

// the names of the parts as the panel shows them
static const char *Prof_PartName( const char *name ) {
	static const char *const names[][2] = {
		{ "input", "input, net" },
		{ "game", "game module" },
		{ "client", "client rest" },
		{ "drawn", "end of frame" },
	};

	for ( size_t i = 0; i < ARRAY_LEN( names ); i++ ) {
		if ( !strcmp( name, names[i][0] ) ) {
			return names[i][1];
		}
	}
	return name;
}

// the window closes for the frame of the main thread: the sums since the window before, a frame
static void Prof_PartsClose( int windowMs ) {
	const frameProfile_t	*now = Com_FrameProfile();
	const int				frames = now->frames - parts.before.frames;
	const int				workers = Com_JobThreads();
	float					named = 0.0f;
	int						jobsRun;
	double					jobsBusy;

	parts.valid = qfalse;
	if ( frames > 0 ) {
		parts.numParts = 0;
		parts.waitGpuMs = parts.waitDisplayMs = 0.0f;
		for ( int i = 0; i < now->numParts && parts.numParts < PROF_PARTS - 2; i++ ) {
			double	was = 0.0;
			float	ms;

			for ( int j = 0; j < parts.before.numParts; j++ ) {
				if ( !strcmp( parts.before.names[j], now->names[i] ) ) {
					was = parts.before.ms[j];
					break;
				}
			}
			ms = (float)( ( now->ms[i] - was ) / frames );
			if ( !strcmp( now->names[i], "wait for gpu" ) ) {
				parts.waitGpuMs = ms;
			} else if ( !strcmp( now->names[i], "wait for display" ) ) {
				parts.waitDisplayMs = ms;
			}
			parts.names[parts.numParts] = Prof_PartName( now->names[i] );
			parts.ms[parts.numParts++] = ms;
			named += ms;
		}
		parts.frameMs = (float)( ( now->frameMs - parts.before.frameMs ) / frames );
		parts.idleMs = (float)( ( now->idleMs - parts.before.idleMs ) / frames );
		parts.names[parts.numParts] = "frame limit";
		parts.ms[parts.numParts++] = parts.idleMs;
		parts.names[parts.numParts] = "not named";
		parts.ms[parts.numParts++] = Q_max( 0.0f, parts.frameMs - parts.idleMs - named );
		parts.busyMs = Q_max( 0.0f, parts.frameMs - parts.idleMs - parts.waitGpuMs - parts.waitDisplayMs );
		parts.valid = qtrue;
	}
	parts.before = *now;

	Com_JobStats( &jobsRun, &jobsBusy );
	parts.jobsPerSecond = ( jobsRun - parts.jobsBefore ) * 1000.0f / Q_max( windowMs, 1 );
	parts.jobsBusyPercent = workers ? (float)( ( jobsBusy - parts.jobsBusyBefore ) * 100.0 / ( (double)Q_max( windowMs, 1 ) * workers ) ) : 0.0f;
	parts.jobsBefore = jobsRun;
	parts.jobsBusyBefore = jobsBusy;
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
#ifdef JP_SPATIAL
		double	soundMs;
		int		soundBatches;

		snd.on = S_SpatialGpuTimes( &soundMs, &soundBatches );
		if ( snd.on && snd.primed ) {
			snd.msPerFrame = (float)( ( soundMs - snd.lastMs ) / cpu.frames );
			snd.tripsPerSecond = ( soundBatches - snd.lastBatches ) * 1000.0f / Q_max( cpu.sumMs, 1 );
		}
		snd.primed = snd.on;
		snd.lastMs = soundMs;
		snd.lastBatches = soundBatches;
#endif
		if ( parts.on ) {
			Prof_PartsClose( cpu.sumMs );
		}
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

// a list with a bar of each one's share of the whole: name, value, the most of it (for the parts of the frame on
// the device), the most first
typedef struct profBar_s {
	const char	*name;
	float		value, most;
} profBar_t;

static void Prof_Bars( profBar_t *bars, int count, float whole, const char *format, qboolean withMost, float tiny, float warn, float width ) {
	const float	ch = ImGui::CalcTextSize( "0" ).x;
	const float	barX = ch * ( withMost ? 32.0f : 26.0f );
	int			i, j;

	for ( i = 1; i < count; i++ ) {
		const profBar_t bar = bars[i];

		for ( j = i; j > 0 && bars[j - 1].value < bar.value; j-- ) {
			bars[j] = bars[j - 1];
		}
		bars[j] = bar;
	}
	for ( i = 0; i < count; i++ ) {
		const profBar_t	*bar = &bars[i];
		const qboolean	small = (qboolean)( bar->value < tiny );
		const float		share = whole > 0.0f ? Com_Clamp( 0.0f, 1.0f, bar->value / whole ) : 0.0f;
		char			name[18];

		// (a long name of a thread would run into the numbers)
		Q_strncpyz( name, bar->name, sizeof( name ) );
		ImGui::PushStyleColor( ImGuiCol_Text, small ? colDim : colText );
		ImGui::TextUnformatted( name );
		ImGui::SameLine( ch * 18.0f );
		ImGui::Text( format, bar->value );
		ImGui::PopStyleColor();
		if ( withMost ) {
			ImGui::SameLine( ch * 25.0f );
			ImGui::PushStyleColor( ImGuiCol_Text, colDim );
			ImGui::Text( format, bar->most );
			ImGui::PopStyleColor();
		}
		ImGui::SameLine( barX );

		const ImVec2	at = ImGui::GetCursorScreenPos();
		const float		lineH = ImGui::GetTextLineHeight();
		const float		barW = Q_max( 0.0f, width - barX );

		ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( at.x, at.y + lineH * 0.25f ),
			ImVec2( at.x + Q_max( barW * share, small ? 0.0f : 1.0f ), at.y + lineH * 0.75f ), share > warn ? colFair : colGpu, 1.0f );
		ImGui::Dummy( ImVec2( barW, lineH ) );
	}
}

// what each part of the frame took on the device
static void Prof_Passes( const gpuProfile_t *gpu, qboolean withMost, float width ) {
	profBar_t bars[GPU_PROFILE_PASSES];

	for ( int i = 0; i < gpu->numPasses; i++ ) {
		bars[i].name = gpu->passNames[i];
		bars[i].value = gpu->passMs[i];
		bars[i].most = gpu->passMaxMs[i];
	}
	Prof_Bars( bars, gpu->numPasses, gpu->frameMs, "%6.3f", withMost, 0.01f, 0.2f, width );
}

// what the last frame was made of
static void Prof_Counts( const gpuProfile_t *gpu ) {
	char value[32], remark[32];

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

// a title over a column: the name bright, then what it is dim
static void Prof_Title( const char *title, const char *what ) {
	ImGui::PushStyleColor( ImGuiCol_Text, colText );
	ImGui::TextUnformatted( title );
	ImGui::PopStyleColor();
	ImGui::SameLine();
	ImGui::PushStyleColor( ImGuiCol_Text, colDim );
	ImGui::TextUnformatted( what );
	ImGui::PopStyleColor();
}

// what limits the frames: the time they wait for the display or com_maxfps, for the card, or else the processor
static const char *Prof_Limit( ImU32 *color ) {
	*color = colText;
	if ( !parts.valid || parts.frameMs <= 0.0f ) {
		return "...";
	}
	if ( parts.idleMs + parts.waitDisplayMs > 0.25f * parts.frameMs ) {
		*color = colGood;
		return "display / com_maxfps";
	}
	if ( parts.waitGpuMs > 0.1f * parts.frameMs ) {
		*color = colGpu;
		return "GPU";
	}
	*color = colFair;
	return "CPU";
}

// the left column of the full panel: the processor
static void Prof_CpuColumn( float width ) {
	const profStats_t	*st = CL_ProfStats();
	const int			hardware = Q_max( 1, (int)std::thread::hardware_concurrency() );
	char				value[32], remark[48];
	ImU32				color;
	const char			*limit = Prof_Limit( &color );

	Prof_Title( "CPU", Sys_ProcessorName() );
	Com_sprintf( value, sizeof( value ), "%.2f ms", parts.busyMs );
	Prof_Row( "Busy", colText, value, "a frame, without waits" );
	Prof_Row( "Limit", color, limit, "" );
	Prof_Separator();
	if ( parts.valid ) {
		profBar_t bars[PROF_PARTS];

		for ( int i = 0; i < parts.numParts; i++ ) {
			bars[i].name = parts.names[i];
			bars[i].value = parts.ms[i];
			bars[i].most = 0.0f;
		}
		Prof_Bars( bars, parts.numParts, parts.frameMs, "%6.3f", qfalse, 0.01f, 2.0f, width );
	}
	Prof_Separator();
	Com_sprintf( value, sizeof( value ), "%.0f%%", st->totalPercent );
	Com_sprintf( remark, sizeof( remark ), "of %i%%, %i threads", hardware * 100, st->numThreads );
	Prof_Row( "Process", colText, value, remark );
	if ( st->numRows ) {
		profBar_t bars[PROF_THREAD_ROWS + 1];
		int count = 0;

		for ( int i = 0; i < st->numRows; i++, count++ ) {
			bars[count].name = st->rows[i].name;
			bars[count].value = st->rows[i].percent;
			bars[count].most = 0.0f;
		}
		Prof_Bars( bars, count, 100.0f, "%5.1f%%", qfalse, 0.5f, 0.85f, width );
		if ( st->restThreads ) {
			Com_sprintf( value, sizeof( value ), "%5.1f%%", st->restPercent );
			Com_sprintf( remark, sizeof( remark ), "%i more", st->restThreads );
			ImGui::PushStyleColor( ImGuiCol_Text, colDim );
			ImGui::TextUnformatted( remark );
			ImGui::SameLine( ImGui::CalcTextSize( "0" ).x * 18.0f );
			ImGui::TextUnformatted( value );
			ImGui::PopStyleColor();
		}
	}
	Com_sprintf( value, sizeof( value ), "%.0f/s", parts.jobsPerSecond );
	Com_sprintf( remark, sizeof( remark ), "%i workers, %.0f%% busy", Com_JobThreads(), parts.jobsBusyPercent );
	Prof_Row( "Jobs", colText, value, remark );
}

// the right column of the full panel: the card
static void Prof_GpuColumn( const gpuProfile_t *gpu, float width ) {
	const profStats_t	*st = CL_ProfStats();
	char				value[32], remark[48];

	Prof_Title( "GPU", gpu ? gpu->device : "" );
	if ( st->cardSource ) {
		if ( st->loadPercent >= 0 ) {
			Com_sprintf( value, sizeof( value ), "%i%%", st->loadPercent );
			remark[0] = '\0';
			if ( st->clockMHz >= 0 ) {
				Com_sprintf( remark, sizeof( remark ), st->memoryClockMHz >= 0 ? "%i / %i MHz" : "%i MHz", st->clockMHz, st->memoryClockMHz );
			}
			Prof_Row( "Load", st->loadPercent >= 95 ? colFair : colText, value, remark );
		}
		if ( st->temperature >= 0 ) {
			Com_sprintf( value, sizeof( value ), "%i C", st->temperature );
			Prof_Row( "Temperature", st->temperature >= 85 ? colBad : st->temperature >= 75 ? colFair : colText, value, st->cardSource );
		}
		if ( st->powerW >= 0.0f ) {
			Com_sprintf( value, sizeof( value ), "%.0f W", st->powerW );
			remark[0] = '\0';
			if ( st->powerLimitW > 0.0f ) {
				Com_sprintf( remark, sizeof( remark ), "of %.0f W", st->powerLimitW );
			}
			Prof_Row( "Power", colText, value, remark );
		}
	}
	if ( gpu && gpu->vramBudgetMB > 0.0f ) {
		// (the budget is what the driver lets this program have now, less when other programs hold memory)
		Com_sprintf( value, sizeof( value ), "%.2f GB", gpu->vramUsedMB / 1024.0f );
		Com_sprintf( remark, sizeof( remark ), "budget %.1f GB", gpu->vramBudgetMB / 1024.0f );
		Prof_Row( "VRAM game", gpu->vramUsedMB > 0.9f * gpu->vramBudgetMB ? colBad : colText, value, remark );
	}
	if ( st->vramTotalMB > 0.0f ) {
		Com_sprintf( value, sizeof( value ), "%.2f GB", st->vramUsedMB / 1024.0f );
		Com_sprintf( remark, sizeof( remark ), "of %.1f GB", st->vramTotalMB / 1024.0f );
		Prof_Row( "VRAM card", colText, value, remark );
	}
	if ( gpu ) {
		Com_sprintf( remark, sizeof( remark ), "%i images, %i frames", gpu->swapImages, gpu->framesInFlight );
		Prof_Row( "Present", colText, gpu->presentMode, remark );
		Prof_Separator();
		Prof_Passes( gpu, qtrue, width );
		Prof_Separator();
		Prof_Counts( gpu );
	}
}

static void Prof_Draw( debugWindow_t *window ) {
	ImGuiIO			&io = ImGui::GetIO();
	gpuProfile_t	gpuStore;
	const qboolean	hasGpu = (qboolean)( re && re->ext.GpuProfile && re->ext.GpuProfile( &gpuStore ) && gpuStore.serial );
	const gpuProfile_t *gpu = hasGpu ? &gpuStore : NULL;
	const ImGuiViewport *view = ImGui::GetMainViewport();
	// (the HUD has its clock and the leader top right)
	const qboolean	left = (qboolean)( v_profiler->integer != 2 && v_profiler->integer != 4 );
	const qboolean	full = (qboolean)( v_profiler->integer >= 3 );
	char			value[32], remark[32];

	if ( full ) {
		CL_ProfStats_Frame( gpu );
	}
	ImGui::PushFont( io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : NULL, 0.0f );
	const float		fs = ImGui::GetFontSize();
	const float		ch = ImGui::CalcTextSize( "0" ).x;
	const float		column = ch * ( full ? 42.0f : 36.0f );
	const float		gap = ch * 3.0f;
	const float		width = full ? column * 2.0f + gap : column;
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
	if ( snd.on ) {
		// (on a device of its own, not in the GPU time above: the echo of v_spatial, v_spatialGpu)
		Com_sprintf( value, sizeof( value ), "%.3f ms", snd.msPerFrame );
		Com_sprintf( remark, sizeof( remark ), "%.0f trips/s", snd.tripsPerSecond );
		Prof_Row( "Sound GPU", colGpu, value, remark );
	}
	Prof_Graph( gpu, width, fs * 3.0f );

	if ( full ) {
		Prof_Separator();
		if ( ImGui::BeginTable( "##columns", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX ) ) {
			ImGui::TableSetupColumn( "cpu", ImGuiTableColumnFlags_WidthFixed, column + gap );
			ImGui::TableSetupColumn( "gpu", ImGuiTableColumnFlags_WidthFixed, column );
			ImGui::TableNextColumn();
			Prof_CpuColumn( column );
			ImGui::TableNextColumn();
			Prof_GpuColumn( gpu, column );
			ImGui::EndTable();
		}
	} else if ( gpu ) {
		Prof_Separator();
		Prof_Passes( gpu, qfalse, width );
		Prof_Separator();
		Prof_Counts( gpu );
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

	// (the frame of the main thread is taken apart only for the full panel: the pushes cost a little every frame)
	const qboolean full = (qboolean)( v_profiler && v_profiler->integer >= 3 && profWindow.open );

	if ( full != parts.on ) {
		Com_FrameProfileOn( full );
		if ( full ) {
			memset( &parts, 0, sizeof( parts ) );
		}
		parts.on = full;
	}
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
	v_profiler = Cvar_Get( "v_profiler", "0", CVAR_ARCHIVE_ND, "GPU profiler over the game: frames a second, the frame on the CPU and on the GPU, a graph of the last frames and what each part of the frame takes on the GPU (rd-vulkan-vision). 1 top left, 2 top right, 3 and 4 the same with more in two columns: the main thread, the threads of the program, the card with its load, clocks, temperature, power and memory" );
}
