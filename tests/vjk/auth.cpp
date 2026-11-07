// game/auth/bg_auth.c: the message of the key login and admin_hosts.txt, the file a client keeps the host keys
// of the servers in
#include "c_api.h"

#include <boost/test/unit_test.hpp>

namespace {

struct Host {
	byte key[AUTH_HOST_SIZE];
	explicit Host( byte fill ) { memset( key, fill, sizeof( key ) ); }
};

std::string Line( const char *address, const Host &host ) {
	char text[AUTH_BASE64_SIZE];

	BG_AuthEncode( host.key, AUTH_HOST_SIZE, text, sizeof( text ) );
	return std::string( address ) + " " + text + "\n";
}

std::string Set( const std::string &text, const char *address, const Host *host, int size = AUTH_HOSTS_FILE_SIZE ) {
	std::string out( size, '\0' );
	const int len = BG_AuthHostsSet( text.c_str(), address, host ? host->key : nullptr, &out[0], size );

	BOOST_REQUIRE( len >= 0 && len < size );
	BOOST_CHECK_EQUAL( strlen( out.c_str() ), (size_t)len );
	out.resize( len );
	return out;
}

}

BOOST_AUTO_TEST_SUITE( auth )

BOOST_AUTO_TEST_CASE( message )
{
	byte nonce[AUTH_NONCE_SIZE], v1[AUTH_MESSAGE_SIZE], v2[AUTH_MESSAGE_SIZE];
	const Host host( 0x5a );
	const char *first1 = "VisionJK admin login v1\n", *first2 = "VisionJK admin login v2\n";

	memset( nonce, 0xa5, sizeof( nonce ) );
	const int len1 = BG_AuthMessage( v1, sizeof( v1 ), nonce, nullptr, "1.2.3.4:29070", 7 );
	const int len2 = BG_AuthMessage( v2, sizeof( v2 ), nonce, host.key, "1.2.3.4:29070", 7 );

	// version 1 stays what clients of before sign
	BOOST_REQUIRE_EQUAL( len1, 24 + AUTH_NONCE_SIZE + 15 );
	BOOST_CHECK( !memcmp( v1, first1, 24 ) );
	BOOST_CHECK( !memcmp( v1 + 24, nonce, AUTH_NONCE_SIZE ) );
	BOOST_CHECK( !memcmp( v1 + 24 + AUTH_NONCE_SIZE, "1.2.3.4:29070\n7", 15 ) );

	BOOST_REQUIRE_EQUAL( len2, len1 + AUTH_HOST_SIZE );
	BOOST_CHECK( !memcmp( v2, first2, 24 ) );
	BOOST_CHECK( !memcmp( v2 + 24 + AUTH_NONCE_SIZE, host.key, AUTH_HOST_SIZE ) );
	BOOST_CHECK( !memcmp( v2 + 24 + AUTH_NONCE_SIZE + AUTH_HOST_SIZE, "1.2.3.4:29070\n7", 15 ) );

	// the longest address fits, one more does not
	const std::string longest( AUTH_ADDRESS_SIZE - 1, 'a' ), tooLong( AUTH_ADDRESS_SIZE, 'a' );
	BOOST_CHECK( BG_AuthMessage( v2, sizeof( v2 ), nonce, host.key, longest.c_str(), 31 ) > 0 );
	BOOST_CHECK_EQUAL( BG_AuthMessage( v2, sizeof( v2 ), nonce, host.key, tooLong.c_str(), 31 ), 0 );
	BOOST_CHECK_EQUAL( BG_AuthMessage( v2, 100, nonce, host.key, "1.2.3.4:29070", 7 ), 0 );
}

