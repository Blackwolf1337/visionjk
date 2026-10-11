"""The rules every menu file goes through on its way to the new look (tools/menus/build.py): what goes, what
becomes a panel, which font and which colour a text gets. What an item does is not touched."""
import menufile as mf

# the colours of the theme, as a menu file has them
TEXT = ['0.86', '0.92', '0.97', '1']            # what can be clicked, and what is said
BRIGHT = ['1', '1', '1', '1']
DIM = ['0.60', '0.69', '0.78', '1']             # labels and what is less important
OFF = ['0.36', '0.42', '0.50', '1']             # what can not be used right now
ACCENT = ['0.43', '0.83', '1', '1']             # what is chosen, switched on or in focus
WARM = ['1', '0.74', '0.30', '1']               # what asks for attention
RED = ['1', '0.45', '0.42', '1']                # the red team, and what went wrong
BLUE = ['0.45', '0.66', '1', '1']               # the blue team
PANEL = ['0.035', '0.07', '0.12', '0.80']       # a panel on the sky or on the level
PANEL_EDGE = ['0.43', '0.83', '1', '0.28']
SOFT = ['0.035', '0.07', '0.12', '0.55']        # a panel on a panel
SOFT_EDGE = ['0.43', '0.83', '1', '0.16']
INSET = ['0', '0', '0', '0.30']                 # a list, a field
HEAD = ['0.43', '0.83', '1', '0.14']            # the bar of a heading
NONE = ['0', '0', '0', '0']

FONT = '5'                                      # SKIN_FONT_SANS

# pictures of the old frame: an item that is only that goes, an item with a text keeps its text
GONE = {
    'gfx/menus/main_background', 'gfx/menus/menu_side_text_japlus', 'gfx/menus/menu_side_text_right_japlus',
    'gfx/menus/menu_side_text', 'gfx/menus/menu_side_text_right', 'gfx/menus/main_centerblue',
    'gfx/menus/menu_boxes_left', 'gfx/menus/menu_boxes_right', 'gfx/menus/jediacademy_japlus',
    'gfx/menus/jediacademy', 'gfx/menus/menu_top_mp', 'gfx/menus/main_ring', 'gfx/menus/main_centerwindow',
    'gfx/menus/main_leftwindow', 'gfx/menus/main_rightwindow', 'gfx/menus/videologo', 'gfx/menus/menu_buttonback',
    'gfx/menus/menu_blendbox', 'gfx/menus/menu_blendbox2', 'gfx/menus/menu_blendbox3',
    'gfx/menus/menu_blendbox_extended', 'gfx/menus/sabermenu_back', 'gfx/menus/charmenu', 'gfx/menus/charmenu_bottom',
    'gfx/menus/menu_blendboxr', 'menu/art/unknownmap_mp',
}
# boxes of the old frame: a panel in their place
BOXES = {
    'gfx/menus/menu_box_ingame', 'gfx/menus/menu_box1', 'gfx/menus/menu_boxred', 'gfx/menus/sabermenu_box',
    'gfx/menus/sabermenu_box_top', 'gfx/menus/sabermenu_box_middle', 'gfx/menus/sabermenu_box_bottom',
    'box',
}

# what a text scale is multiplied with when its font becomes the new one, by the font it had: the new font is
# wider than the narrow ones of the game and is 18 units at a scale of 1 (0, no font named: the medium one)
SCALE = {0: 0.86, 1: 0.80, 2: 0.86, 3: 0.74, 4: 0.68}
# and how far its text moves down at a scale of 1: the letters of the game stand lower in their line
DROP = {0: 1.5, 1: 1.5, 2: 1.5, 3: 2.5, 4: 1.0}

SCRIPTS = ('action', 'onopen', 'onclose', 'onesc', 'onaccept', 'mouseenter', 'mouseexit', 'mouseentertext',
           'mouseexittext', 'onfocus', 'leavefocus', 'doubleclick')


def rgba(args):
    """Four numbers of a colour, or None."""
    if not args or len(args) < 4:
        return None
    v = [mf.num(a) for a in args[:4]]
    return None if None in v else v


