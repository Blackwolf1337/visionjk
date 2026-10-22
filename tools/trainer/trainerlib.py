"""Shared parts of the tools of the training bot: the enums of the game, the movement log (vs_movelog) and the
features and actions of the clone. The features here and the ones of codemp/game/trainer/g_clone.c are the same;
the class tables both use are written from here into codemp/game/trainer/clone_tables.h (gen_tables.py).
"""
import csv
import math
import os
import re

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
GAME = os.path.join(ROOT, 'codemp', 'game')

BUTTON_ATTACK = 1
BUTTON_USE = 32
BUTTON_HOOK = 4096

# one server frame at sv_fps 20: the clone decides once in it, like every bot
FRAME_MSEC = 50


def _enum(path, first, last):
    """the names of an enum in order, from the line with first to the line with last"""
    names, on = [], False
    for line in open(path, encoding='latin-1'):
        m = re.match(r'\s*([A-Z][A-Z0-9_]+)\s*(=\s*0\s*)?,', line)
        if not on and m and m.group(1) == first:
            on = True
        if on:
            if m:
                names.append(m.group(1))
            elif re.match(r'\s*%s\b' % last, line):
                break
    return names


def anim_names():
    return _enum(os.path.join(GAME, 'anims.h'), 'FACE_TALK0', 'MAX_ANIMATIONS')


def saber_move_names():
    return _enum(os.path.join(GAME, 'bg_public.h'), 'LS_NONE', 'LS_MOVE_MAX')


# legs: what a player does with his body
LEGS_OTHER, LEGS_FLIP, LEGS_DOWN, LEGS_AIR, LEGS_RUN, LEGS_HOOK = range(6)


def legs_class(name):
    if name in ('BOTH_WALL_FLIP_BACK1', 'BOTH_WALL_FLIP_LEFT', 'BOTH_WALL_FLIP_RIGHT'):
        return LEGS_FLIP
    if name.startswith(('BOTH_KNOCKDOWN', 'BOTH_GETUP', 'BOTH_FORCE_GETUP')):
        return LEGS_DOWN
    if name == 'BOTH_RESISTPUSH':
        return LEGS_HOOK
    if name.startswith(('BOTH_JUMP', 'BOTH_INAIR', 'BOTH_FORCEJUMP', 'BOTH_FLIP_', 'BOTH_FORCEINAIR', 'BOTH_LAND')):
        return LEGS_AIR
    if name.startswith(('BOTH_RUN', 'BOTH_WALK', 'BOTH_SPRINT')):
        return LEGS_RUN
    return LEGS_OTHER


# saber: where in a swing a player is
SABER_IDLE, SABER_START, SABER_ATTACK, SABER_RETURN, SABER_DEFEND, SABER_TRANSITION = range(6)


def saber_phase(name):
    if name in ('LS_NONE', 'LS_READY', 'LS_DRAW', 'LS_PUTAWAY'):
        return SABER_IDLE
    if name.startswith('LS_S_'):
        return SABER_START
    if name.startswith('LS_R_'):
        return SABER_RETURN
    if name.startswith('LS_T1_'):
        return SABER_TRANSITION
    if re.match(r'LS_(B1|D1|V1|K1|H1|PARRY|REFLECT|BOUNCE)', name):
        return SABER_DEFEND
    return SABER_ATTACK


def tables():
    """the class of every animation and saber move, by number"""
    return [legs_class(n) for n in anim_names()], [saber_phase(n) for n in saber_move_names()]


def num(v):
    return float(v) if ('.' in v or 'e' in v) else int(v)


def read_log(path):
    """the rows of a movement log as dicts, None for a log without the columns the clone needs"""
    with open(path, encoding='latin-1') as f:
        f.readline()
        rows = [{k: num(v) for k, v in d.items()} for d in csv.DictReader(f)]
    if not rows or 'yawErr' not in rows[0] or 'ohook' not in rows[0]:
        return None
    return rows


def angle_sub(a, b):
    """a - b in -180..180"""
    d = (a - b) % 360.0
    return d - 360.0 if d > 180.0 else d


# the features, in the order of g_clone.c (CLONE_FEATURES there)
FEATURES = [
    'flat', 'dz', 'vAlong', 'vAcross', 'vz', 'oAlong', 'oAcross', 'ovz', 'ground', 'oground', 'hook', 'ohook',
    'oFacing', 'oSaber', 'oLegs', 'legs', 'health', 'ohealth', 'force', 'pMove', 'pJump', 'pHook', 'pAttack',
    'pYaw', 'yawErr', 'pitch',
]

