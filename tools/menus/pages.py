"""What single pages get beyond the rules of theme.py (tools/menus/build.py): the frame of the pages outside a
game (a bar at the top with the four sections, a title, the buttons at the bottom), and the pages that are laid
out anew."""
import os

import menufile as mf
import options
import profile
import texts
from menufile import Entry
from theme import *         # noqa: F401,F403

NAV = ('newgamebutton', 'playbutton', 'profilebutton', 'controlsbutton', 'setupbutton')
NAV_X = {'newgamebutton': 330, 'playbutton': 330, 'profilebutton': 404, 'controlsbutton': 478, 'setupbutton': 552}
NAV_W = 72
SHIFT = -48                 # the old pages began under a logo of 120 units


def items(m):
    return [e for e in m if e.k == 'itemdef']


def name_of(b):
    n = mf.get(b, 'name')
    return mf.unquote(n[0]).lower() if n else ''


def rect_of(b):
    r = mf.get(b, 'rect')
    v = [mf.num(a) for a in r[:4]] if r and len(r) >= 4 else None
    return None if not v or None in v else v


def set_rect(b, x, y, w, h):
    mf.put(b, 'rect', mf.fmt(x), mf.fmt(y), mf.fmt(w), mf.fmt(h))


def item(name, rect, *props):
    """A new itemDef: decoration unless its properties say otherwise."""
    b = [Entry('name', [name]), Entry('rect', [mf.fmt(v) for v in rect]), Entry('visible', ['1'])]
    for key, *args in props:
        b.append(Entry(key, [str(a) for a in args]))
    return Entry('itemDef', [], b)


def label(name, rect, text, scale, color, *more):
    return item(name, rect, ('text', text), ('font', FONT), ('textscale', mf.fmt(scale)), ('forecolor', *color),
                ('decoration',), *more)


def box(name, rect, fill=PANEL, edge=PANEL_EDGE, radius=8, shadow=12, sheen='0.08'):
    return item(name, rect, ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *fill), ('bordercolor', *edge),
                ('radius', radius), ('shadow', shadow), ('sheen', sheen), ('decoration',))


def text_left(b, x_pad, scale, y_drop):
    mf.put(b, 'font', FONT)
    mf.put(b, 'textscale', mf.fmt(scale))
    mf.put(b, 'textalign', 'ITEM_ALIGN_LEFT')
    mf.put(b, 'textalignx', mf.fmt(x_pad))
    mf.put(b, 'textaligny', mf.fmt(y_drop))


def text_center(b, scale, y_drop):
    r = rect_of(b)
    mf.put(b, 'font', FONT)
    mf.put(b, 'textscale', mf.fmt(scale))
    mf.put(b, 'textalign', 'ITEM_ALIGN_CENTER')
    mf.put(b, 'textalignx', mf.fmt(r[2] / 2))
    mf.put(b, 'textaligny', mf.fmt(y_drop))


def top_bar(m, here):
    """The bar at the top of a page outside a game: the name of the game at the left, the four sections at the
    right, the one that is open marked. Returns what it adds, to be put in front of the other items."""
    new = [
        item('vision_bar', (0, 0, 640, 34), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', '0.02', '0.04', '0.08', '0.55'),
             ('wide', 1), ('decoration',)),
        item('vision_bar_line', (0, 34, 640, 0.67), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *(ACCENT[:3] + ['0.22'])),
             ('wide', 1), ('decoration',)),
        item('vision_logo', (22, 6, 22, 22), ('type', 'ITEM_TYPE_LOGO'), ('forecolor', '1', '1', '1', '1'), ('decoration',)),
        label('vision_mark', (50, 7, 120, 20), '"VISIONJK"', 1.0, BRIGHT),
        label('vision_mark2', (138, 12, 160, 14), '"JEDI ACADEMY"', 0.5, DIM),
    ]
    for e in items(m):
        b = e.block
        n = name_of(b)
        if n in NAV:
            set_rect(b, NAV_X[n], 7, NAV_W, 20)
            text_center(b, 0.66, 4)
            mf.put(b, 'caps', 1)
            if NAV_X[n] == NAV_X.get(here):
                mf.put(b, 'forecolor', *ACCENT)
                new.append(item('vision_here', (NAV_X[n] + 10, 31, NAV_W - 20, 2), ('style', 'WINDOW_STYLE_PANEL'),
                                ('backcolor', *ACCENT), ('radius', 1), ('decoration',)))
            else:
                mf.put(b, 'forecolor', *TEXT)
    return new


FOOT = ['0.02', '0.04', '0.08', '0.50']         # the bar at the bottom of a page, and the row of tabs of an options screen


