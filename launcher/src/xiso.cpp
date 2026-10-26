#include "xiso.h"

#include <QDir>
#include <QFileInfo>
#include <cstring>

namespace {
constexpr qint64 kSector = 2048;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
// bare game partition, rebuilt images, redump (video partition first), the
// full disc layout, a redump full-disc dump
constexpr qint64 kBases[] = {0x00000000, 0x0000FD90, 0x00030600, 0x0FD90000, 0x18300000};

quint32 u32(const char *p) { quint32 v; std::memcpy(&v, p, 4); return v; }
quint16 u16(const char *p) { quint16 v; std::memcpy(&v, p, 2); return v; }
}

bool Xiso::open(const QString &path, QString *error)
{
    file_.setFileName(path);
    if (!file_.open(QIODevice::ReadOnly)) {
        if (error) *error = file_.errorString();
        return false;
    }
    for (qint64 base : kBases) {
        if (!file_.seek(base + 32 * kSector)) continue;
        QByteArray d = file_.read(kSector);
        if (d.size() == kSector && d.startsWith(kMagic)) {
            base_ = base;
            rootSector_ = u32(d.constData() + 0x14);
            rootSize_ = u32(d.constData() + 0x18);
            return true;
        }
    }
    if (error) *error = QStringLiteral("not an Xbox disc image");
    file_.close();
    return false;
}

QList<XisoEntry> Xiso::readDir(quint32 sector, quint32 size)
{
    QList<XisoEntry> out;
    if (!file_.seek(base_ + qint64(sector) * kSector)) return out;
    QByteArray data = file_.read(size);
    // the tree is also laid out entry by entry in each sector: a linear walk
    // with a resync per sector reads all of them without following links
    for (qint64 at = 0; at < data.size(); at += kSector) {
        const char *chunk = data.constData() + at;
        qint64 len = qMin<qint64>(kSector, data.size() - at), off = 0;
        while (off + 0x0E <= len) {
            quint16 l = u16(chunk + off), r = u16(chunk + off + 2);
            quint8 nlen = quint8(chunk[off + 0x0D]);
            if ((l == 0xFFFF && r == 0xFFFF) || nlen == 0 || off + 0x0E + nlen > len) break;
            XisoEntry e;
            e.sector = u32(chunk + off + 4);
            e.size = u32(chunk + off + 8);
            e.attributes = quint8(chunk[off + 0x0C]);
            e.name = QString::fromLatin1(chunk + off + 0x0E, nlen);
            out.append(e);
            off += (0x0E + nlen + 3) & ~3;
        }
    }
    return out;
}

QList<XisoEntry> Xiso::list(const QString &path, bool *ok)
{
    quint32 sector = rootSector_, size = rootSize_;
    for (const QString &part : QString(path).replace('\\', '/').split('/', Qt::SkipEmptyParts)) {
        bool found = false;
        for (const XisoEntry &e : readDir(sector, size))
            if (e.isDir() && e.name.compare(part, Qt::CaseInsensitive) == 0) {
                sector = e.sector; size = e.size; found = true;
                break;
            }
        if (!found) {
            if (ok) *ok = false;
            return {};
        }
    }
    if (ok) *ok = true;
    return readDir(sector, size);
}

bool Xiso::find(const QString &path, XisoEntry *out)
{
    QString p = QString(path).replace('\\', '/');
    int slash = p.lastIndexOf('/');
    bool ok = false;
    const QList<XisoEntry> entries = list(slash < 0 ? QString() : p.left(slash), &ok);
    if (!ok) return false;
    const QString name = slash < 0 ? p : p.mid(slash + 1);
    for (const XisoEntry &e : entries)
        if (e.name.compare(name, Qt::CaseInsensitive) == 0) {
            *out = e;
            return true;
        }
    return false;
}

QByteArray Xiso::read(const XisoEntry &e)
{
    if (!file_.seek(base_ + qint64(e.sector) * kSector)) return {};
    return file_.read(e.size);
}

bool Xiso::extract(const XisoEntry &e, const QString &dest, QString *error,
                   const std::function<bool(qint64)> &progress)
{
    QDir().mkpath(QFileInfo(dest).absolutePath());
    QFile out(dest + QStringLiteral(".part"));
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = out.errorString();
        return false;
    }
    if (!file_.seek(base_ + qint64(e.sector) * kSector)) {
        if (error) *error = QStringLiteral("image truncated");
        return false;
    }
    qint64 left = e.size;
    while (left > 0) {
        QByteArray chunk = file_.read(qMin<qint64>(left, 4 << 20));
        if (chunk.isEmpty()) {
            if (error) *error = QStringLiteral("image truncated reading %1").arg(e.name);
            out.remove();
            return false;
        }
        if (out.write(chunk) != chunk.size()) {
            if (error) *error = out.errorString();
            out.remove();
            return false;
        }
        left -= chunk.size();
        if (progress && !progress(chunk.size())) {
            out.remove();
            if (error) *error = QStringLiteral("cancelled");
            return false;
        }
    }
    out.close();
    QFile::remove(dest);
    if (!out.rename(dest)) {
        if (error) *error = out.errorString();
        return false;
    }
    return true;
}

void Xiso::walk(const QString &path, const std::function<void(const QString &, const XisoEntry &)> &visit)
{
    for (const XisoEntry &e : list(path)) {
        const QString sub = path.isEmpty() ? e.name : path + '/' + e.name;
        if (e.isDir()) walk(sub, visit);
        else visit(path, e);
    }
}
