// Reading an Xbox disc image (XDVDFS): enough to list it and copy files out.
// After port/third_party/xboxrecomp/tools/xiso/xdvdfs.py: the descriptor at
// sector 32 of the game partition, which starts at one of a few known bases
// (bare XISO, redump with the video partition first, ...).
#pragma once

#include <QFile>
#include <QList>
#include <QString>
#include <functional>

struct XisoEntry {
    QString name;
    quint32 sector = 0;
    quint32 size = 0;
    quint8 attributes = 0;
    bool isDir() const { return attributes & 0x10; }
};

class Xiso {
public:
    bool open(const QString &path, QString *error);
    // Entries of a directory ("" = root, "SOUND", "VIDEO/30fps"), matched
    // case-insensitively.
    QList<XisoEntry> list(const QString &path, bool *ok = nullptr);
    // One file by its path, case-insensitively; false when absent.
    bool find(const QString &path, XisoEntry *out);
    QByteArray read(const XisoEntry &e);
    // Copies one file; progress(bytes) after every chunk, false = cancel.
    bool extract(const XisoEntry &e, const QString &dest, QString *error,
                 const std::function<bool(qint64)> &progress = {});
    // Every file below path: (directory, entry).
    void walk(const QString &path, const std::function<void(const QString &, const XisoEntry &)> &visit);

private:
    QList<XisoEntry> readDir(quint32 sector, quint32 size);
    QFile file_;
    qint64 base_ = 0;
    quint32 rootSector_ = 0, rootSize_ = 0;
};
