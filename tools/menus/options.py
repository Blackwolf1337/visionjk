"""The options screens of the new look: Setup and Controls, outside a game and in one (tools/menus/build.py).

One layout for the four of them: the pages of the screen as a row of tabs at its top, the settings of the open
page as a list of rows (its name at the left, its widget at the right) that the wheel moves, beside the list
what the row under the mouse is for, and a bar at the bottom with the way back. The code that draws and moves
all of that is in codemp/ui/ui_shared.c ("page", "tab", "scroll", "row", ITEM_TYPE_DESC, ITEM_TYPE_PAGESCROLL).

The rows of the old menu are taken as they are (their cvar, their list, their script) and put into the pages
by the tables below. A row the old menus did not have is written there in full."""
import menufile as mf
from menufile import Entry
from theme import *         # noqa: F401,F403
import texts
from texts import ref

ROW_H = 22                  # a row of a list, and a heading in it
ROWS = 16                   # rows the window of a list shows
ROW_SCALE = 0.7
CLICK = ['play', '"sound/interface/button1.wav"', ';']
WINDOW = ['0.035', '0.07', '0.12', '0.94']
BAND = ['0.02', '0.04', '0.08', '0.50']         # the row of tabs and the bar at the bottom, as pages.FOOT


class Layout:
    """Where the parts of the screen are, in the units of its menu."""

    def __init__(self, ingame):
        self.ingame = ingame
        if ingame:
            # a window under the bar of the menu of a game
            self.menu = (16, 30, 608, 436)
            self.width = 608
            self.strip_y, self.left = 0, 12
            self.list = (12, 38, 380, ROWS * ROW_H)
            self.pane = (408, 38, 188, ROWS * ROW_H)
            self.bar_y, self.bar_h = 404, 32
        else:
            self.menu = (0, 0, 640, 480)
            self.width = 640
            self.strip_y, self.left = 34, 32
            self.list = (32, 72, 384, ROWS * ROW_H)
            self.pane = (432, 72, 176, ROWS * ROW_H)
            self.bar_y, self.bar_h = 446, 34
        self.right = self.width - self.left