def bottom_button(b, primary=False):
    """A button of the bar at the bottom of a page: its word on the bar, and the one that goes on as a filled block."""
    r = rect_of(b)
    mf.put(b, 'font', FONT)
    mf.put(b, 'textscale', '0.6')
    mf.put(b, 'textalign', 'ITEM_ALIGN_CENTER')
    mf.put(b, 'textalignx', mf.fmt(r[2] / 2))
    mf.put(b, 'textaligny', mf.fmt((r[3] - 18.0 * 0.6) / 2))
    mf.put(b, 'caps', 1)
    if primary:
        mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
        mf.put(b, 'backcolor', *(ACCENT[:3] + ['0.22']))
        mf.put(b, 'bordercolor', *(ACCENT[:3] + ['0.8']))
        mf.put(b, 'radius', 3)
        mf.put(b, 'forecolor', *BRIGHT)
    else:
        mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
        mf.put(b, 'forecolor', *TEXT)


SECTION = {'main': None, 'multiplayermenu': 'newgamebutton', 'joinserver': 'newgamebutton', 'createserver': 'newgamebutton',
           'quickgame': 'newgamebutton', 'quickgame2': 'newgamebutton', 'demo': 'newgamebutton',
           'rulesmenu': 'newgamebutton', 'rulesmenu_games': 'newgamebutton', 'rulesmenu_force': 'newgamebutton',
           'rulesmenu_weapons': 'newgamebutton', 'rulesmenu_items': 'newgamebutton', 'rulesmenu_moves': 'newgamebutton',
           'playermenu': 'profilebutton', 'playermenu2': 'profilebutton', 'sabermenu': 'profilebutton',
           'controlsmenu': 'controlsbutton', 'setup_menu': 'setupbutton', 'quitmenu': None}

# the heading of a page: the item of the old page that had it, or a text for a page that had none
TITLE = {'setup_menu': 'setup_title', 'controlsmenu': 'control_title', 'multiplayermenu': 'title', 'quitmenu': 'quit_title',
         'playermenu': '@MENUS_PROFILE', 'demo': 'default_rulesheader', 'rulesmenu': 'default_rulesheader',
         'rulesmenu_games': 'game_rulesheader', 'rulesmenu_force': 'force_rulesheader',
         'rulesmenu_weapons': 'weapon_rulesheader', 'rulesmenu_items': 'item_rulesheader',
         'rulesmenu_moves': 'game_rulesheader', 'createserver': 'create_title', 'joinserver': 'join_title',
         'playermenu2': 'creation_title', 'sabermenu': 'creation_title', 'quickgame': 'title', 'quickgame2': 'title'}
PRIMARY = ('applybutton', 'apply', 'joinbutton', 'join_button', 'join', 'beginbutton', 'begin', 'acceptbutton', 'next',
           'next_button', 'newgame_begin', 'startserver', 'startbutton', 'advanced_begin', 'quitgame_yes')
# what a button says that goes on with what the window is for
PRIMARY_TEXT = ('@menus_ok', '@menus_apply', '@menus_yes', '@menus_join', '@menus_accept', '@menus_begin', '@menus_next',
                '@menus_save', '"apply', '"ok', '"yes', '"join')

# the four sections of the bar at the top, for a page that has no buttons for them
NAV_MAKE = (('newgamebutton', '@MENUS_PLAY', '@MENUS_START_PLAYING_NOW', 'multiplayermenu'),
            ('profilebutton', '@MENUS_PROFILE', '@MENUS_PROFILE_DESC', 'playerMenu'),
            ('controlsbutton', '@MENUS_CONTROLS2', '@MENUS_CONFIGURE_GAME_CONTROLS', 'controlsMenu'),
            ('setupbutton', '@MENUS_SETUP', '@MENUS_CONFIGURE_GAME_SETTINGS', 'setup_menu'))

TOP = 100                   # where the content of a page begins under its heading,
TOP_DENSE = 42              #	and on a page that needs all of the height: right under the bar
FLOOR = 438                 # and where it ends, over the buttons at the bottom
WINDOW = ['0.035', '0.07', '0.12', '0.94']      # a window on a page or on the level: nearly solid, its text has to be read


def group_of(b):
    return (mf.get(b, 'group') or [''])[0].lower()


def hidden(b):
    v = mf.get(b, 'visible')
    return bool(v and v[0].lower() in ('0', 'menu_false'))


def clickable(b):
    return mf.get(b, 'text') is not None and mf.find(b, 'action') is not None


def is_primary(b):
    t = (mf.get(b, 'text') or [''])[0].lower()
    return name_of(b) in PRIMARY or t.startswith(PRIMARY_TEXT)


