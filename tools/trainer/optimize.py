#!/usr/bin/env python3
"""Tunes the values of the training bot (trainer_params.h) in simulated matches against the clone: CMA-ES over
the values, the fitness is the damage the bot deals minus the damage it takes, per minute, over several maps.

    optimize.py --game DIR --clone FILE [--out DIR] [--workers 8] [--gens 40] [--pop 12] [--rounds 20]
                [--maps mp/duel1,mp/duel5,mp/ffa3] [--level 5]

Every generation writes OUT/best.cfg (the best values so far, a file for vs_trainerParams) and a line to
OUT/history.csv. The values are scaled: positive ones by a factor exp(0.35 x), the start scores of the approaches
by + 5 x, so x = 0 is the default. --range keeps every factor between 1/(1 + range) and 1 + range (the first run
without a tight range pushed many values to the edge, against the weak points of the clone).
"""
import argparse
import math
import multiprocessing
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import simrun  # noqa: E402
import trainerlib as T  # noqa: E402

# what is not tuned: how the bot learns during a fight and values it needs as they are
FIXED = {'LEARN_RATE', 'EXPLORE', 'TRY_MSEC', 'HOOK_FUEL', 'HOOK_MAX', 'KICK_TIME', 'SIDE_TIME', 'GRAB_TIME',
         'SWING_TIME', 'COUNTER_GRAB', 'GRAB_RANGE', 'GRAB_REACH', 'RED_WAIT', 'PRESS_SEC'}
ADDITIVE = {'PRIOR_HOOKKICK', 'PRIOR_RUNKICK', 'PRIOR_SIDEKICK', 'PRIOR_SWING', 'PRIOR_WAIT'}


XMAX = 3.0	# set from --range in main()


def to_params(names, defaults, x):
    out = {}
    for n, v in zip(names, x):
        v = max(-XMAX, min(XMAX, v))
        if n in ADDITIVE:
            out[n] = defaults[n] + 5.0 * v
        else:
            out[n] = defaults[n] * math.exp(0.35 * v)
    return out


def job(args):
    game, clone, params, level, rounds, mapname, port, _ = args
    res = simrun.run_match(game, 'clone', level, rounds, mapname, params, clone, port)
    if not res or res.get('sec', 0) <= 0:
        return None
    minutes = res['sec'] / 60.0
    return (res['dealtB'] - res['dealtA']) / minutes, res


class CMA:
    """a compact CMA-ES (Hansen, The CMA Evolution Strategy: A Tutorial), minimising"""

    def __init__(self, n, sigma=0.5, pop=None):
        self.n, self.sigma = n, sigma
        self.lam = pop or 4 + int(3 * math.log(n))
        self.mu = self.lam // 2
        w = np.log(self.mu + 0.5) - np.log(np.arange(1, self.mu + 1))
        self.w = w / w.sum()
        self.mueff = 1.0 / np.sum(self.w ** 2)
        self.cc = (4 + self.mueff / n) / (n + 4 + 2 * self.mueff / n)
        self.cs = (self.mueff + 2) / (n + self.mueff + 5)
        self.c1 = 2 / ((n + 1.3) ** 2 + self.mueff)
        self.cmu = min(1 - self.c1, 2 * (self.mueff - 2 + 1 / self.mueff) / ((n + 2) ** 2 + self.mueff))
        self.damps = 1 + 2 * max(0, math.sqrt((self.mueff - 1) / (n + 1)) - 1) + self.cs
        self.chin = math.sqrt(n) * (1 - 1 / (4 * n) + 1 / (21 * n * n))
        self.mean = np.zeros(n)
        self.pc, self.ps = np.zeros(n), np.zeros(n)
        self.C = np.eye(n)
        self.gen = 0

    def ask(self):
        self.D2, self.B = np.linalg.eigh(self.C)
        self.D = np.sqrt(np.maximum(self.D2, 1e-20))
        self.z = np.random.randn(self.lam, self.n)
        self.y = self.z * self.D @ self.B.T
        return self.mean + self.sigma * self.y

    def tell(self, xs, costs):
        order = np.argsort(costs)
        y = self.y[order[:self.mu]]
        z = self.z[order[:self.mu]]
        self.mean = self.mean + self.sigma * (self.w @ y)
        zw = self.w @ z
        self.ps = (1 - self.cs) * self.ps + math.sqrt(self.cs * (2 - self.cs) * self.mueff) * (self.B @ zw)
        self.gen += 1
        hsig = np.linalg.norm(self.ps) / math.sqrt(1 - (1 - self.cs) ** (2 * self.gen)) / self.chin < 1.4 + 2 / (self.n + 1)
        self.pc = (1 - self.cc) * self.pc + hsig * math.sqrt(self.cc * (2 - self.cc) * self.mueff) * (self.w @ y)
        self.C = ((1 - self.c1 - self.cmu) * self.C + self.c1 * (np.outer(self.pc, self.pc)
                  + (not hsig) * self.cc * (2 - self.cc) * self.C) + self.cmu * (y.T * self.w) @ y)
        self.sigma *= math.exp((self.cs / self.damps) * (np.linalg.norm(self.ps) / self.chin - 1))


