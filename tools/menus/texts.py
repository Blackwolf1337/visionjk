"""The texts the new look adds to the menus, in the languages of the game. tools/menus/build.py writes them as
a string package of their own (strings/<language>/vision.str), and a menu file names one as @VISION_<KEY>, the
way it names a text of the game as @MENUS_<KEY>.

A text is written here in English with its German next to it. French and Spanish read the English until
somebody translates them: their files say "#same", which is how a string package says so."""
import os
import re

from texts_more import MORE

# what an old menu file of this tree says in English, where the new menus say it better
OLD = {
    'Shadows Type': 'Shadows',
    'select the player Shadow type': 'How the shadows of the players are drawn.',
    'No Shadow': 'None',
    'Render To TextureFX': 'Render To Texture Effects',
    'Reduce some laggy effects(cloak, push blur, protected spheres ...) on some ATI video cards':
        'Off: no slow effects (cloak, push blur, protection spheres) on graphics cards that have trouble with them.',
    'Display Mode:': 'Display Mode',
    'A window, the whole screen, or a window without a border': 'A window, the whole screen, or a window without a border.',
    'Renderer:': 'Renderer',
    'Draw with Vulkan or with OpenGL': 'Draw with Vulkan or with OpenGL.',
    'Anti-Aliasing:': 'Anti-Aliasing',
    'Smooths the edges, costs speed': 'Smooths the edges, costs speed.',
    '3D Sound:': '3D Sound',
    'Steam Audio: sound that knows the level. Needs its library next to the game':
        'Steam Audio: sound that knows the level. Needs its library next to the game.',
    'Output:': 'Output',
    '3D Sound: direction for two ears, or plain panning for speakers':
        '3D Sound: direction for two ears, or plain panning for speakers.',
    'Reverb:': 'Reverb',
    '3D Sound: how loud the rooms ring': '3D Sound: how loud the rooms ring.',
    'Voice Chat:': 'Voice Chat',
    'Mode:': 'Mode',
    'Push to talk: hold the talk key (Controls, JA+).': 'Push to talk: hold the talk key (Controls, Extras).',
    'Volume:': 'Voice Chat Volume',
    'Microphone Gain:': 'Microphone Gain',
    'Activation Level:': 'Activation Level',
    'Muffle Behind Walls:': 'Muffle Behind Walls',
    'Show Who Talks:': 'Show Who Talks',
    'On Other Servers:': 'On Other Servers',
    'Own Voice In The Room:': 'Own Voice In The Room',
    'Radio Sound:': 'Radio Sound',
    'JetPack': 'Jetpack',
    'Grapple hook to throw on walls': 'A hook on a rope that pulls you to where it hits.',
    'used to turn On/off the JetPack': 'Turns the jetpack on and off.',
    'used to engage a Duel with all allowed force powers': 'Challenges to a duel with every force power that is allowed.',
    'Message Mod': 'Message Menu',
    'used to draw an interface for clan Say, private talk, amsay, ampsay...': 'A menu for clan chat, private and admin messages.',
    'hold to talk in the voice chat (push to talk)': 'Hold to talk in the voice chat (push to talk).',
    'Red: ': 'Red', 'Red ': 'Red', 'Green: ': 'Green', 'Blue: ': 'Blue', 'Blue ': 'Blue',
    'Line: ': 'Blade Style',
    'hold to talk to your clan over the radio, wherever they are': 'Hold to talk to your clan over the radio, wherever they are.',
}

