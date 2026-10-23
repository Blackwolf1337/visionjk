#!/usr/bin/env python3
"""Trains the policy by playing: PPO with matches on headless servers (simrun.py) as the world.

    ppo.py --game DIR --start policy.dat --clone clone.dat [--out DIR] [--actors 22] [--hours 8]

The actors are threads, each runs one match after another on its own server: the newest policy (ai) against a
mix of opponents (itself, older versions of itself, the clone, trainer levels 3-5), on the training maps. The
policy writes what it saw, did and got (rollouts, codemp/game/trainer/g_policy.c); the learner here takes them,
updates the policy and the value network on the GPU and writes the next policy file, which the next matches load.
Early on the policy is kept near the start (the behaviour clone of the player, bc.py) by a KL term that fades.
Pressing the saber key and changing the move or the aim cost a little (--saber-cost, --change-cost): without that
the policy toggles its saber twice a second and jitters, which the bots do not punish but looks broken.

OUT gets policy.dat (the newest), league/ (every --snapshot versions), checkpoint.pt (to go on with --resume) and
log.csv (one line per update).
"""
import argparse
import collections
import csv
import os
import queue
import random
import shutil
import sys
import threading
import time

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import model  # noqa: E402
import rollouts  # noqa: E402
import simrun  # noqa: E402
import spec  # noqa: E402

TRAIN_MAPS = ['mp/duel1', 'mp/duel3', 'mp/duel5', 'mp/duel6', 'mp/duel7', 'mp/duel8', 'mp/duel9', 'mp/ffa3']
# who the policy fights, and how often: itself, an older version (ai2), the behaviour clone of a player (user, with
# --user: also ai2), the clone, the trainer levels and the ones of them that hop (5h)
OPPONENTS = [('ai', 0.3), ('ai2', 0.1), ('user', 0.1), ('clone', 0.1), ('5', 0.1), ('4', 0.05), ('3', 0.05),
             ('5h', 0.1), ('4h', 0.05), ('3h', 0.05)]


class Actors:
    """threads that play matches and put (info, records) of every rollout file into a queue"""

    def __init__(self, args, out):
        self.args, self.out = args, out
        self.data = queue.Queue(maxsize=4 * args.actors)
        self.results = queue.Queue()
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.threads = [threading.Thread(target=self.run, args=(i,), daemon=True) for i in range(args.actors)]

    def start(self):
        for t in self.threads:
            t.start()

    def pick(self):
        """the opponent and the policy file of slot 1 it needs (an older version or the clone of the player)"""
        league = sorted(os.listdir(os.path.join(self.out, 'league')))
        names, weights = zip(*[(n, w) for n, w in OPPONENTS if (n != 'ai2' or league) and (n != 'user' or self.args.user)])
        opp = random.choices(names, weights)[0]
        if opp == 'ai2':
            return opp, os.path.join(self.out, 'league', random.choice(league))
        if opp == 'user':
            return opp, self.args.user
        return opp, None

    def run(self, i):
        port = self.args.port + i
        while not self.stop.is_set():
            opp, snap = self.pick()
            with self.lock:
                # a copy of the newest policy, the learner may replace it any moment
                mine = os.path.join(self.out, 'actors', 'policy%d.dat' % i)
                shutil.copy(os.path.join(self.out, 'policy.dat'), mine)
            files = {'trainer/policy.dat': mine}
            sets = {'vs_rlRollouts': 1, 'vs_rlReward': self.args.reward}
            if snap:
                files['trainer/opp.dat'] = snap
                sets['vs_policyOpponent'] = 'trainer/opp.dat'
            # half of the matches with the policy as the second bot: the first one thinks first in a frame
            swap = random.random() < 0.5
            level = 'ai2' if opp == 'user' else opp
            a, b = (level, 'ai') if swap else ('ai', level)
            res = simrun.run_match(self.args.game, a, b, self.args.rounds, random.choice(TRAIN_MAPS),
                                   clone=self.args.clone, port=port, timeout=600, files=files, sets=sets,
                                   collect=collect)
            for item in res.get('collected', []):
                self.data.put(item)
            if res.get('sec'):
                me, them = ('B', 'A') if swap else ('A', 'B')
                self.results.put({'opp': opp, 'sec': res['sec'], 'dealt': res.get('dealt' + me, 0),
                                  'taken': res.get('dealt' + them, 0), 'kills': res.get('kills' + me, 0),
                                  'deaths': res.get('kills' + them, 0)})


