"""The options screens of the new look: Setup and Controls, outside a game and in one (tools/menus/build.py).

One layout for the four of them: the pages of the screen as a row of tabs at its top, the settings of the open
page as a list of rows (its name at the left, its widget at the right) that the wheel moves, beside the list
what the row under the mouse is for, and a bar at the bottom with the way back. The code that draws and moves
all of that is in codemp/ui/ui_shared.c ("page", "tab", "scroll", "row", ITEM_TYPE_DESC, ITEM_TYPE_PAGESCROLL).

The rows of the old menu are taken as they are (their cvar, their list, their script) and put into the pages
by the tables below. A row the old menus did not have is written there in full."""
import menufile as mf
from menufile import Entry
from theme import *         # noqa: F401,F403
import texts
from texts import ref

ROW_H = 22                  # a row of a list, and a heading in it
ROWS = 16                   # rows the window of a list shows
ROW_SCALE = 0.7
CLICK = ['play', '"sound/interface/button1.wav"', ';']
WINDOW = ['0.035', '0.07', '0.12', '0.94']
BAND = ['0.02', '0.04', '0.08', '0.50']         # the row of tabs and the bar at the bottom, as pages.FOOT


class Layout:
    """Where the parts of the screen are, in the units of its menu."""

    def __init__(self, ingame):
        self.ingame = ingame
        if ingame:
            # a window under the bar of the menu of a game
            self.menu = (16, 30, 608, 436)
            self.width = 608
            self.strip_y, self.left = 0, 12
            self.list = (12, 38, 380, ROWS * ROW_H)
            self.pane = (408, 38, 188, ROWS * ROW_H)
            self.bar_y, self.bar_h = 404, 32
        else:
            self.menu = (0, 0, 640, 480)
            self.width = 640
            self.strip_y, self.left = 34, 32
            self.list = (32, 72, 384, ROWS * ROW_H)
            self.pane = (432, 72, 176, ROWS * ROW_H)
            self.bar_y, self.bar_h = 446, 34
        self.right = self.width - self.left


