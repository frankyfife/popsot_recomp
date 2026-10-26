#include "installer.h"
#include "tr.h"
#include "xiso.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStorageInfo>

namespace install {

namespace {
// default.xbe of the USA disc (source/xbox-retail/target.h POPSOT_XBE_SHA256)
const char kUsaXbe[] = "753C58B191279BF4D1A8B28A936DF36AA222ED566487AB1FBDFCD090E91EE379";
const char kEnglish[] = "sound/english/";
const char kGerman[] = "sound/german/";
const char kBackup[] = "original-english";
const char *const kArchives[] = {"SOUNDX", "soundx0"};
const char *const kFilms[] = {"CINE_9014.INT", "CINE_9034.INT", "CINE_9044.INT", "CINE_9054.INT", "CINE_9064.INT"};

struct FatEntry { QString name; quint32 offset, size; };

// A .fat is a run of entries: u32 offset of the entry in the .fat, u32 offset
// in the .big, u32 size, u32 0, u32 name length with its NUL, the name.
bool parseFat(const QByteArray &data, QList<FatEntry> *out)
{
    qint64 i = 0;
    while (i + 20 <= data.size()) {
        quint32 v[5];
        memcpy(v, data.constData() + i, 20);
        if (v[0] != quint32(i) || v[3] != 0 || i + 20 + v[4] > data.size()) return false;
        QByteArray name = data.mid(i + 20, v[4]);
        while (name.endsWith('\0')) name.chop(1);
        out->append({QString::fromLatin1(name), v[1], v[2]});
        i += 20 + v[4];
    }
    return i == data.size();
}

QString sha256(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toUpper());
}

// a folder entry case-insensitively ("SOUND" or "sound"), or the name as is
QString child(const QString &dir, const QString &name)
{
    const QStringList found = QDir(dir).entryList({name}, QDir::AllEntries | QDir::NoDotAndDotDot);
    return dir + '/' + (found.isEmpty() ? name : found.first());
}
}

QString usaXbeSha256() { return QString::fromLatin1(kUsaXbe); }

Status status(const QString &gameDir)
{
    Status s;
    if (gameDir.isEmpty()) return s;
    QDir d(gameDir);
    s.game = QFileInfo::exists(child(gameDir, QStringLiteral("prince.bf")))
          && QFileInfo(child(gameDir, QStringLiteral("VIDEO"))).isDir()
          && QFileInfo(child(gameDir, QStringLiteral("SOUND"))).isDir()
          && QFileInfo::exists(child(gameDir, QStringLiteral("shaders.bin")));
    QFile xbe(child(gameDir, QStringLiteral("default.xbe")));
    if (xbe.open(QIODevice::ReadOnly))
        s.usaXbe = sha256(xbe.readAll()) == QLatin1String(kUsaXbe);
    s.germanSpeech = QFileInfo(child(child(gameDir, QStringLiteral("SOUND")), QLatin1String(kBackup))).isDir();
    s.germanFilms = QFileInfo::exists(child(child(gameDir, QStringLiteral("VIDEO")), QStringLiteral("CINE_9014.INT")));
    return s;
}

// The title ID in an XBE's certificate (Sands of Time: 0x5553001D on both
// discs; The Two Thrones, also with SOUND/SOUNDX4, is 0x55530058).
static quint32 titleId(const QByteArray &xbe)
{
    if (xbe.size() < 0x120) return 0;
    quint32 base, cert, id = 0;
    memcpy(&base, xbe.constData() + 0x104, 4);
    memcpy(&cert, xbe.constData() + 0x118, 4);
    if (cert < base || cert - base + 12 > quint32(xbe.size())) return 0;
    memcpy(&id, xbe.constData() + (cert - base) + 8, 4);
    return id;
}
static const quint32 kSandsOfTime = 0x5553001D;

bool checkUsaIso(const QString &iso, QString *message)
{
    Xiso x;
    QString err;
    if (!x.open(iso, &err)) { *message = T("This is not an Xbox disc image.") ; return false; }
    XisoEntry xbe;
    if (!x.find(QStringLiteral("default.xbe"), &xbe)) { *message = T("The image has no default.xbe."); return false; }
    const QByteArray data = x.read(xbe);
    if (sha256(data) != QLatin1String(kUsaXbe)) {
        *message = titleId(data) == kSandsOfTime
            ? T("This is Prince of Persia: The Sands of Time, but not the revision the port runs (default.xbe SHA-256 %1...). "
                "It needs the USA disc's executable with SHA-256 753C58B1...; the European disc can add the German speech afterwards.")
                  .arg(sha256(data).left(8))
            : T("This is not the USA disc (NTSC retail). The port runs only that version; the European disc can add the German speech afterwards.");
        return false;
    }
    *message = T("USA disc (NTSC retail) recognised.");
    return true;
}

