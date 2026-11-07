// shared/sys/sys_memwatch_rules.c: how the memory monitor of the client reads /proc/self/maps and judges a region
#include "c_api.h"
#include "sys/sys_memwatch.h"

#include <boost/test/unit_test.hpp>

namespace {

const char * const linuxSystem[] = { "/usr", "/lib", "/opt", nullptr };
const char * const linuxTemp[] = { "/tmp", "/dev/shm", "/run/user/1000", nullptr };
const memwPlaces_t linuxPlaces = { "/home/p/VisionJK", linuxSystem, linuxTemp, qfalse };

const char * const winSystem[] = { "C:\\Windows", nullptr };
const char * const winTemp[] = { "C:\\Users\\p\\AppData\\Local\\Temp\\", nullptr };
const memwPlaces_t winPlaces = { "C:\\Games\\VisionJK", winSystem, winTemp, qtrue };

memwKind_t Line( const char *line, bool *parsed = nullptr ) {
	sysMemRegion_t r;
	const qboolean ok = MemW_ParseMapsLine( line, &r );

	if ( parsed ) {
		*parsed = ok;
	}
	return ok ? MemW_Classify( &r, &linuxPlaces ) : MEMW_NONE;
}

memwKind_t Image( const char *path, bool write = false ) {
	sysMemRegion_t r = {};

	r.start = 0x1000;
	r.end = 0x2000;
	r.image = qtrue;
	r.write = write ? qtrue : qfalse;
	Q_strncpyz( r.path, path, sizeof( r.path ) );
	return MemW_Classify( &r, &winPlaces );
}

}

BOOST_AUTO_TEST_SUITE( memwatch )

BOOST_AUTO_TEST_CASE( parse )
{
	sysMemRegion_t r;

	BOOST_REQUIRE( MemW_ParseMapsLine( "7f3a1c000000-7f3a1c021000 r-xp 00002000 103:02 1311242                   /usr/lib/libc.so.6\n", &r ) );
	BOOST_CHECK_EQUAL( r.start, (uintptr_t)0x7f3a1c000000ULL );
	BOOST_CHECK_EQUAL( r.end, (uintptr_t)0x7f3a1c021000ULL );
	BOOST_CHECK( !r.write );
	BOOST_CHECK( r.image );
	BOOST_CHECK( !r.deleted );
	BOOST_CHECK_EQUAL( r.path, "/usr/lib/libc.so.6" );

	// a path with blanks, and a deleted file
	BOOST_REQUIRE( MemW_ParseMapsLine( "1000-2000 r-xp 00000000 08:01 42 /home/p/My Games/a.so (deleted)", &r ) );
	BOOST_CHECK_EQUAL( r.path, "/home/p/My Games/a.so" );
	BOOST_CHECK( r.deleted );

	// anonymous, named by the kernel
	BOOST_REQUIRE( MemW_ParseMapsLine( "7ffd1000-7ffd3000 r-xp 00000000 00:00 0                          [vdso]", &r ) );
	BOOST_CHECK( !r.image );
	BOOST_CHECK_EQUAL( r.path, "[vdso]" );
	BOOST_REQUIRE( MemW_ParseMapsLine( "7f0000000000-7f0000010000 rwxp 00000000 00:00 0 ", &r ) );
	BOOST_CHECK( r.write );
	BOOST_CHECK_EQUAL( r.path, "" );

	// not executable, or no line of the file
	BOOST_CHECK( !MemW_ParseMapsLine( "1000-2000 r--p 00000000 08:01 42 /usr/lib/libc.so.6", &r ) );
	BOOST_CHECK( !MemW_ParseMapsLine( "2000-1000 r-xp 00000000 08:01 42 /usr/lib/libc.so.6", &r ) );
	BOOST_CHECK( !MemW_ParseMapsLine( "zzzz", &r ) );
	BOOST_CHECK( !MemW_ParseMapsLine( "1000-2000 r-xp", &r ) );
	BOOST_CHECK( !MemW_ParseMapsLine( "", &r ) );
	BOOST_CHECK( !MemW_ParseMapsLine( "11111111111111111-2000 r-xp 0 0 0 /x", &r ) );
}

