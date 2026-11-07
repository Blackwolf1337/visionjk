// codemp/ui/ui_markdown.c: the entries of the changelog and their small Markdown, as tools/changelog.py of the
// website reads them
#include "c_api.h"

#include <boost/test/unit_test.hpp>

#include <vector>

namespace {

struct Block {
	mdBlockType_t type;
	std::string text;
};

std::vector<Block> Blocks( const std::string &body ) {
	std::vector<Block> out;
	mdReader_t reader;
	mdBlock_t block;

	MD_StartBlocks( &reader, body.data(), (int)body.size() );
	while ( MD_NextBlock( &reader, &block ) ) {
		BOOST_REQUIRE_GE( block.len, 0 );
		BOOST_REQUIRE( block.text >= body.data() && block.text + block.len <= body.data() + body.size() );
		out.push_back( { block.type, std::string( block.text, block.len ) } );
	}
	return out;
}

// the spans as "style:text" (and "|url" for a link), one after the other
std::string Spans( const std::string &text ) {
	std::string out;
	const char *p = text.data(), *end = p + text.size();
	mdSpan_t span;
	int guard = 0;

	while ( MD_NextSpan( &p, end, &span ) ) {
		static const char *names[] = { "t", "b", "i", "c", "l" };

		BOOST_REQUIRE( ++guard < 1000 );
		BOOST_REQUIRE( p <= end );
		out += std::string( names[span.style] ) + ":" + std::string( span.text, span.len );
		if ( span.style == MD_LINK ) {
			out += "|" + std::string( span.url, span.urlLen );
		}
		out += " ";
	}
	return out;
}

const std::string LOG =
	"---\n"
	"version: 0.2.9-dev.2\n"
	"date: 2026-10-24\n"
	"summary: A **new** scope.\n"
	"---\n"
	"\n"
	"- One\n"
	"  more of one\n"
	"  - below\n"
	"- Two\n"
	"\n"
	"---\n"
	"version: 0.2.8\n"
	"date: 2026-10-24\n"
	"summary: Channels.\n"
	"---\n"
	"\n"
	"Text.\n";

}

BOOST_AUTO_TEST_SUITE( markdown )

BOOST_AUTO_TEST_CASE( entries )
{
	mdEntry_t entries[4];

	BOOST_REQUIRE_EQUAL( MD_ReadEntries( LOG.data(), (int)LOG.size(), entries, 4 ), 2 );
	BOOST_CHECK_EQUAL( entries[0].version, "0.2.9-dev.2" );
	BOOST_CHECK_EQUAL( entries[0].date, "2026-10-24" );
	BOOST_CHECK_EQUAL( entries[0].summary, "A **new** scope." );
	BOOST_CHECK_EQUAL( std::string( entries[0].body, entries[0].bodyLen ), "\n- One\n  more of one\n  - below\n- Two\n\n" );
	BOOST_CHECK_EQUAL( entries[1].version, "0.2.8" );
	BOOST_CHECK_EQUAL( std::string( entries[1].body, entries[1].bodyLen ), "\nText.\n" );

	// no more than asked for, and nothing from a text that is no changelog
	BOOST_CHECK_EQUAL( MD_ReadEntries( LOG.data(), (int)LOG.size(), entries, 1 ), 1 );
	const std::string html = "<html>404</html>\n";
	BOOST_CHECK_EQUAL( MD_ReadEntries( html.data(), (int)html.size(), entries, 4 ), 0 );
	BOOST_CHECK_EQUAL( MD_ReadEntries( "", 0, entries, 4 ), 0 );

	// a front matter that is cut off, and one with a version that is none
	const std::string cut = "---\nversion: 0.1.0\ndate: 2026-10-10\n";
	BOOST_CHECK_EQUAL( MD_ReadEntries( cut.data(), (int)cut.size(), entries, 4 ), 0 );
	const std::string bad = "---\nversion: soon\n---\nx\n---\nversion: 0.1.0\nsummary: s\n---\n";
	BOOST_REQUIRE_EQUAL( MD_ReadEntries( bad.data(), (int)bad.size(), entries, 4 ), 1 );
	BOOST_CHECK_EQUAL( entries[0].version, "0.1.0" );

	// Windows line ends, and fields longer than their room
	const std::string crlf = "---\r\nversion: 0.1.0\r\nsummary: " + std::string( 1000, 's' ) + "\r\n---\r\n- a\r\n";
	BOOST_REQUIRE_EQUAL( MD_ReadEntries( crlf.data(), (int)crlf.size(), entries, 4 ), 1 );
	BOOST_CHECK_EQUAL( entries[0].version, "0.1.0" );
	BOOST_CHECK_EQUAL( strlen( entries[0].summary ), (size_t)MD_SUMMARY_LEN - 1 );
}

