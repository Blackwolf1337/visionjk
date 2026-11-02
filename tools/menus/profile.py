"""The pages of the player in the new look: profile, character, lightsaber and (in a game) force powers
(tools/menus/build.py). They are menus of their own, as they were: what makes them one screen is a strip at the
top that leads from one to the other and marks the one that is open, the same places for the same things on each
(what is set at the left, what it looks like at the right), and the bar at the bottom.

Every item keeps its name, its group, its cvar, its feeder, its owner draw and its script: the code of the game
finds them by name and shows and hides them (the powers of a side, the buttons that join a game). What changes is
where an item is and how it is drawn. What the old pages needed to get from one to the other (a picture to click
for the custom character, the saber and the powers, a page of sliders behind a button) is gone: the strip and
rows that are always there stand in its place."""
import menufile as mf
from menufile import Entry
from theme import *         # noqa: F401,F403
import options
import texts
from options import ROW_H, CLICK, item, fill, panel_item, text_item, centered, name_of, group_of, cvar_of, items
from texts import ref

COLUMN = 316                # the settings of the force powers, at the left of their page
GAP = 12
LEFT_W = 250                # the redesign of 2026-11-01: what is set at the left, the figure in the middle, the rest
MIDDLE_W = 140              #	at the right, each on a panel of its own
STRIP_SCALE = 0.58

# the entries of the strip: (key, text, what for). The key says which menu an entry opens, by the table below.
ENTRIES = (('profile', '@MENUS_PROFILE', 'Name, model and team colour.'),
           ('character', 'Character', 'Build a character of your own.'),
           ('saber', '@MENUS_LIGHTSABER_CAPS', 'Hilt and blade of your lightsaber.'),
           ('force', '@MENUS_DP_FORCE', 'Your force powers.'),
           ('moves', '@MENUS_MOVES', 'The list of moves.'))
# file: (which entry it is, the menus the entries open from here)
SCREENS = {
    'player.menu': ('profile', {'character': 'playerMenu2', 'saber': 'saberMenu'}),
    'player2.menu': ('character', {'profile': 'playerMenu', 'saber': 'saberMenu'}),
    'saber.menu': ('saber', {'profile': 'playerMenu', 'character': 'playerMenu2'}),
    'ingame_player.menu': ('profile', {'character': 'ingame_player2', 'saber': 'ingame_saber', 'force': 'ingame_playerforce',
                                       'moves': 'rulesMenu_moves'}),
    'ingame_player2.menu': ('character', {'profile': 'ingame_player', 'saber': 'ingame_saber', 'force': 'ingame_playerforce',
                                          'moves': 'rulesMenu_moves'}),
    'ingame_saber.menu': ('saber', {'profile': 'ingame_player', 'character': 'ingame_player2', 'force': 'ingame_playerforce',
                                    'moves': 'rulesMenu_moves'}),
    'ingame_playerforce.menu': ('force', {'profile': 'ingame_player', 'character': 'ingame_player2', 'saber': 'ingame_saber',
                                          'moves': 'rulesMenu_moves'}),
    'ingame_moves.menu': ('moves', {'profile': 'ingame_player', 'character': 'ingame_player2', 'saber': 'ingame_saber',
                                    'force': 'ingame_playerforce'}),
}
ICONS = ('blue', 'green', 'orange', 'purple', 'yellow', 'red', 'black')


def named(old, *names):
    return [e for e in items(old) if name_of(e.block) in names]


def props(e, *pairs):
    for key, *args in pairs:
        mf.put(e.block, key, *args)
    return e


def rect(e, x, y, w, h):
    mf.put(e.block, 'rect', *[mf.fmt(v) for v in (x, y, w, h)])
    return e


def shown(e):
    mf.put(e.block, 'visible', 1)
    return e


def plain(e):
    """An item without the window the old page gave it."""
    for key in ('background', 'border', 'bordersize', 'bordercolor', 'backcolor', 'radius', 'shadow', 'sheen', 'mouseenter',
                'mouseexit', 'mouseentertext', 'mouseexittext', 'text2', 'text2alignx', 'text2aligny'):
        mf.drop(e.block, key)
    mf.put(e.block, 'style', 'WINDOW_STYLE_EMPTY')
    return e


