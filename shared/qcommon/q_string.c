#include "q_string.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q_color.h"

int Q_isprint( int c )
{
	if ( c >= 0x20 && c <= 0x7E )
		return ( 1 );
	return ( 0 );
}

// a character that may stand in a log or on a console as it is: printable ASCII without the quote, the percent
// sign and the caret of the colour codes (VisionJK)
int Q_isLogChar( int c )
{
	return ( c >= ' ' && c < 127 && c != '"' && c != '%' && c != '^' );
}

// in copied to out for a terminal: control characters but the line break and the tab, DEL and the C1 controls in
// UTF-8 (C2 80..C2 9F, the CSI among them) become a dot, so text from others can not move the cursor, clear the
// screen or talk to the terminal. Stops before out is full, never inside such a pair. Returns the bytes of in
// that were taken (VisionJK)
int Q_TermSafe( char *out, int outSize, const char *in )
{
	const unsigned char	*s = (const unsigned char *)in;
	int					n = 0;

	if ( outSize < 1 )
		return 0;
	while ( *s && n < outSize - 1 )
	{
		if ( s[0] == 0xC2 && s[1] >= 0x80 && s[1] <= 0x9F )
		{
			out[n++] = '.';
			s += 2;
		}
		else
		{
			out[n++] = ( ( *s < 0x20 && *s != '\n' && *s != '\t' ) || *s == 0x7F ) ? '.' : (char)*s;
			s++;
		}
	}
	out[n] = '\0';
	return (int)( (const char *)s - in );
}

int Q_isprintext( int c )
{
	if ( c >= 0x20 && c <= 0x7E )
		return (1);
	if ( c >= 0x80 && c <= 0xFE )
		return (1);
	return (0);
}

int Q_isgraph( int c )
{
	if ( c >= 0x21 && c <= 0x7E )
		return (1);
	if ( c >= 0x80 && c <= 0xFE )
		return (1);
	return (0);
}

int Q_islower( int c )
{
	if (c >= 'a' && c <= 'z')
		return ( 1 );
	return ( 0 );
}

int Q_isupper( int c )
{
	if (c >= 'A' && c <= 'Z')
		return ( 1 );
	return ( 0 );
}

int Q_isalpha( int c )
{
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
		return ( 1 );
	return ( 0 );
}

qboolean Q_isanumber( const char *s )
{
	char *p;
	double ret;

	if( *s == '\0' )
		return qfalse;

	ret = strtod( s, &p );

	if ( ret == HUGE_VAL || errno == ERANGE )
		return qfalse;

	return (qboolean)(*p == '\0');
}

qboolean Q_isintegral( float f )
{
	return (qboolean)( (int)f == f );
}

char* Q_strrchr( const char* string, int c )
{
	char cc = c;
	char *s;
	char *sp=(char *)0;

	s = (char*)string;

	while (*s)
	{
		if (*s == cc)
			sp = s;
		s++;
	}
	if (cc == 0)
		sp = s;

	return sp;
}

/*
=============
Q_strncpyz

Safe strncpy that ensures a trailing zero
=============
*/
void Q_strncpyz( char *dest, const char *src, int destsize ) {
	assert(src);
	assert(dest);
	assert(destsize);

	strncpy( dest, src, destsize-1 );
	dest[destsize-1] = 0;
}

int Q_stricmpn (const char *s1, const char *s2, int n) {
	int		c1, c2;

	if ( s1 == NULL ) {
		if ( s2 == NULL )
			return 0;
		else
			return -1;
	}
	else if ( s2==NULL )
		return 1;

	do {
		c1 = *s1++;
		c2 = *s2++;

		if (!n--) {
			return 0;		// strings are equal until end point
		}

		if (c1 != c2) {
			if (c1 >= 'a' && c1 <= 'z') {
				c1 -= ('a' - 'A');
			}
			if (c2 >= 'a' && c2 <= 'z') {
				c2 -= ('a' - 'A');
			}
			if (c1 != c2) {
				return c1 < c2 ? -1 : 1;
			}
		}
	} while (c1);

	return 0;		// strings are equal
}

int Q_stricmp (const char *s1, const char *s2) {
	return (s1 && s2) ? Q_stricmpn (s1, s2, 99999) : -1;
}

int Q_strncmp (const char *s1, const char *s2, int n) {
	int		c1, c2;

	do {
		c1 = *s1++;
		c2 = *s2++;

		if (!n--) {
			return 0;		// strings are equal until end point
		}

		if (c1 != c2) {
			return c1 < c2 ? -1 : 1;
		}
	} while (c1);

	return 0;		// strings are equal
}

char *Q_strlwr( char *s1 ) {
	char	*s;

	s = s1;
	while ( *s ) {
		*s = tolower(*s);
		s++;
	}
	return s1;
}