# ---- the pages. An entry of a page:
#   ('head', text)                              a heading
#   ('old', group[, only][, skip])              the rows of that group of the old menu, in their order. only: just
#                                               these, in this order. skip: not these. Both by cvar or name.
#   ('yesno', name, cvar, what for)             a row the old menus did not have
#   ('slider', name, cvar, default, least, most, what for)
#   ('multi', name, cvar, ((name, value), ...), what for)
#   ('edit' | 'secret', name, cvar, letters, what for)
#   ('bind', name, command, what for)
SETUP = [
    ('@MENUS_VIDEO', '@MENUS_CONFIGURE_VIDEO_SETTINGS', [
        ('head', 'Screen'),
        ('old', 'video', ('ui_r_glcustom', 'ui_r_resolution', 'ui_r_displaymode', 'ui_cl_renderer', 'ui_r_ext_multisample',
                          'r_swapinterval')),
        ('multi', 'Frame Limit', 'com_maxfps', (('60', 60), ('90', 90), ('125', 125), ('144', 144), ('165', 165), ('240', 240),
                                                ('None', 0)), 'The most frames a second the game draws.'),
        ('yesno', 'Wide Screen Layout', 'r_2dAspect',
         'Menus and HUD keep their shape on a wide window. Counts after a restart of the video.'),
        ('old', 'video2', ('r_gamma',)),
        ('head', 'Detail'),
        ('old', 'video', ('ui_r_lodbias', 'ui_r_picmip', 'ui_r_texturemode', 'ui_r_detailtextures',
                          'ui_r_ext_compress_textures')),
        ('old', 'video2', ('r_ext_texture_filter_anisotropic', 'r_dynamiclight')),
        ('yesno', 'Dynamic Glow', 'r_DynamicGlow', 'Lightsabers and lights glow.'),
        ('old', 'video2', ('cg_marks', 'cg_shadows', 'cg_rendertotexturefx', 'advancedvideo')),
    ]),
    ('@MENUS_SOUND', '@MENUS_CONFIGURE_SOUND_SETTINGS', [
        ('head', 'Volume'),
        ('old', 'sound', ('s_volume', 's_musicvolume', 's_volumevoice')),
        ('head', '3D Sound'),
        ('old', 'sound', ('ui_s_spatialquality', 's_spatialhrtf', 's_spatialreverb')),
        ('yesno', 'Room Reflections', 's_spatialReflections', 'The room around you rings.'),
        ('yesno', 'Sound Around Corners', 's_spatialPaths', 'A sound finds its way around corners.'),
        ('yesno', 'Doppler Effect', 's_doppler', 'What flies past changes its pitch.'),
        ('head', 'Voice Chat'),
        ('old', 'voicechat'),
    ]),
    ('@MENUS_OPTIONS', '@MENUS_CONFIGURE_GAME_OPTIONS', [
        ('head', 'Game'),
        ('old', 'options', None, ('cl_allowdownload', 'se_language', 's_language', 'langapply')),
        ('head', 'Language'),
        ('old', 'options', ('se_language', 's_language', 'langapply')),
        ('head', 'Bot Mode'),
        ('old', 'botmode'),
    ]),
    ('Interface', 'How menus, HUD and camera look.', [
        ('head', 'Menus and HUD'),
        ('yesno', 'New HUD', 'cg_hud', 'The HUD of VisionJK. Off: the HUD of the game.'),
        ('yesno', 'Classic Menus', 'ui_classic', 'The menus of the game as they were. Counts from the next start of the game.'),
        ('yesno', 'Menu in the Level', 'cg_immersiveMenu', 'In a game an open menu stands in the level next to you.'),
        ('slider', 'Menu Blur', 'r_menuBlur', 1, 0, 4, 'How far the level behind an open menu is out of focus.'),
        ('yesno', 'Show Frame Rate', 'cg_drawFPS', 'Frames a second in the corner of the screen.'),
        ('yesno', 'Show Timer', 'cg_drawTimer', 'The time of the level in the corner of the screen.'),
        ('yesno', 'Show Lagometer', 'cg_lagometer', 'A graph of your connection.'),
        ('head', 'View'),
        ('slider', 'Field of View', 'cg_fov', 80, 60, 130, 'How wide you see.'),
        ('slider', 'Camera Distance', 'cg_thirdPersonRange', 80, 40, 200, 'How far the camera is behind you in third person.'),
        ('slider', 'Crosshair Size', 'cg_crosshairSize', 24, 12, 64, 'The size of the crosshair.'),
    ]),
    ('Chat & Social', 'The chat box, bubbles and what stands above the heads.', [
        ('head', 'Players'),
        ('yesno', 'Chat Bubbles', 'cg_chatBubbles', 'What a player says stands above his head.'),
        ('slider', 'Bubble Range', 'cg_bubbleRange', 1200, 300, 3000, 'How far away a bubble is still drawn.'),
        ('yesno', 'Player Statuses', 'cg_playerStatuses', 'Away from keyboard and music above the heads of the others.'),
        ('yesno', 'Show My Music', 'cg_shareMusic', 'Tell the others what you listen to. Everybody on the server reads it.'),
        ('yesno', 'Select Players', 'cg_selectPlayers',
         'With the mouse free, a click on a player opens a card with what you can do with him.'),
        ('yesno', 'Selection Outline', 'cg_selectOutline', 'A glowing outline around the player under the mouse.'),
        ('head', 'Chat'),
        ('yesno', 'Own Chat Line', 'cg_chatInput', 'Type into the chat box of VisionJK, with history and links.'),
        ('slider', 'Chat Box Width', 'cg_chatBoxWidth', 380, 200, 600, 'How wide the chat box is.'),
        ('yesno', 'Chat Beep', 'cg_chatBeep', 'A sound when somebody says something.'),
        ('head', 'Console'),
        ('yesno', 'Sharp Console Font', 'con_font', 'The console in a font with every character.'),
        ('slider', 'Console Text Size', 'con_scale', 1, 0.5, 3, 'The size of the text of the console.'),
        ('slider', 'Console Opacity', 'con_opacity', 1, 0.2, 1, 'How much of the game shows through the console.'),
        ('head', 'Clan'),
        ('secret', 'Clan Password', 'cp_clanPwd', 32,
         'Clan chat and clan radio reach everybody on the server who has the same one. The server sees it.'),
    ]),
    ('Network', 'Updates, downloads and security.', [
        ('head', 'Updates and Downloads'),
        ('yesno', 'Check for Updates', 'cl_updateCheck', 'Ask at every start whether a newer version is out.'),
        ('old', 'options', ('cl_allowdownload',)),
        ('head', 'Security'),
        ('yesno', 'Intrusion Log', 'cl_ids',
         'Write down what a server sends that no server of the game does, and leave one that keeps at it.'),
        ('yesno', 'Key Login by Agent', 'cg_authAgent', 'The admin login asks your ssh-agent for the key (1Password, OpenSSH).'),
        ('edit', 'Key File', 'cg_authKeyFile', 120, 'The file with the key for the admin login, where no agent has it.'),
        ('head', 'Text'),
        ('yesno', 'UTF-8 Text', 'cl_utf8', 'Every character in names and chat. Off for a language with a code page of its own.'),
    ]),
    ('@MENUS_MODS', 'Game folders with other mods.', [('mods',)]),
]

