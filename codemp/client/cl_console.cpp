/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// console.c

#include "client.h"
#include "ui/ui_palette.h"
#include "cl_console_local.h"
#include "cl_cgameapi.h"
#include "qcommon/stringed/stringed_ingame.h"
#include "qcommon/game_version.h"
#include "qcommon/q_version.h"


int g_console_field_width = 78;

console_t	con;

cvar_t		*con_conspeed;
cvar_t		*con_notifytime;
cvar_t		*con_opacity; // background alpha multiplier
cvar_t		*con_autoclear;
cvar_t		*con_height;
cvar_t		*con_scale;
cvar_t		*con_timestamps;
cvar_t		*tay_consoleClock;

#define	DEFAULT_CONSOLE_WIDTH	78

#define CON_SCROLL_L_CHAR		'$'
#define CON_SCROLL_R_CHAR		'$'
#define CON_MIN_WIDTH			20


static const conChar_t CON_BLANK = { { ColorIndex(COLOR_WHITE), CON_BLANK_CHAR } };

vec4_t	console_color = {0.509f, 0.609f, 0.847f, 1.0f};

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f (void) {
	// closing a full screen console restarts the demo loop
	if ( cls.state == CA_DISCONNECTED && Key_GetCatcher( ) == KEYCATCH_CONSOLE ) {
		CL_StartDemoLoop();
		return;
	}

	if( con_autoclear->integer )
		Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;

	Con_ClearNotify ();
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_CONSOLE );
}

/*
===================
Con_ToggleMenu_f
===================
*/
void Con_ToggleMenu_f( void ) {
	CL_KeyEvent( A_ESCAPE, qtrue, Sys_Milliseconds() );
	CL_KeyEvent( A_ESCAPE, qfalse, Sys_Milliseconds() );
}

/*
================
Con_ChatByCgame

A cgame of this tree has a chat box with its own line to type in (cgame/chat/cg_chat.c) and says so by setting
v_chatHandler, which the engine clears whenever a cgame is loaded. The message mode commands open that box
then, not the line of the engine. mode: 0 to all, 1 to the team, 2 to the player `player`.
================
*/
static qboolean Con_ChatByCgame( int mode, int player ) {
	if ( !cls.cgameStarted || cls.state != CA_ACTIVE || !Cvar_VariableIntegerValue( "v_chatHandler" ) ) {
		return qfalse;
	}
	Cbuf_ExecuteText( EXEC_NOW, va( "v_chatOpen %i %i\n", mode, player ) );
	return qtrue;
}

