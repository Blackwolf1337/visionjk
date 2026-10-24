// shared/update/upd_manifest.c: the file lists of the website and the paths in them, what the updater trusts
#include "c_api.h"

#include <boost/test/unit_test.hpp>

#include <memory>

namespace {

const std::string kHash( 64, 'a' );

// a list as the website writes it, with the lines after the head and the version
std::string List( const std::string &files, const std::string &version = "version 0.2.6\n" ) {
	return "visionjk-update 1\n" + version + files;
}

std::string FileLine( const std::string &path, const std::string &bytes = "18046", const std::string &kind = "file" ) {
	return kind + " " + bytes + " " + kHash + " " + path + "\n";
}

const char *Parse( const std::string &text, updList_t *list ) {
	return UPD_ParseList( text.data(), text.size(), list );
}

}

BOOST_AUTO_TEST_SUITE( update )

BOOST_AUTO_TEST_CASE( version )
{
	int v[3];

	BOOST_CHECK( UPD_ParseVersion( "0.2.6", v ) );
	BOOST_CHECK_EQUAL( v[0], 0 );
	BOOST_CHECK_EQUAL( v[1], 2 );
	BOOST_CHECK_EQUAL( v[2], 6 );
	BOOST_CHECK( UPD_ParseVersion( "9999.0.10", v ) );
	BOOST_CHECK( !UPD_ParseVersion( "10000.0.0", v ) );
	BOOST_CHECK( !UPD_ParseVersion( "0.2", v ) );
	BOOST_CHECK( !UPD_ParseVersion( "0.2.6.1", v ) );
	BOOST_CHECK( !UPD_ParseVersion( "0.2.6 ", v ) );
	BOOST_CHECK( !UPD_ParseVersion( " 0.2.6", v ) );
	BOOST_CHECK( !UPD_ParseVersion( "0..6", v ) );
	BOOST_CHECK( !UPD_ParseVersion( "0.2.-6", v ) );
	BOOST_CHECK( !UPD_ParseVersion( "", v ) );
}

BOOST_AUTO_TEST_CASE( release )
{
	int v[3];

	BOOST_CHECK( UPD_ParseRelease( "v0.2.6", v ) );
	// a build between two releases is somebody's own
	BOOST_CHECK( !UPD_ParseRelease( "v0.2.6-1-g6b0be1b", v ) );
	BOOST_CHECK( !UPD_ParseRelease( "0.2.6", v ) );
	BOOST_CHECK( !UPD_ParseRelease( "v0.1.0-rc1", v ) );
}

BOOST_AUTO_TEST_CASE( compare )
{
	const int a[3] = { 0, 2, 6 }, b[3] = { 0, 10, 0 }, c[3] = { 1, 0, 0 };

	BOOST_CHECK_LT( UPD_CompareVersion( a, b ), 0 );
	BOOST_CHECK_GT( UPD_CompareVersion( c, b ), 0 );
	BOOST_CHECK_EQUAL( UPD_CompareVersion( a, a ), 0 );
}

BOOST_AUTO_TEST_CASE( path )
{
	BOOST_CHECK( UPD_PathAllowed( "visionjk.x86_64" ) );
	BOOST_CHECK( UPD_PathAllowed( "japlus/zz_japlus_client_assets.pk3" ) );
	BOOST_CHECK( UPD_PathAllowed( "base/cgamex86_64.so" ) );

	// out of the folder of the game
	BOOST_CHECK( !UPD_PathAllowed( "../visionjk.x86_64" ) );
	BOOST_CHECK( !UPD_PathAllowed( "base/../../x" ) );
	BOOST_CHECK( !UPD_PathAllowed( "/etc/passwd" ) );
	BOOST_CHECK( !UPD_PathAllowed( "C:/Windows/x.dll" ) );
	BOOST_CHECK( !UPD_PathAllowed( "base\\x.dll" ) );
	BOOST_CHECK( !UPD_PathAllowed( "~/x" ) );
	// hidden files and empty parts
	BOOST_CHECK( !UPD_PathAllowed( ".bashrc" ) );
	BOOST_CHECK( !UPD_PathAllowed( "base/.x" ) );
	BOOST_CHECK( !UPD_PathAllowed( "base/./x" ) );
	BOOST_CHECK( !UPD_PathAllowed( "base//x" ) );
	BOOST_CHECK( !UPD_PathAllowed( "base/" ) );
	BOOST_CHECK( !UPD_PathAllowed( "" ) );
	BOOST_CHECK( !UPD_PathAllowed( "a b" ) );
	// the files of the updater itself
	BOOST_CHECK( !UPD_PathAllowed( "visionjk.x86_64.vjknew" ) );
	BOOST_CHECK( !UPD_PathAllowed( "visionjk.x86_64.vjkold" ) );
	BOOST_CHECK( !UPD_PathAllowed( "visionjk-update.txt" ) );

	BOOST_CHECK( UPD_PathAllowed( std::string( UPD_MAX_PATH - 1, 'a' ).c_str() ) );
	BOOST_CHECK( !UPD_PathAllowed( std::string( UPD_MAX_PATH, 'a' ).c_str() ) );
}

