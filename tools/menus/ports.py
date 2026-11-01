"""The main pages outside a game in the pixels of the design canvas (canvas.py), as the boards of the canvas VisionJK
Settings draw them: Home, Join a game, Create a game, Solo game and Quit. pages.py lays these pages out first (the
rules there say what an item of the old page is); here the same items get the places and sizes of the boards, the
bars of the board at the top and the bottom, and "canvas 1600 900"."""
import menufile as mf
from menufile import Entry
from theme import *         # noqa: F401,F403
import canvas as cv
import texts

TOP = cv.HEAD_H
FOOT_Y = cv.H - cv.FOOT_H
SUB_H = 52                  # the strip under the bar of a section: Join a game, Create a game, Solo game, Demos, Rules


def by_name(m):
    out = {}
    for e in m:
        if e.k == 'itemdef':
            n = (mf.get(e.block, 'name') or [''])[0].strip('"').lower()
            out.setdefault(n, []).append(e)
    return out


def drop(m, names):
    names = {n.lower() for n in names}
    m[:] = [e for e in m if not (e.k == 'itemdef' and (mf.get(e.block, 'name') or [''])[0].strip('"').lower() in names)]


def place(e, rect):
    mf.put(e.block, 'rect', *[mf.fmt(v) for v in rect])


def sized(e, px):
    mf.put(e.block, 'font', FONT)
    mf.put(e.block, 'textscale', cv.scale(px))


def middle(e, rect, px, align='center', pad=0):
    """Its words on the middle of its height, at the left (after pad), in the middle or at the right."""
    x, y, w, h = rect
    place(e, rect)
    sized(e, px)
    if align == 'center':
        mf.put(e.block, 'textalign', 'ITEM_ALIGN_CENTER')
        mf.put(e.block, 'textalignx', mf.fmt(w / 2.0))
    elif align == 'right':
        mf.put(e.block, 'textalign', 'ITEM_ALIGN_RIGHT')
        mf.put(e.block, 'textalignx', mf.fmt(w - pad))
    else:
        mf.put(e.block, 'textalign', 'ITEM_ALIGN_LEFT')
        mf.put(e.block, 'textalignx', mf.fmt(pad))
    mf.put(e.block, 'textaligny', mf.fmt((h - px) / 2.0))


def control(e, rect, px=15, kind='outline', align='center', pad=0):
    """An item as a button or field of the board: 'outline', 'primary' (filled with the accent), 'field' (dark, for an
    owner draw that writes its own name and value), 'plain' (only its words)."""
    b = e.block
    for key in ('border', 'bordersize', 'background', 'mouseenter', 'mouseexit', 'flow', 'sheen'):
        mf.drop(b, key)
    if kind == 'plain':
        mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
        mf.put(b, 'forecolor', *cv.NAV_TEXT)
    else:
        mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
        mf.put(b, 'radius', 6)
        mf.put(b, 'backcolor', *(ACCENT if kind == 'primary' else cv.FIELD if kind == 'field' else NONE))
        mf.put(b, 'bordercolor', *(ACCENT if kind == 'primary' else cv.EDGE))
        mf.put(b, 'forecolor', *(ON_ACCENT if kind == 'primary' else cv.NAV_TEXT))
    if kind != 'field':
        mf.put(b, 'caps', 1)
    middle(e, rect, px, align, pad)
    if kind == 'primary':
        mf.put(b, 'font', cv.BOLD)


def frame(m, here, subnav=None):
    """The bars of the board: the one at the top with the sections, the strip of the pages of Play under it (subnav:
    the menu that is open, or None for no strip), the one at the bottom. The bars the page had go."""
    drop(m, cv.GRID_HEAD + cv.GRID_NAV + cv.GRID_FOOT + ('vision_title', 'vision_veil'))
    new = cv.header(here)
    if subnav is not None:
        new += [cv.fill('vision_sub', (0, TOP, cv.W, SUB_H), cv.RAIL, ('wide', 1)),
                cv.fill('vision_sub_line', (0, TOP + SUB_H - 1, cv.W, 1), cv.LINE, ('wide', 1))]
        x = 36
        for name, words, target, width in (('vision_sub_join', '@MENUS_JOIN_SERVER_CAPS', 'joinserver', 190),
                                           ('vision_sub_create', '@MENUS_CREATE_SERVER_CAPS', 'createserver', 210),
                                           ('vision_sub_solo', '@MENUS_SOLO_GAME', 'quickgame', 160),
                                           ('vision_sub_demo', '@MENUS_PLAY_DEMO_CAPS', 'demo', 160),
                                           ('vision_sub_rules', '@MENUS_RULES', 'rulesMenu', 110)):
            on = target == subnav
            act = list(cv.CLICK) + ['close', 'all', ';', 'open', target, ';']
            new.append(cv.text(name, (x, TOP, width, SUB_H), words, 15, BRIGHT if on else cv.SOFT_TEXT, 'center', 0, FONT,
                               ('type', 'ITEM_TYPE_BUTTON'), ('caps', 1), ('action', act)))
            if on:
                new.append(cv.fill(name + '_line', (x + 16, TOP + SUB_H - 2, width - 32, 2), ACCENT))
            x += width + 4
    first = next((i for i, e in enumerate(m) if e.k == 'itemdef'), len(m))
    m[first:first] = new
    m.extend(cv.footer(FOOT_Y))
    mf.put(m, 'canvas', cv.W, cv.H)
    # (the line about the item under the mouse stands in the middle of the bar, clear of its buttons)
    mf.put(m, 'descX', cv.W / 2.0)
    mf.put(m, 'descY', FOOT_Y + 24)
    mf.put(m, 'descAlignment', 'ITEM_ALIGN_CENTER')
    mf.put(m, 'descScale', cv.scale(13))


def footer_buttons(m, left, right, *rest):
    """The buttons of the bar at the bottom, after the bar: words at the left, the one that goes on filled at the right."""
    its = by_name(m)
    x = 32
    for n in left:
        for e in its.get(n.lower(), []):
            control(e, (x, FOOT_Y + 12, 110, 44), 18, 'plain')
            m.remove(e)
            m.append(e)
        if its.get(n.lower()):
            x += 116
    for n in right:
        for e in its.get(n.lower(), []):
            control(e, (cv.W - 48 - 200, FOOT_Y + 12, 200, 44), 15, 'primary')
            m.remove(e)
            m.append(e)


