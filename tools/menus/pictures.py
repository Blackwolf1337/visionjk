#!/usr/bin/env python3
"""Takes the before and after pictures of the settings of Setup (PICTURES of options.py) in the game.

usage: tools/menus/pictures.py <game folder> [<picture> ...]
       <game folder>: one with visionjk.x86_64 and base/assets*.pk3 (cmake --install, then the pk3 of the game)

The game runs without a window and without sound (tools/test/headless.sh), loads mp/duel1, joins and stands at
one place with the HUD off, and looks the way the view of the picture says (VIEWS). For each picture it sets the
cvars for a, restarts the video where the table says so, takes a screenshot, and the same for b. Then it puts every cvar back to what it was before the
picture. The part of the frame the table names is cut out, made PICTURE_SIZE large (a little squeezed, the pane
draws it at 16:9 again) and written as
assets/japlus_client/<PICTURE_DIR>/<picture>_a.jpg and _b.jpg. The screenshots of the run stay in a folder of
its own, the script says where.

It needs Pillow and gamescope. The pictures are of the Vulkan renderer at 1280 x 720."""
import os
import subprocess
import sys
import tempfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import options                  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.normpath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(TREE, 'assets', 'japlus_client', options.PICTURE_DIR)
HEADLESS = os.path.join(TREE, 'tools', 'test', 'headless.sh')

PICTURE_SIZE = (512, 256)      # (the engine takes no picture whose sides are no powers of two: drawn at 16:9 again)
MAP = 'mp/duel1'
# at a spawn point of mp/duel1, facing stairs whose steps glow. (A player with a lightsaber is always in third
# person: the camera is close behind him, and he is not drawn.)
VIEWS = {
    'stairs': ['set cg_thirdPersonAlpha 0', 'set cg_thirdPersonAngle 0', 'set cg_thirdPersonRange 30',
               'set cg_thirdPersonPitchOffset 0', 'setviewpos 1306 442 40 90'],
}
# what every picture starts from, so that only its own cvar makes the difference
BASE = (('r_picmip', 0), ('r_gamma', 1.2), ('r_DynamicGlow', 1), ('tay_bloom', 0), ('tay_bloomIntensity', 0.6),
        ('tay_bloomThreshold', 0.5))
SETTLE = 150            # frames after the place is taken again, before a screenshot
RESTART = 200           # frames after a restart of the video


def script(pictures):
    """The commands of the run, as lines of a cfg."""
    # (the camera without its lag, so that a and b are taken from the same place)
    lines = ['wait 100', 'devmap %s' % MAP, 'wait 250', 'team free', 'wait 150', 'set cg_thirdPerson 1',
             'set cg_thirdPersonCameraDamp 1', 'set cg_thirdPersonTargetDamp 1', 'set cg_smoothCamera 0']
    lines += ['set %s "%s"' % (cvar, value) for cvar, value in BASE] + ['vid_restart', 'wait %d' % RESTART]
    for name, _, _, cvars, restart, (view, _) in pictures:
        for half, index in (('a', 1), ('b', 2)):
            lines += ['set %s "%s"' % (c[0], c[index]) for c in cvars]
            if restart:
                lines += ['vid_restart', 'wait %d' % RESTART]
            lines += VIEWS[view] + ['wait %d' % SETTLE, 'screenshot %s_%s' % (name, half)]
        # back to the start for the next one
        base = dict(BASE)
        lines += ['set %s "%s"' % (c[0], base[c[0]]) for c in cvars if c[0] in base]
        if restart:
            lines += ['vid_restart', 'wait %d' % RESTART]
    return lines + ['wait 20', 'quit']


def cut(path, crop, out):
    """The part of a screenshot the table names, at PICTURE_SIZE."""
    im = Image.open(path).convert('RGB')
    w, h = im.size
    left, top, width = crop
    box = (int(left * w), int(top * h), int((left + width) * w), int(top * h + width * w * 9 / 16))
    im.crop(box).resize(PICTURE_SIZE, Image.LANCZOS).save(out, quality=85, optimize=True)


def main():
    if len(sys.argv) < 2 or not os.path.exists(os.path.join(sys.argv[1], 'visionjk.x86_64')):
        sys.exit(__doc__)
    game = os.path.abspath(sys.argv[1])
    wanted = set(sys.argv[2:])
    pictures = [p for p in options.PICTURES.values() if not wanted or p[0] in wanted]
    if not pictures:
        sys.exit('no picture of that name in options.PICTURES')

    home = tempfile.mkdtemp(prefix='vision-pictures-')
    os.makedirs(os.path.join(home, 'japlus'))
    with open(os.path.join(home, 'japlus', 'pictures.cfg'), 'w') as f:
        f.write('\n'.join(script(pictures)) + '\n')
    env = dict(os.environ, HEADLESS_TIMEOUT=str(120 + 40 * len(pictures)), HEADLESS_LOG=os.path.join(home, 'run.log'))
    # (v_ids 0: a test folder of the game can be a temporary one, which the memory monitor takes for an attack)
    cmd = [HEADLESS, './visionjk.x86_64', '+set', 'fs_homepath', home, '+set', 'v_ids', '0', '+set', 'v_updateCheck', '0',
           '+set', 'v_master', '', '+set', 'cl_motd', '0', '+set', 's_volume', '0', '+set', 'r_mode', '-1',
           '+set', 'r_customwidth', '1280', '+set', 'r_customheight', '720', '+set', 'r_fullscreen', '0',
           '+set', 'com_maxfps', '60', '+set', 'cl_renderer', 'rd-vulkan-vision', '+set', 'cg_draw2D', '0',
           '+set', 'v_hud', '0', '+set', 'cl_noprint', '1',
           '+exec', 'pictures.cfg']
    code = subprocess.call(cmd, cwd=game, env=env)
    shots = os.path.join(home, 'japlus', 'screenshots')
    os.makedirs(OUT, exist_ok=True)
    missing = []
    for name, _, _, _, _, (_, crop) in pictures:
        for half in ('a', 'b'):
            shot = os.path.join(shots, '%s_%s.jpg' % (name, half))
            if not os.path.exists(shot):
                missing.append(shot)
                continue
            cut(shot, crop, os.path.join(OUT, '%s_%s.jpg' % (name, half)))
    print('the run: %s (exit %d), its log run.log' % (home, code))
    if missing:
        sys.exit('no screenshot: ' + ', '.join(os.path.basename(m) for m in missing))
    print('%d pictures in %s' % (2 * len(pictures), os.path.relpath(OUT, TREE)))


if __name__ == '__main__':
    main()
