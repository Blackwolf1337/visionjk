"""The pages of the redesign of 2026-11-02 are written in the pixels of the design canvas (1600 x 900, the Setup board
"Setup, decluttered" of the canvas VisionJK Settings), not in the 640 x 480 units of the game: the menu says
"canvas 1600 900" and the ui puts that picture on the screen with the same scale across and down
(Menu_CanvasLayout in codemp/ui/ui_shared.c). The sizes below are the sizes of the board.

A size of letters is given in pixels as well: a text scale of the menu file is pixels / 18 (the new font is 18 units
high at a scale of 1)."""
import menufile as mf
from menufile import Entry
from theme import *         # noqa: F401,F403

W, H = 1600, 900            # the canvas of a page
IN_W, IN_H = 1600, 1200     # the canvas of a window in a game: 4:3, the panel in the level has the 640 x 480 of a menu
HEAD_H = 72                 # the bar at the top
FOOT_H = 68                 # the bar at the bottom
RAIL_W = 260                # the column at the left (the pages of Setup)
PANE_W = 340                # the column at the right (what the row under the mouse is for)
ROW = 52                    # a row of settings
CARD_LINE = 55              # the line under the head of a card: its words in the 55 above it, the line to 56
CARD_HEAD = 66              # the head of a card and the room under its line (10)
CARD_TEXT_Y = (2 + CARD_LINE) / 2.0  # the middle of the words of a head: between its rim (2 pixels) and its line
CARD_BOTTOM = 10            # the room under the last row of a card: as much as under the line
CARD_GAP = 24               # between two cards of a column, and between the two columns
CARD_PAD = 22               # from the edge of a card to the names of its rows

BAR = ['0.016', '0.031', '0.063', '0.55']       # the bars at the top and the bottom (rgba(4,8,16,.55))
FOOT = ['0.016', '0.031', '0.063', '0.6']
RAIL = ['0.016', '0.031', '0.063', '0.35']      # the columns at the left and the right
LINE = ['0.086', '0.149', '0.247', '1']         # #16263f
CARD = ['0.031', '0.055', '0.11', '0.78']       # rgba(8,14,28,.78)
CARD_EDGE = ['0.11', '0.18', '0.29', '1']       # #1c2e4a
FIELD = ['0.027', '0.051', '0.102', '1']        # #070d1a
EDGE = ['0.153', '0.251', '0.373', '1']         # #27405f
MUTED = ['0.56', '0.64', '0.74', '1']           # #8fa3bd
SOFT_TEXT = ['0.706', '0.765', '0.839', '1']    # #b4c3d6, a page that is not open
NAV_TEXT = ['0.86', '0.90', '0.945', '1']       # #dbe5f1
WARN = ['0.91', '0.77', '0.42', '1']            # #e9c46a
BOLD = '7'                                      # SKIN_FONT_BOLD

CLICK = ['play', '"sound/interface/button1.wav"', ';']


def scale(px):
    """The text scale of letters that many pixels high."""
    return mf.fmt(px / 18.0)


def item(name, rect, *props):
    b = [Entry('name', [name]), Entry('rect', [mf.fmt(v) for v in rect]), Entry('visible', ['1'])]
    for key, *args in props:
        if key in ('action', 'cvarFloatList', 'cvarStrList', 'onFocus', 'showCvar', 'hideCvar'):
            # (a block: its words in { }; showCvar and hideCvar may be given as (key, [], words))
            b.append(Entry(key, [], list(args[-1])))
        else:
            b.append(Entry(key, [str(a) for a in args]))
    return Entry('itemDef', [], b)


def fill(name, rect, color, *more):
    return item(name, rect, ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *color), ('decoration',), *more)


def panel(name, rect, back, edge, radius, *more):
    return item(name, rect, ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *back), ('bordercolor', *edge), ('radius', radius),
                ('decoration',), *more)