def collect(jp):
    out = []
    folder = os.path.join(jp, 'rollouts')
    for name in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        r = rollouts.read(os.path.join(folder, name))
        if r and len(r[1]):
            out.append(r)
    return out


def costs(rec, saber, change, wall=0.0):
    """what the saber key, changes of the move and the aim (to the decision before) and walls take off the reward.
    A wall: the hook holding at body height (not the floor, -16 to 40 units from the middle of the body), or going
    forward on the ground two decisions in a row and still slower than 60 units a second."""
    act = rec['act'].astype(np.int64)
    obs = rec['obs']
    cost = saber * (act[:, 6] == 1)
    if wall:
        a = spec.OBS_AT
        hz = obs[:, a['HOOKAT'] + 2] * 500.0
        cost = cost + wall * ((obs[:, a['HOOKED']] > 0.5) & (hz > -16.0) & (hz < 40.0))
        speed = np.hypot(obs[:, a['VEL']], obs[:, a['VEL'] + 1]) * 1000.0
        forward = act[:, 0] // 3 == 2
        stuck = np.zeros(len(act), dtype=bool)
        stuck[1:] = forward[1:] & forward[:-1] & (obs[1:, a['GROUND']] > 0.5) & (speed[1:] < 60.0) & (rec['done'][:-1] == 0)
        cost = cost + wall * stuck
    if len(act) > 1:
        moved = np.zeros(len(act), dtype=bool)
        for h in (0, 4, 5):		# move, aim yaw, aim pitch
            moved[1:] |= act[1:, h] != act[:-1, h]
        moved[1:] &= rec['done'][:-1] == 0		# not over the start of an episode
        cost = cost + change * moved
    return cost.astype(np.float32)


def advantages(rec, values, gamma, lam):
    """GAE over one rollout file; an episode that ran out of time goes on from its last value"""
    n = len(rec)
    adv = np.zeros(n, dtype=np.float32)
    last = 0.0
    for t in range(n - 1, -1, -1):
        done = rec['done'][t]
        if done == rollouts.DONE_END:
            nxt, last = 0.0, 0.0
        elif done == rollouts.DONE_TIME or t == n - 1:
            nxt, last = values[t], 0.0
        else:
            nxt = values[t + 1]
        delta = rec['reward'][t] + gamma * nxt - values[t]
        last = delta + gamma * lam * last
        adv[t] = last
    return adv, adv + values


def renorm(nets, obs):
    """the parts the normalisation leaves as they are (mean 0, std 1: the start never saw them) get the mean and
    std of obs; the first layer of each network is changed so that it computes the same as before"""
    net0 = nets[0]
    raw = (net0.mean == 0) & (net0.std == 1)
    mean, std = obs.mean(0), obs.std(0)
    idx = torch.nonzero(raw & (std > 1e-3)).squeeze(-1)
    with torch.no_grad():
        for net in nets:
            s0, m0 = net.scale[idx].clone(), net.mean[idx].clone()
            s1 = 1.0 / std[idx]
            w = net.l1.weight[:, idx]
            net.l1.bias += (w * s0) @ (mean[idx] - m0)
            net.l1.weight[:, idx] = w * s0 / s1
            net.mean[idx] = mean[idx]
            net.std[idx] = std[idx]
            net.scale[idx] = s1
    return len(idx)


