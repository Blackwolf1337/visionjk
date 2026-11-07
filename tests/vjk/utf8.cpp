// shared/qcommon/q_string.c: UTF-8 and Windows-1252, what chat and names of other clients go through
#include "c_api.h"

#include <boost/test/unit_test.hpp>

namespace {

// the code point at s and the bytes it took
std::pair<unsigned int, int> Decode( const char *s ) {
	unsigned int	codePoint;
	const int		len = Q_UTF8_Decode( s, &codePoint );

	return { codePoint, len };
}

}

BOOST_AUTO_TEST_SUITE( utf8 )

BOOST_AUTO_TEST_CASE( decode )
{
	BOOST_CHECK( Decode( "" ) == std::make_pair( 0u, 0 ) );
	BOOST_CHECK( Decode( "A" ) == std::make_pair( 0x41u, 1 ) );
	BOOST_CHECK( Decode( "\xC3\xA4" ) == std::make_pair( 0xE4u, 2 ) );					// ä
	BOOST_CHECK( Decode( "\xE2\x82\xAC" ) == std::make_pair( 0x20ACu, 3 ) );				// €
	BOOST_CHECK( Decode( "\xF0\x9F\x98\x80" ) == std::make_pair( 0x1F600u, 4 ) );		// an emoji
	BOOST_CHECK( Decode( "\xF4\x8F\xBF\xBF" ) == std::make_pair( 0x10FFFFu, 4 ) );
}

// no UTF-8: the first byte is one character of Windows-1252, and the bytes behind it are read on their own
BOOST_AUTO_TEST_CASE( decode_invalid )
{
	BOOST_CHECK( Decode( "\xC0\x80" ) == std::make_pair( 0xC0u, 1 ) );				// too long for its value
	BOOST_CHECK( Decode( "\xE0\x80\x80" ) == std::make_pair( 0xE0u, 1 ) );
	BOOST_CHECK( Decode( "\xF0\x80\x80\x80" ) == std::make_pair( 0xF0u, 1 ) );
	BOOST_CHECK( Decode( "\xED\xA0\x80" ) == std::make_pair( 0xEDu, 1 ) );			// a surrogate
	BOOST_CHECK( Decode( "\xF4\x90\x80\x80" ) == std::make_pair( 0xF4u, 1 ) );		// above U+10FFFF
	BOOST_CHECK( Decode( "\xF5\x80\x80\x80" ) == std::make_pair( 0xF5u, 1 ) );
	BOOST_CHECK( Decode( "\x80" ) == std::make_pair( 0x20ACu, 1 ) );					// € of Windows-1252
	BOOST_CHECK( Decode( "\xE4" ) == std::make_pair( 0xE4u, 1 ) );					// ä of Windows-1252
	// cut off by the end of the string: the zero is never taken as part of a character
	BOOST_CHECK( Decode( "\xC3" ) == std::make_pair( 0xC3u, 1 ) );
	BOOST_CHECK( Decode( "\xE2\x82" ) == std::make_pair( 0xE2u, 1 ) );
	BOOST_CHECK( Decode( "\xF0\x9F\x98" ) == std::make_pair( 0xF0u, 1 ) );
}

BOOST_AUTO_TEST_CASE( encode )
{
	char out[4];

	for ( unsigned int c : { 0x41u, 0x7Fu, 0x80u, 0x7FFu, 0x800u, 0xFFFFu, 0x10000u, 0x10FFFFu } ) {
		const int len = Q_UTF8_Encode( c, out );
		const std::string text( out, (size_t)len );

		BOOST_CHECK( Decode( text.c_str() ) == std::make_pair( c, len ) );
	}
	// a surrogate and what is above Unicode become the replacement character
	BOOST_CHECK_EQUAL( Q_UTF8_Encode( 0xD800, out ), 3 );
	BOOST_CHECK_EQUAL( std::string( out, 3 ), "\xEF\xBF\xBD" );
	BOOST_CHECK_EQUAL( Q_UTF8_Encode( 0x110000, out ), 3 );
	BOOST_CHECK_EQUAL( std::string( out, 3 ), "\xEF\xBF\xBD" );
}

BOOST_AUTO_TEST_CASE( cp1252_round_trip )
{
	for ( int byte = 1; byte < 256; byte++ ) {
		const char	in[2] = { (char)byte, 0 };
		char		utf8[8], back[4];

		Q_UTF8_FromCP1252( in, utf8, sizeof( utf8 ) );
		Q_UTF8_ToCP1252( utf8, back, sizeof( back ) );
		BOOST_CHECK_EQUAL( (unsigned char)back[0], byte );
		BOOST_CHECK_EQUAL( back[1], 0 );
	}
}

BOOST_AUTO_TEST_CASE( to_cp1252 )
{
	char out[16];

	Q_UTF8_ToCP1252( "a\xE2\x82\xAC\xF0\x9F\x98\x80z", out, sizeof( out ) );
	BOOST_CHECK_EQUAL( std::string( out ), "a\x80?z" );
	// never past the end of out
	Q_UTF8_ToCP1252( "abcdef", out, 4 );
	BOOST_CHECK_EQUAL( std::string( out ), "abc" );
	out[0] = 'x';
	Q_UTF8_ToCP1252( "abc", out, 0 );
	BOOST_CHECK_EQUAL( out[0], 'x' );
}

BOOST_AUTO_TEST_CASE( from_cp1252 )
{
	char out[8];

	// a character that does not fit any more ends the copy, it is not cut in half
	Q_UTF8_FromCP1252( "ab\x80", out, 5 );
	BOOST_CHECK_EQUAL( std::string( out ), "ab" );
	Q_UTF8_FromCP1252( "ab\x80", out, 6 );
	BOOST_CHECK_EQUAL( std::string( out ), "ab\xE2\x82\xAC" );
}

BOOST_AUTO_TEST_CASE( truncate )
{
	char text[16];

	Q_strncpyz( text, "a\xE2\x82\xAC" "b", sizeof( text ) );
	Q_UTF8_Truncate( text, 3 );
	BOOST_CHECK_EQUAL( std::string( text ), "a" );
	Q_strncpyz( text, "a\xE2\x82\xAC" "b", sizeof( text ) );
	Q_UTF8_Truncate( text, 4 );
	BOOST_CHECK_EQUAL( std::string( text ), "a\xE2\x82\xAC" );
	Q_strncpyz( text, "abc", sizeof( text ) );
	Q_UTF8_Truncate( text, 10 );
	BOOST_CHECK_EQUAL( std::string( text ), "abc" );
}

BOOST_AUTO_TEST_CASE( prev_char )
{
	const char *text = "a\xE2\x82\xAC" "b";

	BOOST_CHECK_EQUAL( Q_UTF8_PrevChar( text, 5 ), 4 );
	BOOST_CHECK_EQUAL( Q_UTF8_PrevChar( text, 4 ), 1 );
	BOOST_CHECK_EQUAL( Q_UTF8_PrevChar( text, 1 ), 0 );
	BOOST_CHECK_EQUAL( Q_UTF8_PrevChar( text, 0 ), 0 );
}

BOOST_AUTO_TEST_SUITE_END()
