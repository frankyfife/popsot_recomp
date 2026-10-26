#include "tr.h"

#include <QHash>

namespace {
bool g_german = true;

const char *const kTable[][2] = {
    {"This is not an Xbox disc image.", "Das ist kein Xbox-Disc-Abbild."},
    {"The image has no default.xbe.", "Das Abbild enthält keine default.xbe."},
    {"This is not the USA disc (NTSC retail). The port runs only that version; the European disc can add the German speech afterwards.",
     "Das ist nicht die USA-Disc (NTSC-Verkaufsversion). Der Port läuft nur mit dieser Fassung; die europäische Disc kann danach die deutsche Sprachausgabe ergänzen."},
    {"This is Prince of Persia: The Sands of Time, but not the revision the port runs (default.xbe SHA-256 %1...). It needs the USA disc's executable with SHA-256 753C58B1...; the European disc can add the German speech afterwards.",
     "Das ist Prince of Persia: The Sands of Time, aber nicht die Revision, mit der der Port läuft (default.xbe SHA-256 %1 …). Gebraucht wird die ausführbare Datei der USA-Disc mit SHA-256 753C58B1 …; die europäische Disc kann danach die deutsche Sprachausgabe ergänzen."},
    {"This is not Prince of Persia: The Sands of Time.", "Das ist nicht Prince of Persia: The Sands of Time."},
    {"USA disc (NTSC retail) recognised.", "USA-Disc (NTSC-Verkaufsversion) erkannt."},
    {"The image has no German speech (SOUND/SOUNDX4). Choose the European disc.",
     "Das Abbild enthält keine deutsche Sprachausgabe (SOUND/SOUNDX4). Bitte die europäische Disc wählen."},
    {"European disc with German speech recognised.", "Europäische Disc mit deutscher Sprachausgabe erkannt."},
    {"Done.", "Fertig."},
    {"Not enough free space: %1 GB needed.", "Nicht genug freier Speicherplatz: %1 GB werden gebraucht."},
    {"Installing the game: %1", "Spiel wird installiert: %1"},
    {"The installed default.xbe is not the USA retail executable.", "Die installierte default.xbe ist nicht die der USA-Verkaufsversion."},
    {"Reading the German speech from the European disc...", "Deutsche Sprachausgabe wird von der europäischen Disc gelesen …"},
    {"The European disc has no SOUND/SOUNDX4.", "Die europäische Disc enthält kein SOUND/SOUNDX4."},
    {"The installed game has no readable %1.fat.", "Im installierten Spiel ist %1.fat nicht lesbar."},
    {"No German file for %1.", "Keine deutsche Datei für %1."},
    {"Could not move %1 aside.", "%1 konnte nicht beiseitegelegt werden."},
    {"Writing %1 with the German speech...", "%1 wird mit deutscher Sprachausgabe geschrieben …"},
    {"Copying the German bonus films...", "Deutsche Bonusfilme werden kopiert …"},
    {"German speech installed.", "Deutsche Sprachausgabe installiert."},
    {"Could not restore %1.", "%1 konnte nicht wiederhergestellt werden."},
    {"POPSoT Recomp Launcher", "POPSoT Recomp Launcher"},
    {"Prince of Persia: The Sands of Time - PC port of the Xbox version",
     "Prince of Persia: The Sands of Time – PC-Port der Xbox-Version"},
    {"Language:", "Sprache:"},
    {"Installation", "Installation"},
    {"Install from ISO...", "Aus ISO installieren …"},
    {"Open game folder", "Spielordner öffnen"},
    {"Choose game folder...", "Spielordner wählen …"},
    {"Game folder", "Spielordner"},
    {"Language", "Sprache"},
    {"German", "Deutsch"},
    {"English", "Englisch"},
    {"Text and menus:", "Texte und Menüs:"},
    {"Subtitles of the films:", "Untertitel der Filme:"},
    {"Install German speech from the European disc...", "Deutsche Sprachausgabe von der europäischen Disc installieren …"},
    {"Restore English speech", "Englische Sprachausgabe wiederherstellen"},
    {"Speech:", "Sprachausgabe:"},
    {"Game", "Spiel"},
    {"Unlimited", "Unbegrenzt"},
    {"Wait for the display (VSync)", "Auf den Bildschirm warten (VSync)"},
    {"Off", "Aus"},
    {"Low", "Niedrig"},
    {"Medium", "Mittel"},
    {"High (Xbox)", "Hoch (Xbox)"},
    {"Ultra", "Ultra"},
    {"100 % (Xbox)", "100 % (Xbox)"},
    {"Sample the soft focus at full resolution", "Weichzeichner in voller Auflösung abtasten"},
    {"Sharpen menus and interface (HD interface)", "Menüs und Oberfläche schärfen (HD-Oberfläche)"},
    {"Renderer:", "Renderer:"},
    {"Internal resolution:", "Interne Auflösung:"},
    {"Frame limit:", "Bildratenbegrenzung:"},
    {"Post processing:", "Nachbearbeitung:"},
    {"Soft focus:", "Weichzeichner:"},
    {"Graphics", "Grafik"},
    {"Stereo", "Stereo"},
    {"Room reverb (the Xbox's I3DL2 reverb)", "Raumhall (I3DL2-Hall der Xbox)"},
    {"Speakers:", "Lautsprecher:"},
    {"Audio", "Audio"},
    {"Look sensitivity:", "Blickempfindlichkeit:"},
    {"Keys and pad buttons are assigned in the game: Port Options > Controls.",
     "Tasten und Pad-Belegung werden im Spiel eingestellt: Port-Optionen > Steuerung."},
    {"Controls", "Steuerung"},
    {"Camera of the PC version", "Kamera der PC-Version"},
    {"Own camera (placed with the free camera)", "Eigene Kamera (mit der freien Kamera gesetzt)"},
    {"Original camera", "Originalkamera"},
    {"Show the console logs", "Konsolen-Logs anzeigen"},
    {"Main menu camera:", "Hauptmenü-Kamera:"},
    {"Advanced", "Erweitert"},
    {"Save", "Speichern"},
    {"Play", "Spielen"},
    {"Saved.", "Gespeichert."},
    {"The launcher's language changes at its next start.", "Die Sprache des Launchers wechselt beim nächsten Start."},
    {"The settings could not be saved: %1", "Die Einstellungen konnten nicht gespeichert werden: %1"},
    {"USA disc (NTSC retail) installed.", "USA-Disc (NTSC-Verkaufsversion) installiert."},
    {"Game files found, but no USA retail default.xbe.", "Spieldateien gefunden, aber keine default.xbe der USA-Verkaufsversion."},
    {"The game is not installed.", "Das Spiel ist nicht installiert."},
    {"German (from the European disc)", "Deutsch (von der europäischen Disc)"},
    {"%1 is missing beside the launcher.", "%1 fehlt neben dem Launcher."},
    {"Working...", "Wird bearbeitet …"},
    {"Cancel", "Abbrechen"},
    {"European disc image", "Abbild der europäischen Disc"},
    {"Xbox disc images (*.iso *.xiso);;All files (*.*)", "Xbox-Disc-Abbilder (*.iso *.xiso);;Alle Dateien (*.*)"},
    {"The game could not be started: %1", "Das Spiel konnte nicht gestartet werden: %1"},
    {"POPSoT Recomp - Setup", "POPSoT Recomp – Einrichtung"},
    {"Next >", "Weiter >"},
    {"< Back", "< Zurück"},
    {"Finish", "Fertigstellen"},
    {"Welcome", "Willkommen"},
    {"This sets up the PC port of the Xbox version of Prince of Persia: The Sands of Time.\n\nYou need an image (ISO) of your own USA disc. The image of the European disc is optional: it adds the German speech and the German bonus films.\n\nThe game files are copied once into a folder of your choice; the images are not needed afterwards.",
     "Hier wird der PC-Port der Xbox-Version von Prince of Persia: The Sands of Time eingerichtet.\n\nDu brauchst ein Abbild (ISO) deiner eigenen USA-Disc. Das Abbild der europäischen Disc ist optional: Es ergänzt die deutsche Sprachausgabe und die deutschen Bonusfilme.\n\nDie Spieldateien werden einmal in einen Ordner deiner Wahl kopiert; die Abbilder werden danach nicht mehr gebraucht."},
    {"USA disc", "USA-Disc"},
    {"Choose the image of the USA disc (NTSC retail). It is required.", "Wähle das Abbild der USA-Disc (NTSC-Verkaufsversion). Es wird zwingend gebraucht."},
    {"Browse...", "Durchsuchen …"},
    {"USA disc image", "Abbild der USA-Disc"},
    {"European disc (optional)", "Europäische Disc (optional)"},
    {"For the German speech, choose the image of the European disc (PAL). Leave the field empty to keep the English speech; you can add it later on the Game tab.",
     "Für die deutsche Sprachausgabe wähle das Abbild der europäischen Disc (PAL). Lass das Feld leer, um die englische Sprachausgabe zu behalten; du kannst sie später im Reiter Spiel ergänzen."},
    {"The game files (about 3.5 GB) are copied into this folder.", "Die Spieldateien (etwa 3,5 GB) werden in diesen Ordner kopiert."},
    {"Done", "Fertig"},
    {"The folder cannot be created.", "Der Ordner kann nicht angelegt werden."},
    {"The game is installed in\n%1\n\nChoose the settings in the launcher and start the game with Play.",
     "Das Spiel ist installiert in\n%1\n\nWähle die Einstellungen im Launcher und starte das Spiel mit „Spielen“."},
    {"The game is installed.", "Das Spiel ist installiert."},
    {"Installation failed: %1", "Installation fehlgeschlagen: %1"},
    {"Cancel the installation?", "Installation abbrechen?"},
};

const QHash<QString, QString> &table()
{
    static const QHash<QString, QString> t = [] {
        QHash<QString, QString> h;
        for (const auto &row : kTable) h.insert(QString::fromUtf8(row[0]), QString::fromUtf8(row[1]));
        return h;
    }();
    return t;
}
}

void setUiGerman(bool german) { g_german = german; }
bool uiGerman() { return g_german; }

QString T(const char *english)
{
    const QString en = QString::fromUtf8(english);
    if (!g_german) return en;
    return table().value(en, en);
}
