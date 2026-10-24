// What the C code under test calls of the engine. Com_Error throws, so a test can see that it came and the code
// behind it does not run on (C code between the throw and the test has nothing to clean up).
#include "c_api.h"

#include <cstdarg>
#include <cstdio>

extern "C" {

void NORETURN QDECL Com_Error( int level, const char *error, ... ) {
	char	text[1024];
	va_list	args;

	va_start( args, error );
	vsnprintf( text, sizeof( text ), error, args );
	va_end( args );
	throw ComError( level, text );
}

void QDECL Com_Printf( const char *msg, ... ) {
	(void)msg;
}

}