def text(name, rect, words, px, color, align='left', pad=0, font=FONT, *more):
    """A text on the middle of the height of its rectangle, at its left (after pad), in its middle or at its right."""
    x, y, w, h = rect
    if align == 'center':
        where = (('textalign', 'ITEM_ALIGN_CENTER'), ('textalignx', mf.fmt(w / 2)))
    elif align == 'right':
        where = (('textalign', 'ITEM_ALIGN_RIGHT'), ('textalignx', mf.fmt(w - pad)))
    else:
        where = (('textalign', 'ITEM_ALIGN_LEFT'), ('textalignx', mf.fmt(pad)))
    return item(name, rect, ('text', words), ('font', font), ('textscale', scale(px)), ('forecolor', *color), *where,
                ('textaligny', mf.fmt((h - px) / 2.0)), *more)


def button(name, rect, words, action, px=15, kind='outline', desc=None, *more):
    """A button of the board: 'primary' filled with the accent with dark words, 'outline' with an edge, 'plain' only
    its words (the buttons of the bar at the bottom)."""
    props = [('type', 'ITEM_TYPE_BUTTON'), ('caps', 1), ('action', action)]
    if desc:
        props.append(('descText', desc))
    if kind == 'primary':
        props += [('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *ACCENT), ('bordercolor', *ACCENT), ('radius', 6)]
        color, font = ON_ACCENT, BOLD
    elif kind == 'outline':
        props += [('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *NONE), ('bordercolor', *EDGE), ('radius', 6)]
        color, font = NAV_TEXT, FONT
    else:
        props += [('style', 'WINDOW_STYLE_EMPTY')]
        color, font = NAV_TEXT, FONT
    return text(name, rect, words, px, color, 'center', 0, font, *props, *more)


# the four sections of the bar at the top: (name, text, what for, menu it opens, width of its button on the board)
NAV = (('vision_nav_play', '@MENUS_PLAY', '@MENUS_START_PLAYING_NOW', 'joinserver', 86),
       ('vision_nav_profile', '@MENUS_PROFILE', '@MENUS_PROFILE_DESC', 'playerMenu', 116),
       ('vision_nav_controls', '@MENUS_CONTROLS2', '@MENUS_CONFIGURE_GAME_CONTROLS', 'controlsMenu', 136),
       ('vision_nav_setup', '@MENUS_SETUP', '@MENUS_CONFIGURE_GAME_SETTINGS', 'setup_menu', 100))


def header(here, subtitle='"JEDI ACADEMY"'):
    """The bar at the top of a page: the icon and the name of the game at the left, the four sections at the right, the
    one that is open in the accent with a line under it. here: the menu of the section that is open, or None."""
    out = [fill('vision_bar', (0, 0, W, HEAD_H), BAR, ('wide', 1)),
           fill('vision_bar_line', (0, HEAD_H - 1, W, 1), LINE, ('wide', 1)),
           item('vision_logo', (48, 19, 34, 34), ('type', 'ITEM_TYPE_LOGO'), ('forecolor', '1', '1', '1', '1'), ('decoration',)),
           text('vision_mark', (96, 16, 180, 40), '"VISIONJK"', 30, BRIGHT, 'left', 0, FONT, ('decoration',)),
           text('vision_mark2', (268, 22, 300, 32), subtitle, 15, MUTED, 'left', 0, FONT, ('decoration',), ('caps', 1))]
    x = W - 48 - sum(n[4] for n in NAV) - 8 * (len(NAV) - 1)
    for name, words, desc, target, width in NAV:
        on = target == here
        act = list(CLICK) + ['close', 'all', ';', 'open', target, ';']
        out.append(text(name, (x, 0, width, HEAD_H), words, 19, ACCENT if on else NAV_TEXT, 'center', 0, FONT,
                        ('type', 'ITEM_TYPE_BUTTON'), ('caps', 1), ('descText', desc), ('action', act)))
        if on:
            out.append(fill('vision_here', (x, HEAD_H - 3, width, 3), ACCENT))
        x += width + 8
    return out


def footer(y, w=W, wide=True):
    """The bar at the bottom: from y to the end of the canvas."""
    more = (('wide', 1),) if wide else ()
    return [fill('vision_foot', (0, y, w, FOOT_H), FOOT, *more),
            fill('vision_foot_line', (0, y, w, 1), LINE, *more)]


def regrid(entries, origin_from, origin_to, k=900.0 / 480.0):
    """Items made in the units of the game (a page of their own that options.py builds the old way) moved into the
    canvas: their rectangles and sizes times k, from origin_from to origin_to."""
    fx, fy = origin_from
    tx, ty = origin_to
    for e in entries:
        b = e.block
        r = mf.get(b, 'rect')
        if r and len(r) >= 4:
            v = [mf.num(a) or 0.0 for a in r[:4]]
            mf.put(b, 'rect', mf.fmt(tx + (v[0] - fx) * k), mf.fmt(ty + (v[1] - fy) * k), mf.fmt(v[2] * k), mf.fmt(v[3] * k))
        for key in ('textscale', 'textalignx', 'textaligny', 'radius', 'shadow', 'elementwidth', 'elementheight'):
            a = mf.get(b, key)
            if a and mf.num(a[0]) is not None:
                mf.put(b, key, mf.fmt(mf.num(a[0]) * k))
    return entries


GRID_HEAD = ('vision_bar', 'vision_bar_line', 'vision_logo', 'vision_mark', 'vision_mark2', 'vision_here')
GRID_NAV = ('newgamebutton', 'playbutton', 'profilebutton', 'controlsbutton', 'setupbutton')
GRID_FOOT = ('vision_foot', 'vision_foot_line')
CONTENT_K = 1.5             # the content of a page of the 640 x 480 menus on the canvas: 80 % of its old size
IN_K = 2.0                  # and of a menu in a game on the canvas of 1600 x 1200 (2.5 would be its old size)


def _scale_props(b, k):
    """The sizes of an item (not its place) times k."""
    for key in ('textscale', 'textalignx', 'textaligny', 'text2alignx', 'text2aligny', 'textroom', 'radius', 'shadow', 'elementwidth',
                'elementheight', 'bordersize'):
        a = mf.get(b, key)
        if a and mf.num(a[0]) is not None:
            mf.put(b, key, mf.fmt(mf.num(a[0]) * k))
    cols = mf.get(b, 'columns')
    if cols and mf.num(cols[0]) is not None:
        out = [cols[0]]
        for i, v in enumerate(cols[1:]):
            # (each column: where it begins, how many letters, how wide)
            out.append(v if i % 3 == 1 else mf.fmt((mf.num(v) or 0) * k))
        mf.put(b, 'columns', *out)


def _rect(b):
    r = mf.get(b, 'rect')
    if not r or len(r) < 4:
        return None
    v = [mf.num(a) for a in r[:4]]
    return None if None in v else v


def convert(m, ingame):
    """A menu made in the units of the 640 x 480 menus (pages.py) put onto the canvas: a page outside a game gets the
    bars of the board at the top and the bottom and its content at 80 % of its old size in the middle; a window, and
    every menu in a game, is made smaller around the middle of the screen the same way."""
    if mf.get(m, 'canvas'):
        return
    full = mf.truth(mf.get(m, 'fullscreen'))
    mr = _rect(m) or [0, 0, 640, 480]
    its = [e for e in m if e.k == 'itemdef']
    cw, ch = (IN_W, IN_H) if ingame else (W, H)
    if ingame:
        k, cx, cy = IN_K, IN_W / 2.0, IN_H / 2.0
    else:
        k, cx, cy = CONTENT_K, W / 2.0, HEAD_H + (H - HEAD_H - FOOT_H) / 2.0
    page = full and not ingame

    def map_y(v):
        # (in a game from the top, where the bar of the menu is; else around the middle of the room for content)
        return v * k if ingame else cy + (v - 240) * k

    names = {(mf.get(e.block, 'name') or [''])[0].strip('"').lower() for e in its}
    has_head = page and 'vision_bar' in names
    has_foot = page and 'vision_foot' in names
    new = []
    here = None
    for e in its:
        b = e.block
        n = (mf.get(b, 'name') or [''])[0].strip('"').lower()
        r = _rect(b)
        if has_head and (n in GRID_HEAD or n in GRID_NAV):
            if n in GRID_NAV and mf.get(b, 'forecolor') == ACCENT:
                here = n
            if n in GRID_HEAD or n in GRID_NAV:
                continue
        if has_foot and n in GRID_FOOT:
            continue
        if r is None:
            new.append(e)
            continue
        x, y, w, h = r
        if page and has_foot and y >= 440:
            # a button of the bar at the bottom: across the width of the canvas, as high as the bar of the board
            fk = (W - 2 * 48.0) / (640 - 2 * 29.0)
            nx, nw = 48 + (x - 29) * fk, w * k
            if x > 320:
                nx = W - 48 - (640 - 29 - (x + w)) * fk - nw
            ny = H - FOOT_H + 12 + (y - 446) * 0.6
            nh = min(44.0, h * k)
            mf.put(b, 'rect', mf.fmt(nx), mf.fmt(ny), mf.fmt(nw), mf.fmt(nh))
            _scale_props(b, k)
            mf.put(b, 'textaligny', mf.fmt((nh - (mf.num((mf.get(b, 'textscale') or ['0.6'])[0]) or 0.6) * 18.0) / 2.0))
            new.append(e)
            continue
        wide = mf.get(b, 'wide') and mf.get(b, 'wide')[0] != '0'
        if full:
            # absolute: around the middle of the screen
            if wide and x <= 0 and w >= 640:
                nx, nw = 0, cw
            else:
                nx, nw = cx + (x - 320) * k, w * k
            mf.put(b, 'rect', mf.fmt(nx), mf.fmt(map_y(y)), mf.fmt(nw), mf.fmt(h * k))
        elif wide and w >= 640 and h >= 480:
            # in a window, what covers the whole screen (the veil that dims the page under it) still covers all of it
            mf.put(b, 'rect', mf.fmt(-(cx + (mr[0] - 320) * k)), mf.fmt(-map_y(mr[1])), mf.fmt(cw), mf.fmt(ch))
        else:
            # in a window: from its corner
            mf.put(b, 'rect', mf.fmt(x * k), mf.fmt(y * k), mf.fmt(w * k), mf.fmt(h * k))
        _scale_props(b, k)
        new.append(e)
    if has_head:
        target = {'newgamebutton': 'joinserver', 'playbutton': 'joinserver', 'profilebutton': 'playerMenu',
                  'controlsbutton': 'controlsMenu', 'setupbutton': 'setup_menu'}.get(here)
        new = header(target) + new
    if has_foot:
        first_btn = next((i for i, e in enumerate(new) if (_rect(e.block) or [0, 0])[1] >= H - FOOT_H), len(new))
        new[first_btn:first_btn] = footer(H - FOOT_H)
    m[:] = [e for e in m if e.k != 'itemdef'] + new
    if not full:
        mf.put(m, 'rect', mf.fmt(cx + (mr[0] - 320) * k), mf.fmt(map_y(mr[1])), mf.fmt(mr[2] * k), mf.fmt(mr[3] * k))
    dx, dy = mf.get(m, 'descx'), mf.get(m, 'descy')
    if dx and dy and mf.num(dx[0]) is not None and mf.num(dy[0]) is not None:
        if page and mf.num(dy[0]) >= 440:
            mf.put(m, 'descX', mf.fmt(W / 2.0))
            mf.put(m, 'descY', mf.fmt(H - FOOT_H + 24))
        else:
            mf.put(m, 'descX', mf.fmt(cx + (mf.num(dx[0]) - 320) * k))
            mf.put(m, 'descY', mf.fmt(map_y(mf.num(dy[0]))))
    ds = mf.get(m, 'descscale')
    if ds and mf.num(ds[0]) is not None:
        mf.put(m, 'descScale', mf.fmt(mf.num(ds[0]) * k))
    mf.put(m, 'canvas', cw, ch)


def profile_page(m):
    """A page of the player outside a game (profile.py builds it in units of the game, its columns as wide as a page of
    752 units): the bars of the board, the strip of the pages of the player under the top bar, the three columns over
    the width of the canvas (twice the units), the texts at the sizes of the board."""
    k = 2.0
    text_k = 17.0 / (18.0 * 0.7)        # a row of the old page (scale 0.7) as a row of the board (17 pixels)
    its = [e for e in m if e.k == 'itemdef']
    new = []
    for e in its:
        b = e.block
        n = (mf.get(b, 'name') or [''])[0].strip('"').lower()
        r = _rect(b)
        if n in GRID_HEAD or n in GRID_NAV or n in GRID_FOOT:
            continue
        if r is None:
            new.append(e)
            continue
        x, y, w, h = r
        if n in ('vision_strip', 'vision_strip_line'):
            continue
        if 30 <= y < 64:
            # an entry of the strip of the pages of the player
            mf.put(b, 'rect', mf.fmt(36 + (x - 29) * k), mf.fmt(HEAD_H), mf.fmt(w * k), mf.fmt(52))
            _scale_props(b, text_k)
            mf.put(b, 'textaligny', mf.fmt((52 - (mf.num((mf.get(b, 'textscale') or ['0.6'])[0]) or 0.6) * 18) / 2))
            mf.put(b, 'textalignx', mf.fmt(w * k / 2))
            new.append(e)
            continue
        if y >= 440:
            # a button of the bar at the bottom
            nx = 48 + (x - 29) * k if x < 320 else W - 48 - 200
            mf.put(b, 'rect', mf.fmt(nx), mf.fmt(H - FOOT_H + 12), mf.fmt(200 if x >= 320 else w * k), mf.fmt(44))
            _scale_props(b, text_k)
            ts = mf.num((mf.get(b, 'textscale') or ['0.6'])[0]) or 0.6
            mf.put(b, 'textaligny', mf.fmt((44 - ts * 18) / 2))
            mf.put(b, 'textalignx', mf.fmt((200 if x >= 320 else w * k) / 2))
            new.append(e)
            continue
        mf.put(b, 'rect', mf.fmt(x * k), mf.fmt(HEAD_H + 52 + 24 + (y - 72) * k), mf.fmt(w * k), mf.fmt(h * k))
        _scale_props(b, k)
        ts = mf.get(b, 'textscale')
        if ts and mf.num(ts[0]) is not None:
            big = mf.num(ts[0])
            mf.put(b, 'textscale', mf.fmt(big * text_k / k))
            # (the text grows by text_k, not k: its box of 18 units moves down by half of what it lost, so its middle
            # stays where the item at twice its size has it)
            ay = mf.get(b, 'textaligny')
            if ay and mf.num(ay[0]) is not None:
                mf.put(b, 'textaligny', mf.fmt(mf.num(ay[0]) + 9.0 * (big - big * text_k / k)))
        new.append(e)
    strip = [fill('vision_sub', (0, HEAD_H, W, 52), RAIL, ('wide', 1)), fill('vision_sub_line', (0, HEAD_H + 51, W, 1), LINE, ('wide', 1))]
    m[:] = [e for e in m if e.k != 'itemdef'] + header('playerMenu') + strip + new
    first_foot = next((i for i, e in enumerate(m) if e.k == 'itemdef' and (_rect(e.block) or [0, 0])[1] >= H - FOOT_H), len(m))
    m[first_foot:first_foot] = footer(H - FOOT_H)
    mf.put(m, 'canvas', W, H)
    mf.put(m, 'descX', W / 2.0)
    mf.put(m, 'descY', H - FOOT_H + 24)
    mf.put(m, 'descScale', scale(13))
