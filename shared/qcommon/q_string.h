#pragma once

#include "q_platform.h"

#if defined(__cplusplus)
extern "C" {
#endif

int Q_isprint( int c );
int Q_isprintext( int c );
int Q_isgraph( int c );
int Q_islower( int c );
int Q_isupper( int c );
int Q_isalpha( int c );
qboolean Q_isanumber( const char *s );
qboolean Q_isintegral( float f );

// portable case insensitive compare
int Q_stricmp(const char *s1, const char *s2);
int	Q_strncmp(const char *s1, const char *s2, int n);
int	Q_stricmpn(const char *s1, const char *s2, int n);
char *Q_strlwr( char *s1 );
char *Q_strupr( char *s1 );
char *Q_strrchr( const char* string, int c );

// buffer size safe library replacements
void Q_strncpyz( char *dest, const char *src, int destsize );
void Q_strcat( char *dest, int size, const char *src );

const char *Q_stristr( const char *s, const char *find);

// strlen that discounts Quake color sequences
int Q_PrintStrlen( const char *string );
int Q_PrintStrLenTo(const char *str, int chars, char *color);

// removes color sequences from string
char *Q_CleanStr( char *string );
void Q_StripColor(char *text);
const char *Q_strchrs( const char *string, const char *search );

void Q_strstrip( char *string, const char *strip, const char *repl );

// UTF-8. Text inside the client is UTF-8, text of legacy clients and of the game's files is Windows-1252 (one
// byte a character). Q_UTF8_Decode() reads both: a byte that starts no valid UTF-8 sequence is taken as
// Windows-1252.
unsigned int Q_CP1252ToUnicode( int c );
int Q_UnicodeToCP1252( unsigned int codePoint );
int Q_UTF8_Decode( const char *s, unsigned int *codePoint );
int Q_UTF8_Encode( unsigned int codePoint, char *out );
int Q_UTF8_PrevChar( const char *string, int offset );
void Q_UTF8_Truncate( char *string, int maxBytes );
void Q_UTF8_FromCP1252( const char *in, char *out, int outSize );
void Q_UTF8_ToCP1252( const char *in, char *out, int outSize );

#if defined (_MSC_VER)
	// vsnprintf is ISO/IEC 9899:1999
	// abstracting this to make it portable
	int Q_vsnprintf( char *str, size_t size, const char *format, va_list args );
#else // not using MSVC
	#define Q_vsnprintf vsnprintf
#endif

#if defined(__cplusplus)
} // extern "C"
#endif
