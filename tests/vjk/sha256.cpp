// shared/web/web_sha256.c: SHA-256, which the updater checks every file of a release with (moved here from
// tools/updatetest/updatetest.c of the agent folder)
#include "c_api.h"

#include <boost/test/unit_test.hpp>

#include <vector>

namespace {

// the hash of len bytes at data, fed to Web_SHA256Update in pieces of piece bytes, in hex
std::string Sha( const void *data, size_t len, size_t piece ) {
	unsigned char	digest[WEB_SHA256_SIZE];
	char			hex[WEB_SHA256_SIZE * 2 + 1];
	webSha256_t		ctx;

	Web_SHA256Init( &ctx );
	for ( size_t at = 0; at < len; at += piece )
		Web_SHA256Update( &ctx, (const char *)data + at, ( len - at < piece ) ? len - at : piece );
	Web_SHA256Final( &ctx, digest );
	for ( int i = 0; i < WEB_SHA256_SIZE; i++ )
		snprintf( hex + i * 2, 3, "%02x", digest[i] );
	return hex;
}

const std::string kEmpty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
const std::string kAbc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
const std::string kMillionA = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

}

BOOST_AUTO_TEST_SUITE( sha256 )

// the values of FIPS 180-4 and its examples
BOOST_AUTO_TEST_CASE( fips )
{
	const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	const std::vector<char> million( 1000000, 'a' );
	unsigned char digest[WEB_SHA256_SIZE];

	BOOST_CHECK_EQUAL( Sha( "", 0, 1 ), kEmpty );
	BOOST_CHECK_EQUAL( Sha( "abc", 3, 3 ), kAbc );
	BOOST_CHECK_EQUAL( Sha( "abc", 3, 1 ), kAbc );
	BOOST_CHECK_EQUAL( Sha( two.data(), two.size(), two.size() ),
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" );
	BOOST_CHECK_EQUAL( Sha( million.data(), million.size(), million.size() ), kMillionA );
	BOOST_CHECK_EQUAL( Sha( million.data(), million.size(), 65536 ), kMillionA );
	BOOST_CHECK_EQUAL( Sha( million.data(), million.size(), 7 ), kMillionA );

	// the one call does the same as the three
	Web_SHA256( "abc", 3, digest );
	BOOST_CHECK_EQUAL( digest[0], 0xba );
	BOOST_CHECK_EQUAL( digest[31], 0xad );
}

// every length from nothing to a few blocks: fed in pieces of every size around a block it is the hash of the whole
BOOST_AUTO_TEST_CASE( pieces )
{
	unsigned char	noise[256];
	unsigned int	state = 4711;

	for ( unsigned char &b : noise ) {
		state = state * 1664525u + 1013904223u;
		b = (unsigned char)( state >> 8 );
	}
	for ( size_t len = 0; len <= 200; len++ ) {
		const std::string whole = Sha( noise, len, len ? len : 1 );

		for ( size_t piece = 1; piece <= 130; piece += ( piece < 70 ) ? 1 : 13 ) {
			if ( Sha( noise, len, piece ) != whole )
				BOOST_ERROR( len << " bytes in pieces of " << piece );
		}
	}
}

BOOST_AUTO_TEST_SUITE_END()