# ---- the pages. An entry of a page:
#   ('head', text)                              a heading
#   ('old', group[, only][, skip])              the rows of that group of the old menu, in their order. only: just
#                                               these, in this order. skip: not these. Both by cvar or name.
#   ('yesno', name, cvar, what for)             a row the old menus did not have
#   ('slider', name, cvar, default, least, most, what for)
#   ('multi', name, cvar, ((name, value), ...), what for)
#   ('multistr', name, cvar, ((name, text), ...), what for)  the same for a cvar that holds a text
#   ('edit' | 'secret', name, cvar, letters, what for)
#   ('bind', name, command, what for)
#   ('open', name, page, what for)                a button that opens a page of its own (PAGES below), not a tab
#   ('menu', name, menu, what for)                a button that opens a window of its own (a menu of the lists)
SETUP = [
    ('@MENUS_VIDEO', '@MENUS_CONFIGURE_VIDEO_SETTINGS', [
        ('head', 'Screen'),
        ('old', 'video', ('ui_r_glcustom', 'ui_r_resolution', 'ui_r_displaymode', 'ui_cl_renderer', 'ui_r_ext_multisample',
                          'r_swapinterval')),
        ('multi', 'Frame Limit', 'com_maxfps', (('60', 60), ('90', 90), ('125', 125), ('144', 144), ('165', 165), ('240', 240),
                                                ('None', 0)), 'The most frames a second the game draws.'),
        ('yesno', 'Wide Screen Layout', 'v_2dAspect',
         'Menus and HUD keep their shape on a wide window. Counts after a restart of the video.'),
        ('old', 'video2', ('r_gamma',)),
        ('multi', 'Render Scale', 'tay_renderScale', (('Half', 0.5), ('Three Quarters', 0.75), ('Full', 1), ('One and a Half', 1.5), ('Double', 2)),
         'The size the level is drawn at, the HUD stays sharp. Vulkan renderer only, counts after a video restart.'),
        ('head', 'Detail'),
        ('old', 'video', ('ui_r_lodbias', 'ui_r_picmip', 'ui_r_texturemode', 'ui_r_detailtextures',
                          'ui_r_ext_compress_textures')),
        ('old', 'video2', ('r_ext_texture_filter_anisotropic', 'r_dynamiclight')),
        ('yesno', 'Dynamic Glow', 'r_DynamicGlow', 'Lightsabers and lights glow.'),
        ('old', 'video2', ('cg_marks', 'cg_shadows', 'cg_rendertotexturefx', 'advancedvideo')),
        ('head', 'Vulkan Renderer'),
        ('yesno', 'Bloom', 'tay_bloom', 'Bright parts of the level shine over their surroundings. Vulkan renderer only.'),
        ('slider', 'Bloom Strength', 'tay_bloomIntensity', 0.6, 0, 2, 'How strongly the bright parts shine.'),
        ('slider', 'Bloom Threshold', 'tay_bloomThreshold', 0.5, 0, 0.95, 'How bright a part has to be to shine.'),
        ('head', 'Screen Effects'),
        ('yesno', 'Screen Effects', 'v_screenFx', 'Master switch for all screen effects. Only with the Vulkan renderer.'),
        ('yesno', 'Explosion Waves', 'v_fxBlast', 'Explosions send a wave through the air that bends the picture. Close by, the colours split.'),
        ('yesno', 'Ringing Ears', 'v_soundTinnitus', 'After an explosion close by the sound turns dull and your ears ring for a moment. Works with any renderer.'),
        ('yesno', 'Heat Haze', 'v_fxHeat', 'The air shimmers over jetpack flames, the flamethrower and the barrel of your own repeater or blaster after a long burst, which also smokes.'),
        ('yesno', 'Hit Feedback', 'v_fxHit', 'A red edge shows on the side a hit comes from, and the colours split for a moment. Below 30 health the colours drain and a heartbeat plays.'),
        ('yesno', 'Under Water', 'v_fxWater', 'Under water the picture sways, fades into the colour of the water and light plays on the floor. The sound is muffled. Drops run down the view after you surface.'),
        ('yesno', 'Force Looks', 'v_fxForce', 'Looks for the Force powers. Rage: dark red tunnel, veins and a quiet hum. Protect: a dome of energy. Absorb: blue streams drawn in. Speed: blur towards the middle. Push and pull: a wave through the air. Force Sight: a spirit world with mist, players as smoking ghosts and a dreamlike sound.'),
        ('yesno', 'Saber Clash and Heat', 'v_fxSaber', 'Clashing blades flash. The air around a lit blade is hot.'),
    ]),
    ('@MENUS_SOUND', '@MENUS_CONFIGURE_SOUND_SETTINGS', [
        ('head', 'Volume'),
        ('old', 'sound', ('s_volume', 's_musicvolume', 's_volumevoice')),
        ('yesno', 'Sound in the Background', 'v_soundUnfocused',
         'The game goes on playing its sound while it is not in front or is minimized.'),
        ('head', '3D Sound'),
        ('old', 'sound', ('v_spatialquality', 'v_spatialhrtf', 'v_spatialreverb')),
        ('yesno', 'Room Reflections', 'v_spatialReflections', 'The room around you rings.'),
        ('multi', 'Echo', 'v_spatialEcho', (('Real Time', 0), ('Precalculated', 1)),
         'Real time: worked out as you go, the loudest sounds echo on their own. Precalculated: worked out once per level and kept, steady while you move, all sounds ring with the room you are in.'),
        ('multi', 'Echo Calculation', 'v_spatialGpu', (('Graphics Card', 1), ('CPU', 0)),
         'Graphics Card: the rays of the echo are traced on the graphics card, which leaves the CPU free. CPU: they are traced on the CPU. Where the card cannot do it, the CPU is used. Does nothing with a precalculated echo.'),
        ('multi', 'Echoes of Their Own', 'v_spatialSources', (('Off', 0), ('4', 4), ('8', 8), ('12', 12), ('16', 16)),
         'How many of the loudest sounds echo off the walls around them. More costs more time a frame.'),
        ('yesno', 'Sound Around Corners', 'v_spatialPaths', 'A sound finds its way around corners.'),
        ('yesno', 'Real Sound Travel', 'v_spatialPhysical',
         'Sounds fade with the distance as real ones do, lose their highs far away and arrive at the speed of sound.'),
        ('yesno', 'Doppler Effect', 's_doppler', 'What comes closer or goes away changes its pitch.'),
        ('head', 'Hits'),
        ('yesno', 'Hit Sounds', 'tay_hitSounds', 'A short tick when your shots or your blade hit someone.'),
        ('yesno', 'Kill Sound', 'tay_killSound', 'Two notes when you kill someone.'),
        ('head', 'Voice Chat'),
        ('old', 'voicechat'),
        ('head', 'Boombox'),
        ('yesno', 'Boomboxes', 'v_boomboxes', 'See and hear the boomboxes on a VisionJK server, which play songs of YouTube.'),
        ('slider', 'Boombox Volume', 'v_boomboxVolume', 0.6, 0, 1, 'How loud the songs of the boomboxes are for you.'),
    ]),
    ('Game', 'The game, lightsabers, duels and what players and weapons look like.', [
        ('head', 'Game'),
        ('old', 'options', None, ('cl_allowdownload', 'se_language', 's_language', 'langapply', 'v_updatechannel', 'visionlookbutton')),
        ('head', 'Language'),
        ('old', 'options', ('se_language', 's_language', 'langapply')),
        ('head', 'Lightsabers'),
        ('multi', 'Blade Look', 'v_saberStyle', (('Classic', 0), ('SFX', 1)),
         'SFX: a thin white core in a glow that is smeared over the swing. RGB styles and black stay classic.'),
        ('slider', 'Trail Length', 'v_saberTrailLength', 1, 0, 4, 'How long the trail behind a swinging blade is.'),
        ('slider', 'Blade Core', 'v_saberCoreScale', 1, 0.25, 3, 'How thick the white core of a blade is.'),
        ('slider', 'Blade Glow', 'v_saberGlowScale', 1, 0.25, 3, 'How wide the glow around a blade is.'),
        ('yesno', 'Saber Team Colours', 'tay_saberTeamColors', 'In team games every blade in the colour of its team. Off: blades keep their colour.'),
        ('yesno', 'Two-Coloured Staff', 'tay_saberStaffColors', 'The second blade of a staff in the second saber colour.'),
        ('head', 'Duels'),
        ('multi', 'Others in a Duel', 'v_duelEffect',
         (('Game', 0), ('Tinted', 1), ('Shield', 2), ('Damaged Shield', 3), ('Force Shell', 4), ('Sight Shell', 5),
          ('Half Shield', 6), ('Cloaked', 7), ('Sight Bubble', 8)),
         'How players look who are in a duel you are not part of. Their colour is v_duelColour in the console.'),
        ('yesno', 'Their Blades', 'v_duelShowSabers', 'The blades of players who are in a duel you are not part of.'),
        ('head', 'Players'),
        ('edit', 'Enemy Model', 'tay_forceEnemyModel', 32, 'Every opponent in this model, as model/skin. Empty: as they chose.'),
        ('edit', 'Ally Model', 'tay_forceAllyModel', 32, 'Every team mate in this model, as model/skin. Empty: as they chose.'),
        ('head', 'Hats and Capes'),
        ('multistr', 'Hat', 'tay_hat',
         (('None', ''), ('Afro', 'afro'), ('Beard', 'beard'), ('Bucket', 'bucket'), ('Cap', 'cap'), ('Propeller Cap', 'cringe'),
          ('Crown', 'crown'), ('Fedora', 'fedora'), ('Fedora 2', 'fedora2'), ('Fedora 3', 'fedora3'), ('Fedora 4', 'fedora4'),
          ('Glasses', 'glasses'), ('Graduation Cap', 'gradcap'), ('Headcrab', 'headcrab'), ('Horns', 'horns'),
          ('Mario Cap', 'mario'), ('Mask', 'mask'), ('Metal Helmet', 'metalhelm'), ('Plague Mask', 'plaguemask'),
          ('Predator Helmet', 'predatorhelm'), ('Pumpkin', 'pumpkin'), ('Santa Hat', 'santahat'), ('Sombrero', 'sombrero'),
          ('Super Saiyan Hair', 'supersaiyan'), ('Top Hat', 'tophat')),
         'What you wear on your head. Other players of VisionJK see it.'),
        ('multistr', 'Cape', 'tay_cape',
         (('None', ''), ('Royal Cape', 'royalcape'), ('Vader Cape', 'vadercape'), ('Yoda', 'yodacape'), ('Grogu', 'grogucape'),
          ('Goose', 'goose'), ('Crowbar', 'crowbar'), ('AK-47', 'ak47'), ('Rocket Launcher', 'rpg')),
         'What you wear on your back. Other players of VisionJK see it.'),
        ('multi', 'Show Them', 'tay_showCosmetics', (('Off', 0), ("Everybody's", 1), ('Mine on Everybody', 2)),
         'What players wear on their heads and backs. Only players of VisionJK wear them.'),
        ('yesno', 'Seasonal Hats', 'tay_seasonalCosmetics', 'A Santa hat at Christmas and a pumpkin at Halloween for those without a hat.'),
        ('head', 'Weapons'),
        ('yesno', 'Jedi Outcast Shots', 'tay_jk2Effects', 'Shots drawn as crisply as in Jedi Outcast. Counts from the next level.'),
        ('multi', 'Disruptor Beam', 'tay_disruptorBeam', (('Game', 0), ('Line', 1), ('Spiral', 2)), 'How the shot of the disruptor looks.'),
        ('yesno', 'Scope Lens', 'v_scope', 'The zoomed disruptor shows the level through a lens. Off: the flat black mask. Only with the Vulkan renderer.'),
        ('yesno', 'Zoom with Mouse Wheel', 'v_scopeWheelZoom', 'While the disruptor zoom is locked, the mouse wheel zooms in and out.'),
        ('slider', 'Motion Tracker Speed', 'v_motionThreshold', 40, 10, 200, 'The speed a player needs before the tracker of the disruptor scope picks him up.'),
        ('yesno', 'Binocular Lens', 'v_binoculars', 'The binoculars look through a lens with mouse wheel zoom, the distance and a compass. Only with the Vulkan renderer.'),
        ('yesno', 'Rocket Lock-On', 'v_lockOn', 'The lock of the rocket launcher draws brackets that close in on the target and beeps higher with every step.'),
    ]),
    ('HUD', 'What the screen shows around the level, the view and the crosshair.', [
        ('head', 'Menus and HUD'),
        ('yesno', 'New HUD', 'v_hud', 'The HUD of VisionJK. Off: the HUD of the game.'),
        ('yesno', 'Classic Menus', 'v_classic', 'The menus of the game as they were. Counts from the next start of the game.'),
        ('yesno', 'Menu in the Level', 'v_immersiveMenu', 'In a game an open menu stands in the level next to you.'),
        ('slider', 'Menu Blur', 'v_menuBlur', 1, 0, 4, 'How far the level behind an open menu is out of focus.'),
        ('head', 'View'),
        ('slider', 'Field of View', 'cg_fov', 80, 60, 130, 'How wide you see.'),
        ('slider', 'Camera Distance', 'cg_thirdPersonRange', 80, 40, 200, 'How far the camera is behind you in third person.'),
        ('yesno', 'Smooth Camera', 'cg_smoothCamera', 'The camera behind you follows the same at every frame rate. It trails a little further behind.'),
        ('head', 'Crosshair'),
        ('slider', 'Crosshair Size', 'cg_crosshairSize', 24, 12, 64, 'The size of the crosshair.'),
        ('multistr', 'Crosshair Colour', 'tay_crosshairColor',
         (('White', ''), ('Green', '0.2 1 0.3 1'), ('Cyan', '0.2 0.9 1 1'), ('Yellow', '1 0.9 0.2 1'), ('Orange', '1 0.5 0 1'),
          ('Red', '1 0.2 0.2 1'), ('Pink', '1 0.4 0.9 1')),
         'The colour of the crosshair with nothing under it.'),
        ('slider', 'Name Opacity', 'tay_crosshairNamesOpacity', 0.5, 0, 1, 'How clearly the name of the player under the crosshair shows.'),
        ('multi', 'Lead Indicator', 'tay_leadIndicator', (('Off', 0), ('On', 1), ('With Own Ping', 2)),
         'Where to aim so a shot that flies meets a moving opponent. Many servers count this as an aim aid.'),
        ('head', 'On Screen'),
        ('yesno', 'Show Frame Rate', 'cg_drawFPS', 'Frames a second in the corner of the screen.'),
        ('yesno', 'Show Timer', 'cg_drawTimer', 'The time of the level in the corner of the screen.'),
        ('yesno', 'Timer Milliseconds', 'tay_timerMsec', 'The timer of the level with milliseconds.'),
        ('yesno', 'Timer Counts Down', 'tay_timerCountdown', 'The timer shows what is left of the time limit, if there is one.'),
        ('yesno', 'Show Lagometer', 'cg_lagometer', 'A graph of your connection.'),
        ('yesno', 'Killfeed', 'tay_killfeed', 'Who killed whom with what, at the upper right.'),
        ('slider', 'Team Overlay Size', 'tay_teamOverlayScale', 1, 0.5, 2.5, 'How large the list of your team mates is.'),
        ('yesno', 'Scoreboard Clock', 'tay_scoreboardClock', 'The time of day on the scoreboard.'),
        ('yesno', 'Spectator Map', 'tay_specHud', 'While you watch, a map of the players around at the upper right.'),
        ('head', 'Movement'),
        ('yesno', 'Speedometer', 'tay_speedometer', 'Your speed under the crosshair, and the speeds of your last jumps.'),
        ('yesno', 'Strafe Helper', 'tay_strafeHelper', 'A bar under the crosshair that shows where to turn to gain speed.'),
        ('yesno', 'Snap HUD', 'tay_snapHud', 'In the air, the angles where the rounding of the game gives a little more speed.'),
        ('yesno', 'Pitch HUD', 'tay_pitchHud', 'A mark where your view would be at a chosen pitch.'),
        ('multi', 'Strafe Trails', 'tay_strafeTrail', (('Off', 0), ('Mine', 1), ('Everybody', 2)),
         'A fading line on the ground along where players ran.'),
        ('yesno', 'Movement Keys', 'tay_movementKeys', 'The keys you move with on the screen, lit while held.'),
        ('slider', 'Start Goal', 'tay_startGoal', 0, 0, 1500, 'The speedometer turns gold from this speed on. 0: off.'),
        ('slider', 'Jump Goal', 'tay_jumpGoal', 0, 0, 1500, 'Jumps at least this fast are gold in the list of jumps. 0: off.'),
    ]),
    ('Chat & Social', 'The chat box, bubbles and what stands above the heads.', [
        ('head', 'Players'),
        ('yesno', 'Chat Bubbles', 'v_chatBubbles', 'What a player says stands above his head.'),
        ('slider', 'Bubble Range', 'v_bubbleRange', 1200, 300, 3000, 'How far away a bubble is still drawn.'),
        ('yesno', 'Player Statuses', 'v_playerStatuses', 'Away from keyboard and music above the heads of the others.'),
        ('yesno', 'Show My Music', 'v_shareMusic', 'Tell the others what you listen to. Everybody on the server reads it.'),
        ('open', 'Choose Players of My Music ...', 'musicplayers',
         'Which players of music may be told: only the songs of the ones you tick are shown to the others.'),
        ('yesno', 'Select Players', 'v_selectPlayers',
         'With the mouse free, a click on a player opens a card with what you can do with him.'),
        ('yesno', 'Selection Outline', 'v_selectOutline', 'A glowing outline around the player under the mouse.'),
        ('yesno', 'Keep Scoreboard on Double Tap', 'v_scoresPick',
         'The scoreboard key pressed twice quickly keeps the board open, with the mouse free to click a player.'),
        ('head', 'Chat'),
        ('yesno', 'Own Chat Line', 'v_chatInput', 'Type into the chat box of VisionJK, with history and links.'),
        ('slider', 'Chat Box Size', 'v_chatBoxScale', 0.85, 0.6, 1.6,
         'How large the chat box and its text are. In the open box Ctrl and the mouse wheel change it too.'),
        ('slider', 'Chat Box Width', 'v_chatBoxWidth', 380, 200, 600, 'How wide the chat box is.'),
        ('slider', 'Chat Box Rows', 'v_chatBoxRows', 10, 4, 24,
         'How many lines the open chat box shows. Its upper right corner can also be dragged with the mouse.'),
        ('yesno', 'Chat Beep', 'cg_chatBeep', 'A sound when somebody says something.'),
        ('yesno', 'Chat Emoji', 'tay_chatEmoji', 'Codes like :smile: are drawn as pictures in the chat box.'),
        ('yesno', 'Chat Log', 'tay_chatLog', 'Every chat line is written into the folder chatlogs, with the time.'),
        ('multi', 'Name Alert', 'tay_notify', (('Off', 0), ('Marked', 1), ('Marked and Sound', 2)),
         'A chat line with your name is marked gold. More words with tay_notifyWords in the console.'),
        ('head', 'Notifications'),
        ('multi', 'Notifications', 'v_notify', (('Off', 0), ('Shown', 1), ('Shown and Sound', 2)),
         'Tells, your name in the chat, boombox lines and new versions slide in at the left.'),
        ('slider', 'Notification Time', 'v_notifyTime', 6000, 2000, 20000, 'How many milliseconds a notification stays.'),
        ('slider', 'Notification Size', 'v_notifyScale', 1, 0.5, 2, 'How large the notifications are.'),
        ('head', 'Console'),
        ('yesno', 'Sharp Console Font', 'con_font', 'The console in a font with every character.'),
        ('slider', 'Console Text Size', 'con_scale', 1, 0.5, 3, 'The size of the text of the console.'),
        ('slider', 'Console Opacity', 'con_opacity', 1, 0.2, 1, 'How much of the game shows through the console.'),
        ('multi', 'Console Clock', 'tay_consoleClock', (('Off', 0), ('Time', 1), ('Date and Time', 2)), 'A clock in the corner of the console.'),
        ('head', 'Clan'),
        ('secret', 'Clan Password', 'cp_clanPwd', 32,
         'Clan chat and clan radio reach everybody on the server who has the same one. The server sees it.'),
        ('head', 'Text'),
        ('yesno', 'UTF-8 Text', 'v_utf8', 'Every character in names and chat. Off for a language with a code page of its own.'),
    ]),
    ('Network', 'Updates, downloads and security.', [
        ('head', 'Updates and Downloads'),
        ('yesno', 'Check for Updates', 'v_updateCheck', 'Ask at every start whether a newer version is out.'),
        ('multistr', 'Update Channel', 'v_updateChannel', (('Stable', 'stable'), ('Development', 'dev')),
         'Stable: the releases. Development: early test builds, and the releases when they are newer.'),
        ('menu', 'Changelog', 'changelog_popmenu', 'What changed in each version, and what a new version brings.'),
        ('menu', 'Clean Up Settings', 'cvarclean_popmenu', 'Settings no code used for a month, from older versions and other mods.'),
        ('old', 'options', ('cl_allowdownload',)),
        ('yesno', 'Choose Downloads', 'v_downloadReview',
         'Before a download choose the files of the server, and see what is in them before they are used.'),
        ('yesno', 'Keep Downloads', 'v_downloadKeep', 'Keep downloaded files in the folder downloads. Off: for this session only.'),
        ('yesno', 'Web Downloads', 'v_downloadHttp',
         'Files of a server that names a web address for them come from there, much faster. Off: from the server itself.'),
        ('head', 'Connection'),
        ('multi', 'Smooth Jittery Lines', 'v_netSmooth', (('Off', 0), ('Up to 20 ms', 20), ('Up to 40 ms', 40), ('Up to 80 ms', 80)),
         'On a line whose snapshots come unevenly the game is drawn a little later, so that nothing stutters.'),
        ('head', 'Security'),
        ('yesno', 'Intrusion Log', 'v_ids',
         'Write down what a server sends that no server of the game does, and leave one that keeps at it.'),
        ('yesno', 'Key Login by Agent', 'v_authAgent', 'The admin login asks your ssh-agent for the key (1Password, OpenSSH).'),
        ('edit', 'Key File', 'v_authKeyFile', 120, 'The file with the key for the admin login, where no agent has it.'),
    ]),
    ('@MENUS_MODS', 'Game folders with other mods.', [('mods',)]),
]

