#!/bin/bash
# Copies the materials saved in the material debug menu (v_matdebug) from a profile into the assets of the
# client, so the next build puts them into zz_japlus_client_assets.pk3: the files shaders/zz_mat_*.oshader (text
# only, the maps are made by the renderer when it loads, tr_matgen.cpp). A file there already is replaced. With
# --done only the shaders whose status in materials/status.txt is done are taken.
#
# usage: tools/materials/export.sh [--done] [PROFILE]
#   --done    only what is done
#   PROFILE   the mod folder of the profile, default ~/.local/share/visionjk/japlus
set -euo pipefail

done_only=false
if [ "${1:-}" = "--done" ]; then
	done_only=true
	shift
fi
profile=${1:-$HOME/.local/share/visionjk/japlus}
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
target=$repo/assets/japlus_client/shaders

shopt -s nullglob
files=("$profile"/shaders/zz_mat_*.oshader)
if [ ${#files[@]} -eq 0 ]; then
	echo "no saved materials in $profile/shaders" >&2
	exit 1
fi

if ! $done_only; then
	cp -v "${files[@]}" "$target/"
	exit 0
fi

# only the shaders that are done: each file is cut into its shaders (a name line, then its braces)
status=$profile/materials/status.txt
if [ ! -f "$status" ]; then
	echo "no $status, nothing is done" >&2
	exit 1
fi
python3 -I - "$status" "$target" "${files[@]}" <<'PY'
import sys, re, os
status_file, target, files = sys.argv[1], sys.argv[2], sys.argv[3:]
done = set()
for line in open(status_file, encoding='latin-1'):
    parts = line.split()
    if len(parts) == 2 and not line.startswith('//') and parts[1] == 'done':
        done.add(parts[0].lower())
for path in files:
    text = open(path, encoding='latin-1').read()
    header, kept, i = [], [], 0
    lines = text.split('\n')
    while i < len(lines):
        name = lines[i].strip()
        if not name or name.startswith('//'):
            if not kept and name:
                header.append(lines[i])
            i += 1
            continue
        depth, block, i = 0, [lines[i]], i + 1
        while i < len(lines):
            block.append(lines[i])
            depth += lines[i].count('{') - lines[i].count('}')
            i += 1
            if depth <= 0 and '}' in block[-1]:
                break
        if name.lower() in done:
            kept.append('\n'.join(block))
    out = os.path.join(target, os.path.basename(path))
    if kept:
        open(out, 'w', encoding='latin-1').write('\n'.join(header) + '\n\n' + '\n\n'.join(kept) + '\n')
        print(f"{out}: {len(kept)} done")
PY
