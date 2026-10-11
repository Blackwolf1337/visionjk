"""What single pages get beyond the rules of theme.py (tools/menus/build.py): the frame of the pages outside a
game (a bar at the top with the four sections, a title, the buttons at the bottom), and the pages that are laid
out anew."""
import os

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


# ---- settings that had no entry: new tabs of the two setup menus, and more rows for the tab of the sound.
# A row: (kind, label, cvar, what it is for, ...). slider: default, least, most. multi: pairs of name and value.
TABS = [
    ('interface', 'INTERFACE', 'How menus, HUD and camera look.', [
        ('yesno', 'New HUD', 'cg_hud', 'The HUD of VisionJK. Off: the HUD of the game.'),
        ('yesno', 'Classic Menus', 'ui_classic', 'The menus of the game as they were. Counts from the next start of the game.'),
        ('yesno', 'Wide Screen Layout', 'r_2dAspect', 'Menus and HUD keep their shape on a wide window. Counts after a restart of the video.'),
        ('yesno', 'Menu in the Level', 'cg_immersiveMenu', 'In a game an open menu stands in the level next to you.'),
        ('slider', 'Menu Blur', 'r_menuBlur', 1, 0, 4, 'How far the level behind an open menu is out of focus.'),
        ('slider', 'Field of View', 'cg_fov', 80, 60, 130, 'How wide you see.'),
        ('slider', 'Camera Distance', 'cg_thirdPersonRange', 80, 40, 200, 'How far the camera is behind you in third person.'),
        ('slider', 'Crosshair Size', 'cg_crosshairSize', 24, 12, 64, 'The size of the crosshair.'),
        ('multi', 'Frame Limit', 'com_maxfps', [('60', 60), ('90', 90), ('125', 125), ('144', 144), ('165', 165), ('240', 240), ('None', 0)],
         'The most frames a second the game draws.'),
        ('yesno', 'Show Frame Rate', 'cg_drawFPS', 'Frames a second in the corner of the screen.'),
        ('yesno', 'Show Timer', 'cg_drawTimer', 'The time of the level in the corner of the screen.'),
        ('yesno', 'Show Lagometer', 'cg_lagometer', 'A graph of your connection.'),
    ]),
    ('social', 'CHAT & SOCIAL', 'The chat box, bubbles and what stands above the heads.', [
        ('yesno', 'Chat Bubbles', 'cg_chatBubbles', 'What a player says stands above his head.'),
        ('slider', 'Bubble Range', 'cg_bubbleRange', 1200, 300, 3000, 'How far away a bubble is still drawn.'),
        ('yesno', 'Player Statuses', 'cg_playerStatuses', 'Away from keyboard and music above the heads of the others.'),
        ('yesno', 'Show My Music', 'cg_shareMusic', 'Tell the others what you listen to. Everybody on the server reads it.'),
        ('yesno', 'Select Players', 'cg_selectPlayers', 'With the mouse free, a click on a player opens a card with what you can do with him.'),
        ('yesno', 'Selection Outline', 'cg_selectOutline', 'A glowing outline around the player under the mouse.'),
        ('yesno', 'Own Chat Line', 'cg_chatInput', 'Type into the chat box of VisionJK, with history and links.'),
        ('slider', 'Chat Box Width', 'cg_chatBoxWidth', 380, 200, 600, 'How wide the chat box is.'),
        ('yesno', 'Chat Beep', 'cg_chatBeep', 'A sound when somebody says something.'),
        ('yesno', 'Sharp Console Font', 'con_font', 'The console in a font with every character.'),
        ('slider', 'Console Text Size', 'con_scale', 1, 0.5, 3, 'The size of the text of the console.'),
        ('slider', 'Console Opacity', 'con_opacity', 1, 0.2, 1, 'How much of the game shows through the console.'),
    ]),
    ('network', 'NETWORK', 'Updates, downloads and security.', [
        ('yesno', 'Check for Updates', 'cl_updateCheck', 'Ask at every start whether a newer version is out.'),
        ('yesno', 'Voice Master', 'cg_voiceMaster', 'Voice chat, clan radio and statuses on servers that have none of their own.'),
        ('yesno', 'Allow Downloads', 'cl_allowDownload', 'Take files a server offers.'),
        ('yesno', 'Intrusion Log', 'cl_ids', 'Write down what a server sends that no server of the game does, and leave one that keeps at it.'),
        ('yesno', 'Key Login by Agent', 'cg_authAgent', 'The admin login asks your ssh-agent for the key (1Password, OpenSSH).'),
        ('yesno', 'UTF-8 Text', 'cl_utf8', 'Every character in names and chat. Off for a language with a code page of its own.'),
    ]),
]
MORE_ROWS = {
    'sound': [
        ('yesno', 'Spatial Audio', 's_spatial', 'Sound that knows the level: walls, rooms and corners. Counts after a restart of the sound.'),
        ('yesno', 'Room Reflections', 's_spatialReflections', 'The room around you rings.'),
        ('yesno', 'Sound Round Corners', 's_spatialPaths', 'A sound finds its way round corners.'),
        ('yesno', 'Doppler', 's_doppler', 'What flies past changes its pitch.'),
    ],
}