def translate(e):
    """Its text, where this tree wrote it in English."""
    t = mf.find(e.block, 'text')
    if t and t.args and not t.args[0].startswith('@'):
        t.args = [texts.old('"%s"' % mf.unquote(' '.join(t.args)))]
    return e


def heading(e, x, y, w, text=None):
    """An item of the old page as the heading of a part, with the line under it. The line has the name of the
    heading, so what hides the one hides the other."""
    plain(e)
    if text:
        mf.put(e.block, 'text', text)
    mf.drop(e.block, 'type')
    mf.drop(e.block, 'action')
    rect(e, x + 2, y, w - 4, ROW_H)
    props(e, ('font', FONT), ('textscale', '0.54'), ('forecolor', *DIM), ('caps', 1), ('textalign', 'ITEM_ALIGN_LEFT'),
          ('textalignx', 0), ('textaligny', 9), ('decoration',), ('visible', 1))
    line = fill(name_of(e.block), (x, y + ROW_H - 1, w, 0.67), PANEL_EDGE)
    for key in ('group', 'cvartest', 'showcvar', 'hidecvar'):
        t = mf.find(e.block, key)
        if t:
            line.block.append(Entry(t.key, list(t.args), list(t.block) if t.block is not None else None))
    return [e, line]


def new_heading(name, text, x, y, w, *more):
    e = text_item(name, (x, y, w, ROW_H), text, 0.54, DIM, *more)
    return heading(e, x, y, w)


def row(e, x, y, w, kind=1, keep_color=False):
    """A setting or a button as a row (ui_shared.c, "row")."""
    plain(e)
    translate(e)
    rect(e, x, y, w, ROW_H)
    props(e, ('row', kind), ('font', FONT), ('textscale', mf.fmt(options.ROW_SCALE)), ('visible', 1))
    if not keep_color:
        mf.put(e.block, 'forecolor', *TEXT)
    for key in ('textalign', 'textalignx', 'textaligny'):
        mf.drop(e.block, key)
    return e


def text_row(e, x, y, w):
    """An item that writes its own value behind its text (an owner draw), in the place of a row, with a line under it."""
    plain(e)
    rect(e, x, y, w, ROW_H)
    props(e, ('font', FONT), ('textscale', mf.fmt(options.ROW_SCALE)), ('forecolor', *TEXT), ('textalign', 'ITEM_ALIGN_LEFT'),
          ('textalignx', 8), ('textaligny', mf.fmt((ROW_H - 1 - 18.0 * options.ROW_SCALE) / 2)), ('visible', 1))
    return e


def choice_row(e, x, y, w):
    """An owner draw that is a choice (team colour, jedi or not) as a row: its name at the left, the value in a field
    between two arrows at the right (ui_shared.c, Item_Row_Paint)."""
    return row(e, x, y, w)


def rule(x, y, w, *more, of=None):
    """The line under a row; with of= the item it belongs to, it is shown and hidden with it."""
    line = fill('vision_rule', (x, y + ROW_H - 1, w, 0.67), ['1', '1', '1', '0.07'], *more)
    if of is not None:
        for key in ('group', 'cvartest', 'showcvar', 'hidecvar'):
            t = mf.find(of.block, key)
            if t:
                line.block.append(Entry(t.key, list(t.args), list(t.block) if t.block is not None else None))
    return line


