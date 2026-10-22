#!/usr/bin/env python3
"""Trains the clone from movement logs (vs_movelog) of a player: one small random forest per decision (move, jump,
hook, attack, aim yaw, aim pitch) on the features of trainerlib.FEATURES, and writes the model for the game.

    train_clone.py [--test LOG] [--out clone.dat] LOG|DIR ...

--test is a whole recording the forests do not see, to judge them by. Without it the newest log is taken. The
model file is read by codemp/game/trainer/g_clone.c (vs_cloneModel), its layout is in write_model().
"""
import argparse
import glob
import os
import struct
import sys

import numpy as np
from sklearn.ensemble import RandomForestClassifier

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trainerlib as T  # noqa: E402

MAGIC = b'VJKCLN01'
TREES = 8
DEPTH = 12
MIN_LEAF = 6


def logs(paths):
    out = []
    for p in paths:
        out += sorted(glob.glob(os.path.join(p, '*.csv'))) if os.path.isdir(p) else [p]
    return out


def load(paths):
    X, Y = [], {h: [] for h in T.HEADS}
    for p in paths:
        rows = T.read_log(p)
        if rows is None:
            print('  %s: too old a log, left out' % os.path.basename(p))
            continue
        fr = T.frames(rows)
        print('  %s: %d frames' % (os.path.basename(p), len(fr)))
        for f, a in fr:
            X.append(f)
            for h in T.HEADS:
                Y[h].append(a[h])
    return np.array(X, dtype=np.float32), {h: np.array(v) for h, v in Y.items()}


def write_model(path, forests):
    """magic, features, heads; per head: classes, trees; per tree: nodes as (feature or -1 for a leaf, threshold,
    left, right, first value or -1); per head: its values, the class shares of each leaf. Little endian."""
    with open(path, 'wb') as f:
        f.write(MAGIC)
        f.write(struct.pack('<ii', len(T.FEATURES), len(T.HEADS)))
        nodes_total = 0
        for h in T.HEADS:
            rf, ncls = forests[h], T.HEAD_CLASSES[h]
            values = []
            f.write(struct.pack('<ii', ncls, len(rf.estimators_)))
            for est in rf.estimators_:
                t = est.tree_
                f.write(struct.pack('<i', t.node_count))
                nodes_total += t.node_count
                for n in range(t.node_count):
                    if t.children_left[n] == -1:
                        share = np.zeros(ncls)
                        counts = t.value[n][0]
                        for k, c in enumerate(est.classes_.astype(int)):
                            share[c] = counts[k]
                        share /= max(share.sum(), 1e-9)
                        f.write(struct.pack('<ifiii', -1, 0.0, -1, -1, len(values)))
                        values += list(share)
                    else:
                        f.write(struct.pack('<ifiii', int(t.feature[n]), float(t.threshold[n]),
                                            int(t.children_left[n]), int(t.children_right[n]), -1))
            f.write(struct.pack('<i', len(values)))
            f.write(struct.pack('<%df' % len(values), *values))
    print('wrote %s: %d nodes, %d bytes' % (path, nodes_total, os.path.getsize(path)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--test')
    ap.add_argument('--out', default='clone.dat')
    args = ap.parse_args()

    train = logs(args.logs)
    test = args.test or train[-1]
    train = [p for p in train if os.path.abspath(p) != os.path.abspath(test)]
    print('training on:')
    Xtr, Ytr = load(train)
    print('testing on:')
    Xte, Yte = load([test])

    forests = {}
    for h in T.HEADS:
        # the classes the logs never have still get a slot in the file (write_model)
        rf = RandomForestClassifier(n_estimators=TREES, max_depth=DEPTH, min_samples_leaf=MIN_LEAF, n_jobs=-1,
                                    random_state=1)
        rf.fit(Xtr, Ytr[h])
        forests[h] = rf
        pred = rf.predict(Xte)
        base = np.mean(Yte[h] == np.bincount(Ytr[h]).argmax())
        acc = np.mean(pred == Yte[h])
        line = '  %-6s accuracy %.3f (always the most common: %.3f)' % (h, acc, base)
        if h in ('jump', 'hook'):
            # the presses are what matters, and they are rare
            press = Yte[h] == 1
            if press.any():
                line += ', presses found %.2f, said %.2f times as often as there' % (
                    np.mean(pred[press] == 1), np.sum(pred == 1) / press.sum())
        print(line)

    # the model the game uses learns from all logs, the test one too
    print('the model for the game, from all logs:')
    Xall = np.concatenate([Xtr, Xte])
    for h in T.HEADS:
        rf = RandomForestClassifier(n_estimators=TREES, max_depth=DEPTH, min_samples_leaf=MIN_LEAF, n_jobs=-1,
                                    random_state=1)
        rf.fit(Xall, np.concatenate([Ytr[h], Yte[h]]))
        forests[h] = rf
    write_model(args.out, forests)


if __name__ == '__main__':
    main()
