#!/usr/bin/env python3
"""Writes codemp/game/trainer/clone_tables.h: the class of every animation and saber move (trainerlib.legs_class,
trainerlib.saber_phase) and the bins of the aim, so the clone in the game sees the player as the training here did.
Run again when anims.h or the saber moves of bg_public.h change.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trainerlib as T  # noqa: E402

HEAD = '''/*
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

// clone_tables.h -- written by tools/trainer/gen_tables.py, do not edit. The classes of the animations and saber
// moves and the aim bins the clone (g_clone.c) was trained with.

#pragma once
'''


def array(name, ctype, values, per_line=32):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append('\t' + ', '.join(str(v) for v in values[i:i + per_line]) + ',')
    return 'static const %s %s[%d] = {\n%s\n};\n' % (ctype, name, len(values), '\n'.join(lines))


def main():
    legs, saber = T.tables()
    out = [HEAD]
    out.append('#define CLONE_ANIMS\t\t\t%d\t// MAX_ANIMATIONS when this was written\n' % len(legs))
    out.append('#define CLONE_SABER_MOVES\t%d\t// LS_MOVE_MAX\n\n' % len(saber))
    out.append(array('cloneLegs', 'byte', legs))
    out.append('\n' + array('cloneSaber', 'byte', saber))
    out.append('\n' + array('cloneYaw', 'float', ['%.1ff' % v for v in T.YAW_CENTERS]))
    out.append('\n' + array('clonePitch', 'float', ['%.1ff' % v for v in T.PITCH_CENTERS]))
    path = os.path.join(T.GAME, 'trainer', 'clone_tables.h')
    with open(path, 'w', newline='\n') as f:
        f.write(''.join(out))
    print('wrote %s: %d animations, %d saber moves' % (path, len(legs), len(saber)))


if __name__ == '__main__':
    main()