def quoted(s):
    return '"%s"' % s


def setting_row(group, spec, rect, alignx):
    kind, text, cvar = spec[:3]
    desc = spec[-1]
    b = [Entry('name', ['%s_%s' % (group, cvar.lower())]), Entry('group', [group]),
         Entry('type', [{'yesno': 'ITEM_TYPE_YESNO', 'slider': 'ITEM_TYPE_SLIDER', 'multi': 'ITEM_TYPE_MULTI'}[kind]]),
         Entry('text', [quoted(text + ':')])]
    if kind == 'slider':
        b.append(Entry('cvarfloat', [quoted(cvar)] + [mf.fmt(v) for v in spec[3:6]]))
    else:
        b.append(Entry('cvar', [quoted(cvar)]))
    if kind == 'multi':
        words = []
        for label_, value in spec[3]:
            words += [quoted(label_), mf.fmt(value)]
        b.append(Entry('cvarFloatList', [], words))
    b += [Entry('rect', [mf.fmt(v) for v in rect]), Entry('textalign', ['ITEM_ALIGN_RIGHT']), Entry('textalignx', [mf.fmt(alignx)]),
          Entry('textaligny', ['0']), Entry('font', ['4']), Entry('textscale', ['1']), Entry('forecolor', ['0.65', '0.65', '1', '1']),
          Entry('visible', ['0']), Entry('descText', [quoted(desc)]),
          Entry('action', [], ['play', '"sound/interface/button1.wav"', ';'])]
    return Entry('itemDef', [], b)


def setup_before(m):
    """The new tabs and rows of a setup menu, written as the old ones are: the rules of theme.py then treat them
    as their neighbours. Called before those rules."""
    tabs = [e for e in items(m) if (mf.get(e.block, 'group') or [''])[0] == 'setup_submenu' and mf.find(e.block, 'action')]
    sample = next((e for e in items(m) if (mf.get(e.block, 'group') or [''])[0] == 'voicechat' and mf.get(e.block, 'text')), None)
    model = next((e for e in tabs if name_of(e.block) == 'voicechatmenubutton'), None)
    if not tabs or not sample or not model:
        return
    row = rect_of(sample.block)                     # x, y of the first row, width, height of a row
    alignx = mf.num(mf.get(sample.block, 'textalignx')[0])
    top = min(rect_of(e.block)[1] for e in items(m) if (mf.get(e.block, 'group') or [''])[0] == 'voicechat' and rect_of(e.block))
    tab_rects = sorted(rect_of(e.block) for e in tabs)
    step = tab_rects[1][1] - tab_rects[0][1]
    last = max(r[1] for r in tab_rects)
    new = []
    for n, (group, title, desc, rows) in enumerate(TABS):
        # every tab that is there hides the new one
        for e in tabs:
            words = mf.find(e.block, 'action').block
            at = next((i for i, w in enumerate(words) if w.lower() == 'setitemcolor'), len(words))
            words[at:at] = ['hide', group, ';', mf.NL]
        button = Entry('itemDef', [], [Entry(x.key, list(x.args), list(x.block) if x.block is not None else None) for x in model.block])
        b = button.block
        mf.put(b, 'name', group + 'menubutton')
        mf.put(b, 'text', quoted(title))
        mf.put(b, 'descText', quoted(desc))
        r = rect_of(model.block)
        set_rect(b, r[0], last + step * (n + 1), r[2], r[3])
        mf.drop(b, 'mouseenter')
        mf.drop(b, 'mouseexit')
        words = mf.find(b, 'action').block
        for i, w in enumerate(words):
            if w == 'voicechatmenubutton':
                words[i] = group + 'menubutton'
            elif w == 'voicechat' and words[i - 1].lower() == 'show':
                words[i - 1] = 'hide'
        at = next((i for i, w in enumerate(words) if w.lower() == 'setitemcolor'), len(words))
        words[at:at] = ['show', group, ';', mf.NL]
        for other, _, _, _ in TABS:
            if other != group and other not in words:
                words[at:at] = ['hide', other, ';', mf.NL]
        new.append(button)
        for i, spec in enumerate(rows):
            new.append(setting_row(group, spec, (row[0], top + i * row[3], row[2], row[3]), alignx))
    for group, rows in MORE_ROWS.items():
        have = [rect_of(e.block) for e in items(m) if (mf.get(e.block, 'group') or [''])[0] == group and rect_of(e.block)]
        if not have:
            continue
        y = max(r[1] for r in have) + row[3]
        for i, spec in enumerate(rows):
            new.append(setting_row(group, spec, (row[0], y + i * row[3], row[2], row[3]), alignx))
    m.extend(new)


