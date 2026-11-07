/*
===========================================================================
Copyright (C) 2026, VisionJK contributors

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

#pragma once

// Empty on purpose. Steam Audio for the browser is built without libmysofa (IPL_DISABLE_SOFA, see
// cmake/Modules/SteamAudioWasm.cmake): the engine only uses the HRTF data that is built into the library and
// reads no SOFA file. Valve's pch.h includes <mysofa.h> all the same, and finds this.
