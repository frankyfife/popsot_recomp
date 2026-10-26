#include "mainwindow.h"
#include "installer.h"
#include "tr.h"
#include "wizard.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QTabWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace {
const char kGameExe[] = "sot-xbox-retail.exe";

QComboBox *combo(const QList<QPair<QString, QVariant>> &items)
{
    auto *c = new QComboBox;
    for (const auto &i : items) c->addItem(i.first, i.second);
    return c;
}

void select(QComboBox *c, const QVariant &v)
{
    const int i = c->findData(v);
    c->setCurrentIndex(i >= 0 ? i : 0);
}

QWidget *tab(QFormLayout **form)
{
    auto *w = new QWidget;
    auto *outer = new QVBoxLayout(w);
    *form = new QFormLayout;
    (*form)->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    outer->addLayout(*form);
    outer->addStretch();
    return w;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ini_(QCoreApplication::applicationDirPath() + QStringLiteral("/popsot-options.ini")),
      dir_(QCoreApplication::applicationDirPath())
{
    ini_.load();
    setUiGerman(ini_.get(QStringLiteral("Launcher"), QStringLiteral("Language"), QStringLiteral("german")) != QLatin1String("english"));
    build();
    load();
    refreshStatus();
}

QString MainWindow::defaultGameDir() const
{
    // a source checkout: the game files beside bin\ (tools/run.ps1's folder)
    const QString dev = QDir::cleanPath(dir_ + QStringLiteral("/../Assets/xbox-retail"));
    if (install::status(dev).game) return dev;
    return dir_ + QStringLiteral("/Game");
}

QString MainWindow::gameDir() const
{
    const QString d = ini_.get(QStringLiteral("Launcher"), QStringLiteral("GameDir"));
    return d.isEmpty() ? defaultGameDir() : d;
}

void MainWindow::build()
{
    setWindowTitle(T("POPSoT Recomp Launcher"));
    const int unit = fontMetrics().height();
    auto *central = new QWidget;
    auto *v = new QVBoxLayout(central);

    // header: icon, name, the launcher's language
    auto *header = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(QApplication::windowIcon().pixmap(48, 48));
    header->addWidget(icon);
    auto *titles = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("POPSoT Recomp"));
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.9);
    tf.setBold(true);
    title->setFont(tf);
    auto *subtitle = new QLabel(T("Prince of Persia: The Sands of Time - PC port of the Xbox version"));
    subtitle->setEnabled(false);
    titles->addWidget(title);
    titles->addWidget(subtitle);
    header->addLayout(titles, 1);
    auto *langForm = new QFormLayout;
    uiLang_ = combo({{QStringLiteral("Deutsch"), QStringLiteral("german")}, {QStringLiteral("English"), QStringLiteral("english")}});
    select(uiLang_, uiGerman() ? QStringLiteral("german") : QStringLiteral("english"));
    langForm->addRow(T("Language:"), uiLang_);
    header->addLayout(langForm);
    v->addLayout(header);

    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    v->addWidget(tabs, 1);

    // Game: the installation and the languages
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        auto *installBox = new QGroupBox(T("Installation"));
        auto *il = new QVBoxLayout(installBox);
        status_ = new QLabel;
        path_ = new QLabel;
        path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        il->addWidget(status_);
        il->addWidget(path_);
        auto *row = new QHBoxLayout;
        auto *setup = new QPushButton(T("Install from ISO..."));
        auto *open = new QPushButton(T("Open game folder"));
        auto *choose = new QPushButton(T("Choose game folder..."));
        row->addWidget(setup);
        row->addWidget(choose);
        row->addWidget(open);
        row->addStretch();
        il->addLayout(row);
        connect(setup, &QPushButton::clicked, this, &MainWindow::runWizard);
        connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(gameDir())); });
        connect(choose, &QPushButton::clicked, this, [this] {
            const QString d = QFileDialog::getExistingDirectory(this, T("Game folder"), gameDir());
            if (d.isEmpty()) return;
            ini_.set(QStringLiteral("Launcher"), QStringLiteral("GameDir"), QDir::toNativeSeparators(d));
            save();
            refreshStatus();
        });
        l->addWidget(installBox);

        auto *langBox = new QGroupBox(T("Language"));
        auto *lf = new QFormLayout(langBox);
        textLang_ = combo({{T("German"), QStringLiteral("german")}, {T("English"), QStringLiteral("english")}});
        movieLang_ = combo({{T("German"), QStringLiteral("german")}, {T("English"), QStringLiteral("english")}});
        lf->addRow(T("Text and menus:"), textLang_);
        lf->addRow(T("Subtitles of the films:"), movieLang_);
        speech_ = new QLabel;
        auto *speechRow = new QHBoxLayout;
        germanButton_ = new QPushButton(T("Install German speech from the European disc..."));
        englishButton_ = new QPushButton(T("Restore English speech"));
        speechRow->addWidget(speech_, 1);
        speechRow->addWidget(germanButton_);
        speechRow->addWidget(englishButton_);
        lf->addRow(T("Speech:"), speechRow);
        connect(germanButton_, &QPushButton::clicked, this, &MainWindow::installGerman);
        connect(englishButton_, &QPushButton::clicked, this, &MainWindow::restoreEnglish);
        l->addWidget(langBox);
        l->addStretch();
        tabs->addTab(w, T("Game"));
    }
    // Graphics
    {
        QFormLayout *f;
        QWidget *w = tab(&f);
        renderer_ = combo({{QStringLiteral("Direct3D 11"), QStringLiteral("d3d11")},
                           {QStringLiteral("Direct3D 12"), QStringLiteral("d3d12")},
                           {QStringLiteral("Vulkan"), QStringLiteral("vulkan")}});
        resolution_ = combo({{QStringLiteral("240p (0.5x)"), 50}, {QStringLiteral("360p (0.75x)"), 75},
                             {QStringLiteral("480p (1x, Xbox)"), 100}, {QStringLiteral("720p (1.5x)"), 150},
                             {QStringLiteral("1080p (2.25x)"), 225}, {QStringLiteral("1440p (3x)"), 300},
                             {QStringLiteral("2160p (4.5x)"), 450}});
        fps_ = combo({{QStringLiteral("30"), 30}, {QStringLiteral("60"), 60}, {QStringLiteral("120"), 120}, {T("Unlimited"), 0}});
        vsync_ = new QCheckBox(T("Wait for the display (VSync)"));
        post_ = combo({{T("Off"), 0}, {T("Low"), 1}, {T("Medium"), 2}, {T("High (Xbox)"), 3}, {T("Ultra"), 4}});
        softFocus_ = combo({{T("Off"), 0}, {QStringLiteral("25 %"), 25}, {QStringLiteral("50 %"), 50},
                            {QStringLiteral("75 %"), 75}, {T("100 % (Xbox)"), 100}});
        sharpSoftFocus_ = new QCheckBox(T("Sample the soft focus at full resolution"));
        hdUi_ = new QCheckBox(T("Sharpen menus and interface (HD interface)"));
        f->addRow(T("Renderer:"), renderer_);
        f->addRow(T("Internal resolution:"), resolution_);
        f->addRow(T("Frame limit:"), fps_);
        f->addRow(QString(), vsync_);
        f->addRow(T("Post processing:"), post_);
        f->addRow(T("Soft focus:"), softFocus_);
        f->addRow(QString(), sharpSoftFocus_);
        f->addRow(QString(), hdUi_);
        tabs->addTab(w, T("Graphics"));
    }
    // Audio
    {
        QFormLayout *f;
        QWidget *w = tab(&f);
        speakers_ = combo({{T("Stereo"), QStringLiteral("stereo")}, {QStringLiteral("5.1"), QStringLiteral("5.1")}});
        reverb_ = new QCheckBox(T("Room reverb (the Xbox's I3DL2 reverb)"));
        f->addRow(T("Speakers:"), speakers_);
        f->addRow(QString(), reverb_);
        tabs->addTab(w, T("Audio"));
    }
    // Controls
    {
        QFormLayout *f;
        QWidget *w = tab(&f);
        look_ = new QComboBox;
        for (int p = 25; p <= 200; p += 25) look_->addItem(QStringLiteral("%1 %").arg(p), p);
        f->addRow(T("Look sensitivity:"), look_);
        auto *note = new QLabel(T("Keys and pad buttons are assigned in the game: Port Options > Controls."));
        note->setWordWrap(true);
        f->addRow(QString(), note);
        tabs->addTab(w, T("Controls"));
    }
    // Advanced
    {
        QFormLayout *f;
        QWidget *w = tab(&f);
        menuCam_ = combo({{T("Camera of the PC version"), QStringLiteral("pc")},
                          {T("Own camera (placed with the free camera)"), QStringLiteral("own")},
                          {T("Original camera"), QStringLiteral("original")}});
        consoleLogs_ = new QCheckBox(T("Show the console logs"));
        f->addRow(T("Main menu camera:"), menuCam_);
        f->addRow(QString(), consoleLogs_);
        tabs->addTab(w, T("Advanced"));
    }

    auto *footer = new QHBoxLayout;
    footer_ = new QLabel;
    footer->addWidget(footer_, 1);
    auto *saveButton = new QPushButton(T("Save"));
    play_ = new QPushButton(T("Play"));
    QFont pf = play_->font();
    pf.setPointSizeF(pf.pointSizeF() * 1.35);
    pf.setBold(true);
    play_->setFont(pf);
    play_->setDefault(true);
    play_->setMinimumWidth(unit * 9);
    footer->addWidget(saveButton);
    footer->addWidget(play_);
    v->addLayout(footer);
    connect(saveButton, &QPushButton::clicked, this, [this] {
        if (save()) footer_->setText(T("Saved."));
    });
    connect(play_, &QPushButton::clicked, this, &MainWindow::play);
    connect(uiLang_, &QComboBox::currentIndexChanged, this, [this] {
        ini_.set(QStringLiteral("Launcher"), QStringLiteral("Language"), uiLang_->currentData().toString());
        save();
        footer_->setText(T("The launcher's language changes at its next start."));
    });

    setCentralWidget(central);
    resize(unit * 52, unit * 36);
}

