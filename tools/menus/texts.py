"""The texts the new look adds to the menus, in the languages of the game. tools/menus/build.py writes them as
a string package of their own (strings/<language>/vision.str), and a menu file names one as @VISION_<KEY>, the
way it names a text of the game as @MENUS_<KEY>.

A text is written here in English with its German next to it, French and Spanish are in texts_more.py. A text
without a translation reads English in that language: its file says "#same", which is how a string package says
so. The English is also the key a menu file or the cgame (CG_VisionText) finds a text by."""
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
    'How menus, HUD and camera look.': 'Aussehen von Menüs, HUD und Kamera.',
    'The game, lightsabers, duels and what players and weapons look like.': 'Das Spiel, Lichtschwerter, Duelle und das Aussehen von Spielern und Waffen.',
    'What the screen shows around the level, the view and the crosshair.': 'Was der Bildschirm um den Level herum zeigt, die Sicht und das Fadenkreuz.',
    'Chat & Social': 'Chat & Community',
    'The chat box, bubbles and what stands above the heads.': 'Chatfenster, Sprechblasen und Anzeigen über den Köpfen.',
    'Network': 'Netzwerk',
    'Updates, downloads and security.': 'Updates, Downloads und Sicherheit.',
    'Force Powers': 'Machtkräfte',
    'The keys of the force powers.': 'Tasten für die Machtkräfte.',
    'Extras': 'Extras',
    'Keys for what VisionJK and JA+ add to the game.': 'Tasten für die Extras von VisionJK und JA+.',
    'The keys you move with.': 'Tasten für die Bewegung.',
    'Attack, use and look.': 'Angreifen, benutzen und umsehen.',
    'The keys of the weapons.': 'Tasten für die Waffen.',
    'Mouse and joystick.': 'Maus und Joystick.',
    'Chat, scores and gestures.': 'Chat, Punktestand und Gesten.',
    'Game folders with other mods.': 'Spielordner mit anderen Mods.',
    'Screen': 'Bildschirm',
    'Detail': 'Details',
    'Volume': 'Lautstärke',
    '3D Sound': '3D-Sound',
    'Voice Chat': 'Sprachchat',
    'Game': 'Spiel',
    'Language': 'Sprache',
    'Menus and HUD': 'Menüs und HUD',
    'View': 'Sicht',
    'Players': 'Spieler',
    'Crosshair': 'Fadenkreuz',
    'Hits': 'Treffer',
    'On Screen': 'Auf dem Bildschirm',
    'Weapons': 'Waffen',
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
    'Enter or click: change the key. Backspace: clear it.': 'Enter oder Klick: Taste ändern. Rücktaste: Zuweisung löschen.',
    'Restart the video with these settings.': 'Grafik mit diesen Einstellungen neu starten.',

    # the pages of the player
    'Player': 'Spieler',
    'Character': 'Charakter',
    'Name, model and team colour.': 'Name, Modell und Teamfarbe.',
    'Build a character of your own.': 'Eigenen Charakter erstellen.',
    'Hilt and blade of your lightsaber.': 'Griff und Klinge deines Lichtschwerts.',
    'Your force powers.': 'Deine Machtkräfte.',
    'The list of moves.': 'Liste der Bewegungen.',
    'Red': 'Rot',
    'Green': 'Grün',
    'Blue': 'Blau',
    'Blade Style': 'Klingenstil',
    'Fixed Color': 'Feste Farbe',
    'Chat Box Size': 'Chatgröße',
    'How large the chat box and its text are. In the open box Ctrl and the mouse wheel change it too.':
        'Wie groß der Chat und sein Text sind. Im geöffneten Chat ändern auch Strg und das Mausrad die Größe.',
    'Chat Box Rows': 'Chatzeilen',
    'How many lines the open chat box shows. Its upper right corner can also be dragged with the mouse.':
        'Wie viele Zeilen der geöffnete Chat zeigt. Du kannst auch die obere rechte Ecke mit der Maus ziehen.',
    'Keep Scoreboard on Double Tap': 'Scoreboard bei Doppeltipp offen halten',
    'The scoreboard key pressed twice quickly keeps the board open, with the mouse free to click a player.':
        'Zweimal schnell die Scoreboard-Taste drücken hält es offen. So kannst du mit der Maus einen Spieler anklicken.',
    'Scoreboard with Mouse': 'Scoreboard mit Maus',
    'Opens the scoreboard with the mouse free: a click on a row opens what you can do with that player.':
        'Öffnet das Scoreboard mit freier Maus: Ein Klick auf eine Zeile zeigt, was du mit dem Spieler tun kannst.',
    'Photo': 'Foto',
    'Photo Mode': 'Fotomodus',
    'The camera circles you for a picture: mouse turns, wheel moves, right button takes it, Esc leaves.':
        'Die Kamera kreist für ein Foto um dich: Maus dreht, Rad verschiebt, rechte Taste knipst, Esc beendet.',
    'Holsters': 'Holster',

    # display
    'Display Mode': 'Anzeigemodus',
    'A window, the whole screen, or a window without a border.': 'Fenster, Vollbild oder Fenster ohne Rahmen.',
    'Window': 'Fenster',
    'Fullscreen': 'Vollbild',
    'Borderless': 'Rahmenlos',
    'Renderer': 'Renderer',
    'Draw with Vulkan or with OpenGL.': 'Mit Vulkan oder OpenGL rendern.',
    'Anti-Aliasing': 'Kantenglättung',
    'Smooths the edges, costs speed.': 'Glättet Kanten, kostet Leistung.',
    'Frame Limit': 'FPS-Limit',
    'The most frames a second the game draws.': 'Maximale Bilder pro Sekunde.',
    'None': 'Keine',
    'Wide Screen Layout': 'Breitbild-Layout',
    'Menus and HUD keep their shape on a wide window. Counts after a restart of the video.':
        'Menüs und HUD behalten bei einem breiten Fenster ihre Proportionen. Gilt nach einem Neustart der Grafik.',
    'Dynamic Glow': 'Dynamisches Leuchten',
    'Lightsabers and lights glow.': 'Lichtschwerter und Lichtquellen leuchten.',
    'Shadows': 'Schatten',
    'How the shadows of the players are drawn.': 'Wie Spielerschatten dargestellt werden.',
    'Default': 'Standard',
    'More Realistic': 'Realistischer',
    'Realistic': 'Realistisch',
    'Render To Texture Effects': 'Render-to-Texture-Effekte',
    'Off: no slow effects (cloak, push blur, protection spheres) on graphics cards that have trouble with them.':
        'Aus: keine langsamen Effekte (Tarnung, Machtstoß-Unschärfe, Schutzsphären) auf Grafikkarten, die damit Probleme haben.',

    # sound
    'Steam Audio: sound that knows the level. Needs its library next to the game.':
        'Steam Audio: Klang, der die Umgebung kennt. Die Bibliothek muss im Spielordner liegen.',
    'Output': 'Ausgabe',
    '3D Sound: direction for two ears, or plain panning for speakers.':
        '3D-Sound: Richtungshören für Kopfhörer oder einfaches Panning für Lautsprecher.',
    'Headphones': 'Kopfhörer',
    'Speakers': 'Lautsprecher',
    'Reverb': 'Hall',
    '3D Sound: how loud the rooms ring.': '3D-Sound: wie stark die Räume nachhallen.',
    'Room Reflections': 'Raumreflexionen',
    'The room around you rings.': 'Der Raum um dich herum hallt mit.',
    'Sound Around Corners': 'Schall um die Ecke',
    'A sound finds its way around corners.': 'Geräusche finden ihren Weg um Ecken.',
    'Doppler Effect': 'Doppler-Effekt',
    'Sound in the Background': 'Ton im Hintergrund',
    'The game goes on playing its sound while it is not in front or is minimized.':
        'Das Spiel spielt den Ton weiter, wenn es im Hintergrund läuft oder minimiert ist.',
    'What comes closer or goes away changes its pitch.': 'Was sich nähert oder entfernt, ändert seine Tonhöhe.',
    # what came from TaystJK (2026-10-17)
    'TaystJK': 'TaystJK',
    'What came from the TaystJK client: HUD, movement, hats and capes and more.': 'Funktionen aus dem TaystJK-Client: HUD, Bewegung, Hüte, Umhänge und mehr.',
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
    'What you wear on your head. Other players of VisionJK see it.': 'Was du auf dem Kopf trägst. Andere VisionJK-Spieler sehen es.',
    'What you wear on your back. Other players of VisionJK see it.': 'Was du auf dem Rücken trägst. Andere VisionJK-Spieler sehen es.',
    'Killfeed': 'Killfeed',
    'Who killed whom with what, at the upper right.': 'Wer wen womit erledigt hat, oben rechts.',
    'Team Overlay Size': 'Größe der Teamliste',
    'How large the list of your team mates is.': 'Wie groß die Liste deiner Teamkameraden ist.',
    'Crosshair Colour': 'Fadenkreuzfarbe',
    'White': 'Weiß',
    'Cyan': 'Cyan',
    'Yellow': 'Gelb',
    'Orange': 'Orange',
    'Pink': 'Rosa',
    'The colour of the crosshair with nothing under it.': 'Farbe des Fadenkreuzes, wenn es auf nichts zielt.',
    'Name Opacity': 'Namen-Deckkraft',
    'How clearly the name of the player under the crosshair shows.': 'Wie deutlich der Name des Spielers unter dem Fadenkreuz angezeigt wird.',
    'Movement': 'Bewegung',
    'Speedometer': 'Tacho',
    'Your speed under the crosshair, and the speeds of your last jumps.': 'Deine Geschwindigkeit unter dem Fadenkreuz und die deiner letzten Sprünge.',
    'Strafe Helper': 'Strafe-Hilfe',
    'A bar under the crosshair that shows where to turn to gain speed.': 'Ein Balken unter dem Fadenkreuz, der zeigt, wohin du drehen musst, um schneller zu werden.',
    'Snap HUD': 'Snap-Anzeige',
    'In the air, the angles where the rounding of the game gives a little more speed.': 'Zeigt in der Luft die Winkel, in denen die Rundung des Spiels etwas mehr Tempo bringt.',
    'Pitch HUD': 'Neigungsanzeige',
    'A mark where your view would be at a chosen pitch.': 'Eine Markierung, wo dein Blick bei einem gewählten Neigungswinkel läge.',
    'Strafe Trails': 'Strafe-Spuren',
    'Off': 'Aus',
    'Mine': 'Eigene',
    'Everybody': 'Alle',
    'A fading line on the ground along where players ran.': 'Eine verblassende Linie auf dem Boden entlang der Laufwege der Spieler.',
    'Looks': 'Aussehen',
    'Hats and Capes': 'Hüte und Umhänge',
    "Everybody's": 'Die von allen',
    'Mine on Everybody': 'Eigene bei allen',
    'What players wear on their heads and backs. Only players of VisionJK wear them.': 'Was Spieler auf dem Kopf und dem Rücken tragen. Gilt nur für VisionJK-Spieler.',
    'Seasonal Hats': 'Saisonale Hüte',
    'A Santa hat at Christmas and a pumpkin at Halloween for those without a hat.': 'Weihnachtsmütze an Weihnachten, Kürbis an Halloween, für alle ohne Hut.',
    'Enemy Model': 'Gegnermodell',
    'Every opponent in this model, as model/skin. Empty: as they chose.': 'Jeder Gegner erscheint mit diesem Modell (Modell/Skin). Leer: wie vom Spieler gewählt.',
    'Ally Model': 'Teammodell',
    'Every team mate in this model, as model/skin. Empty: as they chose.': 'Jeder Teamkamerad erscheint mit diesem Modell (Modell/Skin). Leer: wie gewählt.',
    'Saber Team Colours': 'Teamfarben für Klingen',
    'In team games every blade in the colour of its team. Off: blades keep their colour.': 'In Teamspielen hat jede Klinge die Farbe ihres Teams. Aus: Klingen behalten ihre Farbe.',
    'Two-Coloured Staff': 'Zweifarbiger Stab',
    'The second blade of a staff in the second saber colour.': 'Die zweite Klinge eines Stabs hat die zweite Schwertfarbe.',
    'Jedi Outcast Shots': 'Jedi-Outcast-Schüsse',
    'Shots drawn as crisply as in Jedi Outcast. Counts from the next level.': 'Schüsse werden so scharf dargestellt wie in Jedi Outcast. Gilt ab dem nächsten Level.',
    'Disruptor Beam': 'Disruptor-Strahl',
    'Line': 'Linie',
    'Spiral': 'Spirale',
    'How the shot of the disruptor looks.': 'Aussehen des Disruptor-Schusses.',
    'Chat Emoji': 'Emojis im Chat',
    'Codes like :smile: are drawn as pictures in the chat box.': 'Codes wie :smile: werden im Chat als Bilder angezeigt.',
    'Chat Log': 'Chatprotokoll',
    'Every chat line is written into the folder chatlogs, with the time.': 'Speichert jede Chatzeile mit Uhrzeit im Ordner chatlogs.',
    'Console Clock': 'Uhr in der Konsole',
    'Time': 'Uhrzeit',
    'Date and Time': 'Datum und Uhrzeit',
    'A clock in the corner of the console.': 'Eine Uhr in der Ecke der Konsole.',
    'Scoreboard Clock': 'Uhr im Scoreboard',
    'The time of day on the scoreboard.': 'Die Uhrzeit im Scoreboard.',
    'Timer Milliseconds': 'Timer mit Millisekunden',
    'The timer of the level with milliseconds.': 'Zeigt den Timer des Levels mit Millisekunden.',
    'Timer Counts Down': 'Timer als Countdown',
    'The timer shows what is left of the time limit, if there is one.': 'Der Timer zeigt die verbleibende Zeit bis zum Zeitlimit, falls es eins gibt.',
    'Hit Sounds': 'Treffergeräusche',
    'A short tick when your shots or your blade hit someone.': 'Ein kurzes Ticken, wenn deine Schüsse oder deine Klinge treffen.',
    'Kill Sound': 'Kill-Sound',
    'Two notes when you kill someone.': 'Zwei Töne, wenn du jemanden erledigst.',
    'Movement Keys': 'Bewegungstasten',
    'The keys you move with on the screen, lit while held.': 'Zeigt die Bewegungstasten auf dem Bildschirm, sie leuchten, solange sie gedrückt sind.',
    'Start Goal': 'Startziel',
    'The speedometer turns gold from this speed on. 0: off.': 'Der Tacho wird ab dieser Geschwindigkeit golden. 0: aus.',
    'Jump Goal': 'Sprungziel',
    'Jumps at least this fast are gold in the list of jumps. 0: off.': 'Sprünge ab dieser Geschwindigkeit erscheinen in der Sprungliste golden. 0: aus.',
    'Smooth Camera': 'Kamera glätten',
    'Vulkan Renderer': 'Vulkan-Renderer',
    'Spectator Map': 'Zuschauerkarte',
    'While you watch, a map of the players around at the upper right.': 'Zeigt beim Zuschauen oben rechts eine Karte der Spieler in der Nähe.',
    'Lead Indicator': 'Vorhalteanzeige',
    'With Own Ping': 'Mit eigenem Ping',
    'Where to aim so a shot that flies meets a moving opponent. Many servers count this as an aim aid.': 'Zeigt, wohin du zielen musst, damit ein fliegender Schuss einen bewegten Gegner trifft. Viele Server werten das als Zielhilfe.',
    'The camera behind you follows the same at every frame rate. It trails a little further behind.': 'Die Kamera hinter dir folgt bei jeder Bildrate gleich. Sie hängt etwas weiter hinterher.',
    'Bloom': 'Bloom',
    'Bright parts of the level shine over their surroundings. Vulkan renderer only.': 'Helle Stellen des Levels strahlen auf ihre Umgebung aus. Nur mit dem Vulkan-Renderer.',
    'Bloom Strength': 'Bloom-Stärke',
    'How strongly the bright parts shine.': 'Wie stark die hellen Stellen strahlen.',
    'Bloom Threshold': 'Bloom-Schwelle',
    'How bright a part has to be to shine.': 'Wie hell eine Stelle sein muss, um zu strahlen.',
    'Render Scale': 'Renderskalierung',
    'Half': 'Halb',
    'Three Quarters': 'Drei Viertel',
    'Full': 'Voll',
    'One and a Half': '1,5-fach',
    'Double': 'Doppelt',
    'The size the level is drawn at, the HUD stays sharp. Vulkan renderer only, counts after a video restart.': 'Auflösung, in der das Level gezeichnet wird. Das HUD bleibt scharf. Nur mit dem Vulkan-Renderer, gilt nach einem Neustart der Grafik.',
    'Name Alert': 'Namenshinweis',
    'Marked': 'Markiert',
    'Marked and Sound': 'Markiert mit Ton',
    'A chat line with your name is marked gold. More words with tay_notifyWords in the console.': 'Eine Chatzeile mit deinem Namen wird golden markiert. Weitere Wörter mit tay_notifyWords in der Konsole.',
    'Echoes of Their Own': 'Eigene Echos',
    'Echo': 'Echo',
    'Real Time': 'Echtzeit',
    'Precalculated': 'Vorberechnet',
    'Echo Calculation': 'Echo-Berechnung',
    'Graphics Card': 'Grafikkarte',
    'CPU': 'CPU',
    'Graphics Card: the rays of the echo are traced on the graphics card, which leaves the CPU free. CPU: they are traced on the CPU. Where the card cannot do it, the CPU is used. Does nothing with a precalculated echo.':
        'Grafikkarte: die Strahlen des Echos werden auf der Grafikkarte verfolgt, die CPU bleibt frei. CPU: sie werden auf der CPU verfolgt. Wo die Karte das nicht kann, rechnet die CPU. Ohne Wirkung bei einem vorberechneten Echo.',
    'Real time: worked out as you go, the loudest sounds echo on their own. Precalculated: worked out once per level and kept, steady while you move, all sounds ring with the room you are in.':
        'Echtzeit: wird laufend berechnet, die lautesten Geräusche haben ihr eigenes Echo. Vorberechnet: einmal pro Level berechnet und gespeichert, stabil beim Bewegen, alle Geräusche hallen mit dem Raum, in dem du stehst.',
    'How many of the loudest sounds echo off the walls around them. More costs more time a frame.':
        'Wie viele der lautesten Geräusche von den Wänden widerhallen. Mehr kostet mehr Leistung pro Bild.',
    'Real Sound Travel': 'Echte Schallausbreitung',
    'Sounds fade with the distance as real ones do, lose their highs far away and arrive at the speed of sound.':
        'Geräusche werden mit der Entfernung leiser wie in echt, verlieren in der Ferne ihre Höhen und erreichen dich mit Schallgeschwindigkeit.',
    'Talk to and hear other players with your microphone.': 'Mit dem Mikrofon andere Spieler hören und mit ihnen sprechen.',
    'Mode': 'Modus',
    'Push to talk: hold the talk key (Controls, Extras).': 'Push-to-Talk: Sprechtaste gedrückt halten (Steuerung, Extras).',
    'Push To Talk': 'Push-to-Talk',
    'Voice Activation': 'Sprachaktivierung',
    'Voice Chat Volume': 'Sprachchat-Lautstärke',
    'Boombox': 'Boombox',
    'Boomboxes': 'Boomboxen',
    'See and hear the boomboxes on a VisionJK server, which play songs of YouTube.':
        'Boomboxen auf VisionJK-Servern sehen und hören. Sie spielen Songs von YouTube.',
    'Boombox Volume': 'Boombox-Lautstärke',
    'How loud the songs of the boomboxes are for you.': 'Lautstärke der Boomboxen für dich.',
    'How loud the other players are.': 'Wie laut du die anderen Spieler hörst.',
    'Microphone Gain': 'Mikrofonverstärkung',
    'How loud your microphone is sent.': 'Lautstärke, mit der dein Mikrofon gesendet wird.',
    'Activation Level': 'Aktivierungsschwelle',
    'Voice activation: how loud you have to be before you are sent.':
        'Sprachaktivierung: wie laut du sein musst, damit gesendet wird.',
    'Muffle Behind Walls': 'Hinter Wänden dämpfen',
    'Voices behind walls and under water sound muffled.': 'Stimmen hinter Wänden und unter Wasser klingen gedämpft.',
    'Show Who Talks': 'Sprecher anzeigen',
    'A list of the players who are talking on the HUD.': 'Zeigt im HUD eine Liste der Spieler, die gerade sprechen.',
    'On Other Servers': 'Auf anderen Servern',
    'Servers without voice: use master.jka.vision, which sees your address.':
        'Server ohne Sprachchat: master.jka.vision verwenden, der deine Adresse sieht.',
    'Own Voice In The Room': 'Eigene Stimme im Raum',
    'With spatial sound: how loud the room rings from what you say yourself.':
        'Mit 3D-Sound: wie stark der Raum auf deine eigene Stimme nachhallt.',
    'Radio Sound': 'Funkklang',
    'The clan radio sounds like one: narrow, a little rough, with a click.':
        'Der Clan-Funk klingt wie echter Funk: schmal, etwas rau, mit einem Klick.',

    # game
    'Initiate': 'Anwärter',
    'Padawan': 'Padawan',
    'Jedi': 'Jedi',
    'Jedi Knight': 'Jedi-Ritter',
    'Jedi Master': 'Jedi-Meister',

    # interface
    'New HUD': 'Neues HUD',
    'The HUD of VisionJK. Off: the HUD of the game.': 'Das HUD von VisionJK. Aus: das HUD des Originalspiels.',
    'Classic Menus': 'Klassische Menüs',
    'The menus of the game as they were. Counts from the next start of the game.':
        'Die Menüs des Originalspiels. Gilt ab dem nächsten Spielstart.',
    'Menu in the Level': 'Menü im Level',
    'In a game an open menu stands in the level next to you.': 'Im Spiel erscheint ein offenes Menü als Teil des Levels neben dir.',
    'Menu Blur': 'Menü-Unschärfe',
    'How far the level behind an open menu is out of focus.': 'Wie unscharf das Level hinter einem offenen Menü ist.',
    'Field of View': 'Sichtfeld',
    'How wide you see.': 'Wie breit dein Blickfeld ist.',
    'Camera Distance': 'Kameraabstand',
    'How far the camera is behind you in third person.': 'Wie weit die Kamera in der Außenansicht hinter dir ist.',
    'Crosshair Size': 'Fadenkreuzgröße',
    'Disruptor': 'Disruptor',
    'Screen Effects': 'Bildschirmeffekte',
    'Master switch for all screen effects. Only with the Vulkan renderer.': 'Hauptschalter für alle Bildschirmeffekte. Nur mit dem Vulkan-Renderer.',
    'Explosion Waves': 'Explosionswellen',
    'Ringing Ears': 'Ohrenklingeln',
    'Explosions send a wave through the air that bends the picture. Close by, the colours split.': 'Explosionen schicken eine Welle durch die Luft, die das Bild verbiegt. In der Nähe spalten sich die Farben.',
    'After an explosion close by the sound turns dull and your ears ring for a moment. Works with any renderer.': 'Nach einer Explosion in der Nähe wird der Ton dumpf und deine Ohren klingeln kurz. Funktioniert mit jedem Renderer.',
    'Heat Haze': 'Hitzeflimmern',
    'The air shimmers over jetpack flames, the flamethrower and the barrel of your own repeater or blaster after a long burst, which also smokes.': 'Die Luft flimmert über Jetpack-Flammen, dem Flammenwerfer und dem Lauf deines Repetierers oder Blasters nach einer langen Salve, der dann auch raucht.',
    'Hit Feedback': 'Trefferrückmeldung',
    'A red edge shows on the side a hit comes from, and the colours split for a moment. Below 30 health the colours drain and a heartbeat plays.': 'Ein roter Rand zeigt die Seite, von der ein Treffer kommt, und die Farben spalten sich kurz. Unter 30 Leben verblassen die Farben und ein Herzschlag ertönt.',
    'Under Water': 'Unter Wasser',
    'Under water the picture sways, fades into the colour of the water and light plays on the floor. The sound is muffled. Drops run down the view after you surface.': 'Unter Wasser schwankt das Bild, nimmt die Farbe des Wassers an und Licht spielt auf dem Boden. Der Ton ist gedämpft. Nach dem Auftauchen laufen Tropfen über das Bild.',
    'Force Looks': 'Macht-Optik',
    'Looks for the Force powers. Rage: dark red tunnel, veins and a quiet hum. Protect: a dome of energy. Absorb: blue streams drawn in. Speed: blur towards the middle. Push and pull: a wave through the air. Force Sight: a spirit world with mist, players as smoking ghosts and a dreamlike sound.': 'Optik für die Machtkräfte. Wut: dunkelroter Tunnel, Adern und ein leises Summen. Schutz: eine Energiekuppel. Absorbieren: blaue Ströme, die eingesogen werden. Tempo: Unschärfe zur Mitte hin. Stoßen und Ziehen: eine Welle durch die Luft. Machtsicht: eine Geisterwelt mit Nebel, Spieler als rauchende Geister und ein traumhafter Klang.',
    'Saber Clash and Heat': 'Klingenschlag und Hitze',
    'Clashing blades flash. The air around a lit blade is hot.': 'Aufeinandertreffende Klingen blitzen auf. Die Luft um eine eingeschaltete Klinge ist heiß.',
    'Binoculars and Rockets': 'Fernglas und Raketen',
    'Binocular Lens': 'Fernglas-Linse',
    'The binoculars look through a lens with mouse wheel zoom, the distance and a compass. Only with the Vulkan renderer.': 'Das Fernglas schaut durch eine Linse mit Mausrad-Zoom, der Entfernung und einem Kompass. Nur mit dem Vulkan-Renderer.',
    'Rocket Lock-On': 'Raketen-Zielerfassung',
    'The lock of the rocket launcher draws brackets that close in on the target and beeps higher with every step.': 'Die Zielerfassung des Raketenwerfers zeichnet Klammern, die sich auf das Ziel zuziehen, und piept mit jedem Schritt höher.',
    'Scope Lens': 'Zielfernrohr-Linse',
    'Zoom with Mouse Wheel': 'Zoom mit Mausrad',
    'Motion Tracker Speed': 'Tempo Bewegungsmelder',
    'The zoomed disruptor shows the level through a lens. Off: the flat black mask. Only with the Vulkan renderer.': 'Der gezoomte Disruptor zeigt den Level durch eine Linse. Aus: die flache schwarze Maske. Nur mit dem Vulkan-Renderer.',
    'While the disruptor zoom is locked, the mouse wheel zooms in and out.': 'Solange der Disruptor-Zoom eingerastet ist, zoomt das Mausrad rein und raus.',
    'The speed a player needs before the tracker of the disruptor scope picks him up.': 'Das Tempo, ab dem der Bewegungsmelder des Disruptor-Zielfernrohrs einen Spieler erfasst.',
    'The size of the crosshair.': 'Die Größe des Fadenkreuzes.',
    'Show Frame Rate': 'FPS anzeigen',
    'Frames a second in the corner of the screen.': 'Bilder pro Sekunde in der Bildschirmecke.',
    'Show Timer': 'Timer anzeigen',
    'The time of the level in the corner of the screen.': 'Die Zeit des Levels in der Bildschirmecke.',
    'Show Lagometer': 'Lagometer anzeigen',
    'A graph of your connection.': 'Ein Diagramm deiner Verbindung.',

    # chat and social
    'Chat Bubbles': 'Sprechblasen',
    'What a player says stands above his head.': 'Was ein Spieler sagt, steht über seinem Kopf.',
    'Bubble Range': 'Sprechblasen-Reichweite',
    'How far away a bubble is still drawn.': 'Bis zu welcher Entfernung Sprechblasen angezeigt werden.',
    'Player Statuses': 'Spielerstatus',
    'Away from keyboard and music above the heads of the others.': 'Zeigt Abwesenheit und Musik über den Köpfen der anderen.',
    'Show My Music': 'Meine Musik zeigen',
    'Tell the others what you listen to. Everybody on the server reads it.':
        'Zeigt anderen, was du hörst. Jeder auf dem Server kann es lesen.',
    'Choose Players of My Music ...': 'Musikplayer auswählen ...',
    'Which players of music may be told: only the songs of the ones you tick are shown to the others.':
        'Welche Musikplayer angezeigt werden dürfen: Nur die Songs der angehakten sehen die anderen.',
    'Players of My Music': 'Meine Musikplayer',
    'Click a player to tick it, or to take the tick away.': 'Ein Klick hakt einen Player an oder entfernt den Haken.',
    'Only the songs of ticked players are shown to the others.': 'Nur die Songs angehakter Player sehen die anderen.',
    'Select Players': 'Spieler auswählen',
    'With the mouse free, a click on a player opens a card with what you can do with him.':
        'Bei freier Maus öffnet ein Klick auf einen Spieler eine Karte mit den möglichen Aktionen.',
    'Selection Outline': 'Auswahlumriss',
    'A glowing outline around the player under the mouse.': 'Ein leuchtender Umriss um den Spieler unter der Maus.',
    'Own Chat Line': 'Eigene Chateingabe',
    'Type into the chat box of VisionJK, with history and links.': 'Im Chatfenster von VisionJK tippen, mit Verlauf und Links.',
    'Chat Box Width': 'Chatbreite',
    'How wide the chat box is.': 'Wie breit die Chatbox ist.',
    'Chat Beep': 'Chat-Ton',
    'A sound when somebody says something.': 'Ein Ton, wenn jemand etwas schreibt.',
    'Sharp Console Font': 'Scharfe Konsolenschrift',
    'The console in a font with every character.': 'Konsole mit einer Schrift, die alle Zeichen enthält.',
    'Console Text Size': 'Konsolen-Textgröße',
    'The size of the text of the console.': 'Größe des Konsolentextes.',
    'Console Opacity': 'Konsolen-Deckkraft',
    'How much of the game shows through the console.': 'Wie viel vom Spiel durch die Konsole zu sehen ist.',
    'Clan Password': 'Clan-Passwort',
    'Clan chat and clan radio reach everybody on the server who has the same one. The server sees it.':
        'Clan-Chat und Clan-Funk erreichen alle auf dem Server mit demselben Passwort. Der Server sieht es.',

    # network
    'Check for Updates': 'Nach Updates suchen',
    'Ask at every start whether a newer version is out.': 'Fragt bei jedem Start, ob eine neuere Version da ist.',
    'Update Channel': 'Update-Kanal',
    'Stable': 'Stabil',
    'Development': 'Entwicklung',
    'Stable: the releases. Development: early test builds, and the releases when they are newer.':
        'Stabil: die Releases. Entwicklung: frühe Testversionen und die Releases, wenn sie neuer sind.',
    'Development builds are early test versions.': 'Entwicklungsversionen sind frühe Testversionen.',
    'They can crash, corrupt your configs or not work on some servers.':
        'Sie können abstürzen, deine Configs beschädigen oder auf manchen Servern nicht laufen.',
    'You can switch back to Stable at any time.': 'Du kannst jederzeit zu Stabil zurückwechseln.',
    'Stay on Stable': 'Bei Stabil bleiben',
    'Keep the releases.': 'Bei den Releases bleiben.',
    'Switch to Development': 'Zu Entwicklung wechseln',
    'Update to the development builds from now on.': 'Ab jetzt auf Entwicklungsversionen aktualisieren.',
    'Changelog': 'Changelog',
    'What changed in each version, and what a new version brings.': 'Was sich in jeder Version geändert hat und was eine neue Version bringt.',
    'Choose Downloads': 'Downloads auswählen',
    'Before a download choose the files of the server, and see what is in them before they are used.':
        'Vor einem Download die Dateien des Servers auswählen und ihren Inhalt ansehen, bevor sie verwendet werden.',
    'Keep Downloads': 'Downloads behalten',
    'Keep downloaded files in the folder downloads. Off: for this session only.':
        'Heruntergeladene Dateien im Ordner downloads behalten. Aus: nur für diese Sitzung.',
    'Web Downloads': 'Web-Downloads',
    'Files of a server that names a web address for them come from there, much faster. Off: from the server itself.':
        'Nennt ein Server eine Webadresse für seine Dateien, werden sie von dort geladen, viel schneller. Aus: direkt vom Server.',
    'Connection': 'Verbindung',
    'Smooth Jittery Lines': 'Jitter ausgleichen',
    'Up to 20 ms': 'Bis 20 ms',
    'Up to 40 ms': 'Bis 40 ms',
    'Up to 80 ms': 'Bis 80 ms',
    'On a line whose snapshots come unevenly the game is drawn a little later, so that nothing stutters.':
        'Kommen die Snapshots einer Verbindung ungleichmäßig an, wird das Spiel etwas später angezeigt, damit nichts ruckelt.',
    'Intrusion Log': 'Angriffsprotokoll',
    'Write down what a server sends that no server of the game does, and leave one that keeps at it.':
        'Protokolliert, was ein Server sendet, das kein Server des Spiels sendet, und verlässt Server, die damit nicht aufhören.',
    'Key Login by Agent': 'Schlüssel-Login per Agent',
    'The admin login asks your ssh-agent for the key (1Password, OpenSSH).':
        'Der Admin-Login holt den Schlüssel von deinem ssh-agent (1Password, OpenSSH).',
    'Key File': 'Schlüsseldatei',
    'The file with the key for the admin login, where no agent has it.':
        'Datei mit dem Schlüssel für den Admin-Login, falls kein Agent ihn hat.',
    'UTF-8 Text': 'UTF-8-Text',
    'Every character in names and chat. Off for a language with a code page of its own.':
        'Alle Zeichen in Namen und Chat. Für Sprachen mit eigener Codepage ausschalten.',

    # controls
    'Grapple Hook': 'Enterhaken',
    'A hook on a rope that pulls you to where it hits.': 'Ein Haken am Seil, der dich dorthin zieht, wo er trifft.',
    'Jetpack': 'Jetpack',
    'Turns the jetpack on and off.': 'Schaltet das Jetpack ein und aus.',
    'Full Force Duel': 'Duell mit allen Kräften',
    'Challenges to a duel with every force power that is allowed.': 'Fordert zu einem Duell mit allen erlaubten Machtkräften heraus.',
    'Message Menu': 'Nachrichtenmenü',
    'A menu for clan chat, private and admin messages.': 'Ein Menü für Clan-Chat sowie private und Admin-Nachrichten.',
    'Voice Chat (Talk)': 'Sprachchat (Sprechen)',
    'Hold to talk in the voice chat (push to talk).': 'Gedrückt halten, um im Sprachchat zu sprechen (Push-to-Talk).',
    'Clan Radio (Talk)': 'Clan-Funk (Sprechen)',
    'Hold to talk to your clan over the radio, wherever they are.': 'Gedrückt halten, um über Funk mit deinem Clan zu sprechen, egal wo er ist.',
    'Free the Mouse (Select Players)': 'Maus freigeben (Spieler auswählen)',
    'Hold to point at players with the mouse: a click opens what you can do with one.':
        'Gedrückt halten, um mit der Maus auf Spieler zu zeigen: Ein Klick zeigt, was du mit ihnen tun kannst.',
    'Quick Wheel (Boombox)': 'Schnellrad (Boombox)',
    'Hold for a wheel: put a boombox or a sound system down, or work the one you look at. Let go on a choice.':
        'Gedrückt halten für ein Rad: Boombox oder Soundanlage aufstellen oder die bedienen, auf die du schaust. Zum Auswählen loslassen.',
    'Command Menu': 'Befehlsmenü',
    'Opens lists of what VisionJK can do, from the boombox to emotes and votes: a number key or a click picks one.':
        'Öffnet Listen mit dem, was VisionJK kann, von der Boombox bis zu Gesten und Abstimmungen: Eine Zifferntaste oder ein Klick wählt aus.',
    'Tool Window (Sandbox)': 'Werkzeugfenster (Sandbox)',
    'Opens the toolgun window (after amsandbox): pick a tool and set its colour, material or properties.':
        'Öffnet das Fenster der Toolgun (nach amsandbox): Werkzeug wählen und Farbe, Material oder Eigenschaften einstellen.',
    'Model Store': 'Modell-Store',
    'Opens the model store of jka.vision: get player models others uploaded, and use them at once.': 'Öffnet den Modell-Store von jka.vision: hole Spielermodelle, die andere hochgeladen haben, und nutze sie sofort.',
    'Throw Snowball': 'Schneeball werfen',
    'Hold to wind up, let go to throw: the longer you hold, the further it flies. Only players of VisionJK see it.': 'Halten zum Ausholen, loslassen zum Werfen: je länger du hältst, desto weiter fliegt er. Nur Spieler von VisionJK sehen ihn.',
    # from the VisionJK of 2019: blade look, duels, notifications
    'Lightsabers': 'Lichtschwerter',
    'Blade Look': 'Klingenoptik',
    'Classic': 'Klassisch',
    'SFX': 'SFX',
    'SFX: a thin white core in a glow that is smeared over the swing. RGB styles and black stay classic.': 'SFX: ein dünner weißer Kern in einem Leuchten, das über den Schwung gezogen wird. RGB-Stile und Schwarz bleiben klassisch.',
    'Trail Length': 'Spurlänge',
    'How long the trail behind a swinging blade is.': 'Wie lang die Spur hinter einer schwingenden Klinge ist.',
    'Blade Core': 'Klingenkern',
    'How thick the white core of a blade is.': 'Wie dick der weiße Kern einer Klinge ist.',
    'Blade Glow': 'Klingenleuchten',
    'How wide the glow around a blade is.': 'Wie breit das Leuchten um eine Klinge ist.',
    'Duels': 'Duelle',
    'Others in a Duel': 'Andere im Duell',
    'Tinted': 'Eingefärbt',
    'Shield': 'Schild',
    'Damaged Shield': 'Getroffener Schild',
    'Force Shell': 'Machthülle',
    'Sight Shell': 'Sichthülle',
    'Half Shield': 'Halber Schild',
    'Cloaked': 'Getarnt',
    'Sight Bubble': 'Sichtblase',
    'How players look who are in a duel you are not part of. Their colour is v_duelColour in the console.': 'Wie Spieler aussehen, die in einem Duell sind, an dem du nicht beteiligt bist. Ihre Farbe steht in v_duelColour in der Konsole.',
    'Their Blades': 'Ihre Klingen',
    'The blades of players who are in a duel you are not part of.': 'Die Klingen von Spielern in einem Duell, an dem du nicht beteiligt bist.',
    'Notifications': 'Benachrichtigungen',
    'Shown': 'Angezeigt',
    'Shown and Sound': 'Angezeigt mit Ton',
    'Tells, your name in the chat, boombox lines and new versions slide in at the left.': 'Private Nachrichten, dein Name im Chat, Boombox-Zeilen und neue Versionen erscheinen links am Bildschirmrand.',
    'Notification Time': 'Anzeigedauer',
    'How many milliseconds a notification stays.': 'Wie viele Millisekunden eine Benachrichtigung sichtbar bleibt.',
    'Notification Size': 'Größe der Benachrichtigungen',
    'How large the notifications are.': 'Wie groß die Benachrichtigungen sind.',
    # the window Intrusions of the server browser
    'Intrusions': 'Angriffe',
    'What the intrusion detection noted about servers, newest first.': 'Was die Angriffserkennung über Server notiert hat, neueste zuerst.',
    'All of it is in ids.log in the game folder.': 'Alles steht in ids.log im Spielordner.',
    # the window Clean Up Settings of Setup, Network (ui/jamp/cvarclean.menu, ui_cvarclean.c)
    'Clean Up Settings': 'Einstellungen aufräumen',
    'Settings no code used for a month, from older versions and other mods.': 'Einstellungen, die einen Monat lang kein Code benutzt hat, aus älteren Versionen und anderen Mods.',
    'Nothing to clean up.': 'Nichts aufzuräumen.',
    'Settings no code used for a month: left from older versions and other mods. Remove takes one out of the config.': 'Einstellungen, die einen Monat lang kein Code benutzt hat: übrig aus älteren Versionen und anderen Mods. Entfernen nimmt eine aus der Config.',
    'Remove All': 'Alle entfernen',
    # the search of Setup
    'Search': 'Suchen',
    'Shows the settings of all pages whose name or description has these words.': 'Zeigt die Einstellungen aller Seiten, in deren Name oder Beschreibung diese Wörter vorkommen.',
}

