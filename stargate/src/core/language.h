#ifndef LANGUAGE_H
#define LANGUAGE_H

#include <QList>
#include <QPair>
#include <QString>

class QTranslator;

// ================================================================
//  Language
//  Interface language (English source texts, French translation in
//  stargate_fr.ts, embedded as :/i18n/stargate_fr.qm). The choice is
//  kept in the settings; by default the system language is used when
//  it is translated, English otherwise.
// ================================================================

namespace Language {

// Code and native name of every language: {"en", "English"}, {"fr", "Français"}
QList<QPair<QString, QString>> available();

// Language saved in the settings, else the system one (if available)
QString current();
void save(const QString &code);

// Installs the translator of the language in the application
// (replaces the previous one). False if its file is missing.
bool apply(const QString &code);

} // namespace Language

#endif // LANGUAGE_H