char *Q_strupr( char *s1 ) {
	char	*s;

	s = s1;
	while ( *s ) {
		*s = toupper(*s);
		s++;
	}
	return s1;
}

// never goes past bounds or leaves without a terminating 0
void Q_strcat( char *dest, int size, const char *src ) {
	int		l1;

	l1 = strlen( dest );
	if ( l1 >= size ) {
		//Com_Error( ERR_FATAL, "Q_strcat: already overflowed" );
		return;
	}
	if ( strlen(src)+1 > (size_t)(size - l1))
	{	//do the error here instead of in Q_strncpyz to get a meaningful msg
		//Com_Error(ERR_FATAL,"Q_strcat: cannot append \"%s\" to \"%s\"", src, dest);
		return;
	}
	Q_strncpyz( dest + l1, src, size - l1 );
}

/*
=============
Q_StringEdExpand

The text of a server with every "@@@REF" in it replaced by lookup( "REF" ), into out of outSize bytes. A
reference ends at a blank, ':', '.' or a line break, which stays. A replacement that does not fit is left out,
as Q_strcat leaves it, and the rest of the text is cut at the end of out. The three copies of this that the
game had (CG_CheckSVStringEdRef and two in the engine) took the cut for the replacement only and went on
writing the text after it past the buffer.
=============
*/
void Q_StringEdExpand( char *out, int outSize, const char *in, const char *( *lookup )( const char *ref ) ) {
	int i = 0, b = 0;

	if ( outSize <= 0 ) {
		return;
	}
	out[0] = '\0';
	while ( in[i] && b < outSize - 1 ) {
		if ( in[i] == '@' && in[i + 1] == '@' && in[i + 2] == '@' && in[i + 3] ) {
			char	ref[1024];
			int		r = 0;

			while ( in[i] == '@' ) {
				i++;
			}
			while ( in[i] && in[i] != ' ' && in[i] != ':' && in[i] != '.' && in[i] != '\n' ) {
				if ( r < (int)sizeof( ref ) - 1 ) {
					ref[r++] = in[i];
				}
				i++;
			}
			ref[r] = '\0';
			out[b] = '\0';
			Q_strcat( out, outSize, lookup( ref ) );
			b = (int)strlen( out );
			if ( !in[i] || b >= outSize - 1 ) {
				break;
			}
		}
		out[b++] = in[i++];
	}
	out[b] = '\0';
}

/*
* Find the first occurrence of find in s.
*/
const char *Q_stristr( const char *s, const char *find )
{
	char c, sc;
	size_t len;

	if ((c = *find++) != 0)
	{
		if (c >= 'a' && c <= 'z')
		{
			c -= ('a' - 'A');
		}
		len = strlen(find);
		do
		{
			do
			{
				if ((sc = *s++) == 0)
					return NULL;
				if (sc >= 'a' && sc <= 'z')
				{
					sc -= ('a' - 'A');
				}
			} while (sc != c);
		} while (Q_stricmpn(s, find, len) != 0);
		s--;
	}
	return s;
}

int Q_PrintStrlen( const char *string ) {
	int			len;
	const char	*p;

	if( !string ) {
		return 0;
	}

	len = 0;
	p = string;
	while( *p ) {
		if( Q_IsColorString( p ) ) {
			p += 2;
			continue;
		}
		p++;
		len++;
	}

	return len;
}

int Q_PrintStrLenTo(const char *str, int chars, char *color) {
	int		offset = 0;
	char	lastColor = 0;
	int		i;

	for (i = 0; i < chars && str[i]; i++) {
		if (Q_IsColorString(&str[i])) {
			i++;
			lastColor = str[i];
		} else {
			offset++;
		}
	}

	if (color) {
		*color = lastColor;
	}

	return offset;
}


char *Q_CleanStr( char *string ) {
	char*	d;
	char*	s;
	int		c;

	s = string;
	d = string;
	while ((c = *s) != 0 ) {
		if ( Q_IsColorString( s ) ) {
			s++;
		}
		else if ( c >= 0x20 && c <= 0x7E ) {
			*d++ = c;
		}
		s++;
	}
	*d = '\0';

	return string;
}

/*
==================
Q_StripColor

Strips coloured strings in-place using multiple passes: "fgs^^56fds" -> "fgs^6fds" -> "fgsfds"

This function modifies INPUT (is mutable)

(Also strips ^8 and ^9)
==================
*/
void Q_StripColor(char *text)
{
	qboolean doPass = qtrue;
	char *read;
	char *write;

	while ( doPass )
	{
		doPass = qfalse;
		read = write = text;
		while ( *read )
		{
			if ( Q_IsColorStringExt(read) )
			{
				doPass = qtrue;
				read += 2;
			}
			else
			{
				// Avoid writing the same data over itself
				if (write != read)
				{
					*write = *read;
				}
				write++;
				read++;
			}
		}
		if ( write < read )
		{
			// Add trailing NUL byte if string has shortened
			*write = '\0';
		}
	}
}

