"""What single pages get beyond the rules of theme.py (tools/menus/build.py): the frame of the pages outside a
game (a bar at the top with the four sections, a title, the buttons at the bottom), and the pages that are laid
out anew."""
import menufile as mf
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
        item('vision_bar', (-400, 0, 1440, 34), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', '0.02', '0.04', '0.08', '0.55'),
             ('decoration',)),
        item('vision_bar_line', (-400, 34, 1440, 0.67), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *(ACCENT[:3] + ['0.22'])),
             ('decoration',)),
        label('vision_mark', (24, 7, 120, 20), '"VISIONJK"', 1.0, BRIGHT),
        label('vision_mark2', (112, 12, 160, 14), '"JEDI ACADEMY"', 0.5, DIM),
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


def bottom_button(b, primary=False):
    """A button of the row at the bottom: a pill, the one that goes on filled."""
    r = rect_of(b)
    set_rect(b, r[0] + r[2] / 2 - 55, 446, 110, 22)
    text_center(b, 0.7, 4.5)
    mf.put(b, 'caps', 1)
    if primary:
        mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
        mf.put(b, 'backcolor', *(ACCENT[:3] + ['0.20']))
        mf.put(b, 'bordercolor', *(ACCENT[:3] + ['0.75']))
        mf.put(b, 'radius', 11)
        mf.put(b, 'forecolor', *BRIGHT)
    else:
        mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
        mf.put(b, 'backcolor', '1', '1', '1', '0.04')
        mf.put(b, 'bordercolor', *SOFT_EDGE)
        mf.put(b, 'radius', 11)
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
         'rulesmenu_games': 'default_rulesheader', 'rulesmenu_force': 'default_rulesheader',
         'rulesmenu_weapons': 'default_rulesheader', 'rulesmenu_items': 'default_rulesheader'}
PRIMARY = ('applybutton', 'apply', 'joinbutton', 'join_button', 'join', 'beginbutton', 'begin', 'acceptbutton', 'next',
           'next_button', 'newgame_begin', 'startserver', 'startbutton', 'advanced_begin', 'quitgame_yes')


def shell(m, menu, had_logo):
    """The frame of a page outside a game."""
    has_nav = any(name_of(e.block) in NAV for e in items(m))
    new = top_bar(m, SECTION.get(menu)) if has_nav else []
    title = TITLE.get(menu, '')
    title_done = False
    if title.startswith('@'):
        new.append(label('vision_title', (40, 46, 400, 28), title, 1.45, BRIGHT, ('textaligny', 2)))
        title_done = True
    for e in items(m):
        b = e.block
        n = name_of(b)
        r = rect_of(b)
        if not r or n in NAV or n.startswith('vision_'):
            continue
        if r[1] >= 430 and mf.get(b, 'text') is not None and mf.find(b, 'action'):
            bottom_button(b, primary=n in PRIMARY)
            continue
        if had_logo and 150 <= r[1] < 430:
            if n == title and not title_done and mf.get(b, 'text') is not None:
                # the heading of the page, large at the top left
                title_done = True
                set_rect(b, 40, 46, 400, 28)
                text_left(b, 0, 1.45, 2)
                mf.put(b, 'forecolor', *BRIGHT)
                mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
                mf.drop(b, 'caps')
                continue
            set_rect(b, r[0], r[1] + SHIFT, r[2], r[3])
    if had_logo:
        # what the mouse is on is explained under the heading
        mf.put(m, 'descX', 40)
        mf.put(m, 'descY', 80)
        mf.put(m, 'descAlignment', 'ITEM_ALIGN_LEFT')
        mf.put(m, 'descScale', '0.8')
    first = next((i for i, e in enumerate(m) if e.k == 'itemdef'), len(m))
    m[first:first] = new


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
    ]
    y = 208
    for n in ('playbutton', 'profilebutton', 'controlsbutton', 'setupbutton', 'exitgamebutton'):
        e = keep.get(n)
        if not e:
            continue
        b = e.block
        set_rect(b, 58, y, 200, 32)
        text_left(b, 16, 1.0, 7)
        mf.put(b, 'caps', 1)
        mf.put(b, 'forecolor', *(BRIGHT if n == 'playbutton' else TEXT))
        mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
        new.append(e)
        y += 35
    mf.put(m, 'descX', 58)
    mf.put(m, 'descY', 404)
    mf.put(m, 'descAlignment', 'ITEM_ALIGN_LEFT')
    mf.put(m, 'descScale', '0.8')
    m.extend(new)


def ingame_bar(m):
    """The row of buttons at the top of the screen in a game: a bar behind it, its texts in one line."""
    new = [
        item('vision_bar', (-400, 0, 1440, 26), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', '0.02', '0.04', '0.08', '0.72'),
             ('decoration',)),
        item('vision_bar_line', (-400, 26, 1440, 0.67), ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *(ACCENT[:3] + ['0.22'])),
             ('decoration',)),
    ]
    for e in items(m):
        b = e.block
        r = rect_of(b)
        if r and mf.get(b, 'text') is not None:
            set_rect(b, r[0], 0, r[2], 26)
            text_center(b, 0.58, 7)
            mf.put(b, 'caps', 1)
            mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
    first = next((i for i, e in enumerate(m) if e.k == 'itemdef'), len(m))
    m[first:first] = new


def page(m, name, ingame):
    menu = mf.unquote(mf.get(m, 'name')[0]).lower()
    full = mf.get(m, 'fullscreen')
    full = bool(full and full[0] != '0')
    if menu == 'main':
        main_page(m)
    elif menu == 'ingame':
        ingame_bar(m)
    elif full and not ingame:
        shell(m, menu, HAD_LOGO.get(name, False))


# pages whose content began under the logo of the game, by file: noted before the rules took the logo away
HAD_LOGO = {}