CONTROLS = [
    ('@MENUS_MOVEMENT', 'The keys you move with.', [('old', 'movecontrols')]),
    ('@MENUS_INTERACTION', 'Attack, use and look.', [('old', 'attackcontrols')]),
    ('@MENUS_WEAPONS', 'The keys of the weapons.', [('old', 'weaponcontrols')]),
    ('Force Powers', 'The keys of the force powers.', [('old', 'forcecontrols'), ('old', 'forcecontrols2')]),
    ('@MENUS_MOUSE_JOYSTICK', 'Mouse and joystick.', [('old', 'joycontrols')]),
    ('@MENUS_OTHER', 'Chat, scores and gestures.', [('old', 'othercontrols')]),
    ('Extras', 'Keys for what VisionJK and JA+ add to the game.', [
        ('head', 'JA+'),
        ('old', 'japluscontrols', ('+button12', 'jetpack', 'engage_fullforceduel', 'ui_openmenu modsay')),
        ('head', 'VisionJK'),
        ('old', 'japluscontrols', ('+voice', '+voiceclan', 'cg_botmode !')),
        ('bind', 'Free the Mouse (Select Players)', '+cursor',
         'Hold to point at players with the mouse: a click opens what you can do with one.'),
    ]),
]

SCREENS = {'setup.menu': SETUP, 'ingame_setup.menu': SETUP, 'controls.menu': CONTROLS, 'ingame_controls.menu': CONTROLS}

# what a row takes over from the item of the old menu: what it is and does, not how it looked
KEEP = ('name', 'group', 'type', 'text', 'cvar', 'cvarfloat', 'cvarfloatlist', 'cvarstrlist', 'feeder', 'action', 'desctext',
        'cvartest', 'showcvar', 'hidecvar', 'enablecvar', 'disablecvar', 'maxchars', 'maxpaintchars', 'invertyesno')
TYPES = {'yesno': 'ITEM_TYPE_YESNO', 'slider': 'ITEM_TYPE_SLIDER', 'multi': 'ITEM_TYPE_MULTI', 'edit': 'ITEM_TYPE_EDITFIELD',
         'secret': 'ITEM_TYPE_EDITFIELD', 'bind': 'ITEM_TYPE_BIND'}
SETTINGS = ('item_type_yesno', 'item_type_slider', 'item_type_multi', 'item_type_editfield', 'item_type_numericfield',
            'item_type_bind', 'item_type_button', '1', '4', '9', '10', '11', '12', '13')

# script commands that showed and marked the tabs of the old menus, with the number of their arguments
GONE_COMMANDS = {'show': 1, 'hide': 1, 'setitemcolor': 6, 'setfocus': 1}
KEPT_ITEMS = ('applychanges',)       # the button a changed video setting brings up


def items(m):
    return [e for e in m if e.k == 'itemdef']


def name_of(b):
    n = mf.get(b, 'name')
    return mf.unquote(n[0]).lower() if n else ''


def group_of(b):
    return (mf.get(b, 'group') or [''])[0].lower()


def cvar_of(b):
    c = mf.get(b, 'cvar') or mf.get(b, 'cvarfloat')
    return mf.unquote(c[0]).lower() if c else ''


