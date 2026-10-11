"""Reads and writes the menu files of the game (ui/*.menu): a tree of entries, so a script can change them
without touching what it does not understand.

An entry is a word with what follows it on its line ("rect 0 0 640 480"), and maybe a block in braces. The
blocks of menuDef, itemDef and assetGlobalDef are entries again. Every other block (a script such as "action",
a list such as "cvarFloatList") is kept as the words it is made of.
"""
import re

STRUCTS = ('menudef', 'itemdef', 'assetglobaldef')
NL = '\n'


class Entry:
    def __init__(self, key, args=None, block=None):
        self.key = key
        self.args = list(args or [])
        self.block = block          # None, a list of Entry (STRUCTS) or a list of words (anything else)

    @property
    def k(self):
        return self.key.lower()

    def __repr__(self):
        return 'Entry(%s %s)' % (self.key, ' '.join(self.args))


def tokens(text):
    """The words of a file: braces, quoted strings with their quotes, line ends, and everything else split at
    white space. Comments are left out."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '\n':
            out.append(NL)
            i += 1
        elif c in ' \t\r':
            i += 1
        elif text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == '"':
            j = text.find('"', i + 1)
            j = n - 1 if j < 0 else j
            out.append(text[i:j + 1])
            i = j + 1
        elif c in '{}':
            out.append(c)
            i += 1
        else:
            j = i
            while j < n and text[j] not in ' \t\r\n{}"' and not text.startswith('//', j):
                j += 1
            out.append(text[i:j])
            i = j
    return out


def parse(text):
    """The entries of a menu file: what stands between its outermost braces."""
    toks = tokens(text)
    pos = 0

    def skip_nl():
        nonlocal pos
        while pos < len(toks) and toks[pos] == NL:
            pos += 1

    def raw_block():
        nonlocal pos
        depth, words = 1, []
        while pos < len(toks):
            t = toks[pos]
            pos += 1
            if t == '{':
                depth += 1
            elif t == '}':
                depth -= 1
                if depth == 0:
                    break
            words.append(t)
        while words and words[0] == NL:
            words.pop(0)
        while words and words[-1] == NL:
            words.pop()
        return words

    def entries():
        nonlocal pos
        out = []
        while True:
            skip_nl()
            if pos >= len(toks):
                return out
            t = toks[pos]
            if t == '}':
                pos += 1
                return out
            if t == '{':
                # a block without a word in front of it: the file itself
                pos += 1
                out.extend(entries())
                continue
            pos += 1
            # a line that starts with a number or a string goes on with the entry before it
            if out and out[-1].block is None and (t[0] in '"-.' or t[0].isdigit()):
                e = out[-1]
                e.args.append(t)
            else:
                e = Entry(t)
                out.append(e)
            while pos < len(toks) and toks[pos] not in (NL, '{', '}'):
                e.args.append(toks[pos])
                pos += 1
            save = pos
            skip_nl()
            if pos < len(toks) and toks[pos] == '{':
                pos += 1
                e.block = entries() if e.k in STRUCTS else raw_block()
            else:
                pos = save

    # The game skips every word of a file that is not one of the two it knows at that level, braces included
    # (one file starts with a line of text that is no comment).
    top = []
    while pos < len(toks):
        t = toks[pos]
        pos += 1
        if t.lower() in ('menudef', 'assetglobaldef'):
            e = Entry(t)
            while pos < len(toks) and toks[pos] not in (NL, '{', '}'):
                e.args.append(toks[pos])
                pos += 1
            skip_nl()
            if pos < len(toks) and toks[pos] == '{':
                pos += 1
                e.block = entries()
                top.append(e)
    return top


def write(entries, indent=0):
    """The text of a list of entries, as the game reads it."""
    lines = []
    tab = '\t' * indent
    for e in entries:
        head = e.key if not e.args else '%s%s%s' % (e.key, '\t' * max(1, 6 - len(e.key) // 4), ' '.join(e.args))
        if e.block is None:
            lines.append(tab + head)
        elif e.k in STRUCTS:
            if lines and lines[-1].strip():
                lines.append('')
            lines.append(tab + head.rstrip())
            lines.append(tab + '{')
            lines.append(write(e.block, indent + 1))
            lines.append(tab + '}')
        else:
            words = [w for w in e.block]
            if NL not in words and len(' '.join(words)) < 100:
                lines.append('%s%s { %s }' % (tab, head, ' '.join(words)))
            else:
                lines.append(tab + head)
                lines.append(tab + '{')
                cur = []
                for w in words + [NL]:
                    if w == NL:
                        if cur:
                            lines.append(tab + '\t' + ' '.join(cur))
                        cur = []
                    else:
                        cur.append(w)
                lines.append(tab + '}')
    return '\n'.join(lines)


def dump(entries):
    return '{\n' + write(entries, 1) + '\n}\n'


# ---- what a script reads and changes in an itemDef or a menuDef

def get(block, key, default=None):
    """The arguments of the first entry of that name in a block."""
    key = key.lower()
    for e in block:
        if e.k == key:
            return e.args
    return default


def find(block, key):
    key = key.lower()
    for e in block:
        if e.k == key:
            return e
    return None


def put(block, key, *args):
    """Sets the arguments of an entry, which is added at the end if it is not there."""
    e = find(block, key)
    args = [str(a) for a in args]
    if e:
        e.args = args
    else:
        block.append(Entry(key, args))


def drop(block, key):
    key = key.lower()
    block[:] = [e for e in block if e.k != key]


def unquote(s):
    return s[1:-1] if s and s[0] == '"' and s[-1] == '"' else s


def num(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def fmt(v):
    """A number as a menu file has it."""
    if abs(v - round(v)) < 0.005:
        return str(int(round(v)))
    return ('%.2f' % v).rstrip('0').rstrip('.')