void MainWindow::load()
{
    const QString G = QStringLiteral("Graphics");
    select(textLang_, ini_.get(QStringLiteral("Language"), QStringLiteral("Text"), QStringLiteral("german")));
    select(movieLang_, ini_.get(QStringLiteral("Language"), QStringLiteral("Movies"), QStringLiteral("german")));
    select(renderer_, ini_.get(G, QStringLiteral("Renderer"), QStringLiteral("d3d11")).toLower());
    select(resolution_, ini_.getInt(G, QStringLiteral("InternalResolutionPercent"), 150));
    select(fps_, ini_.getInt(G, QStringLiteral("FPSLimit"), 60));
    vsync_->setChecked(ini_.getInt(G, QStringLiteral("VSync"), 0) != 0);
    select(post_, ini_.getInt(G, QStringLiteral("PostProcessingQuality"), 4));
    const int strength = ini_.getInt(G, QStringLiteral("SoftFocusStrength"), 100);
    select(softFocus_, ini_.getInt(G, QStringLiteral("TextureGlowEffect"), 1) ? (strength + 12) / 25 * 25 : 0);
    sharpSoftFocus_->setChecked(ini_.getInt(G, QStringLiteral("SoftFocus"), 1) != 0);
    hdUi_->setChecked(ini_.getInt(G, QStringLiteral("HDInterface"), 1) != 0);
    select(speakers_, ini_.get(QStringLiteral("Audio"), QStringLiteral("Speakers"), QStringLiteral("stereo")));
    reverb_->setChecked(ini_.getInt(QStringLiteral("Audio"), QStringLiteral("Reverb"), 1) != 0);
    select(look_, ini_.getInt(QStringLiteral("Controls"), QStringLiteral("LookSensitivityPercent"), 200));
    select(menuCam_, ini_.get(QStringLiteral("Menu"), QStringLiteral("CameraMode"), QStringLiteral("pc")));
    consoleLogs_->setChecked(ini_.getInt(QStringLiteral("Diagnostics"), QStringLiteral("ConsoleLogs"), 0) != 0);
}

