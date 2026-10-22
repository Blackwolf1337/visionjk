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

// trainer_params.h -- the values of the training bot (g_trainer.c) that a file can change (vs_trainerParams), and
// their defaults. Included several times with TRAINER_PARAM( name, default ) defined: for the struct, the defaults
// and the names the file uses. Distances in units, times in msec unless the name says SEC. No include guard.

TRAINER_PARAM( HOOK_MIN,      250.0f   )	// further away a long pull brings it in
TRAINER_PARAM( HOOK_MAX,      1200.0f  )
TRAINER_PARAM( HOOK_NEAR,     200.0f   )	// a long pull is let go here
TRAINER_PARAM( HOOK_FUEL,     20.0f    )	// JP_HOOK_MIN_FUEL is 10, a bit more so the rope does not run dry halfway
TRAINER_PARAM( HOOK_TIME,     1600.0f  )	// msec on a long pull at most
TRAINER_PARAM( YANK_FROM,     220.0f   )	// in the air this close: the short pull (the player: up to 465, mostly 160)
TRAINER_PARAM( YANK_CHAIN,    230.0f   )	// during the flip back after a kick from this close
TRAINER_PARAM( YANK_TO,       105.0f   )	// let go here
TRAINER_PARAM( YANK_MSEC,     60.0f    )	// or after this long on the rope
TRAINER_PARAM( YANK_MISS,     350.0f   )	// the hook hit nothing
TRAINER_PARAM( YANK_ABOVE,    10.0f    )	// only this far above the player: the pull goes down in front of him
TRAINER_PARAM( YANK_RISE,     250.0f   )	// and not rising fast any more
TRAINER_PARAM( YANK_LEAD,     0.12f    )	// sec: let go where the speed of the pull would bring it in this time
TRAINER_PARAM( CHAIN_WAIT,    100.0f   )	// msec after a kick before the hook pulls it in again
TRAINER_PARAM( HOOK_SHORT,    76.0f    )	// the hook goes into the floor this far in front of the player, at his feet
TRAINER_PARAM( HOOK_SIDE,     40.0f    )	// and up to this far to a side
TRAINER_PARAM( ESCAPE_LEN,    186.0f   )	// after a missed kick a hook into the floor this far off (the player: 106 to 318)
TRAINER_PARAM( ESCAPE_HOLD,   200.0f   )	// msec on it (the player: 150 to 200)
TRAINER_PARAM( ESCAPE_AIM,    10.0f    )	// degrees off at most when that hook goes out
TRAINER_PARAM( ESCAPE_WAIT,   250.0f   )	// msec it may take to turn there
TRAINER_PARAM( HOP_RANGE,     230.0f   )	// jumps here to hook from the air
TRAINER_PARAM( ATTACK_RANGE,  260.0f   )	// runs in for a kick or a swing from here
TRAINER_PARAM( TAKEOFF,       55.0f    )	// the kick jump from the ground, flat distance, and
TRAINER_PARAM( TAKEOFF_SPEED, 0.15f    )	// the speed times this further (the player: 75 to 106 at 250 to 466)
TRAINER_PARAM( TAKEOFF_MAX,   130.0f   )
TRAINER_PARAM( TOO_CLOSE,     35.0f    )	// nearer than this flat, or on top of him: a step back first
TRAINER_PARAM( LEVEL_DZ,      32.0f    )	// about the same height
TRAINER_PARAM( JUMP_HOLD,     100.0f   )	// msec the kick jump is held, the force jump keeps the speed up
TRAINER_PARAM( JUMP_GAP,      50.0f    )	// msec it is let go before the press that kicks (PMF_JUMP_HELD)
TRAINER_PARAM( KICK_TIME,     2500.0f  )
TRAINER_PARAM( SIDE_RANGE,    140.0f   )	// strafes in for a side kick from here
TRAINER_PARAM( SIDE_JUMP,     40.0f    )	// and jumps here: the trace of PM_CheckJump reaches 16 units past its box
TRAINER_PARAM( SIDE_LEVEL,    16.0f    )	// the player at about the same height
TRAINER_PARAM( SIDE_AIM,      25.0f    )	// degrees off 90 at most when it jumps
TRAINER_PARAM( SIDE_TIME,     2000.0f  )
TRAINER_PARAM( GRAB_RANGE,    70.0f    )	// a grab is started from here
TRAINER_PARAM( GRAB_REACH,    46.0f    )	// the hand reaches the player
TRAINER_PARAM( GRAB_TIME,     3000.0f  )
TRAINER_PARAM( SWING_RANGE,   90.0f    )
TRAINER_PARAM( SWING_REACH,   90.0f    )	// a red swing of the player reaches this far: on guard it keeps out of it
TRAINER_PARAM( SWING_TIME,    1500.0f  )
TRAINER_PARAM( RED_WAIT,      800.0f   )	// msec it waits for the red style before it swings in the style it has
TRAINER_PARAM( STRIKE_RANGE,  120.0f   )	// the attack button of the player counts as an attack this close
TRAINER_PARAM( RING_NEAR,     110.0f   )	// between attacks it keeps this far off and circles
TRAINER_PARAM( RING_FAR,      220.0f   )
TRAINER_PARAM( PUNISH_RANGE,  100.0f   )	// swings at a player who is open from here, without the pause
TRAINER_PARAM( KICK_BACK,     110.0f   )	// after stepping out of his hook or kick it kicks back from here
TRAINER_PARAM( MEET_RANGE,    135.0f   )	// a player coming through the air is met with a kick from this flat distance
TRAINER_PARAM( MEET_BELOW,    10.0f    )	// if he is not further below than this
TRAINER_PARAM( PRESS_SEC,     0.15f    )	// from the kick jump to the press that kicks (TRAINER_JUMP_HOLD + TRAINER_JUMP_GAP)
TRAINER_PARAM( REACH_NEAR,    28.0f    )	// at the press the player is this far off (flat): the kick trace hits him
TRAINER_PARAM( REACH_FAR,     70.0f    )	// (32 units past the box, the player kicked at 30 to 68)
TRAINER_PARAM( ARRIVE_SEC,    0.3f     )	// a player in the air who reaches this in this time is waited for, not dodged
TRAINER_PARAM( WAIT_MSEC,     1500.0f  )	// the approach TA_WAIT: in the ring for the player to come
TRAINER_PARAM( TRY_MSEC,      2500.0f  )	// an approach is scored on the damage of this time after it began
TRAINER_PARAM( LEARN_RATE,    0.25f    )	// weight of the newest score in the moving average
TRAINER_PARAM( EXPLORE,       15.0f    )	// bonus (in health) for approaches tried less
TRAINER_PARAM( CIRCLE_MIN,    700.0f   )	// msec before it may circle the other way
TRAINER_PARAM( CIRCLE_MAX,    1600.0f  )
TRAINER_PARAM( COUNTER_KICK,  220.0f   )	// a player jumping at it from here is a flip kick
TRAINER_PARAM( COUNTER_GRAB,  110.0f   )

// the start scores of the approaches, see Trainer_Choose
TRAINER_PARAM( PRIOR_HOOKKICK, 2.0f    )
TRAINER_PARAM( PRIOR_RUNKICK,  0.0f    )
TRAINER_PARAM( PRIOR_SIDEKICK, 0.0f    )
TRAINER_PARAM( PRIOR_SWING,    -2.0f   )
TRAINER_PARAM( PRIOR_WAIT,     0.0f    )
