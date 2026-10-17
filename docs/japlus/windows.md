# The Windows client, run as a whole

The client and its modules are built for 64 bit Windows with mingw on Linux. Until 2026-10-11 only the updater
had been run on a real Windows ([update.md](update.md)); everything else was "built, never run". This is the
run that was owed.

## Where

Windows 11 Pro 24H2 (German, build 26100) in a virtual machine: 4 cores, 8 GB, no graphics card, a virtual
sound card that plays into nothing. So:

- OpenGL is Mesa's software renderer (llvmpipe, `opengl32.dll` beside the game), Vulkan is Mesa's lavapipe
  with the loader of Khronos (`vulkan-1.dll`) beside the game. A real graphics driver was not involved.
- Nothing was heard and no microphone spoke. Sound was checked by what the game reports and by what it would
  have played.
- Nobody sat at it. The game was driven by console scripts (`+exec`), nothing was clicked.

The build was the tree at `feature/select` (v0.1.5-8-g9974a0a7 and two small commits after it), from
`buildwin64`. `agent/japlus_port/tools/wintest/full.sh` makes the package and the batch file that runs the
scenarios in the VM and sends back logs and pictures.

## What ran

| | Result |
|---|---|
| Start | from an unpacked folder, no installer. Windows asks once whether the game may be reached from the network (the firewall question), and the game runs without an answer |
| `rd-vanilla` | starts on Mesa's OpenGL 4.6, draws menus and levels |
| `rd-vulkan-vision` | starts on lavapipe (Vulkan 1.4), draws the menus. The first time this DLL ran on Windows at all. A level was not loaded with it here |
| The new menus | the first page, Play, Setup with its new tabs, Controls, the profile page and the server browser look as on Linux, on both renderers. The fonts `vision_sans` and `vision_mono` are there |
| The new HUD | health, shield, force, saber, score, as on Linux |
| The menu in the level | the panel stands beside the player with the in-game bar and Setup on it (rd-vanilla) |
| Sound | SDL opens the device through WASAPI |
| Spatial sound (`v_spatial 1`) | `phonon.dll` is loaded (Steam Audio 4.8.1), the level is handed over (50749 triangles of mp/ffa3 in 96 ms) and its paths are worked out in the background (740 probes) and stored. It is off by default on Windows |
| Voice chat, devices | `v_voice_devices` lists the microphone and the speakers of Windows by name |
| Voice chat, two games | one game is the server and sends the test tone, a second one joins it over the loopback address: "Padawan talks, 8583 frames heard". Relay of the Windows game module, both ends on Windows. What the second game played was written to a file: the tone, both pitches, without a gap in 4 seconds on the sound device of Windows and in 22 seconds on the device that plays into nothing. `v_voice_status` says the speakers run ("16.4 seconds played so far", 4 seconds later 20.4) |
| Clan radio | both the relay of a server and the one for servers without the module (the masterserver's, a program of its own then) say they have it. Not talked over |
| Statuses, by the server | both players read the music of the other (`v_status_test`), and each is "Away from keyboard" after a minute without a key |
| Music from a window title | a window whose title ends in " - YouTube Music" (Notepad with a file of that name stood in for the browser): "Listening to YT Music: Test Band Windows Song" above the player. `v_statuses` now also says what music the game finds |
| The relay for servers without the module | a Windows build of it as a program of its own ("the master", the voice master of then) ran in the VM. A game on a server that says nothing of voice (a stand-in game module) and reached by the address of the network links to it: "through the voice master ... connected" (the line of then), and its statuses go through it. With one player there, so nobody was heard that way |
| Admin login, key file | `v_amkey new` writes the key, the server reads it from `admin_keys.txt`, `v_amkey login` logs in, with `sv_floodProtect 1` |
| Admin login, ssh-agent | the OpenSSH agent of Windows (its service has to be switched on, which asks for the administrator once) holds an Ed25519 key, `v_authAgent 1`: "Waiting for your ssh-agent to sign", then logged in |
| Chat | a line with umlauts, the euro sign and Greek letters is sent and comes back as it was typed |
| Demos | `record` writes one |
| A long run | three bots (Alora, Desann, Cultist) fought for eight and a half minutes on mp/ffa3 next to a player who stood still. The game took 448 to 454 MB at eight looks a minute apart, with no trend, and its log has no error |
| The updater | the four scenarios of [update.md](update.md) again with this tree (nothing to do, a repair, modules of the player in the way, an update): every file the one of the list, nothing left over. The update needed a second go: in the first one the script of the test ended the game while the second file was still to be fetched, with the VM busy from the runs before |
| A game started from the client | the Windows game module runs a level: spawn, JA+ messages, statuses, relay, key login. Its web panel says it is off for lack of an account, as it should |

## What it found

- The outline of a clicked player crashed the game on any system: fixed before it was committed, see
  [client.md](client.md), "Clickable players". Found by the Linux run that was made for this one.
- `v_spatial` is 0 by default on Windows and 1 on Linux. Nothing was wrong with 1 here. Left as it is: nobody
  has listened to it on Windows.
- Tests, not the game: the bot of the first run did not exist by that name, the script of a game that joins
  another one ran out before it was in (it draws fewer frames than the server next to it, and a script
  counts frames), and a little program that was to stand in for Spotify did not stay alive in the VM. All of
  it was run again in another way.
- Two commands say more since this run, because the run needed to know: `v_voice_status` tells whether the
  speakers really play, `v_statuses` what music is found.

Nothing in the Windows code had to be changed.

## Not checked

- A real graphics card and its drivers, OpenGL and Vulkan. A level on `rd-vulkan-vision`.
- Anything heard: how voices, the radio and the spatial sound sound, a real microphone, echo.
- A mouse and a keyboard in a person's hands: clicks in the menus, the pointer on a player, the card, the
  console marked with the mouse, a link opened from the chat.
- The internet: the server browser with real servers, the masterserver at its real address, a download.
- The music status from Spotify itself (only the window of a browser with YouTube Music was imitated).
- A dedicated server for Windows (the tree builds none), siege, a team game.
- Windows 10, a Windows in another language than German, a user without administrator rights (apart from the
  one question of the ssh-agent, nothing asked for them).
- A stock `jamp.exe` with these modules: it is 32 bit, the tree is 64 bit only.
