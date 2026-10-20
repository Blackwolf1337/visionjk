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

// vm_hotpatch.cpp -- developer builds only (cmake -DBuildDevHotpatch=ON, Linux x86_64, GCC): puts new versions
// of functions of the game, cgame and ui modules into them while they run.
//
// tools/hotpatch.py compiles the changed source files of a module into a patch library and writes a plan for it.
// Each frame (every HP_CHECK_MSEC) the engine looks at <dir>/<module>.patch, the list of the patches, and loads
// the new ones in order:
//	ptr		the patch's GOT entries for globals point at the running module's variables, not at its own copies
//	copy	file static variables of a changed file take over the values of the old version (live patches only)
//	jmp		the start of the old function becomes "jmp [rip+0]; dq new" (14 bytes, over the nops the module was
//			built with), so every caller and every stored function pointer lands in the new version
//	need	a symbol the dynamic linker had to find (libc ...) is there
// A module that is loaded again (vid_restart, a new map) gets all patches of its build at once, without copies.
// Patches are only put in between two frames, when no module code is on the stack. Code that runs in a thread
// of its own (the voice chat's audio callbacks) can be in a function while it is patched: do not patch those.

#include "qcommon/qcommon.h"

#ifdef VISIONJK_HOTPATCH

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define HP_MAX_IMAGES	64		// the module and its patches
#define HP_CHECK_MSEC	250
#define HP_JMP_SIZE		14		// FF 25 00000000 (jmp [rip+0]) and the 8 byte address

typedef struct hpImage_s {
	void		*handle;		// dlopen handle of a patch, NULL for the module itself
	uintptr_t	base;			// load address
	uintptr_t	size;			// end of the highest PT_LOAD segment
} hpImage_t;

typedef struct hpSlot_s {
	vm_t		*vm;
	char		buildId[41];
	hpImage_t	images[HP_MAX_IMAGES];	// [0] is the module, [k] patch k
	int			numImages;
	struct stat	listStat;				// of the list when it was last read: a patch that failed is tried again when it changes
} hpSlot_t;

typedef enum {
	HP_NEED,
	HP_PTR,
	HP_COPY,
	HP_JMP
} hpOp_t;

typedef struct hpRecord_s {
	hpOp_t		op;
	int			dstImage, srcImage;
	uintptr_t	dstOff, srcOff;
	intptr_t	arg;			// addend of HP_PTR, size of HP_COPY
	char		name[64];		// for messages
} hpRecord_t;

static hpSlot_t	hpSlots[MAX_VM];
static cvar_t	*v_devHotpatch;
static cvar_t	*v_devHotpatchDir;
static int		hpNextCheck;

static const byte hpEndbr64[4] = { 0xf3, 0x0f, 0x1e, 0xfa };
static const byte hpJmpOp[6] = { 0xff, 0x25, 0x00, 0x00, 0x00, 0x00 };

/*
=================
HP_Dir
=================
*/
static const char *HP_Dir( void ) {
	static char dir[MAX_OSPATH];

	if ( v_devHotpatchDir->string[0] )
		Q_strncpyz( dir, v_devHotpatchDir->string, sizeof( dir ) );
	else
		Com_sprintf( dir, sizeof( dir ), "%s/hotpatch", Cvar_VariableString( "fs_basepath" ) );
	return dir;
}

/*
=================
HP_ImageInfo

load address, size and build id of a loaded library
=================
*/
typedef struct hpPhdrSearch_s {
	uintptr_t	base;
	const char	*name;
	uintptr_t	size;
	char		*buildId;		// 41 bytes or NULL
	qboolean	found;
} hpPhdrSearch_t;

