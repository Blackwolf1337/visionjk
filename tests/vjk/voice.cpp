// codemp/game/voice/bg_voice.c: the status above a head and the look of a player, which reach every client of
// a room through the masterserver as somebody else sent them
#include "c_api.h"

#include <boost/test/unit_test.hpp>

namespace {

voicePacket_t Packet( int type, const byte *data, int len ) {
	voicePacket_t p = {};

	p.type = type;
	p.data = data;
	p.dataLen = len;
	return p;
}

}

BOOST_AUTO_TEST_SUITE( voice )

BOOST_AUTO_TEST_CASE( status_round_trip )
{
	byte		out[VOICE_STATUS_TEXT];
	char		text[VOICE_STATUS_TEXT];
	qboolean	away = qfalse;
	const int	len = BG_VoiceStatusPack( out, qtrue, "listens to \xE2\x99\xAA lofi" );
	const voicePacket_t p = Packet( VOICE_STATUS, out, len );

	BOOST_CHECK( BG_VoiceStatusUnpack( &p, &away, text, sizeof( text ) ) );
	BOOST_CHECK( away );
	BOOST_CHECK_EQUAL( std::string( text ), "listens to \xE2\x99\xAA lofi" );
}

// a text that is too long is cut where a character ends, never inside one
BOOST_AUTO_TEST_CASE( status_cut )
{
	byte		out[VOICE_STATUS_TEXT];
	std::string	text( VOICE_STATUS_TEXT - 3, 'a' );

	text += "\xE2\x82\xAC\xE2\x82\xAC";
	const int len = BG_VoiceStatusPack( out, qfalse, text.c_str() );
	BOOST_CHECK_EQUAL( len, 1 + VOICE_STATUS_TEXT - 3 );
	BOOST_CHECK_EQUAL( out[0], 0 );

	const int plain = BG_VoiceStatusPack( out, qfalse, std::string( 200, 'a' ).c_str() );
	BOOST_CHECK_EQUAL( plain, VOICE_STATUS_TEXT );
}

BOOST_AUTO_TEST_CASE( status_refused )
{
	char		text[VOICE_STATUS_TEXT];
	qboolean	away;
	const byte	control[] = { 0, 'a', '\n', 'b' }, del[] = { 0, 'a', 127 }, plain[] = { 0, 'a', 'b' };

	voicePacket_t p = Packet( VOICE_STATUS, control, sizeof( control ) );
	BOOST_CHECK( !BG_VoiceStatusUnpack( &p, &away, text, sizeof( text ) ) );
	p = Packet( VOICE_STATUS, del, sizeof( del ) );
	BOOST_CHECK( !BG_VoiceStatusUnpack( &p, &away, text, sizeof( text ) ) );
	// no room for it and its end
	p = Packet( VOICE_STATUS, plain, sizeof( plain ) );
	BOOST_CHECK( !BG_VoiceStatusUnpack( &p, &away, text, 2 ) );
	BOOST_CHECK( BG_VoiceStatusUnpack( &p, &away, text, 3 ) );
	// another kind of packet, and one without its flags
	p = Packet( VOICE_LOOK, plain, sizeof( plain ) );
	BOOST_CHECK( !BG_VoiceStatusUnpack( &p, &away, text, sizeof( text ) ) );
	p = Packet( VOICE_STATUS, plain, 0 );
	BOOST_CHECK( !BG_VoiceStatusUnpack( &p, &away, text, sizeof( text ) ) );
}

BOOST_AUTO_TEST_CASE( look )
{
	char		text[32];
	const byte	good[] = "kyle/default", bad[] = { 'a', '\x1b', 'b' }, high[] = { 'a', 0xC3, 0xA4 };

	voicePacket_t p = Packet( VOICE_LOOK, good, sizeof( good ) - 1 );
	BOOST_CHECK( BG_VoiceLookUnpack( &p, text, sizeof( text ) ) );
	BOOST_CHECK_EQUAL( std::string( text ), "kyle/default" );
	BOOST_CHECK( !BG_VoiceLookUnpack( &p, text, (int)sizeof( good ) - 1 ) );
	p = Packet( VOICE_LOOK, bad, sizeof( bad ) );
	BOOST_CHECK( !BG_VoiceLookUnpack( &p, text, sizeof( text ) ) );
	// names of models are ASCII
	p = Packet( VOICE_LOOK, high, sizeof( high ) );
	BOOST_CHECK( !BG_VoiceLookUnpack( &p, text, sizeof( text ) ) );
	p = Packet( VOICE_LOOK, good, 0 );
	BOOST_CHECK( !BG_VoiceLookUnpack( &p, text, sizeof( text ) ) );
}

// whatever comes in from the network: Unpack takes it or not, but never reads past it
BOOST_AUTO_TEST_CASE( unpack_any_bytes )
{
	byte data[64];

	for ( int len = 0; len <= (int)sizeof( data ); len++ ) {
		for ( int fill = 0; fill < 256; fill += 51 ) {
			voicePacket_t p;

			memset( data, fill, sizeof( data ) );
			BG_VoiceUnpack( data, len, qtrue, &p );
			BG_VoiceUnpack( data, len, qfalse, &p );
		}
	}
	BOOST_CHECK( true );
}

BOOST_AUTO_TEST_SUITE_END()