/*
Q_strstrip

Description:	Replace strip[x] in string with repl[x] or remove characters entirely
Mutates:		string
Return:			--

Examples:		Q_strstrip( "Bo\nb is h\rairy!!", "\n\r!", "123" );	// "Bo1b is h2airy33"
Q_strstrip( "Bo\nb is h\rairy!!", "\n\r!", "12" );	// "Bo1b is h2airy"
Q_strstrip( "Bo\nb is h\rairy!!", "\n\r!", NULL );	// "Bob is hairy"
*/

void Q_strstrip( char *string, const char *strip, const char *repl )
{
	char		*out=string, *p=string, c;
	const char	*s=strip;
	int			replaceLen = repl?strlen( repl ):0, offset=0;
	qboolean	recordChar = qtrue;

	while ( (c = *p++) != '\0' )
	{
		recordChar = qtrue;
		for ( s=strip; *s; s++ )
		{
			offset = s-strip;
			if ( c == *s )
			{
				if ( !repl || offset >= replaceLen )
					recordChar = qfalse;
				else
					c = repl[offset];
				break;
			}
		}
		if ( recordChar )
			*out++ = c;
	}
	*out = '\0';
}

/*
Q_strchrs

Description:	Find any characters in a string. Think of it as a shorthand strchr loop.
Mutates:		--
Return:			first instance of any characters found
otherwise NULL
*/

const char *Q_strchrs( const char *string, const char *search )
{
	const char *p = string, *s = search;

	while ( *p != '\0' )
	{
		for ( s=search; *s; s++ )
		{
			if ( *p == *s )
				return p;
		}
		p++;
	}

	return NULL;
}

/*
===============================================================================

UTF-8

Text inside the client is UTF-8. Text of legacy clients and of the files of the game is Windows-1252, where a
byte is a character. Q_UTF8_Decode() reads both, so a name or a chat line of a legacy client shows right without
being converted first: a byte that starts no valid UTF-8 sequence is taken as Windows-1252.

===============================================================================
*/

