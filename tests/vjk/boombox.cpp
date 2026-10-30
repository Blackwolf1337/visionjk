// codemp/game/boombox/bg_boombox_fetch.c: what a player types for a boombox, and what yt-dlp is asked for. Only
// a link of YouTube becomes an address, anything else becomes words to look for behind "ytsearch1:", so that
// nothing a player types reaches yt-dlp as an option or another site. And how the sound of a song comes: ffmpeg
// from the address yt-dlp found, or yt-dlp into ffmpeg when there is none, with two scripts in place of the
// programs that write down how they were started.
#include "c_api.h"

#include <boost/test/unit_test.hpp>

#ifndef _WIN32
	#include <fstream>
	#include <poll.h>
	#include <stdlib.h>
	#include <sys/stat.h>
	#include <unistd.h>
#endif

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

#ifndef _WIN32

namespace {

const int kSamples = 9600;		// what the scripts make of a song: 0.2 s of one channel

// A folder with yt-dlp and ffmpeg as scripts. yt-dlp prints $BOXTEST_URL and a User-Agent for a lookup of the
// address, and the bytes of a song for "-o -". ffmpeg passes on what comes from a pipe, and makes a song from an
// address only with BOXTEST_DIRECT=ok. Both write their arguments into log.
struct Tools {
	std::string	dir;
	boxTools_t	tools;

