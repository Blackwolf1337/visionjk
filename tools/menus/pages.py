"""What single pages get beyond the rules of theme.py (tools/menus/build.py): the frame of the pages outside a
game (a bar at the top with the four sections, a title, the buttons at the bottom), and the pages that are laid
out anew."""
import os

import menufile as mf
import options
import canvas
import ports
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


def box(name, rect, fill=PANEL, edge=PANEL_EDGE, radius=5.3, shadow=12, sheen='0'):
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
        item('vision_bar_line', (0, 34, 640, 0.67), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *BAR_LINE),
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
        mf.put(b, 'backcolor', *ACCENT)
        mf.put(b, 'bordercolor', *ACCENT)
        mf.put(b, 'radius', 3.2)
        mf.put(b, 'forecolor', *ON_ACCENT)
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
WINDOW = ['0.031', '0.055', '0.11', '0.97']      # a window on a page or on the level: nearly solid, its text has to be read


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
    mine = box('vision_rail_%d' % round(y0), (x - pad, y0 - pad, w + 2 * pad, y1 - y0 + 2 * pad), SOFT, SOFT_EDGE, 5.3, 8, '0')
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
                 ('backcolor', *BAR_LINE), ('wide', 1), ('decoration',))]
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
    """A button at the bottom of a window: filled with the accent where it goes on, an outline where it does not."""
    r = rect_of(b)
    w = max(70, min(r[2], 110))
    set_rect(b, r[0] + r[2] / 2 - w / 2, r[1] + r[3] / 2 - 10, w, 20)
    text_center(b, 0.64, 4)
    mf.put(b, 'caps', 1)
    mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
    mf.put(b, 'radius', 3.2)
    if primary:
        mf.put(b, 'backcolor', *ACCENT)
        mf.put(b, 'bordercolor', *ACCENT)
        mf.put(b, 'forecolor', *ON_ACCENT)
    else:
        mf.put(b, 'backcolor', *NONE)
        mf.put(b, 'bordercolor', *CONTROL_EDGE)
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
        panel(frame.block, WINDOW, PANEL_EDGE, 6.4, 14)
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


def card(name, rect, text, target, desc, primary=False):
    """A button of the first page as a card: filled with the accent for the way into a game, a panel for the others.
    Its word at the left, on the middle of its height."""
    x, y, w, h = rect
    scale = 1.05 if primary else 0.66
    e = item(name, rect, ('type', 'ITEM_TYPE_BUTTON'), ('text', text), ('descText', desc), ('font', FONT),
             ('textscale', mf.fmt(scale)), ('caps', 1), ('textalign', 'ITEM_ALIGN_LEFT'), ('textalignx', 16 if primary else 11),
             ('textaligny', mf.fmt((h - 18.0 * scale) / 2)), ('style', 'WINDOW_STYLE_PANEL'), ('radius', 5.3),
             ('backcolor', *(ACCENT if primary else PANEL)), ('bordercolor', *(ACCENT if primary else PANEL_EDGE)),
             ('forecolor', *(ON_ACCENT if primary else BRIGHT)))
    e.block.append(Entry('action', [], ['play', '"sound/interface/button1.wav"', ';', 'close', 'all', ';', 'open', target, ';']))
    return e