def nav_button(name, text, desc, target):
    return Entry('itemDef', [], [
        Entry('name', [name]), Entry('group', ['toprow']), Entry('style', ['WINDOW_STYLE_EMPTY']), Entry('type', ['ITEM_TYPE_BUTTON']),
        Entry('rect', ['0', '0', '72', '20']), Entry('text', [text]), Entry('descText', [desc]), Entry('visible', ['1']),
        Entry('action', [], ['play', '"sound/interface/button1.wav"', ';', mf.NL, 'close', 'all', ';', mf.NL, 'open', target, ';'])])


def find_rails(m, floor=430):
    """The columns of buttons of a menu: four or more that can be seen, one under the other, as wide as each other
    and as far apart. The sections of a page (Setup, Controls, the rules) and plain lists of choices."""
    cols = {}
    for e in items(m):
        b = e.block
        r = rect_of(b)
        n = name_of(b)
        if not r or not clickable(b) or hidden(b) or n in NAV or n.startswith('vision_') or r[1] >= floor or r[3] > 40 \
                or kind_of(b) not in ('button', '1'):
            continue
        cols.setdefault((round(r[0]), round(r[2])), []).append(e)
    out = []
    for col in cols.values():
        col.sort(key=lambda e: rect_of(e.block)[1])
        ys = [rect_of(e.block)[1] for e in col]
        gaps = [b - a for a, b in zip(ys, ys[1:])]
        if len(col) >= 4 and min(gaps) > 0 and max(gaps) - min(gaps) <= 3:
            out.append(col)
    return out


def rail(m, col, pad=5):
    """A column of buttons becomes a list on a panel of its own: its texts begin at the left, one under the other."""
    rs = [rect_of(e.block) for e in col]
    x, w = rs[0][0], rs[0][2]
    y0, y1 = rs[0][1], rs[-1][1] + rs[-1][3]
    for e in col:
        b = e.block
        mf.put(b, 'textalign', 'ITEM_ALIGN_LEFT')
        mf.put(b, 'textalignx', 12)
        mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
    at = next(i for i, e in enumerate(m) if e is col[0])
    mine = box('vision_rail_%d' % round(y0), (x - pad, y0 - pad, w + 2 * pad, y1 - y0 + 2 * pad), SOFT, SOFT_EDGE, 6, 8, '0.06')
    m.insert(at, mine)
    # the panel beside the list begins and ends where the list does
    rx, ry, rw, rh = rect_of(mine.block)
    top, bottom, beside = ry, ry + rh, []
    for e in items(m):
        b = e.block
        r = rect_of(b)
        if e is mine or not r or name_of(b).startswith('vision_') or mf.get(b, 'text') is not None or clickable(b) \
                or (mf.get(b, 'style') or [''])[0] != 'WINDOW_STYLE_PANEL' or kind_of(b) in ('list', 'scroll') or r[3] < 60:
            continue
        over = min(r[1] + r[3], ry + rh) - max(r[1], ry)
        if rx + rw - 2 <= r[0] <= rx + rw + 30 and over >= 0.5 * min(r[3], rh):
            beside.append(b)
            top, bottom = min(top, r[1]), max(bottom, r[1] + r[3])
    for b in beside + [mine.block]:
        r = rect_of(b)
        set_rect(b, r[0], top, r[2], bottom - top)


def content_box(m, floor=430):
    """The items of a page that are its content, and the box around them: not the bar at the top, not the heading,
    not the buttons at the bottom, not what lies behind everything."""
    out, xs, ys = [], [], []
    for e in items(m):
        b = e.block
        r = rect_of(b)
        n = name_of(b)
        if not r or n in NAV or (n.startswith('vision_') and not n.startswith('vision_rail')) or n == 'creditsbutton':
            continue
        if r[1] >= floor and clickable(b):
            continue
        out.append(e)
        if r[2] >= 630 or r[0] < -50 or r[2] <= 0 or r[3] <= 0 or r[1] < -20 or r[1] + r[3] > 480:
            continue                # behind everything, or parked outside the screen
        xs += [r[0], r[0] + r[2]]
        ys += [r[1], r[1] + r[3]]
    return out, (min(xs), min(ys), max(xs), max(ys)) if xs else None


