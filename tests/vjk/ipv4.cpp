// shared/qcommon/q_string.c: Q_ParseIPv4, the strict IPv4 address of the bind cvars
#include "c_api.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE( ipv4 )

BOOST_AUTO_TEST_CASE( good )
{
	unsigned char ip[4] = {};

	BOOST_CHECK( Q_ParseIPv4( "192.168.0.180", ip ) );
	BOOST_CHECK( ip[0] == 192 && ip[1] == 168 && ip[2] == 0 && ip[3] == 180 );
	BOOST_CHECK( Q_ParseIPv4( "0.0.0.0", ip ) );
	BOOST_CHECK( ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0 );
	BOOST_CHECK( Q_ParseIPv4( "255.255.255.255", ip ) );
	BOOST_CHECK( ip[0] == 255 && ip[3] == 255 );
}

BOOST_AUTO_TEST_CASE( bad )
{
	unsigned char ip[4] = { 1, 2, 3, 4 };

	// what inet_addr takes and inet_pton does not
	for ( const char *s : { "0", "1", "127.1", "0x7f.0.0.1", "1.2.3" } ) {
		BOOST_CHECK_MESSAGE( !Q_ParseIPv4( s, ip ), s );
	}
	for ( const char *s : { "", "256.0.0.1", "1.2.3.4.5", "1.2.3.4 ", " 1.2.3.4", "1..2.3", "1.2.3.", "1.2.3.0004",
		"localhost", "1.2.3.4:29070", "-1.2.3.4" } ) {
		BOOST_CHECK_MESSAGE( !Q_ParseIPv4( s, ip ), s );
	}
	// a failed parse leaves the output as it was
	BOOST_CHECK( ip[0] == 1 && ip[1] == 2 && ip[2] == 3 && ip[3] == 4 );
}

BOOST_AUTO_TEST_SUITE_END()