def main_page(m):
    """The first page (the redesign of 2026-11-01): the bar of the sections at the top, at the left the name of the game
    and the ways into a game as cards, in the middle the player's model, at the right the box of the icon and what is
    new, the way out at the bottom."""
    keep = {}
    for e in items(m):
        keep[name_of(e.block)] = e
    m[:] = [e for e in m if e.k != 'itemdef']
    nav = [keep[n] for n in NAV if n in keep]
    m.extend(nav)
    new = top_bar(m, None)
    for e in nav:
        m.remove(e)
    new += nav
    new += [
        label('vision_home_mark', (40, 56, 300, 46), '"VISIONJK"', 2.35, BRIGHT),
        label('vision_home_tagline', (42, 102, 300, 16), '"Jedi Academy, rebuilt"', 0.84, DIM),
        item('vision_rule', (42, 126, 30, 1.6), ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *ACCENT), ('radius', 0.8),
             ('decoration',)),
        card('vision_join', (40, 140, 268, 58), '@MENUS_JOIN_SERVER_CAPS', 'joinserver', '@MENUS_SEARCH_FOR_SERVERS_TO', True),
        card('vision_create', (40, 208, 131, 44), '@MENUS_CREATE_SERVER_CAPS', 'createserver', '@MENUS_CREATE_YOUR_OWN_SERVER'),
        card('vision_solo', (177, 208, 131, 44), '@MENUS_SOLO_GAME', 'quickgame', '@MENUS_QUICKSTART_DESC'),
        card('vision_demo', (40, 258, 131, 44), '@MENUS_PLAY_DEMO_CAPS', 'demo', '@MENUS_PLAY_BACK_A_RECORDED'),
        card('vision_rules', (177, 258, 131, 44), '@MENUS_RULES', 'rulesMenu', '@MENUS_RULES_DESC'),
        # the player's model, put on the item by uiScript homeModel (codemp/ui/ui_main.c)
        item('vision_glow', (343, 378, 100, 14), ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *(ACCENT[:3] + ['0.07'])),
             ('radius', 7), ('glowcolor', *(ACCENT[:3] + ['0.14'])), ('decoration',)),
        item('character', (318, 46, 150, 352), ('type', 'ITEM_TYPE_MODEL'), ('asset_model', '"models/players/jedi_hm/model.glm"'),
             ('model_g2anim', '"BOTH_STAND1"'), ('model_angle', 160), ('model_rotation', 0), ('model_g2mins', -30, -15, -14),
             ('model_g2maxs', 20, 15, 30), ('model_fovx', 50), ('model_fovy', 50), ('decoration',)),
        # the box of the icon, turning, and the site
        item('vision_hero', (476, 46, 140, 134), ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *PANEL), ('bordercolor', *PANEL_EDGE),
             ('radius', 5.3), ('shadow', 12), ('decoration',)),
        item('vision_cube', (496, 52, 100, 100), ('type', 'ITEM_TYPE_CUBE'), ('decoration',)),
        label('vision_hero2', (476, 154, 140, 14), '"jka.vision"', 0.66, ACCENT, ('textalign', 'ITEM_ALIGN_CENTER'), ('textalignx', 70)),
    ]
    # the bar at the bottom: the way out at the left
    new += [item('vision_foot', (0, 446, 640, 34), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *FOOT), ('wide', 1),
                 ('decoration',)),
            item('vision_foot_line', (0, 446, 640, 0.67), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *BAR_LINE), ('wide', 1),
                 ('decoration',))]
    # (UI_VERSION ends its text at the left edge of its rectangle)
    new.append(item('vision_version', (612, 459, 0, 12), ('ownerdraw', 'UI_VERSION'), ('font', FONT), ('textscale', '0.48'),
                    ('forecolor', *DIM), ('decoration',)))
    out = keep.get('exitgamebutton')
    if out:
        set_rect(out.block, 29, 446, 90, 34)
        bottom_button(out.block)
        mf.put(out.block, 'flow', 1)
        new.append(out)
    mf.put(m, 'descX', 40)
    mf.put(m, 'descY', 316)
    mf.put(m, 'descAlignment', 'ITEM_ALIGN_LEFT')
    mf.put(m, 'descScale', '0.7')
    e = mf.find(m, 'onopen')
    if e and isinstance(e.block, list):
        e.block = ['uiScript', 'homeModel', ';', mf.NL] + e.block
    else:
        m.append(Entry('onOpen', [], ['uiScript', 'homeModel', ';']))
    m.extend(new)


