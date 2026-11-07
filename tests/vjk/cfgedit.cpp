// codemp/game/webadmin/wa_cfgedit.c: a config as statements, the value of one cvar changed, statements taken out
#include "c_api.h"

extern "C" {
#include "game/webadmin/wa_cfgedit.h"
}

#include <boost/test/unit_test.hpp>
#include <set>

namespace {

std::string Set( const std::string &text, const char *name, const char *value ) {
	char out[4096];
	const int n = WA_CfgSetValue( text.data(), (int)text.size(), out, sizeof( out ), name, value );

	BOOST_REQUIRE( n >= 0 );
	return std::string( out, n );
}

std::string Get( const std::string &text, const char *name ) {
	char out[256];

	if ( !WA_CfgGetValue( text.data(), (int)text.size(), name, out, sizeof( out ) ) )
		return "<none>";
	return out;
}

// the statements that set a cvar of these names go
qboolean PickNames( const char *text, const waCfgStatement_t *st, void *context ) {
	char name[64];

	if ( !WA_CfgSetter( text, st, name, sizeof( name ) ) )
		return qfalse;
	return ( (std::set<std::string> *)context )->count( name ) ? qtrue : qfalse;
}

// the statements on these lines go
qboolean PickLines( const char *, const waCfgStatement_t *st, void *context ) {
	return ( (std::set<int> *)context )->count( st->line ) ? qtrue : qfalse;
}

std::string Remove( const std::string &text, std::set<std::string> names ) {
	char out[4096];
	const int n = WA_CfgRemove( text.data(), (int)text.size(), out, sizeof( out ), PickNames, &names );

	BOOST_REQUIRE( n >= 0 );
	return std::string( out, n );
}

}

BOOST_AUTO_TEST_SUITE( cfgedit )

BOOST_AUTO_TEST_CASE( statements )
{
	const std::string text = "seta a \"1; 2\" // c; set x 1\n\n  set b 3;set c \"x y\" z\r\n// set d 4\nexec x.cfg";
	waCfgStatement_t st;
	int pos = 0, line = 1;
	char name[32], value[64];

	BOOST_REQUIRE( WA_CfgNext( text.data(), (int)text.size(), &pos, &line, &st ) );
	BOOST_CHECK_EQUAL( st.line, 1 );
	BOOST_CHECK_EQUAL( st.words, 3 );
	BOOST_REQUIRE( WA_CfgSetter( text.data(), &st, name, sizeof( name ) ) );
	BOOST_CHECK_EQUAL( name, "a" );
	WA_CfgValue( text.data(), &st, 2, value, sizeof( value ) );
	BOOST_CHECK_EQUAL( value, "1; 2" );

	BOOST_REQUIRE( WA_CfgNext( text.data(), (int)text.size(), &pos, &line, &st ) );
	BOOST_CHECK_EQUAL( st.line, 3 );
	BOOST_REQUIRE( WA_CfgSetter( text.data(), &st, name, sizeof( name ) ) );
	BOOST_CHECK_EQUAL( name, "b" );

	BOOST_REQUIRE( WA_CfgNext( text.data(), (int)text.size(), &pos, &line, &st ) );
	BOOST_CHECK_EQUAL( st.line, 3 );
	BOOST_CHECK_EQUAL( st.words, 4 );
	WA_CfgValue( text.data(), &st, 2, value, sizeof( value ) );
	BOOST_CHECK_EQUAL( value, "x y z" );

	// the comment line is no statement
	BOOST_REQUIRE( WA_CfgNext( text.data(), (int)text.size(), &pos, &line, &st ) );
	BOOST_CHECK_EQUAL( st.line, 5 );
	BOOST_CHECK( !WA_CfgSetter( text.data(), &st, name, sizeof( name ) ) );
	BOOST_CHECK( !WA_CfgNext( text.data(), (int)text.size(), &pos, &line, &st ) );
}

