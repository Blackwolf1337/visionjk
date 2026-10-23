#!/usr/bin/env python3
"""How good a policy is: matches against fixed opponents on maps the training does not use (ppo.py TRAIN_MAPS),
half of them with the policy as the second bot (the first one thinks first in a frame).

    eval.py --game DIR --policy FILE --clone FILE [--opponents 3,4,5,clone] [--vs FILE] [--maps ...]
            [--matches 4] [--rounds 20] [--temp 1] [--workers 22]

--vs FILE adds an older policy as the opponent ai2. Prints per opponent: health dealt minus taken a minute, kills
to deaths and landed flip kicks a minute of both (a kick lands as MOD_MELEE; the trainer levels also hit with
their melee grab).
"""
import argparse
import collections
import multiprocessing
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import simrun  # noqa: E402

EVAL_MAPS = ['mp/duel2', 'mp/duel4', 'mp/ffa1']


def job(j):
    args, opp, mapname, swap, port = j
    files = {'trainer/policy.dat': args.policy}
    sets = {'vs_policyTemp': args.temp}
    if opp == 'ai2':
        files['trainer/opp.dat'] = args.vs
        sets['vs_policyOpponent'] = 'trainer/opp.dat'
    a, b = (opp, 'ai') if swap else ('ai', opp)
    r = simrun.run_match(args.game, a, b, args.rounds, mapname, clone=args.clone, port=port, timeout=900,
                         files=files, sets=sets)
    if not r.get('sec'):
        return None
    me, them = ('B', 'A') if swap else ('A', 'B')
    return opp, {'sec': r['sec'], 'dealt': r['dealt' + me], 'taken': r['dealt' + them], 'kills': r['kills' + me],
                 'deaths': r['kills' + them], 'kicks': r.get('kicks' + me, 0), 'okicks': r.get('kicks' + them, 0)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--policy', required=True)
    ap.add_argument('--clone', required=True)
    ap.add_argument('--opponents', default='3,4,5,clone')
    ap.add_argument('--vs')
    ap.add_argument('--maps', default=','.join(EVAL_MAPS))
    ap.add_argument('--matches', type=int, default=4, help='per opponent and map, half of them on each side')
    ap.add_argument('--rounds', type=int, default=20)
    ap.add_argument('--temp', type=float, default=1.0)
    ap.add_argument('--workers', type=int, default=os.cpu_count())
    ap.add_argument('--port', type=int, default=29900)
    args = ap.parse_args()
    args.policy = os.path.abspath(args.policy)
    opps = args.opponents.split(',') + (['ai2'] if args.vs else [])
    jobs = []
    for opp in opps:
        for m in args.maps.split(','):
            for k in range(args.matches):
                jobs.append((args, opp, m, k % 2 == 1, args.port + len(jobs) % 500))
    with multiprocessing.Pool(args.workers) as pool:
        results = pool.map(job, jobs)
    tot = collections.defaultdict(lambda: collections.Counter())
    for r in results:
        if r:
            tot[r[0]].update(r[1])
    print('%s, temperature %g, %d rounds a match' % (os.path.basename(args.policy), args.temp, args.rounds))
    print('%-6s %10s %13s %12s %12s' % ('vs', 'dmg/min', 'kills:deaths', 'kicks/min', 'theirs/min'))
    for opp in opps:
        t = tot[opp]
        if not t['sec']:
            print('%-6s no result' % opp)
            continue
        mins = t['sec'] / 60.0
        print('%-6s %+10.1f %8d:%-4d %12.1f %12.1f' % (opp, (t['dealt'] - t['taken']) / mins, t['kills'], t['deaths'],
                                                    t['kicks'] / mins, t['okicks'] / mins))


if __name__ == '__main__':
    main()