def control(b, rect, scale=0.6, align='center', primary=False, field=False):
    """An item as a control of the redesign: an outlined button (or a filled one, or a dark field around an owner draw)
    with its word in the middle or at the left."""
    x, y, w, h = rect
    set_rect(b, x, y, w, h)
    mf.put(b, 'font', FONT)
    mf.put(b, 'textscale', mf.fmt(scale))
    mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
    mf.put(b, 'radius', 3.2)
    mf.put(b, 'backcolor', *(ACCENT if primary else INSET if field else NONE))
    mf.put(b, 'bordercolor', *(ACCENT if primary else CONTROL_EDGE))
    mf.put(b, 'forecolor', *(ON_ACCENT if primary else TEXT))
    for key in ('border', 'bordersize', 'background', 'mouseenter', 'mouseexit'):
        mf.drop(b, key)
    if align == 'center':
        mf.put(b, 'textalign', 'ITEM_ALIGN_CENTER')
        mf.put(b, 'textalignx', mf.fmt(w / 2))
    else:
        mf.put(b, 'textalign', 'ITEM_ALIGN_LEFT')
        mf.put(b, 'textalignx', 8)
    mf.put(b, 'textaligny', mf.fmt((h - 18.0 * scale) / 2))
    if not field:
        mf.put(b, 'caps', 1)


def join_page(m):
    """The server browser (the redesign of 2026-11-01): the filters in two rows at the top, the list at the left, at the
    right the level of the server that is chosen and what can be done with it."""
    by = {}
    for e in items(m):
        by.setdefault(name_of(e.block), []).append(e)

    def one(n):
        return [e.block for e in by.get(n.lower(), [])]

    # what goes: the boxes behind the filters and the buttons, the frames of the columns, the lines between the rows
    gone = {'setup_background', 'secondary_background', 'horizontalseparators', 'servercolumn', 'mapcolumn', 'playercolumn',
            'typecolumn', 'pingcolumn'}
    m[:] = [e for e in m if not (e.k == 'itemdef' and name_of(e.block) in gone)]
    for b in one('getnewlist_button'):
        control(b, (14, 42, 112, 22))
    for b in one('refreshFilter_button'):
        control(b, (130, 42, 112, 22))
    for n, rect in (('netsource', (250, 42, 122, 22)), ('netfilter', (376, 42, 124, 22)), ('gametypefilter', (504, 42, 122, 22))):
        for b in one(n):
            control(b, rect, 0.58, 'left', field=True)
    for n, rect in (('viewEmpty', (14, 68, 140, 16)), ('viewFull', (160, 68, 140, 16)), ('viewBotsOnly', (306, 68, 150, 16)),
                    ('datarate', (462, 68, 164, 16))):
        for b in one(n):
            r = rect_of(b)
            set_rect(b, *rect)
            mf.put(b, 'textscale', '0.58')
    # the heads of the columns, and where the columns of the list are
    cols = (('server', 10, 190), ('map', 206, 100), ('Players', 310, 44), ('Type', 354, 56), ('Ping', 414, 38))
    for n, x, w in cols:
        for b in one(n):
            set_rect(b, x, 88, w, 18)
            mf.put(b, 'font', FONT)
            mf.put(b, 'textscale', '0.5')
            mf.put(b, 'caps', 1)
            mf.put(b, 'forecolor', *DIM)
            mf.put(b, 'textalign', 'ITEM_ALIGN_LEFT')
            mf.put(b, 'textalignx', 4)
            mf.put(b, 'textaligny', mf.fmt((18 - 9.0) / 2))
            mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
            mf.drop(b, 'background')
    for b in one('serverlist'):
        set_rect(b, 10, 106, 442, 298)
        mf.put(b, 'elementheight', 22)
        mf.put(b, 'textscale', '0.62')
        mf.put(b, 'textaligny', 5)
        mf.put(b, 'columns', 5, 4, 40, 186, 200, 40, 100, 304, 5, 40, 348, 20, 54, 408, 20, 34)
        mf.put(b, 'radius', 5.3)
        mf.put(b, 'bordercolor', *PANEL_EDGE)
        mf.put(b, 'backcolor', *PANEL)
        mf.drop(b, 'mouseenter')
        mf.drop(b, 'mouseexit')
    for b in one('refreshdate'):
        set_rect(b, 12, 408, 300, 14)
        mf.put(b, 'textscale', '0.5')
        mf.put(b, 'forecolor', *DIM)
    # the right: the level of the server under the mouse or chosen, and the buttons under it
    pane = [box('vision_pane', (462, 88, 166, 334), PANEL, PANEL_EDGE, 5.3, 0),
            item('vision_netmap', (470, 96, 150, 112), ('ownerdraw', 'UI_NETMAPPREVIEW'), ('decoration',))]
    at = next(i for i, e in enumerate(m) if e.k == 'itemdef' and name_of(e.block) == 'serverlist')
    m[at:at] = pane
    y = 218
    for n in ('serverinfoText', 'addFavorite', 'passwordText', 'createFavoriteText', 'findplayerText', 'idsText'):
        for b in one(n):
            control(b, (470, y, 150, 22), 0.58)
        y += 26
    for b in one('delfavorite'):
        control(b, (470, 218 + 26, 150, 22), 0.58)
    mf.put(m, 'descX', 12)
    mf.put(m, 'descY', 428)
    mf.put(m, 'descAlignment', 'ITEM_ALIGN_LEFT')
    mf.put(m, 'descScale', '0.6')