BOOST_AUTO_TEST_CASE( linux )
{
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /usr/lib/libvulkan_radeon.so" ), MEMW_NONE );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /home/p/VisionJK/visionjk.x86_64" ), MEMW_NONE );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /home/p/VisionJK/japlus/cgamex86_64.so" ), MEMW_NONE );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 00:00 0 [vdso]" ), MEMW_NONE );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 00:00 0 [vsyscall]" ), MEMW_NONE );

	// strong
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 00:01 7 /memfd:x (deleted)" ), MEMW_EXEC_FILE );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /home/p/VisionJK/visionjk.x86_64 (deleted)" ), MEMW_EXEC_FILE );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /tmp/.x/payload.so" ), MEMW_EXEC_FILE );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /dev/shm/p" ), MEMW_EXEC_FILE );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /run/user/1000/p.so" ), MEMW_EXEC_FILE );

	// weak
	BOOST_CHECK_EQUAL( Line( "1000-2000 rwxp 0 00:00 0" ), MEMW_RWX );
	BOOST_CHECK_EQUAL( Line( "1000-2000 rwxp 0 08:01 1 /usr/lib/libodd.so" ), MEMW_RWX );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 00:00 0" ), MEMW_ANON );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 00:00 0 [anon:jit]" ), MEMW_ANON );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /home/p/.local/share/Steam/ubuntu12_64/gameoverlayrenderer.so" ), MEMW_LIBRARY );

	// a folder that only begins like the program folder, or like a temporary one
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /home/p/VisionJK2/x.so" ), MEMW_LIBRARY );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /tmpfoo/x.so" ), MEMW_LIBRARY );
	BOOST_CHECK_EQUAL( Line( "1000-2000 r-xp 0 08:01 1 /usrx/x.so" ), MEMW_LIBRARY );
}

BOOST_AUTO_TEST_CASE( windows )
{
	BOOST_CHECK_EQUAL( Image( "C:\\Windows\\System32\\kernel32.dll" ), MEMW_NONE );
	BOOST_CHECK_EQUAL( Image( "c:\\windows\\SysWOW64\\USER32.dll" ), MEMW_NONE );
	BOOST_CHECK_EQUAL( Image( "C:\\Games\\VisionJK\\SDL2.dll" ), MEMW_NONE );
	BOOST_CHECK_EQUAL( Image( "C:/Games/VisionJK/rd-vulkan-vision_x86_64.dll" ), MEMW_NONE );
	BOOST_CHECK_EQUAL( Image( "C:\\Users\\p\\AppData\\Local\\Temp\\x.dll" ), MEMW_EXEC_FILE );
	BOOST_CHECK_EQUAL( Image( "C:\\Program Files (x86)\\Steam\\gameoverlayrenderer64.dll" ), MEMW_LIBRARY );
	BOOST_CHECK_EQUAL( Image( "C:\\Games\\VisionJK\\a.dll", true ), MEMW_RWX );
}

BOOST_AUTO_TEST_CASE( under )
{
	BOOST_CHECK( MemW_Under( "/a/b", "/a", qfalse ) );
	BOOST_CHECK( MemW_Under( "/a/b", "/a/", qfalse ) );
	BOOST_CHECK( MemW_Under( "/a", "/a", qfalse ) );
	BOOST_CHECK( !MemW_Under( "/ab", "/a", qfalse ) );
	BOOST_CHECK( !MemW_Under( "/A/b", "/a", qfalse ) );
	BOOST_CHECK( MemW_Under( "C:\\A\\b", "c:/a", qtrue ) );
	BOOST_CHECK( !MemW_Under( "/a/b", "", qfalse ) );
	BOOST_CHECK( !MemW_Under( "/a/b", nullptr, qfalse ) );
}

BOOST_AUTO_TEST_SUITE_END()