def shell(m, menu, had_logo):
    """The frame of a page outside a game: the bar at the top with the four sections, the heading of the page under
    it, the content under that and in the middle, the buttons in a row at the bottom."""
    have = {name_of(e.block) for e in items(m)}
    if not have & set(NAV):
        m.extend(nav_button(*n) for n in NAV_MAKE)
    new = top_bar(m, SECTION.get(menu))

    # the heading: an item of the old page, or a text
    title = TITLE.get(menu, '')
    head = None
    if title.startswith('@'):
        head = label('vision_title', (40, 46, 400, 28), title, 1.45, BRIGHT, ('textaligny', 2), ('caps', 1))
        new.append(head)
    else:
        head = next((e for e in items(m) if name_of(e.block) == title and mf.get(e.block, 'text') is not None), None)

    # the buttons at the bottom
    bottom = [e for e in items(m) if rect_of(e.block) and rect_of(e.block)[1] >= 430 and clickable(e.block)
              and name_of(e.block) not in NAV]
    for col in find_rails(m):
        rail(m, col)
    content, bb = content_box(m)
    if head is not None and head in content:
        content.remove(head)
        _, bb = content_box([e for e in m if e is not head])

    dense = False
    if bb:
        # the content under the heading where it fits, right under the bar where the page needs its height
        dense = bb[3] - bb[1] > FLOOR - TOP
        dy = (TOP_DENSE if dense else TOP) - bb[1]
        if dense and bb[3] + dy > 446:
            dy = 446 - bb[3]
        # its left edge under the left edge of the heading, a page as wide as the screen in the middle
        if bb[2] - bb[0] <= 560:
            dx = 40 - bb[0]
        else:
            dx = round(320 - (bb[0] + bb[2]) / 2)
        for e in content:
            r = rect_of(e.block)
            set_rect(e.block, r[0] + dx, r[1] + dy, r[2], r[3])
        bb = (bb[0] + dx, bb[1] + dy, bb[2] + dx, bb[3] + dy)

    if head is not None and not title.startswith('@'):
        b = head.block
        if dense:
            # no room for a heading: its words stand in the bar, after the name of the game
            for e in new:
                if name_of(e.block) == 'vision_mark2':
                    mf.put(e.block, 'text', *mf.get(b, 'text'))
                    mf.put(e.block, 'caps', 1)
            m.remove(head)
        else:
            set_rect(b, 40, 46, 400, 28)
            text_left(b, 0, 1.45, 2)
            mf.put(b, 'forecolor', *BRIGHT)
            mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
            mf.put(b, 'caps', 1)
            mf.drop(b, 'background')
    # a page whose content is wider than the column of the heading: the heading begins where the content does
    head_x = 40 if not bb or dense else min(40, bb[0] + 2)
    if head is not None and head_x != 40 and (head in m or head in new):
        r = rect_of(head.block)
        set_rect(head.block, head_x, r[1], r[2], r[3])

    # the row at the bottom: what goes back at the left, what goes on at the right. (A page that needs all of its
    # height has no button that leaves the game: the first page has one, and the line about what the mouse is on
    # stands in its place.)
    row_y = 446 if not bb or bb[3] <= 440 else min(456, bb[3] + 5)
    row_h = 480 - row_y
    if dense:
        for e in [e for e in bottom if name_of(e.block) == 'exitgamebutton']:
            bottom.remove(e)
            m.remove(e)
    left = sorted((e for e in bottom if not is_primary(e.block)), key=lambda e: rect_of(e.block)[0])
    right = sorted((e for e in bottom if is_primary(e.block)), key=lambda e: -rect_of(e.block)[0])
    # a bar over the width of the screen: its buttons at the left as words with a line between them, the ones that
    # go on at the right as blocks
    foot = [item('vision_foot', (0, row_y, 640, row_h), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *FOOT), ('wide', 1),
                 ('decoration',)),
            item('vision_foot_line', (0, row_y, 640, 0.67), ('style', 'WINDOW_STYLE_FILLED'),
                 ('backcolor', *(ACCENT[:3] + ['0.18'])), ('wide', 1), ('decoration',))]
    # (The words at the left are as wide as they are in the language of the player and follow each other: "flow",
    # Menu_TabsLayout in ui_shared.c. The first begins where the content of a page does. Buttons that lie on each
    # other take turns: the one that is hidden takes no room.)
    for n, e in enumerate(left):
        set_rect(e.block, 29 + 90 * n, row_y, 90, row_h)
        bottom_button(e.block)
        mf.put(e.block, 'flow', 1)
    seen = {}
    for e in right:
        old = round(rect_of(e.block)[0])
        x = seen.setdefault(old, 486 - 130 * len(seen))
        set_rect(e.block, x, row_y + 6, 122, row_h - 12)
        bottom_button(e.block, primary=True)
    bottom = left + right
    # (the bar lies on everything: a figure of the page may reach down to it)
    for e in bottom:
        m.remove(e)
    m.extend(foot + bottom)
    left_end = 29 + 90 * len(left)
    # what the mouse is on is explained under the heading, on a page without one between the buttons
    gap0 = max(left_end, 40)
    gap1 = min([rect_of(e.block)[0] for e in right] or [608])
    mf.put(m, 'descX', mf.fmt(head_x) if not dense else mf.fmt((gap0 + gap1) / 2))
    mf.put(m, 'descY', 80 if not dense else mf.fmt(row_y + (row_h - 10) / 2))
    mf.put(m, 'descAlignment', 'ITEM_ALIGN_LEFT' if not dense else 'ITEM_ALIGN_CENTER')
    mf.put(m, 'descScale', '0.8' if not dense else '0.55')
    first = next((i for i, e in enumerate(m) if e.k == 'itemdef'), len(m))
    m[first:first] = new


