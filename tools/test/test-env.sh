# shellcheck shell=bash disable=SC2034 # the scripts that source this use these
# Where the test scripts of tools/test find things, sourced by each of them. Every path can be given in the
# environment; the defaults are the folders next to this repository on the machine of the project:
#   VJK_TEST_WORK        what the tests make (builds, unpacked games, logs): build-test in this repository
#   VJK_BASE_ASSETS      assets0.pk3 to assets3.pk3 of the original game: ../GameData/base
#   VJK_DOWNLOADS        the release zips: ../visionjk-website/site/downloads (tools/build-game.sh writes them)
#   VJK_AGENT            the working folder of the agent, with the Steam Audio SDK and stock OpenJK:
#                        ../../agent/visionjk
#   VJK_STEAMAUDIO_SDK   the unpacked Steam Audio SDK: $VJK_AGENT/japlus_port/steamaudio/steamaudio
#   VJK_STOCK_SRC        the source of stock OpenJK for compat-test.sh: $VJK_AGENT/openjk_stock/src
#   VJK_WEBSITE          the website repository, whose scripts lint.sh checks too when it is there:
#                        ../visionjk-website
#   VJK_JOBS, VJK_NICE   build jobs (all cores) and their niceness (10)

TEST_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
GAME_REPO=$(cd "$TEST_DIR/../.." && pwd)
ROOT=$(dirname "$GAME_REPO")
WORK=${VJK_TEST_WORK:-$GAME_REPO/build-test}
BASE_ASSETS=${VJK_BASE_ASSETS:-$ROOT/GameData/base}
DOWNLOADS=${VJK_DOWNLOADS:-$ROOT/visionjk-website/site/downloads}
AGENT=${VJK_AGENT:-$(dirname "$ROOT")/agent/visionjk}
STEAMAUDIO_SDK=${VJK_STEAMAUDIO_SDK:-$AGENT/japlus_port/steamaudio/steamaudio}
STOCK_SRC=${VJK_STOCK_SRC:-$AGENT/openjk_stock/src}
WEBSITE=${VJK_WEBSITE:-$ROOT/visionjk-website}
BUILD_JOBS=${VJK_JOBS:-$(nproc)}
BUILD_NICE=${VJK_NICE:-10}
