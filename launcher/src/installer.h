// Installing the game from the player's discs, on a worker thread.
//
// - The USA disc (NTSC retail, the only executable the port runs): every file
//   except $SystemUpdate is copied into the game folder; its default.xbe must
//   have the SHA-256 the port was built for.
// - The European disc, optional: its German speech replaces the English set
//   (SOUND/SOUNDX4 -> SOUNDX / soundx0, the English archives kept in
//   SOUND/original-english), and its five German bonus films go to VIDEO.
//   After tools/german-speech.py.
#pragma once

#include <QObject>
#include <QString>
#include <atomic>

namespace install {

// What a game folder holds.
struct Status {
    bool game = false;          // prince.bf, VIDEO, SOUND, shaders.bin
    bool usaXbe = false;        // default.xbe is the USA retail one
    bool germanSpeech = false;  // SOUND/original-english exists
    bool germanFilms = false;   // VIDEO/CINE_9014.INT exists
};
Status status(const QString &gameDir);

// The checks the wizard runs on a chosen image before installing.
bool checkUsaIso(const QString &iso, QString *message);
bool checkPalIso(const QString &iso, QString *message);

QString usaXbeSha256();

class Worker : public QObject {
    Q_OBJECT
public:
    Worker(QString usaIso, QString palIso, QString gameDir, bool restoreEnglish = false)
        : usaIso_(std::move(usaIso)), palIso_(std::move(palIso)), gameDir_(std::move(gameDir)),
          restoreEnglish_(restoreEnglish) {}
    void cancel() { cancel_ = true; }

public slots:
    void run();

signals:
    void progress(int percent, const QString &text);
    void finished(bool ok, const QString &message);

private:
    bool installGame(QString *error);
    bool installGerman(QString *error);
    bool restoreEnglish(QString *error);

    QString usaIso_, palIso_, gameDir_;
    bool restoreEnglish_;
    std::atomic<bool> cancel_{false};
};

}