def window_button(b, primary):
    """A button at the bottom of a window: a pill around its text."""
    r = rect_of(b)
    w = max(70, min(r[2], 110))
    set_rect(b, r[0] + r[2] / 2 - w / 2, r[1] + r[3] / 2 - 10, w, 20)
    text_center(b, 0.64, 4)
    mf.put(b, 'caps', 1)
    mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
    mf.put(b, 'radius', 10)
    if primary:
        mf.put(b, 'backcolor', *(ACCENT[:3] + ['0.20']))
        mf.put(b, 'bordercolor', *(ACCENT[:3] + ['0.75']))
        mf.put(b, 'forecolor', *BRIGHT)
    else:
        mf.put(b, 'backcolor', '1', '1', '1', '0.05')
        mf.put(b, 'bordercolor', *SOFT_EDGE)
        mf.put(b, 'forecolor', *TEXT)


def window(m, menu, ingame):
    """A menu that is a window on a page or on the level: one frame around it that hides what is behind it, its
    columns of buttons as lists, the buttons at its bottom as pills, and under it the line about what the mouse
    is on. A window on a page darkens the page."""
    mr = rect_of(m)
    if not mr or mr[2] <= 0 or mr[3] <= 0:
        return
    its = items(m)
    # the frame of the old window: an item with nothing in it that covers most of the menu
    frame = None
    for e in its[:4]:
        b = e.block
        r = rect_of(b)
        if r and mf.get(b, 'text') is None and mf.get(b, 'cvar') is None and not mf.find(b, 'action') \
                and kind_of(b) in ('', 'text') and (mf.get(b, 'style') or [''])[0] == 'WINDOW_STYLE_PANEL' \
                and r[2] * r[3] >= 0.45 * mr[2] * mr[3]:
            frame = e
            break
    own = (mf.get(m, 'style') or [''])[0] == 'WINDOW_STYLE_PANEL'
    if not frame and not own:
        return                      # no window: a row of buttons, a notice
    for col in find_rails(m, floor=10000):
        rail(m, col, pad=3)
    in_rail = {id(e) for col in find_rails(m, floor=10000) for e in col}
    # The frame goes around what is in the window, with some air: the pictures of the old frames were larger than
    # that or lay beside it. (A text that fills what room there is counts with its first lines.)
    xs, ys, plain = [], [], False
    for e in items(m):
        b = e.block
        r = rect_of(b)
        if e is frame or not r or r[2] <= 0 or r[3] <= 0 or name_of(b).startswith('vision_rail'):
            continue
        k = kind_of(b)
        shown = mf.get(b, 'text') is not None or mf.get(b, 'cvar') is not None or k in ('list', 'scroll', 'model', 'owner') \
            or (mf.get(b, 'style') or [''])[0] in ('WINDOW_STYLE_PANEL', 'WINDOW_STYLE_SHADER', '3')
        if not shown:
            continue
        h = r[3] if k in ('list', 'scroll', 'model', 'owner') or r[3] <= 60 or not (mf.get(b, 'text') or mf.get(b, 'cvar')) else 120
        xs += [r[0], r[0] + r[2]]
        ys += [r[1], r[1] + h]
        plain = plain or not clickable(b)
    if not xs:
        return
    x0, y0, x1, y1 = min(xs) - 10, min(ys) - 10, max(xs) + 10, max(ys) + 10
    if frame:
        # (no wider than the old frame where that one was the narrower: a title often has a line as wide as the menu)
        r = rect_of(frame.block)
        x0, x1 = max(x0, min(r[0], min(xs) - 4)), min(x1, max(r[0] + r[2], max(xs) + 4))
    x0, y0 = max(x0, 0), max(y0, 0)
    # The pills: the buttons in the lowest row of buttons, in a window that has more in it than buttons. Buttons one
    # under the other are a list of things to do and stay rows of it.
    low = [e for e in items(m) if rect_of(e.block) and clickable(e.block) and id(e) not in in_rail and rect_of(e.block)[3] <= 34
           and rect_of(e.block)[2] <= 200 and kind_of(e.block) in ('button', '', 'text', '1')]
    if plain and low:
        floor = max(rect_of(e.block)[1] + rect_of(e.block)[3] for e in low)
        row = [e for e in low if rect_of(e.block)[1] + rect_of(e.block)[3] >= floor - 10]
        above = [e for e in low if e not in row and floor - 44 <= rect_of(e.block)[1] + rect_of(e.block)[3]]
        if not above and floor >= max(ys) - 70:
            for e in row:
                window_button(e.block, is_primary(e.block))
                y1 = max(y1, rect_of(e.block)[1] + 20 + 9)
    if frame:
        set_rect(frame.block, x0, y0, x1 - x0, y1 - y0)
        panel(frame.block, WINDOW, PANEL_EDGE, 8, 14)
        mf.put(frame.block, 'sheen', '0.08')
        mf.put(frame.block, 'decoration')
        for key in ('backcolor', 'bordercolor', 'radius', 'shadow', 'sheen', 'border', 'bordersize'):
            mf.drop(m, key)
        mf.put(m, 'style', 'WINDOW_STYLE_EMPTY')
    else:
        mf.put(m, 'backcolor', *WINDOW)
    # (a click outside the rectangle of the menu is none on it: the rectangle follows the frame)
    mf.put(m, 'rect', mf.fmt(mr[0]), mf.fmt(mr[1]), mf.fmt(max(mr[2], x1)), mf.fmt(max(mr[3], y1)))
    under = mr[1] + y1 + 5
    mf.put(m, 'descX', mf.fmt(mr[0] + (x0 + x1) / 2))
    mf.put(m, 'descY', mf.fmt(under if under <= 462 else mr[1] + y0 - 15))
    mf.put(m, 'descAlignment', 'ITEM_ALIGN_CENTER')
    mf.put(m, 'descScale', '0.7')
    mf.put(m, 'descColor', *DIM)
    if not ingame:
        first = next((i for i, e in enumerate(m) if e.k == 'itemdef'), len(m))
        m.insert(first, item('vision_veil', (-mr[0], -mr[1], 640, 480), ('style', 'WINDOW_STYLE_FILLED'),
                             ('backcolor', '0', '0.01', '0.03', '0.6'), ('wide', 1), ('decoration',)))


