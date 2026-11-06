#!/usr/bin/env python3
"""The test map of the materials, vjk_matlab: rooms to judge a material in (v_matdebug, v_matlight).

  python3 tools/maps/matlab.py [--q3map2 PATH] [--gamedata PATH] [--work DIR]

Writes into assets/japlus_client (packed into zz_japlus_client_assets.pk3 by the build):
  textures/vjk_matlab/*.png       the pictures of the lab: grey grid, the colour chart, checker, lamp
  shaders/vjk_matlab.shader       its shaders with the keywords of every renderer (q3map2 reads these)
  shaders/vjk_matlab.oshader      the same shaders with their materials, for rd-vulkan-vision (rend2 drops a shader
                                  with a word it does not know, and never reads .oshader files)
  maps/vjk_matlab.bsp             compiled with q3map2 (BSP, VIS, light)
  maps/vjk_matlab.cams            the views the quality control takes its pictures from: name, origin, yaw, pitch
The .map and the models (OBJ, only q3map2 reads them: they are baked into the level) go to the work folder.

The rooms, one after the other along x, joined by doorways:
  studio   a grey room with even light: test objects on plinths (sphere, cube, cylinder, a sphere on a cube, an
           upright panel) with the shader textures/vjk_matlab/object_a, which v_matdebug remaps to the material that
           is judged; a chrome ball and an 18 percent grey ball; a colour chart (the 24 patches of the ColorChecker,
           sRGB) and a grey card on the far wall; a grid of 64 units on floor and walls (a player is 64 high)
  court    open to the sky (the desert sky with its sun): objects with object_b, a polished metal strip on the floor
  dark     a dark hall lit only by lamp panels that give light: objects with object_c, for the lamps of v_matlight
  glass    a checker wall behind panes of glass (clear, tinted, frosted) and a pool of water for comparison
"""

import argparse
import math
import os
import subprocess
import sys

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASSETS = os.path.join(REPO, "assets", "japlus_client")
NAME = "vjk_matlab"
TEX = "textures/vjk_matlab"

# the 24 patches of the ColorChecker Classic in sRGB (the values X-Rite published for the chart made after 2014),
# row by row from the top left
COLOR_CHART = [
    (115, 82, 68), (194, 150, 130), (98, 122, 157), (87, 108, 67), (133, 128, 177), (103, 189, 170),
    (214, 126, 44), (80, 91, 166), (193, 90, 99), (94, 60, 108), (157, 188, 64), (224, 163, 46),
    (56, 61, 150), (70, 148, 73), (175, 54, 60), (231, 199, 31), (187, 86, 149), (8, 133, 161),
    (243, 243, 242), (200, 200, 200), (160, 160, 160), (122, 122, 121), (85, 85, 85), (52, 52, 52),
]
GREY18 = 118    # sRGB of 18 percent reflectance, the grey of a photographer's grey card


# ---------------------------------------------------------------------------------------------------------------
# pictures

