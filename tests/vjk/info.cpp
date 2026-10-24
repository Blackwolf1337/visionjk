// codemp/qcommon/q_shared.c: info strings (userinfo, serverinfo, configstrings), as the game modules read what
// clients and servers send. The engine has its own copy in q_shared.cpp.
#include "c_api.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE( info )

BOOST_AUTO_TEST_CASE( value_for_key )
{
	const char *info = "\\name\\Padawan\\rate\\25000\\model\\kyle/default";

	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( info, "name" ) ), "Padawan" );
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( info, "RATE" ) ), "25000" );
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( info, "model" ) ), "kyle/default" );
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( info, "snaps" ) ), "" );
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( "", "name" ) ), "" );
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( nullptr, "name" ) ), "" );
	// a key without a value, at the end
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( "\\name", "name" ) ), "" );
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( "\\name\\", "name" ) ), "" );
	// two values at once: each call has its own buffer
	const char *a = Info_ValueForKey( info, "name" ), *b = Info_ValueForKey( info, "rate" );
	BOOST_CHECK_EQUAL( std::string( a ), "Padawan" );
	BOOST_CHECK_EQUAL( std::string( b ), "25000" );
}

BOOST_AUTO_TEST_CASE( value_for_key_oversize )
{
	const std::string big = "\\k\\" + std::string( BIG_INFO_STRING, 'x' );

	BOOST_CHECK_THROW( Info_ValueForKey( big.c_str(), "k" ), ComError );
	// the longest one that is taken
	const std::string most = "\\k\\" + std::string( BIG_INFO_STRING - 4, 'x' );
	BOOST_CHECK_EQUAL( strlen( Info_ValueForKey( most.c_str(), "k" ) ), (size_t)BIG_INFO_STRING - 4 );
}

BOOST_AUTO_TEST_CASE( set_value_for_key )
{
	char info[MAX_INFO_STRING] = "\\name\\Padawan\\rate\\25000";

	Info_SetValueForKey( info, "rate", "90000" );
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( info, "rate" ) ), "90000" );
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( info, "name" ) ), "Padawan" );
	Info_SetValueForKey( info, "snaps", "40" );
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( info, "snaps" ) ), "40" );
	// an empty value takes the key out
	Info_SetValueForKey( info, "snaps", "" );
	BOOST_CHECK( !strstr( info, "snaps" ) );
}

// a backslash would make a key of a value, a quote or a semicolon end a command line
BOOST_AUTO_TEST_CASE( set_value_for_key_refused )
{
	char		info[MAX_INFO_STRING] = "\\name\\Padawan";
	const std::string before = info;

	Info_SetValueForKey( info, "name", "a\\rate\\1" );
	Info_SetValueForKey( info, "name", "a;quit" );
	Info_SetValueForKey( info, "name", "a\"b" );
	Info_SetValueForKey( info, "na\\me", "x" );
	BOOST_CHECK_EQUAL( std::string( info ), before );

	// past MAX_INFO_STRING nothing is added
	Info_SetValueForKey( info, "big", std::string( MAX_INFO_STRING, 'x' ).c_str() );
	BOOST_CHECK_LT( strlen( info ), (size_t)MAX_INFO_STRING );
	BOOST_CHECK_EQUAL( std::string( Info_ValueForKey( info, "big" ) ), "" );

	char big[BIG_INFO_STRING];
	memset( big, 'x', sizeof( big ) );
	big[sizeof( big ) - 1] = '\0';
	BOOST_CHECK_THROW( Info_SetValueForKey( big, "k", "v" ), ComError );
}

BOOST_AUTO_TEST_CASE( remove_key )
{
	char info[MAX_INFO_STRING] = "\\name\\Padawan\\rate\\25000\\snaps\\40";

	Info_RemoveKey( info, "rate" );
	BOOST_CHECK_EQUAL( std::string( info ), "\\name\\Padawan\\snaps\\40" );
	Info_RemoveKey( info, "nothing" );
	BOOST_CHECK_EQUAL( std::string( info ), "\\name\\Padawan\\snaps\\40" );
	// a key with a backslash is none
	Info_RemoveKey( info, "name\\Padawan" );
	BOOST_CHECK_EQUAL( std::string( info ), "\\name\\Padawan\\snaps\\40" );
}

BOOST_AUTO_TEST_CASE( validate )
{
	BOOST_CHECK( Info_Validate( "\\name\\Padawan" ) );
	BOOST_CHECK( !Info_Validate( "\\name\\a\"b" ) );
	BOOST_CHECK( !Info_Validate( "\\name\\a;b" ) );
	BOOST_CHECK( !Info_Validate( "\\name\\a\nb" ) );
}

BOOST_AUTO_TEST_CASE( next_pair )
{
	const char	*s = "\\name\\Padawan\\rate\\25000";
	char		key[BIG_INFO_KEY], value[BIG_INFO_VALUE];

	BOOST_CHECK( Info_NextPair( &s, key, value ) );
	BOOST_CHECK_EQUAL( std::string( key ), "name" );
	BOOST_CHECK_EQUAL( std::string( value ), "Padawan" );
	BOOST_CHECK( Info_NextPair( &s, key, value ) );
	BOOST_CHECK_EQUAL( std::string( key ), "rate" );
	BOOST_CHECK_EQUAL( std::string( value ), "25000" );
}

BOOST_AUTO_TEST_SUITE_END()
