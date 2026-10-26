#include "ini.h"

#include <QFile>
#include <QSaveFile>

void IniFile::load()
{
    lines_.clear();
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QString text = QString::fromLatin1(f.readAll());
    lines_ = text.split('\n');
    for (QString &l : lines_)
        if (l.endsWith('\r')) l.chop(1);
    while (!lines_.isEmpty() && lines_.last().isEmpty()) lines_.removeLast();
}

bool IniFile::save(QString *error)
{
    QSaveFile f(path_);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write((lines_.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n")).toLatin1());
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

// The line of section/key, or -1; *sectionEnd: where a new key of that
// section goes (-1 when the section is absent).
int IniFile::find(const QString &section, const QString &key, int *sectionEnd) const
{
    bool in = false;
    *sectionEnd = -1;
    for (int i = 0; i < lines_.size(); i++) {
        const QString l = lines_[i].trimmed();
        if (l.startsWith('[') && l.endsWith(']')) {
            in = l.mid(1, l.size() - 2).compare(section, Qt::CaseInsensitive) == 0;
            if (in) *sectionEnd = i + 1;
            continue;
        }
        if (!in || l.isEmpty() || l.startsWith(';')) continue;
        *sectionEnd = i + 1;
        const int eq = l.indexOf('=');
        if (eq > 0 && l.left(eq).trimmed().compare(key, Qt::CaseInsensitive) == 0) return i;
    }
    return -1;
}

QString IniFile::get(const QString &section, const QString &key, const QString &fallback) const
{
    int end;
    const int i = find(section, key, &end);
    if (i < 0) return fallback;
    const QString &l = lines_[i];
    return l.mid(l.indexOf('=') + 1).trimmed();
}

int IniFile::getInt(const QString &section, const QString &key, int fallback) const
{
    bool ok = false;
    const int v = get(section, key).toInt(&ok);
    return ok ? v : fallback;
}

void IniFile::set(const QString &section, const QString &key, const QString &value)
{
    int end;
    const int i = find(section, key, &end);
    const QString line = key + '=' + value;
    if (i >= 0) lines_[i] = line;
    else if (end >= 0) lines_.insert(end, line);
    else {
        lines_.append('[' + section + ']');
        lines_.append(line);
    }
}
