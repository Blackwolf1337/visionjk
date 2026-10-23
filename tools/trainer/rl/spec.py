"""The observation and the actions of the learned policy (codemp/game/trainer/g_policy.c), defined once here.

gen_policy_header.py writes the layout into codemp/game/trainer/policy_spec.h, g_policy.c fills the same slots from
the game, observe() here fills them from a row of a movement log (behaviour cloning). Directions are in the frame of
the line to the opponent: along it (to him) and across it (to the left of it).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import trainerlib as T  # noqa: E402

# the slots of the observation, in order: (name, count)
OBS = [
    ('FLAT', 1), ('DZ', 1),
    ('VEL', 3), ('OVEL', 3),
    ('GROUND', 1), ('OGROUND', 1), ('FLOOR', 1), ('OFLOOR', 1),
    ('YAWERR', 2), ('PITCH', 1), ('PITCHERR', 1),
    ('OFACING', 2), ('OPITCH', 1),
    ('HEALTH', 1), ('OHEALTH', 1), ('FORCE', 1),
    ('WEAPONTIME', 1), ('LEGSTIMER', 1), ('TORSOTIMER', 1),
    ('JUMPHELD', 1), ('OJUMPHELD', 1), ('KICKFLIP', 1),
    ('HOOKOUT', 1), ('HOOKED', 1), ('HOOKAT', 3),
    ('OHOOKOUT', 1), ('OHOOKED', 1), ('OHOOKAT', 3),
    ('HANDEXT', 1), ('OHANDEXT', 1), ('STYLE', 1), ('OSTYLE', 1), ('HOLSTERED', 1), ('OHOLSTERED', 1),
    ('OATTACK', 1), ('OHOOKBUTTON', 1), ('OMOVES', 3),
    ('LEGS', 6), ('OLEGS', 6), ('OSABER', 6), ('SABER', 6),
    ('PMOVE', 9), ('PJUMP', 4), ('PHOOK', 4), ('PATTACK', 1), ('PSABER', 1),
    ('AIM', 2),
    ('PROBED', 1), ('WALL', 8), ('DROP', 8),
]
OBS_SIZE = sum(n for _, n in OBS)
# what logs of 66 columns or from before the probes lack
NEWER = ['FLOOR', 'OFLOOR', 'OJUMPHELD', 'HOOKAT', 'OHOOKED', 'OHOOKAT', 'HANDEXT', 'OHANDEXT', 'OMOVES', 'PROBED',
         'WALL', 'DROP']
OBS_AT = {}
_at = 0
for _name, _n in OBS:
    OBS_AT[_name] = _at
    _at += _n

# the heads of the action: the ones of the clone (trainerlib.HEADS) and the saber key, 1 turns it on or off. The
# style is not one: the player fights in red (94 percent of his logs), g_trainer.c keeps the bot in it.
HEADS = list(T.HEADS) + ['saber']
HEAD_SIZES = [T.HEAD_CLASSES[h] for h in T.HEADS] + [2]
NO_ACTION = dict(T.NO_ACTION, saber=0)

# probes: 8 directions from the line to the opponent, counterclockwise (to the left first)
PROBES = 8
WALL_RANGE = 256.0		# a wall this far or nearer
DROP_OUT = 96.0			# the floor looked for this far out
DROP_RANGE = 512.0		# and this far down (MOVELOG_FLOOR_RANGE)

FLOOR_RANGE = 512.0
PMF_JUMP_HELD = 2
SS_STRONG = 3


def _clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def _onehot(x, at, count, i):
    if 0 <= i < count:
        x[at + i] = 1.0


def observe(r, prev, aim, legs, saber):
    """the observation of one movement log row (the state at the end of a server frame). prev: the actions of the
    frame before (trainerlib.actions), aim: (yaw offset, pitch) the aim moves toward. Logs from before the probes
    leave PROBED 0, logs of 66 columns lack the floor, the hook points, hand extend and the moves of the opponent:
    those stay 0 (the floor: 0 on the ground, half the range in the air)."""
    x = [0.0] * OBS_SIZE
    g = r.get
    flat = math.hypot(r['dx'], r['dy'])
    ux, uy = (r['dx'] / flat, r['dy'] / flat) if flat > 0.001 else (1.0, 0.0)

    def along(vx, vy):
        return vx * ux + vy * uy, -vx * uy + vy * ux

    a = OBS_AT
    x[a['FLAT']] = _clamp(flat / 1000.0, 0.0, 3.0)
    x[a['DZ']] = _clamp(r['dz'] / 500.0, -3.0, 3.0)
    va, vc = along(r['vx'], r['vy'])
    x[a['VEL']:a['VEL'] + 3] = [va / 1000.0, vc / 1000.0, r['vz'] / 1000.0]
    oa, oc = along(r['ovx'], r['ovy'])
    x[a['OVEL']:a['OVEL'] + 3] = [oa / 1000.0, oc / 1000.0, r['ovz'] / 1000.0]
    x[a['GROUND']] = float(r['ground'])
    x[a['OGROUND']] = float(r['oground'])
    x[a['FLOOR']] = g('floor', 0.0 if r['ground'] else FLOOR_RANGE / 2) / FLOOR_RANGE
    x[a['OFLOOR']] = g('ofloor', 0.0 if r['oground'] else FLOOR_RANGE / 2) / FLOOR_RANGE
    ye = math.radians(r['yawErr'])
    x[a['YAWERR']:a['YAWERR'] + 2] = [math.sin(ye), math.cos(ye)]
    x[a['PITCH']] = r['pitch'] / 90.0
    x[a['PITCHERR']] = r['pitchErr'] / 90.0
    of = math.radians(T.angle_sub(math.degrees(math.atan2(-r['dy'], -r['dx'])), r['oyaw']))
    x[a['OFACING']:a['OFACING'] + 2] = [math.sin(of), math.cos(of)]
    x[a['OPITCH']] = r['opitch'] / 90.0
    x[a['HEALTH']] = _clamp(r['health'] / 100.0, -1.0, 2.0)
    x[a['OHEALTH']] = _clamp(r['ohp'] / 100.0, -1.0, 2.0)
    x[a['FORCE']] = r['force'] / 100.0
    x[a['WEAPONTIME']] = _clamp(r['weaponTime'] / 1000.0, 0.0, 2.0)
    x[a['LEGSTIMER']] = _clamp(r['legsTimer'] / 1000.0, 0.0, 2.0)
    x[a['TORSOTIMER']] = _clamp(r['torsoTimer'] / 1000.0, 0.0, 2.0)
    x[a['JUMPHELD']] = float(bool(r['pmFlags'] & PMF_JUMP_HELD))
    x[a['OJUMPHELD']] = float(bool(g('opmFlags', 0) & PMF_JUMP_HELD))
    x[a['KICKFLIP']] = float(r['kick'] != 0)
    x[a['HOOKOUT']] = float(r['hook'] != 0)
    x[a['HOOKED']] = float(r['hook'] != 0 and r['hooked'] != 0)
    if g('hookOn', -1) != -1:
        ha, hc = along(r['hookX'] - r['x'], r['hookY'] - r['y'])
        x[a['HOOKAT']:a['HOOKAT'] + 3] = [ha / 500.0, hc / 500.0, (r['hookZ'] - r['z']) / 500.0]
    x[a['OHOOKOUT']] = float(r['ohook'] != 0)
    x[a['OHOOKED']] = float(g('ohookOn', -1) >= 0)
    if g('ohookOn', -1) != -1:
        ha, hc = along(r['ohookX'] - r['x'], r['ohookY'] - r['y'])
        x[a['OHOOKAT']:a['OHOOKAT'] + 3] = [ha / 500.0, hc / 500.0, (r['ohookZ'] - r['z']) / 500.0]
    x[a['HANDEXT']] = float(g('handExtend', 0) != 0)
    x[a['OHANDEXT']] = float(g('ohandExtend', 0) != 0)
    x[a['STYLE']] = float(r['style'] == SS_STRONG)
    x[a['OSTYLE']] = float(r['ostyle'] == SS_STRONG)
    x[a['HOLSTERED']] = float(r['holstered'] != 0)
    x[a['OHOLSTERED']] = float(r['oholstered'] != 0)
    x[a['OATTACK']] = float(bool(r['obuttons'] & T.BUTTON_ATTACK))
    x[a['OHOOKBUTTON']] = float(bool(r['obuttons'] & T.BUTTON_HOOK))
    x[a['OMOVES']:a['OMOVES'] + 3] = [g('ofwd', 0) / 127.0, g('oright', 0) / 127.0, g('oup', 0) / 127.0]
    _onehot(x, a['LEGS'], 6, legs[r['legsAnim']] if 0 <= r['legsAnim'] < len(legs) else T.LEGS_OTHER)
    _onehot(x, a['OLEGS'], 6, legs[r['olegs']] if 0 <= r['olegs'] < len(legs) else T.LEGS_OTHER)
    _onehot(x, a['OSABER'], 6, saber[r['osaberMove']] if 0 <= r['osaberMove'] < len(saber) else T.SABER_IDLE)
    _onehot(x, a['SABER'], 6, saber[r['saberMove']] if 0 <= r['saberMove'] < len(saber) else T.SABER_IDLE)
    _onehot(x, a['PMOVE'], 9, prev['move'])
    _onehot(x, a['PJUMP'], 4, prev['jump'])
    _onehot(x, a['PHOOK'], 4, prev['hook'])
    x[a['PATTACK']] = float(prev['attack'])
    x[a['PSABER']] = float(prev['saber'])
    x[a['AIM']:a['AIM'] + 2] = [aim[0] / 180.0, aim[1] / 90.0]
    if 'wall0' in r:
        x[a['PROBED']] = 1.0
        for i in range(PROBES):
            x[a['WALL'] + i] = r['wall%d' % i]
            x[a['DROP'] + i] = r['drop%d' % i]
    return x


def complete(rows):
    """the log has all of the observation (88 columns and the probes)"""
    return bool(rows) and 'floor' in rows[0] and 'wall0' in rows[0]


# the actions of the frame before: the player holds his keys, so a copy of him that sees them learns "do it again"
# and stands where it once stood (the copycat problem of behaviour cloning)
HISTORY = ['PMOVE', 'PJUMP', 'PHOOK', 'PATTACK', 'PSABER']


def slots(names):
    """the indices of these parts of the observation"""
    n = dict(OBS)
    return [OBS_AT[name] + i for name in names for i in range(n[name])]


def frames(rows, near=1500.0):
    """(observation, actions) per server frame of a log, as trainerlib.frames: the state at the end of the frame
    before decides the next one. The aim of the observation is where the frame before aimed (its bin centres)."""
    legs, saber = T.tables()
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
        act = T.actions(groups[i], state)
        # the saber went on or off in this frame
        act['saber'] = int((groups[i][-1]['holstered'] != 0) != (state['holstered'] != 0))
        aim = (T.YAW_CENTERS[prev['yaw']], T.PITCH_CENTERS[prev['pitch']])
        if state['other'] >= 0 and state['dist'] < near and state['health'] > 0:
            out.append((observe(state, prev, aim, legs, saber), act))
        prev = act
    return out