BOOST_AUTO_TEST_CASE( hosts_find )
{
	const Host a( 1 ), b( 2 ), c( 3 );
	const std::string text = Line( "1.1.1.1:29070", a ) + "# a line of a hand\n\n" + Line( "2.2.2.2:29070", b );
	char other[AUTH_ADDRESS_SIZE];

	BOOST_CHECK_EQUAL( BG_AuthHostsFind( "", "1.1.1.1:29070", a.key, other, sizeof( other ) ), AUTH_HOST_NEW );
	BOOST_CHECK_EQUAL( BG_AuthHostsFind( text.c_str(), "1.1.1.1:29070", a.key, other, sizeof( other ) ), AUTH_HOST_SAME );
	BOOST_CHECK_EQUAL( BG_AuthHostsFind( text.c_str(), "1.1.1.1:29070", c.key, other, sizeof( other ) ), AUTH_HOST_CHANGED );
	BOOST_CHECK_EQUAL( BG_AuthHostsFind( text.c_str(), "3.3.3.3:29070", c.key, other, sizeof( other ) ), AUTH_HOST_NEW );

	// a hostile server at 3.3.3.3 that passes on the question of 2.2.2.2 with its key
	BOOST_CHECK_EQUAL( BG_AuthHostsFind( text.c_str(), "3.3.3.3:29070", b.key, other, sizeof( other ) ), AUTH_HOST_ELSEWHERE );
	BOOST_CHECK_EQUAL( std::string( other ), "2.2.2.2:29070" );
	// and one that the client knows: its own line counts, the key of the other is "changed"
	BOOST_CHECK_EQUAL( BG_AuthHostsFind( text.c_str(), "1.1.1.1:29070", b.key, other, sizeof( other ) ), AUTH_HOST_CHANGED );
	BOOST_CHECK_EQUAL( std::string( other ), "" );

	// Windows line ends, no end on the last line, a broken key
	const std::string crlf = "1.1.1.1:29070 " + Line( "", a ).substr( 1, 44 ) + "\r\n2.2.2.2:29070 !!!";
	BOOST_CHECK_EQUAL( BG_AuthHostsFind( crlf.c_str(), "1.1.1.1:29070", a.key, other, sizeof( other ) ), AUTH_HOST_SAME );
	BOOST_CHECK_EQUAL( BG_AuthHostsFind( crlf.c_str(), "2.2.2.2:29070", b.key, other, sizeof( other ) ), AUTH_HOST_NEW );

	// an address in another case is the same one (a name as the player typed it)
	BOOST_CHECK_EQUAL( BG_AuthHostsFind( "Server.Example:29070 AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=\n", "server.example:29070", a.key, other, sizeof( other ) ), AUTH_HOST_SAME );
}

BOOST_AUTO_TEST_CASE( hosts_set )
{
	const Host a( 1 ), b( 2 ), c( 3 );
	const std::string text = Line( "1.1.1.1:29070", a ) + Line( "2.2.2.2:29070", b );

	BOOST_CHECK_EQUAL( Set( "", "1.1.1.1:29070", &a ), Line( "1.1.1.1:29070", a ) );
	// a changed one goes to the end, nothing twice
	BOOST_CHECK_EQUAL( Set( text, "1.1.1.1:29070", &c ), Line( "2.2.2.2:29070", b ) + Line( "1.1.1.1:29070", c ) );
	// taken away
	BOOST_CHECK_EQUAL( Set( text, "2.2.2.2:29070", nullptr ), Line( "1.1.1.1:29070", a ) );
	BOOST_CHECK_EQUAL( Set( text, "9.9.9.9:29070", nullptr ), text );
	// lines that are none are dropped, a last line without an end gets one
	BOOST_CHECK_EQUAL( Set( "junk\n" + text.substr( 0, text.size() - 1 ), "3.3.3.3:29070", nullptr ), text );

	// the oldest lines go when the file is full
	const int lineLen = (int)Line( "1.1.1.1:29070", a ).size();
	const std::string full = Set( text, "3.3.3.3:29070", &c, 2 * lineLen + 1 );
	BOOST_CHECK_EQUAL( full, Line( "2.2.2.2:29070", b ) + Line( "3.3.3.3:29070", c ) );
	BOOST_CHECK_EQUAL( Set( text, "3.3.3.3:29070", &c, lineLen + 1 ), Line( "3.3.3.3:29070", c ) );
}

BOOST_AUTO_TEST_SUITE_END()
