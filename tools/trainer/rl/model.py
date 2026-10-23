"""The networks of the learned policy: the policy itself (what codemp/game/trainer/g_policy.c runs) and the value
network PPO needs besides (only here). export() writes the policy file g_policy.c reads, Policy_Parse() there has
the same layout.
"""
import os
import struct

import numpy as np
import torch
import torch.nn as nn

import spec

MAGIC = b'VJKPOL01'
HIDDEN = (256, 256)


class Net(nn.Module):
    """obs -> tanh -> tanh -> out, the observation normalised first as g_policy.c does: (x - mean) * scale, scale
    1 / std, 0 for a part that never changed"""

    def __init__(self, out, mean, std, hidden=HIDDEN):
        super().__init__()
        self.l1 = nn.Linear(spec.OBS_SIZE, hidden[0])
        self.l2 = nn.Linear(hidden[0], hidden[1])
        self.l3 = nn.Linear(hidden[1], out)
        mean = torch.as_tensor(mean, dtype=torch.float32)
        std = torch.as_tensor(std, dtype=torch.float32)
        self.register_buffer('mean', mean)
        self.register_buffer('std', std)
        self.register_buffer('scale', torch.where(std > 1e-6, 1.0 / std.clamp(min=1e-6), torch.zeros_like(std)))

    def forward(self, x):
        x = (x - self.mean) * self.scale
        return self.l3(torch.tanh(self.l2(torch.tanh(self.l1(x)))))


def policy_net(mean, std):
    return Net(sum(spec.HEAD_SIZES), mean, std)


def value_net(mean, std):
    return Net(1, mean, std)


def norm_stats(obs):
    """mean and standard deviation of the observations; a part that never changes keeps mean 0 and std 1, so
    what it shows later (the probes, which old logs lack) still gets through"""
    mean = obs.mean(axis=0)
    std = obs.std(axis=0)
    flat = std < 1e-3
    mean[flat] = 0.0
    std[flat] = 1.0
    return mean.astype(np.float32), std.astype(np.float32)


def split(logits):
    return torch.split(logits, spec.HEAD_SIZES, dim=-1)


def log_prob(logits, acts):
    """the log-probability of the actions (n x heads), summed over the heads as g_policy.c sums them"""
    out = 0.0
    for h, lg in enumerate(split(logits)):
        out = out + torch.log_softmax(lg, dim=-1).gather(-1, acts[:, h:h + 1].long()).squeeze(-1)
    return out


def entropy(logits):
    out = 0.0
    for lg in split(logits):
        p = torch.log_softmax(lg, dim=-1)
        out = out - (p.exp() * p).sum(-1)
    return out


def export(net, path, version):
    """the policy file: magic, version, observations, heads, the size of each head, the hidden layers (ints), then
    mean, std, layer 1, layer 2, the logits (floats, weights by output then input, then the biases). Written to a
    file beside it first, then renamed, so a server never reads half of it."""
    with torch.no_grad():
        parts = [net.mean, net.std, net.l1.weight, net.l1.bias, net.l2.weight, net.l2.bias, net.l3.weight,
                 net.l3.bias]
        floats = np.concatenate([p.detach().cpu().float().numpy().ravel() for p in parts]).astype('<f4')
    head = [version, spec.OBS_SIZE, len(spec.HEAD_SIZES)] + list(spec.HEAD_SIZES) + [net.l1.out_features,
                                                                                     net.l2.out_features]
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(MAGIC)
        f.write(struct.pack('<%di' % len(head), *head))
        f.write(floats.tobytes())
    os.replace(tmp, path)


def load(path):
    """a policy file back into a network (to go on from it)"""
    data = open(path, 'rb').read()
    if data[:8] != MAGIC:
        raise ValueError('%s: no policy file' % path)
    nh = len(spec.HEAD_SIZES)
    head = struct.unpack_from('<%di' % (3 + nh + 2), data, 8)
    version, obs, heads = head[:3]
    if obs != spec.OBS_SIZE or heads != nh or list(head[3:3 + nh]) != list(spec.HEAD_SIZES):
        raise ValueError('%s: another layout than spec.py' % path)
    h1, h2 = head[3 + nh:]
    f = np.frombuffer(data, dtype='<f4', offset=8 + 4 * len(head))
    at = 0

    def take(n):
        nonlocal at
        out = f[at:at + n]
        at += n
        return torch.tensor(out.copy())

    mean, std = take(obs), take(obs)
    net = Net(sum(spec.HEAD_SIZES), mean, std, (h1, h2))
    with torch.no_grad():
        net.l1.weight.copy_(take(h1 * obs).view(h1, obs))
        net.l1.bias.copy_(take(h1))
        net.l2.weight.copy_(take(h2 * h1).view(h2, h1))
        net.l2.bias.copy_(take(h2))
        out = sum(spec.HEAD_SIZES)
        net.l3.weight.copy_(take(out * h2).view(out, h2))
        net.l3.bias.copy_(take(out))
    if at != len(f):
        raise ValueError('%s: %d floats too many' % (path, len(f) - at))
    return net, version
