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

// cl_profstats.cpp: the readings of the full profiler (v_profiler 3 and 4) that come from the system, not from the
// engine: the time each thread of the program ran on a processor (Sys_ThreadTimes), and load, clocks, temperature,
// power and memory of the card from its driver: NVML for NVIDIA (Linux and Windows, loaded when first asked, the
// driver brings it), the files of amdgpu for AMD (Linux). Intel cards and macOS get no values of the driver.
//
// A reading runs as a job (com_jobs.cpp) every half second: asking a driver may take a millisecond, and the frame
// must not wait for it. The job touches only what is in this file and the system, never the engine.

#include "client.h"
#include "cl_profstats.h"
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32	0x00000800	// (Windows 7 with KB2533623 and later)
#endif
#else
#include <dirent.h>
#include <dlfcn.h>
#endif

#define STATS_MSEC			500
#define STATS_MAX_THREADS	256

/*
===============================================================================

NVML, the library of the NVIDIA driver (nvml.h of the CUDA toolkit; only what is used, with its numbers)

===============================================================================
*/

typedef int						nvmlReturn_t;			// 0: NVML_SUCCESS
typedef struct nvmlDevice_st	*nvmlDevice_t;

typedef struct {
	unsigned int		gpu, memory;					// percent of the last sample period
} nvmlUtilization_t;

typedef struct {
	unsigned long long	total, free, used;				// bytes
} nvmlMemory_t;

#define NVML_CLOCK_GRAPHICS		0
#define NVML_CLOCK_MEM			2
#define NVML_TEMPERATURE_GPU	0

static struct {
	qboolean		tried, ready;
	nvmlDevice_t	device;
	nvmlReturn_t	( *Init )( void );
	nvmlReturn_t	( *HandleByPciBusId )( const char *pciBusId, nvmlDevice_t *device );
	nvmlReturn_t	( *HandleByUUID )( const char *uuid, nvmlDevice_t *device );
	nvmlReturn_t	( *Utilization )( nvmlDevice_t device, nvmlUtilization_t *utilization );
	nvmlReturn_t	( *Clock )( nvmlDevice_t device, int type, unsigned int *mhz );
	nvmlReturn_t	( *Temperature )( nvmlDevice_t device, int sensor, unsigned int *celsius );
	nvmlReturn_t	( *Power )( nvmlDevice_t device, unsigned int *milliwatts );
	nvmlReturn_t	( *PowerLimit )( nvmlDevice_t device, unsigned int *milliwatts );
	nvmlReturn_t	( *Memory )( nvmlDevice_t device, nvmlMemory_t *memory );
} nvml;

// the card of the renderer, as the job got it to read
typedef struct statsCard_s {
	unsigned int	vendorID;
	qboolean		hasPci;
	unsigned int	pciDomain, pciBus, pciDevice, pciFunction;
	byte			uuid[16];
} statsCard_t;

static void *Stats_Symbol( void *lib, const char *name ) {
#ifdef _WIN32
	return (void *)GetProcAddress( (HMODULE)lib, name );
#else
	return dlsym( lib, name );
#endif
}