def home(m):
    """Home, as the board "Home": the name of the game and the ways into a game at the left, the player's model in the
    middle, the box of the icon at the right."""
    its = by_name(m)
    frame(m, None)
    for n, rect, px in (('vision_home_mark', (48, 116, 560, 90), 84), ('vision_home_tagline', (48, 210, 560, 32), 22)):
        for e in its.get(n, []):
            middle(e, rect, px, 'left')
    for e in its.get('vision_rule', []):
        place(e, (50, 254, 56, 3))
        mf.put(e.block, 'radius', 1.5)
    cards = (('vision_join', (48, 283, 560, 104), 28, 'primary'),
             ('vision_create', (48, 415, 272, 96), 19, 'card'), ('vision_solo', (336, 415, 272, 96), 19, 'card'),
             ('vision_demo', (48, 527, 272, 96), 19, 'card'), ('vision_rules', (336, 527, 272, 96), 19, 'card'))
    lines = {'vision_join': 'Browse servers and jump in', 'vision_create': 'Host a server for others', 'vision_solo': 'Play against bots',
             'vision_demo': 'Watch a recorded game', 'vision_rules': 'Game types, force, weapons'}
    for n, rect, px, kind in cards:
        for e in its.get(n, []):
            b = e.block
            pad = 28 if kind == 'primary' else 22
            middle(e, rect, px, 'left', pad)
            mf.put(b, 'radius', 10)
            mf.put(b, 'font', cv.BOLD if kind == 'primary' else FONT)
            # the name a little above the middle, a line about it under it (as the board)
            x, y, w, h = rect
            sub = 16 if kind == 'primary' else 14
            mf.put(b, 'textaligny', mf.fmt((h - px - sub - 4) / 2.0))
            m.append(cv.text(n + '_line', (x + pad, y + (h - px - sub - 4) / 2.0 + px + 4, w - 2 * pad, sub + 4), texts.ref(lines[n]), sub,
                             ['0.043', '0.165', '0.251', '1'] if kind == 'primary' else cv.MUTED, 'left', 0, FONT, ('decoration',)))
            if kind == 'primary':
                m.append(cv.text(n + '_arrow', (x + w - 60, y, 40, h), '">"', 30, ON_ACCENT, 'center', 0, cv.BOLD, ('decoration',)))
    # the player's model in the middle, on a soft light, and the way to the profile under it
    for e in its.get('character', []):
        place(e, (760, 140, 300, 600))
    for e in its.get('vision_glow', []):
        glow(e, 910, 724, 300)
    # (an item without a text writes the value of its cvar: the name of the player)
    m.append(cv.item('vision_name', (660, 748, 500, 36), ('cvar', '"name"'), ('font', FONT), ('textscale', cv.scale(26)),
                     ('forecolor', *BRIGHT), ('textalign', 'ITEM_ALIGN_CENTER'), ('textalignx', 250), ('textaligny', 5), ('decoration',)))
    m.append(cv.button('vision_profile', (820, 786, 180, 44), texts.ref('Edit profile'),
                       list(cv.CLICK) + ['close', 'all', ';', 'open', 'playerMenu', ';'], 14, 'outline', '@MENUS_PROFILE_DESC'))
    # the box of the icon at the right, and what is new
    for e in its.get('vision_hero', []):
        place(e, (1172, 120, 380, 330))
        mf.put(e.block, 'radius', 10)
    for e in its.get('vision_cube', []):
        place(e, (1262, 140, 200, 200))
    for e in its.get('vision_hero2', []):
        middle(e, (1172, 350, 380, 30), 18, 'center')
    news = cv.button('vision_news', (1262, 392, 200, 40), '@VISION_WHAT_S_NEW', list(cv.CLICK) + ['open', 'changelog_popmenu', ';'], 14,
                     'outline')
    m.append(news)
    for e in its.get('vision_version', []):
        place(e, (1552, FOOT_Y + 25, 0, 20))
        sized(e, 13)
    for e in its.get('exitgamebutton', []):
        control(e, (32, FOOT_Y + 12, 110, 44), 18, 'plain')
        m.remove(e)
        m.append(e)
    mf.put(m, 'descX', 48)
    mf.put(m, 'descY', 650)
    mf.put(m, 'descScale', cv.scale(15))