def main_page(m):
    """The first page: the name of the game, the sections as a list at the left, and a line about this version."""
    keep = {}
    for e in items(m):
        keep[name_of(e.block)] = e
    m[:] = [e for e in m if e.k != 'itemdef']
    new = [
        label('vision_mark', (56, 96, 400, 50), '"VISIONJK"', 2.6, BRIGHT),
        label('vision_mark2', (58, 146, 400, 16), '"JEDI ACADEMY  MULTIPLAYER"', 0.62, DIM),
        item('vision_rule', (58, 172, 64, 2), ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *ACCENT), ('radius', 1), ('decoration',)),
        box('vision_rail', (48, 196, 220, 196)),
        # the box of the icon, turning, on a panel of its own with a light around it
        item('vision_hero', (336, 96, 256, 296), ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', '0.035', '0.07', '0.12', '0.55'),
             ('bordercolor', *PANEL_EDGE), ('radius', 10), ('shadow', 14), ('sheen', '0.10'), ('decoration',)),
        item('vision_cube', (364, 102, 200, 200), ('type', 'ITEM_TYPE_CUBE'), ('decoration',)),
        label('vision_hero1', (336, 312, 256, 18), '"Jedi Academy, rebuilt"', 0.86, BRIGHT, ('textalign', 'ITEM_ALIGN_CENTER'),
              ('textalignx', 128)),
        label('vision_hero2', (336, 334, 256, 14), '"jka.vision"', 0.66, ACCENT, ('textalign', 'ITEM_ALIGN_CENTER'),
              ('textalignx', 128)),
        item('vision_version', (584, 372, 0, 12), ('ownerdraw', 'UI_VERSION'), ('font', FONT), ('textscale', '0.5'),
             ('forecolor', *DIM), ('decoration',)),
    ]
    y = 205
    for n in ('playbutton', 'profilebutton', 'controlsbutton', 'setupbutton', 'exitgamebutton'):
        e = keep.get(n)
        if not e:
            continue
        b = e.block
        set_rect(b, 58, y, 200, 30)
        text_left(b, 16, 1.0, 6)
        mf.put(b, 'caps', 1)
        mf.put(b, 'forecolor', *(BRIGHT if n == 'playbutton' else TEXT))
        mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
        new.append(e)
        y += 31
    mf.put(m, 'descX', 58)
    mf.put(m, 'descY', 404)
    mf.put(m, 'descAlignment', 'ITEM_ALIGN_LEFT')
    mf.put(m, 'descScale', '0.8')
    m.extend(new)


