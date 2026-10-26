// First installation: the USA disc (required), the European disc (optional,
// for the German speech), the game folder, then the copy.
#pragma once

#include <QWizard>

class QLabel;
class QLineEdit;
class QProgressBar;
class QThread;
namespace install { class Worker; }

class SetupWizard : public QWizard {
    Q_OBJECT
public:
    explicit SetupWizard(const QString &defaultGameDir, QWidget *parent = nullptr);
    QString gameDir() const;
    bool germanInstalled() const { return germanDone_; }

protected:
    bool validateCurrentPage() override;
    void initializePage(int id) override;
    void reject() override;

private:
    enum { Welcome, UsaPage, PalPage, FolderPage, InstallPage, DonePage };
    QWizardPage *page(const QString &title, const QString &text);
    void startInstall();

    QLineEdit *usa_ = nullptr, *pal_ = nullptr, *folder_ = nullptr;
    QLabel *usaNote_ = nullptr, *palNote_ = nullptr, *installNote_ = nullptr, *doneNote_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QThread *thread_ = nullptr;
    install::Worker *worker_ = nullptr;
    bool installDone_ = false, installOk_ = false, germanDone_ = false;
};