SETTING_KINDS = ('editfield', 'numericfield', 'yesno', 'multi', 'slider', '4', '9', '10', '11', '12', '13')


def as_rows(blocks, x, y0, w, pitch=22, gap=8, px=None):
    """Settings and their headings in a column, one under the other in the order they stood: a setting becomes a row
    (its name at the left, its widget at the right, "row 1"), a heading a small word in capitals. Items that stood
    in the same place (the menu shows one of them) stay in the same place. Returns where the column ends."""
    rows = sorted(blocks, key=lambda b: rect_of(b)[1])
    y, last = y0, None
    for b in rows:
        oy = round(rect_of(b)[1])
        k = kind_of(b)
        owner = mf.get(b, 'ownerdraw') is not None
        heading = k not in SETTING_KINDS and not owner and k not in ('button', '1')
        if last is not None and oy != last:
            y += pitch + (gap if heading else 0)
        last = oy
        set_rect(b, x, y, w, pitch)
        mf.put(b, 'font', FONT)
        if heading:
            mf.put(b, 'textscale', mf.fmt(12 / 18.0) if px else '0.52')
            mf.put(b, 'caps', 1)
            mf.put(b, 'forecolor', *DIM)
            mf.put(b, 'textalign', 'ITEM_ALIGN_LEFT')
            mf.put(b, 'textalignx', 2)
            mf.put(b, 'textaligny', mf.fmt(pitch - 18) if px else 5)
            mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
            if px:
                mf.put(b, 'font', '7')
            mf.drop(b, 'background')
        elif k in ('button', '1'):
            if px:
                import ports
                ports.control(Entry('itemDef', [], b), (x, y + 4, w, pitch - 8), 15)
            else:
                control(b, (x + 2, y + 2, w - 4, pitch - 4), 0.58)
        elif owner:
            # (an owner draw writes its name and what it is set to itself: it stays a line of text)
            mf.put(b, 'textscale', mf.fmt(px / 18.0) if px else '0.66')
            mf.put(b, 'textalign', 'ITEM_ALIGN_LEFT')
            mf.put(b, 'textalignx', 12 if px else 8)
            mf.put(b, 'textaligny', mf.fmt((pitch - px) / 2.0) if px else mf.fmt((pitch - 18.0 * 0.66) / 2))
        else:
            mf.put(b, 'row', 3 if k == 'editfield' else 1)
            mf.put(b, 'textscale', mf.fmt(px / 18.0) if px else '0.66')
            mf.put(b, 'forecolor', *TEXT)
            mf.drop(b, 'textalign')
            mf.drop(b, 'textalignx')
    return y + pitch