def quoted(s):
    return '"%s"' % s


def word(text):
    """A text of the tables above as a menu file names it: one of the game (@MENUS_...), or one of texts.py."""
    return text if text.startswith('@') else ref(text)


def item(name, rect, *props):
    b = [Entry('name', [name]), Entry('rect', [mf.fmt(v) for v in rect]), Entry('visible', ['1'])]
    for key, *args in props:
        if key in ('action', 'cvarFloatList', 'cvarStrList'):
            b.append(Entry(key, [], list(args[0])))
        else:
            b.append(Entry(key, [str(a) for a in args]))
    return Entry('itemDef', [], b)


def fill(name, rect, color, *more):
    return item(name, rect, ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *color), ('decoration',), *more)


def panel_item(name, rect, fill_=SOFT, edge=SOFT_EDGE, radius=4, *more):
    return item(name, rect, ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *fill_), ('bordercolor', *edge), ('radius', radius),
                ('decoration',), *more)


def text_item(name, rect, text, scale, color, *more):
    return item(name, rect, ('text', text), ('font', FONT), ('textscale', mf.fmt(scale)), ('forecolor', *color), *more)


def centered(rect, scale):
    """The properties that put a text on the middle of a rectangle."""
    return (('textalign', 'ITEM_ALIGN_CENTER'), ('textalignx', mf.fmt(rect[2] / 2)),
            ('textaligny', mf.fmt((rect[3] - 18.0 * scale) / 2)))


def clean(words):
    """A script without the commands that showed, hid and marked the tabs of the old menu."""
    out, i = [], 0
    while i < len(words):
        w = words[i]
        k = w.lower().rstrip(';')
        n = GONE_COMMANDS.get(k)
        if w == mf.NL or n is None or w.endswith(';'):
            out.append(w)
            i += 1
            continue
        args = words[i + 1:i + 1 + n]
        if k in ('show', 'hide') and args and args[0].lower().rstrip(';') in KEPT_ITEMS:
            out.extend(words[i:i + 1 + n])
            i += 1 + n
            continue
        closed = bool(args) and args[-1].endswith(';')
        i += 1 + n
        if not closed and i < len(words) and words[i] == ';':
            i += 1
    while out and out[0] in (mf.NL, ';'):
        out.pop(0)
    # (the sound of a click was named without quotes in many rows of the game: the menu reader takes such a path
    # apart at its slashes, and the row made no sound and three lines in the console)
    for i, w in enumerate(out[:-1]):
        if w.lower() == 'play' and '/' in out[i + 1] and not out[i + 1].startswith('"'):
            end = ';' if out[i + 1].endswith(';') else ''
            out[i + 1] = '"%s"%s' % (out[i + 1].rstrip(';'), end)
    return out


def translated(block):
    """The texts of an item of the old menu that this tree wrote in English, as references into texts.py."""
    for key in ('text', 'desctext'):
        e = mf.find(block, key)
        if e and e.args:
            e.args[0] = texts.old(e.args[0])
    for key in ('cvarfloatlist', 'cvarstrlist'):
        e = mf.find(block, key)
        if e and isinstance(e.block, list):
            # names and values take turns
            n = 0
            for i, w in enumerate(e.block):
                if w in (mf.NL, ',', ';'):
                    continue
                if n % 2 == 0:
                    e.block[i] = texts.old(w)
                n += 1


def as_row(block, page, y, L):
    """The properties that make a setting a row of the list of a page."""
    x, _, w, _ = L.list
    for key in ('rect', 'page', 'scroll', 'row', 'font', 'textscale', 'forecolor', 'visible'):
        mf.drop(block, key)
    secret = name_of(block).startswith('vision_secret')
    block += [Entry('rect', [mf.fmt(v) for v in (x, y, w, ROW_H)]), Entry('page', [str(page)]), Entry('scroll', ['1']),
              Entry('row', ['2' if secret else '1']), Entry('font', [FONT]), Entry('textscale', [mf.fmt(ROW_SCALE)]),
              Entry('forecolor', list(TEXT)), Entry('visible', ['1'])]