static int HP_PhdrCallback( struct dl_phdr_info *info, size_t size, void *data ) {
	hpPhdrSearch_t *s = (hpPhdrSearch_t *)data;

	if ( info->dlpi_addr != s->base || !info->dlpi_name || strcmp( info->dlpi_name, s->name ) )
		return 0;

	s->found = qtrue;
	for ( int i = 0; i < info->dlpi_phnum; i++ ) {
		const ElfW(Phdr) *ph = &info->dlpi_phdr[i];

		if ( ph->p_type == PT_LOAD ) {
			if ( ph->p_vaddr + ph->p_memsz > s->size )
				s->size = ph->p_vaddr + ph->p_memsz;
		} else if ( ph->p_type == PT_NOTE && s->buildId ) {
			const byte *p = (const byte *)( info->dlpi_addr + ph->p_vaddr );
			const byte *end = p + ph->p_memsz;

			while ( p + sizeof( ElfW(Nhdr) ) <= end ) {
				const ElfW(Nhdr) *n = (const ElfW(Nhdr) *)p;
				const byte *desc = p + sizeof( *n ) + ( ( n->n_namesz + 3 ) & ~3u );

				if ( n->n_type == NT_GNU_BUILD_ID && n->n_namesz == 4 && !memcmp( p + sizeof( *n ), "GNU", 4 ) ) {
					unsigned len = n->n_descsz > 20 ? 20 : n->n_descsz;

					for ( unsigned j = 0; j < len; j++ )
						Com_sprintf( s->buildId + j * 2, 3, "%02x", desc[j] );
				}
				p = desc + ( ( n->n_descsz + 3 ) & ~3u );
			}
		}
	}
	return 1;
}

static qboolean HP_ImageInfo( void *handle, hpImage_t *image, char *buildId ) {
	struct link_map *lm = NULL;
	hpPhdrSearch_t s;

	if ( dlinfo( handle, RTLD_DI_LINKMAP, &lm ) || !lm )
		return qfalse;

	memset( &s, 0, sizeof( s ) );
	s.base = lm->l_addr;
	s.name = lm->l_name;
	s.buildId = buildId;
	if ( buildId )
		buildId[0] = '\0';
	dl_iterate_phdr( HP_PhdrCallback, &s );
	if ( !s.found )
		return qfalse;

	image->base = s.base;
	image->size = s.size;
	return qtrue;
}

/*
=================
HP_ReadList

<dir>/<module>.patch: "base <build id>" and "count <n>"
=================
*/
static qboolean HP_ReadList( const char *path, char *base, int baseSize, int *count ) {
	char line[256], word[64];
	FILE *f = fopen( path, "r" );

	if ( !f )
		return qfalse;

	base[0] = '\0';
	*count = 0;
	while ( fgets( line, sizeof( line ), f ) ) {
		if ( sscanf( line, "base %63s", word ) == 1 )
			Q_strncpyz( base, word, baseSize );
		else
			sscanf( line, "count %d", count );
	}
	fclose( f );
	return (qboolean)( base[0] != '\0' );
}

/*
=================
HP_ReadPlan

returns the records (Z_Free them) or NULL with a message
=================
*/
static hpRecord_t *HP_ReadPlan( const char *path, const hpSlot_t *slot, int patch, int *numRecords ) {
	char line[512], word[64], name[64];
	unsigned long long dstOff, srcOff;
	long long arg;
	int lines = 0, n = 0, planPatch = -1, dstImage, srcImage;
	qboolean baseOk = qfalse;
	hpRecord_t *records;
	FILE *f = fopen( path, "r" );

	if ( !f ) {
		Com_Printf( S_COLOR_RED "hotpatch: can't open %s\n", path );
		return NULL;
	}
	while ( fgets( line, sizeof( line ), f ) )
		lines++;
	rewind( f );

	records = (hpRecord_t *)Z_Malloc( ( lines + 1 ) * sizeof( hpRecord_t ), TAG_VM, qtrue );
	while ( fgets( line, sizeof( line ), f ) && n < lines ) {
		hpRecord_t *r = &records[n];

		name[0] = '\0';
		if ( sscanf( line, "base %63s", word ) == 1 ) {
			baseOk = (qboolean)!strcmp( word, slot->buildId );
		} else if ( sscanf( line, "patch %d", &planPatch ) == 1 ) {
		} else if ( sscanf( line, "need %d %llx %63s", &dstImage, &dstOff, name ) == 3 ) {
			r->op = HP_NEED;
			r->dstImage = dstImage; r->dstOff = dstOff;
			Q_strncpyz( r->name, name, sizeof( r->name ) );
			n++;
		} else if ( sscanf( line, "ptr %d %llx %d %llx %lld %63s", &dstImage, &dstOff, &srcImage, &srcOff, &arg, name ) >= 5 ) {
			r->op = HP_PTR;
			r->dstImage = dstImage; r->dstOff = dstOff;
			r->srcImage = srcImage; r->srcOff = srcOff;
			r->arg = arg;
			Q_strncpyz( r->name, name, sizeof( r->name ) );
			n++;
		} else if ( sscanf( line, "copy %d %llx %d %llx %lld %63s", &dstImage, &dstOff, &srcImage, &srcOff, &arg, name ) >= 5 ) {
			r->op = HP_COPY;
			r->dstImage = dstImage; r->dstOff = dstOff;
			r->srcImage = srcImage; r->srcOff = srcOff;
			r->arg = arg;
			Q_strncpyz( r->name, name, sizeof( r->name ) );
			n++;
		} else if ( sscanf( line, "jmp %d %llx %d %llx %63s", &dstImage, &dstOff, &srcImage, &srcOff, name ) >= 4 ) {
			r->op = HP_JMP;
			r->dstImage = dstImage; r->dstOff = dstOff;
			r->srcImage = srcImage; r->srcOff = srcOff;
			Q_strncpyz( r->name, name, sizeof( r->name ) );
			n++;
		}
	}
	fclose( f );

	if ( !baseOk || planPatch != patch ) {
		Com_Printf( S_COLOR_RED "hotpatch: %s is not patch %d for build %s\n", path, patch, slot->buildId );
		Z_Free( records );
		return NULL;
	}
	*numRecords = n;
	return records;
}

