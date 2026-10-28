// shared/qcommon/q_string.c Q_StringEdExpand: the "@@@REF" lines of a server, as the cgame and the engine show them
#include "c_api.h"

#include <boost/test/unit_test.hpp>

namespace {

const char *Lookup( const char *ref ) {
	if ( !strcmp( ref, "PLENTER" ) ) {
		return "entered the game";
	}
	if ( !strcmp( ref, "LONG" ) ) {
		static std::string longText( 900, 'x' );
		return longText.c_str();
	}
	return "";
}

std::string Expand( const std::string &in, int outSize = 1024 ) {
	std::string out( outSize, '\x7f' );

	Q_StringEdExpand( &out[0], outSize, in.c_str(), Lookup );
	BOOST_REQUIRE_LT( strlen( out.c_str() ), (size_t)outSize );
	return out.c_str();
}

}

BOOST_AUTO_TEST_SUITE( stringed )

BOOST_AUTO_TEST_CASE( expand )
{
	BOOST_CHECK_EQUAL( Expand( "Kyle @@@PLENTER\n" ), "Kyle entered the game\n" );
	BOOST_CHECK_EQUAL( Expand( "@@@PLENTER." ), "entered the game." );
	BOOST_CHECK_EQUAL( Expand( "a @@@PLENTER" ), "a entered the game" );
	BOOST_CHECK_EQUAL( Expand( "no reference @@ here" ), "no reference @@ here" );
	BOOST_CHECK_EQUAL( Expand( "@@@" ), "@@@" );
	BOOST_CHECK_EQUAL( Expand( "" ), "" );
}

BOOST_AUTO_TEST_CASE( bounded )
{
	// the text after a long replacement is cut at the end of the buffer (it was written past it)
	const std::string tail( 1000, 'y' );
	const std::string out = Expand( "@@@LONG " + tail );

	BOOST_CHECK_EQUAL( out.size(), 1023u );
	BOOST_CHECK_EQUAL( out.substr( 0, 900 ), std::string( 900, 'x' ) );
	// a replacement that does not fit is left out, as Q_strcat leaves it
	BOOST_CHECK_EQUAL( Expand( "ab @@@LONG", 64 ), "ab " );
	// a small buffer
	BOOST_CHECK_EQUAL( Expand( "Kyle @@@PLENTER", 8 ), "Kyle " );
	BOOST_CHECK_EQUAL( Expand( std::string( 5000, 'z' ) ).size(), 1023u );
	BOOST_CHECK_EQUAL( Expand( "@@@" + std::string( 3000, 'R' ) + " end" ), " end" );
}

BOOST_AUTO_TEST_SUITE_END()
