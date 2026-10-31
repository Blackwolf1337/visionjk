// game/toys/bg_toys.c: a throw of a toy as the relays hand it on (VOICE_TOY, vs_toy)
#include "c_api.h"

#include <boost/test/unit_test.hpp>

namespace {

toyThrow_t Throw( int kind, int x, int y, int z, int vx, int vy, int vz ) {
	toyThrow_t t = {};
	t.kind = kind;
	t.origin[0] = x; t.origin[1] = y; t.origin[2] = z;
	t.velocity[0] = vx; t.velocity[1] = vy; t.velocity[2] = vz;
	return t;
}

bool Valid( toyThrow_t t ) {
	return BG_ToyValid( &t ) != qfalse;
}

}

BOOST_AUTO_TEST_SUITE( toys )

BOOST_AUTO_TEST_CASE( round_trip )
{
	const toyThrow_t in = Throw( TOY_SNOWBALL, -1234, 56789 - 65536, 300, -900, 700, -150 );
	byte buf[TOY_BYTES + 4];
	toyThrow_t out;

	BOOST_REQUIRE_EQUAL( BG_ToyPack( &in, buf ), TOY_BYTES );
	BOOST_REQUIRE( BG_ToyUnpack( buf, TOY_BYTES, &out ) );
	BOOST_CHECK_EQUAL( out.kind, TOY_SNOWBALL );
	for ( int i = 0; i < 3; i++ ) {
		BOOST_CHECK_EQUAL( out.origin[i], in.origin[i] );
		BOOST_CHECK_EQUAL( out.velocity[i], in.velocity[i] );
	}
	// a byte too few or too many
	BOOST_CHECK( !BG_ToyUnpack( buf, TOY_BYTES - 1, &out ) );
	BOOST_CHECK( !BG_ToyUnpack( buf, TOY_BYTES + 1, &out ) );
	BOOST_CHECK( !BG_ToyUnpack( buf, 0, &out ) );
}

BOOST_AUTO_TEST_CASE( hello )
{
	const toyThrow_t in = Throw( TOY_HELLO, 0, 0, 0, 0, 0, 0 );
	byte buf[TOY_BYTES];
	toyThrow_t out;

	BOOST_REQUIRE_EQUAL( BG_ToyPack( &in, buf ), TOY_HELLO_BYTES );
	BOOST_CHECK( BG_ToyUnpack( buf, TOY_HELLO_BYTES, &out ) );
	BOOST_CHECK_EQUAL( out.kind, TOY_HELLO );
	// a hello with a body, a throw without one
	BOOST_CHECK( !BG_ToyUnpack( buf, TOY_BYTES, &out ) );
	buf[0] = TOY_SNOWBALL;
	BOOST_CHECK( !BG_ToyUnpack( buf, TOY_HELLO_BYTES, &out ) );
}

BOOST_AUTO_TEST_CASE( limits )
{
	BOOST_CHECK( Valid( Throw( TOY_SNOWBALL, 0, 0, 0, TOY_MAX_SPEED, 0, 0 ) ) );
	BOOST_CHECK( !Valid( Throw( TOY_SNOWBALL, 0, 0, 0, TOY_MAX_SPEED + 1, 0, 0 ) ) );
	// each axis within, the whole too fast
	BOOST_CHECK( !Valid( Throw( TOY_SNOWBALL, 0, 0, 0, 1200, 1200, 0 ) ) );
	BOOST_CHECK( !Valid( Throw( TOY_SNOWBALL, TOY_MAX_COORD, 0, 0, 0, 0, 0 ) ) );
	BOOST_CHECK( !Valid( Throw( TOY_SNOWBALL, 0, -TOY_MAX_COORD, 0, 0, 0, 0 ) ) );
	BOOST_CHECK( !Valid( Throw( TOY_HELLO, 0, 0, 0, 0, 0, 0 ) ) );
	BOOST_CHECK( !Valid( Throw( TOY_KINDS, 0, 0, 0, 0, 0, 0 ) ) );

	byte buf[TOY_BYTES];
	toyThrow_t out;
	const toyThrow_t fast = Throw( TOY_SNOWBALL, 0, 0, 0, 1200, 1200, 0 );
	BG_ToyPack( &fast, buf );
	BOOST_CHECK( !BG_ToyUnpack( buf, TOY_BYTES, &out ) );
}

BOOST_AUTO_TEST_SUITE_END()
