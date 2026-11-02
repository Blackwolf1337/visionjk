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

// cl_clipboard.cpp -- a picture from the clipboard of the system, for the drawing board of issue #9 (cgame/toys/
// cg_board.c). SDL 2 has the text of the clipboard only, so each system is asked itself. Not from OpenJK.
//
// v_clipboardImage <path> <cgame command>: the picture on the clipboard goes into <path>.png (or .jpg, or .tga) of the
// home folder, and the cgame command is run with the name of that file, or without one when the clipboard holds no
// picture. Nothing waits: on Windows the clipboard is read at once (the registered format PNG that browsers and
// paint programs put there, else CF_DIBV5 or CF_DIB, written as a TGA); under X11, also XWayland, the owner of the
// selection CLIPBOARD is asked on a connection of this file's own (first TARGETS, then image/png or image/jpeg, a
// large picture comes in parts by INCR, ICCCM 2.7.2), and the answers are looked for once a frame (CL_ClipboardFrame);
// on macOS the general pasteboard is read at once (public.png) through the Objective-C runtime, looked up by name.

#include "client.h"
#include "cl_cgameapi.h"

#if defined( _WIN32 )
#include <windows.h>
#elif defined( __APPLE__ )
#include <dlfcn.h>
#elif !defined( __EMSCRIPTEN__ )
#define CLIP_X11
#include <dlfcn.h>
#endif

#define CLIP_MOST			( 32 * 1024 * 1024 )	// bytes of a picture at most
#define CLIP_WAIT_MSEC		3000					// the owner of the selection has this long to answer

static struct {
	char		path[MAX_QPATH];		// without the extension
	char		command[64];			// of the cgame
	byte		*data;					// what came, Z_Malloc
	int			len, size;
} clip;

static void CLIP_Free( void ) {
	if ( clip.data ) {
		Z_Free( clip.data );
	}
	clip.data = NULL;
	clip.len = clip.size = 0;
}

static qboolean CLIP_Append( const void *bytes, int len ) {
	if ( len <= 0 ) {
		return qtrue;
	}
	if ( clip.len + len > CLIP_MOST ) {
		return qfalse;
	}
	if ( clip.len + len > clip.size ) {
		const int	size = Q_max( clip.len + len, clip.size * 2 + 64 * 1024 );
		byte		*more = (byte *)Z_Malloc( size, TAG_TEMP_WORKSPACE, qfalse );

		if ( clip.data ) {
			memcpy( more, clip.data, clip.len );
			Z_Free( clip.data );
		}
		clip.data = more;
		clip.size = size;
	}
	memcpy( clip.data + clip.len, bytes, len );
	clip.len += len;
	return qtrue;
}

// the cgame is told: the file the picture went to, or "" when there is none
static void CLIP_Done( const char *file ) {
	CLIP_Free();
	if ( clip.command[0] && cls.cgameStarted ) {
		Cmd_TokenizeString( va( "%s \"%s\"", clip.command, file ) );
		CGVM_ConsoleCommand();
	}
	clip.command[0] = '\0';
}

// what came, as a file of the home folder: PNG and JPEG as they are
static void CLIP_Write( const char *extension ) {
	const char		*file = va( "%s.%s", clip.path, extension );
	fileHandle_t	f = FS_FOpenFileWrite( file );

	if ( !f ) {
		CLIP_Done( "" );
		return;
	}
	FS_Write( clip.data, clip.len, f );
	FS_FCloseFile( f );
	CLIP_Done( file );
}

#if defined( _WIN32 )