bool checkPalIso(const QString &iso, QString *message)
{
    Xiso x;
    QString err;
    if (!x.open(iso, &err)) { *message = T("This is not an Xbox disc image."); return false; }
    XisoEntry xbe;
    if (!x.find(QStringLiteral("default.xbe"), &xbe) || titleId(x.read(xbe)) != kSandsOfTime) {
        *message = T("This is not Prince of Persia: The Sands of Time.");
        return false;
    }
    XisoEntry fat;
    if (!x.find(QStringLiteral("SOUND/SOUNDX4.fat"), &fat)) {
        *message = T("The image has no German speech (SOUND/SOUNDX4). Choose the European disc.");
        return false;
    }
    *message = T("European disc with German speech recognised.");
    return true;
}

void Worker::run()
{
    QString error;
    bool ok = true;
    if (restoreEnglish_) ok = restoreEnglish(&error);
    else {
        if (!usaIso_.isEmpty()) ok = installGame(&error);
        if (ok && !palIso_.isEmpty()) ok = installGerman(&error);
    }
    emit finished(ok, ok ? T("Done.") : error);
}

bool Worker::installGame(QString *error)
{
    Xiso x;
    if (!x.open(usaIso_, error)) return false;
    QList<QPair<QString, XisoEntry>> files;
    qint64 total = 0;
    x.walk(QString(), [&](const QString &dir, const XisoEntry &e) {
        // the dashboard's update files are not the game's
        if (dir.startsWith(QLatin1String("$SystemUpdate"), Qt::CaseInsensitive)) return;
        if (dir.isEmpty() && (e.name.compare(QLatin1String("update.xbe"), Qt::CaseInsensitive) == 0
                              || e.name.compare(QLatin1String("dashupdate.xbe"), Qt::CaseInsensitive) == 0)) return;
        files.append({dir.isEmpty() ? e.name : dir + '/' + e.name, e});
        total += e.size;
    });
    QDir().mkpath(gameDir_);
    QStorageInfo disk(gameDir_);
    if (disk.isValid() && disk.bytesAvailable() < total + (64 << 20)) {
        *error = T("Not enough free space: %1 GB needed.").arg(double(total) / (1 << 30), 0, 'f', 1);
        return false;
    }
    qint64 done = 0;
    for (const auto &f : files) {
        const QString dest = gameDir_ + '/' + f.first;
        if (QFileInfo(dest).size() == f.second.size) {           // already there
            done += f.second.size;
            continue;
        }
        if (!x.extract(f.second, dest, error, [&](qint64 n) {
                done += n;
                emit progress(int(done * 100 / qMax<qint64>(total, 1)),
                              T("Installing the game: %1").arg(f.first));
                return !cancel_.load();
            }))
            return false;
    }
    if (!status(gameDir_).usaXbe) {
        *error = T("The installed default.xbe is not the USA retail executable.");
        return false;
    }
    return true;
}