# English: German
TR = {
    # pages and the parts of a page
    'Interface': 'Oberfläche',
    'How menus, HUD and camera look.': 'Wie Menüs, HUD und Kamera aussehen.',
    'Chat & Social': 'Chat & Soziales',
    'The chat box, bubbles and what stands above the heads.': 'Die Chatbox, Sprechblasen und was über den Köpfen steht.',
    'Network': 'Netzwerk',
    'Updates, downloads and security.': 'Updates, Downloads und Sicherheit.',
    'Force Powers': 'Machtkräfte',
    'The keys of the force powers.': 'Die Tasten der Machtkräfte.',
    'Extras': 'Extras',
    'Keys for what VisionJK and JA+ add to the game.': 'Tasten für das, was VisionJK und JA+ dem Spiel hinzufügen.',
    'The keys you move with.': 'Die Tasten, mit denen Sie sich bewegen.',
    'Attack, use and look.': 'Angreifen, benutzen und umsehen.',
    'The keys of the weapons.': 'Die Tasten der Waffen.',
    'Mouse and joystick.': 'Maus und Joystick.',
    'Chat, scores and gestures.': 'Chat, Punkte und Gesten.',
    'Game folders with other mods.': 'Spielordner mit anderen Mods.',
    'Screen': 'Bildschirm',
    'Detail': 'Details',
    'Volume': 'Lautstärke',
    '3D Sound': '3D-Sound',
    'Voice Chat': 'Sprach-Chat',
    'Game': 'Spiel',
    'Language': 'Sprache',
    'Menus and HUD': 'Menüs und HUD',
    'View': 'Sicht',
    'Players': 'Spieler',
    'Chat': 'Chat',
    'Console': 'Konsole',
    'Clan': 'Clan',
    'Updates and Downloads': 'Updates und Downloads',
    'Security': 'Sicherheit',
    'Text': 'Text',
    'Light Side': 'Helle Seite',
    'Dark Side': 'Dunkle Seite',
    'Both Sides': 'Beide Seiten',
    'VisionJK': 'VisionJK',
    'JA+': 'JA+',
    'Enter or click: change the key. Backspace: clear it.': 'Enter oder Klick: Taste ändern. Rücktaste: Taste löschen.',
    'Restart the video with these settings.': 'Die Grafik mit diesen Einstellungen neu starten.',

    # the pages of the player
    'Player': 'Spieler',
    'Character': 'Charakter',
    'Name, model and team colour.': 'Name, Modell und Teamfarbe.',
    'Build a character of your own.': 'Einen eigenen Charakter zusammenstellen.',
    'Hilt and blade of your lightsaber.': 'Griff und Klinge Ihres Lichtschwerts.',
    'Your force powers.': 'Ihre Machtkräfte.',
    'The list of moves.': 'Die Liste der Aktionen.',
    'Red': 'Rot',
    'Green': 'Grün',
    'Blue': 'Blau',
    'Blade Style': 'Klingenstil',
    'Fixed Color': 'Feste Farbe',
    'Chat Box Size': 'Größe der Chatbox',
    'How large the chat box and its text are. In the open box Ctrl and the mouse wheel change it too.':
        'Wie groß die Chatbox und ihr Text sind. In der offenen Box ändern es auch Strg und das Mausrad.',
    'Chat Box Rows': 'Zeilen der Chatbox',
    'How many lines the open chat box shows. Its upper right corner can also be dragged with the mouse.':
        'Wie viele Zeilen die offene Chatbox zeigt. Ihre obere rechte Ecke lässt sich auch mit der Maus ziehen.',
    'Keep Scoreboard on Double Tap': 'Punktetafel bei Doppeltipp halten',
    'The scoreboard key pressed twice quickly keeps the board open, with the mouse free to click a player.':
        'Die Taste der Punktetafel zweimal schnell gedrückt hält die Tafel offen, mit freier Maus zum Anklicken eines Spielers.',
    'Scoreboard with Mouse': 'Punktetafel mit Maus',
    'Opens the scoreboard with the mouse free: a click on a row opens what you can do with that player.':
        'Öffnet die Punktetafel mit freier Maus: Ein Klick auf eine Zeile öffnet, was Sie mit dem Spieler tun können.',
    'Holsters': 'Holster',

    # display
    'Display Mode': 'Anzeigemodus',
    'A window, the whole screen, or a window without a border.': 'Ein Fenster, der ganze Bildschirm oder ein Fenster ohne Rand.',
    'Window': 'Fenster',
    'Fullscreen': 'Vollbild',
    'Borderless': 'Randlos',
    'Renderer': 'Renderer',
    'Draw with Vulkan or with OpenGL.': 'Mit Vulkan oder mit OpenGL zeichnen.',
    'Anti-Aliasing': 'Kantenglättung',
    'Smooths the edges, costs speed.': 'Glättet die Kanten, kostet Leistung.',
    'Frame Limit': 'Bildratenlimit',
    'The most frames a second the game draws.': 'So viele Bilder pro Sekunde zeichnet das Spiel höchstens.',
    'None': 'Keins',
    'Wide Screen Layout': 'Breitbild-Layout',
    'Menus and HUD keep their shape on a wide window. Counts after a restart of the video.':
        'Menüs und HUD behalten auf einem breiten Bild ihre Form. Gilt nach einem Neustart der Grafik.',
    'Dynamic Glow': 'Dynamisches Leuchten',
    'Lightsabers and lights glow.': 'Lichtschwerter und Lichter leuchten.',
    'Shadows': 'Schatten',
    'How the shadows of the players are drawn.': 'Wie die Schatten der Spieler gezeichnet werden.',
    'Default': 'Standard',
    'More Realistic': 'Realistischer',
    'Realistic': 'Realistisch',
    'Render To Texture Effects': 'Render-to-Texture-Effekte',
    'Off: no slow effects (cloak, push blur, protection spheres) on graphics cards that have trouble with them.':
        'Aus: keine langsamen Effekte (Tarnung, Unschärfe beim Machtstoß, Schutzsphären) auf Grafikkarten, die damit Mühe haben.',

    # sound
    'Steam Audio: sound that knows the level. Needs its library next to the game.':
        'Steam Audio: Klang, der den Level kennt. Braucht seine Bibliothek neben dem Spiel.',
    'Output': 'Ausgabe',
    '3D Sound: direction for two ears, or plain panning for speakers.':
        '3D-Sound: Richtung für zwei Ohren oder einfaches Panorama für Lautsprecher.',
    'Headphones': 'Kopfhörer',
    'Speakers': 'Lautsprecher',
    'Reverb': 'Hall',
    '3D Sound: how loud the rooms ring.': '3D-Sound: wie laut die Räume nachklingen.',
    'Room Reflections': 'Raumreflexionen',
    'The room around you rings.': 'Der Raum um Sie herum klingt mit.',
    'Sound Around Corners': 'Schall um Ecken',
    'A sound finds its way around corners.': 'Ein Geräusch findet seinen Weg um Ecken.',
    'Doppler Effect': 'Doppler-Effekt',
    'Sound in the Background': 'Ton im Hintergrund',
    'The game goes on playing its sound while it is not in front or is minimized.':
        'Das Spiel spielt seinen Ton weiter, wenn es nicht im Vordergrund oder minimiert ist.',
    'What comes closer or goes away changes its pitch.': 'Was näher kommt oder sich entfernt, ändert seine Tonhöhe.',
    # what came from TaystJK (2026-10-17)
    'TaystJK': 'TaystJK',
    'What came from the TaystJK client: HUD, movement, hats and capes and more.': 'Was vom Client TaystJK kommt: HUD, Bewegung, Hüte und Umhänge und mehr.',
    'HUD': 'HUD',
    'Chat and Console': 'Chat und Konsole',
    'Show Them': 'Anzeigen',
    'Headcrab': 'Headcrab',
    'Mario Cap': 'Mario-Mütze',
    'Mask': 'Maske',
    'Predator Helmet': 'Predator-Helm',
    'Pumpkin': 'Kürbis',
    'Super Saiyan Hair': 'Super-Saiyajin-Haare',
    'Vader Cape': 'Vader-Umhang',
    'Yoda': 'Yoda',
    'Grogu': 'Grogu',
    'Goose': 'Gans',
    'Crowbar': 'Brechstange',
    'AK-47': 'AK-47',
    'Rocket Launcher': 'Raketenwerfer',
    'Hat': 'Hut',
    'Cape': 'Umhang',
    'Afro': 'Afro',
    'Beard': 'Bart',
    'Bucket': 'Eimer',
    'Cap': 'Kappe',
    'Propeller Cap': 'Propellermütze',
    'Crown': 'Krone',
    'Fedora': 'Fedora',
    'Fedora 2': 'Fedora 2',
    'Fedora 3': 'Fedora 3',
    'Fedora 4': 'Fedora 4',
    'Glasses': 'Brille',
    'Graduation Cap': 'Doktorhut',
    'Horns': 'Hörner',
    'Metal Helmet': 'Metallhelm',
    'Plague Mask': 'Pestmaske',
    'Santa Hat': 'Weihnachtsmütze',
    'Sombrero': 'Sombrero',
    'Top Hat': 'Zylinder',
    'Royal Cape': 'Königsumhang',
    'What you wear on your head. Other players of VisionJK see it.': 'Was Sie auf dem Kopf tragen. Andere Spieler von VisionJK sehen es.',
    'What you wear on your back. Other players of VisionJK see it.': 'Was Sie auf dem Rücken tragen. Andere Spieler von VisionJK sehen es.',
    'Killfeed': 'Abschussliste',
    'Who killed whom with what, at the upper right.': 'Wer wen womit besiegt hat, oben rechts.',
    'Team Overlay Size': 'Größe der Teamliste',
    'How large the list of your team mates is.': 'Wie groß die Liste Ihrer Mitspieler ist.',
    'Crosshair Colour': 'Farbe des Fadenkreuzes',
    'White': 'Weiß',
    'Cyan': 'Cyan',
    'Yellow': 'Gelb',
    'Orange': 'Orange',
    'Pink': 'Rosa',
    'The colour of the crosshair with nothing under it.': 'Die Farbe des Fadenkreuzes, wenn nichts darunter ist.',
    'Name Opacity': 'Deckkraft der Namen',
    'How clearly the name of the player under the crosshair shows.': 'Wie deutlich der Name des Spielers unter dem Fadenkreuz zu sehen ist.',
    'Movement': 'Bewegung',
    'Speedometer': 'Tacho',
    'Your speed under the crosshair, and the speeds of your last jumps.': 'Ihre Geschwindigkeit unter dem Fadenkreuz und die Ihrer letzten Sprünge.',
    'Strafe Helper': 'Strafe-Hilfe',
    'A bar under the crosshair that shows where to turn to gain speed.': 'Ein Balken unter dem Fadenkreuz, der zeigt, wohin Sie drehen müssen, um schneller zu werden.',
    'Snap HUD': 'Snap-Anzeige',
    'In the air, the angles where the rounding of the game gives a little more speed.': 'In der Luft die Winkel, in denen das Runden des Spiels etwas mehr Tempo gibt.',
    'Pitch HUD': 'Neigungsanzeige',
    'A mark where your view would be at a chosen pitch.': 'Eine Marke, wo Ihr Blick bei einer gewählten Neigung wäre.',
    'Strafe Trails': 'Laufspuren',
    'Off': 'Aus',
    'Mine': 'Meine',
    'Everybody': 'Alle',
    'A fading line on the ground along where players ran.': 'Eine verblassende Linie auf dem Boden, wo Spieler gelaufen sind.',
    'Looks': 'Aussehen',
    'Hats and Capes': 'Hüte und Umhänge',
    "Everybody's": 'Die von allen',
    'Mine on Everybody': 'Meine bei allen',
    'What players wear on their heads and backs. Only players of VisionJK wear them.': 'Was Spieler auf dem Kopf und dem Rücken tragen. Nur Spieler von VisionJK tragen sie.',
    'Seasonal Hats': 'Hüte zur Jahreszeit',
    'A Santa hat at Christmas and a pumpkin at Halloween for those without a hat.': 'Eine Weihnachtsmütze zu Weihnachten und ein Kürbis zu Halloween für alle ohne Hut.',
    'Enemy Model': 'Modell der Gegner',
    'Every opponent in this model, as model/skin. Empty: as they chose.': 'Jeder Gegner in diesem Modell, als Modell/Skin. Leer: wie gewählt.',
    'Ally Model': 'Modell der Mitspieler',
    'Every team mate in this model, as model/skin. Empty: as they chose.': 'Jeder Mitspieler in diesem Modell, als Modell/Skin. Leer: wie gewählt.',
    'Saber Team Colours': 'Teamfarben der Schwerter',
    'In team games every blade in the colour of its team. Off: blades keep their colour.': 'In Teamspielen jede Klinge in der Farbe ihres Teams. Aus: Klingen behalten ihre Farbe.',
    'Two-Coloured Staff': 'Zweifarbiger Stab',
    'The second blade of a staff in the second saber colour.': 'Die zweite Klinge eines Stabs in der zweiten Schwertfarbe.',
    'Jedi Outcast Shots': 'Schüsse wie in Jedi Outcast',
    'Shots drawn as crisply as in Jedi Outcast. Counts from the next level.': 'Schüsse so scharf gezeichnet wie in Jedi Outcast. Gilt ab dem nächsten Level.',
    'Disruptor Beam': 'Disruptor-Strahl',
    'Line': 'Linie',
    'Spiral': 'Spirale',
    'How the shot of the disruptor looks.': 'Wie der Schuss des Disruptors aussieht.',
    'Chat Emoji': 'Emojis im Chat',
    'Codes like :smile: are drawn as pictures in the chat box.': 'Codes wie :smile: werden im Chatfenster als Bilder gezeigt.',
    'Chat Log': 'Chatprotokoll',
    'Every chat line is written into the folder chatlogs, with the time.': 'Jede Chatzeile wird mit der Uhrzeit in den Ordner chatlogs geschrieben.',
    'Console Clock': 'Uhr in der Konsole',
    'Time': 'Uhrzeit',
    'Date and Time': 'Datum und Uhrzeit',
    'A clock in the corner of the console.': 'Eine Uhr in der Ecke der Konsole.',
    'Scoreboard Clock': 'Uhr auf der Punktetafel',
    'The time of day on the scoreboard.': 'Die Uhrzeit auf der Punktetafel.',
    'Timer Milliseconds': 'Timer mit Millisekunden',
    'The timer of the level with milliseconds.': 'Der Timer des Levels mit Millisekunden.',
    'Timer Counts Down': 'Timer zählt rückwärts',
    'The timer shows what is left of the time limit, if there is one.': 'Der Timer zeigt, was vom Zeitlimit übrig ist, falls es eines gibt.',
    'Hit Sounds': 'Treffergeräusch',
    'A short tick when your shots or your blade hit someone.': 'Ein kurzes Ticken, wenn deine Schüsse oder deine Klinge jemanden treffen.',
    'Kill Sound': 'Abschussgeräusch',
    'Two notes when you kill someone.': 'Zwei Töne, wenn du jemanden besiegst.',
    'Movement Keys': 'Bewegungstasten',
    'The keys you move with on the screen, lit while held.': 'Die Tasten, mit denen du dich bewegst, auf dem Bildschirm, hell solange sie gedrückt sind.',
    'Start Goal': 'Startziel',
    'The speedometer turns gold from this speed on. 0: off.': 'Der Tacho wird ab dieser Geschwindigkeit golden. 0: aus.',
    'Jump Goal': 'Sprungziel',
    'Jumps at least this fast are gold in the list of jumps. 0: off.': 'Sprünge, die mindestens so schnell sind, stehen golden in der Liste der Sprünge. 0: aus.',
    'Smooth Camera': 'Gleichmäßige Kamera',
    'Vulkan Renderer': 'Vulkan-Renderer',
    'Spectator Map': 'Karte für Zuschauer',
    'While you watch, a map of the players around at the upper right.': 'Beim Zuschauen eine Karte der Spieler in der Nähe oben rechts.',
    'Lead Indicator': 'Vorhalteanzeige',
    'With Own Ping': 'Mit eigenem Ping',
    'Where to aim so a shot that flies meets a moving opponent. Many servers count this as an aim aid.': 'Wohin du zielen musst, damit ein fliegender Schuss einen Gegner in Bewegung trifft. Viele Server zählen das als Zielhilfe.',
    'The camera behind you follows the same at every frame rate. It trails a little further behind.': 'Die Kamera hinter dir folgt bei jeder Bildrate gleich. Sie hängt etwas weiter zurück.',
    'Bloom': 'Bloom',
    'Bright parts of the level shine over their surroundings. Vulkan renderer only.': 'Helle Stellen des Levels strahlen über ihre Umgebung. Nur mit dem Vulkan-Renderer.',
    'Bloom Strength': 'Stärke des Blooms',
    'How strongly the bright parts shine.': 'Wie stark die hellen Stellen strahlen.',
    'Bloom Threshold': 'Schwelle des Blooms',
    'How bright a part has to be to shine.': 'Wie hell eine Stelle sein muss, um zu strahlen.',
    'Render Scale': 'Renderauflösung',
    'Half': 'Halb',
    'Three Quarters': 'Drei Viertel',
    'Full': 'Voll',
    'One and a Half': 'Anderthalbfach',
    'Double': 'Doppelt',
    'The size the level is drawn at, the HUD stays sharp. Vulkan renderer only, counts after a video restart.': 'Die Größe, in der das Level gezeichnet wird, das HUD bleibt scharf. Nur mit dem Vulkan-Renderer, gilt nach einem Neustart des Videos.',
    'Name Alert': 'Namensalarm',
    'Marked': 'Markiert',
    'Marked and Sound': 'Markiert mit Ton',
    'A chat line with your name is marked gold. More words with tay_notifyWords in the console.': 'Eine Chatzeile mit deinem Namen wird golden markiert. Weitere Wörter mit tay_notifyWords in der Konsole.',
    'Echoes of Their Own': 'Eigenes Echo',
    'How many of the loudest sounds echo off the walls around them. More costs more time a frame.':
        'Wie viele der lautesten Geräusche von den Wänden um sie herum widerhallen. Mehr kostet mehr Zeit pro Bild.',
    'Real Sound Travel': 'Echte Schallausbreitung',
    'Sounds fade with the distance as real ones do, lose their highs far away and arrive at the speed of sound.':
        'Geräusche werden mit der Entfernung leiser wie echte, verlieren in der Ferne ihre Höhen und kommen mit Schallgeschwindigkeit an.',
    'Talk to and hear other players with your microphone.': 'Mit dem Mikrofon mit anderen Spielern sprechen und sie hören.',
    'Mode': 'Modus',
    'Push to talk: hold the talk key (Controls, Extras).': 'Push-to-Talk: die Sprechtaste halten (Steuerung, Extras).',
    'Push To Talk': 'Push-to-Talk',
    'Voice Activation': 'Sprachaktivierung',
    'Voice Chat Volume': 'Lautstärke Sprach-Chat',
    'Boombox': 'Boombox',
    'Boomboxes': 'Boomboxen',
    'See and hear the boomboxes on a VisionJK server, which play songs of YouTube.':
        'Die Boomboxen auf einem VisionJK-Server sehen und hören, die Lieder von YouTube spielen.',
    'Boombox Volume': 'Lautstärke Boombox',
    'How loud the songs of the boomboxes are for you.': 'Wie laut die Lieder der Boomboxen für Sie sind.',
    'How loud the other players are.': 'Wie laut die anderen Spieler sind.',
    'Microphone Gain': 'Mikrofonverstärkung',
    'How loud your microphone is sent.': 'Wie laut Ihr Mikrofon gesendet wird.',
    'Activation Level': 'Aktivierungspegel',
    'Voice activation: how loud you have to be before you are sent.':
        'Sprachaktivierung: wie laut Sie sein müssen, bevor gesendet wird.',
    'Muffle Behind Walls': 'Hinter Wänden dämpfen',
    'Voices behind walls and under water sound muffled.': 'Stimmen hinter Wänden und unter Wasser klingen gedämpft.',
    'Show Who Talks': 'Sprecher anzeigen',
    'A list of the players who are talking on the HUD.': 'Eine Liste der Spieler, die gerade sprechen, im HUD.',
    'On Other Servers': 'Auf anderen Servern',
    'Servers without voice: use master.jka.vision, which sees your address.':
        'Server ohne Sprach-Chat: master.jka.vision nutzen, das Ihre Adresse sieht.',
    'Own Voice In The Room': 'Eigene Stimme im Raum',
    'With spatial sound: how loud the room rings from what you say yourself.':
        'Mit 3D-Sound: wie laut der Raum von dem nachklingt, was Sie selbst sagen.',
    'Radio Sound': 'Funkklang',
    'The clan radio sounds like one: narrow, a little rough, with a click.':
        'Der Clan-Funk klingt wie Funk: schmal, etwas rau, mit einem Klick.',

    # game
    'Initiate': 'Anwärter',
    'Padawan': 'Padawan',
    'Jedi': 'Jedi',
    'Jedi Knight': 'Jedi-Ritter',
    'Jedi Master': 'Jedi-Meister',

    # interface
    'New HUD': 'Neues HUD',
    'The HUD of VisionJK. Off: the HUD of the game.': 'Das HUD von VisionJK. Aus: das HUD des Spiels.',
    'Classic Menus': 'Klassische Menüs',
    'The menus of the game as they were. Counts from the next start of the game.':
        'Die Menüs des Spiels, wie sie waren. Gilt ab dem nächsten Start des Spiels.',
    'Menu in the Level': 'Menü im Level',
    'In a game an open menu stands in the level next to you.': 'Im Spiel steht ein offenes Menü im Level neben Ihnen.',
    'Menu Blur': 'Menü-Unschärfe',
    'How far the level behind an open menu is out of focus.': 'Wie unscharf der Level hinter einem offenen Menü ist.',
    'Field of View': 'Sichtfeld',
    'How wide you see.': 'Wie weit Ihr Blick reicht.',
    'Camera Distance': 'Kameraabstand',
    'How far the camera is behind you in third person.': 'Wie weit die Kamera in der Außenansicht hinter Ihnen ist.',
    'Crosshair Size': 'Fadenkreuzgröße',
    'The size of the crosshair.': 'Die Größe des Fadenkreuzes.',
    'Show Frame Rate': 'Bildrate anzeigen',
    'Frames a second in the corner of the screen.': 'Bilder pro Sekunde in der Ecke des Bildschirms.',
    'Show Timer': 'Timer anzeigen',
    'The time of the level in the corner of the screen.': 'Die Zeit des Levels in der Ecke des Bildschirms.',
    'Show Lagometer': 'Lagometer anzeigen',
    'A graph of your connection.': 'Ein Diagramm Ihrer Verbindung.',

    # chat and social
    'Chat Bubbles': 'Sprechblasen',
    'What a player says stands above his head.': 'Was ein Spieler sagt, steht über seinem Kopf.',
    'Bubble Range': 'Reichweite der Sprechblasen',
    'How far away a bubble is still drawn.': 'Bis zu welcher Entfernung eine Sprechblase gezeichnet wird.',
    'Player Statuses': 'Spielerstatus',
    'Away from keyboard and music above the heads of the others.': 'Abwesenheit und Musik über den Köpfen der anderen.',
    'Show My Music': 'Meine Musik zeigen',
    'Tell the others what you listen to. Everybody on the server reads it.':
        'Den anderen zeigen, was Sie hören. Jeder auf dem Server liest es.',
    'Select Players': 'Spieler auswählen',
    'With the mouse free, a click on a player opens a card with what you can do with him.':
        'Mit freier Maus öffnet ein Klick auf einen Spieler eine Karte mit dem, was Sie mit ihm tun können.',
    'Selection Outline': 'Auswahl-Umriss',
    'A glowing outline around the player under the mouse.': 'Ein leuchtender Umriss um den Spieler unter der Maus.',
    'Own Chat Line': 'Eigene Chatzeile',
    'Type into the chat box of VisionJK, with history and links.': 'In die Chatbox von VisionJK tippen, mit Verlauf und Links.',
    'Chat Box Width': 'Breite der Chatbox',
    'How wide the chat box is.': 'Wie breit die Chatbox ist.',
    'Chat Beep': 'Chat-Ton',
    'A sound when somebody says something.': 'Ein Ton, wenn jemand etwas sagt.',
    'Sharp Console Font': 'Scharfe Konsolenschrift',
    'The console in a font with every character.': 'Die Konsole in einer Schrift mit allen Zeichen.',
    'Console Text Size': 'Textgröße der Konsole',
    'The size of the text of the console.': 'Die Größe des Textes der Konsole.',
    'Console Opacity': 'Deckkraft der Konsole',
    'How much of the game shows through the console.': 'Wie viel vom Spiel durch die Konsole scheint.',
    'Clan Password': 'Clan-Passwort',
    'Clan chat and clan radio reach everybody on the server who has the same one. The server sees it.':
        'Clan-Chat und Clan-Funk erreichen jeden auf dem Server, der dasselbe hat. Der Server sieht es.',

    # network
    'Check for Updates': 'Nach Updates suchen',
    'Ask at every start whether a newer version is out.': 'Bei jedem Start fragen, ob es eine neuere Version gibt.',
    'Choose Downloads': 'Downloads auswählen',
    'Before a download choose the files of the server, and see what is in them before they are used.':
        'Vor einem Download die Dateien des Servers auswählen und ihren Inhalt sehen, bevor sie benutzt werden.',
    'Keep Downloads': 'Downloads behalten',
    'Keep downloaded files in the folder downloads. Off: for this session only.':
        'Heruntergeladene Dateien im Ordner downloads behalten. Aus: nur für diese Sitzung.',
    'Connection': 'Verbindung',
    'Smooth Jittery Lines': 'Unruhige Leitungen glätten',
    'Up to 20 ms': 'Bis 20 ms',
    'Up to 40 ms': 'Bis 40 ms',
    'Up to 80 ms': 'Bis 80 ms',
    'On a line whose snapshots come unevenly the game is drawn a little later, so that nothing stutters.':
        'Auf einer Leitung, deren Snapshots ungleichmäßig kommen, wird das Spiel etwas später gezeichnet, damit nichts ruckelt.',
    'Intrusion Log': 'Einbruchsprotokoll',
    'Write down what a server sends that no server of the game does, and leave one that keeps at it.':
        'Aufschreiben, was ein Server sendet, das kein Server des Spiels sendet, und einen verlassen, der nicht damit aufhört.',
    'Key Login by Agent': 'Schlüssel-Login per Agent',
    'The admin login asks your ssh-agent for the key (1Password, OpenSSH).':
        'Der Admin-Login fragt Ihren ssh-agent nach dem Schlüssel (1Password, OpenSSH).',
    'Key File': 'Schlüsseldatei',
    'The file with the key for the admin login, where no agent has it.':
        'Die Datei mit dem Schlüssel für den Admin-Login, wenn kein Agent ihn hat.',
    'UTF-8 Text': 'UTF-8-Text',
    'Every character in names and chat. Off for a language with a code page of its own.':
        'Alle Zeichen in Namen und Chat. Aus für eine Sprache mit eigener Codepage.',

    # controls
    'Grapple Hook': 'Enterhaken',
    'A hook on a rope that pulls you to where it hits.': 'Ein Haken am Seil, der Sie dorthin zieht, wo er trifft.',
    'Jetpack': 'Jetpack',
    'Turns the jetpack on and off.': 'Schaltet das Jetpack an und aus.',
    'Full Force Duel': 'Duell mit allen Kräften',
    'Challenges to a duel with every force power that is allowed.': 'Fordert zu einem Duell mit allen erlaubten Machtkräften heraus.',
    'Message Menu': 'Nachrichtenmenü',
    'A menu for clan chat, private and admin messages.': 'Ein Menü für Clan-Chat, private und Admin-Nachrichten.',
    'Voice Chat (Talk)': 'Sprach-Chat (Sprechen)',
    'Hold to talk in the voice chat (push to talk).': 'Halten, um im Sprach-Chat zu sprechen (Push-to-Talk).',
    'Clan Radio (Talk)': 'Clan-Funk (Sprechen)',
    'Hold to talk to your clan over the radio, wherever they are.': 'Halten, um über Funk mit Ihrem Clan zu sprechen, wo immer er ist.',
    'Free the Mouse (Select Players)': 'Maus freigeben (Spieler auswählen)',
    'Hold to point at players with the mouse: a click opens what you can do with one.':
        'Halten, um mit der Maus auf Spieler zu zeigen: Ein Klick öffnet, was Sie mit einem tun können.',
}