/*
=================
HP_InImage
=================
*/
static qboolean HP_InImage( const hpSlot_t *slot, int numImages, int image, uintptr_t off, uintptr_t len ) {
	if ( image < 0 || image >= numImages )
		return qfalse;
	return (qboolean)( off < slot->images[image].size && len <= slot->images[image].size - off );
}

/*
=================
HP_JmpSite

where the jump goes: after an endbr64, over 14 bytes of nops or an earlier jump of this file
=================
*/
static byte *HP_JmpSite( byte *p ) {
	if ( !memcmp( p, hpEndbr64, sizeof( hpEndbr64 ) ) )
		p += sizeof( hpEndbr64 );
	if ( !memcmp( p, hpJmpOp, sizeof( hpJmpOp ) ) )
		return p;
	for ( int i = 0; i < HP_JMP_SIZE; i++ ) {
		if ( p[i] != 0x90 )
			return NULL;
	}
	return p;
}

/*
=================
HP_WriteJmp
=================
*/
static qboolean HP_WriteJmp( byte *p, uintptr_t target ) {
	const uintptr_t pageSize = (uintptr_t)sysconf( _SC_PAGESIZE );
	const uintptr_t start = (uintptr_t)p & ~( pageSize - 1 );
	const size_t len = (uintptr_t)p + HP_JMP_SIZE - start;
	byte code[HP_JMP_SIZE];

	memcpy( code, hpJmpOp, sizeof( hpJmpOp ) );
	memcpy( code + sizeof( hpJmpOp ), &target, sizeof( target ) );

	// stays executable while it is written: other threads may run code on the same pages
	if ( mprotect( (void *)start, len, PROT_READ | PROT_WRITE | PROT_EXEC ) )
		return qfalse;
	memcpy( p, code, sizeof( code ) );
	mprotect( (void *)start, len, PROT_READ | PROT_EXEC );
	return qtrue;
}

