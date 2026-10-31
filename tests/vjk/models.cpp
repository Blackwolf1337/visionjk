// shared/web/web_models.c: the models of the model store of the site (index.txt, the files of a model pak)
#include "c_api.h"

#include <boost/test/unit_test.hpp>

namespace {

const char *kSha = "222cad6b6dc259181f8f05b13d37e6daacf6d88dc7447c870588428ae1a52e03";

std::string From( const char *value ) {
	char out[WEB_MODEL_NAME];

	return WEB_ModelFromInfo( value, out, sizeof( out ) ) ? std::string( out ) : std::string( "-" );
}

}

BOOST_AUTO_TEST_SUITE( models )

BOOST_AUTO_TEST_CASE( names )
{
	BOOST_CHECK( WEB_ModelName( "vader" ) );
	BOOST_CHECK( WEB_ModelName( "jan_2" ) );
	BOOST_CHECK( WEB_ModelName( std::string( 32, 'a' ).c_str() ) );
	BOOST_CHECK( !WEB_ModelName( std::string( 33, 'a' ).c_str() ) );
	BOOST_CHECK( !WEB_ModelName( "a" ) );
	BOOST_CHECK( !WEB_ModelName( "" ) );
	BOOST_CHECK( !WEB_ModelName( "Vader" ) );
	BOOST_CHECK( !WEB_ModelName( "da-rth" ) );
	BOOST_CHECK( !WEB_ModelName( "../kyle" ) );

	BOOST_CHECK_EQUAL( From( "Vader/red" ), "vader" );
	BOOST_CHECK_EQUAL( From( "jan" ), "jan" );
	BOOST_CHECK_EQUAL( From( "jan/" ), "jan" );
	BOOST_CHECK_EQUAL( From( "/default" ), "-" );
	BOOST_CHECK_EQUAL( From( "a b/default" ), "-" );
	BOOST_CHECK_EQUAL( From( ( std::string( 40, 'x' ) + "/default" ).c_str() ), "-" );
	char small[8];
	BOOST_CHECK( !WEB_ModelFromInfo( "vader", small, sizeof( small ) ) );
}

BOOST_AUTO_TEST_CASE( entries )
{
	const char *const allowed[] = {
		"models/players/vader/model.glm", "models/players/vader/model_default.skin", "models/players/Vader/Body.JPG",
		"models/players/vader/sub/arm.tga", "models/players/vader/sounds.cfg", "models/players/vader/animation.cfg",
		"models/players/vader/animevents.cfg", "models/players/vader/custom.gla", "sound/chars/vader/misc/taunt.mp3",
		"sound/chars/vader/misc/death1.wav", "shaders/vader.shader", "readme.txt", "Readme_Vader.TXT", NULL };
	const char *const refused[] = {
		"mpdefault.cfg", "autoexec.cfg", "ui/jamp/main.menu", "maps/mp/ffa3.bsp", "jampgamex86.dll",
		"models/players/vader/autoexec.cfg", "models/players/vader/model.dll", "models/players/vader/sub/sounds.cfg",
		"models/players/kyle/model.glm", "models/players/vader2/model.glm", "models/players/vader",
		"models/players/vader/", "shaders/players.shader", "shaders/vader2.shader", "shaders/vader.shader.txt",
		"sound/chars/vader/misc/x.cfg", "sound/chars/kyle/misc/x.mp3", "docs/readme.txt", "readme.cfg",
		"../models/players/vader/x.jpg", "models/players/vader/../kyle/x.jpg", "models/players/vader//x.jpg",
		"models/players/vader/./x.jpg", "/models/players/vader/x.jpg", "models\\players\\vader\\x.jpg",
		"models/players/vader/a;b.jpg", "models/players/vader/a\"b.jpg", "models/players/vader/a'b.jpg",
		"c:/models/players/vader/x.jpg", "models/players/vader/x\x01.jpg", "", NULL };
	int i;

	for ( i = 0; allowed[i]; i++ ) {
		BOOST_CHECK_MESSAGE( WEB_ModelEntryAllowed( allowed[i], "vader" ), allowed[i] );
	}
	for ( i = 0; refused[i]; i++ ) {
		BOOST_CHECK_MESSAGE( !WEB_ModelEntryAllowed( refused[i], "vader" ), refused[i] );
	}
	BOOST_CHECK( !WEB_ModelEntryAllowed( ( "models/players/vader/" + std::string( 200, 'a' ) + ".jpg" ).c_str(), "vader" ) );
	BOOST_CHECK( !WEB_ModelEntryAllowed( "models/players/x/model.glm", "x" ) );
}

BOOST_AUTO_TEST_CASE( index )
{
	const std::string text = std::string( "# the model store\n" )
		+ "jan 1276771 " + kSha + "\n"
		+ "jantest 5 " + kSha + "\r\n"
		+ "big 104857601 " + kSha + "\n"
		+ "short 10 abcd\n"
		+ "zero 0 " + kSha + "\n"
		+ "last 7 " + kSha;
	long long bytes = 0;
	unsigned char sha[WEB_SHA256_SIZE];

	BOOST_REQUIRE( WEB_ModelIndexFind( text.c_str(), "jan", &bytes, sha ) );
	BOOST_CHECK_EQUAL( bytes, 1276771 );
	BOOST_CHECK_EQUAL( sha[0], 0x22 );
	BOOST_CHECK_EQUAL( sha[31], 0x03 );
	// a name that begins like another one, and \r\n
	BOOST_REQUIRE( WEB_ModelIndexFind( text.c_str(), "jantest", &bytes, sha ) );
	BOOST_CHECK_EQUAL( bytes, 5 );
	BOOST_REQUIRE( WEB_ModelIndexFind( text.c_str(), "last", &bytes, sha ) );
	BOOST_CHECK_EQUAL( bytes, 7 );
	BOOST_CHECK( !WEB_ModelIndexFind( text.c_str(), "big", &bytes, sha ) );
	BOOST_CHECK( !WEB_ModelIndexFind( text.c_str(), "short", &bytes, sha ) );
	BOOST_CHECK( !WEB_ModelIndexFind( text.c_str(), "zero", &bytes, sha ) );
	BOOST_CHECK( !WEB_ModelIndexFind( text.c_str(), "ja", &bytes, sha ) );
	BOOST_CHECK( !WEB_ModelIndexFind( text.c_str(), "nope", &bytes, sha ) );
	BOOST_CHECK( !WEB_ModelIndexFind( "", "jan", &bytes, sha ) );
}

BOOST_AUTO_TEST_SUITE_END()