def kind(c):
    """What a colour of the old menus stands for."""
    r, g, b, a = c
    if r >= 0.75 and 0.4 <= g <= 0.8 and b <= 0.25 and r - g >= 0.2:
        return 'orange'             # what can be clicked
    if r >= 0.95 and g >= 0.95 and b >= 0.95:
        return 'white'              # what is chosen
    if b >= 0.8 and r <= 0.8 and g <= 0.9 and r >= 0.4:
        return 'blue'               # labels and titles
    if r >= 0.9 and g >= 0.9 and b <= 0.3:
        return 'yellow'
    if 0.7 <= r <= 0.9 and 0.55 <= g <= 0.75 and b <= 0.35:
        return 'gold'               # headings of columns, notices
    if r >= 0.9 and g <= 0.35 and b <= 0.35:
        return 'red'
    if b >= 0.9 and r <= 0.35 and g <= 0.35:
        return 'teamblue'
    if abs(r - g) <= 0.06 and abs(g - b) <= 0.06:
        if 0.6 <= r < 0.95:
            return 'grey'           # a choice that is not the chosen one
        if 0.3 <= r < 0.6:
            return 'dark'           # a choice that can not be made
    return None


def recolor(c, chosen=ACCENT):
    """The colour of the theme for a colour of the old menus, with its alpha kept."""
    k = kind(c)
    new = {'orange': TEXT, 'white': chosen, 'blue': DIM, 'yellow': WARM, 'gold': WARM, 'red': RED, 'teamblue': BLUE,
           'grey': TEXT, 'dark': OFF}.get(k)
    if not new:
        return None
    return new[:3] + [mf.fmt(c[3])]


def script_colors(words):
    """setitemcolor <item> forecolor r g b a inside a script: the colour of the theme in its place. These are
    the scripts that mark the section or tab that is open."""
    i = 0
    while i < len(words) - 6:
        if words[i].lower() == 'setitemcolor' and words[i + 2].lower() == 'forecolor':
            # (the last number often has the ";" of the command on it)
            end = ';' if words[i + 6].endswith(';') else ''
            c = rgba(words[i + 3:i + 6] + [words[i + 6].rstrip(';')])
            new = recolor(c) if c else None
            if new:
                words[i + 3:i + 7] = new[:3] + [new[3] + end]
            i += 7
        else:
            i += 1


def panel(b, fill=SOFT, edge=SOFT_EDGE, radius=5, shadow=0):
    """Makes the window of an item or a menu a panel of the new look."""
    mf.put(b, 'style', 'WINDOW_STYLE_PANEL')
    mf.put(b, 'backcolor', *fill)
    mf.put(b, 'bordercolor', *edge)
    mf.put(b, 'radius', radius)
    mf.drop(b, 'border')
    mf.drop(b, 'bordersize')
    mf.drop(b, 'background')
    if shadow:
        mf.put(b, 'shadow', shadow)


def style_of(b):
    s = mf.get(b, 'style')
    return {'1': 'filled', 'window_style_filled': 'filled', '3': 'shader', 'window_style_shader': 'shader'}.get(
        s[0].lower() if s else '', '')


def kind_of(b):
    t = mf.get(b, 'type')
    t = t[0].lower() if t else ''
    if not t and mf.get(b, 'ownerdraw') is not None:
        return 'owner'
    return {'1': 'button', 'item_type_button': 'button', '6': 'list', 'item_type_listbox': 'list',
            '14': 'scroll', 'item_type_textscroll': 'scroll', '7': 'model', 'item_type_model': 'model',
            '8': 'owner', 'item_type_ownerdraw': 'owner'}.get(t, t.replace('item_type_', ''))


def retext(b):
    """The new font for the text of an item, at the size and in the place the old one had."""
    font = mf.get(b, 'font')
    old = int(mf.num(font[0])) if font and mf.num(font[0]) is not None else 0
    if old >= 5:
        return
    scale = mf.get(b, 'textscale')
    s = mf.num(scale[0]) if scale else None
    if s is None:
        s = 0.55                     # what the game gives an item that names none
    mf.put(b, 'font', FONT)
    mf.put(b, 'textscale', mf.fmt(s * SCALE.get(old, 0.86)))
    if old == 3:
        mf.put(b, 'caps', 1)        # the large font of the game only had capital letters
    y = mf.get(b, 'textaligny')
    if y is not None or mf.get(b, 'text') is not None:
        mf.put(b, 'textaligny', mf.fmt((mf.num(y[0]) if y else 0) + DROP.get(old, 1.5) * s))
    # a shadow under every letter was for text on pictures
    st = mf.get(b, 'textstyle')
    if st and st[0] in ('3', '6', 'ITEM_TEXTSTYLE_SHADOWED', 'ITEM_TEXTSTYLE_SHADOWEDMORE'):
        mf.put(b, 'textstyle', '0')


