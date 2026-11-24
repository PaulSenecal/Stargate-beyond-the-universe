#include "language.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace {

QTranslator *g_translator = nullptr;

} // namespace

namespace Language {

QList<QPair<QString, QString>> available()
{
    return {{QStringLiteral("en"), QStringLiteral("English")},
            {QStringLiteral("fr"), QStringLiteral("Français")}};
}

QString current()
{
    const QString saved = QSettings("Stargate", "stargate").value("ui/language").toString();
    for (const auto &language : available())
        if (language.first == saved)
            return saved;
    const QString system = QLocale::system().name().section('_', 0, 0);
    for (const auto &language : available())
        if (language.first == system)
            return system;
    return QStringLiteral("en");
}

void save(const QString &code)
{
    QSettings("Stargate", "stargate").setValue("ui/language", code);
}

bool apply(const QString &code)
{
    if (g_translator) {
        QCoreApplication::removeTranslator(g_translator);
        delete g_translator;
        g_translator = nullptr;
    }
    if (code == "en")
        return true;                                // source language
    auto *translator = new QTranslator;
    if (!translator->load(QStringLiteral(":/i18n/stargate_%1").arg(code))) {
        delete translator;
        return false;
    }
    QCoreApplication::installTranslator(translator);
    g_translator = translator;
    return true;
}

} // namespace Language
