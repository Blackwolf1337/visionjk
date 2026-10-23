"""Reads the rollout files the matches write (vs_rlRollouts 1, Policy_MatchStart in codemp/game/trainer/g_policy.c):
a header, one record per decision of the policy, a footer with the number of records."""
import struct

import numpy as np

import spec

MAGIC = b'VJKRL001'
FOOTER = b'END!'
HEADER = 8 + 4 * 4 + 64
DONE_END, DONE_TIME = 1, 2

RECORD = np.dtype([
    ('obs', '<f4', (spec.OBS_SIZE,)),
    ('act', 'u1', (len(spec.HEAD_SIZES),)),
    ('done', 'u1'),
    ('pad', 'u1'),
    ('logp', '<f4'),
    ('reward', '<f4'),
])


def read(path):
    """(info, records) of a finished rollout file, None for one that is cut off or of another layout"""
    data = open(path, 'rb').read()
    if len(data) < HEADER + 8 or data[:8] != MAGIC or data[-8:-4] != FOOTER:
        return None
    obs, heads, version, opponent = struct.unpack_from('<4i', data, 8)
    if obs != spec.OBS_SIZE or heads != len(spec.HEAD_SIZES):
        return None
    count = struct.unpack_from('<i', data, len(data) - 4)[0]
    body = data[HEADER:-8]
    if len(body) != count * RECORD.itemsize:
        return None
    mapname = data[24:88].split(b'\0')[0].decode('latin-1')
    info = {'version': version, 'opponent': opponent, 'map': mapname, 'records': count}
    return info, np.frombuffer(body, dtype=RECORD)
