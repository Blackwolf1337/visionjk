"""The main pages outside a game in the pixels of the design canvas (canvas.py), as the boards of the canvas VisionJK
Settings draw them: Home, Join a game, Create a game, Solo game and Quit. pages.py lays these pages out first (the
rules there say what an item of the old page is); here the same items get the places and sizes of the boards, the
bars of the board at the top and the bottom, and "canvas 1600 900"."""
import menufile as mf
from menufile import Entry
from theme import *         # noqa: F401,F403
import canvas as cv

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
    mf.put(m, 'descX', 48)
    mf.put(m, 'descY', FOOT_Y + 24)
    mf.put(m, 'descAlignment', 'ITEM_ALIGN_LEFT')
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
    for n, rect, px, kind in cards:
        for e in its.get(n, []):
            b = e.block
            middle(e, rect, px, 'left', 28 if kind == 'primary' else 22)
            mf.put(b, 'radius', 10)
            mf.put(b, 'font', cv.BOLD if kind == 'primary' else FONT)
    # the player's model in the middle, on a soft light, and the way to the profile under it
    for e in its.get('character', []):
        place(e, (760, 140, 300, 600))
    for e in its.get('vision_glow', []):
        place(e, (800, 714, 220, 28))
        mf.put(e.block, 'radius', 14)
    # (an item without a text writes the value of its cvar: the name of the player)
    m.append(cv.item('vision_name', (660, 748, 500, 36), ('cvar', '"name"'), ('font', FONT), ('textscale', cv.scale(26)),
                     ('forecolor', *BRIGHT), ('textalign', 'ITEM_ALIGN_CENTER'), ('textalignx', 250), ('textaligny', 5), ('decoration',)))
    m.append(cv.button('vision_profile', (835, 786, 150, 40), '@MENUS_PROFILE', list(cv.CLICK) + ['close', 'all', ';', 'open', 'playerMenu', ';'],
                       14, 'outline', '@MENUS_PROFILE_DESC'))
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
        place(e, (px + 28, y0, cv.W - px - 56, 210))
        m.remove(e)
        m.append(e)
    y = y0 + 234
    for n in ('serverinfotext', 'addfavorite', 'passwordtext', 'createfavoritetext', 'findplayertext', 'idstext'):
        for e in its.get(n, []):
            control(e, (px + 28, y, cv.W - px - 56, 44), 14)
            m.remove(e)
            m.append(e)
        y += 54
    for e in its.get('delfavorite', []):
        control(e, (px + 28, y0 + 234 + 54, cv.W - px - 56, 44), 14)
        m.remove(e)
        m.append(e)
    footer_buttons(m, ('back',), ('join_button',))


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
    if menu == 'quickgame':
        for e in its.get('mappreview', []):
            if mf.get(e.block, 'ownerdraw'):
                place(e, (446, y0 + 102, 1106 - 12, FOOT_Y - y0 - 126 - 12))
            else:
                place(e, (440, y0 + 96, 1112, FOOT_Y - y0 - 120))
                mf.put(e.block, 'radius', 10)
        footer_buttons(m, ('backbutton', 'exitgamebutton'), ('next_button',))
        return
    rule_items = [e for n in ('options', 'normal', 'expert_button', 'difficulty_button', 'custombots_button', 'rules_title',
                              'end_game_title', 'bots_title') for e in its.get(n, [])]
    m.append(cv.panel('vision_rules2', (36, y0 + 90, 600, FOOT_Y - y0 - 114), cv.CARD, cv.CARD_EDGE, 10))
    rows(rule_items, 58, y0 + 100, 556, 48, 10)
    for e in rule_items:
        m.remove(e)
        m.append(e)
    for e in its.get('mappreview', []):
        if mf.get(e.block, 'ownerdraw'):
            place(e, (678, y0 + 102, 868, 548))
        else:
            place(e, (672, y0 + 96, 880, 560))
            mf.put(e.block, 'radius', 10)
    footer_buttons(m, ('back_button', 'exitgame_button'), ('next_button',))


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
        control(e, (892, 478, 120, 44), 15, 'primary')
        mf.put(e.block, 'backcolor', *RED)
        mf.put(e.block, 'bordercolor', *RED)
        mf.put(e.block, 'forecolor', '0.16', '0.03', '0.02', '1')