/*
================
Con_MessageMode_f
================
*/
void Con_MessageMode_f (void) {	//yell
	if ( Con_ChatByCgame( 0, -1 ) ) {
		return;
	}
	chat_playerNum = -1;
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;

	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_MessageMode2_f
================
*/
void Con_MessageMode2_f (void) {	//team chat
	if ( Con_ChatByCgame( 1, -1 ) ) {
		return;
	}
	chat_playerNum = -1;
	chat_team = qtrue;
	Field_Clear( &chatField );
	chatField.widthInChars = 25;
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_MessageMode3_f
================
*/
void Con_MessageMode3_f (void)
{		//target chat
	if (!cls.cgameStarted)
	{
		assert(!"null cgvm");
		return;
	}

	chat_playerNum = CGVM_CrosshairPlayer();
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	if ( Con_ChatByCgame( 2, chat_playerNum ) ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_MessageMode4_f
================
*/
void Con_MessageMode4_f (void)
{	//attacker
	if (!cls.cgameStarted)
	{
		assert(!"null cgvm");
		return;
	}

	chat_playerNum = CGVM_LastAttacker();
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	if ( Con_ChatByCgame( 2, chat_playerNum ) ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_MESSAGE );
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f (void) {
	int		i;

	for ( i = 0 ; i < CON_TEXTSIZE ; i++ ) {
		con.text[i] = CON_BLANK;
	}

	Con_ClearSelection();
	Con_Bottom();		// go to end
}


// The character of a cell as text for a file, returns its bytes (out has room for 4). Where text is UTF-8 a cell
// holds a code point, else a byte of the code page of the language, which goes out as it is.
static int Con_CellText( const conChar_t *cell, char *out ) {
	if ( CL_UTF8() ) {
		return Q_UTF8_Encode( cell->f.character, out );
	}
	out[0] = (char)cell->f.character;
	return 1;
}

/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
void Con_Dump_f (void)
{
	char			filename[MAX_QPATH];
	qboolean		empty;
	int				l, i, j;
	int				line;
	int				lineLen;
	fileHandle_t	f;
	// a cell can be a character of four bytes of UTF-8, and the end of the line has two on Windows
	static char		buffer[( CON_TIMESTAMP_LEN + MAXPRINTMSG ) * 4 + 2];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("%s\n", SE_GetString("CON_TEXT_DUMP_USAGE"));
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".txt" );

	if(!COM_CompareExtension(filename, ".txt"))
	{
		Com_Printf( "Con_Dump_f: Only the \".txt\" extension is supported by this command!\n" );
		return;
	}

	f = FS_FOpenFileWrite( filename );
	if (!f)
	{
		Com_PrintError( "couldn't open %s.\n", filename);
		return;
	}

	Com_Printf ("Dumped console text to %s.\n", filename );

	// skip empty lines
	for (l = 1, empty = qtrue ; l < con.totallines && empty ; l++)
	{
		line = ((con.current + l) % con.totallines) * con.rowwidth;

		for (j = CON_TIMESTAMP_LEN ; j < con.rowwidth - 1 ; j++)
			if (con.text[line + j].f.character != CON_BLANK_CHAR)
				empty = qfalse;
	}

	for ( ; l < con.totallines ; l++)
	{
		lineLen = 0;
		i = 0;

		// Print timestamp
		if (con_timestamps->integer) {
			line = ((con.current + l) % con.totallines) * con.rowwidth;

			for (i = 0; i < CON_TIMESTAMP_LEN; i++)
				buffer[i] = (char)con.text[line + i].f.character;

			lineLen = CON_TIMESTAMP_LEN;
		}

		// Concatenate wrapped lines
		for ( ; l < con.totallines ; l++)
		{
			line = ((con.current + l) % con.totallines) * con.rowwidth;

			for (j = CON_TIMESTAMP_LEN; j < con.rowwidth - 1 && i < (int)sizeof(buffer) - 6; j++) {
				const int bytes = Con_CellText( &con.text[line + j], buffer + i );

				i += bytes;
				if (con.text[line + j].f.character != CON_BLANK_CHAR)
					lineLen = i;
			}

			if (i >= (int)sizeof(buffer) - 6)
				break;

			if (con.text[line + j].compare != CON_WRAP.compare)
				break;
		}

#ifdef WIN32 // I really don't like this inconsistency, but OpenJK has been doing this since April 2013
		buffer[lineLen] = '\r';
		buffer[lineLen+1] = '\n';
		FS_Write(buffer, lineLen + 2, f);
#else
		buffer[lineLen] = '\n';
		FS_Write(buffer, lineLen + 1, f);
#endif
	}

	FS_FCloseFile( f );
}


/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	int		i;

	for ( i = 0 ; i < NUM_CON_TIMES ; i++ ) {
		con.times[i] = 0;
	}
}

/*
================
Con_Initialize

Initialize console for the first time.
================
*/
void Con_Initialize(void)
{
	int	i;

	VectorCopy4(colorWhite, con.color);
	con.charWidth = SMALLCHAR_WIDTH;
	con.charHeight = SMALLCHAR_HEIGHT;
	con.linewidth = DEFAULT_CONSOLE_WIDTH;
	con.rowwidth = CON_TIMESTAMP_LEN + con.linewidth + 1;
	con.totallines = CON_TEXTSIZE / con.rowwidth;
	con.current = con.totallines - 1;
	con.display = con.current;
	con.xadjust = 1.0f;
	con.yadjust = 1.0f;
	for(i=0; i<CON_TEXTSIZE; i++)
	{
		con.text[i] = CON_BLANK;
	}

	con.initialized = qtrue;
}



/*
================
Con_Resize

Reformat the buffer for new row width
================
*/
static void Con_Resize(int rowwidth)
{
	static conChar_t tbuf[CON_TEXTSIZE];
	int		i, j;
	int		oldrowwidth;
	int		oldtotallines;

	oldrowwidth = con.rowwidth;
	oldtotallines = con.totallines;

	con.rowwidth = rowwidth;
	con.totallines = CON_TEXTSIZE / rowwidth;

	Com_Memcpy (tbuf, con.text, sizeof(tbuf));
	for(i=0; i<CON_TEXTSIZE; i++)
		con.text[i] = CON_BLANK;

	int oi = 0;
	int ni = 0;

	while (oi < oldtotallines)
		{
			conChar_t	line[MAXPRINTMSG];
			conChar_t	timestamp[CON_TIMESTAMP_LEN];
			int		lineLen = 0;
			int		oldline = ((con.current + oi) % oldtotallines) * oldrowwidth;
			int		newline = (ni % con.totallines) * con.rowwidth;

			// Store timestamp
			for (i = 0; i < CON_TIMESTAMP_LEN; i++)
				timestamp[i] = tbuf[oldline + i];

			// Store whole line concatenating on CON_WRAP
			for (i = 0; oi < oldtotallines; oi++)
				{
					oldline = ((con.current + oi) % oldtotallines) * oldrowwidth;

					for (j = CON_TIMESTAMP_LEN; j < oldrowwidth - 1 && i < (int)ARRAY_LEN(line); j++, i++) {
						line[i] = tbuf[oldline + j];

						if (line[i].f.character != CON_BLANK_CHAR)
							lineLen = i + 1;
					}

					if (i == ARRAY_LEN(line))
						break;

					if (tbuf[oldline + j].compare != CON_WRAP.compare)
						break;
				}

			oi++;

			// Print stored line to a new text buffer
			for (i = 0; ; ni++) {
				newline = (ni % con.totallines) * con.rowwidth;

				// Print timestamp at the begining of each line
				for (j = 0; j < CON_TIMESTAMP_LEN; j++)
					con.text[newline + j] = timestamp[j];

				for (j = CON_TIMESTAMP_LEN; j < con.rowwidth - 1 && i < lineLen; j++, i++)
					con.text[newline + j] = line[i];

				if (i == lineLen) {
					// Erase remaining chars in case newline wrapped
					for (; j < con.rowwidth - 1; j++)
						con.text[newline + j] = CON_BLANK;

					ni++;
					break;
				}

				con.text[newline + j] = CON_WRAP;
			}
		}

	con.current = ni;

	// Erase con.current line for next CL_ConsolePrint
	int newline = (con.current % con.totallines) * con.rowwidth;
	for (j = 0; j < con.rowwidth; j++)
		con.text[newline + j] = CON_BLANK;

	Con_ClearNotify ();
	Con_ClearSelection();

	con.display = con.current;
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int		charWidth, rowwidth, width;
	float	scale;

	assert(SMALLCHAR_HEIGHT >= SMALLCHAR_WIDTH);

	scale = ((con_scale->value > 0.0f) ? con_scale->value : 1.0f);
	charWidth = scale * SMALLCHAR_WIDTH;

	if (charWidth < 1) {
		charWidth = 1;
		scale = (float)charWidth / SMALLCHAR_WIDTH;
	}

	width = (cls.glconfig.vidWidth / charWidth) - 2;

	if (width < 20) {
		width = 20;
		charWidth = cls.glconfig.vidWidth / 22;
		scale = (float)charWidth / SMALLCHAR_WIDTH;
	}

	if (charWidth < 1) {
		Com_Error(ERR_FATAL, "Con_CheckResize: Window too small to draw a console");
	}

	rowwidth = width + 1 + (con_timestamps->integer ? 0 : CON_TIMESTAMP_LEN);

	con.charWidth = charWidth;
	con.charHeight = scale * SMALLCHAR_HEIGHT;
	con.linewidth = width;
	con.xadjust = ( SCREEN_WIDTH + 2.0f * cls.bias2D ) / cls.glconfig.vidWidth;
	con.yadjust = ( SCREEN_HEIGHT + 2.0f * cls.bias2DY ) / cls.glconfig.vidHeight;
	g_consoleField.widthInChars = width - 1; // Command prompt

	if (con.rowwidth != rowwidth)
	{
		Con_Resize(rowwidth);
	}
}


/*
==================
Cmd_CompleteTxtName
==================
*/
void Cmd_CompleteTxtName( char *args, int argNum ) {
	if ( argNum == 2 )
		Field_CompleteFilename( "", "txt", qfalse, qtrue );
}

/*
================
Con_Init
================
*/
void Con_Init (void) {
	int		i;

	con_notifytime = Cvar_Get ("con_notifytime", "3", 0, "How many seconds notify messages should be shown before they fade away");
	con_conspeed = Cvar_Get ("scr_conspeed", "3", 0, "Console open/close speed");
	Cvar_CheckRange (con_conspeed, 1.0f, 100.0f, qfalse);

	con_opacity = Cvar_Get ("con_opacity", "1.0", CVAR_ARCHIVE_ND, "Opacity of console background");
	con_autoclear = Cvar_Get ("con_autoclear", "1", CVAR_ARCHIVE_ND, "Automatically clear console input on close");
	con_height = Cvar_Get ("con_height", "0.5", CVAR_ARCHIVE_ND);

	con_scale = Cvar_Get ("con_scale", "1", CVAR_ARCHIVE_ND, "Scale console font");
	con_timestamps = Cvar_Get ("con_timestamps", "0", CVAR_ARCHIVE_ND, "Display timestamps infront of console lines");
	tay_consoleClock = Cvar_Get ("tay_consoleClock", "0", CVAR_ARCHIVE_ND, "A clock left of the version in the console: 0 none, 1 the time, 2 the date and the time");

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;
	for ( i = 0 ; i < COMMAND_HISTORY ; i++ ) {
		Field_Clear( &historyEditLines[i] );
		historyEditLines[i].widthInChars = g_console_field_width;
	}

	Cmd_AddCommand( "toggleconsole", Con_ToggleConsole_f, "Show/hide console" );
	Cmd_AddCommand( "togglemenu", Con_ToggleMenu_f, "Show/hide the menu" );
	Cmd_AddCommand( "messagemode", Con_MessageMode_f, "Global Chat" );
	Cmd_AddCommand( "messagemode2", Con_MessageMode2_f, "Team Chat" );
	Cmd_AddCommand( "messagemode3", Con_MessageMode3_f, "Private Chat with Target Player" );
	Cmd_AddCommand( "messagemode4", Con_MessageMode4_f, "Private Chat with Last Attacker" );
	Cmd_AddCommand( "clear", Con_Clear_f, "Clear console text" );
	Cmd_AddCommand( "condump", Con_Dump_f, "Dump console text to file" );
	Cmd_SetCommandCompletionFunc( "condump", Cmd_CompleteTxtName );

	//Initialize values on first print
	con.initialized = qfalse;
}

/*
================
Con_Shutdown
================
*/
void Con_Shutdown(void)
{
	Cmd_RemoveCommand("toggleconsole");
	Cmd_RemoveCommand("togglemenu");
	Cmd_RemoveCommand("messagemode");
	Cmd_RemoveCommand("messagemode2");
	Cmd_RemoveCommand("messagemode3");
	Cmd_RemoveCommand("messagemode4");
	Cmd_RemoveCommand("clear");
	Cmd_RemoveCommand("condump");
}

/*
===============
Con_Linefeed
===============
*/
static void Con_Linefeed (qboolean skipnotify)
{
	int		i;
	int		line = (con.current % con.totallines) * con.rowwidth;

	// print timestamp on the PREVIOUS line
	{
		time_t t = time( NULL );
		struct tm *tms = localtime( &t );
		char	timestamp[CON_TIMESTAMP_LEN + 1];
		const unsigned char color = ColorIndex(COLOR_GREY);

		Com_sprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d] ",
			tms->tm_hour, tms->tm_min, tms->tm_sec);

		for ( i = 0; i < CON_TIMESTAMP_LEN; i++ ) {
			con.text[line + i].f.color = color;
			con.text[line + i].f.character = (unsigned char)timestamp[i];
		}
	}

	// mark time for transparent overlay
	if (con.current >= 0)
		con.times[con.current % NUM_CON_TIMES] = skipnotify ? 0 : cls.realtime;

	con.x = 0;

	if (con.display == con.current)
		con.display++;
	con.current++;

	line = (con.current % con.totallines) * con.rowwidth;

	for ( i = 0; i < con.rowwidth; i++ )
		con.text[line + i] = CON_BLANK;
}

/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void CL_ConsolePrint( const char *txt) {
	int		y;
	unsigned int	c;
	unsigned char	color;
	qboolean skipnotify = qfalse;		// NERVE - SMF
	int prev;							// NERVE - SMF

	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if ( !Q_strncmp( txt, "[skipnotify]", 12 ) ) {
		skipnotify = qtrue;
		txt += 12;
	}
	if ( txt[0] == '*' ) {
		skipnotify = qtrue;
		txt += 1;
	}

	// for some demos we don't want to ever show anything on the console
	if ( cl_noprint && cl_noprint->integer ) {
		return;
	}
	Con_FeedAdd( txt, skipnotify );

	if (!con.initialized) {
		Con_Initialize();
	}

	color = ColorIndex(COLOR_WHITE);

	while ( (c = (unsigned char) *txt) != 0 ) {
		if ( Q_IsColorString( (unsigned char*) txt ) ) {
			color = ColorIndex( *(txt+1) );
			txt += 2;
			continue;
		}

		if ( CL_UTF8() ) {
			// a cell holds the code point of a character, a byte that is no UTF-8 is read as Windows-1252
			txt += Q_UTF8_Decode( txt, &c );
		} else
		txt++;

		switch (c)
		{
		case '\n':
			Con_Linefeed (skipnotify);
			break;
		case '\r':
			con.x = 0;
			break;
		default:	// display character and advance
			y = con.current % con.totallines;

			if (con.x == con.rowwidth - CON_TIMESTAMP_LEN - 1) {
				con.text[y * con.rowwidth + CON_TIMESTAMP_LEN + con.x] = CON_WRAP;
				Con_Linefeed( skipnotify );
				y = con.current % con.totallines;
			}

			con.text[y * con.rowwidth + CON_TIMESTAMP_LEN + con.x].f.color = color;
			con.text[y * con.rowwidth + CON_TIMESTAMP_LEN + con.x].f.character = c;
			con.x++;
			break;
		}
	}


	// mark time for transparent overlay

	if (con.current >= 0 )
	{
		// NERVE - SMF
		if ( skipnotify ) {
			prev = con.current % NUM_CON_TIMES - 1;
			if ( prev < 0 )
				prev = NUM_CON_TIMES - 1;
			con.times[prev] = 0;
		}
		else
		// -NERVE - SMF
			con.times[con.current % NUM_CON_TIMES] = cls.realtime;
	}
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
void Con_DrawInput (void) {
	int		y;

	if ( cls.state != CA_DISCONNECTED && !(Key_GetCatcher( ) & KEYCATCH_CONSOLE ) ) {
		return;
	}

	y = con.vislines - ( con.charHeight * (re->Language_IsAsian() ? 1.5 : 2) );

	re->SetColor( con.color );

	Field_Draw( &g_consoleField, 2 * con.charWidth, y, qtrue, qtrue );

	SCR_DrawConChar( con.charWidth, y, CONSOLE_PROMPT_CHAR );

	re->SetColor( g_color_table[ColorIndex(COLOR_GREY)] );

	if ( g_consoleField.scroll > 0 )
		SCR_DrawConChar( 0, y, CON_SCROLL_L_CHAR );

	int len = Q_PrintStrlen( g_consoleField.buffer );
	int pos = Q_PrintStrLenTo( g_consoleField.buffer, g_consoleField.scroll, NULL );
	if ( pos + g_consoleField.widthInChars < len )
		SCR_DrawConChar( cls.glconfig.vidWidth - con.charWidth, y, CON_SCROLL_R_CHAR );
}




/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify (void)
{
	int		x, v;
	int		lineLimit = con.linewidth;
	conChar_t		*text;
	int		i;
	int		time;
	int		skip;
	int		currentColor;
	const char* chattext;

	currentColor = 7;
	re->SetColor( g_color_table[currentColor] );

	int iFontIndex = cls.consoleFont;
	float fFontScale = 1.0f;
	int iPixelHeightToAdvance = 0;
	if (re->Language_IsAsian())
	{
		fFontScale = con.charWidth * 10.0f /
			re->Font_StrLenPixels("aaaaaaaaaa", iFontIndex, 1.0f);
		fFontScale *= con.yadjust;
		iPixelHeightToAdvance = 2+(1.3/con.yadjust) * re->Font_HeightPixels(iFontIndex, fFontScale);
	}

	v = 0;
	for (i= con.current-NUM_CON_TIMES+1 ; i<=con.current ; i++)
	{
		if (i < 0)
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if (time > con_notifytime->value*1000)
			continue;
		text = con.text + (i % con.totallines)*con.rowwidth;
		if (con_timestamps->integer == 0 || con_timestamps->integer == 2) {
			// don't show timestamps in the notify lines
			text += CON_TIMESTAMP_LEN;
			lineLimit -= CON_TIMESTAMP_LEN;
		}

		if (cl.snap.ps.pm_type != PM_INTERMISSION && Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
			continue;
		}
		// VisionJK: the cgame draws them in its feed
		if ( Con_FeedByCgame() ) {
			continue;
		}


		if (!cl_conXOffset)
		{
			cl_conXOffset = Cvar_Get ("cl_conXOffset", "0", 0);
		}

		// asian language needs to use the new font system to print glyphs...
		//
		// (ignore colours since we're going to print the whole thing as one string)
		//
		if (re->Language_IsAsian())
		{
			// concat the text to be printed...
			//
			char sTemp[4096];	// ott
			sTemp[0] = '\0';
			for (x = 0 ; x < lineLimit ; x++)
			{
				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					strcat(sTemp,va("^%i", currentColor ));
				}
				strcat(sTemp,va("%c",(char)text[x].f.character));
			}
			//
			// and print...
			//
			re->Font_DrawString(cl_conXOffset->integer + con.xadjust * (con.xadjust + con.charWidth) - cls.bias2D, con.yadjust * v - cls.bias2DY, sTemp,
				g_color_table[currentColor], iFontIndex, -1, fFontScale);

			v +=  iPixelHeightToAdvance;
		}
		else
		{
			for (x = 0 ; x < lineLimit ; x++) {
				if ( text[x].f.character == ' ' ) {
					continue;
				}
				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					re->SetColor( g_color_table[currentColor] );
				}
				if (!cl_conXOffset)
				{
					cl_conXOffset = Cvar_Get ("cl_conXOffset", "0", 0);
				}
				SCR_DrawConChar( (int)(cl_conXOffset->integer + (x+1)*con.charWidth), v, text[x].f.character );
			}

			v += con.charHeight;
		}
	}

	re->SetColor( NULL );

	if (Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
		return;
	}

	// draw the chat line
	if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE )
	{
		if (chat_team)
		{
			chattext = SE_GetString("MP_SVGAME", "SAY_TEAM");
			SCR_DrawBigString (8 - (int)cls.bias2D, v, chattext, 1.0f, qfalse );
			skip = strlen(chattext)+1;
		}
		else
		{
			chattext = SE_GetString("MP_SVGAME", "SAY");
			SCR_DrawBigString (8 - (int)cls.bias2D, v, chattext, 1.0f, qfalse );
			skip = strlen(chattext)+1;
		}

		Field_BigDraw( &chatField, skip * BIGCHAR_WIDTH - (int)cls.bias2D, v, qtrue, qtrue );

		v += BIGCHAR_HEIGHT;
	}

}

