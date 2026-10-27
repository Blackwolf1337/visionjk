// shared/web/web_missing.c: the paks a web address did not have (japlus/urlRedirect_cache.txt)
#include "c_api.h"

#include <boost/test/unit_test.hpp>

namespace {

const long long kNow = 1800000000LL;
const long long kDay = 24 * 60 * 60;
const char *kBase = "http://dl.example.org/jka/";

std::string Line( long long time, const std::string &base, const std::string &remote ) {
	return std::to_string( time ) + " " + base + " " + remote + "\n";
}

std::string Add( const std::string &text, const char *remote, long long now = kNow, size_t size = 4096 ) {
	std::string out( size, '\0' );
	const size_t len = WEB_MissingAdd( text.c_str(), kBase, remote, now, &out[0], size );

	BOOST_REQUIRE_EQUAL( len, strlen( out.c_str() ) );
	return out.substr( 0, len );
}

}

BOOST_AUTO_TEST_SUITE( missing )

BOOST_AUTO_TEST_CASE( listed )
{
	const std::string text = Line( kNow - kDay, kBase, "japlus/maps one.pk3" )
		+ "a line that is none\n"
		+ Line( kNow - 7 * kDay, kBase, "japlus/old.pk3" )
		+ Line( kNow - kDay, "http://other.example.org/", "japlus/other.pk3" )
		+ Line( kNow + kDay, kBase, "japlus/future.pk3" )
		+ std::to_string( kNow ) + " " + kBase + " japlus/crlf.pk3\r\n";

	// the name with a blank in it is the rest of the line
	BOOST_CHECK( WEB_MissingListed( text.c_str(), kBase, "japlus/maps one.pk3", kNow ) );
	BOOST_CHECK( WEB_MissingListed( text.c_str(), kBase, "japlus/crlf.pk3", kNow ) );
	// older than six days, another address, a time after now
	BOOST_CHECK( !WEB_MissingListed( text.c_str(), kBase, "japlus/old.pk3", kNow ) );
	BOOST_CHECK( !WEB_MissingListed( text.c_str(), kBase, "japlus/other.pk3", kNow ) );
	BOOST_CHECK( WEB_MissingListed( text.c_str(), "http://other.example.org/", "japlus/other.pk3", kNow ) );
	BOOST_CHECK( !WEB_MissingListed( text.c_str(), kBase, "japlus/future.pk3", kNow ) );
	BOOST_CHECK( !WEB_MissingListed( text.c_str(), kBase, "japlus/maps", kNow ) );
	BOOST_CHECK( !WEB_MissingListed( "", kBase, "japlus/maps one.pk3", kNow ) );
	// six days to the second is too old
	BOOST_CHECK( WEB_MissingListed( Line( kNow - WEB_MISSING_SECONDS + 1, kBase, "japlus/a.pk3" ).c_str(), kBase, "japlus/a.pk3", kNow ) );
	BOOST_CHECK( !WEB_MissingListed( Line( kNow - WEB_MISSING_SECONDS, kBase, "japlus/a.pk3" ).c_str(), kBase, "japlus/a.pk3", kNow ) );
}

BOOST_AUTO_TEST_CASE( add )
{
	const std::string text = Line( kNow - 2 * kDay, kBase, "japlus/a.pk3" )
		+ "broken\n"
		+ Line( kNow - 8 * kDay, kBase, "japlus/old.pk3" )
		+ Line( kNow - kDay, kBase, "japlus/b.pk3" );

	// old and broken lines go, the new one comes last, a pak listed again has one line
	BOOST_CHECK_EQUAL( Add( text, "japlus/c.pk3" ), Line( kNow - 2 * kDay, kBase, "japlus/a.pk3" )
		+ Line( kNow - kDay, kBase, "japlus/b.pk3" ) + Line( kNow, kBase, "japlus/c.pk3" ) );
	BOOST_CHECK_EQUAL( Add( text, "japlus/a.pk3" ), Line( kNow - kDay, kBase, "japlus/b.pk3" ) + Line( kNow, kBase, "japlus/a.pk3" ) );
	BOOST_CHECK_EQUAL( Add( "", "japlus/c.pk3" ), Line( kNow, kBase, "japlus/c.pk3" ) );
	BOOST_CHECK( WEB_MissingListed( Add( text, "japlus/c.pk3" ).c_str(), kBase, "japlus/c.pk3", kNow ) );
}

BOOST_AUTO_TEST_CASE( full )
{
	std::string text;

	for ( int i = 0; i < 40; i++ ) {
		text += Line( kNow - kDay + i, kBase, "japlus/pak" + std::to_string( i ) + ".pk3" );
	}
	// what does not fit are the oldest lines
	const std::string out = Add( text, "japlus/new.pk3", kNow, 400 );

	BOOST_CHECK_LT( out.size(), 400u );
	BOOST_CHECK( WEB_MissingListed( out.c_str(), kBase, "japlus/new.pk3", kNow ) );
	BOOST_CHECK( WEB_MissingListed( out.c_str(), kBase, "japlus/pak39.pk3", kNow ) );
	BOOST_CHECK( !WEB_MissingListed( out.c_str(), kBase, "japlus/pak0.pk3", kNow ) );
	BOOST_CHECK_EQUAL( out.back(), '\n' );
}

BOOST_AUTO_TEST_CASE( refused )
{
	char out[256] = "x";

	// a name with a line break would make a line of its own, an address with a blank is none
	BOOST_CHECK_EQUAL( WEB_MissingAdd( "", kBase, "japlus/a\n1 http://x/ japlus/b.pk3", kNow, out, sizeof( out ) ), 0u );
	BOOST_CHECK_EQUAL( WEB_MissingAdd( "", "http://x y/", "japlus/a.pk3", kNow, out, sizeof( out ) ), 0u );
	BOOST_CHECK_EQUAL( WEB_MissingAdd( "", kBase, "japlus/a.pk3", kNow, out, 10 ), 0u );
	BOOST_CHECK_EQUAL( WEB_MissingAdd( "", kBase, "", kNow, out, sizeof( out ) ), 0u );
}

BOOST_AUTO_TEST_SUITE_END()