	Tools() {
		char temp[] = "/tmp/vjk-boxtest-XXXXXX";

		dir = mkdtemp( temp );
		Script( "yt-dlp", "case \" $* \" in *' -o - '*) head -c " + std::to_string( kSamples * 2 ) + " /dev/zero ;;"
			" *) printf '%s\\n%s\\n' \"$BOXTEST_URL\" 'TestAgent/1.0' ;; esac" );
		Script( "ffmpeg", "case \" $* \" in *' -i pipe:0 '*) cat ;; *) [ \"$BOXTEST_DIRECT\" = ok ] && head -c "
			+ std::to_string( kSamples * 2 ) + " /dev/zero ;; esac; exit 0" );
		memset( &tools, 0, sizeof( tools ) );
		Q_strncpyz( tools.ytdlp, ( dir + "/yt-dlp" ).c_str(), sizeof( tools.ytdlp ) );
		Q_strncpyz( tools.ffmpeg, ( dir + "/ffmpeg" ).c_str(), sizeof( tools.ffmpeg ) );
		setenv( "BOXTEST_DIR", dir.c_str(), 1 );
	}
	~Tools() {
		unlink( ( dir + "/yt-dlp" ).c_str() );
		unlink( ( dir + "/ffmpeg" ).c_str() );
		unlink( ( dir + "/log" ).c_str() );
		rmdir( dir.c_str() );
	}
	void Script( const std::string &name, const std::string &body ) {
		const std::string path = dir + "/" + name;
		std::ofstream( path ) << "#!/bin/sh\necho \"" << name << " $*\" >> \"$BOXTEST_DIR/log\"\n" << body << "\n";
		chmod( path.c_str(), 0755 );
	}
	std::string Log() {
		std::ifstream in( dir + "/log" );
		return std::string( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
	}
	void ClearLog() {
		unlink( ( dir + "/log" ).c_str() );
	}
};

boxStream_t stream;		// (the ring is large)

// the song until all of it came or its programs are through, at most five seconds
int Play( const char *id, int from, const boxTools_t &tools ) {
	char	why[128] = "";
	int		now = 1000, i;

	BOOST_REQUIRE_MESSAGE( Box_StreamStart( &stream, &tools, id, from, qfalse, 1, now, why, sizeof( why ) ), why );
	for ( i = 0; i < 250 && !stream.ended && Box_StreamQueued( &stream ) < kSamples; i++ ) {
		struct pollfd fds[2] = { { stream.proc.out, POLLIN, 0 }, { stream.proc.err, POLLIN, 0 } };

		poll( fds, 2, 20 );
		Box_StreamFill( &stream, now += 20 );
	}
	return Box_StreamQueued( &stream );
}

}

BOOST_AUTO_TEST_CASE( stream_direct )
{
	Tools t;

	setenv( "BOXTEST_URL", "https://rr1---sn-abc.googlevideo.com/videoplayback?expire=1&id=x", 1 );
	setenv( "BOXTEST_DIRECT", "ok", 1 );
	BOOST_CHECK_EQUAL( Play( "aaaaaaaaaaa", 0, t.tools ), kSamples );
	BOOST_CHECK_EQUAL( stream.stage, BOX_STAGE_DIRECT );
	const std::string log = t.Log();
	BOOST_CHECK( log.find( "--print url" ) != std::string::npos );
	BOOST_CHECK( log.find( "-protocol_whitelist https,tls,tcp" ) != std::string::npos );
	BOOST_CHECK( log.find( "-user_agent TestAgent/1.0" ) != std::string::npos );
	BOOST_CHECK( log.find( "-i https://rr1---sn-abc.googlevideo.com/videoplayback" ) != std::string::npos );
	BOOST_CHECK( log.find( "-o -" ) == std::string::npos );

	// a seek: the address that was kept, and the part of the song from there, without yt-dlp
	t.ClearLog();
	BOOST_CHECK_EQUAL( Play( "aaaaaaaaaaa", 30 * BOX_RATE, t.tools ), kSamples );
	const std::string seek = t.Log();
	BOOST_CHECK( seek.find( "yt-dlp" ) == std::string::npos );
	BOOST_CHECK( seek.find( "-ss 30.000 -i https://" ) != std::string::npos );
	Box_StreamStop( &stream );
}

BOOST_AUTO_TEST_CASE( stream_foreign_address )
{
	Tools t;

	// what is no https of googlevideo.com never reaches ffmpeg: yt-dlp fetches the song itself
	setenv( "BOXTEST_DIRECT", "ok", 1 );
	const char *bad[] = { "http://rr1.googlevideo.com/videoplayback", "https://127.0.0.1/videoplayback",
		"https://evil.example/.googlevideo.com/x", "https://x.googlevideo.com.evil.example/x", "https://googlevideo.com/x",
		"https://x.googlevideo.com/a b", "file:///etc/passwd", "NA" };
	const char *ids[] = { "bbbbbbbbbb1", "bbbbbbbbbb2", "bbbbbbbbbb3", "bbbbbbbbbb4", "bbbbbbbbbb5", "bbbbbbbbbb6", "bbbbbbbbbb7", "bbbbbbbbbb8" };

	for ( int i = 0; i < 8; i++ ) {
		t.ClearLog();
		setenv( "BOXTEST_URL", bad[i], 1 );
		BOOST_CHECK_EQUAL( Play( ids[i], 0, t.tools ), kSamples );
		BOOST_CHECK_EQUAL( stream.stage, BOX_STAGE_PIPE );
		BOOST_CHECK_MESSAGE( t.Log().find( std::string( "-i " ) + bad[i] ) == std::string::npos, bad[i] );
	}
	Box_StreamStop( &stream );
}

BOOST_AUTO_TEST_CASE( stream_dead_address )
{
	Tools t;

	// ffmpeg gets nothing from the address: the pipe takes over, and the address is not tried again
	setenv( "BOXTEST_URL", "https://rr2.googlevideo.com/videoplayback?gone=1", 1 );
	setenv( "BOXTEST_DIRECT", "no", 1 );
	BOOST_CHECK_EQUAL( Play( "ccccccccccc", 0, t.tools ), kSamples );
	BOOST_CHECK_EQUAL( stream.stage, BOX_STAGE_PIPE );
	t.ClearLog();
	BOOST_CHECK_EQUAL( Play( "ccccccccccc", 0, t.tools ), kSamples );
	BOOST_CHECK( t.Log().find( "--print url" ) != std::string::npos );
	Box_StreamStop( &stream );
}

BOOST_AUTO_TEST_CASE( stream_proxy )
{
	Tools t;

	// an address yt-dlp found through a proxy is no good for ffmpeg here: the pipe, without a lookup of it
	setenv( "BOXTEST_URL", "https://rr3.googlevideo.com/videoplayback", 1 );
	setenv( "BOXTEST_DIRECT", "ok", 1 );
	Q_strncpyz( t.tools.ytdlpArgs, "--proxy socks5://127.0.0.1:9050", sizeof( t.tools.ytdlpArgs ) );
	BOOST_CHECK_EQUAL( Play( "ddddddddddd", 0, t.tools ), kSamples );
	BOOST_CHECK_EQUAL( stream.stage, BOX_STAGE_PIPE );
	BOOST_CHECK( t.Log().find( "--print url" ) == std::string::npos );
	Box_StreamStop( &stream );
}

#endif

BOOST_AUTO_TEST_SUITE_END()