class Frame:
    """What every page of the player has: where its content is, the strip, the bar at the bottom."""

    def __init__(self, m, name, ingame, nav):
        self.m, self.name, self.ingame = m, name, ingame
        self.L = L = options.Layout(ingame)
        self.x, self.y, self.h = L.list[0], L.list[1], L.list[3]
        self.w = L.right - L.left
        if not ingame:
            # (outside a game the page is put onto the canvas at twice its units, canvas.profile_page: the columns are
            # laid out for a width of 752 units from 24, 1504 pixels from 48, and a height of 352 units)
            self.x, self.w = 24, 752
            # (and the panels, 6 units around the content, end as far over the bar at the bottom as they begin under
            # the strip: 12 pixels)
            self.h = 330
        self.px = self.x + COLUMN + GAP          # the pane beside the column (the force powers)
        self.pw = self.w - COLUMN - GAP
        # the three columns of the other pages
        self.lx, self.lw = self.x, LEFT_W
        self.cx, self.cw = self.x + LEFT_W + GAP, MIDDLE_W
        self.rx = self.cx + MIDDLE_W + GAP
        self.rw = self.x + self.w - self.rx
        self.here, self.targets = SCREENS[name]
        self.menu = mf.unquote(mf.get(m, 'name')[0])
        self.old = list(m)
        self.new = list(nav)
        self.last = []              # items that have to come after all others
        wide = (('wide', 1),) if not ingame else ()
        self.new += [fill('vision_strip', (0, L.strip_y, L.width, 26), options.BAND, *wide),
                     fill('vision_strip_line', (0, L.strip_y + 26, L.width, 0.67), BAR_LINE, *wide)]
        self.strip()

    def leave(self, target):
        """The script of an entry of the strip: this menu closes as its way back does, the other one opens."""
        words = list(CLICK)
        if self.menu.lower() == 'sabermenu':
            words += ['uiScript', '"updatesabercvars"', ';']        # (as leaving this page always did)
        return words + ['close', self.menu, ';', 'open', target, ';']

    def strip(self):
        L = self.L
        n = 0
        for key, text, about in ENTRIES:
            if key != self.here and key not in self.targets:
                continue
            r = (L.left - 3 + n * 80, L.strip_y, 80, 26)
            common = (('caps', 1), ('style', 'WINDOW_STYLE_EMPTY'), *centered(r, STRIP_SCALE))
            if key == self.here:
                e = text_item('vision_strip_here', r, options.word(text), STRIP_SCALE, DIM, ('flow', 3), ('decoration',), *common)
            else:
                e = text_item('vision_to_' + key, r, options.word(text), STRIP_SCALE, DIM, ('type', 'ITEM_TYPE_BUTTON'), ('flow', 2),
                              ('descText', ref(about)), ('action', self.leave(self.targets[key])), *common)
                if key == 'force':
                    # the code of the game hides what belongs to the force where a server has none
                    mf.put(e.block, 'group', 'yesforce')
            self.new.append(e)
            n += 1

    def pane(self):
        self.new.append(panel_item('vision_pane', (self.px, self.y, self.pw, self.h), SOFT, SOFT_EDGE, 4))

    def figure(self):
        """The character as it will look, as high as the pane and in its middle. (The view keeps its shape: the old
        pages gave it both of its angles.)"""
        for e in named(self.old, 'character'):
            w = self.h * 200.0 / 225.0
            rect(e, self.px + self.pw / 2 - w / 2, self.y, w, self.h)
            shown(e)
            self.new.append(e)

    def columns(self):
        """The panels of the three columns, and the character standing in the middle one on a soft light."""
        x, y, h = self.x, self.y, self.h
        self.new += [panel_item('vision_left', (self.lx - 6, y - 6, self.lw + 12, h + 12), PANEL, PANEL_EDGE, 5.3),
                     panel_item('vision_middle', (self.cx, y - 6, self.cw, h + 12), SOFT, SOFT_EDGE, 5.3),
                     panel_item('vision_right', (self.rx - 6, y - 6, self.rw + 12, h + 12), PANEL, PANEL_EDGE, 5.3),
                     item('vision_glow', (self.cx + 20, y + h - 22, self.cw - 40, 12), ('style', 'WINDOW_STYLE_PANEL'),
                          ('backcolor', *(ACCENT[:3] + ['0.07'])), ('radius', 6), ('glowcolor', *(ACCENT[:3] + ['0.14'])),
                          ('decoration',))]
        # (the figure is put after everything else, as the old page had it: drawn before the panels it would lie under
        # them, and it shows the model the list of heads chose in the same frame)
        for e in named(self.old, 'character'):
            rect(e, self.cx + 2, y, self.cw - 4, h - 8)
            shown(e)
            self.last.append(e)

    def foot(self, left, right, back=None):
        """The bar at the bottom: words at the left (a way back first, if the page gets one), blocks at the right."""
        L = self.L
        wide = (('wide', 1),) if not self.ingame else ()
        self.new += [fill('vision_foot', (0, L.bar_y, L.width, L.bar_h), options.BAND, *wide),
                     fill('vision_foot_line', (0, L.bar_y, L.width, 0.67), BAR_LINE, *wide)]
        x = L.left - 3
        if back:
            self.new.append(options.bar_button('vision_back', x, 72, '@MENUS_BACK', '@MENUS_BACK', back, L, ('flow', 1)))
            x += 72
        for n, e in enumerate(left):
            plain(e)
            r = (x + 72 * n, L.bar_y, 72, L.bar_h)
            rect(e, *r)
            props(e, ('font', FONT), ('textscale', '0.6'), ('forecolor', *TEXT), ('caps', 1), ('flow', 1), *centered(r, 0.6))
            self.new.append(e)
        for e in right:
            plain(e)
            r = (L.right - 132, L.bar_y + 6, 132, L.bar_h - 12)
            rect(e, *r)
            props(e, ('font', FONT), ('textscale', '0.6'), ('forecolor', *ON_ACCENT), ('caps', 1), ('style', 'WINDOW_STYLE_PANEL'),
                  ('backcolor', *ACCENT), ('bordercolor', *ACCENT), ('radius', 3.2),
                  *centered(r, 0.6))
            self.new.append(e)

    def done(self):
        m, L = self.m, self.L
        m[:] = [e for e in m if e.k != 'itemdef'] + self.new + self.last
        # what the mouse is on is said in the bar at the bottom, between its buttons
        mf.put(m, 'descX', mf.fmt(L.width / 2))
        mf.put(m, 'descY', mf.fmt(L.bar_y + (L.bar_h - 10) / 2))
        mf.put(m, 'descAlignment', 'ITEM_ALIGN_CENTER')
        mf.put(m, 'descScale', '0.55')
        mf.put(m, 'descColor', *DIM)
        if self.ingame:
            options.window_frame(m, L)