CONTROLS = [
    ('@MENUS_MOVEMENT', 'The keys you move with.', [('old', 'movecontrols')]),
    ('@MENUS_INTERACTION', 'Attack, use and look.', [('old', 'attackcontrols')]),
    ('@MENUS_WEAPONS', 'The keys of the weapons.', [('old', 'weaponcontrols')]),
    ('Force Powers', 'The keys of the force powers.', [('old', 'forcecontrols'), ('old', 'forcecontrols2')]),
    ('@MENUS_MOUSE_JOYSTICK', 'Mouse and joystick.', [('old', 'joycontrols')]),
    ('@MENUS_OTHER', 'Chat, scores and gestures.', [('old', 'othercontrols')]),
    ('Extras', 'Keys for what VisionJK and JA+ add to the game.', [
        ('head', 'JA+'),
        ('old', 'japluscontrols', ('+button12', 'jetpack', 'engage_fullforceduel', 'ui_openmenu modsay')),
        ('head', 'VisionJK'),
        ('old', 'japluscontrols', ('+v_voice', '+v_voiceclan')),
        ('bind', 'Free the Mouse (Select Players)', '+v_cursor',
         'Hold to point at players with the mouse: a click opens what you can do with one.'),
        ('bind', 'Quick Wheel (Boombox)', '+v_radial',
         'Hold for a wheel: put a boombox or a sound system down, or work the one you look at. Let go on a choice.'),
        ('bind', 'Scoreboard with Mouse', 'v_scores_pick',
         'Opens the scoreboard with the mouse free: a click on a row opens what you can do with that player.'),
        ('bind', 'Command Menu', 'v_commandmenu',
         'Opens lists of what VisionJK can do, from the boombox to emotes and votes: a number key or a click picks one.'),
        ('bind', 'Tool Window (Sandbox)', 'v_tools',
         'Opens the toolgun window (after amsandbox): pick a tool and set its colour, material or properties.'),
        ('bind', 'Model Store', 'v_modelbrowser',
         'Opens the model store of jka.vision: get player models others uploaded, and use them at once.'),
        ('bind', 'Throw Snowball', '+v_snowball',
         'Hold to wind up, let go to throw: the longer you hold, the further it flies. Only players of VisionJK see it.'),
        ('bind', 'Water Gun', '+v_water',
         'Hold to spray water where you look: it splashes, leaves puddles that dry, and who is hit sees drops run down the view. Only players of VisionJK see it.'),
        ('bind', 'RC Car', 'v_rccar',
         'Puts a remote controlled droid in front of you and you drive it: W and S drive, A and D steer, Space hops, the mouse looks, Esc puts it away. Only players of VisionJK see it.'),
        ('bind', 'Photo Mode', 'v_photo',
         'The camera circles you for a picture: mouse turns, wheel moves, right button takes it, Esc leaves.'),
    ]),
]

