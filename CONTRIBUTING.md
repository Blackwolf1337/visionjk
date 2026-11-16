# Contributing to VisionJK

Thanks for helping. Please read the [Code of Conduct](CODE_OF_CONDUCT.md) first. Security problems do not go into issues or pull requests, see [SECURITY.md](SECURITY.md).

## Building

```
cmake -S . -B build64 -DCMAKE_BUILD_TYPE=Release
cmake --build build64 -j"$(nproc)"
cmake --install build64 --prefix release
```

`release/JediAcademy` is then a complete game folder. The Windows cross build with mingw-w64 and the rest are in [BUILDING.md](BUILDING.md).

## Code style

The code follows the idTech 3 / Raven Software style. Match the surrounding file.

- C, built with `-Wall -Wextra`. Do not add new warnings.
- Use bounded string functions such as `Q_strncpyz`, `Com_sprintf` or `snprintf`. No `strcpy`, `strcat` or `sprintf`.
- No blocking waits or sleeps in game code. Use callbacks and per frame checks, so the game never hangs.
- Look for an existing helper in the engine or the project before writing a new one.
- Comments only where the code is not obvious: offsets, signatures, why a hook exists, which version it applies to.

New cvars and commands start with `v_` (client) or `vs_` (server and wire).

## Branches

Work on `feature/<name>` or `fix/<name>`, made from `develop`. Pull requests go into `develop`. `master` only holds releases. The maintainer makes the `release/<version>` and `hotfix/<version>` branches.

As the README says, pull requests are not merged on GitHub itself. The change is applied in the main repository with the author kept, and the pull request is closed after the next sync.

## Commits

Keep commits small, one topic each. The message is plain English and says what changed.

## Pull requests

Use the pull request template. It asks what changed, why, how it was tested, what was not tested, and which platforms were built.

## Questions

Ask on Discord, user `xleuiv`, or see the website, [jka.vision](https://jka.vision).