def evaluate(pool, game, clone, cands, level, rounds, maps):
    """fitness of each candidate (params dict): damage per minute, the mean over the maps"""
    jobs = []
    for ci, params in enumerate(cands):
        for mi, m in enumerate(maps):
            jobs.append((ci, (game, clone, params, level, rounds, m, 29300 + (ci * len(maps) + mi) % 500, XMAX)))
    results = pool.map(job, [j[1] for j in jobs])
    fit = [[] for _ in cands]
    for (ci, _), r in zip(jobs, results):
        if r:
            fit[ci].append(r[0])
    return [float(np.mean(f)) if f else -1e3 for f in fit]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--clone', required=True)
    ap.add_argument('--out', default='optimize-out')
    ap.add_argument('--workers', type=int, default=os.cpu_count())
    ap.add_argument('--gens', type=int, default=40)
    ap.add_argument('--pop', type=int, default=12)
    ap.add_argument('--rounds', type=int, default=20)
    ap.add_argument('--maps', default='mp/duel1,mp/duel5,mp/ffa3')
    ap.add_argument('--level', default='5')
    ap.add_argument('--range', type=float, default=0.5)
    args = ap.parse_args()
    global XMAX
    XMAX = math.log(1.0 + args.range) / 0.35

    defaults = simrun.read_defaults(os.path.join(T.GAME, 'trainer', 'trainer_params.h'))
    names = [n for n in defaults if n not in FIXED]
    maps = args.maps.split(',')
    os.makedirs(args.out, exist_ok=True)
    hist = open(os.path.join(args.out, 'history.csv'), 'a')
    print('%d values to tune: %s' % (len(names), ' '.join(names)))

    with multiprocessing.Pool(args.workers) as pool:
        base = evaluate(pool, args.game, args.clone, [to_params(names, defaults, np.zeros(len(names)))] * 3,
                        args.level, args.rounds, maps)
        print('the defaults: %.1f damage a minute (%s)' % (np.mean(base), ', '.join('%.1f' % b for b in base)))
        hist.write('gen,best,mean,base\n0,%.2f,%.2f,%.2f\n' % (max(base), np.mean(base), np.mean(base)))
        es = CMA(len(names), sigma=0.5, pop=args.pop)
        best, best_fit = np.zeros(len(names)), float(np.mean(base))
        for g in range(1, args.gens + 1):
            xs = es.ask()
            fits = evaluate(pool, args.game, args.clone, [to_params(names, defaults, x) for x in xs], args.level,
                            args.rounds, maps)
            es.tell(xs, [-f for f in fits])
            i = int(np.argmax(fits))
            # a lucky candidate is checked again before it is the best
            if fits[i] > best_fit:
                again = evaluate(pool, args.game, args.clone, [to_params(names, defaults, xs[i])] * 2, args.level,
                                 args.rounds, maps)
                score = float(np.mean([fits[i]] + again))
                if score > best_fit:
                    best, best_fit = xs[i].copy(), score
                    simrun.write_params(os.path.join(args.out, 'best.cfg'), to_params(names, defaults, best))
            print('gen %3d: best %.1f, mean %.1f, best so far %.1f, sigma %.3f' % (
                g, max(fits), np.mean(fits), best_fit, es.sigma), flush=True)
            hist.write('%d,%.2f,%.2f,%.2f\n' % (g, max(fits), np.mean(fits), np.mean(base)))
            hist.flush()


if __name__ == '__main__':
    main()