SCREENS = {'setup.menu': SETUP, 'ingame_setup.menu': SETUP, 'controls.menu': CONTROLS, 'ingame_controls.menu': CONTROLS}

# The before and after of a setting, in the pane while its row has the focus ("descPic" of the row, codemp/ui/ui_shared.c).
# By the cvar of the row: the picture (gfx/menus/vision/setup/<picture>_a and _b), the names of the two values, and
# for tools/menus/pictures.py, which takes them in the game: what it sets for a and for b, whether that needs a restart
# of the video, the view (VIEWS of pictures.py) and the part of the frame that is kept (left, top, width as parts of
# it; the height is 9/16 of the width).
# Only settings whose difference shows at the size of the pane are here: geometric detail, the texture filters,
# detailed shaders, anti-aliasing and the render scale were taken and looked the same on both sides (2026-10-30), and
# the stencil shadows of cg_shadows 2 draw the player wrongly with the Vulkan renderer. The name of a value is a
# reference to a text or a number without quotes: the reader of the menu files takes a string with % apart, and it
# joins strings in quotes that stand next to each other into one.
PICTURE_DIR = 'gfx/menus/vision/setup'
PICTURE_STEPS = ('stairs', (0.3, 0.35, 0.55))         # close to steps that glow, where texture detail shows
PICTURE_STAIRS = ('stairs', (0.0, 0.0, 0.7))          # the stairs and the wall above them, for glow and bloom
PICTURE_ROOM = ('stairs', (0.0, 0.0, 1.0))
PICTURES = {
    'ui_r_picmip': ('textures', '@MENUS_LOW', '@MENUS_VERY_HIGH', (('r_picmip', 3, 0),), True, PICTURE_STEPS),
    'r_gamma': ('brightness', '0.8', '1.4', (('r_gamma', 0.8, 1.4),), True, PICTURE_ROOM),
    'r_dynamicglow': ('glow', '@MENUS_OFF', '@MENUS_ON', (('r_DynamicGlow', 0, 1),), True, PICTURE_STAIRS),
    'tay_bloom': ('bloom', '@MENUS_OFF', '@MENUS_ON', (('tay_bloom', 0, 1),), False, PICTURE_STAIRS),
    'tay_bloomintensity': ('bloomstrength', '0.3', '1.2', (('tay_bloom', 1, 1), ('tay_bloomIntensity', 0.3, 1.2)), False,
                           PICTURE_STAIRS),
    'tay_bloomthreshold': ('bloomthreshold', '0.8', '0.3', (('tay_bloom', 1, 1), ('tay_bloomThreshold', 0.8, 0.3)), False,
                           PICTURE_STAIRS),
}