// once: the library, and the card the renderer draws on among those it knows, by its address on the bus or its UUID
static qboolean Stats_NvmlOpen( const statsCard_t *card ) {
	void *lib;
	char  id[64];

	if ( nvml.tried ) {
		return nvml.ready;
	}
	nvml.tried = qtrue;
#ifdef _WIN32
	// (from the folder of the system only: a nvml.dll put next to the game is never loaded)
	lib = (void *)LoadLibraryExA( "nvml.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32 );
#elif defined(__APPLE__)
	lib = NULL;
#else
	lib = dlopen( "libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL );
#endif
	if ( !lib ) {
		return qfalse;
	}
	*(void **)&nvml.Init = Stats_Symbol( lib, "nvmlInit_v2" );
	*(void **)&nvml.HandleByPciBusId = Stats_Symbol( lib, "nvmlDeviceGetHandleByPciBusId_v2" );
	*(void **)&nvml.HandleByUUID = Stats_Symbol( lib, "nvmlDeviceGetHandleByUUID" );
	*(void **)&nvml.Utilization = Stats_Symbol( lib, "nvmlDeviceGetUtilizationRates" );
	*(void **)&nvml.Clock = Stats_Symbol( lib, "nvmlDeviceGetClockInfo" );
	*(void **)&nvml.Temperature = Stats_Symbol( lib, "nvmlDeviceGetTemperature" );
	*(void **)&nvml.Power = Stats_Symbol( lib, "nvmlDeviceGetPowerUsage" );
	*(void **)&nvml.PowerLimit = Stats_Symbol( lib, "nvmlDeviceGetEnforcedPowerLimit" );
	*(void **)&nvml.Memory = Stats_Symbol( lib, "nvmlDeviceGetMemoryInfo" );
	if ( !nvml.Init || !nvml.HandleByPciBusId || !nvml.HandleByUUID || nvml.Init() != 0 ) {
		return qfalse;		// (the library stays loaded: unloading it while a driver thread may run is worse)
	}
	if ( card->hasPci ) {
		snprintf( id, sizeof( id ), "%x:%x:%x.%x", card->pciDomain, card->pciBus, card->pciDevice, card->pciFunction );
		nvml.ready = (qboolean)( nvml.HandleByPciBusId( id, &nvml.device ) == 0 );
	}
	if ( !nvml.ready ) {
		// (the UUID Vulkan gives an NVIDIA card is the one of NVML)
		const byte *u = card->uuid;

		snprintf( id, sizeof( id ), "GPU-%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15] );
		nvml.ready = (qboolean)( nvml.HandleByUUID( id, &nvml.device ) == 0 );
	}
	return nvml.ready;
}

static qboolean Stats_ReadNvml( const statsCard_t *card, profStats_t *out ) {
	nvmlUtilization_t	utilization;
	nvmlMemory_t		memory;
	unsigned int		value;

	if ( !Stats_NvmlOpen( card ) ) {
		return qfalse;
	}
	out->cardSource = "NVML";
	if ( nvml.Utilization && nvml.Utilization( nvml.device, &utilization ) == 0 ) {
		out->loadPercent = (int)utilization.gpu;
	}
	if ( nvml.Clock && nvml.Clock( nvml.device, NVML_CLOCK_GRAPHICS, &value ) == 0 ) {
		out->clockMHz = (int)value;
	}
	if ( nvml.Clock && nvml.Clock( nvml.device, NVML_CLOCK_MEM, &value ) == 0 ) {
		out->memoryClockMHz = (int)value;
	}
	if ( nvml.Temperature && nvml.Temperature( nvml.device, NVML_TEMPERATURE_GPU, &value ) == 0 ) {
		out->temperature = (int)value;
	}
	if ( nvml.Power && nvml.Power( nvml.device, &value ) == 0 ) {
		out->powerW = value / 1000.0f;
	}
	if ( nvml.PowerLimit && nvml.PowerLimit( nvml.device, &value ) == 0 ) {
		out->powerLimitW = value / 1000.0f;
	}
	if ( nvml.Memory && nvml.Memory( nvml.device, &memory ) == 0 ) {
		out->vramUsedMB = (float)( memory.used / ( 1024.0 * 1024.0 ) );
		out->vramTotalMB = (float)( memory.total / ( 1024.0 * 1024.0 ) );
	}
	return qtrue;
}

/*
===============================================================================

amdgpu: the files of the kernel driver under the card's address on the bus (Linux)

===============================================================================
*/

#if defined(__linux__)
// a number out of a small file, qfalse when it is not there
static qboolean Stats_ReadNumber( const char *path, long long *value ) {
	FILE	*file = fopen( path, "r" );
	int		got;

	if ( !file ) {
		return qfalse;
	}
	got = fscanf( file, "%lld", value );
	fclose( file );
	return (qboolean)( got == 1 );
}

static qboolean Stats_ReadAmd( const statsCard_t *card, profStats_t *out ) {
	static char	hwmon[MAX_OSPATH];		// (found once; the job runs one at a time)
	char		dev[96], path[MAX_OSPATH + 32];
	long long	value;

	if ( !card->hasPci ) {
		return qfalse;
	}
	snprintf( dev, sizeof( dev ), "/sys/bus/pci/devices/%04x:%02x:%02x.%x", card->pciDomain, card->pciBus, card->pciDevice, card->pciFunction );
	snprintf( path, sizeof( path ), "%s/gpu_busy_percent", dev );
	if ( !Stats_ReadNumber( path, &value ) ) {
		return qfalse;		// (not amdgpu, or too old a kernel)
	}
	out->cardSource = "amdgpu";
	out->loadPercent = (int)value;
	snprintf( path, sizeof( path ), "%s/mem_info_vram_used", dev );
	if ( Stats_ReadNumber( path, &value ) ) {
		out->vramUsedMB = (float)( value / ( 1024.0 * 1024.0 ) );
	}
	snprintf( path, sizeof( path ), "%s/mem_info_vram_total", dev );
	if ( Stats_ReadNumber( path, &value ) ) {
		out->vramTotalMB = (float)( value / ( 1024.0 * 1024.0 ) );
	}
	if ( !hwmon[0] ) {
		DIR				*dir;
		struct dirent	*entry;

		char			folder[128];

		snprintf( folder, sizeof( folder ), "%s/hwmon", dev );
		if ( ( dir = opendir( folder ) ) != NULL ) {
			while ( ( entry = readdir( dir ) ) != NULL ) {
				if ( !strncmp( entry->d_name, "hwmon", 5 ) ) {
					snprintf( hwmon, sizeof( hwmon ), "%s/%s", folder, entry->d_name );
					break;
				}
			}
			closedir( dir );
		}
	}
	if ( hwmon[0] ) {
		// temp1 is the edge of the chip, as the tools show it; freq1 the shader clock, freq2 that of the memory
		snprintf( path, sizeof( path ), "%s/temp1_input", hwmon );
		if ( Stats_ReadNumber( path, &value ) ) {
			out->temperature = (int)( value / 1000 );
		}
		snprintf( path, sizeof( path ), "%s/freq1_input", hwmon );
		if ( Stats_ReadNumber( path, &value ) ) {
			out->clockMHz = (int)( value / 1000000 );
		}
		snprintf( path, sizeof( path ), "%s/freq2_input", hwmon );
		if ( Stats_ReadNumber( path, &value ) ) {
			out->memoryClockMHz = (int)( value / 1000000 );
		}
		// (older kernels have the average, newer ones the input; microwatts)
		snprintf( path, sizeof( path ), "%s/power1_average", hwmon );
		if ( Stats_ReadNumber( path, &value ) ) {
			out->powerW = value / 1000000.0f;
		} else {
			snprintf( path, sizeof( path ), "%s/power1_input", hwmon );
			if ( Stats_ReadNumber( path, &value ) ) {
				out->powerW = value / 1000000.0f;
			}
		}
		snprintf( path, sizeof( path ), "%s/power1_cap", hwmon );
		if ( Stats_ReadNumber( path, &value ) ) {
			out->powerLimitW = value / 1000000.0f;
		}
	}
	return qtrue;
}
#endif

/*
===============================================================================

The reading

===============================================================================
*/

static struct {
	jobGroup_t			*group;
	qboolean			running;
	int					started;		// Sys_Milliseconds()

	// given to the job, not changed while it runs
	unsigned long long	mainId;
	statsCard_t			card;

	// the job's own
	sysThread_t			now[STATS_MAX_THREADS], before[STATS_MAX_THREADS];
	int					numNow, numBefore;
	long long			beforeUsec;
	profStats_t			result;
} stats;

static profStats_t	shown;

static long long Stats_Usec( void ) {
	return (long long)std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

static int QDECL Stats_CompareRows( const void *a, const void *b ) {
	const float d = ( (const profThreadRow_t *)b )->percent - ( (const profThreadRow_t *)a )->percent;

	return d > 0.0f ? 1 : d < 0.0f ? -1 : 0;
}

// the job: what each thread took since the reading before, and the card
static void Stats_Read( void * ) {
	static profThreadRow_t	all[STATS_MAX_THREADS];
	profStats_t				*out = &stats.result;
	const long long			now = Stats_Usec();
	const double			wallNsec = ( now - stats.beforeUsec ) * 1000.0;
	int						numAll = 0;

	memset( out, 0, sizeof( *out ) );
	out->loadPercent = out->clockMHz = out->memoryClockMHz = out->temperature = -1;
	out->powerW = out->powerLimitW = -1.0f;

	stats.numNow = Sys_ThreadTimes( stats.now, STATS_MAX_THREADS );
	out->numThreads = stats.numNow;
	for ( int i = 0; i < stats.numNow && stats.beforeUsec && wallNsec > 0.0; i++ ) {
		const sysThread_t *thread = &stats.now[i];

		// (a thread that is new since the reading before has nothing to compare with)
		for ( int j = 0; j < stats.numBefore; j++ ) {
			if ( stats.before[j].id == thread->id && thread->cpuNsec >= stats.before[j].cpuNsec ) {
				profThreadRow_t *row = &all[numAll++];

				Q_strncpyz( row->name, thread->id == stats.mainId ? "main" : thread->name, sizeof( row->name ) );
				row->percent = (float)( ( thread->cpuNsec - stats.before[j].cpuNsec ) * 100.0 / wallNsec );
				out->totalPercent += row->percent;
				break;
			}
		}
	}
	qsort( all, numAll, sizeof( all[0] ), Stats_CompareRows );
	out->numRows = Q_min( numAll, PROF_THREAD_ROWS );
	memcpy( out->rows, all, out->numRows * sizeof( all[0] ) );
	for ( int i = out->numRows; i < numAll; i++ ) {
		out->restPercent += all[i].percent;
		out->restThreads++;
	}
	memcpy( stats.before, stats.now, stats.numNow * sizeof( stats.now[0] ) );
	stats.numBefore = stats.numNow;
	stats.beforeUsec = now;

	if ( stats.card.vendorID == 0x10de ) {
		Stats_ReadNvml( &stats.card, out );
#if defined(__linux__)
	} else if ( stats.card.vendorID == 0x1002 ) {
		Stats_ReadAmd( &stats.card, out );
#endif
	}
}

/*
===============
CL_ProfStats_Frame

Every frame while the full panel is open: takes a reading that is done, starts the next one every half second
===============
*/
void CL_ProfStats_Frame( const gpuProfile_t *gpu ) {
	const int now = Sys_Milliseconds();

	if ( !stats.group ) {
		stats.group = Com_JobGroupNew();
		stats.mainId = Sys_ThreadId();		// (this is the main thread)
	}
	if ( stats.running ) {
		if ( !Com_JobGroupDone( stats.group ) ) {
			return;
		}
		const int serial = shown.serial + 1;

		stats.running = qfalse;
		shown = stats.result;
		shown.serial = serial;
	}
	if ( stats.started && now - stats.started < STATS_MSEC ) {
		return;
	}
	if ( gpu ) {
		stats.card.vendorID = gpu->vendorID;
		stats.card.hasPci = gpu->hasPci;
		stats.card.pciDomain = gpu->pciDomain;
		stats.card.pciBus = gpu->pciBus;
		stats.card.pciDevice = gpu->pciDevice;
		stats.card.pciFunction = gpu->pciFunction;
		memcpy( stats.card.uuid, gpu->deviceUUID, sizeof( stats.card.uuid ) );
	}
	stats.started = now;
	stats.running = qtrue;
	Com_JobAdd( stats.group, Stats_Read, NULL );
}

const profStats_t *CL_ProfStats( void ) {
	return &shown;
}
