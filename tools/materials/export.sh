#!/bin/bash
# Copies the materials saved in the material debug menu (v_matdebug) from a profile into the assets of the
# client, so the next build puts them into zz_japlus_client_assets.pk3: each shaders/zz_mat_<file>.oshader of the
# profile becomes assets/japlus_client/shaders/vmat_<file>.oshader (text only, the maps are made by the renderer
# when it loads, tr_matgen.cpp). The shipped files have a name of their own so that a player's own saves
# (zz_mat_<file> in the profile) never hide a whole shipped file: overrides are read in the order of their names,
# vmat_ before zz_mat_, so a saved shader takes the place of the shipped one of its name alone. A file there
# already is replaced. With --done only the shaders whose status in materials/status.txt is done are taken.
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
status=$profile/materials/status.txt
if $done_only && [ ! -f "$status" ]; then
	echo "no $status, nothing is done" >&2
	exit 1
fi

# each file is cut into its shaders (a name line, then its braces); with --done only those that are done stay
python3 -I - "$done_only" "${status:-}" "$target" "${files[@]}" <<'PY'
import sys, os
done_only, status_file, target, files = sys.argv[1] == 'true', sys.argv[2], sys.argv[3], sys.argv[4:]
done = set()
if done_only:
    for line in open(status_file, encoding='latin-1'):
        parts = line.split()
        if len(parts) == 2 and not line.startswith('//') and parts[1] == 'done':
            done.add(parts[0].lower())
total = 0
for path in files:
    lines = open(path, encoding='latin-1').read().split('\n')
    header, kept, i = [], [], 0
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
        if not done_only or name.lower() in done:
            kept.append('\n'.join(block))
    out = os.path.join(target, 'vmat_' + os.path.basename(path)[len('zz_mat_'):])
    if kept:
        open(out, 'w', encoding='latin-1').write('\n'.join(header) + '\n\n' + '\n\n'.join(kept) + '\n')
        print(f"{os.path.relpath(out)}: {len(kept)} shaders")
        total += len(kept)
print(f"{total} shaders exported")
PY