// A CF_DIB or CF_DIBV5 as a TGA of 32 bits: rows of 24 or 32 bits, from the bottom or (height below 0) from the top.
// Alpha only when the header of version 5 has a mask for it and some pixel uses it.
static qboolean CLIP_FromDib( const byte *dib, SIZE_T len ) {
	const BITMAPINFOHEADER	*h = (const BITMAPINFOHEADER *)dib;
	const int				width = h->biWidth, height = abs( h->biHeight ), bits = h->biBitCount;
	const qboolean			topDown = (qboolean)( h->biHeight < 0 );
	DWORD					masks[4] = { 0x00ff0000, 0x0000ff00, 0x000000ff, 0 };
	SIZE_T					offset = h->biSize;
	byte					head[18];
	int						x, y, stride;
	qboolean				hasAlpha = qfalse;

	if ( len < sizeof( BITMAPINFOHEADER ) || width <= 0 || height <= 0 || width > 8192 || height > 8192 || ( bits != 24 && bits != 32 )
		|| ( h->biCompression != BI_RGB && h->biCompression != BI_BITFIELDS ) ) {
		return qfalse;
	}
	if ( h->biCompression == BI_BITFIELDS ) {
		if ( h->biSize >= sizeof( BITMAPV5HEADER ) ) {
			const BITMAPV5HEADER *v5 = (const BITMAPV5HEADER *)dib;

			masks[0] = v5->bV5RedMask;
			masks[1] = v5->bV5GreenMask;
			masks[2] = v5->bV5BlueMask;
			masks[3] = v5->bV5AlphaMask;
		} else {
			// (the three masks follow a plain header)
			memcpy( masks, dib + h->biSize, sizeof( DWORD ) * 3 );
			offset += sizeof( DWORD ) * 3;
		}
	} else if ( bits == 32 && h->biSize >= sizeof( BITMAPV5HEADER ) ) {
		masks[3] = ( (const BITMAPV5HEADER *)dib )->bV5AlphaMask;
	}
	stride = ( ( width * bits + 31 ) / 32 ) * 4;
	if ( offset + (SIZE_T)stride * height > len ) {
		return qfalse;
	}
	memset( head, 0, sizeof( head ) );
	head[2] = 2;						// true colour, not packed
	head[12] = (byte)width;
	head[13] = (byte)( width >> 8 );
	head[14] = (byte)height;
	head[15] = (byte)( height >> 8 );
	head[16] = 32;
	head[17] = 0x28;					// eight bits of alpha, the first row at the top
	CLIP_Free();
	if ( !CLIP_Append( head, sizeof( head ) ) ) {
		return qfalse;
	}
	for ( y = 0; y < height; y++ ) {
		const byte *row = dib + offset + (SIZE_T)stride * ( topDown ? y : height - 1 - y );

		for ( x = 0; x < width; x++ ) {
			DWORD	value = 0;
			byte	bgra[4];
			int		c;

			memcpy( &value, row + x * ( bits / 8 ), bits / 8 );
			for ( c = 0; c < 4; c++ ) {
				const DWORD	mask = masks[c == 0 ? 2 : c == 2 ? 0 : c];
				DWORD		v = value & mask;
				int			shift = 0;

				if ( !mask ) {
					bgra[c] = 255;
					continue;
				}
				while ( !( ( mask >> shift ) & 1 ) ) {
					shift++;
				}
				v >>= shift;
				bgra[c] = (byte)( v * 255 / ( mask >> shift ) );
			}
			if ( masks[3] && bgra[3] ) {
				hasAlpha = qtrue;
			}
			if ( !CLIP_Append( bgra, 4 ) ) {
				return qfalse;
			}
		}
	}
	// an alpha mask that no pixel uses: the picture is opaque
	if ( !hasAlpha ) {
		for ( x = 18 + 3; x < clip.len; x += 4 ) {
			clip.data[x] = 255;
		}
	}
	return qtrue;
}

static void CLIP_Start( void ) {
	const UINT	png = RegisterClipboardFormatA( "PNG" );
	HANDLE		handle;
	const byte	*bytes;

	if ( !OpenClipboard( NULL ) ) {
		CLIP_Done( "" );
		return;
	}
	if ( ( handle = GetClipboardData( png ) ) != NULL && ( bytes = (const byte *)GlobalLock( handle ) ) != NULL ) {
		const SIZE_T len = GlobalSize( handle );
		const qboolean ok = (qboolean)( len > 8 && len < CLIP_MOST && CLIP_Append( bytes, (int)len ) );

		GlobalUnlock( handle );
		CloseClipboard();
		if ( ok ) {
			CLIP_Write( "png" );
		} else {
			CLIP_Done( "" );
		}
		return;
	}
	if ( ( ( handle = GetClipboardData( CF_DIBV5 ) ) != NULL || ( handle = GetClipboardData( CF_DIB ) ) != NULL )
		&& ( bytes = (const byte *)GlobalLock( handle ) ) != NULL ) {
		const qboolean ok = CLIP_FromDib( bytes, GlobalSize( handle ) );

		GlobalUnlock( handle );
		CloseClipboard();
		if ( ok ) {
			CLIP_Write( "tga" );
		} else {
			CLIP_Done( "" );
		}
		return;
	}
	CloseClipboard();
	CLIP_Done( "" );
}