/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
void Con_DrawSolidConsole( float frac ) {
	int				i, x, y;
	int				rows;
	conChar_t		*text;
	int				row;
	int				lines;
//	qhandle_t		conShader;
	int				currentColor;
	qboolean		haveSelection;
	int				cursorX, cursorY;
	static vec4_t	selColor = { 0.25f, 0.45f, 0.85f, 0.65f };

	lines = (int) (cls.glconfig.vidHeight * frac);
	if (lines <= 0)
		return;

	if (lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;

	// draw the background
	const vec4_t	themeLine = { SKIN_RGB_ACCENT, 0.9f };
	qboolean		themed = qfalse;

	// in units of the grid, which begins at -bias2DY on a narrow window: the console comes down over the window
	y = (int) ( frac * ( SCREEN_HEIGHT + 2.0f * cls.bias2DY ) - cls.bias2DY - 2 );
	if ( y < 1 - (int)cls.bias2DY ) {
		y = 0;
	}
	else {
		// The look of the menus of this tree: dark glass in place of the picture of the game, unless the player
		// asked for the menus of before (v_classic 1)
		if ( !Cvar_VariableIntegerValue( "v_classic" ) )
		{
			// frosted: what is behind the console blurred first (v_backdropBlur of the renderer), then thinner glass
			const qboolean frosted = (qboolean)( frac < 1.0f && re->ext.DrawBackdrop
				&& re->ext.DrawBackdrop( -cls.bias2D, -cls.bias2DY, SCREEN_WIDTH + 2.0f * cls.bias2D, (float) y + cls.bias2DY, 0.0f ) );
			const vec4_t glass = { 0.02f, 0.04f, 0.08f, ( frosted ? 0.72f : 0.94f ) * ( ( frac < 1.0f ) ? Com_Clamp( 0.0f, 1.0f, con_opacity->value ) : 1.0f ) };

			SCR_FillRect( -cls.bias2D, -cls.bias2DY, SCREEN_WIDTH + 2.0f * cls.bias2D, (float) y + cls.bias2DY, glass );
			themed = qtrue;
		}
		else
		{
		// draw the background at full opacity only if fullscreen
		if (frac < 1.0f)
		{
			vec4_t con_color;
			MAKERGBA(con_color, 1.0f, 1.0f, 1.0f, Com_Clamp(0.0f, 1.0f, con_opacity->value));
			re->SetColor(con_color);
		}
		else
		{
			re->SetColor(NULL);
		}
		SCR_DrawPic( -cls.bias2D, -cls.bias2DY, SCREEN_WIDTH + 2.0f * cls.bias2D, (float) y + cls.bias2DY, cls.consoleShader );
		}
	}

	// draw the bottom bar and version number

	re->SetColor( themed ? themeLine : console_color );
	re->DrawStretchPic( -cls.bias2D, y, SCREEN_WIDTH + 2.0f * cls.bias2D, 2, 0, 0, 0, 0, cls.whiteShader );

	i = strlen( JK_VERSION );

	for (x=0 ; x<i ; x++) {
		SCR_DrawConChar( cls.glconfig.vidWidth - ( i - x + 1 ) * con.charWidth,
			(lines-(con.charHeight+con.charHeight/2)), (unsigned char)JK_VERSION[x] );
	}

	// the clock of the machine left of the version (TaystJK's con_datetime)
	if ( tay_consoleClock->integer ) {
		qtime_t	now;
		char	clock[32];
		int		len;

		Com_RealTime( &now );
		if ( tay_consoleClock->integer == 2 ) {
			Com_sprintf( clock, sizeof( clock ), "%04i-%02i-%02i %02i:%02i:%02i", now.tm_year + 1900, now.tm_mon + 1,
				now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec );
		} else {
			Com_sprintf( clock, sizeof( clock ), "%02i:%02i:%02i", now.tm_hour, now.tm_min, now.tm_sec );
		}
		len = strlen( clock );
		for ( x = 0; x < len; x++ ) {
			SCR_DrawConChar( cls.glconfig.vidWidth - ( i + 3 + len - x ) * con.charWidth,
				(lines-(con.charHeight+con.charHeight/2)), (unsigned char)clock[x] );
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput ();

	// draw the text
	con.vislines = lines;
	rows = (lines-con.charHeight)/con.charHeight;		// rows of text to draw

	y = lines - (con.charHeight*3);

	// draw from the bottom up
	if (con.display != con.current)
	{
	// draw arrows to show the buffer is backscrolled
		re->SetColor( console_color );
		for (x=0 ; x<con.linewidth ; x+=4)
			SCR_DrawConChar( (x+1)*con.charWidth, y, '^' );
		y -= con.charHeight;
		rows--;
	}

	row = con.display;

	if ( con.x == 0 ) {
		row--;
	}

	// where the text is, for the mouse, then what the mouse does this frame
	haveSelection = Con_SelectionBegin( y, row, rows );

	currentColor = 7;
	re->SetColor( g_color_table[currentColor] );

	int iFontIndex = cls.consoleFont;
	float fFontScale = 1.0f;
	int iPixelHeightToAdvance = con.charHeight;
	if (re->Language_IsAsian())
	{
		fFontScale = con.charWidth * 10.0f /
			re->Font_StrLenPixels("aaaaaaaaaa", iFontIndex, 1.0f);
		fFontScale *= con.yadjust;
		iPixelHeightToAdvance = 2+(1.3/con.yadjust) * re->Font_HeightPixels(iFontIndex, fFontScale);
	}

	for (i=0 ; i<rows ; i++, y -= iPixelHeightToAdvance, row--)
	{
		if (row < 0)
			break;
		if (con.current - row >= con.totallines) {
			// past scrollback wrap point
			continue;
		}

		text = con.text + (row % con.totallines)*con.rowwidth;
		if (!con_timestamps->integer)
			text += CON_TIMESTAMP_LEN;

		// asian language needs to use the new font system to print glyphs...
		//
		// (ignore colours since we're going to print the whole thing as one string)
		//
		if (re->Language_IsAsian())
		{
			// concat the text to be printed...
			//
			char sTemp[4096];	// ott
			sTemp[0] = '\0';
			for (x = 0 ; x < con.linewidth + 1 ; x++)
			{
				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					strcat(sTemp,va("^%i", currentColor ));
				}
				strcat(sTemp,va("%c",(char)text[x].f.character));
			}
			//
			// and print...
			//
			re->Font_DrawString(con.xadjust*(con.xadjust + con.charWidth) - cls.bias2D, con.yadjust * y - cls.bias2DY, sTemp, g_color_table[currentColor],
				iFontIndex, -1, fFontScale);
		}
		else
		{
			int from, to;

			// what is marked in this row, behind its text
			if ( haveSelection && Con_SelectionRow( row, &from, &to ) ) {
				const int shown = con_timestamps->integer ? CON_TIMESTAMP_LEN : 0;

				SCR_FillRect( ( from + shown + 1 ) * con.charWidth * con.xadjust - cls.bias2D, y * con.yadjust - cls.bias2DY,
					( to - from ) * con.charWidth * con.xadjust, con.charHeight * con.yadjust, selColor );
				re->SetColor( g_color_table[currentColor] );
			}

			for (x = 0; x < con.linewidth + 1 ; x++) {
				if ( text[x].f.character == ' ' ) {
					continue;
				}

				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					re->SetColor( g_color_table[currentColor] );
				}
				SCR_DrawConChar( (x+1)*con.charWidth, y, text[x].f.character );
			}
		}
	}

	re->SetColor( NULL );

	// the cursor of the console, where the system does not draw one
	if ( Con_SelectionCursor( &cursorX, &cursorY ) ) {
		re->DrawStretchPic( cursorX * con.xadjust - cls.bias2D, cursorY * con.yadjust - cls.bias2DY, 24, 24, 0, 0, 1, 1, cls.cursorShader );
	}
}



/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {
	// check for console width changes from a vid mode change
	Con_CheckResize ();

	// if disconnected, render console full screen
	if ( cls.state == CA_DISCONNECTED ) {
		if ( !( Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME)) ) {
			Con_DrawSolidConsole( 1.0 );
			return;
		}
	}

	if ( con.displayFrac ) {
		Con_DrawSolidConsole( con.displayFrac );
	} else {
		// draw notify lines
		if ( cls.state == CA_ACTIVE ) {
			Con_DrawNotify ();
		}
	}
}

//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole (void) {
	// decide on the destination height of the console
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
		con.finalFrac = con_height->value;
	else
		con.finalFrac = 0;				// none visible

	// scroll towards the destination height
	if (con.finalFrac < con.displayFrac)
	{
		con.displayFrac -= con_conspeed->value*(float)(cls.realFrametime*0.001);
		if (con.finalFrac > con.displayFrac)
			con.displayFrac = con.finalFrac;

	}
	else if (con.finalFrac > con.displayFrac)
	{
		con.displayFrac += con_conspeed->value*(float)(cls.realFrametime*0.001);
		if (con.finalFrac < con.displayFrac)
			con.displayFrac = con.finalFrac;
	}

}


void Con_PageUp( void ) {
	con.display -= 2;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_PageDown( void ) {
	con.display += 2;
	if (con.display > con.current) {
		con.display = con.current;
	}
}

void Con_Top( void ) {
	con.display = con.totallines;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_Bottom( void ) {
	con.display = con.current;
}


void Con_Close( void ) {
	if ( !com_cl_running->integer ) {
		return;
	}
	Field_Clear( &g_consoleField );
	Con_ClearNotify ();
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CONSOLE );
	con.finalFrac = 0;				// none visible
	con.displayFrac = 0;
}
