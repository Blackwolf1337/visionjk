// codemp/qcommon/cvar/cvar_seen.c: the day code last used a cvar, and which ones cvar_cleanup offers
#include "c_api.h"

extern "C" {
#include "qcommon/cvar/cvar_seen.h"
}

#include <boost/test/unit_test.hpp>

namespace {

const int today = 20750;

std::string Write() {
	static char out[1 << 20];
	const int n = CvarSeen_Write( out, sizeof( out ) );

	BOOST_REQUIRE( n >= 0 );
	return std::string( out, n );
}

}

BOOST_AUTO_TEST_SUITE( cvarseen )

BOOST_AUTO_TEST_CASE( days )
{
	CvarSeen_Clear();
	BOOST_CHECK_EQUAL( CvarSeen_Start(), 0 );

	CvarSeen_Touch( "cg_fov", today - 40 );
	BOOST_CHECK_EQUAL( CvarSeen_Start(), today - 40 );
	BOOST_CHECK_EQUAL( CvarSeen_Day( "CG_FOV" ), today - 40 );
	// a later day wins, an earlier one does not
	CvarSeen_Touch( "cg_fov", today - 2 );
	CvarSeen_Touch( "cg_fov", today - 50 );
	BOOST_CHECK_EQUAL( CvarSeen_Day( "cg_fov" ), today - 2 );
	BOOST_CHECK_EQUAL( CvarSeen_Day( "other" ), 0 );
}

BOOST_AUTO_TEST_CASE( unused )
{
	CvarSeen_Clear();
	CvarSeen_Touch( "fresh", today - 3 );
	CvarSeen_Touch( "old", today - CVARSEEN_DAYS );

	// the tracking began 3 days ago: a name never used is not offered yet
	BOOST_CHECK( !CvarSeen_Unused( "fresh", today ) );
	BOOST_CHECK( CvarSeen_Unused( "old", today ) );
	CvarSeen_Clear();
	CvarSeen_Touch( "fresh", today - 3 );
	BOOST_CHECK( !CvarSeen_Unused( "never", today ) );

	// a month later it is
	BOOST_CHECK( CvarSeen_Unused( "never", today + CVARSEEN_DAYS ) );
	BOOST_CHECK( CvarSeen_Unused( "fresh", today + CVARSEEN_DAYS ) );
	BOOST_CHECK( !CvarSeen_Unused( "fresh", today + CVARSEEN_DAYS - 4 ) );

	// a name too long to be kept is never offered
	const std::string longName( 80, 'x' );
	BOOST_CHECK( !CvarSeen_Unused( longName.c_str(), today + 1000 ) );
}

BOOST_AUTO_TEST_CASE( file_round_trip )
{
	CvarSeen_Clear();
	CvarSeen_Touch( "a", today - 10 );
	CvarSeen_Touch( "b", today - 1 );
	const std::string text = Write();

	BOOST_CHECK_EQUAL( text, "start " + std::to_string( today - 10 ) + "\na " + std::to_string( today - 10 ) + "\nb "
		+ std::to_string( today - 1 ) + "\n" );
	BOOST_CHECK( !CvarSeen_Dirty() );

	// this session used a again, the file says b: both stay, the earlier start counts
	CvarSeen_Clear();
	CvarSeen_Touch( "a", today );
	CvarSeen_Parse( ( text + "broken line\nc 0\nd 99999999\n" ).c_str(), today );
	BOOST_CHECK_EQUAL( CvarSeen_Start(), today - 10 );
	BOOST_CHECK_EQUAL( CvarSeen_Day( "a" ), today );
	BOOST_CHECK_EQUAL( CvarSeen_Day( "b" ), today - 1 );
	BOOST_CHECK_EQUAL( CvarSeen_Day( "c" ), 0 );
	BOOST_CHECK_EQUAL( CvarSeen_Day( "d" ), 0 );
	// the touch of a is not in the file yet
	BOOST_CHECK( CvarSeen_Dirty() );

	char tiny[8];
	BOOST_CHECK_EQUAL( CvarSeen_Write( tiny, sizeof( tiny ) ), -1 );
}

BOOST_AUTO_TEST_CASE( full_table )
{
	CvarSeen_Clear();
	for ( int i = 0; i < CVARSEEN_MAX; i++ )
		CvarSeen_Touch( ( "v" + std::to_string( i ) ).c_str(), today - CVARSEEN_MAX + i );
	// one more: the oldest eighth goes, the newest stay
	CvarSeen_Touch( "new", today );
	BOOST_CHECK_EQUAL( CvarSeen_Day( "new" ), today );
	BOOST_CHECK_EQUAL( CvarSeen_Day( "v0" ), 0 );
	BOOST_CHECK_EQUAL( CvarSeen_Day( ( "v" + std::to_string( CVARSEEN_MAX - 1 ) ).c_str() ), today - 1 );
	CvarSeen_Clear();
}

BOOST_AUTO_TEST_SUITE_END()
