// The launcher: the installation, the settings of popsot-options.ini on
// tabs, and Play.
#pragma once

#include "ini.h"

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    // Opens the setup wizard when no installed game is found.
    void setupIfNeeded();

private:
    void build();
    void load();
    bool save();
    void refreshStatus();
    void play();
    void runWizard();
    void installGerman();
    void restoreEnglish();
    void runWorker(const QString &palIso, bool restore);
    QString gameDir() const;
    QString defaultGameDir() const;

    IniFile ini_;
    QString dir_;                      // the launcher's and the game executable's folder
    QLabel *status_ = nullptr, *path_ = nullptr, *speech_ = nullptr, *footer_ = nullptr;
    QPushButton *germanButton_ = nullptr, *englishButton_ = nullptr, *play_ = nullptr;
    QComboBox *uiLang_ = nullptr, *textLang_ = nullptr, *movieLang_ = nullptr;
    QComboBox *renderer_ = nullptr, *resolution_ = nullptr, *fps_ = nullptr, *post_ = nullptr, *softFocus_ = nullptr;
    QCheckBox *vsync_ = nullptr, *hdUi_ = nullptr, *sharpSoftFocus_ = nullptr;
    QComboBox *speakers_ = nullptr, *look_ = nullptr, *menuCam_ = nullptr;
    QCheckBox *reverb_ = nullptr, *consoleLogs_ = nullptr;
};
