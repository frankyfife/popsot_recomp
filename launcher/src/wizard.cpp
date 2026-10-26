#include "wizard.h"
#include "installer.h"
#include "tr.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

namespace {
QString chooseIso(QWidget *parent, const QString &title)
{
    return QFileDialog::getOpenFileName(parent, title, QString(),
                                        T("Xbox disc images (*.iso *.xiso);;All files (*.*)"));
}
}

QWizardPage *SetupWizard::page(const QString &title, const QString &text)
{
    auto *p = new QWizardPage;
    p->setTitle(title);
    auto *l = new QVBoxLayout(p);
    if (!text.isEmpty()) {
        auto *label = new QLabel(text);
        label->setWordWrap(true);
        l->addWidget(label);
    }
    return p;
}

SetupWizard::SetupWizard(const QString &defaultGameDir, QWidget *parent) : QWizard(parent)
{
    setWindowTitle(T("POPSoT Recomp - Setup"));
    setWizardStyle(QWizard::ModernStyle);
    setPixmap(QWizard::LogoPixmap, QApplication::windowIcon().pixmap(48, 48));
    setOption(QWizard::NoBackButtonOnLastPage);
    setButtonText(QWizard::NextButton, T("Next >"));
    setButtonText(QWizard::BackButton, T("< Back"));
    setButtonText(QWizard::FinishButton, T("Finish"));
    setButtonText(QWizard::CancelButton, T("Cancel"));
    resize(fontMetrics().height() * 46, fontMetrics().height() * 32);

    setPage(Welcome, page(T("Welcome"),
        T("This sets up the PC port of the Xbox version of Prince of Persia: The Sands of Time.\n\n"
          "You need an image (ISO) of your own USA disc. The image of the European disc is optional: "
          "it adds the German speech and the German bonus films.\n\n"
          "The game files are copied once into a folder of your choice; the images are not needed afterwards.")));

    {
        auto *p = page(T("USA disc"), T("Choose the image of the USA disc (NTSC retail). It is required."));
        auto *row = new QHBoxLayout;
        usa_ = new QLineEdit;
        auto *browse = new QPushButton(T("Browse..."));
        row->addWidget(usa_);
        row->addWidget(browse);
        usaNote_ = new QLabel;
        usaNote_->setWordWrap(true);
        static_cast<QVBoxLayout *>(p->layout())->addLayout(row);
        p->layout()->addWidget(usaNote_);
        static_cast<QVBoxLayout *>(p->layout())->addStretch();
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString f = chooseIso(this, T("USA disc image"));
            if (!f.isEmpty()) usa_->setText(QDir::toNativeSeparators(f));
        });
        setPage(UsaPage, p);
    }
    {
        auto *p = page(T("European disc (optional)"),
                       T("For the German speech, choose the image of the European disc (PAL). "
                         "Leave the field empty to keep the English speech; you can add it later on the Game tab."));
        auto *row = new QHBoxLayout;
        pal_ = new QLineEdit;
        auto *browse = new QPushButton(T("Browse..."));
        row->addWidget(pal_);
        row->addWidget(browse);
        palNote_ = new QLabel;
        palNote_->setWordWrap(true);
        static_cast<QVBoxLayout *>(p->layout())->addLayout(row);
        p->layout()->addWidget(palNote_);
        static_cast<QVBoxLayout *>(p->layout())->addStretch();
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString f = chooseIso(this, T("European disc image"));
            if (!f.isEmpty()) pal_->setText(QDir::toNativeSeparators(f));
        });
        setPage(PalPage, p);
    }
    {
        auto *p = page(T("Game folder"), T("The game files (about 3.5 GB) are copied into this folder."));
        auto *row = new QHBoxLayout;
        folder_ = new QLineEdit(QDir::toNativeSeparators(defaultGameDir));
        auto *browse = new QPushButton(T("Browse..."));
        row->addWidget(folder_);
        row->addWidget(browse);
        static_cast<QVBoxLayout *>(p->layout())->addLayout(row);
        static_cast<QVBoxLayout *>(p->layout())->addStretch();
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString d = QFileDialog::getExistingDirectory(this, T("Game folder"), folder_->text());
            if (!d.isEmpty()) folder_->setText(QDir::toNativeSeparators(d));
        });
        setPage(FolderPage, p);
    }
    {
        auto *p = page(T("Installation"), QString());
        bar_ = new QProgressBar;
        bar_->setRange(0, 100);
        installNote_ = new QLabel;
        installNote_->setWordWrap(true);
        p->layout()->addWidget(bar_);
        p->layout()->addWidget(installNote_);
        static_cast<QVBoxLayout *>(p->layout())->addStretch();
        setPage(InstallPage, p);
    }
    {
        auto *p = page(T("Done"), QString());
        doneNote_ = new QLabel;
        doneNote_->setWordWrap(true);
        p->layout()->addWidget(doneNote_);
        static_cast<QVBoxLayout *>(p->layout())->addStretch();
        setPage(DonePage, p);
    }
}

QString SetupWizard::gameDir() const { return QDir::fromNativeSeparators(folder_->text().trimmed()); }

bool SetupWizard::validateCurrentPage()
{
    QString message;
    switch (currentId()) {
    case UsaPage: {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = install::checkUsaIso(usa_->text().trimmed(), &message);
        QApplication::restoreOverrideCursor();
        usaNote_->setText(message);
        return ok;
    }
    case PalPage: {
        if (pal_->text().trimmed().isEmpty()) return true;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = install::checkPalIso(pal_->text().trimmed(), &message);
        QApplication::restoreOverrideCursor();
        palNote_->setText(message);
        return ok;
    }
    case FolderPage:
        if (gameDir().isEmpty() || !QDir().mkpath(gameDir())) {
            QMessageBox::warning(this, windowTitle(), T("The folder cannot be created."));
            return false;
        }
        return true;
    case InstallPage:
        return installDone_ && installOk_;
    default:
        return true;
    }
}

void SetupWizard::initializePage(int id)
{
    QWizard::initializePage(id);
    if (id == InstallPage) startInstall();
    if (id == DonePage)
        doneNote_->setText(T("The game is installed in\n%1\n\nChoose the settings in the launcher and start the game with Play.")
                               .arg(QDir::toNativeSeparators(gameDir())));
}

void SetupWizard::startInstall()
{
    installDone_ = installOk_ = false;
    button(QWizard::NextButton)->setEnabled(false);
    button(QWizard::BackButton)->setEnabled(false);
    thread_ = new QThread(this);
    worker_ = new install::Worker(usa_->text().trimmed(), pal_->text().trimmed(), gameDir());
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::started, worker_, &install::Worker::run);
    connect(worker_, &install::Worker::progress, this, [this](int percent, const QString &text) {
        bar_->setValue(percent);
        installNote_->setText(text);
    });
    connect(worker_, &install::Worker::finished, this, [this](bool ok, const QString &message) {
        installDone_ = true;
        installOk_ = ok;
        germanDone_ = ok && !pal_->text().trimmed().isEmpty();
        bar_->setValue(ok ? 100 : bar_->value());
        installNote_->setText(ok ? T("The game is installed.") : T("Installation failed: %1").arg(message));
        button(QWizard::BackButton)->setEnabled(!ok);
        button(QWizard::NextButton)->setEnabled(ok);
        thread_->quit();
    });
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_->start();
}

void SetupWizard::reject()
{
    if (thread_ && thread_->isRunning()) {
        if (QMessageBox::question(this, windowTitle(), T("Cancel the installation?")) != QMessageBox::Yes) return;
        worker_->cancel();
        thread_->quit();
        thread_->wait();
    }
    QWizard::reject();
}