/*
=================
HP_Apply

loads patch number slot->numImages; live is qfalse right after the module was loaded (no state to take over)
=================
*/
static qboolean HP_Apply( hpSlot_t *slot, qboolean live ) {
	const int patch = slot->numImages;
	const char *dir = HP_Dir();
	char soPath[MAX_OSPATH], planPath[MAX_OSPATH];
	hpImage_t *image = &slot->images[patch];
	hpRecord_t *records;
	int numRecords = 0, numJmps = 0, numCopies = 0, i;

	if ( patch >= HP_MAX_IMAGES ) {
		Com_Printf( S_COLOR_RED "hotpatch: %s has %d patches, reload it (vid_restart or a new map)\n", slot->vm->name, HP_MAX_IMAGES - 1 );
		return qfalse;
	}

	Com_sprintf( soPath, sizeof( soPath ), "%s/%s/%04d.so", dir, slot->vm->name, patch );
	Com_sprintf( planPath, sizeof( planPath ), "%s/%s/%04d.plan", dir, slot->vm->name, patch );

	memset( image, 0, sizeof( *image ) );
	image->handle = dlopen( soPath, RTLD_NOW | RTLD_LOCAL );
	if ( !image->handle ) {
		Com_Printf( S_COLOR_RED "hotpatch: %s\n", dlerror() );
		return qfalse;
	}
	if ( !HP_ImageInfo( image->handle, image, NULL ) ) {
		Com_Printf( S_COLOR_RED "hotpatch: no load address for %s\n", soPath );
		dlclose( image->handle );
		image->handle = NULL;
		return qfalse;
	}

	records = HP_ReadPlan( planPath, slot, patch, &numRecords );
	if ( !records ) {
		dlclose( image->handle );
		image->handle = NULL;
		return qfalse;
	}

	// check everything before anything is written
	for ( i = 0; i < numRecords; i++ ) {
		const hpRecord_t *r = &records[i];
		const char *error = NULL;

		switch ( r->op ) {
		case HP_NEED:
			if ( !HP_InImage( slot, patch + 1, r->dstImage, r->dstOff, sizeof( uintptr_t ) ) )
				error = "need outside the image";
			else if ( !*(uintptr_t *)( slot->images[r->dstImage].base + r->dstOff ) )
				error = "symbol not found";
			break;
		case HP_PTR:
			if ( !HP_InImage( slot, patch + 1, r->dstImage, r->dstOff, sizeof( uintptr_t ) )
				|| !HP_InImage( slot, patch + 1, r->srcImage, r->srcOff, 0 ) )
				error = "ptr outside the image";
			break;
		case HP_COPY:
			if ( r->arg <= 0 || !HP_InImage( slot, patch + 1, r->dstImage, r->dstOff, r->arg )
				|| !HP_InImage( slot, patch + 1, r->srcImage, r->srcOff, r->arg ) )
				error = "copy outside the image";
			break;
		case HP_JMP:
			if ( !HP_InImage( slot, patch + 1, r->dstImage, r->dstOff, sizeof( hpEndbr64 ) + HP_JMP_SIZE )
				|| !HP_InImage( slot, patch + 1, r->srcImage, r->srcOff, 1 ) )
				error = "jmp outside the image";
			else if ( !HP_JmpSite( (byte *)( slot->images[r->dstImage].base + r->dstOff ) ) )
				error = "no room for a jump (module built without BuildDevHotpatch?)";
			break;
		}
		if ( error ) {
			Com_Printf( S_COLOR_RED "hotpatch: %s patch %d: %s: %s\n", slot->vm->name, patch, r->name, error );
			Z_Free( records );
			dlclose( image->handle );
			memset( image, 0, sizeof( *image ) );
			return qfalse;
		}
	}

	// state first, then the patch's references, then the jumps that send the callers there
	if ( live ) {
		for ( i = 0; i < numRecords; i++ ) {
			const hpRecord_t *r = &records[i];

			if ( r->op == HP_COPY ) {
				memcpy( (void *)( slot->images[r->dstImage].base + r->dstOff ),
					(const void *)( slot->images[r->srcImage].base + r->srcOff ), r->arg );
				numCopies++;
			}
		}
	}
	for ( i = 0; i < numRecords; i++ ) {
		const hpRecord_t *r = &records[i];

		if ( r->op == HP_PTR )
			*(uintptr_t *)( slot->images[r->dstImage].base + r->dstOff ) = slot->images[r->srcImage].base + r->srcOff + r->arg;
	}
	for ( i = 0; i < numRecords; i++ ) {
		const hpRecord_t *r = &records[i];

		if ( r->op != HP_JMP )
			continue;
		if ( !HP_WriteJmp( HP_JmpSite( (byte *)( slot->images[r->dstImage].base + r->dstOff ) ), slot->images[r->srcImage].base + r->srcOff ) ) {
			// the earlier jumps already lead into this patch, so it stays loaded
			Com_Printf( S_COLOR_RED "hotpatch: %s patch %d: mprotect failed at %s, reload the module\n", slot->vm->name, patch, r->name );
			break;
		}
		numJmps++;
	}
	Z_Free( records );

	slot->numImages++;
	Com_Printf( "hotpatch: %s patch %d in, %d functions%s\n", slot->vm->name, patch, numJmps,
		live ? va( ", %d statics kept", numCopies ) : "" );
	return qtrue;
}