def controls_before(m):
    """The key that frees the mouse, as a row under the one of the clan radio."""
    model = next((e for e in items(m) if (mf.get(e.block, 'cvar') or [''])[0] == '"+voiceclan"'), None)
    if not model:
        return
    group = (mf.get(model.block, 'group') or [''])[0]
    rows = [rect_of(e.block) for e in items(m) if (mf.get(e.block, 'group') or [''])[0] == group and rect_of(e.block)
            and mf.get(e.block, 'cvar')]
    r = rect_of(model.block)
    row = Entry('itemDef', [], [Entry(x.key, list(x.args), list(x.block) if x.block is not None else None) for x in model.block])
    b = row.block
    mf.put(b, 'name', 'vision_cursorkey')
    mf.put(b, 'text', quoted('Free the Mouse (Select Players)'))
    mf.put(b, 'cvar', quoted('+cursor'))
    mf.put(b, 'descText', quoted('hold to point at players with the mouse: a click opens what you can do with one'))
    set_rect(b, r[0], max(x[1] for x in rows) + r[3], r[2], r[3])
    for key in ('mouseenter', 'mouseexit'):
        e = mf.find(b, key)
        if e and isinstance(e.block, list):
            # the bar that lit behind the row of the model is not this row's
            words, out, i = e.block, [], 0
            while i < len(words):
                if words[i].lower() in ('show', 'hide') and i + 1 < len(words) and words[i + 1].lower().startswith('highlight'):
                    i += 2
                else:
                    out.append(words[i])
                    i += 1
            e.block = out
    m.append(row)


def before(m, name, ingame):
    """What a page gets before the rules of theme.py."""
    if name in ('setup.menu', 'ingame_setup.menu'):
        setup_before(m)
    elif name in ('controls.menu', 'ingame_controls.menu'):
        controls_before(m)


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
    full = mf.get(m, 'fullscreen')
    full = bool(full and full[0] != '0')
    if menu == 'main':
        main_page(m)
    elif menu == 'connect':
        # the sky of the menus (backdrop) under the panel the code draws (UI_ConnectPanel), no picture of the game.
        # (The window of the menu was a picture frame as well: left without a picture it draws the engine's
        # stand-in for a missing one over the sky.)
        m[:] = [e for e in m if e.k != 'itemdef']
        mf.drop(m, 'style')
    elif menu == 'ingame':
        ingame_bar(m)
    elif full and not ingame:
        shell(m, menu, HAD_LOGO.get(name, False))


# pages whose content began under the logo of the game, by file: noted before the rules took the logo away
HAD_LOGO = {}