# What the cgame says by itself: the card of a player, the scoreboard, the chat box, what stands above a head.
# It asks for a text by its English (CG_VisionText() of cgame/hud/cg_hud.c makes the key from it as key_of() does
# here), so none of these may need a number behind its key.
CGAME = {
    # the card of a player
    'Mute his voice': 'Stimme stummschalten',
    'Hear his voice again': 'Stimme wieder hören',
    'Voice quieter (%i %%)': 'Stimme leiser (%i %%)',
    'Voice louder (%i %%)': 'Stimme lauter (%i %%)',
    'Hide his chat lines': 'Chatzeilen ausblenden',
    'Show his chat lines again': 'Chatzeilen wieder zeigen',
    'Whisper to him': 'Ihm flüstern',
    'Watch him': 'Ihm zuschauen',
    'Copy his name': 'Namen kopieren',
    'Call a vote to kick him': 'Abstimmen: hinauswerfen',
    'Click again to call the vote': 'Nochmal klicken: abstimmen',
    'Admin: kick': 'Admin: hinauswerfen',
    'Admin: silence': 'Admin: stummschalten',
    'Admin: sleep': 'Admin: schlafen legen',
    'Admin: slap': 'Admin: Ohrfeige',
    'Admin: go to him': 'Admin: zu ihm gehen',
    'Admin: bring him here': 'Admin: herholen',
    'You': 'Sie',
    'Player': 'Spieler',
    'Bot': 'Bot',
    'Spectator': 'Zuschauer',
    'Score %i': 'Punkte %i',
    'Ping %i': 'Ping %i',
    '%i min': '%i Min.',
    'Another client': 'Anderer Client',
    # what stands above a head
    'Away from keyboard': 'Nicht an der Tastatur',
    'Listening to %s': 'Hört %s',
    # the scoreboard
    'Spectators': 'Zuschauer',
    'connecting': 'verbindet',
    'and %i more': 'und %i weitere',
    'The scores key': 'Die Punktetaste',
    '%s twice: pick a player': '%s zweimal: Spieler auswählen',
    'Click a player. %s or Esc closes.': 'Spieler anklicken. %s oder Esc schließt.',
    # the classic Setup menu (ui/jamp/setup.menu), which is written by hand: its way back
    'New Menus': 'Neue Menüs',
    'The menus and the HUD of VisionJK. The video restarts.': 'Die Menüs und das HUD von VisionJK. Das Video startet neu.',
    # who talks ("You" is above)
    'You, clan radio': 'Sie, Clan-Funk',
    'radio': 'Funk',
    # the chat box
    'All': 'Alle',
    'Team': 'Team',
    'Private': 'Privat',
    'Say': 'Sagen',
    'To %s': 'An %s',
    'Open %s ? Enter: yes, any other key: no': '%s öffnen? Enter: ja, jede andere Taste: nein',
    # the boombox (cgame/boombox/cg_boombox.c): its card, the line over it, the chat box asking for a song
    'Play a song ...': 'Lied abspielen ...',
    'Add a song ...': 'Lied hinzufügen ...',
    'Pause': 'Pause',
    'Resume': 'Weiter',
    'Skip this song': 'Lied überspringen',
    'Stop the music': 'Musik stoppen',
    'Quieter for everybody (%i %%)': 'Leiser für alle (%i %%)',
    'Louder for everybody (%i %%)': 'Lauter für alle (%i %%)',
    'Mute it for me': 'Für mich stumm',
    'Hear it again': 'Wieder hören',
    'Move it': 'Verschieben',
    'Take it away': 'Wegnehmen',
    'Click again to take it away': 'Nochmal klicken: wegnehmen',
    'Plays': 'Spielt',
    'Paused': 'Pausiert',
    'Loading ...': 'Lädt ...',
    'Silent': 'Still',
    'Next: %s': 'Danach: %s',
    'Looking a song up ...': 'Sucht ein Lied ...',
    'Volume %i %%': 'Lautstärke %i %%',
    'muted for you': 'für Sie stumm',
    'Boombox, link or song': 'Boombox, Link oder Lied',
    'Put down by %s': 'Aufgestellt von %s',
    'Asked for by %s': 'Gewünscht von %s',
    # the window of a box (cgame/boombox/cg_boombox_ui.c)
    'Open the player': 'Player öffnen',
    'Nothing plays': 'Es läuft nichts',
    'Skip': 'Überspringen',
    'Stop': 'Stopp',
    'In the line: %s': 'Warteschlange: %s',
    'In the line': 'Warteschlange',
    'Nothing waits': 'Nichts wartet',
    'Mute': 'Stumm',
    'Unmute': 'Wieder hören',
    # the windows of a download (client/cl_download.cpp): the engine asks for these by the key texts.py makes
    'Files of the server': 'Dateien des Servers',
    'The server offers these files to download.': 'Der Server bietet diese Dateien zum Herunterladen an.',
    'Before anything of them is used you see what is inside. Programs stay blocked.': 'Bevor etwas davon benutzt wird, sehen Sie den Inhalt. Programme bleiben gesperrt.',
    'Keep them (downloads folder)': 'Behalten (Ordner downloads)',
    'This session only': 'Nur für diese Sitzung',
    'A pure server may not let you play without its files.': 'Ein Server mit sv_pure lässt Sie ohne seine Dateien eventuell nicht spielen.',
    'Download': 'Herunterladen',
    'Join without': 'Ohne beitreten',
    'Disconnect': 'Trennen',
    'What is inside': 'Inhalt',
    'Untick what you do not want: nothing of it is used.': 'Entfernen Sie den Haken bei allem, was Sie nicht wollen: davon wird nichts benutzt.',
    'Programs could run on your computer: they are blocked unless you tick them.': 'Programme könnten auf Ihrem Computer laufen: sie bleiben gesperrt, außer Sie setzen den Haken.',
    'Maps': 'Karten',
    'Models': 'Modelle',
    'Textures and effects': 'Texturen und Effekte',
    'Sounds and music': 'Klänge und Musik',
    'Shaders and scripts': 'Shader und Skripte',
    'Menus and HUD': 'Menüs und HUD',
    'Configs (they can run commands)': 'Konfigurationen (sie können Befehle ausführen)',
    'Other files': 'Andere Dateien',
    'Programs (they could run on your computer)': 'Programme (sie könnten auf Ihrem Computer laufen)',
    'Use them': 'Verwenden',
    # the HUD (ui/vision/hud.menu, written by hand: @VISION_SABER, @VISION_ALTERNATE_DIMENSION)
    'Saber': 'Schwert',
    'Alternate Dimension': 'Andere Dimension',
}