bool MainWindow::save()
{
    const QString G = QStringLiteral("Graphics");
    ini_.set(QStringLiteral("Language"), QStringLiteral("Text"), textLang_->currentData().toString());
    ini_.set(QStringLiteral("Language"), QStringLiteral("Movies"), movieLang_->currentData().toString());
    ini_.set(G, QStringLiteral("Renderer"), renderer_->currentData().toString());
    ini_.set(G, QStringLiteral("InternalResolutionPercent"), resolution_->currentData().toInt());
    ini_.set(G, QStringLiteral("FPSLimit"), fps_->currentData().toInt());
    ini_.set(G, QStringLiteral("VSync"), vsync_->isChecked() ? 1 : 0);
    ini_.set(G, QStringLiteral("PostProcessingQuality"), post_->currentData().toInt());
    const int sf = softFocus_->currentData().toInt();
    ini_.set(G, QStringLiteral("TextureGlowEffect"), sf ? 1 : 0);
    if (sf) ini_.set(G, QStringLiteral("SoftFocusStrength"), sf);
    ini_.set(G, QStringLiteral("SoftFocus"), sharpSoftFocus_->isChecked() ? 1 : 0);
    ini_.set(G, QStringLiteral("HDInterface"), hdUi_->isChecked() ? 1 : 0);
    ini_.set(QStringLiteral("Audio"), QStringLiteral("Speakers"), speakers_->currentData().toString());
    ini_.set(QStringLiteral("Audio"), QStringLiteral("Reverb"), reverb_->isChecked() ? 1 : 0);
    ini_.set(QStringLiteral("Controls"), QStringLiteral("LookSensitivityPercent"), look_->currentData().toInt());
    ini_.set(QStringLiteral("Menu"), QStringLiteral("CameraMode"), menuCam_->currentData().toString());
    ini_.set(QStringLiteral("Diagnostics"), QStringLiteral("ConsoleLogs"), consoleLogs_->isChecked() ? 1 : 0);
    QString error;
    if (!ini_.save(&error)) {
        QMessageBox::warning(this, windowTitle(), T("The settings could not be saved: %1").arg(error));
        return false;
    }
    return true;
}