BOOST_AUTO_TEST_CASE( get_value )
{
	const std::string text = "seta g_gravity 800\nSET G_GRAVITY \"1000\" // moon\nsv_fps 30\nset sv_fps\n";

	// the last one counts, the name in any case
	BOOST_CHECK_EQUAL( Get( text, "g_gravity" ), "1000" );
	// "name value" sets as well, "set name" without a value is an empty one
	BOOST_CHECK_EQUAL( Get( text, "sv_fps" ), "" );
	BOOST_CHECK_EQUAL( Get( "sv_fps 30", "sv_fps" ), "30" );
	BOOST_CHECK_EQUAL( Get( text, "g_speed" ), "<none>" );
	// a word with the name inside a value is no statement of it
	BOOST_CHECK_EQUAL( Get( "set d1 \"set g_speed 1\"", "g_speed" ), "<none>" );
}

BOOST_AUTO_TEST_CASE( set_in_place )
{
	// the last one changes, its comment and everything else stay
	BOOST_CHECK_EQUAL( Set( "seta a 1 // one\nseta a 2   // two\nseta b 3\n", "a", "5" ),
		"seta a 1 // one\nseta a \"5\"   // two\nseta b 3\n" );
	// a value of several words is one value
	BOOST_CHECK_EQUAL( Set( "set motd hello there; set b 1", "motd", "hi" ), "set motd \"hi\"; set b 1" );
	BOOST_CHECK_EQUAL( Set( "set a", "a", "1" ), "set a \"1\"" );
	BOOST_CHECK_EQUAL( Set( "a 1\r\n", "a", "2" ), "a \"2\"\r\n" );
}

BOOST_AUTO_TEST_CASE( set_appended )
{
	const std::string once = Set( "seta a 1", "b", "2" );

	BOOST_CHECK_EQUAL( once, "seta a 1\n\n// set by the web panel\nseta b \"2\"\n" );
	// the block is made once
	BOOST_CHECK_EQUAL( Set( once, "c", "x y" ), once + "seta c \"x y\"\n" );
	BOOST_CHECK_EQUAL( Set( "", "a", "1" ), "// set by the web panel\nseta a \"1\"\n" );
}

BOOST_AUTO_TEST_CASE( set_no_room )
{
	char out[8];

	BOOST_CHECK_EQUAL( WA_CfgSetValue( "seta a 1", 8, out, sizeof( out ), "a", "12345" ), -1 );
}

BOOST_AUTO_TEST_CASE( remove )
{
	// a line of its own goes with its comment
	BOOST_CHECK_EQUAL( Remove( "seta a 1 // old\nseta b 2\n", { "a" } ), "seta b 2\n" );
	// one of several on a line
	BOOST_CHECK_EQUAL( Remove( "set a 1; set b 2 // c\n", { "a" } ), "set b 2 // c\n" );
	BOOST_CHECK_EQUAL( Remove( "set a 1; set b 2 // c\n", { "b" } ), "set a 1 // c\n" );
	BOOST_CHECK_EQUAL( Remove( "set a 1;set b 2;set c 3\n", { "b" } ), "set a 1;set c 3\n" );
	// all of a line
	BOOST_CHECK_EQUAL( Remove( "x\nset a 1; set b 2\ny", { "a", "b" } ), "x\ny" );
	BOOST_CHECK_EQUAL( Remove( "x\nset a 1", { "a" } ), "x\n" );
	// a ';' in quotes ends nothing
	BOOST_CHECK_EQUAL( Remove( "set d1 \"map x; set a 1\"\nset a 1\n", { "a" } ), "set d1 \"map x; set a 1\"\n" );
	BOOST_CHECK_EQUAL( Remove( "exec a.cfg\n", { "a" } ), "exec a.cfg\n" );
}

BOOST_AUTO_TEST_CASE( remove_by_line )
{
	const std::string text = "seta a 1\nseta a 2\nseta a 3\n";
	std::set<int> lines = { 1, 2 };
	char out[256];
	const int n = WA_CfgRemove( text.data(), (int)text.size(), out, sizeof( out ), PickLines, &lines );

	BOOST_CHECK_EQUAL( std::string( out, n ), "seta a 3\n" );
}

BOOST_AUTO_TEST_SUITE_END()