def by_text(m):
    """The items of a menu by the reference of their text (or owner draw), in their order."""
    out = {}
    for e in items(m):
        k = (mf.get(e.block, 'text') or mf.get(e.block, 'ownerdraw') or [''])[0].upper()
        out.setdefault(k, []).append(e.block)
    return out


def drop_named(m, names):
    m[:] = [e for e in m if not (e.k == 'itemdef' and name_of(e.block) in names)]


def preview(m, rect):
    """The picture of the level that is chosen (UI_STARTMAPCINEMATIC) in a frame of the redesign."""
    x, y, w, h = rect
    for e in items(m):
        b = e.block
        if name_of(b) != 'mappreview':
            continue
        if mf.get(b, 'ownerdraw'):
            set_rect(b, x + 6, y + 6, w - 12, h - 12)
        else:
            set_rect(b, x, y, w, h)
            panel(b, NONE, PANEL_EDGE, 5.3)
            mf.put(b, 'decoration')


def create_page(m):
    """Create a game (the redesign of 2026-11-01): the kind of game and the levels at the left, the level that is chosen
    and the places of the players in the middle, the server and its rules at the right."""
    t = by_text(m)
    drop_named(m, {'setup_background'})
    for b in t.get('@MENUS_GAME_TYPE', []):
        control(b, (10, 44, 170, 22), 0.6, 'left', field=True)
    for e in items(m):
        b = e.block
        if name_of(b) == 'maplist':
            set_rect(b, 10, 72, 170, 330)
            panel(b, PANEL, PANEL_EDGE, 5.3)
            mf.put(b, 'elementheight', 20)
            mf.put(b, 'textscale', '0.62')
    preview(m, (190, 44, 230, 170))
    # the server and its rules: rows on a panel under the picture
    rules = [e.block for e in items(m) if name_of(e.block) in ('expert', 'normal', 'rules_title') and rect_of(e.block)
             and kind_of(e.block) != 'list']
    at = next(i for i, e in enumerate(m) if e.k == 'itemdef' and e.block in rules)
    end = as_rows(rules, 196, 226, 218, pitch=20, gap=4)
    m.insert(at, box('vision_rules', (190, 222, 230, min(max(end, 396), 436) - 222 + 4), PANEL, PANEL_EDGE, 5.3, 0))
    # the places of the players: two columns of eight at the right
    slots = [e.block for e in items(m) if name_of(e.block) in ('humanbotfield', 'humanbotnonfield')]
    at = next(i for i, e in enumerate(m) if e.k == 'itemdef' and e.block in slots)
    m.insert(at, box('vision_slots', (430, 44, 198, 214), PANEL, PANEL_EDGE, 5.3, 0))
    top = min(rect_of(x)[1] for x in slots)
    for b in slots:
        r = rect_of(b)
        col = 0 if r[0] < 300 else 1
        row = round((r[1] - top) / 20)
        set_rect(b, 438 + col * 92, 54 + row * 20, 90, 20)
        mf.put(b, 'textscale', '0.6')
    for b in by_text(m).get('@MENUS_SKILL', []):
        set_rect(b, 438, 54 + 8 * 20 + 10, 180, 20)
        mf.put(b, 'textscale', '0.6')
        mf.put(b, 'textalign', 'ITEM_ALIGN_LEFT')
        mf.put(b, 'textalignx', 4)
    mf.put(m, 'descX', 12)
    mf.put(m, 'descY', 428)
    mf.put(m, 'descAlignment', 'ITEM_ALIGN_LEFT')
    mf.put(m, 'descScale', '0.6')