def write_textures():
    out = os.path.join(ASSETS, TEX)
    os.makedirs(out, exist_ok=True)

    def grid(name, base, line):
        img = Image.new("RGB", (256, 256), (base, base, base))
        px = img.load()
        for i in range(256):
            for w in (0, 1):
                for j in range(0, 256, 64):
                    px[j + w, i] = (line, line, line)
                    px[i, j + w] = (line, line, line)
        img.save(os.path.join(out, name + ".png"))

    grid("floor", GREY18, GREY18 - 22)
    grid("wall", 150, 132)
    grid("ceiling", 170, 160)

    # the objects: light grey with a faint checker, so the tiling of a remapped material is not mistaken for it
    img = Image.new("RGB", (256, 256))
    px = img.load()
    for y in range(256):
        for x in range(256):
            v = 200 if ((x // 32) + (y // 32)) % 2 == 0 else 190
            px[x, y] = (v, v, v)
    img.save(os.path.join(out, "object.png"))

    Image.new("RGB", (8, 8), (GREY18,) * 3).save(os.path.join(out, "grey18.png"))
    Image.new("RGB", (8, 8), (235, 235, 235)).save(os.path.join(out, "chrome.png"))
    for i, rgb in enumerate(COLOR_CHART):
        Image.new("RGB", (8, 8), rgb).save(os.path.join(out, "chart%02d.png" % i))

    img = Image.new("RGB", (256, 256))
    px = img.load()
    for y in range(256):
        for x in range(256):
            v = 235 if ((x // 32) + (y // 32)) % 2 == 0 else 30
            px[x, y] = (v, v, v)
    img.save(os.path.join(out, "checker.png"))

    # a lamp panel: a white field in a dark frame, and its glow
    img = Image.new("RGB", (64, 64), (40, 40, 44))
    glow = Image.new("RGB", (64, 64), (0, 0, 0))
    for y in range(8, 56):
        for x in range(8, 56):
            img.putpixel((x, y), (255, 250, 235))
            glow.putpixel((x, y), (255, 245, 220))
    img.save(os.path.join(out, "lamp.png"))
    glow.save(os.path.join(out, "lamp_glow.png"))

    # glass: light, nearly white, its alpha says how much shows through
    for name, rgba in (("glass_clear", (230, 240, 240, 40)), ("glass_tint", (90, 170, 150, 110)), ("glass_frost", (225, 230, 232, 170))):
        Image.new("RGBA", (8, 8), rgba).save(os.path.join(out, name + ".png"))


# ---------------------------------------------------------------------------------------------------------------
# shaders

def lightmapped(name, image, material=None, extra=""):
    """A plain surface lit by its lightmap; material: the stage lines of the material (.oshader only)."""
    mat = "".join("\t\t%s\n" % line for line in (material or []))
    return (
        "%s/%s\n{\n\tqer_editorimage %s/%s.png\n%s\t{\n\t\tmap $lightmap\n\t}\n\t{\n\t\tmap %s/%s.png\n"
        "\t\tblendFunc GL_DST_COLOR GL_ZERO\n%s\t}\n}\n" % (TEX, name, TEX, image, extra, TEX, image, mat))


def shaders(with_materials):
    m = (lambda lines: lines) if with_materials else (lambda lines: None)
    plain = [
        ("floor", "floor", ["normalGen 0.6 1 0.3", "roughness 0.85", "metalness 0"]),
        ("wall", "wall", ["roughness 0.9", "metalness 0"]),
        ("ceiling", "ceiling", ["roughness 0.9", "metalness 0"]),
        ("object_a", "object", ["roughness 0.5", "metalness 0"]),
        ("object_b", "object", ["roughness 0.5", "metalness 0"]),
        ("object_c", "object", ["roughness 0.5", "metalness 0"]),
        ("grey18", "grey18", ["roughness 0.9", "metalness 0"]),
        ("chrome", "chrome", ["roughness 0.05", "metalness 1"]),
        ("mirrorfloor", "chrome", ["roughness 0.08", "metalness 1"]),
        ("checker", "checker", ["roughness 0.6", "metalness 0"]),
    ]
    text = []
    for name, image, mat in plain:
        text.append(lightmapped(name, image, m(mat)))
    for i in range(len(COLOR_CHART)):
        text.append(lightmapped("chart%02d" % i, "chart%02d" % i, m(["roughness 0.8", "metalness 0"])))

    # a lamp panel that gives light to the lightmap (q3map_surfacelight) and glows
    text.append(
        "%s/lamp\n{\n\tqer_editorimage %s/lamp.png\n\tq3map_surfacelight 6000\n\tq3map_lightRGB 1 0.95 0.85\n"
        "\tsurfaceparm nomarks\n\t{\n\t\tmap $lightmap\n\t}\n\t{\n\t\tmap %s/lamp.png\n\t\tblendFunc GL_DST_COLOR GL_ZERO\n\t}\n"
        "\t{\n\t\tmap %s/lamp_glow.png\n\t\tblendFunc GL_ONE GL_ONE\n\t\tglow\n\t}\n}\n" % (TEX, TEX, TEX, TEX))

    # glass: blended, no lightmap, lets light through; with its material rd-vulkan-vision bends and tints what is
    # behind it (transmission, ior, thickness, absorption)
    glass = {
        "glass_clear": ["roughness 0.02", "metalness 0", "transmission 1", "ior 1.5"],
        "glass_tint": ["roughness 0.04", "metalness 0", "transmission 1", "ior 1.52", "thickness 6", "absorption 0.45 0.8 0.65 8"],
        "glass_frost": ["roughness 0.55", "metalness 0", "transmission 1", "ior 1.5"],
    }
    for name, mat in glass.items():
        lines = "".join("\t\t%s\n" % line for line in (mat if with_materials else []))
        text.append(
            "%s/%s\n{\n\tqer_editorimage %s/%s.png\n\tsurfaceparm trans\n\tsurfaceparm nolightmap\n\tsurfaceparm glass\n"
            "\tcull none\n\t{\n\t\tmap %s/%s.png\n\t\tblendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA\n\t\trgbGen identity\n%s\t}\n}\n"
            % (TEX, name, TEX, name, TEX, name, lines))

    # the sky: the desert sky of the game, with a sun for the lightmap and the traced shadows
    text.append(
        "%s/sky\n{\n\tqer_editorimage textures/skies/sky.tga\n\tsurfaceparm sky\n\tsurfaceparm noimpact\n"
        "\tsurfaceparm nomarks\n\tsurfaceparm nolightmap\n\tq3map_sun 1 0.95 0.85 180 40 55\n"
        "\tq3map_skylight 220 4\n\tsun 255 242 217 180 40 55\n\tskyParms textures/skies/desert 512 -\n}\n" % TEX)

    head = ("// the test map of the materials (tools/maps/matlab.py writes this file, do not edit it by hand)%s\n\n"
            % (": with the materials of rd-vulkan-vision, read after the shader files" if with_materials else ""))
    return head + "\n".join(text)


def write_shaders():
    folder = os.path.join(ASSETS, "shaders")
    with open(os.path.join(folder, NAME + ".shader"), "w", encoding="latin-1") as f:
        f.write(shaders(False))
    with open(os.path.join(folder, NAME + ".oshader"), "w", encoding="latin-1") as f:
        f.write(shaders(True))


# ---------------------------------------------------------------------------------------------------------------
# models (OBJ, baked into the level by q3map2)

def obj(name, verts, uvs, normals, faces, shader):
    """faces: lists of (v, vt, vn) indexes from 1"""
    lines = ["o %s" % name]
    lines += ["v %.4f %.4f %.4f" % v for v in verts]
    lines += ["vt %.4f %.4f" % t for t in uvs]
    lines += ["vn %.4f %.4f %.4f" % n for n in normals]
    lines.append("usemtl %s" % shader)
    for face in faces:
        lines.append("f " + " ".join("%d/%d/%d" % c for c in face))
    return "\n".join(lines) + "\n"


def sphere(radius, shader, rings=24, segments=48):
    verts, uvs, normals, faces = [], [], [], []
    for r in range(rings + 1):
        phi = math.pi * r / rings
        for s in range(segments + 1):
            theta = 2 * math.pi * s / segments
            n = (math.sin(phi) * math.cos(theta), math.sin(phi) * math.sin(theta), math.cos(phi))
            verts.append(tuple(radius * c for c in n))
            normals.append(n)
            uvs.append((2.0 * s / segments, r / rings))
    row = segments + 1
    for r in range(rings):
        for s in range(segments):
            a, b, c, d = r * row + s + 1, r * row + s + 2, (r + 1) * row + s + 2, (r + 1) * row + s + 1
            if r > 0:
                faces.append([(a, a, a), (d, d, d), (b, b, b)])
            if r < rings - 1:
                faces.append([(b, b, b), (d, d, d), (c, c, c)])
    return obj("sphere", verts, uvs, normals, faces, shader)


def box_model(name, half, shader, uv_scale=1.0 / 64):
    hx, hy, hz = half
    verts, uvs, normals, faces = [], [], [], []
    sides = [
        ((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, -1, 0), (0, 0, 1)),
        ((0, 1, 0), (-1, 0, 0), (0, 0, 1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
        ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (1, 0, 0), (0, -1, 0)),
    ]
    size = (hx, hy, hz)
    for n, u, v in sides:
        base = len(verts)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = tuple(n[i] * size[i] + su * u[i] * size[i] + sv * v[i] * size[i] for i in range(3))
            verts.append(p)
            normals.append(n)
            uvs.append((sum(p[i] * u[i] for i in range(3)) * uv_scale, -sum(p[i] * v[i] for i in range(3)) * uv_scale))
        k = [base + 1, base + 2, base + 3, base + 4]
        faces.append([(k[0],) * 3, (k[1],) * 3, (k[2],) * 3])
        faces.append([(k[0],) * 3, (k[2],) * 3, (k[3],) * 3])
    return obj(name, verts, uvs, normals, faces, shader)


def cylinder(radius, half_height, shader, segments=48):
    verts, uvs, normals, faces = [], [], [], []
    for s in range(segments + 1):
        theta = 2 * math.pi * s / segments
        n = (math.cos(theta), math.sin(theta), 0.0)
        for z in (-half_height, half_height):
            verts.append((radius * n[0], radius * n[1], z))
            normals.append(n)
            uvs.append((2.0 * s / segments, -z / 64.0))
    for s in range(segments):
        a, b, c, d = 2 * s + 1, 2 * s + 2, 2 * s + 4, 2 * s + 3
        faces.append([(a,) * 3, (d,) * 3, (c,) * 3])
        faces.append([(a,) * 3, (c,) * 3, (b,) * 3])
    for z, nz in ((half_height, 1.0), (-half_height, -1.0)):
        center = len(verts) + 1
        verts.append((0.0, 0.0, z))
        normals.append((0.0, 0.0, nz))
        uvs.append((0.5, 0.5))
        ring = []
        for s in range(segments):
            theta = 2 * math.pi * s / segments
            verts.append((radius * math.cos(theta), radius * math.sin(theta), z))
            normals.append((0.0, 0.0, nz))
            uvs.append((0.5 + 0.5 * math.cos(theta), 0.5 + 0.5 * math.sin(theta)))
            ring.append(len(verts))
        for s in range(segments):
            a, b = ring[s], ring[(s + 1) % segments]
            faces.append([(center,) * 3, (a,) * 3, (b,) * 3] if nz > 0 else [(center,) * 3, (b,) * 3, (a,) * 3])
    return obj("cylinder", verts, uvs, normals, faces, shader)


def write_models(work):
    folder = os.path.join(work, "models", NAME)
    os.makedirs(folder, exist_ok=True)
    models = {}
    for letter in "abc":
        shader = "%s/object_%s" % (TEX, letter)
        models["sphere_" + letter] = sphere(40, shader)
        models["cube_" + letter] = box_model("cube", (32, 32, 32), shader)
        models["cylinder_" + letter] = cylinder(32, 48, shader)
        models["panel_" + letter] = box_model("panel", (4, 64, 64), shader)
    models["chrome"] = sphere(40, TEX + "/chrome")
    models["grey18"] = sphere(40, TEX + "/grey18")
    for name, text in models.items():
        with open(os.path.join(folder, name + ".obj"), "w") as f:
            f.write(text)


# ---------------------------------------------------------------------------------------------------------------
# the level

class Map:
    def __init__(self):
        self.brushes = []
        self.entities = []

    def box(self, mins, maxs, shader, caulk="common/caulk", faces=None):
        """An axis aligned brush; faces: {'+z': shader, ...} for the faces that are seen, the rest caulk."""
        (x0, y0, z0), (x1, y1, z1) = mins, maxs
        planes = {
            "-z": ((x0, y0, z0), (x1, y1, z0), (x0, y1, z0)),
            "+z": ((x0, y0, z1), (x0, y1, z1), (x1, y1, z1)),
            "-y": ((x0, y0, z0), (x1, y0, z1), (x1, y0, z0)),
            "+y": ((x1, y1, z0), (x0, y1, z1), (x0, y1, z0)),
            "-x": ((x0, y1, z0), (x0, y0, z1), (x0, y0, z0)),
            "+x": ((x1, y0, z0), (x1, y1, z1), (x1, y1, z0)),
        }
        lines = []
        for side, (a, b, c) in planes.items():
            sh = shader if faces is None else faces.get(side, caulk)
            lines.append("( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s 0 0 0 1 1 0 0 0" % (*a, *b, *c, sh))
        self.brushes.append("{\n" + "\n".join(lines) + "\n}\n")

    def entity(self, classname, **keys):
        self.entities.append(dict(classname=classname, **keys))

    def text(self, worldspawn):
        out = ['{\n"classname" "worldspawn"\n']
        out += ['"%s" "%s"\n' % kv for kv in worldspawn.items()]
        out += self.brushes
        out.append("}\n")
        for e in self.entities:
            out.append("{\n" + "".join('"%s" "%s"\n' % (k, v) for k, v in e.items()) + "}\n")
        return "".join(out)


def tex(name):
    return "vjk_matlab/" + name


def room(m, x0, x1, y0, y1, height, sky=False, floor="floor", wall="wall", ceiling="ceiling"):
    """Floor, ceiling (or sky) and the long walls of a room inside x0..x1, y0..y1, 0..height, 16 thick. The walls
    across x are made by cross_wall, once between two rooms."""
    t = 16
    m.box((x0 - t, y0 - t, -t), (x1 + t, y1 + t, 0), None, faces={"+z": tex(floor)})
    m.box((x0 - t, y0 - t, height), (x1 + t, y1 + t, height + t), None, faces={"-z": tex("sky" if sky else ceiling)})
    m.box((x0 - t, y0 - t, 0), (x1 + t, y0, height), None, faces={"+y": tex(wall)})
    m.box((x0 - t, y1, 0), (x1 + t, y1 + t, height), None, faces={"-y": tex(wall)})


def cross_wall(m, x, y0, y1, height, west=True, east=True, door=False, wall="wall"):
    """The wall from x to x + 16 across y0..y1, seen from the room to the west (-x), the east (+x) or both; door: an
    opening 192 wide and 160 high in its middle."""
    faces = {}
    if west:
        faces["-x"] = tex(wall)
    if east:
        faces["+x"] = tex(wall)
    if not door:
        m.box((x, y0, 0), (x + 16, y1, height), None, faces=faces)
        return
    m.box((x, y0, 0), (x + 16, -96, height), None, faces=dict(faces, **{"+y": tex(wall)}))
    m.box((x, 96, 0), (x + 16, y1, height), None, faces=dict(faces, **{"-y": tex(wall)}))
    m.box((x, -96, 160), (x + 16, 96, height), None, faces=dict(faces, **{"-z": tex(wall)}))


def plinth(m, x, y, model, height=32):
    m.box((x - 48, y - 48, 0), (x + 48, y + 48, height), tex("wall"))
    m.entity("misc_model", model="models/%s/%s.obj" % (NAME, model), origin="%g %g %g" % (x, y, height + 48),
             spawnflags="4", _lightmapscale="0.5")


def build_map():
    m = Map()
    cams = []
    # the rooms along x (inside), and the walls between them
    studio, court, dark, glass = (0, 1280), (1296, 2576), (2592, 3616), (3632, 4656)

    cross_wall(m, -16, -656, 656, 656, west=False)
    cross_wall(m, studio[1], -656, 656, 656, door=True)
    cross_wall(m, court[1], -656, 656, 656, door=True)
    cross_wall(m, dark[1], -528, 528, 528, door=True)
    cross_wall(m, glass[1], -528, 528, 528, east=False)

    # studio
    room(m, studio[0], studio[1], -640, 640, 640)
    for i, model in enumerate(("sphere_a", "cube_a", "cylinder_a", "panel_a")):
        plinth(m, 640, -360 + i * 240, model)
    # a sphere on a cube, the shape of a shader ball
    plinth(m, 420, 0, "cube_a")
    m.entity("misc_model", model="models/%s/sphere_a.obj" % NAME, origin="420 0 152", spawnflags="4", _lightmapscale="0.5")
    plinth(m, 900, -120, "chrome")
    plinth(m, 900, 120, "grey18")
    # the colour chart and the grey card on the far wall, 2 in front of it
    for i in range(len(COLOR_CHART)):
        row, col = divmod(i, 6)
        y = -165 + col * 56
        z = 360 - row * 56
        m.box((1276, y, z), (1278, y + 52, z + 52), None, faces={"-x": tex("chart%02d" % i)})
    m.box((1276, 260, 200), (1278, 388, 328), None, faces={"-x": tex("grey18")})
    for x in (320, 640, 960):
        for y in (-400, 0, 400):
            m.entity("light", origin="%d %d 560" % (x, y), light="2600", _color="1 1 1")
    m.entity("info_player_deathmatch", origin="160 0 24", angle="0")
    cams += [("studio_objects", (200, 0, 120), 0, 8), ("studio_chart", (1000, 0, 280), 0, 0),
             ("studio_balls", (760, 0, 140), 0, 10)]

    # court, open to the sky
    room(m, court[0], court[1], -640, 640, 640, sky=True)
    for i, model in enumerate(("sphere_b", "cube_b", "cylinder_b", "panel_b")):
        plinth(m, 2100, -360 + i * 240, model)
    m.box((1420, -64, 0), (2420, 64, 1), None, faces={"+z": tex("mirrorfloor")})
    cams += [("court_objects", (1620, 0, 140), 0, 6), ("court_mirror", (1340, 0, 90), 0, 18)]

    # dark hall, lit by lamp panels only
    room(m, dark[0], dark[1], -512, 512, 384)
    for i, model in enumerate(("sphere_c", "cube_c", "cylinder_c", "panel_c")):
        plinth(m, 3100, -300 + i * 200, model)
    m.box((3060, -512, 220), (3140, -510, 260), None, faces={"+y": tex("lamp")})
    m.box((3060, 510, 220), (3140, 512, 260), None, faces={"-y": tex("lamp")})
    cams += [("dark_objects", (2720, 0, 120), 0, 6)]

    # glass: a checker wall behind panes (clear, tinted, frosted, and tinted behind clear)
    room(m, glass[0], glass[1], -512, 512, 512)
    m.box((4640, -400, 0), (4642, 400, 400), None, faces={"-x": tex("checker")})
    for i, name in enumerate(("glass_clear", "glass_tint", "glass_frost")):
        y = -340 + i * 240
        m.box((4240, y, 0), (4244, y + 200, 300), tex(name))
    m.box((4340, -340, 0), (4344, -140, 300), tex("glass_tint"))
    for x in (3850, 4400):
        for y in (-300, 300):
            m.entity("light", origin="%d %d 460" % (x, y), light="1800", _color="1 1 1")
    cams += [("glass_panes", (3840, 0, 150), 0, 0)]

    worldspawn = {"message": "VisionJK material lab", "_lightmapscale": "0.5", "_minlight": "10",
                  "_ambient": "12", "gridsize": "64 64 64"}
    return m.text(worldspawn), cams


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--q3map2", default=os.path.expanduser("~/projects/agent/visionjk/netradiant-custom/install/q3map2"))
    parser.add_argument("--gamedata", default=os.path.join(os.path.dirname(REPO), "GameData"))
    parser.add_argument("--work", default=os.path.expanduser("~/projects/agent/visionjk/matlab-map"))
    parser.add_argument("--fast", action="store_true", help="light without bounces, for a quick look")
    args = parser.parse_args()

    write_textures()
    write_shaders()
    os.makedirs(os.path.join(args.work, "maps"), exist_ok=True)
    write_models(args.work)
    text, cams = build_map()
    mapfile = os.path.join(args.work, "maps", NAME + ".map")
    with open(mapfile, "w") as f:
        f.write(text)

    common = [args.q3map2, "-game", "ja", "-fs_basepath", args.gamedata, "-fs_game", "japlus",
              "-fs_homepath", os.path.join(args.work, "home"), "-fs_pakpath", ASSETS, "-fs_pakpath", args.work]
    light = ["-light", "-fast", "-samples", "2", "-patchshadows"] + ([] if args.fast else ["-bounce", "2"])
    for stage in (["-meta", "-patchmeta"], ["-vis", "-saveprt"], light):
        cmd = common + stage + [mapfile]
        print(" ".join(cmd), flush=True)
        result = subprocess.run(cmd, capture_output=True, text=True)
        out = result.stdout + result.stderr
        # (the OBJ loader says ERROR for every material without an .mtl file: the name is the shader, as wanted)
        errors = [line for line in out.splitlines() if "ERROR" in line and "failed to locate material" not in line]
        if result.returncode != 0 or "LEAKED" in out or errors:
            print(out[-4000:])
            sys.exit("q3map2 failed in %s" % stage[0])

    os.makedirs(os.path.join(ASSETS, "maps"), exist_ok=True)
    bsp = os.path.join(args.work, "maps", NAME + ".bsp")
    with open(bsp, "rb") as src, open(os.path.join(ASSETS, "maps", NAME + ".bsp"), "wb") as dst:
        dst.write(src.read())
    with open(os.path.join(ASSETS, "maps", NAME + ".cams"), "w") as f:
        f.write("// the views of the quality control in vjk_matlab: name, origin x y z, yaw, pitch (tools/maps/matlab.py)\n")
        for name, origin, yaw, pitch in cams:
            f.write("%s %g %g %g %g %g\n" % (name, *origin, yaw, pitch))
    print("wrote", os.path.join(ASSETS, "maps", NAME + ".bsp"))


if __name__ == "__main__":
    main()