bool Worker::installGerman(QString *error)
{
    const QString sound = child(gameDir_, QStringLiteral("SOUND"));
    const QString backup = sound + '/' + kBackup;
    Xiso x;
    if (!x.open(palIso_, error)) return false;
    emit progress(0, T("Reading the German speech from the European disc..."));

    if (!QFileInfo(backup).isDir()) {
        XisoEntry fatE, bigE;
        if (!x.find(QStringLiteral("SOUND/SOUNDX4.fat"), &fatE) || !x.find(QStringLiteral("SOUND/SOUNDX4.big"), &bigE)) {
            *error = T("The European disc has no SOUND/SOUNDX4.");
            return false;
        }
        QList<FatEntry> german;
        if (!parseFat(x.read(fatE), &german)) { *error = QStringLiteral("SOUNDX4.fat unreadable"); return false; }
        const QByteArray germanBig = x.read(bigE);
        QHash<QString, QByteArray> byName;
        for (const FatEntry &e : german) {
            if (!e.name.startsWith(QLatin1String(kGerman))) { *error = QStringLiteral("SOUNDX4 holds ") + e.name; return false; }
            byName.insert(e.name.mid(int(strlen(kGerman))), germanBig.mid(e.offset, e.size));
        }
        QList<FatEntry> source[2];
        for (int a = 0; a < 2; a++) {
            QFile fat(child(sound, QString::fromLatin1(kArchives[a]) + QStringLiteral(".fat")));
            if (!fat.open(QIODevice::ReadOnly) || !parseFat(fat.readAll(), &source[a])) {
                *error = T("The installed game has no readable %1.fat.").arg(QLatin1String(kArchives[a]));
                return false;
            }
        }
        for (int a = 0; a < 2; a++)
            for (const FatEntry &e : source[a])
                if (e.name.startsWith(QLatin1String(kEnglish)) && !byName.contains(e.name.mid(int(strlen(kEnglish))))) {
                    *error = T("No German file for %1.").arg(e.name);
                    return false;
                }
        QDir().mkpath(backup);
        for (int a = 0; a < 2; a++)
            for (const char *ext : {".fat", ".big"}) {
                const QString name = QString::fromLatin1(kArchives[a]) + QLatin1String(ext);
                if (!QFile::rename(child(sound, name), backup + '/' + name)) {
                    *error = T("Could not move %1 aside.").arg(name);
                    return false;
                }
            }
        for (int a = 0; a < 2; a++) {
            emit progress(30 + 30 * a, T("Writing %1 with the German speech...").arg(QLatin1String(kArchives[a])));
            QFile oldBig(backup + '/' + kArchives[a] + QStringLiteral(".big"));
            QFile big(sound + '/' + kArchives[a] + QStringLiteral(".big.tmp"));
            if (!oldBig.open(QIODevice::ReadOnly) || !big.open(QIODevice::WriteOnly)) {
                *error = big.errorString();
                return false;
            }
            QByteArray fat;
            quint32 offset = 0;
            for (const FatEntry &e : source[a]) {
                QByteArray data;
                if (e.name.startsWith(QLatin1String(kEnglish))) data = byName.value(e.name.mid(int(strlen(kEnglish))));
                else { oldBig.seek(e.offset); data = oldBig.read(e.size); }
                QByteArray raw = e.name.toLatin1() + '\0';
                quint32 head[5] = {quint32(fat.size()), offset, quint32(data.size()), 0, quint32(raw.size())};
                fat.append(reinterpret_cast<const char *>(head), 20);
                fat.append(raw);
                big.write(data);
                offset += quint32(data.size());
            }
            big.close();
            QFile fatFile(sound + '/' + kArchives[a] + QStringLiteral(".fat.tmp"));
            if (!fatFile.open(QIODevice::WriteOnly) || fatFile.write(fat) != fat.size()) { *error = fatFile.errorString(); return false; }
            fatFile.close();
            for (const char *ext : {".big", ".fat"}) {
                const QString final = sound + '/' + kArchives[a] + QLatin1String(ext);
                QFile::remove(final);
                QFile::rename(final + QStringLiteral(".tmp"), final);
            }
        }
    }
    // the German bonus films (the extras with German subtitles)
    const QString video = child(gameDir_, QStringLiteral("VIDEO"));
    int n = 0;
    for (const char *film : kFilms) {
        emit progress(70 + 6 * n++, T("Copying the German bonus films..."));
        XisoEntry e;
        if (!x.find(QStringLiteral("VIDEO/30fps/") + QLatin1String(film), &e)) continue;
        const QString dest = video + '/' + film;
        if (QFileInfo(dest).size() == e.size) continue;
        if (!x.extract(e, dest, error, [&](qint64) { return !cancel_.load(); })) return false;
    }
    emit progress(100, T("German speech installed."));
    return true;
}

bool Worker::restoreEnglish(QString *error)
{
    const QString sound = child(gameDir_, QStringLiteral("SOUND"));
    const QString backup = sound + '/' + kBackup;
    if (!QFileInfo(backup).isDir()) return true;
    for (const char *a : kArchives)
        for (const char *ext : {".fat", ".big"}) {
            const QString name = QLatin1String(a) + QLatin1String(ext);
            QFile::remove(sound + '/' + name);
            if (!QFile::rename(backup + '/' + name, sound + '/' + name)) {
                *error = T("Could not restore %1.").arg(name);
                return false;
            }
        }
    QDir().rmdir(backup);
    for (const char *film : kFilms)
        QFile::remove(child(gameDir_, QStringLiteral("VIDEO")) + '/' + film);
    return true;
}

}