def old_rows(m, group, only=None, skip=()):
    """The settings of a group of the old menu, as new items."""
    found = []
    for e in items(m):
        b = e.block
        kind = (mf.get(b, 'type') or [''])[0].lower()
        if group_of(b) != group or kind not in SETTINGS or mf.get(b, 'text') is None:
            continue
        ident = (cvar_of(b), name_of(b))
        if only is not None and not set(ident) & set(only):
            continue
        if set(ident) & set(skip):
            continue
        new = [Entry(x.key, list(x.args), list(x.block) if x.block is not None else None) for x in b if x.k in KEEP]
        act = mf.find(new, 'action')
        if act and isinstance(act.block, list):
            act.block = clean(act.block)
        translated(new)
        found.append((ident, new))
    if only is not None:
        found.sort(key=lambda f: min(only.index(i) for i in f[0] if i in only))
    return [new for _, new in found]


def new_row(spec):
    """A setting the old menus did not have."""
    kind, text, cvar = spec[:3]
    b = [Entry('name', ['vision_%s_%s' % (kind, cvar.lower().replace(' ', '_').replace('+', ''))]), Entry('type', [TYPES[kind]]),
         Entry('text', [ref(text)]), Entry('descText', [ref(spec[-1])])]
    if kind == 'slider':
        b.append(Entry('cvarfloat', [quoted(cvar)] + [mf.fmt(v) for v in spec[3:6]]))
    else:
        b.append(Entry('cvar', [quoted(cvar)]))
    if kind == 'multi':
        words = []
        for name, value in spec[3]:
            words += [ref(name) if name in texts.TR else quoted(name), mf.fmt(value)]
        b.append(Entry('cvarFloatList', [], words))
    elif kind in ('edit', 'secret'):
        b += [Entry('maxChars', [str(spec[3])]), Entry('maxPaintChars', ['24'])]
    b.append(Entry('action', [], list(CLICK)))
    return b


def page_items(m, page, content, L):
    """The items of one page: its rows one under the other."""
    out = []
    x, y, w, h = L.list
    for entry in content:
        kind = entry[0]
        if kind == 'head':
            out.append(text_item('vision_head', (x + 2, y, w - 4, ROW_H), word(entry[1]), 0.54, DIM, ('textaligny', 9), ('caps', 1),
                                 ('decoration',), ('page', page), ('scroll', 1)))
            out.append(fill('vision_headline', (x, y + ROW_H - 1, w, 0.67), ACCENT[:3] + ['0.32'], ('page', page), ('scroll', 1)))
            y += ROW_H
        elif kind == 'old':
            for b in old_rows(m, entry[1], entry[2] if len(entry) > 2 else None, entry[3] if len(entry) > 3 else ()):
                as_row(b, page, y, L)
                out.append(Entry('itemDef', [], b))
                y += ROW_H
        elif kind == 'mods':
            # a list of folders and a button: no rows
            lists = [e for e in items(m) if group_of(e.block) == 'mods' and (mf.get(e.block, 'type') or [''])[0] == 'ITEM_TYPE_LISTBOX']
            load = [e for e in items(m) if name_of(e.block) == 'loadmod']
            if not lists or not load:
                return []
            b = lists[0].block
            mf.put(b, 'rect', mf.fmt(x), mf.fmt(y), mf.fmt(w), mf.fmt(h - 34))
            mf.put(b, 'visible', 1)
            mf.put(b, 'page', page)
            out.append(lists[0])
            b = load[0].block
            rect = (x, y + h - 24, 130, 22)
            mf.put(b, 'rect', *[mf.fmt(v) for v in rect])
            for key, *args in (('visible', 1), ('page', page), ('font', FONT), ('textscale', '0.62'), ('caps', 1), ('forecolor', *BRIGHT),
                               ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *(ACCENT[:3] + ['0.20'])),
                               ('bordercolor', *(ACCENT[:3] + ['0.75'])), ('radius', 3)) + centered(rect, 0.62):
                mf.put(b, key, *args)
            act = mf.find(b, 'action')
            act.block = clean(act.block)
            out.append(load[0])
        else:
            b = new_row(entry)
            as_row(b, page, y, L)
            out.append(Entry('itemDef', [], b))
            y += ROW_H
    return out