def join(m):
    """Join a game, as the board "Play: join a game": the filters over the list, the list of servers, at the right the
    level of the chosen server and what can be done with it."""
    its = by_name(m)
    frame(m, 'joinserver', 'joinserver')
    y0 = TOP + SUB_H + 24
    for n, rect in (('getnewlist_button', (36, y0, 170, 44)), ('refreshfilter_button', (216, y0, 170, 44))):
        for e in its.get(n, []):
            control(e, rect, 14)
    for n, rect in (('netsource', (406, y0, 230, 44)), ('netfilter', (646, y0, 250, 44)), ('gametypefilter', (906, y0, 258, 44))):
        for e in its.get(n, []):
            control(e, rect, 15, 'field', 'left', 14)
    for n, rect in (('viewempty', (36, y0 + 58, 250, 30)), ('viewfull', (296, y0 + 58, 250, 30)), ('viewbotsonly', (556, y0 + 58, 280, 30)),
                    ('datarate', (846, y0 + 58, 318, 30))):
        for e in its.get(n, []):
            place(e, rect)
            sized(e, 15)
            mf.put(e.block, 'textaligny', mf.fmt((30 - 15) / 2.0))
    table = (36, y0 + 106, 1128, FOOT_Y - 24 - (y0 + 106))
    tx, ty, tw, th = table
    m.insert(next(i for i, e in enumerate(m) if e.k == 'itemdef' and (mf.get(e.block, 'name') or [''])[0] == 'serverlist'),
             cv.panel('vision_table', table, cv.CARD, cv.CARD_EDGE, 10))
    for n, x, w in (('server', 12, 520), ('map', 552, 210), ('players', 782, 110), ('type', 912, 120), ('ping', 1052, 64)):
        for e in its.get(n, []):
            middle(e, (tx + x, ty, w, 48), 12, 'left', 10)
            mf.put(e.block, 'font', cv.BOLD)
    m.append(cv.fill('vision_table_line', (tx, ty + 48, tw, 1), cv.CARD_EDGE))
    for e in its.get('serverlist', []):
        b = e.block
        place(e, (tx + 10, ty + 54, tw - 20, th - 54 - 44))
        mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
        mf.put(b, 'elementheight', 48)
        sized(e, 16)
        mf.put(b, 'textaligny', 14)
        mf.put(b, 'columns', 5, 12, 40, 520, 552, 40, 210, 782, 5, 110, 912, 20, 120, 1052, 20, 64)
    for e in its.get('refreshdate', []):
        middle(e, (tx + 22, ty + th - 44, 600, 44), 13, 'left')
        mf.put(e.block, 'forecolor', *cv.MUTED)
    # at the right: the level of the chosen server and the buttons
    px = 1200
    m.append(cv.fill('vision_pane', (px, TOP + SUB_H, cv.W - px, FOOT_Y - TOP - SUB_H), cv.RAIL))
    m.append(cv.fill('vision_pane_line', (px, TOP + SUB_H, 1, FOOT_Y - TOP - SUB_H), cv.LINE))
    drop(m, ('vision_pane',))
    m.append(cv.fill('vision_pane2', (px, TOP + SUB_H, cv.W - px, FOOT_Y - TOP - SUB_H), cv.RAIL))
    for e in its.get('vision_netmap', []):
        # (4:3, as the level shot is)
        place(e, (px + 28, y0, cv.W - px - 56, (cv.W - px - 56) * 3 / 4.0))
        m.remove(e)
        m.append(e)
    y0 += (cv.W - px - 56) * 3 / 4.0 - 210
    # two buttons to a row, as the board: what the chosen server is, and what can be done with it
    bw = (cv.W - px - 56 - 10) / 2.0
    for k, n in enumerate(('serverinfotext', 'addfavorite', 'passwordtext', 'idstext')):
        for e in its.get(n, []):
            control(e, (px + 28 + (k % 2) * (bw + 10), y0 + 234 + (k // 2) * 54, bw, 44), 13)
            m.remove(e)
            m.append(e)
    for e in its.get('delfavorite', []):
        control(e, (px + 28 + bw + 10, y0 + 234, bw, 44), 13)
        m.remove(e)
        m.append(e)
    # finding a player and a server by its address: words in the accent in the foot of the list
    x = tx + tw - 22
    for n in ('createfavoritetext', 'findplayertext'):
        for e in its.get(n, []):
            control(e, (x - 170, ty + th - 44, 170, 44), 13, 'plain', 'right', 0)
            mf.put(e.block, 'forecolor', *ACCENT)
            m.remove(e)
            m.append(e)
        x -= 190
    m.append(cv.fill('vision_table_foot', (tx, ty + th - 44, tw, 1), cv.CARD_EDGE))
    footer_buttons(m, ('back',), ('join_button',))
    m.append(cv.text('vision_join_hint', (cv.W - 48 - 200 - 16 - 400, FOOT_Y, 400, cv.FOOT_H), texts.ref('Double-click a server to join'), 14,
                     cv.MUTED, 'right', 0, FONT, ('decoration',)))


def rows(entries, x, y, w, pitch=52, gap=12):
    """Settings and headings in a column of rows of the board (pages.as_rows, in pixels of the canvas)."""
    import pages
    return pages.as_rows([e.block for e in entries], x, y, w, pitch, gap, px=17)


def create(m):
    """Create a game: the kind of game and the levels at the left, the level that is chosen and the places of the
    players in the middle, the server and its rules at the right."""
    its = by_name(m)
    frame(m, 'joinserver', 'createserver')
    y0 = TOP + SUB_H
    m.append(cv.fill('vision_left', (0, y0, 300, FOOT_Y - y0), cv.RAIL))
    m.append(cv.fill('vision_left_line', (299, y0, 1, FOOT_Y - y0), cv.LINE))
    for e in its.get('gametypefield', []):
        control(e, (24, y0 + 24, 252, 44), 15, 'field', 'left', 14)
    for e in its.get('maplist', []):
        b = e.block
        place(e, (24, y0 + 84, 252, FOOT_Y - y0 - 108))
        mf.put(b, 'elementheight', 40)
        sized(e, 16)
        mf.put(b, 'textaligny', 11)
        mf.put(b, 'radius', 10)
        mf.put(b, 'backcolor', *cv.CARD)
        mf.put(b, 'bordercolor', *cv.CARD_EDGE)
    # the picture of the level, the places of the players under it
    for e in its.get('mappreview', []):
        if mf.get(e.block, 'ownerdraw'):
            place(e, (336, y0 + 30, 528, 396))
        else:
            place(e, (330, y0 + 24, 540, 408))
            mf.put(e.block, 'radius', 10)
    # the 16 places of the players, by their number (the text of each is @MENUS_1_1 ... @MENUS_16): four columns of four
    import re
    for e in [e for n in ('humanbotfield', 'humanbotnonfield') for e in its.get(n, [])]:
        t = (mf.get(e.block, 'text') or [''])[0]
        num = re.match(r'@MENUS_(\d+)', t)
        if not num:
            continue
        i = int(num.group(1)) - 1
        place(e, (336 + (i // 4) * 134, y0 + 452 + (i % 4) * 34, 130, 34))
        sized(e, 15)
    for e in its.get('difficultyfield', []):
        middle(e, (330, y0 + 452 + 4 * 34 + 8, 540, 34), 15, 'left', 0)
    for e in its.get('vision_slots', []):
        place(e, (318, y0 + 440, 564, 4 * 34 + 64))
        mf.put(e.block, 'radius', 10)
    # the server and its rules at the right
    rx = 900
    m.append(cv.fill('vision_right', (rx, y0, cv.W - rx, FOOT_Y - y0), cv.RAIL))
    m.append(cv.fill('vision_right_line', (rx, y0, 1, FOOT_Y - y0), cv.LINE))
    for e in its.get('vision_rules', []):
        m.remove(e)
    rule_items = [e for n in ('expert', 'normal', 'rules_title') for e in its.get(n, []) if cv._rect(e.block)]
    end = rows(rule_items, rx + 36, y0 + 24, cv.W - rx - 72)
    for e in rule_items:
        m.remove(e)
        m.append(e)
    footer_buttons(m, ('back', 'advanced'), ('accept',))


def solo(m, menu):
    """The two pages of a game against bots: the levels and the picture of the chosen one, then the rules as rows with
    the picture beside them."""
    its = by_name(m)
    frame(m, 'joinserver', 'quickgame')
    y0 = TOP + SUB_H
    for e in its.get('title', []):
        middle(e, (48, y0 + 28, 800, 44), 30, 'left')
        mf.put(e.block, 'forecolor', *BRIGHT)
    for e in its.get('solo_gametypefield', []):
        control(e, (48, y0 + 96, 360, 44), 15, 'field', 'left', 14)
    for e in its.get('create_title', []):
        middle(e, (48, y0 + 156, 360, 24), 12, 'left')
        mf.put(e.block, 'font', cv.BOLD)
    for e in its.get('maplist', []):
        b = e.block
        place(e, (48, y0 + 188, 360, FOOT_Y - y0 - 212))
        mf.put(b, 'elementheight', 40)
        sized(e, 16)
        mf.put(b, 'textaligny', 11)
        mf.put(b, 'radius', 10)
    drop(m, ('vision_rules',))
    # (leaving the game is on Home: not a button of a page of Play)
    drop(m, ('exitgamebutton', 'exitgame_button'))
    if menu == 'quickgame':
        map_preview(its, (446, y0 + 102, 1100, FOOT_Y - y0 - 138))
        footer_buttons(m, ('backbutton',), ('next_button',))
        return
    rule_items = [e for n in ('options', 'normal', 'expert_button', 'difficulty_button', 'custombots_button', 'rules_title',
                              'end_game_title', 'bots_title') for e in its.get(n, [])]
    m.append(cv.panel('vision_rules2', (36, y0 + 90, 600, FOOT_Y - y0 - 114), cv.CARD, cv.CARD_EDGE, 10))
    rows(rule_items, 58, y0 + 100, 556, 44, 8)
    for e in rule_items:
        if mf.get(e.block, 'ownerdraw') and mf.get(e.block, 'rect'):
            # (an owner draw writes its name and value itself: in a dark field like Type at the left)
            x, y, w, h = (mf.num(v) or 0 for v in mf.get(e.block, 'rect')[:4])
            control(e, (x, y + 2, w, 40), 15, 'field', 'left', 14)
        m.remove(e)
        m.append(e)
    map_preview(its, (678, y0 + 102, 868, FOOT_Y - y0 - 138))
    footer_buttons(m, ('back_button',), ('next_button',))


def quit_page(m):
    """Leaving the game: a dialog in the middle of the page, as the board "Dialogs" draws it."""
    its = by_name(m)
    frame(m, None)
    drop(m, ('vision_dialog',))
    first = next((i for i, e in enumerate(m) if e.k == 'itemdef' and (mf.get(e.block, 'name') or [''])[0].strip('"').lower()
                  in ('quit_title', 'creditsbutton')), len(m))
    m.insert(first, cv.panel('vision_dialog', (560, 330, 480, 220), ['0.039', '0.071', '0.133', '0.96'], cv.EDGE, 12, ('shadow', 40)))
    for e in its.get('quit_title', []):
        middle(e, (590, 356, 420, 20), 12, 'left')
        mf.put(e.block, 'font', cv.BOLD)
    for e in its.get('confirm', []):
        middle(e, (590, 384, 420, 40), 26, 'left')
    for e in its.get('creditsbutton', []):
        place(e, (590, 350, 420, 30))
    for e in its.get('quitgame_cancel', []):
        control(e, (762, 478, 120, 44), 15)
    for e in its.get('quitgame_yes', []):
        mf.put(e.block, 'text', '@MENUS_QUIT')
        control(e, (892, 478, 120, 44), 15, 'primary')
        mf.put(e.block, 'backcolor', *RED)
        mf.put(e.block, 'bordercolor', *RED)
        mf.put(e.block, 'forecolor', '0.16', '0.03', '0.02', '1')


def glow(e, cx, y, w):
    """The soft light under a figure: only the glow around a thin line (Skin_Soft), no pill."""
    place(e, (cx - w / 2.0, y, w, 4))
    mf.put(e.block, 'backcolor', *(ACCENT[:3] + ['0']))
    mf.put(e.block, 'glowcolor', *(ACCENT[:3] + ['0.3']))
    mf.put(e.block, 'radius', 2)


def fit43(x, y, w, h):
    """The largest 4:3 rectangle in the middle of x, y, w, h: a level shot of the game is 4:3 and must not be
    stretched."""
    if w * 3 > h * 4:
        nw = h * 4 / 3.0
        return (x + (w - nw) / 2.0, y, nw, h)
    nh = w * 3 / 4.0
    return (x, y + (h - nh) / 2.0, w, nh)


def map_preview(its, area):
    """The picture of a level (owner draw) at 4:3 in the area, its frame 6 pixels around it."""
    x, y, w, h = fit43(*area)
    for e in its.get('mappreview', []):
        if mf.get(e.block, 'ownerdraw'):
            place(e, (x, y, w, h))
        else:
            place(e, (x - 6, y - 6, w + 12, h + 12))
            mf.put(e.block, 'radius', 10)


def _script(e, key):
    s = mf.find(e.block, key) if hasattr(e, 'block') else mf.find(e, key)
    return list(s.block) if s and isinstance(s.block, list) else []


def _label(name, x, y, w, words):
    """A small heading in capitals over a group: NAME, CHARACTER, STYLE, ..."""
    return cv.text(name, (x, y, w, 18), words, 12, cv.MUTED, 'left', 0, cv.BOLD, ('caps', 1), ('decoration',))


def _listbox(e, rect, cell, horizontal=False):
    b = e.block
    place(e, rect)
    for key in ('page', 'tab', 'group', 'border', 'bordersize', 'background'):
        mf.drop(b, key)
    mf.put(b, 'elementwidth', mf.fmt(cell))
    mf.put(b, 'elementheight', mf.fmt(cell))
    mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
    mf.put(b, 'backcolor', *cv.FIELD)
    mf.put(b, 'bordercolor', *cv.EDGE)
    mf.put(b, 'radius', 6)
    mf.put(b, 'visible', 1)
    if horizontal:
        mf.put(b, 'horizontalscroll')


def profile_all(m, extra):
    """The player outside a game as the board "Profile": one page instead of three. At the left the name, the models to
    choose from, the colour and the parts of a character of your own (from player2.menu), in the middle the figure,
    at the right the lightsaber (from saber.menu): its type, the colour of the blade(s) and the hilt. extra: the menus
    player2.menu and saber.menu as theme.py made them."""
    import profile as pr
    import texts
    its = by_name(m)
    two, sab = by_name(extra['player2.menu']), by_name(extra['saber.menu'])
    get = lambda table, n: list(table.get(n, []))  # noqa: E731
    new = []
    LX, LW = 48, 384            # the column at the left (460 wide with its margins)
    MX, MW = 460, 700           # the middle
    RX, RW = 1188, 364          # the column at the right (440 wide)
    bottom = FOOT_Y

    new += [cv.fill('vision_left', (0, TOP, 460, bottom - TOP), cv.RAIL), cv.fill('vision_left_line', (459, TOP, 1, bottom - TOP), cv.LINE),
            cv.fill('vision_right', (1160, TOP, 440, bottom - TOP), cv.RAIL), cv.fill('vision_right_line', (1160, TOP, 1, bottom - TOP), cv.LINE)]

    # the left: name, models, colour, the parts of your own character
    y = TOP + 28
    new.append(_label('vision_l_name', LX, y, LW, '@MENUS_NAME'))
    for e in get(its, 'namefield'):
        # (a row without a name is its widget alone: the field over the whole width)
        mf.put(e.block, 'text', '""')
        mf.put(e.block, 'row', 3)
        place(e, (LX, y + 24, LW, 48))
        sized(e, 18)
        mf.put(e.block, 'forecolor', *BRIGHT)
        mf.put(e.block, 'maxPaintChars', 24)
        new.append(e)
    y += 96
    new.append(_label('vision_l_model', LX, y, LW, texts.ref('Character')))
    for e in get(its, 'headlist'):
        # six to a row, as the board
        _listbox(e, (LX, y + 24, LW, 3 * 61 + 6), 61)
        new.append(e)
    y += 24 + 3 * 61 + 6 + 24
    new.append(_label('vision_l_color', LX, y, LW, '@MENUS_COLOR'))
    for e in get(two, 'colorbox'):
        _listbox(e, (LX, y + 24, LW, 52), 44, True)
        act = mf.find(e.block, 'action')
        act.block = list(act.block) + ['uiScript', 'character', ';', 'uiScript', 'updatecharmodel', ';']
        new.append(e)
    y += 24 + 52 + 24
    new.append(_label('vision_l_look', LX, y, LW, '@MENUS_APPEARANCE'))
    y += 22
    for e in get(two, 'speciesbut'):
        mf.put(e.block, 'text', '@MENUS_SPECIES')
        for key in ('group', 'page'):
            mf.drop(e.block, key)
        mf.put(e.block, 'row', 1)
        # (a row has its name 28 pixels in, room for the dot of a changed setting: its name under the others)
        place(e, (LX - 28, y, LW + 40, cv.ROW))
        sized(e, 16)
        act = mf.find(e.block, 'action')
        act.block = list(act.block) + ['uiScript', 'updatecharmodel', ';']
        new.append(e)
    y += cv.ROW
    for n, (box, words) in enumerate((('headlistbox', '@MENUS_HEAD'), ('torsolistbox', '@MENUS_TORSO'), ('lowerlistbox', '@MENUS_LEGS'))):
        new.append(cv.text('vision_l_part%d' % n, (LX, y, 150, cv.ROW), words, 16, cv.NAV_TEXT, 'left', 0, FONT, ('decoration',)))
        for e in get(two, box):
            _listbox(e, (LX + LW - 220, y + 4, 220, cv.ROW - 8), cv.ROW - 12, True)
            act = mf.find(e.block, 'action')
            act.block = ['uiScript', 'character', ';'] + list(act.block) + ['uiScript', 'updatecharmodel', ';']
            new.append(e)
        new.append(cv.fill('vision_l_rule%d' % n, (LX, y + cv.ROW - 1, LW, 1), cv.CARD_EDGE))
        y += cv.ROW

    # the middle: the figure on its glow, the name under it
    for e in get(its, 'vision_glow') + get(its, 'character'):
        if pr.name_of(e.block) == 'character':
            place(e, (MX + 150, TOP + 40, MW - 300, 640))
        new.append(e)
    halo = cv.panel('vision_glow2', (0, 0, 1, 1), NONE, NONE, 2)
    glow(halo, MX + MW / 2.0, TOP + 662, 340)
    new.append(halo)
    new.append(cv.item('vision_who', (MX, TOP + 690, MW, 40), ('type', 'ITEM_TYPE_TEXT'), ('cvar', '"ui_Name"'), ('font', FONT),
                       ('textscale', cv.scale(30)), ('forecolor', *BRIGHT), ('textalign', 'ITEM_ALIGN_CENTER'),
                       ('textalignx', mf.fmt(MW / 2)), ('textaligny', 5), ('decoration',)))

    # the right: the saber, its type, the colour of its blade(s), its hilt
    gone = {'show': 1, 'hide': 1, 'transition2': 7}
    y = TOP + 28
    new.append(cv.panel('vision_saberbox', (RX, y, RW, 92), cv.FIELD, cv.CARD_EDGE, 10, ('decoration',)))
    for n, name in enumerate(('saber', 'saber2')):
        for e in get(sab, name):
            # (the model is drawn in a square: the blade lies across the middle of the box)
            # (the saber lies at about two thirds of the height of its square, measured)
            side = 330
            place(e, (RX + (RW - side) / 2.0, y + 46 - 0.68 * side + n * 18, side, side))
            new.append(e)
    y += 92 + 22
    new.append(_label('vision_r_style', RX, y, RW, texts.ref('Style')))
    new.append(cv.panel('vision_r_seg', (RX, y + 24, RW, 44), cv.FIELD, cv.EDGE, 6, ('decoration',)))
    seg_w = (RW - 6) / 3.0
    for n, name in enumerate(('typebut_single', 'typebut_dual', 'typebut_staff')):
        for e in get(sab, name):
            act = mf.find(e.block, 'action')
            act.block = pr.without(act.block, gone)
            control(e, (RX + 3 + n * seg_w, y + 27, seg_w, 38), 12, 'plain')
            # (the scripts of the menu colour the type that is set: setitemcolor in onOpen and the actions)
            new.append(e)
    y += 24 + 44 + 22
    new.append(_label('vision_r_blade', RX, y, RW, '@MENUS_BLADE_COLOR'))
    second_y = y + 24 + 44 + 16
    for table_name, entries in sab.items():
        for e in entries:
            n = pr.name_of(e.block)
            color = n[:-5] if n.endswith('icon2') else n[:-4] if n.endswith('icon') else None
            if color not in pr.ICONS:
                continue
            second = n.endswith('icon2')
            k = pr.ICONS.index(color)
            place(e, (RX + k * 52, (second_y + 24 if second else y + 24), 44, 44))
            for key in ('mouseenter', 'mouseexit'):
                mf.drop(e.block, key)
            if second:
                mf.put(e.block, 'cvarTest', 'ui_saber_type')
                mf.drop(e.block, 'showcvar')
                e.block.append(Entry('showCvar', [], ['"dual"']))
            new.append(e)
    new.append(cv.text('vision_r_blade2', (RX, second_y, RW, 18), '@MENUS_COLOR2', 12, cv.MUTED, 'left', 0, cv.BOLD, ('caps', 1),
                       ('decoration',), ('cvarTest', 'ui_saber_type'), ('showCvar', [], ['"dual"'])))
    # the hilt: right under the colour of the blade, or under the second colour for two sabers
    y1, y2 = second_y, second_y + 24 + 44 + 22
    dual = (('cvarTest', 'ui_saber_type'), ('showCvar', [], ['"dual"']))
    new.append(cv.text('vision_r_hilt', (RX, y1, RW, 18), texts.ref('Hilt'), 12, cv.MUTED, 'left', 0, cv.BOLD, ('caps', 1), ('decoration',),
                       ('cvarTest', 'ui_saber_type'), ('hideCvar', [], ['"dual"'])))
    new.append(cv.text('vision_r_hilt2', (RX, y2, RW, 18), texts.ref('Hilt'), 12, cv.MUTED, 'left', 0, cv.BOLD, ('caps', 1), ('decoration',),
                       *dual))
    for name, top, high in (('hiltbut', y1 + 24, bottom - y1 - 52), ('hiltbut_staves', y1 + 24, bottom - y1 - 52),
                            ('hiltbut1', y2 + 24, (bottom - y2 - 64) / 2), ('hiltbut2', y2 + 36 + (bottom - y2 - 64) / 2, (bottom - y2 - 64) / 2)):
        for e in get(sab, name):
            b = e.block
            place(e, (RX, top, RW, high))
            for key in ('border', 'bordersize', 'background'):
                mf.drop(b, key)
            mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
            mf.put(b, 'backcolor', *cv.FIELD)
            mf.put(b, 'bordercolor', *cv.EDGE)
            mf.put(b, 'radius', 6)
            mf.put(b, 'elementheight', 40)
            sized(e, 16)
            new.append(e)

    # the bars, the scripts of the three menus as one
    m[:] = [e for e in m if e.k != 'itemdef']
    m.extend(cv.header('playerMenu'))
    m.extend(new)
    m.extend(cv.footer(FOOT_Y))
    esc = _script(m, 'onesc') or ['close', 'playerMenu', ';', 'open', 'main', ';']
    back = list(cv.CLICK) + [w for w in esc if w != mf.NL and not w.lower().startswith(('play', '"sound'))]
    m.append(cv.button('vision_back', (32, FOOT_Y + 12, 110, 44), '@MENUS_BACK', back, 18, 'plain', '@MENUS_BACK'))
    opens = _script(m, 'onopen') + ['uiScript', 'getcharcvars', ';'] + pr.without(_script(extra['saber.menu'], 'onopen'), gone)
    closes = _script(m, 'onclose') + ['uiScript', 'updatesabercvars', ';']
    for key, words in (('onOpen', opens), ('onClose', closes)):
        mf.drop(m, key.lower())
        m.append(Entry(key, [], words))
    mf.put(m, 'canvas', cv.W, cv.H)
    mf.put(m, 'descX', cv.W / 2.0)
    mf.put(m, 'descY', FOOT_Y + 24)
    mf.put(m, 'descAlignment', 'ITEM_ALIGN_CENTER')
    mf.put(m, 'descScale', cv.scale(13))


def _drop_rails(m):
    """The side panel the old frame of a page put in (pages.shell): the boards have none."""
    m[:] = [e for e in m if not (e.k == 'itemdef' and (mf.get(e.block, 'name') or [''])[0].strip('"').lower().startswith('vision_rail'))]


def demo(m):
    """Play a demo: the list of demos over the width of the page, Play at the right of the bar at the bottom."""
    its = by_name(m)
    drop(m, ('exitgamebutton', 'creditsbutton'))
    _drop_rails(m)
    frame(m, 'joinserver', 'demo')
    y0 = TOP + SUB_H + 24
    for e in its.get('default_rulesheader', []):
        middle(e, (36, y0, 800, 41), 30, 'left')
        mf.put(e.block, 'forecolor', *BRIGHT)
    for e in its.get('demolist', []):
        b = e.block
        box = (36, y0 + 64, cv.W - 72, FOOT_Y - y0 - 88)
        place(e, box)
        mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
        mf.put(b, 'backcolor', *cv.CARD)
        mf.put(b, 'bordercolor', *cv.CARD_EDGE)
        mf.put(b, 'radius', 10)
        mf.put(b, 'elementheight', 44)
        sized(e, 16)
        mf.put(b, 'textaligny', 12)
        mf.put(b, 'textalignx', 22)
    footer_buttons(m, ('back',), ('begin',))


def rules(m):
    """The rules: one card for each part, three to a row."""
    its = by_name(m)
    drop(m, ('exitgamebutton',))
    _drop_rails(m)
    frame(m, 'joinserver', 'rulesMenu')
    y0 = TOP + SUB_H + 24
    for e in its.get('default_rulesheader', []):
        middle(e, (36, y0, 800, 41), 30, 'left')
        mf.put(e.block, 'forecolor', *BRIGHT)
    cards = [e for e in m if e.k == 'itemdef' and (mf.get(e.block, 'name') or [''])[0].strip('"').lower() in
             ('gamesmenubutton', 'forcemenubutton', 'weaponmenubutton', 'itemmenubutton')]
    w = (cv.W - 72 - 2 * 16) / 3.0
    for k, e in enumerate(cards):
        control(e, (36 + (k % 3) * (w + 16), y0 + 64 + (k // 3) * 112, w, 96), 19, 'outline', 'left', 22)
        mf.put(e.block, 'radius', 10)
        mf.put(e.block, 'backcolor', *cv.CARD)
        mf.put(e.block, 'bordercolor', *cv.CARD_EDGE)
    footer_buttons(m, ('backbutton',), ())


def _rect_of(e):
    r = mf.get(e.block, 'rect')
    return tuple(mf.num(v) or 0.0 for v in r[:4]) if r else None


def rules_page(m):
    """A part of the rules (games, force powers, weapons, items): its entries as a list at the left as the pages of
    Setup, what the chosen one is on a card at the right: its picture at the top, its text under it."""
    drop(m, ('exitgamebutton', 'creditsbutton'))
    _drop_rails(m)
    its = [e for e in m if e.k == 'itemdef']
    names = lambda e: (mf.get(e.block, 'name') or [''])[0].strip('"').lower()  # noqa: E731
    bg = next((e for e in its if names(e) == 'setup_background'), None)
    old_bg = _rect_of(bg) if bg else None
    frame(m, 'joinserver', 'rulesMenu')
    y0 = TOP + SUB_H + 24
    card = (420, y0 + 64, cv.W - 456, FOOT_Y - y0 - 88)
    cx, cy, cw, ch = card
    for e in its:
        n = names(e)
        r = _rect_of(e)
        if n.endswith('header') or n == 'rules_title':
            middle(e, (36, y0, 1000, 41), 30, 'left')
            mf.put(e.block, 'forecolor', *BRIGHT)
    buttons = [e for e in its if names(e).endswith('menubutton')]
    pitch = min(44.0, (FOOT_Y - 16 - (y0 + 64)) / max(1, len(buttons)))
    for k, e in enumerate(buttons):
        control(e, (20, y0 + 64 + k * pitch, 380, pitch - 2), 15, 'plain', 'left', 16)
        mf.put(e.block, 'forecolor', *cv.SOFT_TEXT)
    if bg:
        place(bg, card)
        for key in ('background', 'border', 'bordersize'):
            mf.drop(bg.block, key)
        mf.put(bg.block, 'style', 'WINDOW_STYLE_PANEL')
        mf.put(bg.block, 'backcolor', *cv.CARD)
        mf.put(bg.block, 'bordercolor', *cv.CARD_EDGE)
        mf.put(bg.block, 'radius', 10)
    if not old_bg:
        footer_buttons(m, ('backbutton',), ())
        return
    bx, by, bw, bh = old_bg
    for e in its:
        n = names(e)
        r = _rect_of(e)
        if not n.endswith('_desc') or not r:
            continue
        if (mf.get(e.block, 'type') or [''])[0] == 'ITEM_TYPE_TEXTSCROLL':
            # the text: from where it began on the old panel to the foot of the card
            top = cy + 28 + max(0.0, (r[1] - by) / bh * ch * 0.55)
            place(e, (cx + 32, top, cw - 64, cy + ch - 24 - top))
            sized(e, 17)
            mf.put(e.block, 'forecolor', *cv.NAV_TEXT)
        else:
            # a picture: as large as before a little more, across the card where it stood on the old panel
            k = 1.4
            w, h = r[2] * k * 1.25, r[3] * k * 1.25
            mid = cx + (r[0] + r[2] / 2.0 - bx) / bw * cw
            place(e, (mid - w / 2.0, cy + 28, w, h))
    footer_buttons(m, ('backbutton',), ())


def rules_moves(m):
    """The list of moves: the type of moves and the moves at the left, the figure that shows the chosen one on a card
    at the right with what it is under it."""
    its = by_name(m)
    drop(m, ('exitgamebutton', 'creditsbutton', 'move_titles_box', 'move_titles_outline', 'moveslist_title_box'))
    _drop_rails(m)
    frame(m, 'joinserver', 'rulesMenu')
    y0 = TOP + SUB_H + 24
    m.append(cv.text('vision_moves_title', (36, y0, 800, 41), '@MENUS_MOVES', 30, BRIGHT, 'left', 0, FONT, ('caps', 1), ('decoration',)))
    y = y0 + 64
    for e in its.get('move_titles_title', []):
        middle(e, (36, y, 400, 18), 12, 'left')
        mf.put(e.block, 'font', cv.BOLD)
        mf.put(e.block, 'forecolor', *cv.MUTED)
    for e in its.get('move_titles', []):
        # (a row without a name is its widget alone: the arrows and the type)
        mf.put(e.block, 'text', '""')
        mf.put(e.block, 'row', 1)
        place(e, (36, y + 24, 400, 44))
        sized(e, 15)
    y += 24 + 44 + 24
    for e in its.get('moveslist_title', []):
        middle(e, (36, y, 400, 18), 12, 'left')
        mf.put(e.block, 'font', cv.BOLD)
        mf.put(e.block, 'forecolor', *cv.MUTED)
    for e in its.get('moveslist', []):
        _listbox(e, (36, y + 24, 400, FOOT_Y - 24 - (y + 24)), 40)
        mf.drop(e.block, 'elementwidth')
        mf.put(e.block, 'elementheight', 40)
        sized(e, 16)
        mf.put(e.block, 'textaligny', 10)
        mf.put(e.block, 'textalignx', 14)
    card = (468, y0 + 64, cv.W - 504, FOOT_Y - 24 - (y0 + 64))
    cx, cy, cw, ch = card
    for e in its.get('character_box', []):
        place(e, card)
        for key in ('background', 'border', 'bordersize'):
            mf.drop(e.block, key)
        mf.put(e.block, 'style', 'WINDOW_STYLE_PANEL')
        mf.put(e.block, 'backcolor', *cv.CARD)
        mf.put(e.block, 'bordercolor', *cv.CARD_EDGE)
        mf.put(e.block, 'radius', 10)
    for e in its.get('character', []):
        place(e, (cx + cw / 2.0 - 260, cy - 60, 520, ch - 70))
    for e in its.get('item_desc', []):
        place(e, (cx + 32, cy + ch - 150, cw - 64, 130))
        sized(e, 16)
        mf.put(e.block, 'forecolor', *cv.NAV_TEXT)
    footer_buttons(m, ('backbutton',), ())


DIALOG = ['0.039', '0.071', '0.133', '0.96']       # the card of a dialog (as the quit dialog)


def dialog(m, w, h, kicker=None, title=None, body=(), buttons=(), primary=None):
    """A window as the board "Dialogs" draws one: the page dimmed, a card in the middle with a small heading in
    capitals, a title, its content, the buttons at the bottom right (primary: filled with the accent, at the
    right). kicker, title: words, or an item of the menu. body: (item, height, px) from the top down, an item of
    height 0 is a line of words. buttons: items, from the left. Items of the menu that are not named go."""
    x0, y0 = (cv.W - w) / 2.0, (cv.H - h) / 2.0
    keep = {id(e) for e in [kicker, title] + [b[0] for b in body] + list(buttons) + [primary] if e is not None and not isinstance(e, str)}
    m[:] = [e for e in m if e.k != 'itemdef' or id(e) in keep]
    new = [cv.fill('vision_veil', (-x0, -y0, cv.W, cv.H), ['0', '0', '0', '0.55'], ('wide', 1)),
           cv.panel('vision_dialog', (0, 0, w, h), DIALOG, cv.EDGE, 12, ('shadow', 40))]
    y = 24
    if kicker is not None:
        if isinstance(kicker, str):
            new.append(cv.text('vision_kicker', (28, y, w - 56, 18), kicker, 12, cv.MUTED, 'left', 0, cv.BOLD, ('caps', 1), ('decoration',)))
        else:
            middle(kicker, (28, y, w - 56, 18), 12, 'left')
            mf.put(kicker.block, 'font', cv.BOLD)
            mf.put(kicker.block, 'forecolor', *cv.MUTED)
            mf.put(kicker.block, 'caps', 1)
            new.append(kicker)
        y += 26
    if title is not None:
        if isinstance(title, str):
            new.append(cv.text('vision_title', (28, y, w - 56, 34), title, 24, BRIGHT, 'left', 0, FONT, ('decoration',)))
        else:
            middle(title, (28, y, w - 56, 34), 24, 'left')
            mf.put(title.block, 'forecolor', *BRIGHT)
            mf.drop(title.block, 'caps')
            new.append(title)
        y += 46
    for e, high, px in body:
        if high == 0:
            middle(e, (28, y, w - 56, px + 8), px, 'left')
            mf.put(e.block, 'forecolor', *cv.MUTED)
            y += px + 10
        else:
            place(e, (28, y, w - 56, high))
            if px:
                sized(e, px)
            y += high + 12
        new.append(e)
    x = w - 28
    if primary is not None:
        x -= 220
        control(primary, (x, h - 28 - 44, 220, 44), 15, 'primary')
    for e in reversed(list(buttons)):
        x -= 12 + 180
        control(e, (x, h - 28 - 44, 180, 44), 15)
    new += [e for e in buttons] + ([primary] if primary is not None else [])
    m[:] = [e for e in m if e.k != 'itemdef'] + new
    mf.put(m, 'canvas', cv.W, cv.H)
    mf.put(m, 'rect', mf.fmt(x0), mf.fmt(y0), w, h)
    mf.put(m, 'style', 'WINDOW_STYLE_EMPTY')
    for key in ('descx', 'descy', 'descalignment', 'descscale', 'border', 'bordersize', 'background'):
        mf.drop(m, key)


def popups(m, menu):
    """The windows outside a game that the board "Dialogs" draws, as dialogs (dialog()). True if it was one."""
    import texts
    its = by_name(m)
    one = lambda n: (its.get(n) or [None])[0]  # noqa: E731
    if menu == 'updatechannelmenu':
        dialog(m, 560, 300, texts.ref('Update Channel'), one('updatechannel_title'),
               [(one(n), 0, 15) for n in ('updatechannel_line1', 'updatechannel_line2', 'updatechannel_line3') if one(n)],
               [one('updatechannel_no')], one('updatechannel_yes'))
        return True
    if menu == 'ids_popmenu':
        lst = one('ids_list')
        dialog(m, 900, 560, None, one('ids_title'), [(lst, 320, 14), (one('ids_hint'), 0, 14)], [], one('ids_done'))
        mf.put(lst.block, 'backcolor', *cv.FIELD)
        mf.put(lst.block, 'bordercolor', *cv.EDGE)
        mf.put(lst.block, 'radius', 6)
        return True
    if menu == 'password_popmenu':
        field = one('passwordentry')
        dialog(m, 480, 230, None, one('passwordtitle'), [(field, 44, 16)], [], one('donetext'))
        # (a row without a name is its widget alone: one field, not a field in a box)
        mf.put(field.block, 'style', 'WINDOW_STYLE_EMPTY')
        mf.put(field.block, 'text', '""')
        mf.put(field.block, 'row', 3)
        return True
    if menu == 'error_popmenu':
        labels = its.get('errorinfo', [])
        label = next((e for e in labels if not mf.get(e.block, 'cvar')), None)
        text = next((e for e in labels if mf.get(e.block, 'cvar')), None)
        dialog(m, 560, 320, label, None, [(text, 180, 17)] if text else [], [], one('exit'))
        if text:
            mf.put(text.block, 'autowrapped')
            mf.put(text.block, 'forecolor', *BRIGHT)
            mf.put(text.block, 'textalignx', 0)
            mf.put(text.block, 'textaligny', 0)
            mf.put(text.block, 'textalign', 'ITEM_ALIGN_LEFT')
        return True
    return False