# what a row takes over from the item of the old menu: what it is and does, not how it looked
KEEP = ('name', 'group', 'type', 'text', 'cvar', 'cvarfloat', 'cvarfloatlist', 'cvarstrlist', 'feeder', 'action', 'desctext',
        'cvartest', 'showcvar', 'hidecvar', 'enablecvar', 'disablecvar', 'maxchars', 'maxpaintchars', 'invertyesno')
# the cvars the presets of the row "3D Sound" set (uiSpatialPresets in codemp/ui/ui_main.c)
SOUND_PRESET_CVARS = ('v_spatialreflections', 'v_spatialpaths', 'v_spatialsources')

TYPES = {'yesno': 'ITEM_TYPE_YESNO', 'slider': 'ITEM_TYPE_SLIDER', 'multi': 'ITEM_TYPE_MULTI', 'multistr': 'ITEM_TYPE_MULTI',
         'edit': 'ITEM_TYPE_EDITFIELD',
         'secret': 'ITEM_TYPE_EDITFIELD', 'bind': 'ITEM_TYPE_BIND'}
SETTINGS = ('item_type_yesno', 'item_type_slider', 'item_type_multi', 'item_type_editfield', 'item_type_numericfield',
            'item_type_bind', 'item_type_button', '1', '4', '9', '10', '11', '12', '13')

# script commands that showed and marked the tabs of the old menus, with the number of their arguments
GONE_COMMANDS = {'show': 1, 'hide': 1, 'setitemcolor': 6, 'setfocus': 1}
KEPT_ITEMS = ('applychanges',)       # the button a changed video setting brings up


def items(m):
    return [e for e in m if e.k == 'itemdef']


def name_of(b):
    n = mf.get(b, 'name')
    return mf.unquote(n[0]).lower() if n else ''


def group_of(b):
    return (mf.get(b, 'group') or [''])[0].lower()


def cvar_of(b):
    c = mf.get(b, 'cvar') or mf.get(b, 'cvarfloat')
    return mf.unquote(c[0]).lower() if c else ''


def quoted(s):
    return '"%s"' % s


def word(text):
    """A text of the tables above as a menu file names it: one of the game (@MENUS_...), or one of texts.py."""
    return text if text.startswith('@') else ref(text)


def item(name, rect, *props):
    b = [Entry('name', [name]), Entry('rect', [mf.fmt(v) for v in rect]), Entry('visible', ['1'])]
    for key, *args in props:
        if key in ('action', 'cvarFloatList', 'cvarStrList'):
            b.append(Entry(key, [], list(args[0])))
        else:
            b.append(Entry(key, [str(a) for a in args]))
    return Entry('itemDef', [], b)


def fill(name, rect, color, *more):
    return item(name, rect, ('style', 'WINDOW_STYLE_FILLED'), ('backcolor', *color), ('decoration',), *more)


def panel_item(name, rect, fill_=SOFT, edge=SOFT_EDGE, radius=4, *more):
    return item(name, rect, ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *fill_), ('bordercolor', *edge), ('radius', radius),
                ('decoration',), *more)


def text_item(name, rect, text, scale, color, *more):
    return item(name, rect, ('text', text), ('font', FONT), ('textscale', mf.fmt(scale)), ('forecolor', *color), *more)


def centered(rect, scale):
    """The properties that put a text on the middle of a rectangle."""
    return (('textalign', 'ITEM_ALIGN_CENTER'), ('textalignx', mf.fmt(rect[2] / 2)),
            ('textaligny', mf.fmt((rect[3] - 18.0 * scale) / 2)))


def clean(words):
    """A script without the commands that showed, hid and marked the tabs of the old menu."""
    out, i = [], 0
    while i < len(words):
        w = words[i]
        k = w.lower().rstrip(';')
        n = GONE_COMMANDS.get(k)
        if w == mf.NL or n is None or w.endswith(';'):
            out.append(w)
            i += 1
            continue
        args = words[i + 1:i + 1 + n]
        if k in ('show', 'hide') and args and args[0].lower().rstrip(';') in KEPT_ITEMS:
            out.extend(words[i:i + 1 + n])
            i += 1 + n
            continue
        closed = bool(args) and args[-1].endswith(';')
        i += 1 + n
        if not closed and i < len(words) and words[i] == ';':
            i += 1
    while out and out[0] in (mf.NL, ';'):
        out.pop(0)
    # (the sound of a click was named without quotes in many rows of the game: the menu reader takes such a path
    # apart at its slashes, and the row made no sound and three lines in the console)
    for i, w in enumerate(out[:-1]):
        if w.lower() == 'play' and '/' in out[i + 1] and not out[i + 1].startswith('"'):
            end = ';' if out[i + 1].endswith(';') else ''
            out[i + 1] = '"%s"%s' % (out[i + 1].rstrip(';'), end)
    return out


def translated(block):
    """The texts of an item of the old menu that this tree wrote in English, as references into texts.py."""
    for key in ('text', 'desctext'):
        e = mf.find(block, key)
        if e and e.args:
            e.args[0] = texts.old(e.args[0])
    for key in ('cvarfloatlist', 'cvarstrlist'):
        e = mf.find(block, key)
        if e and isinstance(e.block, list):
            # names and values take turns
            n = 0
            for i, w in enumerate(e.block):
                if w in (mf.NL, ',', ';'):
                    continue
                if n % 2 == 0:
                    e.block[i] = texts.old(w)
                n += 1


def as_row(block, page, y, L):
    """The properties that make a setting a row of the list of a page."""
    x, _, w, _ = L.list
    for key in ('rect', 'page', 'scroll', 'row', 'font', 'textscale', 'forecolor', 'visible', 'descpic'):
        mf.drop(block, key)
    pic = PICTURES.get(cvar_of(block))
    if pic:
        block.append(Entry('descPic', [quoted('%s/%s' % (PICTURE_DIR, pic[0])), pic[1], pic[2]]))
    secret = name_of(block).startswith('vision_secret')
    block += [Entry('rect', [mf.fmt(v) for v in (x, y, w, ROW_H)]), Entry('page', [str(page)]), Entry('scroll', ['1']),
              Entry('row', ['2' if secret else '1']), Entry('font', [FONT]), Entry('textscale', [mf.fmt(ROW_SCALE)]),
              Entry('forecolor', list(TEXT)), Entry('visible', ['1'])]