def without(words, commands):
    """A script without some of its commands. commands: name -> how many arguments it has."""
    out, i = [], 0
    while i < len(words):
        n = commands.get(words[i].lower().rstrip(';'))
        if words[i] == mf.NL or n is None or words[i].endswith(';'):
            out.append(words[i])
            i += 1
            continue
        closed = words[i + n].endswith(';') if i + n < len(words) else True
        i += 1 + n
        if not closed and i < len(words) and words[i] == ';':
            i += 1
    return out


def esc_words(m, fallback):
    e = mf.find(m, 'onesc')
    words = [w for w in (e.block if e and isinstance(e.block, list) else []) if w != mf.NL]
    return words or fallback


def profile(f):
    """The models to choose from at the left, the figure in the middle, the name, the team colour and the holsters at
    the right."""
    old, x, y, w = f.old, f.lx, f.y, f.lw
    f.columns()
    for e in named(old, 'modeltitle')[:1]:
        f.new += heading(e, x, y, w)
    for e in named(old, 'headlist'):
        rect(plain(e), x, y + 24, w, f.h - 24)
        props(e, ('elementwidth', 56), ('elementheight', 56), ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *INSET),
              ('bordercolor', *SOFT_EDGE), ('radius', 3.2))
        f.new.append(shown(e))
    rx, rw = f.rx, f.rw
    f.new += new_heading('vision_player', ref('Player'), rx, y, rw)
    for e in named(old, 'namefield'):
        f.new.append(row(e, rx, y + 22, rw, 3))
    for e in named(old, 'setcolor'):
        f.new.append(choice_row(e, rx, y + 44, rw))
    f.new.append(rule(rx, y + 44, rw))
    top = y + 66
    for e in named(old, 'jedinonjedi'):
        f.new.append(choice_row(e, rx, top, rw))
        f.new.append(rule(rx, top, rw, of=e))
        top += ROW_H
    holsters = named(old, 'holsterlist')
    if holsters:
        top += 6
        for e in named(old, 'title_holster')[:1]:
            f.new += heading(translate(e), rx, top, rw)
        for e in holsters:
            rect(plain(e), rx, top + 24, rw, 130)
            props(e, ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *INSET), ('bordercolor', *SOFT_EDGE), ('radius', 3.2))
            f.new.append(shown(e))
    if f.ingame:
        # the buttons that take what was set into the game: the code shows the ones that fit (apply, or join)
        f.foot(named(old, 'applyspectate', 'applyred', 'applyblue'), named(old, 'applycurrent', 'applyjoin'))
    else:
        f.foot([], named(old, 'next'), back=esc_words(f.m, CLICK))


