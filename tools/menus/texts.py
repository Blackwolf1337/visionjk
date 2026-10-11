"""The texts the new look adds to the menus, in the languages of the game. tools/menus/build.py writes them as
a string package of their own (strings/<language>/vision.str), and a menu file names one as @VISION_<KEY>, the
way it names a text of the game as @MENUS_<KEY>.

A text is written here in English with its German next to it. French and Spanish read the English until
somebody translates them: their files say "#same", which is how a string package says so."""
import os
import re

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
    'Bot Mode:': 'Bot Mode',
    'Skill:': 'Skill',
    'Fight:': 'Fight',
    'Walk The Route:': 'Walk The Route',
    'No: it stays where it is and only turns and fights.': 'Off: it stays where it is and only turns and fights.',
    'Keys Take Over:': 'Keys Take Over',
    'A move or attack key ends the bot mode. A key for on and off: Controls, JA+.':
        'A move or attack key ends the bot mode. A key for on and off: Controls, Extras.',
    'JetPack': 'Jetpack',
    'Grapple hook to throw on walls': 'A hook on a rope that pulls you to where it hits.',
    'used to turn On/off the JetPack': 'Turns the jetpack on and off.',
    'used to engage a Duel with all allowed force powers': 'Challenges to a duel with every force power that is allowed.',
    'Message Mod': 'Message Menu',
    'used to draw an interface for clan Say, private talk, amsay, ampsay...': 'A menu for clan chat, private and admin messages.',
    'hold to talk in the voice chat (push to talk)': 'Hold to talk in the voice chat (push to talk).',
    'your client plays by itself, and stops (Setup, Bot Mode)': 'Your client plays by itself, and stops (Setup, Bot Mode).',
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
    'Bot Mode': 'Bot-Modus',
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
    'What flies past changes its pitch.': 'Was vorbeifliegt, ändert seine Tonhöhe.',
    'Talk to and hear other players with your microphone.': 'Mit dem Mikrofon mit anderen Spielern sprechen und sie hören.',
    'Mode': 'Modus',
    'Push to talk: hold the talk key (Controls, Extras).': 'Push-to-Talk: die Sprechtaste halten (Steuerung, Extras).',
    'Push To Talk': 'Push-to-Talk',
    'Voice Activation': 'Sprachaktivierung',
    'Voice Chat Volume': 'Lautstärke Sprach-Chat',
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
    'Servers without voice: use voice.jka.vision, which sees your address.':
        'Server ohne Sprach-Chat: voice.jka.vision nutzen, das Ihre Adresse sieht.',
    'Own Voice In The Room': 'Eigene Stimme im Raum',
    'With spatial sound: how loud the room rings from what you say yourself.':
        'Mit 3D-Sound: wie laut der Raum von dem nachklingt, was Sie selbst sagen.',
    'Radio Sound': 'Funkklang',
    'The clan radio sounds like one: narrow, a little rough, with a click.':
        'Der Clan-Funk klingt wie Funk: schmal, etwas rau, mit einem Klick.',

    # game
    'Your client plays by itself, on any server: it walks the route of the map and fights.':
        'Ihr Client spielt von selbst, auf jedem Server: Er läuft die Route der Karte ab und kämpft.',
    'Skill': 'Können',
    'How fast it turns, how well it aims and how soon it attacks.': 'Wie schnell er sich dreht, wie gut er zielt und wie früh er angreift.',
    'Initiate': 'Anwärter',
    'Padawan': 'Padawan',
    'Jedi': 'Jedi',
    'Jedi Knight': 'Jedi-Ritter',
    'Jedi Master': 'Jedi-Meister',
    'Fight': 'Kämpfen',
    'Whom it attacks: nobody, only the one you are in a duel with, or every enemy it sees.':
        'Wen er angreift: niemanden, nur Ihren Duellgegner oder jeden Feind, den er sieht.',
    'Never': 'Nie',
    'In Duels': 'In Duellen',
    'Always': 'Immer',
    'Walk The Route': 'Route ablaufen',
    'Off: it stays where it is and only turns and fights.': 'Aus: Er bleibt, wo er ist, dreht sich nur und kämpft.',
    'Keys Take Over': 'Tasten übernehmen',
    'A move or attack key ends the bot mode. A key for on and off: Controls, Extras.':
        'Eine Bewegungs- oder Angriffstaste beendet den Bot-Modus. Eine Taste für An und Aus: Steuerung, Extras.',

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
    'Bot Mode (On/Off)': 'Bot-Modus (An/Aus)',
    'Your client plays by itself, and stops (Setup, Bot Mode).': 'Ihr Client spielt von selbst und hört wieder auf (Setup, Bot-Modus).',
    'Clan Radio (Talk)': 'Clan-Funk (Sprechen)',
    'Hold to talk to your clan over the radio, wherever they are.': 'Halten, um über Funk mit Ihrem Clan zu sprechen, wo immer er ist.',
    'Free the Mouse (Select Players)': 'Maus freigeben (Spieler auswählen)',
    'Hold to point at players with the mouse: a click opens what you can do with one.':
        'Halten, um mit der Maus auf Spieler zu zeigen: Ein Klick öffnet, was Sie mit einem tun können.',
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
                lines.append('%-19s "#same"' % lang)
            lines.append('')
        lines += ['', 'ENDMARKER', '', '']
        os.makedirs(os.path.join(strings_dir, folder), exist_ok=True)
        with open(os.path.join(strings_dir, folder, 'vision.str'), 'w', encoding='cp1252', newline='\r\n') as f:
            f.write('\n'.join(lines))