def solo_page(m, menu):
    """The two pages of a game against bots: the levels at the left and the picture of the chosen one beside them
    (quickgame), then the rules as rows at the left with the picture beside them (quickgame2)."""
    t = by_text(m)
    drop_named(m, {'setup_background'})
    for e in items(m):
        b = e.block
        if name_of(b) == 'maplist':
            set_rect(b, 40, 132, 190, 270)
            panel(b, PANEL, PANEL_EDGE, 5.3)
            mf.put(b, 'elementheight', 20)
            mf.put(b, 'textscale', '0.62')
    for b in t.get('@MENUS_MAP_LISTING', []):
        set_rect(b, 40, 112, 190, 18)
        mf.put(b, 'textscale', '0.52')
        mf.put(b, 'caps', 1)
        mf.put(b, 'forecolor', *DIM)
        mf.put(b, 'textalign', 'ITEM_ALIGN_LEFT')
        mf.put(b, 'textalignx', 2)
    for b in t.get('@MENUS_GAME_TYPE', []):
        control(b, (40, 84, 190, 22), 0.6, 'left', field=True)
    if menu == 'quickgame':
        preview(m, (244, 84, 356, 318))
        return
    rules = [e.block for e in items(m) if name_of(e.block) in ('options', 'normal', 'expert_button', 'difficulty_button',
                                                                  'custombots_button', 'rules_title', 'end_game_title', 'bots_title')]
    at = next(i for i, e in enumerate(m) if e.k == 'itemdef' and e.block in rules)
    end = as_rows(rules, 46, 92, 224)
    m.insert(at, box('vision_rules', (40, 84, 236, end - 84 + 6), PANEL, PANEL_EDGE, 5.3, 0))
    preview(m, (290, 84, 310, 236))


# the buttons of the bar in a game, in the order of the redesign: the match, the player, the settings; the ways
# out at the right. Buttons that share a place (join and the class of a siege, the profile and the objectives,
# voice chat and adding a bot) stay together.
INGAME_LEFT = ('join', 'class', 'vote', 'callvote', 'chat', 'addbot', 'player', 'objectives', 'controls', 'setup', 'photo')
INGAME_RIGHT = ('about', 'servers', 'leave')


def quit_page(m):
    """Leaving the game (the redesign of 2026-11-01): a dialog in the middle of the page, no at the left, quitting at the
    right in red."""
    t = {name_of(e.block): e.block for e in items(m)}
    at = next(i for i, e in enumerate(m) if e.k == 'itemdef' and name_of(e.block) == 'quit_title')
    m.insert(at, box('vision_dialog', (180, 150, 280, 140), WINDOW, CONTROL_EDGE, 6.4, 14))
    if 'quit_title' in t:
        b = t['quit_title']
        set_rect(b, 196, 164, 248, 14)
        text_left(b, 0, 0.52, 2)
        mf.put(b, 'forecolor', *DIM)
    if 'confirm' in t:
        b = t['confirm']
        set_rect(b, 196, 184, 248, 30)
        text_left(b, 0, 1.0, 6)
        mf.put(b, 'forecolor', *BRIGHT)
        mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
    if 'quitgame_cancel' in t:
        b = t['quitgame_cancel']
        control(b, (298, 254, 70, 24), 0.62)
        mf.drop(b, 'flow')
    if 'quitgame_yes' in t:
        b = t['quitgame_yes']
        control(b, (374, 254, 70, 24), 0.62)
        mf.put(b, 'backcolor', *RED)
        mf.put(b, 'bordercolor', *RED)
        mf.put(b, 'forecolor', '0.16', '0.03', '0.02', '1')
    # (the old page had an invisible button over its middle that opened the credits: it stays, on the heading of the
    # dialog, so it does not lie over the buttons)
    if 'creditsbutton' in t:
        set_rect(t['creditsbutton'], 196, 160, 248, 20)
    m[:] = [e for e in m if not (e.k == 'itemdef' and name_of(e.block) in ('vision_foot', 'vision_foot_line'))]