def bar_button(name, x, w, text, desc, action, L, *more):
    rect = (x, L.bar_y, w, L.bar_h)
    return text_item(name, rect, text, 0.6, TEXT, ('type', 'ITEM_TYPE_BUTTON'), ('style', 'WINDOW_STYLE_EMPTY'), ('caps', 1),
                     ('descText', desc), ('action', action), *centered(rect, 0.6), *more)


def defaults_page(m, page, L):
    """What the old menu asked before it set everything back, on a page of its own: the button at the bottom opens it."""
    yes = next((e for e in items(m) if name_of(e.block) == 'default_yes'), None)
    if not yes:
        return []
    x, y, w, _ = L.list
    act = clean(mf.find(yes.block, 'action').block)
    rect_yes, rect_no = (x + 150, y + 150, 110, 24), (x + 20, y + 150, 110, 24)
    pill = (('type', 'ITEM_TYPE_BUTTON'), ('style', 'WINDOW_STYLE_PANEL'), ('radius', 3), ('caps', 1), ('page', page))
    return [
        text_item('vision_warning', (x + 20, y + 24, w - 40, 24), '@MENUS_WARNING', 1.1, WARM, ('decoration',), ('page', page)),
        text_item('vision_warning1', (x + 20, y + 62, w - 40, 16), '@MENUS_THIS_WILL_SET_ALL_GAME', 0.7, TEXT, ('decoration',),
                  ('page', page)),
        text_item('vision_warning2', (x + 20, y + 80, w - 40, 16), '@MENUS_TO_THEIR_FACTORY_SETTINGS', 0.7, TEXT, ('decoration',),
                  ('page', page)),
        text_item('vision_warning3', (x + 20, y + 110, w - 40, 16), '@MENUS_VID_RESTART3', 0.7, DIM, ('decoration',), ('page', page)),
        text_item('default_no', rect_no, '@MENUS_NO', 0.64, TEXT, *pill, ('backcolor', '1', '1', '1', '0.05'),
                  ('bordercolor', *SOFT_EDGE), ('descText', '@MENUS_DO_NOT_RESET_DEFAULT'), ('tab', 1), ('action', CLICK),
                  *centered(rect_no, 0.64)),
        text_item('default_yes', rect_yes, '@MENUS_YES', 0.64, BRIGHT, *pill, ('backcolor', *(ACCENT[:3] + ['0.20'])),
                  ('bordercolor', *(ACCENT[:3] + ['0.75'])), ('descText', '@MENUS_USE_DEFAULT_SETTINGS'), ('action', act),
                  *centered(rect_yes, 0.64)),
    ]


