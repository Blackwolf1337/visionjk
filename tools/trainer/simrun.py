#!/usr/bin/env python3
"""Matches of the training bot on headless dedicated servers: vs_simFast with fixedtime runs the frames back to
back, vs_trainermatch plays the rounds and writes one result line. Each match has its own home folder and port,
so several run at once.

    simrun.py --game DIR [--params FILE] [--clone FILE] [--map mp/duel1] [--rounds 30] A B

DIR has visionjkded.x86_64 and base/ with the pk3 files of the game, japlus/ with the game module. A and B are
levels 1-5, clone, ai or ai2 (the learned policy, tools/trainer/rl). Prints the result as key=value.
"""
import argparse
import os
import re
import shutil
import subprocess
import tempfile

SERVER = 'visionjkded.x86_64'


def read_defaults(header):
    """the values of trainer_params.h: name -> default"""
    out = {}
    for m in re.finditer(r'TRAINER_PARAM\(\s*(\w+),\s*(-?[0-9.]+)f?\s*\)', open(header).read()):
        out[m.group(1)] = float(m.group(2))
    return out


def write_params(path, params):
    with open(path, 'w') as f:
        f.write('// written by tools/trainer\n')
        for k, v in params.items():
            f.write('%s %.6g\n' % (k, v))


def parse_result(line):
    out = {}
    for tok in line.split():
        if '=' in tok:
            k, v = tok.split('=', 1)
            try:
                out[k] = float(v.split('/')[0])
            except ValueError:
                out[k] = v
    return out


def run_match(game, a, b, rounds=30, mapname='mp/duel1', params=None, clone=None, port=29300, timeout=900,
              keep=False, files=None, sets=None, collect=None):
    """one match, its result as a dict (empty if the server gave none). files: {path in japlus/: file} put there
    first, sets: {cvar: value} set at the start, collect(japlus folder): called before the folder goes, what it
    returns is the result's 'collected'."""
    home = tempfile.mkdtemp(prefix='vjksim_')
    try:
        jp = os.path.join(home, 'japlus')
        os.makedirs(os.path.join(jp, 'trainer'))
        cfg = ['fixedtime 50', 'vs_simFast 1', 'vs_trainermatch %s %s %d match.txt quit' % (a, b, rounds)]
        args = [os.path.join(game, SERVER), '+set', 'fs_basepath', game, '+set', 'fs_homepath', home,
                '+set', 'fs_game', 'japlus', '+set', 'dedicated', '1', '+set', 'net_port', str(port),
                '+set', 'bot_enable', '1', '+set', 'g_gametype', '0', '+set', 'jp_altDim', '0', '+set', 'sv_fps', '20']
        if params:
            write_params(os.path.join(jp, 'trainer', 'params.cfg'), params)
            args += ['+set', 'vs_trainerParams', 'trainer/params.cfg']
        if clone:
            shutil.copy(clone, os.path.join(jp, 'trainer', 'clone.dat'))
        for dest, src in (files or {}).items():
            shutil.copy(src, os.path.join(jp, dest))
        for k, v in (sets or {}).items():
            args += ['+set', k, str(v)]
        with open(os.path.join(jp, 'match.cfg'), 'w') as f:
            f.write('\n'.join(cfg) + '\n')
        args += ['+devmap', mapname, '+exec', 'match.cfg']
        with open(os.path.join(home, 'server.log'), 'w') as log:
            subprocess.run(args, cwd=game, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                           timeout=timeout)
        res = os.path.join(jp, 'match.txt')
        lines = open(res).read().strip().splitlines() if os.path.exists(res) else []
        out = parse_result(lines[-1]) if lines else {}
        if collect:
            out['collected'] = collect(jp)
        return out
    except subprocess.TimeoutExpired:
        return {}
    finally:
        if not keep:
            shutil.rmtree(home, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--params')
    ap.add_argument('--clone')
    ap.add_argument('--map', default='mp/duel1')
    ap.add_argument('--rounds', type=int, default=30)
    ap.add_argument('a')
    ap.add_argument('b')
    args = ap.parse_args()
    params = None
    if args.params:
        params = {}
        for line in open(args.params):
            parts = line.split('//')[0].split()
            if len(parts) == 2:
                params[parts[0]] = float(parts[1])
    res = run_match(args.game, args.a, args.b, args.rounds, args.map, params, args.clone)
    print(' '.join('%s=%s' % kv for kv in res.items()) if res else 'no result')


if __name__ == '__main__':
    main()
