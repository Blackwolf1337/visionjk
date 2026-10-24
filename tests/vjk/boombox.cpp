// codemp/game/boombox/bg_boombox_fetch.c: what a player types for a boombox, and what yt-dlp is asked for. Only
// a link of YouTube becomes an address, anything else becomes words to look for behind "ytsearch1:", so that
// nothing a player types reaches yt-dlp as an option or another site.
#include "c_api.h"

#include <boost/test/unit_test.hpp>

namespace {

// what Box_Target makes of it, or "!" and why not
std::string Target( const std::string &asked ) {
	char target[512], why[256];

	if ( !Box_Target( asked.c_str(), target, sizeof( target ), why, sizeof( why ) ) )
		return std::string( "!" ) + why;
	return target;
}

const std::string kWatch = "https://www.youtube.com/watch?v=";

}

BOOST_AUTO_TEST_SUITE( boombox )

BOOST_AUTO_TEST_CASE( video )
{
	const std::string id = "dQw4w9WgXcQ";

	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/watch?v=" + id ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "youtube.com/watch?v=" + id + "&t=42s" ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "https://youtube.com/watch?feature=share&v=" + id ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "https://m.youtube.com/watch?v=" + id ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "https://music.youtube.com/watch?v=" + id ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "https://youtu.be/" + id + "?si=abc" ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "HTTPS://WWW.YOUTUBE.COM/watch?v=" + id ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/shorts/" + id ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/embed/" + id ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "  ^1https://youtu.be/" + id + "  " ), kWatch + id );
}

BOOST_AUTO_TEST_CASE( playlist )
{
	const std::string list = "PLx0sYbCqOb8TBPRdmBHs5Iftvv9TPboYG", id = "dQw4w9WgXcQ";

	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/playlist?list=" + list ),
		"https://www.youtube.com/playlist?list=" + list );
	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/watch?v=" + id + "&list=" + list + "&index=3" ),
		kWatch + id + "&list=" + list );
	BOOST_CHECK_EQUAL( Target( "https://youtu.be/" + id + "?list=" + list ), kWatch + id + "&list=" + list );
	// the lists of an account are not asked for, the video is played alone
	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/watch?v=" + id + "&list=WL" ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/watch?v=" + id + "&list=RDMMdQw4w9WgXcQ" ), kWatch + id );
	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/playlist?list=LL" )[0], '!' );
}

BOOST_AUTO_TEST_CASE( search )
{
	BOOST_CHECK_EQUAL( Target( "lofi beats" ), "ytsearch1:lofi beats" );
	BOOST_CHECK_EQUAL( Target( "song.mp3" ), "ytsearch1:song.mp3" );
	// options of yt-dlp stay words to look for
	BOOST_CHECK_EQUAL( Target( "--exec rm" ), "ytsearch1:--exec rm" );
	BOOST_CHECK_EQUAL( Target( "-o /tmp/x" ), "ytsearch1:-o /tmp/x" );
	// quotes, backslashes and control characters are taken out
	BOOST_CHECK_EQUAL( Target( "a\"b\\c\nd" ), "ytsearch1:abcd" );
	BOOST_CHECK_EQUAL( Target( "   " )[0], '!' );
	BOOST_CHECK_EQUAL( Target( "" )[0], '!' );
}

BOOST_AUTO_TEST_CASE( refused )
{
	BOOST_CHECK_EQUAL( Target( "https://evil.example/watch?v=dQw4w9WgXcQ" )[0], '!' );
	BOOST_CHECK_EQUAL( Target( "www.example.com" )[0], '!' );
	BOOST_CHECK_EQUAL( Target( "file:///etc/passwd" ).substr( 0, 10 ), "ytsearch1:" );
	BOOST_CHECK_EQUAL( Target( "https://youtube.com.evil.example/watch?v=dQw4w9WgXcQ" )[0], '!' );
	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/watch?v=short" )[0], '!' );
	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/watch?v=dQw4w9WgXc$" )[0], '!' );
	BOOST_CHECK_EQUAL( Target( "https://www.youtube.com/" )[0], '!' );
}

// the address never runs past target, whatever was typed
BOOST_AUTO_TEST_CASE( bounded )
{
	char target[24], why[8];

	BOOST_CHECK( Box_Target( std::string( 2000, 'x' ).c_str(), target, sizeof( target ), why, sizeof( why ) ) );
	BOOST_CHECK_EQUAL( strlen( target ), sizeof( target ) - 1 );
	BOOST_CHECK( !Box_Target( "https://evil.example/x", target, sizeof( target ), why, sizeof( why ) ) );
	BOOST_CHECK_EQUAL( strlen( why ), sizeof( why ) - 1 );
}

BOOST_AUTO_TEST_SUITE_END()