def restyle_item(b):
    """One itemDef. Returns False when the item goes."""
    bg = mf.get(b, 'background')
    bg = mf.unquote(bg[0]).lower() if bg else ''
    has_text = mf.get(b, 'text') is not None or mf.get(b, 'cvar') is not None
    k = kind_of(b)
    style = style_of(b)

    if bg in GONE:
        if not has_text and k not in ('list', 'owner', 'model'):
            return False
        mf.drop(b, 'background')
        mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
        style = ''
    elif bg in BOXES:
        panel(b)
        style = ''

    back = rgba(mf.get(b, 'backcolor'))
    border = mf.get(b, 'border')
    border = bool(border and border[0] not in ('0', 'WINDOW_BORDER_NONE'))
    if k == 'list' or k == 'scroll':
        if style == 'filled' or border:
            panel(b, INSET, SOFT_EDGE, 4)
        mf.put(b, 'outlinecolor', *(ACCENT[:3] + ['0.22']))
    elif style == 'filled':
        r = mf.get(b, 'rect')
        w, h = (mf.num(r[2]) or 0, mf.num(r[3]) or 0) if r and len(r) >= 4 else (0, 0)
        if back and back[3] >= 0.95 and not has_text:
            panel(b, HEAD, NONE, 3)                 # a bar that was solid: the head of a box
        elif back and back[3] > 0.0 and not has_text and h <= 24 and w >= 100:
            # the stripes behind the rows of a list: every other one a little lighter
            mf.drop(b, 'border')
            if back[0] + back[1] + back[2] >= 0.4:
                mf.put(b, 'backcolor', '1', '1', '1', '0.03')
            else:
                mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
        elif back and back[3] > 0.0:
            panel(b, HEAD if has_text else SOFT, NONE if has_text else SOFT_EDGE, 3 if has_text else 5)
        elif border and h >= 100 and w <= 320 and not has_text:
            # the frame around one column of a list: the list has its own
            mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
            mf.drop(b, 'border')
        elif border:
            panel(b, NONE, SOFT_EDGE, 5)            # a frame around something
        else:
            mf.put(b, 'style', 'WINDOW_STYLE_EMPTY')
    elif border and style != 'shader':
        panel(b, NONE, SOFT_EDGE, 5)

    fore = rgba(mf.get(b, 'forecolor'))
    if fore and (has_text or k in ('list', 'scroll', 'owner')):
        # the white of an item that can be clicked marked the section that is open. (A row of settings that was
        # white is a row like the others.)
        if k in ('bind', 'yesno', 'multi', 'slider', 'editfield', 'numericfield', '4', '9', '10', '11', '12', '13'):
            new = recolor(fore, DIM)
        else:
            new = recolor(fore, ACCENT if k == 'button' or mf.find(b, 'action') else TEXT)
        if new:
            mf.put(b, 'forecolor', *new)
    if has_text or k in ('list', 'scroll', 'owner'):
        retext(b)
    for e in b:
        if e.k in SCRIPTS and isinstance(e.block, list):
            script_colors(e.block)
    return True


def restyle(m, name, ingame):
    """One menuDef."""
    full = mf.truth(mf.get(m, 'fullscreen'))
    mf.put(m, 'skin', 1)
    mf.put(m, 'focusColor', *ACCENT)
    mf.put(m, 'disableColor', *OFF)
    if mf.get(m, 'desccolor') is not None:
        mf.put(m, 'descColor', *DIM)
    if full:
        mf.put(m, 'backdrop', 1)
    else:
        mf.put(m, 'openSlide', 8)
        # a window with a frame of its own: a panel that stands on the level
        style, border = mf.get(m, 'style'), mf.get(m, 'border')
        if (style and style[0] in ('1', 'WINDOW_STYLE_FILLED')) or (border and border[0] != '0'):
            panel(m, PANEL, PANEL_EDGE, 8, 14)
            mf.put(m, 'sheen', '0.08')
    for e in m:
        if e.k in SCRIPTS and isinstance(e.block, list):
            script_colors(e.block)
    m[:] = [e for e in m if e.k != 'itemdef' or restyle_item(e.block)]


def assets(a):
    """assetGlobalDef: the fonts and sounds stay (other menus and the code name them), the colours follow."""
    mf.put(a, 'focuscolor', *ACCENT)