# names that are the same in every language
SAME = {'2x', '4x', '8x', 'Vulkan', 'OpenGL', 'Español'}

LANGUAGES = (('English', None), ('German', 'LANG_GERMAN'), ('French', 'LANG_FRENCH'), ('Spanish', 'LANG_SPANISH'))

used = {}           # English: key, in the order the menus asked for them
missing = set()     # what an old menu says that has no entry here


def key_of(english):
    key = re.sub(r'[^A-Z0-9]+', '_', english.upper()).strip('_')[:36].strip('_')
    taken = set(used.values())
    n, out = 2, key
    while out in taken:
        out = '%s_%d' % (key, n)
        n += 1
    return out


def ref(english):
    """The word a menu file names this text with."""
    if english not in TR:
        raise KeyError('no text for %r in tools/menus/texts.py' % english)
    if english not in used:
        used[english] = key_of(english)
    return '@VISION_' + used[english]


def old(word):
    """A word of an old menu file (a quoted text, or a reference into the texts of the game): the reference of the
    text of this file that stands for it, or the word as it is."""
    if not word or word[0] != '"':
        return word
    english = word[1:-1]
    english = OLD.get(english, english)
    if english in TR:
        return ref(english)
    if english.strip() and english not in SAME:
        missing.add(english)
    return word


