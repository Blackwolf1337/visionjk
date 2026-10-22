# Training bot tools

Scripts for the clone and for the values of the training bot (docs/japlus/server.md, "Training bot"). Python 3 with
numpy. scikit-learn is needed only by `train_clone.py`. They run on any Linux machine that has the release zip and
the four base pk3 files.

## Steps

1. Record a player: the "Record moves" button in the web panel or the server command `vs_movelog <player>`. The logs
   are CSV files in `movelog/` in the home folder of the server.
2. Train the clone from the logs. It holds out one recording to judge the forests by and writes the model file.
3. Run `gen_tables.py` only when the animations (`anims.h`) or the saber moves (`bg_public.h`) change. It writes
   `codemp/game/trainer/clone_tables.h`, which goes into the game module, so build it again afterwards.
4. Try a match with `simrun.py`.
5. Tune the values with `optimize.py` against the clone.
6. Use `OUT/best.cfg` with `vs_trainerParams`, or copy the values into `codemp/game/trainer/trainer_params.h` to make
   them the new defaults.

## Files

| File | Use |
|------|-----|
| `trainerlib.py` | library: enums from `anims.h` and `bg_public.h`, reading the movement log, the features and actions, frames of 50 ms |
| `gen_tables.py` | writes `clone_tables.h` |
| `train_clone.py` | trains the clone from logs, writes the model file |
| `simrun.py` | one headless match, prints the result as `key=value` |
| `optimize.py` | CMA-ES over the 52 values of `trainer_params.h` against the clone |

## Commands

```
train_clone.py --test ~/movelog/2026-10-20_201500_3.csv --out clone.dat ~/movelog/
gen_tables.py
simrun.py --game ~/vjk --clone clone.dat --map mp/duel1 --rounds 30 5 clone
optimize.py --game ~/vjk --clone clone.dat --out run1 --workers 8 --gens 40 --pop 12 --rounds 20
```

`train_clone.py` takes logs or folders of logs. `--test` is a whole recording kept out of the training. Without it the
newest log is taken. The model file is read by `codemp/game/trainer/g_clone.c` (`vs_cloneModel`, default
`trainer/clone.dat` in the game folder of the server). Its layout is in `write_model()` of `train_clone.py`.

`simrun.py` takes the two sides last: a level 1 to 5 or `clone`. `--params FILE` is a file for `vs_trainerParams`.
`--clone FILE` is the model. The default map is `mp/duel1`.

`optimize.py` scales the values around their defaults and leaves out the ones that must stay as they are (how the bot
learns, the timing of the attacks). The fitness is the damage the bot deals minus the damage it takes, per minute,
over the maps of `--maps` (default `mp/duel1,mp/duel5,mp/ffa3`) against the clone, `--level` is the level of the bot
(default 5). Every generation it writes `OUT/best.cfg` and a line to `OUT/history.csv`.

## What they need

`--game DIR` has `visionjkded.x86_64`, `base/` with the four base pk3 files (`assets0.pk3` to `assets3.pk3`) and
`japlus/` with the game module (from the release zip, or built from this tree, as `clone_tables.h` and
`trainer_params.h` are part of it). Each match starts the server with its own home folder and port, started with
`devmap`, `fixedtime 50`, `sv_fps 20` and `vs_simFast 1`, so several run at once.
