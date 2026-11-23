#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QSurfaceFormat>
#include <QTranslator>

int main(int argc, char *argv[])
{
    // The 3D battle view needs an OpenGL 3.3 core context (macOS: 4.1 core).
    // Must be set before the QApplication is created.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication a(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "stargate_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.showMaximized();
    return a.exec();
}