def old_rows(m, group, only=None, skip=()):
    """The settings of a group of the old menu, as new items."""
    found = []
    for e in items(m):
        b = e.block
        kind = (mf.get(b, 'type') or [''])[0].lower()
        if group_of(b) != group or kind not in SETTINGS or mf.get(b, 'text') is None:
            continue
        ident = (cvar_of(b), name_of(b))
        if only is not None and not set(ident) & set(only):
            continue
        if set(ident) & set(skip):
            continue
        new = [Entry(x.key, list(x.args), list(x.block) if x.block is not None else None) for x in b if x.k in KEEP]
        act = mf.find(new, 'action')
        if act and isinstance(act.block, list):
            act.block = clean(act.block)
        translated(new)
        found.append((ident, new))
    if only is not None:
        found.sort(key=lambda f: min(only.index(i) for i in f[0] if i in only))
    return [new for _, new in found]


# placeholder for the number of a page of its own, which is known after the tabs (build)
PAGE_MARK = '@@PAGE_%s@@'


def open_row(spec):
    """A button among the rows that opens a page of its own (PAGES)."""
    _, text, page, about = spec
    return [Entry('name', ['vision_open_' + page]), Entry('type', ['ITEM_TYPE_BUTTON']), Entry('text', [ref(text)]),
            Entry('descText', [ref(about)]), Entry('action', [], list(CLICK) + ['setpage', PAGE_MARK % page, ';'])]


def music_page(page, back, L):
    """The players of music (FEEDER_MUSICPLAYERS of codemp/ui/ui_main.c): a click ticks one for v_shareMusicPlayers."""
    x, y, w, h = L.list
    rect_back = (x, y + h - 26, 110, 24)
    return [
        text_item('vision_music_title', (x + 2, y + 2, w - 4, 22), ref('Players of My Music'), 0.8, BRIGHT, ('decoration',),
                  ('page', page)),
        text_item('vision_music_about1', (x + 2, y + 30, w - 4, 16), ref('Click a player to tick it, or to take the tick away.'), 0.56,
                  TEXT, ('decoration',), ('page', page)),
        text_item('vision_music_about2', (x + 2, y + 46, w - 4, 16), ref('Only the songs of ticked players are shown to the others.'),
                  0.56, DIM, ('decoration',), ('page', page)),
        item('vision_music_list', (x, y + 70, w, h - 104), ('type', 'ITEM_TYPE_LISTBOX'), ('style', 'WINDOW_STYLE_PANEL'),
             ('elementwidth', 120), ('elementheight', 20), ('font', FONT), ('textscale', '0.59'), ('elementtype', 'LISTBOX_TEXT'),
             ('feeder', 'FEEDER_MUSICPLAYERS'), ('textalign', 3), ('textaligny', 24), ('bordercolor', *(ACCENT[:3] + ['0.16'])),
             ('outlinecolor', *(ACCENT[:3] + ['0.22'])), ('forecolor', *TEXT), ('backcolor', '0', '0', '0', '0.30'), ('radius', 4),
             ('page', page)),
        text_item('vision_music_back', rect_back, '@MENUS_BACK', 0.62, BRIGHT, ('type', 'ITEM_TYPE_BUTTON'), ('style', 'WINDOW_STYLE_PANEL'),
                  ('radius', 3), ('caps', 1), ('backcolor', *(ACCENT[:3] + ['0.20'])), ('bordercolor', *(ACCENT[:3] + ['0.75'])),
                  ('descText', '@MENUS_BACK'), ('action', list(CLICK) + ['setpage', str(back), ';']), ('page', page),
                  *centered(rect_back, 0.62)),
    ]


def menu_row(spec):
    """A button among the rows that opens a window (ui/jamp/changelog.menu and the like)."""
    _, text, menu, about = spec
    return [Entry('name', ['vision_menu_' + menu]), Entry('type', ['ITEM_TYPE_BUTTON']), Entry('text', [ref(text)]),
            Entry('descText', [ref(about)]), Entry('action', [], list(CLICK) + ['open', menu, ';'])]


def new_row(spec):
    """A setting the old menus did not have."""
    kind, text, cvar = spec[:3]
    if kind == 'open':
        return open_row(spec)
    if kind == 'menu':
        return menu_row(spec)
    b = [Entry('name', ['vision_%s_%s' % (kind, cvar.lower().replace(' ', '_').replace('+', ''))]), Entry('type', [TYPES[kind]]),
         Entry('text', [ref(text)]), Entry('descText', [ref(spec[-1])])]
    if kind == 'slider':
        b.append(Entry('cvarfloat', [quoted(cvar)] + [mf.fmt(v) for v in spec[3:6]]))
    else:
        b.append(Entry('cvar', [quoted(cvar)]))
    if kind == 'multi':
        words = []
        for name, value in spec[3]:
            words += [ref(name) if name in texts.TR else quoted(name), mf.fmt(value)]
        b.append(Entry('cvarFloatList', [], words))
    elif kind == 'multistr':
        words = []
        for name, value in spec[3]:
            words += [ref(name) if name in texts.TR else quoted(name), quoted(value)]
        b.append(Entry('cvarStrList', [], words))
    elif kind in ('edit', 'secret'):
        b += [Entry('maxChars', [str(spec[3])]), Entry('maxPaintChars', ['24'])]
    action = list(CLICK)
    if cvar.lower() in SOUND_PRESET_CVARS:
        # the row "3D Sound" says which preset the cvars are (UI_GetSoundSetup), also after this one changed
        action += ['uiScript', 'getsoundsetup', ';']
    if cvar == 'v_updateChannel':
        # the development builds only after the warning of channel_page (UI_RunMenuScript sets it back and opens it)
        action += ['uiScript', 'updateChannel', PAGE_MARK % 'updatechannel', ';']
    b.append(Entry('action', [], action))
    return b