/*
=================
HP_Check

loads the patches of the list that are not in yet
=================
*/
static void HP_Check( hpSlot_t *slot, qboolean live ) {
	char path[MAX_OSPATH], base[64];
	struct stat st;
	int count;

	Com_sprintf( path, sizeof( path ), "%s/%s.patch", HP_Dir(), slot->vm->name );
	if ( stat( path, &st ) )
		return;
	if ( live && st.st_ino == slot->listStat.st_ino && st.st_size == slot->listStat.st_size
		&& st.st_mtim.tv_sec == slot->listStat.st_mtim.tv_sec && st.st_mtim.tv_nsec == slot->listStat.st_mtim.tv_nsec )
		return;
	slot->listStat = st;

	if ( !HP_ReadList( path, base, sizeof( base ), &count ) )
		return;
	if ( strcmp( base, slot->buildId ) ) {
		if ( live )
			Com_Printf( S_COLOR_YELLOW "hotpatch: the patches of %s are for another build of it (%s, running %s): install the build and reload it\n",
				slot->vm->name, base, slot->buildId );
		return;
	}
	if ( count < slot->numImages - 1 ) {
		Com_Printf( S_COLOR_YELLOW "hotpatch: the patches of %s started over, reload it (vid_restart or a new map)\n", slot->vm->name );
		return;
	}
	while ( slot->numImages <= count ) {
		if ( !HP_Apply( slot, live ) )
			break;
	}
}

/*
=================
VM_HotpatchInit
=================
*/
void VM_HotpatchInit( void ) {
	v_devHotpatch = Cvar_Get( "v_devHotpatch", "0", CVAR_ARCHIVE, "Developer build: put patches of the game, cgame and ui modules (tools/hotpatch.py) into them while they run" );
	v_devHotpatchDir = Cvar_Get( "v_devHotpatchDir", "", CVAR_ARCHIVE, "Folder of the patches, empty for <fs_basepath>/hotpatch" );
	memset( hpSlots, 0, sizeof( hpSlots ) );
}

/*
=================
VM_HotpatchLoaded

a module was loaded: remember where, and put in the patches made for its build
=================
*/
void VM_HotpatchLoaded( vm_t *vm ) {
	hpSlot_t *slot = &hpSlots[vm->slot];

	memset( slot, 0, sizeof( *slot ) );
	if ( !HP_ImageInfo( vm->dllHandle, &slot->images[0], slot->buildId ) || !slot->buildId[0] ) {
		Com_Printf( S_COLOR_YELLOW "hotpatch: no build id for %s, it can't be patched\n", vm->name );
		return;
	}
	slot->vm = vm;
	slot->numImages = 1;
	if ( v_devHotpatch && v_devHotpatch->integer )
		HP_Check( slot, qfalse );
}

/*
=================
VM_HotpatchFreed

before the module is unloaded
=================
*/
void VM_HotpatchFreed( vm_t *vm ) {
	hpSlot_t *slot = &hpSlots[vm->slot];

	if ( slot->vm != vm )
		return;
	for ( int i = slot->numImages - 1; i > 0; i-- ) {
		if ( slot->images[i].handle )
			dlclose( slot->images[i].handle );
	}
	memset( slot, 0, sizeof( *slot ) );
}

/*
=================
VM_HotpatchFrame

at the start of a frame, no module code is running
=================
*/
void VM_HotpatchFrame( void ) {
	int now;

	if ( !v_devHotpatch || !v_devHotpatch->integer )
		return;
	now = Sys_Milliseconds();
	if ( now - hpNextCheck < 0 )
		return;
	hpNextCheck = now + HP_CHECK_MSEC;

	for ( int i = 0; i < MAX_VM; i++ ) {
		if ( hpSlots[i].vm )
			HP_Check( &hpSlots[i], qtrue );
	}
}

#endif // VISIONJK_HOTPATCH