def build(m, name, ingame, nav):
    """Makes the menu `m` (after the rules of theme.py) the options screen of the table that belongs to its file.
    nav: what the caller has for the top of a page outside a game (the bar with the sections)."""
    L = Layout(ingame)
    screen = SCREENS[name]
    setup = screen is SETUP
    old = list(m)
    esc = mf.find(m, 'onesc')
    back = clean(list(esc.block)) if esc and isinstance(esc.block, list) else list(CLICK)

    pages = []
    for title, about, content in screen:
        made = page_items(old, len(pages) + 1, content, L)
        if made:
            pages.append((title, about, made))
    new = list(nav)

    # the row of tabs
    new.append(fill('vision_strip', (0, L.strip_y, L.width, 26), BAND, *((('wide', 1),) if not ingame else ())))
    new.append(fill('vision_strip_line', (0, L.strip_y + 26, L.width, 0.67), ACCENT[:3] + ['0.18'],
                    *((('wide', 1),) if not ingame else ())))
    # (as wide as their words are in the language of the player: the code moves them together, Menu_TabsLayout.
    # The word of the first one begins where the names of the rows do.)
    tab_w = min(92.0, (L.right - L.left) / len(pages))
    for n, (title, about, made) in enumerate(pages):
        rect = (L.left - 3 + n * tab_w, L.strip_y, tab_w, 26)
        act = list(CLICK)
        if title == '@MENUS_MODS':
            act += ['uiScript', 'loadMods', ';']
        new.append(text_item('vision_tab%d' % (n + 1), rect, word(title), 0.58, DIM, ('type', 'ITEM_TYPE_BUTTON'), ('caps', 1),
                             ('style', 'WINDOW_STYLE_EMPTY'), ('tab', n + 1), ('descText', word(about)), ('action', act),
                             *centered(rect, 0.58)))

    # the lists, the bar beside them, and what the row under the mouse is for
    for _, _, made in pages:
        new.extend(made)
    x, y, w, h = L.list
    new.append(item('vision_scroll', (x + w + 3, y, 6, h), ('type', 'ITEM_TYPE_PAGESCROLL')))
    px, py, pw, ph = L.pane
    new.append(panel_item('vision_pane', L.pane, SOFT, SOFT_EDGE, 4))
    new.append(text_item('vision_about', (px + 12, py + 14, pw - 24, ph - 28), '""', 0.6, BRIGHT, ('type', 'ITEM_TYPE_DESC'),
                         ('decoration',)))

    # the bar at the bottom: back at the left, and what else the screen has to offer
    new.append(fill('vision_foot', (0, L.bar_y, L.width, L.bar_h), BAND, *((('wide', 1),) if not ingame else ())))
    new.append(fill('vision_foot_line', (0, L.bar_y, L.width, 0.67), ACCENT[:3] + ['0.18'], *((('wide', 1),) if not ingame else ())))
    # (as wide as their words and one after the other: "flow", Menu_TabsLayout in ui_shared.c)
    bx = L.left - 3
    new.append(bar_button('vision_back', bx, 72, '@MENUS_BACK', '@MENUS_BACK', back, L, ('flow', 1)))
    made = defaults_page(old, len(pages) + 1, L) if setup else []
    if setup:
        if made:
            new.append(bar_button('vision_defaults', bx + 72, 104, '@MENUS_DEFAULTS', '@MENUS_RESTORE_DEFAULT_SETTINGS', CLICK, L,
                                  ('flow', 1), ('tab', len(pages) + 1)))
            new.extend(made)
        # a changed video setting brings up the button that restarts the video with it
        apply = next((e for e in items(old) if name_of(e.block) == 'applychanges'), None)
        if apply:
            rect = (L.right - 128, L.bar_y + 6, 128, L.bar_h - 12)
            # (it does what the "yes" of the old menu did after its question)
            yes = next((e for e in items(old) if name_of(e.block) == 'vidrestart_yes'), None)
            act = clean(list(mf.find(yes.block, 'action').block)) if yes else CLICK + ['uiScript', 'updatevideosetup', ';']
            new.append(text_item('applyChanges', rect, '@MENUS_APPLY_CHANGES', 0.6, BRIGHT, ('type', 'ITEM_TYPE_BUTTON'),
                                 ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *(ACCENT[:3] + ['0.22'])),
                                 ('bordercolor', *(ACCENT[:3] + ['0.8'])), ('radius', 3), ('caps', 1),
                                 ('descText', ref('Restart the video with these settings.')),
                                 ('action', act), *centered(rect, 0.6)))
            mf.put(new[-1].block, 'visible', 0)
    else:
        rect = (L.right - 300, L.bar_y, 300, L.bar_h)
        new.append(text_item('vision_hint', rect, ref('Enter or click: change the key. Backspace: clear it.'), 0.5, DIM,
                             ('decoration',), ('textalign', 'ITEM_ALIGN_RIGHT'), ('textalignx', 300),
                             ('textaligny', mf.fmt((L.bar_h - 9.0) / 2))))

    m[:] = [e for e in m if e.k != 'itemdef'] + new
    for key in ('onopen', 'onclose', 'onesc'):
        e = mf.find(m, key)
        if e and isinstance(e.block, list):
            e.block = clean(e.block)
    mf.put(m, 'scrollRect', *[mf.fmt(v) for v in L.list])
    mf.put(m, 'scrollStep', ROW_H)
    if ingame:
        mf.put(m, 'rect', *[mf.fmt(v) for v in L.menu])
        mf.put(m, 'style', 'WINDOW_STYLE_PANEL')
        mf.put(m, 'backcolor', *WINDOW)
        mf.put(m, 'bordercolor', *PANEL_EDGE)
        mf.put(m, 'radius', 6)
        mf.put(m, 'shadow', 14)
        mf.drop(m, 'border')
        mf.drop(m, 'bordersize')
        mf.drop(m, 'background')