def ingame_bar(m):
    """The row of buttons at the top of the screen in a game: a bar behind it, its buttons as wide as their words, the
    match first, then the player and the settings, the ways out last with leaving in red."""
    new = [
        item('vision_bar', (0, 0, 640, 26), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', '0.016', '0.031', '0.063', '0.8'),
             ('wide', 1), ('decoration',)),
        item('vision_bar_line', (0, 26, 640, 0.67), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *BAR_LINE),
             ('wide', 1), ('decoration',)),
    ]
    # the photo mode (v_photo, codemp/cgame/photo/cg_photo.c) after Setup. The menu closes first, so the keys are
    # free when the cgame takes them.
    setup = next(e for e in items(m) if name_of(e.block) == 'setup')
    photo = item('photo', rect_of(setup.block), ('text', texts.ref('Photo')), ('type', 'ITEM_TYPE_BUTTON'))
    photo.block.append(Entry('action', [], options.CLICK + [mf.NL, 'close', 'all', ';', mf.NL, 'exec', '"v_photo"', ';']))
    m.insert(m.index(setup) + 1, photo)
    buttons = {name_of(e.block): e for e in items(m) if mf.get(e.block, 'text') is not None}
    left = [buttons[n] for n in INGAME_LEFT if n in buttons]
    right = [buttons[n] for n in INGAME_RIGHT if n in buttons]
    rest = [e for e in buttons.values() if e not in left and e not in right]
    left += rest
    for e in left + right:
        m.remove(e)
    # (one row that Menu_TabsLayout fits into the width of the bar or of the panel in the level: two rows of
    # words side by side would run into each other on a narrow panel)
    for e in left + right:
        b = e.block
        set_rect(b, 8, 0, 52, 26)
        text_center(b, 0.56, 7)
        mf.put(b, 'caps', 1)
        mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
        mf.put(b, 'flow', 1)
        if name_of(b) == 'leave':
            mf.put(b, 'forecolor', *RED)
    first = next((i for i, e in enumerate(m) if e.k == 'itemdef'), len(m))
    m[first:first] = new + left + right


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
FRAMES = {'changelog.menu': 'changelog_background', 'cvarclean.menu': 'cvarclean_background'}


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
    if name == 'player.menu' and not ingame and EXTRA:
        # the player outside a game: profile, character and lightsaber on one page, as the board (ports.profile_all)
        ports.profile_all(m, EXTRA)
        return
    if name in profile.SCREENS:
        # the pages of the player: one frame for profile, character, lightsaber and force powers (profile.py)
        nav = []
        if not ingame:
            if not {name_of(e.block) for e in items(m)} & set(NAV):
                m.extend(nav_button(*n) for n in NAV_MAKE)
            nav = top_bar(m, 'profilebutton')
            nav += [e for e in items(m) if name_of(e.block) in NAV]
        profile.build(m, name, ingame, nav)
        if ingame:
            canvas.convert(m, ingame)
        else:
            canvas.profile_page(m)
        return
    if name in options.SCREENS:
        # Setup and Controls: the board "Setup, decluttered" in the pixels of the design canvas (options.build_canvas)
        options.build_canvas(m, name, ingame)
        return
    if menu == 'main':
        main_page(m)
        ports.home(m)
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
        if menu == 'joinserver':
            join_page(m)
            ports.join(m)
        elif menu == 'createserver':
            create_page(m)
            ports.create(m)
        elif menu in ('quickgame', 'quickgame2'):
            solo_page(m, menu)
            ports.solo(m, menu)
        elif menu == 'quitmenu':
            quit_page(m)
            ports.quit_page(m)
    elif not full:
        window(m, menu, ingame)
    if menu == 'ingame_siegeclass':
        # what the item under the mouse is for stands between Cancel and Okay at the bottom of this menu: in the
        # size it had it reached into their row
        mf.put(m, 'descScale', '0.6')
        mf.put(m, 'descY', '447')
    if not mf.get(m, 'canvas'):
        # (a page in the pixels of the canvas, ports.py, has its texts and rows where it wants them)
        tidy(m)
    # everything onto the design canvas (canvas.py): the bars of the board, the content smaller and in the middle
    canvas.convert(m, ingame)


# pages whose content began under the logo of the game, by file: noted before the rules took the logo away
HAD_LOGO = {}
# menus another page takes items from, as theme.py made them, by file (build.py: player2.menu and saber.menu)
EXTRA = {}