void CL_ClipboardFrame( void ) {
}

#elif defined( __APPLE__ )

// [[NSPasteboard generalPasteboard] dataForType:@"public.png"], the calls looked up by name (Cocoa is in the process,
// SDL loads it); each call cast to its own type, as arm64 needs
typedef void *( *clipMsgFn_t )( void *, void * );
typedef void *( *clipMsgArgFn_t )( void *, void *, const void * );
typedef unsigned long ( *clipMsgSizeFn_t )( void *, void * );

static void CLIP_Start( void ) {
	void		*( *getClass )( const char * ) = ( void *( * )( const char * ) )dlsym( RTLD_DEFAULT, "objc_getClass" );
	void		*( *selector )( const char * ) = ( void *( * )( const char * ) )dlsym( RTLD_DEFAULT, "sel_registerName" );
	void		*send = dlsym( RTLD_DEFAULT, "objc_msgSend" );
	void		*board, *type, *data;
	unsigned long	len;
	const void	*bytes;

	if ( !getClass || !selector || !send || !getClass( "NSPasteboard" ) || !getClass( "NSString" ) ) {
		CLIP_Done( "" );
		return;
	}
	board = ( (clipMsgFn_t)send )( getClass( "NSPasteboard" ), selector( "generalPasteboard" ) );
	type = ( (clipMsgArgFn_t)send )( getClass( "NSString" ), selector( "stringWithUTF8String:" ), "public.png" );
	data = board && type ? ( (clipMsgArgFn_t)send )( board, selector( "dataForType:" ), type ) : NULL;
	if ( !data ) {
		CLIP_Done( "" );
		return;
	}
	len = ( (clipMsgSizeFn_t)send )( data, selector( "length" ) );
	bytes = ( (clipMsgFn_t)send )( data, selector( "bytes" ) );
	if ( !bytes || len <= 8 || len >= CLIP_MOST || !CLIP_Append( bytes, (int)len ) ) {
		CLIP_Done( "" );
		return;
	}
	CLIP_Write( "png" );
}

void CL_ClipboardFrame( void ) {
}

#elif defined( CLIP_X11 )

// Xlib, looked up at run time: the game does not need X11 to start (SDL may run on Wayland alone, then DISPLAY of
// XWayland is used, or there is nothing to ask). The types are the ones of Xlib.h, as far as this file needs them.
typedef unsigned long	clipXID_t;
typedef unsigned long	clipAtom_t;
typedef struct {
	int				type;
	unsigned long	serial;
	int				sendEvent;
	void			*display;
	clipXID_t		requestor;
	clipAtom_t		selection, target, property;
	unsigned long	time;
} clipSelectionEvent_t;
typedef struct {
	int				type;
	unsigned long	serial;
	int				sendEvent;
	void			*display;
	clipXID_t		window;
	clipAtom_t		atom;
	unsigned long	time;
	int				state;
} clipPropertyEvent_t;
typedef union {
	int						type;
	clipSelectionEvent_t	selection;
	clipPropertyEvent_t		property;
	long					pad[24];
} clipEvent_t;

#define CLIP_SELECTION_NOTIFY	31
#define CLIP_PROPERTY_NOTIFY	28
#define CLIP_PROPERTY_NEW		0
#define CLIP_PROPERTY_CHANGE	( 1L << 22 )
#define CLIP_ANY_TYPE			0L

static struct {
	void		*lib;
	void		*( *OpenDisplay )( const char * );
	int			( *CloseDisplay )( void * );
	clipXID_t	( *DefaultRootWindow )( void * );
	clipXID_t	( *CreateSimpleWindow )( void *, clipXID_t, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long );
	int			( *SelectInput )( void *, clipXID_t, long );
	clipAtom_t	( *InternAtom )( void *, const char *, int );
	int			( *ConvertSelection )( void *, clipAtom_t, clipAtom_t, clipAtom_t, clipXID_t, unsigned long );
	int			( *Pending )( void * );
	int			( *CheckTypedWindowEvent )( void *, clipXID_t, int, clipEvent_t * );
	int			( *GetWindowProperty )( void *, clipXID_t, clipAtom_t, long, long, int, clipAtom_t, clipAtom_t *, int *, unsigned long *, unsigned long *, unsigned char ** );
	int			( *DeleteProperty )( void *, clipXID_t, clipAtom_t );
	int			( *Free )( void * );
	int			( *Flush )( void * );
	int			( *DestroyWindow )( void *, clipXID_t );
	qboolean	failed;
} x11;