void MainWindow::refreshStatus()
{
    const install::Status s = install::status(gameDir());
    const bool exe = QFileInfo::exists(dir_ + '/' + kGameExe);
    if (s.game && s.usaXbe)
        status_->setText(QStringLiteral("<span style='color:#2e9e3e'>&#10003;</span> ") + T("USA disc (NTSC retail) installed."));
    else if (s.game)
        status_->setText(QStringLiteral("<span style='color:#c89000'>&#9888;</span> ") + T("Game files found, but no USA retail default.xbe."));
    else
        status_->setText(QStringLiteral("<span style='color:#c42b1c'>&#10007;</span> ") + T("The game is not installed."));
    path_->setText(QDir::toNativeSeparators(gameDir()));
    speech_->setText(s.germanSpeech ? T("German (from the European disc)") : T("English"));
    germanButton_->setVisible(s.game && !s.germanSpeech);
    englishButton_->setVisible(s.game && s.germanSpeech);
    play_->setEnabled(s.game && exe);
    if (!exe) footer_->setText(T("%1 is missing beside the launcher.").arg(QLatin1String(kGameExe)));
}

void MainWindow::setupIfNeeded()
{
    if (!install::status(gameDir()).game) runWizard();
}

void MainWindow::runWizard()
{
    SetupWizard w(install::status(gameDir()).game ? gameDir() : defaultGameDir(), this);
    if (w.exec() == QDialog::Accepted) {
        ini_.set(QStringLiteral("Launcher"), QStringLiteral("GameDir"), QDir::toNativeSeparators(w.gameDir()));
        save();
    }
    refreshStatus();
}

void MainWindow::runWorker(const QString &palIso, bool restore)
{
    QProgressDialog dlg(T("Working..."), T("Cancel"), 0, 100, this);
    dlg.setWindowTitle(windowTitle());
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    QThread thread;
    auto *worker = new install::Worker(QString(), palIso, gameDir(), restore);
    worker->moveToThread(&thread);
    bool ok = false;
    QString message;
    connect(&thread, &QThread::started, worker, &install::Worker::run);
    connect(worker, &install::Worker::progress, &dlg, [&dlg](int p, const QString &t) {
        dlg.setValue(p);
        dlg.setLabelText(t);
    });
    connect(worker, &install::Worker::finished, &dlg, [&](bool o, const QString &m) {
        ok = o;
        message = m;
        thread.quit();
    });
    connect(&dlg, &QProgressDialog::canceled, &dlg, [worker] { worker->cancel(); });
    thread.start();
    while (thread.isRunning()) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    delete worker;
    dlg.close();
    if (!ok) QMessageBox::warning(this, windowTitle(), message);
    refreshStatus();
}

void MainWindow::installGerman()
{
    const QString iso = QFileDialog::getOpenFileName(this, T("European disc image"), QString(),
                                                     T("Xbox disc images (*.iso *.xiso);;All files (*.*)"));
    if (iso.isEmpty()) return;
    QString message;
    if (!install::checkPalIso(iso, &message)) {
        QMessageBox::warning(this, windowTitle(), message);
        return;
    }
    runWorker(iso, false);
}

void MainWindow::restoreEnglish()
{
    runWorker(QString(), true);
}

void MainWindow::play()
{
    if (!save()) return;
    QProcess p;
    p.setProgram(dir_ + '/' + kGameExe);
    p.setWorkingDirectory(dir_);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("POPSOT_ASSET_DIR"), QDir::toNativeSeparators(gameDir()));
    env.insert(QStringLiteral("POPSOT_FROM_LAUNCHER"), QStringLiteral("1"));
    p.setProcessEnvironment(env);
    if (!p.startDetached()) {
        QMessageBox::warning(this, windowTitle(), T("The game could not be started: %1").arg(p.errorString()));
        return;
    }
    close();
}