# the actions: one decision per head
HEADS = ['move', 'jump', 'hook', 'attack', 'yaw', 'pitch']
JUMP_NONE, JUMP_PRESS, JUMP_HOLD, JUMP_CROUCH = range(4)
HOOK_NONE, HOOK_PRESS, HOOK_HOLD, HOOK_DROP = range(4)
# where the player is in the view after the decision (+ left), and how far it looks down
YAW_EDGES = [-120, -60, -25, -8, 8, 25, 60, 120]
YAW_CENTERS = [-150, -90, -42, -16, 0, 16, 42, 90, 150]
PITCH_EDGES = [-20, 5, 25, 40, 55]
PITCH_CENTERS = [-35, -7, 15, 32, 47, 65]
HEAD_CLASSES = {'move': 9, 'jump': 4, 'hook': 4, 'attack': 2, 'yaw': len(YAW_CENTERS), 'pitch': len(PITCH_CENTERS)}


def bin_of(edges, v):
    for i, e in enumerate(edges):
        if v < e:
            return i
    return len(edges)


def sign(v):
    return (v > 0) - (v < 0)


def features(r, legs, saber, prev):
    """the features of one row (the state the clone sees), prev the actions of the frame before"""
    flat = math.hypot(r['dx'], r['dy'])
    ux, uy = (r['dx'] / flat, r['dy'] / flat) if flat > 0.001 else (1.0, 0.0)
    to_self = math.degrees(math.atan2(-r['dy'], -r['dx']))
    hook = 0 if not r['hook'] else (2 if r['hooked'] else 1)
    return [
        min(flat, 2000.0), r['dz'],
        r['vx'] * ux + r['vy'] * uy, -r['vx'] * uy + r['vy'] * ux, r['vz'],
        r['ovx'] * ux + r['ovy'] * uy, -r['ovx'] * uy + r['ovy'] * ux, r['ovz'],
        r['ground'], r['oground'], hook, r['ohook'],
        abs(angle_sub(to_self, r['oyaw'])),
        saber[r['osaberMove']] if 0 <= r['osaberMove'] < len(saber) else SABER_IDLE,
        legs[r['olegs']] if 0 <= r['olegs'] < len(legs) else LEGS_OTHER,
        legs[r['legsAnim']] if 0 <= r['legsAnim'] < len(legs) else LEGS_OTHER,
        min(r['health'], 200), min(r['ohp'], 200), r['force'],
        prev['move'], prev['jump'], prev['hook'], prev['attack'], prev['yaw'],
        r['yawErr'], r['pitch'],
    ]


def actions(group, last_before):
    """what the player did in one server frame (its usercmds), last_before the last row of the frame before"""
    cnt = {}
    for r in group:
        k = (sign(r['fwd']), sign(r['right']))
        cnt[k] = cnt.get(k, 0) + 1
    fs, rs = max(cnt, key=cnt.get)
    move = (fs + 1) * 3 + (rs + 1)

    up = [r['up'] > 0 for r in group]
    before = last_before is not None and last_before['up'] > 0
    if sum(r['up'] < 0 for r in group) * 2 > len(group):
        jump = JUMP_CROUCH
    elif any(u and not p for u, p in zip(up, [before] + up[:-1])):
        jump = JUMP_PRESS
    elif up[-1]:
        jump = JUMP_HOLD
    else:
        jump = JUMP_NONE

    hb = [bool(r['buttons'] & BUTTON_HOOK) for r in group]
    hbefore = last_before is not None and bool(last_before['buttons'] & BUTTON_HOOK)
    if any((r['buttons'] & BUTTON_USE) and r['hook'] for r in group):
        hook = HOOK_DROP
    elif any(h and not p for h, p in zip(hb, [hbefore] + hb[:-1])):
        hook = HOOK_PRESS
    elif hb[-1]:
        hook = HOOK_HOLD
    else:
        hook = HOOK_NONE

    end = group[-1]
    return {
        'move': move, 'jump': jump, 'hook': hook,
        'attack': int(any(r['buttons'] & BUTTON_ATTACK for r in group)),
        'yaw': bin_of(YAW_EDGES, end['yawErr']), 'pitch': bin_of(PITCH_EDGES, end['pitch']),
    }


NO_ACTION = {'move': 4, 'jump': JUMP_NONE, 'hook': HOOK_NONE, 'attack': 0, 'yaw': bin_of(YAW_EDGES, 0), 'pitch': 1}


def frames(rows, near=1500.0):
    """(features, actions) per server frame of a log: the state at the end of the frame before decides the next one.
    Rows of one server frame share their level time; frames without a player fought near are left out."""
    legs, saber = tables()
    groups, cur = [], []
    for r in rows:
        if cur and r['time'] != cur[-1]['time']:
            groups.append(cur)
            cur = []
        cur.append(r)
    if cur:
        groups.append(cur)
    out, prev = [], dict(NO_ACTION)
    for i in range(1, len(groups)):
        state = groups[i - 1][-1]
        act = actions(groups[i], state)
        if state['other'] >= 0 and state['dist'] < near and state['health'] > 0:
            out.append((features(state, legs, saber, prev), act))
        prev = act
    return out