// 0x80 to 0x9F of Windows-1252. The rest of it is Latin-1: the code point is the byte.
static const unsigned short cp1252High[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

unsigned int Q_CP1252ToUnicode( int c ) {
	c &= 255;
	if ( c >= 0x80 && c <= 0x9F ) {
		return cp1252High[c - 0x80];
	}
	return (unsigned int)c;
}

// the byte of a code point in Windows-1252, -1 when it has none
int Q_UnicodeToCP1252( unsigned int codePoint ) {
	int i;

	if ( codePoint < 0x80 || ( codePoint >= 0xA0 && codePoint <= 0xFF ) ) {
		return (int)codePoint;
	}
	for ( i = 0; i < 32; i++ ) {
		if ( cp1252High[i] == codePoint ) {
			return 0x80 + i;
		}
	}
	return -1;
}

/*
=============
Q_UTF8_Decode

The character at s. Returns the bytes it takes (1 to 4), 0 at the end of the string. A sequence that is too long
for its value, a surrogate, a value above U+10FFFF and a byte that fits nowhere are no UTF-8: the byte is one
character of Windows-1252 then.
=============
*/
int Q_UTF8_Decode( const char *s, unsigned int *codePoint ) {
	const unsigned char	*b = (const unsigned char *)s;
	unsigned int		c = b[0];

	if ( !c ) {
		*codePoint = 0;
		return 0;
	}
	if ( c < 0x80 ) {
		*codePoint = c;
		return 1;
	}
	if ( c >= 0xC2 && c <= 0xDF ) {
		if ( ( b[1] & 0xC0 ) == 0x80 ) {
			*codePoint = ( ( c & 0x1F ) << 6 ) | ( b[1] & 0x3F );
			return 2;
		}
	} else if ( c >= 0xE0 && c <= 0xEF ) {
		const unsigned int low = ( c == 0xE0 ) ? 0xA0 : 0x80;
		const unsigned int high = ( c == 0xED ) ? 0x9F : 0xBF;

		if ( b[1] >= low && b[1] <= high && ( b[2] & 0xC0 ) == 0x80 ) {
			*codePoint = ( ( c & 0x0F ) << 12 ) | ( ( b[1] & 0x3F ) << 6 ) | ( b[2] & 0x3F );
			return 3;
		}
	} else if ( c >= 0xF0 && c <= 0xF4 ) {
		const unsigned int low = ( c == 0xF0 ) ? 0x90 : 0x80;
		const unsigned int high = ( c == 0xF4 ) ? 0x8F : 0xBF;

		if ( b[1] >= low && b[1] <= high && ( b[2] & 0xC0 ) == 0x80 && ( b[3] & 0xC0 ) == 0x80 ) {
			*codePoint = ( ( c & 0x07 ) << 18 ) | ( ( b[1] & 0x3F ) << 12 ) | ( ( b[2] & 0x3F ) << 6 ) | ( b[3] & 0x3F );
			return 4;
		}
	}
	*codePoint = Q_CP1252ToUnicode( (int)c );
	return 1;
}

// writes a code point as UTF-8 without a terminating zero, out has room for 4 bytes. Returns the bytes written.
int Q_UTF8_Encode( unsigned int codePoint, char *out ) {
	if ( codePoint < 0x80 ) {
		out[0] = (char)codePoint;
		return 1;
	}
	if ( codePoint < 0x800 ) {
		out[0] = (char)( 0xC0 | ( codePoint >> 6 ) );
		out[1] = (char)( 0x80 | ( codePoint & 0x3F ) );
		return 2;
	}
	if ( codePoint > 0x10FFFF || ( codePoint >= 0xD800 && codePoint <= 0xDFFF ) ) {
		codePoint = 0xFFFD;		// the replacement character
	}
	if ( codePoint < 0x10000 ) {
		out[0] = (char)( 0xE0 | ( codePoint >> 12 ) );
		out[1] = (char)( 0x80 | ( ( codePoint >> 6 ) & 0x3F ) );
		out[2] = (char)( 0x80 | ( codePoint & 0x3F ) );
		return 3;
	}
	out[0] = (char)( 0xF0 | ( codePoint >> 18 ) );
	out[1] = (char)( 0x80 | ( ( codePoint >> 12 ) & 0x3F ) );
	out[2] = (char)( 0x80 | ( ( codePoint >> 6 ) & 0x3F ) );
	out[3] = (char)( 0x80 | ( codePoint & 0x3F ) );
	return 4;
}

// the offset of the character that ends at offset, for a cursor that steps back
int Q_UTF8_PrevChar( const char *string, int offset ) {
	int pos = 0;

	while ( pos < offset ) {
		unsigned int	codePoint;
		const int		len = Q_UTF8_Decode( string + pos, &codePoint );

		if ( !len || pos + len >= offset ) {
			break;
		}
		pos += len;
	}
	return pos;
}

// cuts a string to maxBytes at most, between two characters
void Q_UTF8_Truncate( char *string, int maxBytes ) {
	int pos = 0;

	for ( ;; ) {
		unsigned int	codePoint;
		const int		len = Q_UTF8_Decode( string + pos, &codePoint );

		if ( !len ) {
			return;
		}
		if ( pos + len > maxBytes ) {
			string[pos] = '\0';
			return;
		}
		pos += len;
	}
}

// Windows-1252 to UTF-8. A character that does not fit into out any more ends the copy.
void Q_UTF8_FromCP1252( const char *in, char *out, int outSize ) {
	int len = 0;

	if ( outSize < 1 ) {
		return;
	}
	for ( ; *in; in++ ) {
		char		bytes[4];
		const int	count = Q_UTF8_Encode( Q_CP1252ToUnicode( (unsigned char)*in ), bytes );
		int			i;

		if ( len + count >= outSize ) {
			break;
		}
		for ( i = 0; i < count; i++ ) {
			out[len++] = bytes[i];
		}
	}
	out[len] = '\0';
}

/*
=============
Q_UTF8_ToCP1252

UTF-8 (or a mix, see Q_UTF8_Decode) to Windows-1252, for a client that knows nothing else. What Windows-1252 does
not have becomes a '?'.
=============
*/
void Q_UTF8_ToCP1252( const char *in, char *out, int outSize ) {
	int len = 0;

	if ( outSize < 1 ) {
		return;
	}
	while ( len < outSize - 1 ) {
		unsigned int	codePoint;
		const int		count = Q_UTF8_Decode( in, &codePoint );
		int				c;

		if ( !count ) {
			break;
		}
		in += count;
		c = Q_UnicodeToCP1252( codePoint );
		out[len++] = (char)( c < 0 ? '?' : c );
	}
	out[len] = '\0';
}

#if defined(_MSC_VER)
/*
=============
Q_vsnprintf

Special wrapper function for Microsoft's broken _vsnprintf() function.
MinGW comes with its own snprintf() which is not broken.
=============
*/

int Q_vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
	int retval;

	retval = _vsnprintf(str, size, format, ap);

	if(retval < 0 || retval == size)
	{
		// Microsoft doesn't adhere to the C99 standard of vsnprintf,
		// which states that the return value must be the number of
		// bytes written if the output string had sufficient length.
		//
		// Obviously we cannot determine that value from Microsoft's
		// implementation, so we have no choice but to return size.

		str[size - 1] = '\0';
		return size;
	}

	return retval;
}
#endif