typedef enum { CLIPX_IDLE, CLIPX_TARGETS, CLIPX_PICTURE, CLIPX_PARTS } clipStage_t;

static struct {
	clipStage_t	stage;
	void		*display;
	clipXID_t	window;
	clipAtom_t	clipboard, targets, property, incr, png, jpeg;
	const char	*extension;
	int			startTime;
} x;

static qboolean CLIP_LoadX11( void ) {
	if ( x11.lib ) {
		return qtrue;
	}
	if ( x11.failed || !( x11.lib = dlopen( "libX11.so.6", RTLD_NOW | RTLD_LOCAL ) ) ) {
		x11.failed = qtrue;
		return qfalse;
	}
#define CLIP_SYM( name ) ( *(void **)&x11.name = dlsym( x11.lib, "X" #name ) )
	if ( !CLIP_SYM( OpenDisplay ) || !CLIP_SYM( CloseDisplay ) || !CLIP_SYM( DefaultRootWindow ) || !CLIP_SYM( CreateSimpleWindow )
		|| !CLIP_SYM( SelectInput ) || !CLIP_SYM( InternAtom ) || !CLIP_SYM( ConvertSelection ) || !CLIP_SYM( Pending )
		|| !CLIP_SYM( CheckTypedWindowEvent ) || !CLIP_SYM( GetWindowProperty ) || !CLIP_SYM( DeleteProperty ) || !CLIP_SYM( Free )
		|| !CLIP_SYM( Flush ) || !CLIP_SYM( DestroyWindow ) ) {
		dlclose( x11.lib );
		x11.lib = NULL;
		x11.failed = qtrue;
		return qfalse;
	}
#undef CLIP_SYM
	return qtrue;
}

static void CLIP_EndX11( const char *extension ) {
	if ( x.display ) {
		x11.DestroyWindow( x.display, x.window );
		x11.CloseDisplay( x.display );
	}
	x.display = NULL;
	x.stage = CLIPX_IDLE;
	if ( extension ) {
		CLIP_Write( extension );
	} else {
		CLIP_Done( "" );
	}
}

// The property the owner put the answer into, deleted after it is read. type 0 and no value: there is no such
// property (yet). qfalse: it could not be read.
static qboolean CLIP_ReadProperty( clipAtom_t *type, int *format, unsigned long *items, unsigned char **value ) {
	unsigned long after = 0;

	*value = NULL;
	*type = 0;
	*items = 0;
	return (qboolean)( x11.GetWindowProperty( x.display, x.window, x.property, 0, CLIP_MOST / 4, 1, CLIP_ANY_TYPE, type, format, items,
		&after, value ) == 0 );
}

static void CLIP_Start( void ) {
	if ( x.stage != CLIPX_IDLE || !CLIP_LoadX11() || !( x.display = x11.OpenDisplay( NULL ) ) ) {
		CLIP_Done( "" );
		return;
	}
	x.window = x11.CreateSimpleWindow( x.display, x11.DefaultRootWindow( x.display ), 0, 0, 1, 1, 0, 0, 0 );
	x11.SelectInput( x.display, x.window, CLIP_PROPERTY_CHANGE );
	x.clipboard = x11.InternAtom( x.display, "CLIPBOARD", 0 );
	x.targets = x11.InternAtom( x.display, "TARGETS", 0 );
	x.property = x11.InternAtom( x.display, "VJK_CLIPBOARD", 0 );
	x.incr = x11.InternAtom( x.display, "INCR", 0 );
	x.png = x11.InternAtom( x.display, "image/png", 0 );
	x.jpeg = x11.InternAtom( x.display, "image/jpeg", 0 );
	x11.ConvertSelection( x.display, x.clipboard, x.targets, x.property, x.window, 0 );
	x11.Flush( x.display );
	x.stage = CLIPX_TARGETS;
	x.startTime = cls.realtime;
}

