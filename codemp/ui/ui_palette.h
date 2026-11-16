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

/*
ui_palette.h -- the colours of the look of this tree, without alpha, for the menus (ui), the HUD and windows of the
cgame and the windows the engine draws. A colour is written with the alpha it needs: { SKIN_RGB_ACCENT, 0.35f }.
The same values are in tools/menus/theme.py for the menu files.
*/

#pragma once

#define SKIN_RGB_ACCENT		0.43f, 0.83f, 1.0f		// the light blue: marks, edges, what is chosen
#define SKIN_RGB_GLASS		0.035f, 0.07f, 0.12f	// the dark inside of a panel
#define SKIN_RGB_TEXT		0.86f, 0.92f, 0.97f		// words
#define SKIN_RGB_DIM		0.60f, 0.69f, 0.78f		// words that matter less