# What the cgame says by itself: the card of a player, the scoreboard, the chat box, what stands above a head.
# It asks for a text by its English (CG_VisionText() of cgame/hud/cg_hud.c makes the key from it as key_of() does
# here), so none of these may need a number behind its key.
CGAME = {
    # the photo mode
    'Picture saved to the screenshots folder.': 'Foto im Screenshot-Ordner gespeichert.',
    'Mouse: turn. Wheel: distance. Left: focus. 1-5: filter. Right: photo. Esc: leave.':
        'Maus: drehen. Rad: Abstand. Links: Fokus. 1-5: Filter. Rechts: Foto. Esc: zurück.',
    # the card of a player
    'Mute his voice': 'Stimme stummschalten',
    'Hear his voice again': 'Stimme wieder einschalten',
    'Voice quieter (%i %%)': 'Stimme leiser (%i %%)',
    'Voice louder (%i %%)': 'Stimme lauter (%i %%)',
    'Hide his chat lines': 'Chatzeilen ausblenden',
    'Show his chat lines again': 'Chatzeilen wieder einblenden',
    'Whisper to him': 'Flüstern',
    'Watch him': 'Zuschauen',
    'Copy his name': 'Namen kopieren',
    'Call a vote to kick him': 'Rauswurf-Abstimmung',
    'Click again to call the vote': 'Zum Starten nochmal klicken',
    'Admin: kick': 'Admin: kicken',
    'Admin: silence': 'Admin: stummschalten',
    'Admin: sleep': 'Admin: schlafen legen',
    'Admin: slap': 'Admin: Ohrfeige',
    'Admin: go to him': 'Admin: hingehen',
    'Admin: bring him here': 'Admin: herholen',
    'You': 'Du',
    'Player': 'Spieler',
    'Bot': 'Bot',
    'Spectator': 'Zuschauer',
    'Score %i': 'Punkte %i',
    'Ping %i': 'Ping %i',
    '%i min': '%i Min.',
    'Another client': 'Anderer Client',
    # what stands above a head
    'Away from keyboard': 'AFK',
    'Listening to %s': 'Hört %s',
    # the scoreboard
    'Spectators': 'Zuschauer',
    'connecting': 'verbindet sich',
    'and %i more': 'und %i weitere',
    'The scores key': 'Die Scoreboard-Taste',
    '%s twice: pick a player': '%s zweimal: Spieler auswählen',
    'Click a player. %s or Esc closes.': 'Spieler anklicken. Schließen mit %s oder Esc.',
    # the classic Setup menu (ui/jamp/setup.menu), which is written by hand: its way back
    'New Menus': 'Neue Menüs',
    'The menus and the HUD of VisionJK. The video restarts.': 'Die Menüs und das HUD von VisionJK. Die Grafik wird neu gestartet.',
    # who talks ("You" is above)
    'You, clan radio': 'Du, Clan-Funk',
    'radio': 'Funk',
    # the chat box
    'All': 'Alle',
    'Team': 'Team',
    'Private': 'Privat',
    'Say': 'Sagen',
    'To %s': 'An %s',
    'Open %s ? Enter: yes, any other key: no': '%s öffnen? Enter: ja, jede andere Taste: nein',
    # the boombox (cgame/boombox/cg_boombox.c): its card, the line over it, the chat box asking for a song
    'Play a song ...': 'Song abspielen ...',
    'Add a song ...': 'Song hinzufügen ...',
    'Pause': 'Pause',
    'Resume': 'Fortsetzen',
    'Skip this song': 'Song überspringen',
    'Stop the music': 'Musik stoppen',
    'Quieter for everybody (%i %%)': 'Leiser für alle (%i %%)',
    'Louder for everybody (%i %%)': 'Lauter für alle (%i %%)',
    'Mute it for me': 'Für mich stummschalten',
    'Hear it again': 'Wieder einschalten',
    'Move it': 'Verschieben',
    'Take it away': 'Entfernen',
    'Click again to take it away': 'Zum Entfernen nochmal klicken',
    'Plays': 'Läuft',
    'Paused': 'Pausiert',
    'Loading ...': 'Lädt ...',
    'Silent': 'Still',
    'Next: %s': 'Als Nächstes: %s',
    'Looking a song up ...': 'Suche Song ...',
    'Volume %i %%': 'Lautstärke %i %%',
    'muted for you': 'für dich stumm',
    'Boombox, link or song': 'Boombox, Link oder Song',
    'Put down by %s': 'Aufgestellt von %s',
    'Asked for by %s': 'Gewünscht von %s',
    # the window of a box (cgame/boombox/cg_boombox_ui.c)
    'Open the player': 'Player öffnen',
    'Nothing plays': 'Es läuft nichts',
    'Skip': 'Überspringen',
    'Stop': 'Stopp',
    'In the line: %s': 'Warteschlange: %s',
    'In the line': 'Warteschlange',
    'Nothing waits': 'Warteschlange leer',
    'Mute': 'Stumm',
    'Unmute': 'Ton an',
    # the windows of a download (client/cl_download.cpp): the engine asks for these by the key texts.py makes
    'Files of the server': 'Dateien des Servers',
    'The server offers these files to download.': 'Der Server bietet diese Dateien zum Download an.',
    'Before anything of them is used you see what is inside. Programs stay blocked.': 'Bevor etwas davon verwendet wird, siehst du den Inhalt. Programme bleiben gesperrt.',
    'Keep them (downloads folder)': 'Behalten (Ordner downloads)',
    'This session only': 'Nur für diese Sitzung',
    'A pure server may not let you play without its files.': 'Ein Server mit sv_pure lässt dich ohne seine Dateien eventuell nicht spielen.',
    'Download': 'Herunterladen',
    'Join without': 'Ohne beitreten',
    'Disconnect': 'Trennen',
    'What is inside': 'Inhalt',
    'Untick what you do not want: nothing of it is used.': 'Nimm den Haken bei allem raus, was du nicht willst: Davon wird nichts verwendet.',
    'Programs could run on your computer: they are blocked unless you tick them.': 'Programme könnten auf deinem Computer ausgeführt werden: Sie bleiben gesperrt, solange du sie nicht anhakst.',
    'Maps': 'Karten',
    'Models': 'Modelle',
    'Textures and effects': 'Texturen und Effekte',
    'Sounds and music': 'Sounds und Musik',
    'Shaders and scripts': 'Shader und Skripte',
    'Menus and HUD': 'Menüs und HUD',
    'Configs (they can run commands)': 'Configs (können Befehle ausführen)',
    'Other files': 'Sonstige Dateien',
    'Programs (they could run on your computer)': 'Programme (könnten auf deinem Computer laufen)',
    'Use them': 'Verwenden',
    'They come from the web address:': 'Sie kommen von dieser Webadresse:',
    # the HUD (ui/vision/hud.menu, written by hand: @VISION_SABER, @VISION_ALTERNATE_DIMENSION)
    'Saber': 'Schwert',
    'Alternate Dimension': 'Andere Dimension',
    # the sound system and the quick wheel (cgame/boombox, cgame/radial)
    'Sound System': 'Soundanlage',
    'Put a boombox here': 'Boombox hier aufstellen',
    'Put a sound system here': 'Soundanlage hier aufstellen',
    'Play': 'Abspielen',
    'Music Player': 'Musikplayer',
    'Louder': 'Lauter',
    'Quieter': 'Leiser',
    'Remove': 'Entfernen',
    'Click a choice, Esc closes': 'Option anklicken, Esc schließt',
    'Left speaker': 'Linker Lautsprecher',
    'Right speaker': 'Rechter Lautsprecher',
    'No boombox here': 'Keine Boombox hier',
    # the players of music in Setup (codemp/ui/ui_main.c)
    'No player of music runs right now': 'Gerade läuft kein Musikplayer',
    'not running': 'läuft nicht',
    # the changelog (ui_changelog.c, ui_update.c)
    "What's new": 'Neuigkeiten',
    "What's new in": 'Neu in',
    'Update Now': 'Jetzt aktualisieren',
    'Version': 'Version',
    'Development build': 'Entwicklungsversion',
    'Installed': 'Installiert',
    'Not installed yet': 'Noch nicht installiert',
    'Click the link again to open it in your browser:': 'Klicke den Link noch einmal, um ihn im Browser zu öffnen:',
    'The changelog is not there. It comes with the next update.': 'Der Changelog fehlt. Er kommt mit dem nächsten Update.',
    # the window Clean Up Settings (ui_cvarclean.c)
    'Never used': 'Nie benutzt',
    '%i days ago': 'vor %i Tagen',
    # the search of Setup
    'No setting has these words.': 'Keine Einstellung hat diese Wörter.',
    # the command menu (cmdmenu/cg_cmdmenu.c, ui/vision/commandmenu.txt)
    'Place a Boombox': 'Boombox aufstellen',
    'Place a Sound System': 'Soundanlage aufstellen',
    'Mute Everybody': 'Alle stummschalten',
    'Hear Everybody Again': 'Alle wieder hören',
    'Voice Chat Status': 'Sprachchat-Status',
    'Emotes': 'Gesten',
    'Sit': 'Hinsetzen',
    'Kneel': 'Knien',
    'Hello': 'Hallo',
    'Victory': 'Sieg',
    'Come On': 'Komm her',
    'Surrender': 'Aufgeben',
    'Break Dance': 'Breakdance',
    'Flip': 'Salto',
    'More': 'Mehr',
    'Sit Differently': 'Anders hinsetzen',
    'Nod': 'Nicken',
    'Shake Head': 'Kopfschütteln',
    'At Ease': 'Rührt euch',
    'Beg': 'Betteln',
    'Wait': 'Warten',
    'Hands on Hips': 'Hände in die Hüften',
    'Power': 'Kraft',
    'Neo': 'Neo',
    'Duel': 'Duell',
    'Challenge to a Duel': 'Zum Duell herausfordern',
    'Join the Game': 'Mitspielen',
    'Join Red': 'Zu Rot',
    'Join Blue': 'Zu Blau',
    'Spectate': 'Zuschauen',
    'Vote': 'Abstimmung',
    'Yes': 'Ja',
    'No': 'Nein',
    'Restart the Map': 'Karte neu starten',
    'Show': 'Anzeigen',
    'Frame Rate': 'Bildrate',
    'Timer': 'Zeitanzeige',
    'Lagometer': 'Lagometer',
    'Screenshot': 'Screenshot',
    # the sandbox: the toolgun, its window and the line under the crosshair
    'Tools': 'Werkzeuge',
    'Remover': 'Entfernen',
    'Color': 'Farbe',
    'Material': 'Material',
    'Physical Properties': 'Physikalische Eigenschaften',
    'Freeze': 'Einfrieren',
    'Plain': 'Schlicht',
    'Chrome': 'Chrom',
    'Gold': 'Gold',
    'Glass': 'Glas',
    'Ice': 'Eis',
    'Lava': 'Lava',
    'Mass': 'Masse',
    'Bounce': 'Elastizität',
    'Grip': 'Haftung',
    'Light': 'Leicht',
    'Heavy': 'Schwer',
    'Bouncy': 'Springend',
    'Slippery': 'Rutschig',
    'Left click removes the prop you look at.': 'Linksklick entfernt das Objekt, auf das du schaust.',
    'Left click paints the prop, right click takes its color.': 'Linksklick färbt das Objekt, Rechtsklick übernimmt seine Farbe.',
    'Left click puts the material on the prop, right click takes it.': 'Linksklick setzt das Material auf das Objekt, Rechtsklick übernimmt es.',
    'Left click gives the prop these properties, right click takes them.': 'Linksklick gibt dem Objekt diese Eigenschaften, Rechtsklick übernimmt sie.',
    'Left click freezes the prop in place, right click frees it, R frees all.': 'Linksklick friert das Objekt an Ort und Stelle ein, Rechtsklick löst es, R löst alle.',
    'Left click: use   Right click: take   R: reset': 'Linksklick: benutzen   Rechtsklick: übernehmen   R: zurücksetzen',
    '%s models': '%s Modelle',
    'The store has no models yet.': 'Der Store hat noch keine Modelle.',
    'The list of the store is on its way.': 'Die Liste des Stores ist unterwegs.',
    'In the game': 'Im Spiel',
    'Coming...': 'Kommt...',
    'Get (%s)': 'Holen (%s)',
    'Upload your own at jka.vision/models.html': 'Lade eigene hoch auf jka.vision/models.html',
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