/*
=================
CL_ClipboardFrame

Once a frame while the clipboard of X11 is asked: the answers that came are taken, nothing waits for one. After
CLIP_WAIT_MSEC without an answer the clipboard holds no picture.
=================
*/
void CL_ClipboardFrame( void ) {
	clipEvent_t		ev;
	clipAtom_t		type;
	int				format;
	unsigned long	items;
	unsigned char	*value;

	if ( x.stage == CLIPX_IDLE ) {
		return;
	}
	if ( cls.realtime - x.startTime > CLIP_WAIT_MSEC ) {
		CLIP_EndX11( NULL );
		return;
	}
	if ( !x11.Pending( x.display ) ) {
		return;
	}
	if ( x.stage == CLIPX_PARTS ) {
		// a part of a large picture: each new value of the property is the next part, an empty one the end. (A
		// notice from before, of a value that was read already, finds no property: it is let go.)
		while ( x11.CheckTypedWindowEvent( x.display, x.window, CLIP_PROPERTY_NOTIFY, &ev ) ) {
			if ( ev.property.atom != x.property || ev.property.state != CLIP_PROPERTY_NEW ) {
				continue;
			}
			if ( !CLIP_ReadProperty( &type, &format, &items, &value ) ) {
				CLIP_EndX11( NULL );
				return;
			}
			if ( !type ) {
				continue;
			}
			if ( !items ) {
				if ( value ) {
					x11.Free( value );
				}
				CLIP_EndX11( clip.len ? x.extension : NULL );
				return;
			}
			if ( !CLIP_Append( value, (int)( items * ( format / 8 ) ) ) ) {
				x11.Free( value );
				CLIP_EndX11( NULL );
				return;
			}
			x11.Free( value );
			x.startTime = cls.realtime;
		}
		return;
	}
	if ( !x11.CheckTypedWindowEvent( x.display, x.window, CLIP_SELECTION_NOTIFY, &ev ) ) {
		return;
	}
	if ( !ev.selection.property || !CLIP_ReadProperty( &type, &format, &items, &value ) || !value ) {
		CLIP_EndX11( NULL );
		return;
	}
	if ( x.stage == CLIPX_TARGETS ) {
		// what the owner can give: a PNG first, else a JPEG
		const clipAtom_t	*list = (const clipAtom_t *)value;
		clipAtom_t			want = 0;

		for ( unsigned long i = 0; format == 32 && i < items; i++ ) {
			if ( list[i] == x.png || ( list[i] == x.jpeg && !want ) ) {
				want = list[i];
			}
		}
		x11.Free( value );
		if ( !want ) {
			CLIP_EndX11( NULL );
			return;
		}
		x.extension = want == x.png ? "png" : "jpg";
		x11.ConvertSelection( x.display, x.clipboard, want, x.property, x.window, 0 );
		x11.Flush( x.display );
		x.stage = CLIPX_PICTURE;
		x.startTime = cls.realtime;
		return;
	}
	if ( type == x.incr ) {
		// a large picture comes in parts: the property was deleted when it was read, which asks for the first
		x11.Free( value );
		x11.Flush( x.display );
		x.stage = CLIPX_PARTS;
		x.startTime = cls.realtime;
		return;
	}
	if ( !CLIP_Append( value, (int)( items * ( format / 8 ) ) ) ) {
		x11.Free( value );
		CLIP_EndX11( NULL );
		return;
	}
	x11.Free( value );
	CLIP_EndX11( x.extension );
}

#else

static void CLIP_Start( void ) {
	CLIP_Done( "" );
}

void CL_ClipboardFrame( void ) {
}

#endif

// v_clipboardImage <path> <cgame command>
static void CL_ClipboardImage_f( void ) {
	const char *path = Cmd_Argv( 1 );

	if ( Cmd_Argc() != 3 || !path[0] || strstr( path, ".." ) || strchr( path, ':' ) || strchr( path, '\\' ) || path[0] == '/' ) {
		Com_Printf( "usage: v_clipboardImage <path in the home folder, without extension> <cgame command>\n" );
		return;
	}
	if ( clip.command[0] ) {
		return;		// (one at a time)
	}
	Q_strncpyz( clip.path, path, sizeof( clip.path ) );
	Q_strncpyz( clip.command, Cmd_Argv( 2 ), sizeof( clip.command ) );
	CLIP_Free();
	CLIP_Start();
}

void CL_ClipboardInit( void ) {
	Cmd_AddCommand( "v_clipboardImage", CL_ClipboardImage_f, "Writes the picture on the clipboard to a file in the home folder for the cgame: v_clipboardImage <path> <cgame command>" );
}