BOOST_AUTO_TEST_CASE( address )
{
	BOOST_CHECK( UPD_AddressAllowed( "https://jka.vision/downloads/update", qfalse ) );
	BOOST_CHECK( UPD_AddressAllowed( "https://jka.vision:8443/downloads/update", qfalse ) );
	BOOST_CHECK( UPD_AddressAllowed( "https://jka.vision", qfalse ) );
	// http only for the address a build was made with
	BOOST_CHECK( !UPD_AddressAllowed( "http://jka.vision/downloads/update", qfalse ) );
	BOOST_CHECK( UPD_AddressAllowed( "http://jka.vision/downloads/update", qtrue ) );

	BOOST_CHECK( !UPD_AddressAllowed( "https://user:pass@jka.vision/x", qfalse ) );
	BOOST_CHECK( !UPD_AddressAllowed( "https://jka.vision/x?y=1", qfalse ) );
	BOOST_CHECK( !UPD_AddressAllowed( "https://jka.vision/x#y", qfalse ) );
	BOOST_CHECK( !UPD_AddressAllowed( "https://jka.vision/../x", qfalse ) );
	BOOST_CHECK( !UPD_AddressAllowed( "https://jka.vision//x", qfalse ) );
	BOOST_CHECK( !UPD_AddressAllowed( "https://jka.vision/x/", qfalse ) );
	BOOST_CHECK( !UPD_AddressAllowed( "https://.jka.vision/x", qfalse ) );
	BOOST_CHECK( !UPD_AddressAllowed( "https://jka.vision:123456/x", qfalse ) );
	BOOST_CHECK( !UPD_AddressAllowed( "https:///x", qfalse ) );
	BOOST_CHECK( !UPD_AddressAllowed( "ftp://jka.vision/x", qfalse ) );
	BOOST_CHECK( !UPD_AddressAllowed( ( "https://" + std::string( UPD_MAX_ADDRESS, 'a' ) ).c_str(), qfalse ) );
}

BOOST_AUTO_TEST_CASE( list )
{
	// (36 KB, on the heap)
	std::unique_ptr<updList_t> list( new updList_t() );

	BOOST_CHECK( Parse( List( FileLine( "LICENSE-VisionJK.txt" ) + FileLine( "japlus/x.cfg", "12", "keep" ) ), list.get() ) == nullptr );
	BOOST_CHECK_EQUAL( list->version[1], 2 );
	BOOST_CHECK_EQUAL( list->numFiles, 2 );
	BOOST_CHECK_EQUAL( list->files[0].bytes, 18046 );
	BOOST_CHECK_EQUAL( list->files[0].sha256[0], 0xaa );
	BOOST_CHECK( !list->files[0].keep );
	BOOST_CHECK( list->files[1].keep );
	BOOST_CHECK_EQUAL( std::string( list->files[1].path ), "japlus/x.cfg" );

	// Windows line ends and lines a later release may add
	BOOST_CHECK( Parse( "visionjk-update 1\r\nversion 0.2.6\r\nnews of 0.3\r\n" + FileLine( "a" ), list.get() ) == nullptr );
	BOOST_CHECK_EQUAL( std::string( list->files[0].path ), "a" );
}

BOOST_AUTO_TEST_CASE( list_refused )
{
	std::unique_ptr<updList_t> list( new updList_t() );
	const std::string one = FileLine( "a" );

	BOOST_CHECK( Parse( "", list.get() ) != nullptr );
	BOOST_CHECK( Parse( "<html>", list.get() ) != nullptr );
	BOOST_CHECK( Parse( "visionjk-update 2\nversion 0.2.6\n" + one, list.get() ) != nullptr );
	BOOST_CHECK( Parse( List( "" ), list.get() ) != nullptr );
	BOOST_CHECK( Parse( List( one, "" ), list.get() ) != nullptr );
	BOOST_CHECK( Parse( List( one, "version 0.2.6\nversion 0.2.7\n" ), list.get() ) != nullptr );
	BOOST_CHECK( Parse( List( one, "version 0.2\n" ), list.get() ) != nullptr );
	// one bad line and nothing of it is kept
	BOOST_CHECK( Parse( List( one + FileLine( "../x" ) ), list.get() ) != nullptr );
	BOOST_CHECK_EQUAL( list->numFiles, 0 );
	BOOST_CHECK_EQUAL( list->files[0].path[0], '\0' );
	BOOST_CHECK( Parse( List( one + one ), list.get() ) != nullptr );
	BOOST_CHECK( Parse( List( FileLine( "a", "1073741825" ) ), list.get() ) != nullptr );
	BOOST_CHECK( Parse( List( FileLine( "a", "-1" ) ), list.get() ) != nullptr );
	BOOST_CHECK( Parse( List( "file 12 " + std::string( 64, 'A' ) + " a\n" ), list.get() ) != nullptr );
	BOOST_CHECK( Parse( List( "file 12 " + std::string( 63, 'a' ) + " a\n" ), list.get() ) != nullptr );
	BOOST_CHECK( Parse( List( "file 12 " + kHash + "\n" ), list.get() ) != nullptr );
	BOOST_CHECK( Parse( List( "file 12 " + kHash + " " + std::string( UPD_MAX_PATH, 'a' ) + "\n" ), list.get() ) != nullptr );

	// a zero byte inside a name
	const std::string zero = List( "file 12 " + kHash + " a" + std::string( 1, '\0' ) + "b\n" );
	BOOST_CHECK( UPD_ParseList( zero.data(), zero.size(), list.get() ) != nullptr );

	std::string many;
	for ( int i = 0; i <= UPD_MAX_FILES; i++ )
		many += FileLine( "f" + std::to_string( i ) );
	BOOST_CHECK( Parse( List( many ), list.get() ) != nullptr );

	BOOST_CHECK( Parse( List( one + std::string( UPD_MAX_LIST, '#' ) ), list.get() ) != nullptr );
}

BOOST_AUTO_TEST_SUITE_END()