def character(f):
    """The custom character: species and colour at the left, the figure in the middle, the three parts of its look at
    the right."""
    old, x, y, w = f.old, f.lx, f.y, f.lw
    f.columns()
    for e in named(old, 'speciesbut'):
        mf.put(e.block, 'text', '@MENUS_SPECIES')
        f.new.append(row(e, x, y, w, 3))
    for e in named(old, 'color')[:1]:
        f.new += heading(e, x, y + 26, w)
    for e in named(old, 'colorbox'):
        rect(e, x, y + 50, w, 48)
        f.new.append(shown(e))
    # the three parts of the colour: rows that are always there, in place of a second page behind a button
    for n, name in enumerate(('barrerouge', 'barrevert', 'barrebleu')):
        for e in named(old, name):
            f.new.append(row(e, x, y + 104 + n * ROW_H, w))
    rx, rw = f.rx, f.rw
    for e in named(old, 'appear')[:1]:
        f.new += heading(e, rx, y, rw)
    # head, torso and legs: three pages of this menu with a tab each (the old buttons showed and hid three groups
    # and moved the figure near and far, which it does not need in a pane of its own)
    tw = rw / 3
    for n, (but, box) in enumerate((('headbut', 'headlistbox'), ('torsobut', 'torsolistbox'), ('legsbut', 'lowerlistbox'))):
        for e in named(old, but):
            plain(e)
            r = (rx + n * tw, y + 24, tw, ROW_H)
            rect(e, *r)
            act = mf.find(e.block, 'action')
            act.block = [w_ for w_ in act.block if w_ == mf.NL or w_.lower().startswith(('play', '"sound'))]
            props(e, ('tab', n + 1), ('font', FONT), ('textscale', mf.fmt(STRIP_SCALE)), ('forecolor', *DIM), ('caps', 1),
                  ('visible', 1), *centered(r, STRIP_SCALE))
            f.new.append(e)
        for e in named(old, box):
            # (a grid of all the pictures in the pane, not the strip of one row the old page had)
            rect(e, rx, y + 52, rw, f.h - 52)
            mf.drop(e.block, 'horizontalscroll')
            props(e, ('page', n + 1), ('visible', 1), ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *INSET),
                  ('bordercolor', *SOFT_EDGE), ('radius', 3.2))
            f.new.append(e)
    if f.ingame:
        f.foot(named(old, 'backmenu_button'), named(old, 'applycurrent'))
    else:
        f.foot([], named(old, 'next'), back=esc_words(f.m, CLICK))