def page_items(m, page, content, L, folds):
    """The items of one page: its rows one under the other. folds: the numbers the headings of the screen have so far,
    a click on a heading folds its rows away ("fold", Menu_ListLayout in codemp/ui/ui_shared.c)."""
    out = []
    x, y, w, h = L.list
    for entry in content:
        kind = entry[0]
        if kind == 'head':
            folds.append(len(folds) + 1)
            out.append(text_item('vision_head', (x + 2, y, w - 4, ROW_H), word(entry[1]), 0.54, DIM, ('textaligny', 9), ('caps', 1),
                                 ('type', 'ITEM_TYPE_BUTTON'), ('style', 'WINDOW_STYLE_EMPTY'), ('fold', folds[-1]),
                                 ('action', CLICK), ('page', page), ('scroll', 1)))
            out.append(fill('vision_headline', (x, y + ROW_H - 1, w, 0.67), ACCENT[:3] + ['0.32'], ('page', page), ('scroll', 1)))
            y += ROW_H
        elif kind == 'old':
            for b in old_rows(m, entry[1], entry[2] if len(entry) > 2 else None, entry[3] if len(entry) > 3 else ()):
                as_row(b, page, y, L)
                out.append(Entry('itemDef', [], b))
                y += ROW_H
        elif kind == 'mods':
            # a list of folders and a button: no rows
            lists = [e for e in items(m) if group_of(e.block) == 'mods' and (mf.get(e.block, 'type') or [''])[0] == 'ITEM_TYPE_LISTBOX']
            load = [e for e in items(m) if name_of(e.block) == 'loadmod']
            if not lists or not load:
                return []
            b = lists[0].block
            mf.put(b, 'rect', mf.fmt(x), mf.fmt(y), mf.fmt(w), mf.fmt(h - 34))
            mf.put(b, 'visible', 1)
            mf.put(b, 'page', page)
            out.append(lists[0])
            b = load[0].block
            rect = (x, y + h - 24, 130, 22)
            mf.put(b, 'rect', *[mf.fmt(v) for v in rect])
            for key, *args in (('visible', 1), ('page', page), ('font', FONT), ('textscale', '0.62'), ('caps', 1), ('forecolor', *BRIGHT),
                               ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *(ACCENT[:3] + ['0.20'])),
                               ('bordercolor', *(ACCENT[:3] + ['0.75'])), ('radius', 3)) + centered(rect, 0.62):
                mf.put(b, key, *args)
            act = mf.find(b, 'action')
            act.block = clean(act.block)
            out.append(load[0])
        else:
            b = new_row(entry)
            as_row(b, page, y, L)
            out.append(Entry('itemDef', [], b))
            y += ROW_H
    return out


def bar_button(name, x, w, text, desc, action, L, *more):
    rect = (x, L.bar_y, w, L.bar_h)
    return text_item(name, rect, text, 0.6, TEXT, ('type', 'ITEM_TYPE_BUTTON'), ('style', 'WINDOW_STYLE_EMPTY'), ('caps', 1),
                     ('descText', desc), ('action', action), *centered(rect, 0.6), *more)


def channel_page(page, back, L):
    """The warning before the development builds (the row "Update Channel" opens it). Its button sets the cvar."""
    x, y, w, _ = L.list
    rect_no, rect_yes = (x + 20, y + 170, 150, 24), (x + 190, y + 170, 170, 24)
    pill = (('type', 'ITEM_TYPE_BUTTON'), ('style', 'WINDOW_STYLE_PANEL'), ('radius', 3), ('caps', 1), ('page', page))
    lines = ('Development builds are early test versions.', 'They can crash, corrupt your configs or not work on some servers.',
             'You can switch back to Stable at any time.')
    out = [text_item('vision_channel_title', (x + 20, y + 24, w - 40, 24), '@MENUS_WARNING', 1.1, WARM, ('decoration',), ('page', page))]
    for n, line in enumerate(lines):
        out.append(text_item('vision_channel%d' % (n + 1), (x + 20, y + 62 + n * 18, w - 40, 16), ref(line), 0.7, DIM if n == 2 else TEXT,
                             ('decoration',), ('page', page)))
    out += [
        text_item('vision_channel_no', rect_no, ref('Stay on Stable'), 0.64, TEXT, *pill, ('backcolor', '1', '1', '1', '0.05'),
                  ('bordercolor', *SOFT_EDGE), ('descText', ref('Keep the releases.')),
                  ('action', list(CLICK) + ['setpage', str(back), ';']), *centered(rect_no, 0.64)),
        text_item('vision_channel_yes', rect_yes, ref('Switch to Development'), 0.64, BRIGHT, *pill,
                  ('backcolor', *(ACCENT[:3] + ['0.20'])), ('bordercolor', *(ACCENT[:3] + ['0.75'])),
                  ('descText', ref('Update to the development builds from now on.')),
                  ('action', list(CLICK) + ['setcvar', 'v_updateChannel', 'dev', ';', 'setpage', str(back), ';']),
                  *centered(rect_yes, 0.64)),
    ]
    return out


def defaults_page(m, page, L):
    """What the old menu asked before it set everything back, on a page of its own: the button at the bottom opens it."""
    yes = next((e for e in items(m) if name_of(e.block) == 'default_yes'), None)
    if not yes:
        return []
    x, y, w, _ = L.list
    act = clean(mf.find(yes.block, 'action').block)
    rect_yes, rect_no = (x + 150, y + 150, 110, 24), (x + 20, y + 150, 110, 24)
    pill = (('type', 'ITEM_TYPE_BUTTON'), ('style', 'WINDOW_STYLE_PANEL'), ('radius', 3), ('caps', 1), ('page', page))
    return [
        text_item('vision_warning', (x + 20, y + 24, w - 40, 24), '@MENUS_WARNING', 1.1, WARM, ('decoration',), ('page', page)),
        text_item('vision_warning1', (x + 20, y + 62, w - 40, 16), '@MENUS_THIS_WILL_SET_ALL_GAME', 0.7, TEXT, ('decoration',),
                  ('page', page)),
        text_item('vision_warning2', (x + 20, y + 80, w - 40, 16), '@MENUS_TO_THEIR_FACTORY_SETTINGS', 0.7, TEXT, ('decoration',),
                  ('page', page)),
        text_item('vision_warning3', (x + 20, y + 110, w - 40, 16), '@MENUS_VID_RESTART3', 0.7, DIM, ('decoration',), ('page', page)),
        text_item('default_no', rect_no, '@MENUS_NO', 0.64, TEXT, *pill, ('backcolor', '1', '1', '1', '0.05'),
                  ('bordercolor', *SOFT_EDGE), ('descText', '@MENUS_DO_NOT_RESET_DEFAULT'), ('tab', 1), ('action', CLICK),
                  *centered(rect_no, 0.64)),
        text_item('default_yes', rect_yes, '@MENUS_YES', 0.64, BRIGHT, *pill, ('backcolor', *(ACCENT[:3] + ['0.20'])),
                  ('bordercolor', *(ACCENT[:3] + ['0.75'])), ('descText', '@MENUS_USE_DEFAULT_SETTINGS'), ('action', act),
                  *centered(rect_yes, 0.64)),
    ]


