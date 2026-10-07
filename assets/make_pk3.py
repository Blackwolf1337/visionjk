#!/usr/bin/env python3
"""Packs a directory into a pk3 (a zip): make_pk3.py <directory> <order file> <output.pk3>

The entries are written in the order of the order file, one name a line, with its directory entries (the lines
that end in /). Both matter to the game: the engine lists the folders of a pk3 only when they have an entry of
their own, and lists (the heads in the player menu) follow the order of the entries. The order of
japlus_client.order is the one of the original clientPlugin_v1.4B4.pk3. Files that are not in the list come
last, by name. Files and folders whose name starts with a dot are left out.

The checksum a pure server compares is made of the checksums of the files in the order of the entries, so the
same files give the same pk3 for the game on every system. Every entry gets the same date.
"""
import os
import sys
import zipfile

DATE = (1980, 1, 1, 0, 0, 0)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__.splitlines()[0])
    root, order_file, out = sys.argv[1:]

    files = set()
    for path, dirs, names in os.walk(root):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for name in names:
            if not name.startswith("."):
                files.add(os.path.relpath(os.path.join(path, name), root).replace(os.sep, "/"))

    with open(order_file, encoding="utf-8") as f:
        order = [line.strip() for line in f if line.strip()]

    tmp = out + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        def add(name):
            info = zipfile.ZipInfo(name, DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(os.path.join(root, name), "rb") as f:
                z.writestr(info, f.read())
            files.discard(name)

        for name in order:
            if name.endswith("/"):
                if os.path.isdir(os.path.join(root, name)):
                    z.writestr(zipfile.ZipInfo(name, DATE), b"")
            elif name in files:
                add(name)
        for name in sorted(files):
            add(name)
    os.replace(tmp, out)


if __name__ == "__main__":
    main()
