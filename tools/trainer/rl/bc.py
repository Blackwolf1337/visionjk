#!/usr/bin/env python3
"""Behaviour cloning: the policy network learns what a player did, from his movement logs (vs_movelog), and is
written as the first policy file (version 0). PPO (ppo.py) starts from it and keeps near it for a while.

    bc.py [--test LOG] [--epochs 30] [--out policy.dat] LOG|DIR ...

--test is a whole recording the network does not learn from, to judge it by (default: the newest log), and the
best number of epochs on it is taken. The file for the game learns from all logs afterwards. Left out, with no
weight in the file, so the policy does without them until PPO teaches it to use them: the actions of the frame
before (spec.HISTORY) and, while some logs lack them, the newer parts of the observation (spec.NEWER).
"""
import argparse
import glob
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import model  # noqa: E402
import spec  # noqa: E402

T = spec.T


def logs(paths):
    out = []
    for p in paths:
        out += sorted(glob.glob(os.path.join(p, '*.csv'))) if os.path.isdir(p) else [p]
    return out


def load(paths):
    """observations, actions, and whether all logs had the whole observation"""
    X, A, whole = [], [], True
    for p in paths:
        rows = T.read_log(p)
        if rows is None:
            print('  %s: too old a log, left out' % os.path.basename(p))
            continue
        fr = spec.frames(rows)
        whole = whole and spec.complete(rows)
        print('  %s: %d frames%s' % (os.path.basename(p), len(fr), '' if spec.complete(rows) else ' (older log)'))
        for obs, act in fr:
            X.append(obs)
            A.append([act[h] for h in spec.HEADS])
    return np.array(X, dtype=np.float32), np.array(A, dtype=np.int64), whole


def train(X, A, mean, std, epochs, device, test=None, blank=()):
    """the network after the epochs; with test, the epochs that were best on it too"""
    net = model.policy_net(mean, std).to(device)
    blank = list(blank)
    if blank:
        with torch.no_grad():
            net.l1.weight[:, blank] = 0.0
    best = (float('inf'), 0)
    opt = torch.optim.Adam(net.parameters(), lr=1e-3, weight_decay=1e-5)
    Xt, At = torch.tensor(X, device=device), torch.tensor(A, device=device)
    for ep in range(epochs):
        perm = torch.randperm(len(Xt), device=device)
        total = 0.0
        for i in range(0, len(perm), 2048):
            idx = perm[i:i + 2048]
            loss = -model.log_prob(net(Xt[idx]), At[idx]).mean()
            opt.zero_grad()
            loss.backward()
            if blank:
                net.l1.weight.grad[:, blank] = 0.0
            opt.step()
            total += loss.item() * len(idx)
        line = 'epoch %2d: loss %.3f' % (ep + 1, total / len(Xt))
        if test is not None:
            t = report(net, *test, device, quiet=True)
            best = min(best, (t, ep + 1))
            line += ', on the test log %.3f' % t
        print(line, flush=True)
    return net, best[1]


def report(net, X, A, device, quiet=False):
    with torch.no_grad():
        logits = net(torch.tensor(X, device=device))
        loss = -model.log_prob(logits, torch.tensor(A, device=device)).mean().item()
        if quiet:
            return loss
        for h, lg in enumerate(model.split(logits)):
            pred = lg.argmax(-1).cpu().numpy()
            line = '  %-6s accuracy %.3f (always the most common: %.3f)' % (
                spec.HEADS[h], np.mean(pred == A[:, h]), np.mean(A[:, h] == np.bincount(A[:, h]).argmax()))
            if spec.HEADS[h] in ('jump', 'hook'):
                # the presses are rare and what matters: how often a draw gives one, against the player
                p = torch.softmax(lg, -1)[:, 1].mean().item()
                line += ', presses drawn %.3f a frame, the player %.3f' % (p, np.mean(A[:, h] == 1))
            print(line)
    return loss


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--test')
    ap.add_argument('--epochs', type=int, default=30)
    ap.add_argument('--out', default='policy.dat')
    args = ap.parse_args()
    device = 'cuda' if torch.cuda.is_available() else 'cpu'
    torch.manual_seed(1)

    paths = logs(args.logs)
    test = args.test or paths[-1]
    train_paths = [p for p in paths if os.path.abspath(p) != os.path.abspath(test)]
    print('training on:')
    Xtr, Atr, whole_tr = load(train_paths)
    print('testing on:')
    Xte, Ate, whole_te = load([test])
    blank = spec.slots(spec.HISTORY)
    if not (whole_tr and whole_te):
        blank += spec.slots(spec.NEWER)
        print('older logs: the newer parts of the observation are left out (spec.NEWER)')
    Xtr[:, blank] = 0.0
    Xte[:, blank] = 0.0
    mean, std = model.norm_stats(np.concatenate([Xtr, Xte]))
    # the parts left out: as they come in the game
    mean[blank], std[blank] = 0.0, 1.0

    net, epochs = train(Xtr, Atr, mean, std, args.epochs, device, (Xte, Ate), blank)
    print('on the test log (after %d epochs, the best was after %d):' % (args.epochs, epochs))
    report(net, Xte, Ate, device)

    print('the policy for the game, from all logs, %d epochs:' % epochs)
    net, _ = train(np.concatenate([Xtr, Xte]), np.concatenate([Atr, Ate]), mean, std, epochs, device, blank=blank)
    model.export(net.cpu(), args.out, 0)
    print('wrote %s (version 0, %d bytes)' % (args.out, os.path.getsize(args.out)))


if __name__ == '__main__':
    main()