BOOST_AUTO_TEST_CASE( blocks )
{
	const auto b = Blocks( "- One\n  more of one\n  - below\n- Two\n\n## Head\nPara one\n  two\n- after\nplain\n### Small\n" );

	BOOST_REQUIRE_EQUAL( b.size(), 8u );
	BOOST_CHECK( b[0].type == MD_ITEM );
	BOOST_CHECK_EQUAL( b[0].text, "One\n  more of one" );
	BOOST_CHECK( b[1].type == MD_SUBITEM );
	BOOST_CHECK_EQUAL( b[1].text, "below" );
	BOOST_CHECK( b[2].type == MD_ITEM );
	BOOST_CHECK( b[3].type == MD_HEADING );
	BOOST_CHECK_EQUAL( b[3].text, "Head" );
	BOOST_CHECK( b[4].type == MD_PARAGRAPH );
	BOOST_CHECK_EQUAL( b[4].text, "Para one\n  two" );
	// a list ends a paragraph, and a line that is not two spaces in ends a list
	BOOST_CHECK( b[5].type == MD_ITEM );
	BOOST_CHECK( b[6].type == MD_PARAGRAPH );
	BOOST_CHECK_EQUAL( b[6].text, "plain" );
	BOOST_CHECK( b[7].type == MD_SUBHEADING );
	BOOST_CHECK_EQUAL( b[7].text, "Small" );

	// an indented line of a list after a paragraph starts a list, it is no line below one
	const auto c = Blocks( "Para\n  - item\n" );
	BOOST_REQUIRE_EQUAL( c.size(), 2u );
	BOOST_CHECK( c[1].type == MD_ITEM );

	// empty, blanks only, a lone mark
	BOOST_CHECK( Blocks( "" ).empty() );
	BOOST_CHECK( Blocks( "  \n\t\n" ).empty() );
	// ("- " with nothing after it is no line of a list: its blank is cut off with the end of the line)
	const auto d = Blocks( "-\n- \n" );
	BOOST_REQUIRE_EQUAL( d.size(), 1u );
	BOOST_CHECK( d[0].type == MD_PARAGRAPH );
	BOOST_CHECK_EQUAL( d[0].text, "-\n-" );
}

BOOST_AUTO_TEST_CASE( spans )
{
	BOOST_CHECK_EQUAL( Spans( "plain text" ), "t:plain text " );
	BOOST_CHECK_EQUAL( Spans( "a **b** *c* `d` e" ), "t:a  b:b t:  i:c t:  c:d t: e " );
	BOOST_CHECK_EQUAL( Spans( "[site](https://jka.vision/a) x" ), "l:site|https://jka.vision/a t: x " );
	BOOST_CHECK_EQUAL( Spans( "see https://jka.vision/download.html." ), "t:see  l:https://jka.vision/download.html|https://jka.vision/download.html t:. " );
	// marks that close nothing are plain text, as in tools/changelog.py
	BOOST_CHECK_EQUAL( Spans( "**a*" ), "t:* i:a " );
	BOOST_CHECK_EQUAL( Spans( "a * b" ), "t:a  t:* b " );
	BOOST_CHECK_EQUAL( Spans( "`open" ), "t:`open " );
	BOOST_CHECK_EQUAL( Spans( "[no](ftp://x)" ), "t:[no](ftp://x) " );
	BOOST_CHECK_EQUAL( Spans( "[a](https://)" ), "t:[a]( l:https://|https:// t:) " );
	BOOST_CHECK_EQUAL( Spans( "``" ), "t:` t:` " );
	BOOST_CHECK_EQUAL( Spans( "" ), "" );
}

BOOST_AUTO_TEST_CASE( versions )
{
	int a[4], b[4];

	BOOST_REQUIRE( MD_ParseVersion( "0.2.9-dev.3", a ) );
	BOOST_REQUIRE( MD_ParseVersion( "0.2.9", b ) );
	BOOST_CHECK_EQUAL( MD_CompareVersion( a, b ), -1 );
	BOOST_REQUIRE( MD_ParseVersion( "0.2.10-dev.1", a ) );
	BOOST_CHECK_EQUAL( MD_CompareVersion( a, b ), 1 );
	BOOST_REQUIRE( MD_ParseVersion( "0.2.9-dev.10", a ) );
	BOOST_REQUIRE( MD_ParseVersion( "0.2.9-dev.9", b ) );
	BOOST_CHECK_EQUAL( MD_CompareVersion( a, b ), 1 );
	BOOST_CHECK_EQUAL( MD_CompareVersion( b, b ), 0 );
	BOOST_CHECK( !MD_ParseVersion( "v0.2.9", a ) );
	BOOST_CHECK( !MD_ParseVersion( "0.2", a ) );
	BOOST_CHECK( !MD_ParseVersion( "0.2.9-rc1", a ) );
	BOOST_CHECK( !MD_ParseVersion( "0.2.9-dev.", a ) );
	BOOST_CHECK( !MD_ParseVersion( "", a ) );
	BOOST_CHECK( MD_IsDev( "0.2.9-dev.1" ) );
	BOOST_CHECK( !MD_IsDev( "0.2.9" ) );
}

BOOST_AUTO_TEST_SUITE_END()