def ingame_bar(m):
    """The row of buttons at the top of the screen in a game: a bar behind it, its texts in one line."""
    new = [
        item('vision_bar', (0, 0, 640, 26), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', '0.02', '0.04', '0.08', '0.72'),
             ('wide', 1), ('decoration',)),
        item('vision_bar_line', (0, 26, 640, 0.67), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *(ACCENT[:3] + ['0.22'])),
             ('wide', 1), ('decoration',)),
    ]
    # the photo mode (v_photo, codemp/cgame/photo/cg_photo.c) after Setup. The menu closes first, so the keys are
    # free when the cgame takes them.
    setup = next(e for e in items(m) if name_of(e.block) == 'setup')
    photo = item('photo', rect_of(setup.block), ('text', texts.ref('Photo')), ('type', 'ITEM_TYPE_BUTTON'))
    photo.block.append(Entry('action', [], options.CLICK + [mf.NL, 'close', 'all', ';', mf.NL, 'exec', '"v_photo"', ';']))
    m.insert(m.index(setup) + 1, photo)
    # the buttons in one row across the bar, one place more than the old menu had. Buttons that stood in the same
    # place (the menu shows one or the other) stay together.
    places = sorted({rect_of(e.block)[0] for e in items(m) if e is not photo and rect_of(e.block)
                     and mf.get(e.block, 'text') is not None})
    left, right = places[0], places[-1] + rect_of(setup.block)[2]
    places.insert(places.index(rect_of(setup.block)[0]) + 1, 'photo')
    width = (right - left) / len(places)
    for e in items(m):
        b = e.block
        r = rect_of(b)
        if r and mf.get(b, 'text') is not None:
            set_rect(b, left + places.index('photo' if e is photo else r[0]) * width, 0, width, 26)
            text_center(b, 0.58, 7)
            mf.put(b, 'caps', 1)
            mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
    first = next((i for i, e in enumerate(m) if e.k == 'itemdef'), len(m))
    m[first:first] = new


# Rows the new look leaves out, by group and cvar. Twice in Setup: bubbles and music are with the chat (Chat &
# Social, tools/menus/options.py). Read by nothing in this tree: the sway of the weapon (ui_disableWeaponSway) and
# the axes of a joystick as buttons (joy_xbutton, joy_ybutton).
LEFT_OUT = {('voicechat', 'v_chatbubbles'), ('voicechat', 'v_sharemusic'),
            ('options', 'ui_disableweaponsway'), ('joycontrols', 'joy_xbutton'), ('joycontrols', 'joy_ybutton')}


def leave_out(m):
    """Takes the rows of LEFT_OUT out of a menu and moves the rows under each of them up into its place."""
    for e in list(items(m)):
        b = e.block
        cv = mf.get(b, 'cvar') or mf.get(b, 'cvarfloat')
        r = rect_of(b)
        if not cv or not r or (group_of(b), mf.unquote(cv[0]).lower()) not in LEFT_OUT or e not in m:
            continue
        twins = [x for x in items(m) if x is not e and group_of(x.block) == group_of(b) and rect_of(x.block)
                 and rect_of(x.block)[1] == r[1] and (mf.get(x.block, 'cvar') or [''])[0] == cv[0]]
        below = sorted({rect_of(x.block)[1] for x in items(m) if group_of(x.block) == group_of(b) and rect_of(x.block)
                        and rect_of(x.block)[1] > r[1]})
        pitch = below[0] - r[1] if below else r[3]
        for x in [e] + twins:
            m.remove(x)
        for x in items(m):
            xr = rect_of(x.block)
            if group_of(x.block) == group_of(b) and xr and xr[1] > r[1]:
                set_rect(x.block, xr[0], xr[1] - pitch, xr[2], xr[3])


# Windows of this tree whose frame is a picture of the game in the old menus: that item becomes the frame of the
# window (window()), so the window hides what is behind it.
FRAMES = {'changelog.menu': 'changelog_background'}


def before(m, name, ingame):
    """What a page gets before the rules of theme.py."""
    if name in options.SCREENS:
        leave_out(m)
    for e in items(m):
        if name_of(e.block) == FRAMES.get(name):
            mf.drop(e.block, 'background')
            mf.put(e.block, 'style', 'WINDOW_STYLE_PANEL')


LINE = 18.0                 # the height of a line of the new font at a scale of 1


