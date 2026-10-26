// popsot-options.ini, read and written line by line: what the launcher does
// not set (key bindings, the game's own entries, comments) stays as it is.
// Windows profile style ([Section], Key=Value), Latin-1 like the game's
// GetPrivateProfileString.
#pragma once

#include <QString>
#include <QStringList>

class IniFile {
public:
    explicit IniFile(QString path) : path_(std::move(path)) {}
    void load();
    bool save(QString *error);
    QString get(const QString &section, const QString &key, const QString &fallback = {}) const;
    int getInt(const QString &section, const QString &key, int fallback) const;
    void set(const QString &section, const QString &key, const QString &value);
    void set(const QString &section, const QString &key, int value) { set(section, key, QString::number(value)); }
    const QString &path() const { return path_; }

private:
    int find(const QString &section, const QString &key, int *sectionEnd) const;
    QString path_;
    QStringList lines_;
};
