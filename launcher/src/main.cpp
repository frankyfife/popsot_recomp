// POPSoT Launcher: installs the game from the player's discs, sets the
// options of popsot-options.ini and starts sot-xbox-retail.exe beside it.
#include "installer.h"
#include "mainwindow.h"

#include <cstdio>

#include <QApplication>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QStyleFactory>

#include <windows.h>

// The executable's own icon (resource 1, the PC version's when the build
// found bin/pop.ico), in every size it has: no image-format plugin needed.
static QIcon appIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        HICON h = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                size, size, LR_DEFAULTCOLOR));
        if (!h) continue;
        icon.addPixmap(QPixmap::fromImage(QImage::fromHICON(h)));
        DestroyIcon(h);
    }
    return icon;
}

// Without the window, for tests and scripts (results on stdout):
//   popsot-launcher --check-iso <image>
//   popsot-launcher --install <USA image> <PAL image or -> <game folder>
static int commandLine(const QStringList &args)
{
    if (args.size() >= 3 && args[1] == QLatin1String("--check-iso")) {
        QString usa, pal;
        const bool u = install::checkUsaIso(args[2], &usa), p = install::checkPalIso(args[2], &pal);
        std::printf("usa %d: %s\npal %d: %s\n", u, qPrintable(usa), p, qPrintable(pal));
        return u || p ? 0 : 1;
    }
    if (args.size() >= 5 && args[1] == QLatin1String("--install")) {
        install::Worker w(args[2] == QLatin1String("-") ? QString() : args[2],
                          args[3] == QLatin1String("-") ? QString() : args[3], args[4]);
        int last = -1;
        bool ok = false;
        QObject::connect(&w, &install::Worker::progress, [&last](int p, const QString &t) {
            if (p / 10 != last / 10) { std::printf("%3d%% %s\n", p, qPrintable(t)); std::fflush(stdout); }
            last = p;
        });
        QObject::connect(&w, &install::Worker::finished, [&ok](bool o, const QString &m) {
            ok = o;
            std::printf("%s: %s\n", o ? "ok" : "failed", qPrintable(m));
        });
        w.run();
        return ok ? 0 : 1;
    }
    return -1;
}

int main(int argc, char **argv)
{
    {
        QCoreApplication core(argc, argv);
        const int r = commandLine(QCoreApplication::arguments());
        if (r >= 0) return r;
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("POPSoT Recomp Launcher"));
    // the native look: Windows 11's style where Qt has it, else the classic one
    if (QStyleFactory::keys().contains(QStringLiteral("windows11"), Qt::CaseInsensitive))
        QApplication::setStyle(QStringLiteral("windows11"));
    QApplication::setWindowIcon(appIcon());
    MainWindow w;
    w.show();
    w.setupIfNeeded();
    return app.exec();
}