def tidy(m):
    """Texts and the windows they belong to, for every menu. A caption that stands on a bar is put on the middle
    of the bar: the old menus had it a little above or below, on a picture of another height. A setting whose
    rectangle is much higher than its line is cut to the line: the mark under the mouse is as large as the
    rectangle, and reached into the rows around it."""
    # the frames around the rows of a list of servers: a line under each row in their place
    for e in items(m):
        b = e.block
        r = rect_of(b)
        if r and name_of(b) == 'horizontalseparators' and mf.get(b, 'text') is None:
            for key in ('bordercolor', 'radius', 'shadow', 'sheen', 'border', 'bordersize'):
                mf.drop(b, key)
            mf.put(b, 'style', 'WINDOW_STYLE_FILLED')
            mf.put(b, 'backcolor', '1', '1', '1', '0.07')
            set_rect(b, r[0], r[1] + r[3] - 1, r[2], 0.67)
    bars = [rect_of(e.block) for e in items(m) if rect_of(e.block) and mf.get(e.block, 'text') is None
            and (mf.get(e.block, 'style') or [''])[0] == 'WINDOW_STYLE_PANEL' and mf.get(e.block, 'backcolor') == HEAD
            and 10 <= rect_of(e.block)[3] <= 34]
    for e in items(m):
        b = e.block
        r = rect_of(b)
        if not r or mf.get(b, 'text') is None or name_of(b).startswith('vision_'):
            continue
        scale = mf.num((mf.get(b, 'textscale') or ['0.5'])[0]) or 0.5
        ay = mf.num((mf.get(b, 'textaligny') or ['0'])[0]) or 0.0
        ax = mf.num((mf.get(b, 'textalignx') or ['0'])[0]) or 0.0
        line = LINE * scale
        k = kind_of(b)
        if k in ('', 'text', '0') and r[3] <= 34:
            # where its text stands: the caption of the bar that lies under that place
            ty, tx = r[1] + ay, r[0] + ax
            for bx, by, bw, bh in bars:
                if bx - 2 <= tx <= bx + bw + 2 and by - 12 <= ty <= by + bh - 2 and line <= bh + 2:
                    mf.put(b, 'textaligny', mf.fmt(by + (bh - line) / 2 - r[1]))
                    break
        elif k in ('editfield', 'numericfield', 'yesno', 'multi', 'slider', 'bind', '4', '9', '10', '11', '12', '13') \
                and r[3] > line + 9:
            set_rect(b, r[0], r[1] + ay - 3, r[2], line + 6)
            mf.put(b, 'textaligny', 3)


def page(m, name, ingame):
    menu = mf.unquote(mf.get(m, 'name')[0]).lower()
    # for a picture of a tab nothing clicks on (tools/uishot.sh): UI_SHOW=<group> makes it the one that is open
    if os.environ.get('UI_SHOW'):
        for e in items(m):
            g = (mf.get(e.block, 'group') or [''])[0]
            if g == os.environ['UI_SHOW'] or name_of(e.block) == 'setup_background':
                mf.put(e.block, 'visible', 1)
            elif g in ('video', 'vidrestart'):
                mf.put(e.block, 'visible', 0)
    full = mf.truth(mf.get(m, 'fullscreen'))
    if name in profile.SCREENS:
        # the pages of the player: one frame for profile, character, lightsaber and force powers (profile.py)
        nav = []
        if not ingame:
            if not {name_of(e.block) for e in items(m)} & set(NAV):
                m.extend(nav_button(*n) for n in NAV_MAKE)
            nav = top_bar(m, 'profilebutton')
            nav += [e for e in items(m) if name_of(e.block) in NAV]
        profile.build(m, name, ingame, nav)
        return
    if name in options.SCREENS:
        # Setup and Controls: one screen of tabs and lists (options.py), under the bar of the sections
        nav = []
        if not ingame:
            if not {name_of(e.block) for e in items(m)} & set(NAV):
                m.extend(nav_button(*n) for n in NAV_MAKE)
            nav = top_bar(m, SECTION.get(menu))
            nav += [e for e in items(m) if name_of(e.block) in NAV]
        options.build(m, name, ingame, nav)
        return
    if menu == 'main':
        main_page(m)
    elif menu == 'connect':
        # the sky of the menus (backdrop) under the panel the code draws (UI_ConnectPanel), no picture of the game.
        # (The window of the menu was a picture frame as well: left without a picture it draws the engine's
        # stand-in for a missing one over the sky.)
        m[:] = [e for e in m if e.k != 'itemdef']
        mf.drop(m, 'style')
        mf.put(m, 'backdrop', 1)
    elif menu == 'ingame':
        ingame_bar(m)
    elif full and not ingame:
        shell(m, menu, HAD_LOGO.get(name, False))
    elif not full:
        window(m, menu, ingame)
    if menu == 'ingame_siegeclass':
        # what the item under the mouse is for stands between Cancel and Okay at the bottom of this menu: in the
        # size it had it reached into their row
        mf.put(m, 'descScale', '0.6')
        mf.put(m, 'descY', '447')
    tidy(m)


# pages whose content began under the logo of the game, by file: noted before the rules took the logo away
HAD_LOGO = {}
