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

// policy_spec.h -- written by tools/trainer/rl/gen_policy_header.py, do not edit. The layout of the
// observation and the actions of the learned policy (g_policy.c), from tools/trainer/rl/spec.py.

#pragma once

#define POLICY_OBS			111
#define POLICY_HEADS		7
#define POLICY_LOGITS		36
#define POLICY_PROBES		8
#define POLICY_WALL_RANGE	256.0f
#define POLICY_DROP_OUT		96.0f
#define POLICY_DROP_RANGE	512.0f

// where each part of the observation starts
#define PO_FLAT            0
#define PO_DZ              1
#define PO_VEL             2	// 3
#define PO_OVEL            5	// 3
#define PO_GROUND          8
#define PO_OGROUND         9
#define PO_FLOOR           10
#define PO_OFLOOR          11
#define PO_YAWERR          12	// 2
#define PO_PITCH           14
#define PO_PITCHERR        15
#define PO_OFACING         16	// 2
#define PO_OPITCH          18
#define PO_HEALTH          19
#define PO_OHEALTH         20
#define PO_FORCE           21
#define PO_WEAPONTIME      22
#define PO_LEGSTIMER       23
#define PO_TORSOTIMER      24
#define PO_JUMPHELD        25
#define PO_OJUMPHELD       26
#define PO_KICKFLIP        27
#define PO_HOOKOUT         28
#define PO_HOOKED          29
#define PO_HOOKAT          30	// 3
#define PO_OHOOKOUT        33
#define PO_OHOOKED         34
#define PO_OHOOKAT         35	// 3
#define PO_HANDEXT         38
#define PO_OHANDEXT        39
#define PO_STYLE           40
#define PO_OSTYLE          41
#define PO_HOLSTERED       42
#define PO_OHOLSTERED      43
#define PO_OATTACK         44
#define PO_OHOOKBUTTON     45
#define PO_OMOVES          46	// 3
#define PO_LEGS            49	// 6
#define PO_OLEGS           55	// 6
#define PO_OSABER          61	// 6
#define PO_SABER           67	// 6
#define PO_PMOVE           73	// 9
#define PO_PJUMP           82	// 4
#define PO_PHOOK           86	// 4
#define PO_PATTACK         90
#define PO_PSABER          91
#define PO_AIM             92	// 2
#define PO_PROBED          94
#define PO_WALL            95	// 8
#define PO_DROP            103	// 8

static const int policyHeadSize[POLICY_HEADS] = { 9, 4, 4, 2, 9, 6, 2 };