def write(strings_dir):
    """The string packages, one per language."""
    for english, german in CGAME.items():
        TR.setdefault(english, german)
    # what the two other languages say has to be a text of this file, with the same values in it, in letters
    # the string files can hold, and without a quote
    for english, more in MORE.items():
        if english not in TR:
            raise KeyError('texts_more.py has %r, which is no text of texts.py' % english)
        for text in more:
            text.encode('cp1252')
            if re.findall(r'%[is%]', text) != re.findall(r'%[is%]', english) or '"' in text:
                raise ValueError('texts_more.py: %r does not have the values of %r' % (text, english))
        plain = re.sub(r'[^A-Z0-9]+', '_', english.upper()).strip('_')[:36].strip('_')
        if ref(english) != '@VISION_' + plain:
            raise KeyError('the cgame can not find %r: another text has its key %s' % (english, plain))
    for folder, lang in LANGUAGES:
        lines = ['// The texts the menus of VisionJK add to the game. Made by tools/menus/build.py from tools/menus/texts.py:',
                 '// change that file, not this one.', '//',
                 'VERSION             "1"', 'CONFIG              "W:\\bin\\stringed.cfg"',
                 'FILENOTES           "Menus of VisionJK"', '']
        for english, key in used.items():
            lines += ['REFERENCE           ' + key, 'LANG_ENGLISH        "%s"' % english]
            if lang == 'LANG_GERMAN':
                lines.append('%-19s "%s"' % (lang, TR[english] if TR[english] != english else '#same'))
            elif lang:
                # (French and Spanish: tools/menus/texts_more.py. What is not in there reads English)
                text = MORE.get(english, (english, english))[0 if lang == 'LANG_FRENCH' else 1]
                lines.append('%-19s "%s"' % (lang, text if text != english else '#same'))
            lines.append('')
        lines += ['', 'ENDMARKER', '', '']
        os.makedirs(os.path.join(strings_dir, folder), exist_ok=True)
        with open(os.path.join(strings_dir, folder, 'vision.str'), 'w', encoding='cp1252', newline='\r\n') as f:
            f.write('\n'.join(lines))
