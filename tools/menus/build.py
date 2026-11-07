#!/usr/bin/env python3
"""Makes the menu files of the new look (assets/japlus_client/ui/vision) from the menu files of the game.

usage: tools/menus/build.py [<assets1.pk3 of the game>]
       (the pk3 is looked for in ../GameData/base next to the tree unless it is given)

Every menu of the two lists the ui loads (ui/jampmenus.txt outside a game, ui/jampingame.txt in one) is read,
changed by the rules below and written under ui/vision/, with two lists of their own (ui/visionmenus.txt,
ui/visioningame.txt). Nothing of ui/jamp is touched: those files stay what the ui loads with v_classic 1 and
what a mod replaces.

What an item does is never changed: its cvar, its scripts, its feeder and its owner draw are taken as they
are. What changes is how it looks, see docs/japlus/ui.md:
  - the art of the old frame goes (backgrounds, side texts, button glows), panels come in its place
  - every text is set in the new font, in the colours of the theme
  - the menu gets "skin 1", so its widgets are drawn by codemp/ui/ui_skin.c
  - the pages outside a game get a bar at the top with the four sections, a title and the buttons at the bottom
A page that needs more than the rules is changed by a function of its own in pages.py.
"""
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import menufile as mf           # noqa: E402
import pages                    # noqa: E402
import texts                    # noqa: E402
from theme import *             # noqa: E402,F401,F403
import theme                    # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.normpath(os.path.join(HERE, '..', '..'))
UI = os.path.join(TREE, 'assets', 'japlus_client', 'ui')
OUT = os.path.join(UI, 'vision')


def read_list(name):
    """The menu files a list names, in its order."""
    out = []
    with open(os.path.join(UI, name), encoding='latin-1') as f:
        for line in f:
            line = line.split('//')[0]
            if 'loadMenu' in line and '"' in line:
                out.append(line.split('"')[1])
    return out


def source(path, base):
    """The text of a menu file: the one of this tree, or the one of the game."""
    own = os.path.join(UI, os.path.relpath(path, 'ui'))
    if os.path.exists(own):
        with open(own, encoding='latin-1') as f:
            return f.read()
    return base.read(path).decode('latin-1')


SCRIPTS = re.compile(r'\b(onOpen|onClose|onESC|onAccept|action|mouseEnter|mouseExit|mouseEnterText|mouseExitText|'
                     r'onFocus|leaveFocus|doubleClick|accept)\s*\{([^}]*)\}', re.I)


def script_limit():
    """How long a script may be for the menu code (MAX_SCRIPT_CHARS of codemp/ui/ui_shared.c)."""
    with open(os.path.join(TREE, 'codemp', 'ui', 'ui_shared.c'), encoding='latin-1') as f:
        return int(re.search(r'#define\s+MAX_SCRIPT_CHARS\s+(\d+)', f.read()).group(1))


def check_scripts(name, text, limit):
    """The menu code keeps a script in a buffer of fixed size, every word in quotes with a blank behind it, and
    drops what does not fit without stopping. The colours of the new look are longer than the ones of the game,
    so a script that fit there may not fit here (the siege menu did not, and lost its last commands)."""
    for m in SCRIPTS.finditer(text):
        words = re.findall(r'"[^"]*"|[^\s;]+|;', m.group(2))
        # counted on the safe side: a sign or an @ before a word may be a word of its own for the reader
        kept = sum(len(w.strip('"')) + 3 + 2 * (w[0] in '-@') for w in words)
        if kept >= limit:
            line = text.count('\n', 0, m.start()) + 1
            sys.exit('%s line %d: the script of %s needs %d characters, the menu code keeps %d (MAX_SCRIPT_CHARS)'
                     % (name, line, m.group(1), kept, limit - 1))


def prepared(path, base, ingame):
    """A menu as the rules of theme.py make it, for a page that takes items from it (pages.EXTRA)."""
    tree = mf.parse(source(path, base))
    for top in tree:
        if top.k == 'menudef':
            pages.before(top.block, os.path.basename(path), ingame)
            theme.restyle(top.block, os.path.basename(path), ingame)
            return top.block
    return []


def build(path, base, ingame):
    name = os.path.basename(path)
    tree = mf.parse(source(path, base))
    for top in tree:
        if top.k == 'assetglobaldef':
            theme.assets(top.block)
        elif top.k == 'menudef':
            pages.HAD_LOGO[name] = any('jediacademy' in ' '.join(mf.get(e.block, 'background') or []).lower()
                                       for e in top.block if e.k == 'itemdef')
            pages.before(top.block, name, ingame)
            theme.restyle(top.block, name, ingame)
            pages.page(top.block, name, ingame)
    head = ('// %s in the new look. Made by tools/menus/build.py from ui/jamp/%s: change that script or its\n'
            '// rules, not this file. See docs/japlus/ui.md.\n' % (name, name))
    text = head + mf.dump(tree)
    check_scripts(name, text, script_limit())
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name), 'w', encoding='latin-1', newline='\n') as f:
        f.write(text)


def write_list(name, files, what):
    with open(os.path.join(UI, name), 'w', encoding='latin-1', newline='\n') as f:
        f.write('// the menus %s, in the new look (ui/vision). Made by tools/menus/build.py.\n{\n' % what)
        for path in files:
            f.write('\tloadMenu { "ui/vision/%s" }\n' % os.path.basename(path))
        f.write('}\n')


def main():
    pk3 = sys.argv[1] if len(sys.argv) > 1 else os.path.join(TREE, '..', 'GameData', 'base', 'assets1.pk3')
    if not os.path.exists(pk3):
        sys.exit('the menus of the game are read from its assets1.pk3, which is not at %s' % pk3)
    with zipfile.ZipFile(pk3) as base:
        outside, inside = read_list('jampmenus.txt'), read_list('jampingame.txt')
        for path in outside:
            if os.path.basename(path) in ('player2.menu', 'saber.menu'):
                pages.EXTRA[os.path.basename(path)] = prepared(path, base, False)
        for path in outside:
            build(path, base, False)
        for path in inside:
            if path not in outside:
                build(path, base, True)
    write_list('visionmenus.txt', outside, 'outside of a game')
    write_list('visioningame.txt', inside, 'in a game')
    # the texts the new menus add to the game, in its languages
    texts.write(os.path.join(TREE, 'assets', 'japlus_client', 'strings'))
    for english in sorted(texts.missing):
        print('no text in texts.py for %r: it stays English in every language' % english)
    print('%d menu files in %s' % (len(set(outside + inside)), os.path.relpath(OUT, TREE)))


if __name__ == '__main__':
    main()