def saber(f):
    """The lightsaber: its type and hilt at the left, its blade at the right, and the saber itself under both."""
    old, x, y = f.old, f.x, f.y
    lw = 270
    rx = x + lw + 20
    rw = f.w - lw - 20
    # The old page hid half of itself behind a button (a page of sliders for the colour) and moved the saber up
    # and down to make room for boxes: its scripts showed, hid and moved things by name. Nothing is hidden here
    # and the saber has a place of its own, so those commands go. What marks the type that is set stays.
    gone = {'show': 1, 'hide': 1, 'transition2': 7}
    opens = mf.find(f.m, 'onopen')
    if opens and isinstance(opens.block, list):
        opens.block = without(opens.block, gone)
    for e in named(old, 'typebut_single', 'typebut_dual', 'typebut_staff'):
        act = mf.find(e.block, 'action')
        act.block = without(act.block, gone)
    # (the columns on panels as the other pages have them; the saber lies under both)
    # (both as high as the second blade of two sabers needs, its last row ends at y + 280: the list of the second
    # hilt ends at y + 254)
    f.new += [panel_item('vision_left', (x - 6, y - 6, lw + 12, 296), PANEL, PANEL_EDGE, 5.3),
              panel_item('vision_right', (rx - 6, y - 6, rw + 12, 296), PANEL, PANEL_EDGE, 5.3)]
    for e in named(old, 'typebut')[:1]:
        f.new += heading(e, x, y, lw)
    for n, name in enumerate(('typebut_single', 'typebut_dual', 'typebut_staff')):
        for e in named(old, name):
            # (its colour says which type is set: the scripts of the three give and take it)
            f.new.append(row(e, x, y + 22 + n * ROW_H, lw, keep_color=True))
    for e in named(old, 'hilttype'):
        second = (mf.get(e.block, 'text') or [''])[0].upper().endswith('HILT2')
        f.new += heading(e, x, y + (176 if second else 94), lw)
    for name, top, high in (('hiltbut', 118, 116), ('hiltbut_staves', 118, 116), ('hiltbut1', 118, 54), ('hiltbut2', 200, 54)):
        for e in named(old, name):
            rect(e, x, y + top, lw, high)
            f.new.append(e)

    # the blade(s): seven colours to click, or one of your own in three parts with the style of its core
    def blade(top, second):
        test = (('cvarTest', 'ui_saber_type'),) if second else ()
        made = new_heading('vision_blade2' if second else 'vision_blade', '@MENUS_COLOR2' if second else '@MENUS_BLADE_COLOR',
                           rx, top, rw, *test)
        if second:
            for e in made:
                e.block.append(Entry('hideCvar', [], ['"single"', ';', '"staff"']))
        f.new.extend(made)
        for e in items(old):
            b = e.block
            n = name_of(b)
            color = n[:-5] if n.endswith('icon2') else n[:-4] if n.endswith('icon') else None
            if color in ICONS and n.endswith('icon2') == second:
                rect(e, rx + 2 + ICONS.index(color) * 27, top + 24, 22, 22)
                for key in ('mouseenter', 'mouseexit'):
                    mf.drop(b, key)
                f.new.append(e)
            cv = cvar_of(b)
            if cv == ('ui_saber2_color' if second else 'ui_saber_color') and mf.get(b, 'type', [''])[0] == 'ITEM_TYPE_MULTI':
                mf.put(b, 'group', 'vision_rgb')
                # (a blade in one of the seven colours has no style: the row says so, not "Custom". A slider that
                # is moved gives it the plain style, UI_SaberMixFrame() of the ui.)
                mf.put(b, 'otherText', texts.ref('Fixed Color'))
                f.new.append(row(e, rx, top + 50, rw))
            for i, part in enumerate(('red', 'green', 'blue')):
                if cv == 'ui_sb%s%d' % (part, 2 if second else 1) and mf.get(b, 'type', [''])[0] == 'ITEM_TYPE_SLIDER':
                    mf.put(b, 'group', 'vision_rgb')
                    f.new.append(row(e, rx, top + 72 + i * ROW_H, rw))

    blade(y, False)
    blade(y + 142, True)
    # the saber lies under the two columns, over the width of the page (the ui frames all of it in the width of its
    # view, ui_shared.c Item_Model_Paint; the second one of two lies a little lower)
    for n, name in enumerate(('saber', 'saber2')):
        for e in named(old, name):
            rect(e, x, y + 280 + n * 20, f.w, 60)
            props(e, ('model_fovx', 70), ('model_fovy', 14))
            f.new.append(e)
    if f.ingame:
        f.foot([], named(old, 'applycurrent'), back=CLICK + ['close', f.menu, ';', 'open', 'ingame_player', ';'])
    else:
        esc = mf.find(f.m, 'onesc')
        if esc and isinstance(esc.block, list):
            # (the page this way back names is one of the single player game)
            esc.block = ['playerMenu' if w_.lower() == 'charactermenu' else w_ for w_ in esc.block]
        f.foot([], named(old, 'newgame_begin'), back=esc_words(f.m, CLICK))