def build(m, name, ingame, nav):
    """Makes the menu `m` (after the rules of theme.py) the options screen of the table that belongs to its file.
    nav: what the caller has for the top of a page outside a game (the bar with the sections)."""
    L = Layout(ingame)
    screen = SCREENS[name]
    setup = screen is SETUP
    old = list(m)
    esc = mf.find(m, 'onesc')
    back = clean(list(esc.block)) if esc and isinstance(esc.block, list) else list(CLICK)

    pages = []
    folds = []
    for title, about, content in screen:
        made = page_items(old, len(pages) + 1, content, L, folds)
        if made:
            pages.append((title, about, made))
    new = list(nav)

    # the row of tabs
    new.append(fill('vision_strip', (0, L.strip_y, L.width, 26), BAND, *((('wide', 1),) if not ingame else ())))
    new.append(fill('vision_strip_line', (0, L.strip_y + 26, L.width, 0.67), ACCENT[:3] + ['0.18'],
                    *((('wide', 1),) if not ingame else ())))
    # (as wide as their words are in the language of the player: the code moves them together, Menu_TabsLayout.
    # The word of the first one begins where the names of the rows do.)
    tab_w = min(92.0, (L.right - L.left) / len(pages))
    for n, (title, about, made) in enumerate(pages):
        rect = (L.left - 3 + n * tab_w, L.strip_y, tab_w, 26)
        act = list(CLICK)
        if title == '@MENUS_MODS':
            act += ['uiScript', 'loadMods', ';']
        new.append(text_item('vision_tab%d' % (n + 1), rect, word(title), 0.58, DIM, ('type', 'ITEM_TYPE_BUTTON'), ('caps', 1),
                             ('style', 'WINDOW_STYLE_EMPTY'), ('tab', n + 1), ('descText', word(about)), ('action', act),
                             *centered(rect, 0.58)))

    # the lists, the bar beside them, and what the row under the mouse is for
    for _, _, made in pages:
        new.extend(made)
    x, y, w, h = L.list
    new.append(item('vision_scroll', (x + w + 3, y, 6, h), ('type', 'ITEM_TYPE_PAGESCROLL')))
    px, py, pw, ph = L.pane
    new.append(panel_item('vision_pane', L.pane, SOFT, SOFT_EDGE, 4))
    top = 34 if setup else 0        # (under the search)
    new.append(text_item('vision_about', (px + 12, py + 14 + top, pw - 24, ph - 28 - top), '""', 0.6, BRIGHT, ('type', 'ITEM_TYPE_DESC'),
                         ('decoration',)))
    # the search: at the top of the pane, above what the row under the mouse is for. While it has words the list
    # shows the rows of all pages that have them (searchCvar, Menu_ListLayout)
    if setup:
        new.append(text_item('vision_search', (px + 8, py + 8, pw - 16, 20), ref('Search'), 0.56, DIM,
                             ('type', 'ITEM_TYPE_EDITFIELD'), ('hint', 1), ('cvar', quoted('v_setupSearch')), ('maxChars', 40),
                             ('maxPaintChars', 18), ('descText', ref('Shows the settings of all pages whose name or description has these words.'))))

    # the bar at the bottom: back at the left, and what else the screen has to offer
    new.append(fill('vision_foot', (0, L.bar_y, L.width, L.bar_h), BAND, *((('wide', 1),) if not ingame else ())))
    new.append(fill('vision_foot_line', (0, L.bar_y, L.width, 0.67), ACCENT[:3] + ['0.18'], *((('wide', 1),) if not ingame else ())))
    # (as wide as their words and one after the other: "flow", Menu_TabsLayout in ui_shared.c)
    bx = L.left - 3
    new.append(bar_button('vision_back', bx, 72, '@MENUS_BACK', '@MENUS_BACK', back, L, ('flow', 1)))
    made = defaults_page(old, len(pages) + 1, L) if setup else []
    if setup:
        if made:
            new.append(bar_button('vision_defaults', bx + 72, 104, '@MENUS_DEFAULTS', '@MENUS_RESTORE_DEFAULT_SETTINGS', CLICK, L,
                                  ('flow', 1), ('tab', len(pages) + 1)))
            new.extend(made)
        # a changed video setting brings up the button that restarts the video with it
        apply = next((e for e in items(old) if name_of(e.block) == 'applychanges'), None)
        if apply:
            rect = (L.right - 128, L.bar_y + 6, 128, L.bar_h - 12)
            # (it does what the "yes" of the old menu did after its question)
            yes = next((e for e in items(old) if name_of(e.block) == 'vidrestart_yes'), None)
            act = clean(list(mf.find(yes.block, 'action').block)) if yes else CLICK + ['uiScript', 'updatevideosetup', ';']
            new.append(text_item('applyChanges', rect, '@MENUS_APPLY_CHANGES', 0.6, BRIGHT, ('type', 'ITEM_TYPE_BUTTON'),
                                 ('style', 'WINDOW_STYLE_PANEL'), ('backcolor', *(ACCENT[:3] + ['0.22'])),
                                 ('bordercolor', *(ACCENT[:3] + ['0.8'])), ('radius', 3), ('caps', 1),
                                 ('descText', ref('Restart the video with these settings.')),
                                 ('action', act), *centered(rect, 0.6)))
            mf.put(new[-1].block, 'visible', 0)
    else:
        rect = (L.right - 300, L.bar_y, 300, L.bar_h)
        new.append(text_item('vision_hint', rect, ref('Enter or click: change the key. Backspace: clear it.'), 0.5, DIM,
                             ('decoration',), ('textalign', 'ITEM_ALIGN_RIGHT'), ('textalignx', 300),
                             ('textaligny', mf.fmt((L.bar_h - 9.0) / 2))))

    # the pages of their own that buttons among the rows open: after the tabs and the page of the defaults
    if setup:
        first = len(pages) + 1 + (1 if made else 0)
        chat = next(n for n, (title, _, _) in enumerate(pages) if title == 'Chat & Social') + 1
        network = next(n for n, (title, _, _) in enumerate(pages) if title == 'Network') + 1
        new.extend(music_page(first, chat, L))
        new.extend(channel_page(first + 1, network, L))
        marks = {PAGE_MARK % 'musicplayers': str(first), PAGE_MARK % 'updatechannel': str(first + 1)}
        for e in new:
            act = mf.find(e.block, 'action') if e.k == 'itemdef' else None
            if act and isinstance(act.block, list):
                act.block = [marks.get(w, w) for w in act.block]
    m[:] = [e for e in m if e.k != 'itemdef'] + new
    for key in ('onopen', 'onclose', 'onesc'):
        e = mf.find(m, key)
        if e and isinstance(e.block, list):
            e.block = clean(e.block)
    if setup:
        # The old menu read the 3D sound of the engine into its entry when the page of the sound was opened
        # (uiScript getsoundsetup in the action of that tab, which went with the tabs). Here all pages are one
        # screen, so it is read when the screen opens. Without it the entry said "Off" after every start of the
        # game, whatever the engine had, and looked as if nothing had been saved.
        e = mf.find(m, 'onopen')
        if e and isinstance(e.block, list):
            e.block = ['uiScript', 'getsoundsetup', mf.NL] + e.block
    mf.put(m, 'scrollRect', *[mf.fmt(v) for v in L.list])
    mf.put(m, 'scrollStep', ROW_H)
    if folds:
        mf.put(m, 'foldCvar', 'v_setupFolds' if setup else 'v_controlsFolds')
    if setup:
        # (Menus_Activate empties it when the screen opens: a search is over then)
        mf.put(m, 'searchCvar', 'v_setupSearch')
    if ingame:
        window_frame(m, L)


def window_frame(m, L):
    """In a game a screen is a window under the bar of the in-game menu: the menu itself is its panel."""
    mf.put(m, 'rect', *[mf.fmt(v) for v in L.menu])
    mf.put(m, 'style', 'WINDOW_STYLE_PANEL')
    mf.put(m, 'backcolor', *WINDOW)
    mf.put(m, 'bordercolor', *PANEL_EDGE)
    mf.put(m, 'radius', 6)
    mf.put(m, 'shadow', 14)
    mf.drop(m, 'border')
    mf.drop(m, 'bordersize')
    mf.drop(m, 'background')
