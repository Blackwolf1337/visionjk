#!/usr/bin/env python3
"""Writes codemp/game/trainer/policy_spec.h from spec.py: where each part of the observation is, the sizes of the
action heads and the probes, so g_policy.c fills the observation as observe() here does. Run again when spec.py
changes, then build the game module.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spec  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import gen_tables  # noqa: E402


def main():
    out = [gen_tables.HEAD.replace('clone_tables.h -- written by tools/trainer/gen_tables.py, do not edit. The classes '
                                   'of the animations and saber\n// moves and the aim bins the clone (g_clone.c) was '
                                   'trained with.',
                                   'policy_spec.h -- written by tools/trainer/rl/gen_policy_header.py, do not edit. '
                                   'The layout of the\n// observation and the actions of the learned policy '
                                   '(g_policy.c), from tools/trainer/rl/spec.py.')]
    out.append('\n#define POLICY_OBS\t\t\t%d\n' % spec.OBS_SIZE)
    out.append('#define POLICY_HEADS\t\t%d\n' % len(spec.HEAD_SIZES))
    out.append('#define POLICY_LOGITS\t\t%d\n' % sum(spec.HEAD_SIZES))
    out.append('#define POLICY_PROBES\t\t%d\n' % spec.PROBES)
    out.append('#define POLICY_WALL_RANGE\t%.1ff\n' % spec.WALL_RANGE)
    out.append('#define POLICY_DROP_OUT\t\t%.1ff\n' % spec.DROP_OUT)
    out.append('#define POLICY_DROP_RANGE\t%.1ff\n\n' % spec.DROP_RANGE)
    out.append('// where each part of the observation starts\n')
    for name, n in spec.OBS:
        out.append('#define PO_%-15s %d%s\n' % (name, spec.OBS_AT[name], '' if n == 1 else '\t// %d' % n))
    out.append('\nstatic const int policyHeadSize[POLICY_HEADS] = { %s };\n' % ', '.join(str(n) for n in spec.HEAD_SIZES))
    path = os.path.join(spec.T.GAME, 'trainer', 'policy_spec.h')
    with open(path, 'w', newline='\n') as f:
        f.write(''.join(out))
    print('wrote %s: %d observations, %d logits' % (path, spec.OBS_SIZE, sum(spec.HEAD_SIZES)))


if __name__ == '__main__':
    main()
