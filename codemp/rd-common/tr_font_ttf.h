/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

This file is part of the VisionJK source code.

VisionJK is free software; you can redistribute it and/or modify it
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

#pragma once

// tr_font_ttf.cpp: glyphs from a TrueType font for the characters the fonts of the game do not have

void				R_FontTTF_Init( void );
void				R_FontTTF_Shutdown( void );
const glyphInfo_t	*R_FontTTF_Glyph( unsigned int codePoint, int pointSize, int *shader );
void				R_FontTTF_Flush( void );
void				R_FontTTF_Info( void );
bool				Font_TextIsUTF8( void );		// tr_font.cpp