def force(f):
    """The force powers: rank, side and points and the stored sets at the left, the powers at the right."""
    old, x, y, w = f.old, f.x, f.y, COLUMN
    f.pane()
    n = 0
    for e in items(old):
        b = e.block
        draw = (mf.get(b, 'ownerdraw') or [''])[0]
        if draw in ('UI_FORCE_RANK', 'UI_FORCE_SIDE', 'UI_FORCE_POINTS'):
            # (the side is a choice: a row with arrows; rank and points are only shown)
            made = choice_row(e, x, y + n * ROW_H, w) if draw == 'UI_FORCE_SIDE' else text_row(e, x, y + n * ROW_H, w)
            f.new += [made, rule(x, y + n * ROW_H, w)]
            n += 1
    for e in [e for e in named(old, 'fcflist') if mf.get(e.block, 'type') is None][:1]:
        f.new += heading(e, x, y + 70, w)
    for e in [e for e in named(old, 'fcflist') if mf.get(e.block, 'type') is not None]:
        rect(e, x, y + 94, w, 140)
        f.new.append(e)
    for e in named(old, 'namefield'):
        f.new.append(row(e, x, y + 240, w, 3))
    for e in named(old, 'templatesave'):
        f.new.append(row(e, x, y + 262, w))
    for e in named(old, 'templatesavetext'):
        rect(plain(e), x + 8, y + 288, w - 16, 16)
        props(e, ('font', FONT), ('textscale', '0.55'), ('forecolor', *WARM), ('textalign', 'ITEM_ALIGN_LEFT'), ('textalignx', 0),
              ('textaligny', 2))
        f.new.append(e)
    # the powers keep their places among each other and move into the pane as one block, a little further apart
    for e in items(old):
        b = e.block
        r = mf.get(b, 'rect')
        nm = name_of(b)
        if not r or not (nm.startswith('setfp_') or nm in ('forceranktitle', 'forceranktitle2', 'nosaber')):
            continue
        ox, oy, ow, oh = (mf.num(v) or 0 for v in r[:4])
        rect(e, f.px + (f.pw - 175) / 2 + ox - 230, y + 12 + (oy - 112) * 1.22, ow, oh)
        f.new.append(e)
    f.foot([], named(old, 'applyjoin'), back=CLICK + ['close', f.menu, ';', 'open', 'ingame_player', ';'])


def moves(f):
    """The moves in a game: the kind of moves and their list at the left, the figure doing the chosen one in the pane
    with what it is about under it."""
    old, x, y = f.old, f.x, f.y
    for e in named(old, 'move_titles_title')[:1]:
        f.new += heading(e, x, y, COLUMN)
    for e in named(old, 'move_titles'):
        # (a row without a name is its widget alone: the arrows and the kind of moves)
        mf.put(e.block, 'text', '""')
        f.new.append(row(e, x, y + 22, COLUMN))
    y2 = y + 22 + ROW_H + 10
    for e in named(old, 'moveslist_title')[:1]:
        f.new += heading(e, x, y2, COLUMN)
    for e in named(old, 'moveslist'):
        rect(plain(e), x, y2 + 24, COLUMN, f.y + f.h - (y2 + 24))
        props(e, ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *INSET), ('bordercolor', *SOFT_EDGE), ('radius', 3.2),
              ('elementheight', 18), ('font', FONT), ('textscale', '0.62'), ('textaligny', 2), ('visible', 1))
        f.new.append(e)
    f.pane()
    fig_h = f.h - 70
    for e in named(old, 'character'):
        w = fig_h * 0.9
        rect(e, f.px + f.pw / 2 - w / 2, y + 4, w, fig_h)
        shown(e)
        f.last.append(e)
    for e in named(old, 'item_desc'):
        rect(plain(e), f.px + 10, y + f.h - 62, f.pw - 20, 58)
        props(e, ('font', FONT), ('textscale', '0.6'), ('forecolor', *TEXT), ('visible', 1))
        f.new.append(e)
    # (the old menu closed a menu of another name on Esc: this one stayed open)
    esc = mf.find(f.m, 'onesc')
    if esc and isinstance(esc.block, list):
        esc.block = [f.menu if w_.strip('"').lower() == 'ingame_moves' else w_ for w_ in esc.block]
    f.foot([], [], back=CLICK + ['close', f.menu, ';', 'open', 'ingame_player', ';'])


PAGES = {'profile': profile, 'character': character, 'saber': saber, 'force': force, 'moves': moves}


def build(m, name, ingame, nav):
    f = Frame(m, name, ingame, nav)
    PAGES[f.here](f)
    f.done()