def kl_to(logits, ref):
    """KL(policy || reference) summed over the heads"""
    out = 0.0
    for lg, rf in zip(model.split(logits), model.split(ref)):
        p = torch.log_softmax(lg, -1)
        q = torch.log_softmax(rf, -1)
        out = out + (p.exp() * (p - q)).sum(-1)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--start', required=True, help='the policy to start from (bc.py)')
    ap.add_argument('--clone', required=True)
    ap.add_argument('--out', default='ppo-out')
    ap.add_argument('--actors', type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument('--port', type=int, default=29500)
    ap.add_argument('--rounds', type=int, default=20)
    ap.add_argument('--hours', type=float, default=8.0)
    ap.add_argument('--batch', type=int, default=200000, help='frames per update')
    ap.add_argument('--minibatch', type=int, default=16384)
    ap.add_argument('--epochs', type=int, default=4)
    ap.add_argument('--lr', type=float, default=3e-4)
    ap.add_argument('--gamma', type=float, default=0.995)
    ap.add_argument('--lam', type=float, default=0.95)
    ap.add_argument('--clip', type=float, default=0.2)
    ap.add_argument('--entropy', type=float, default=0.005)
    ap.add_argument('--kl', type=float, default=0.3, help='weight of the KL to the start, fades to 0')
    ap.add_argument('--kl-updates', type=int, default=300, help='updates the KL fades over')
    ap.add_argument('--snapshot', type=int, default=20)
    ap.add_argument('--max-lag', type=int, default=3, help='rollouts of older versions are left out')
    ap.add_argument('--reward', default='0.01 1 1 0.3')
    ap.add_argument('--saber-cost', type=float, default=0.0, help='taken off the reward for a press of the saber key')
    ap.add_argument('--change-cost', type=float, default=0.0, help='for a change of the move or the aim')
    ap.add_argument('--wall-cost', type=float, default=0.0, help='a frame hooked into a wall or running into one')
    ap.add_argument('--user', help='the behaviour clone of a player (bc.py) as an opponent')
    ap.add_argument('--renorm', action='store_true', help='the parts of the observation the start never saw '
                    '(mean 0, std 1) get the mean and std of the first rollouts; the networks stay the same')
    ap.add_argument('--resume', action='store_true')
    args = ap.parse_args()
    device = 'cuda' if torch.cuda.is_available() else 'cpu'
    out = os.path.abspath(args.out)
    for d in ('league', 'actors'):
        os.makedirs(os.path.join(out, d), exist_ok=True)

    ref, _ = model.load(args.start)
    ref = ref.to(device).eval()
    policy, _ = model.load(args.start)
    policy = policy.to(device)
    value = model.value_net(ref.mean.cpu(), ref.std.cpu()).to(device)
    opt_p = torch.optim.Adam(policy.parameters(), lr=args.lr)
    opt_v = torch.optim.Adam(value.parameters(), lr=args.lr)
    version = 0
    ckpt = os.path.join(out, 'checkpoint.pt')
    if args.resume and os.path.exists(ckpt):
        state = torch.load(ckpt, map_location=device)
        policy.load_state_dict(state['policy'])
        value.load_state_dict(state['value'])
        opt_p.load_state_dict(state['opt_p'])
        opt_v.load_state_dict(state['opt_v'])
        version = state['version']
        print('going on from version %d' % version)
    model.export(policy, os.path.join(out, 'policy.dat'), version)

    actors = Actors(args, out)
    actors.start()
    log_path = os.path.join(out, 'log.csv')
    new_log = not os.path.exists(log_path)
    log = open(log_path, 'a', newline='')
    w = csv.writer(log)
    if new_log:
        w.writerow(['version', 'time', 'frames', 'frames_s', 'lag', 'ret_ep', 'kl_coef', 'kl_start', 'entropy',
                    'clipfrac', 'vloss', 'opp_stats'])
    t0, total, renormed = time.time(), 0, False
    deadline = t0 + args.hours * 3600
    print('%d actors on %s, the learner on %s' % (args.actors, args.game, device), flush=True)

    while time.time() < deadline:
        batch, frames, lags, stale = [], 0, [], 0
        while frames < args.batch:
            info, rec = actors.data.get()
            if info['version'] < version - args.max_lag:
                stale += 1
                continue
            batch.append(rec)
            lags.append(version - info['version'])
            frames += len(rec)
        total += frames

        obs = torch.tensor(np.concatenate([r['obs'] for r in batch]), device=device)
        if args.renorm and not renormed:
            renormed = True
            n = renorm([policy, value], obs)
            model.export(policy, os.path.join(out, 'policy.dat'), version)
            print('%d parts of the observation normalised from the first rollouts' % n, flush=True)
        act = torch.tensor(np.concatenate([r['act'] for r in batch]).astype(np.int64), device=device)
        old_logp = torch.tensor(np.concatenate([r['logp'] for r in batch]), device=device)
        with torch.no_grad():
            vals = value(obs).squeeze(-1).cpu().numpy()
        adv, ret, at = [], [], 0
        for r in batch:
            if args.saber_cost or args.change_cost or args.wall_cost:
                r = r.copy()
                r['reward'] -= costs(r, args.saber_cost, args.change_cost, args.wall_cost)
            a, g = advantages(r, vals[at:at + len(r)], args.gamma, args.lam)
            adv.append(a)
            ret.append(g)
            at += len(r)
        adv = torch.tensor(np.concatenate(adv), device=device)
        ret = torch.tensor(np.concatenate(ret), device=device)
        adv = (adv - adv.mean()) / (adv.std() + 1e-8)
        kl_coef = args.kl * max(0.0, 1.0 - version / max(1, args.kl_updates))

        stats = collections.defaultdict(list)
        for _ in range(args.epochs):
            perm = torch.randperm(len(obs), device=device)
            for i in range(0, len(perm), args.minibatch):
                idx = perm[i:i + args.minibatch]
                logits = policy(obs[idx])
                logp = model.log_prob(logits, act[idx])
                ratio = torch.exp(logp - old_logp[idx])
                a = adv[idx]
                pg = -torch.min(ratio * a, ratio.clamp(1 - args.clip, 1 + args.clip) * a).mean()
                ent = model.entropy(logits).mean()
                loss = pg - args.entropy * ent
                if kl_coef > 0:
                    with torch.no_grad():
                        ref_logits = ref(obs[idx])
                    kl = kl_to(logits, ref_logits).mean()
                    loss = loss + kl_coef * kl
                    stats['kl'].append(kl.item())
                opt_p.zero_grad()
                loss.backward()
                torch.nn.utils.clip_grad_norm_(policy.parameters(), 0.5)
                opt_p.step()
                vloss = ((value(obs[idx]).squeeze(-1) - ret[idx]) ** 2).mean()
                opt_v.zero_grad()
                vloss.backward()
                torch.nn.utils.clip_grad_norm_(value.parameters(), 0.5)
                opt_v.step()
                stats['ent'].append(ent.item())
                stats['clip'].append(((ratio - 1).abs() > args.clip).float().mean().item())
                stats['vloss'].append(vloss.item())

        version += 1
        model.export(policy, os.path.join(out, 'policy.dat'), version)
        if version % args.snapshot == 0:
            shutil.copy(os.path.join(out, 'policy.dat'), os.path.join(out, 'league', 'v%05d.dat' % version))
        torch.save({'policy': policy.state_dict(), 'value': value.state_dict(), 'opt_p': opt_p.state_dict(),
                    'opt_v': opt_v.state_dict(), 'version': version}, ckpt + '.tmp')
        os.replace(ckpt + '.tmp', ckpt)

        # the matches since the last update: health dealt minus taken a minute, kills to deaths, by opponent
        res = collections.defaultdict(lambda: [0.0, 0.0, 0, 0])
        while not actors.results.empty():
            r = actors.results.get()
            s = res[r['opp']]
            s[0] += r['dealt'] - r['taken']
            s[1] += r['sec'] / 60.0
            s[2] += int(r['kills'])
            s[3] += int(r['deaths'])
        opp_stats = ' '.join('%s:%+.0f/min,%d:%d' % (k, v[0] / max(v[1], 1e-6), v[2], v[3]) for k, v in sorted(res.items()))
        episodes = sum(int((r['done'] > 0).sum()) for r in batch)
        ret_ep = float(sum(r['reward'].sum() for r in batch)) / max(1, episodes)
        row = [version, round(time.time() - t0), frames, round(total / max(time.time() - t0, 1e-6)), round(np.mean(lags), 2),
               round(ret_ep, 3), round(kl_coef, 3), round(np.mean(stats['kl']), 4) if stats['kl'] else 0,
               round(np.mean(stats['ent']), 3), round(np.mean(stats['clip']), 3), round(np.mean(stats['vloss']), 4),
               opp_stats]
        w.writerow(row)
        log.flush()
        print('v%d %ds: %d frames (%d/s since the start, lag %.1f, %d stale), return/episode %.2f, entropy %.2f, clip %.2f | %s' % (
            version, row[1], frames, row[3], row[4], stale, ret_ep, row[8], row[9], opp_stats), flush=True)

    actors.stop.set()
    print('done after %d versions' % version)


if __name__ == '__main__':
    main()
