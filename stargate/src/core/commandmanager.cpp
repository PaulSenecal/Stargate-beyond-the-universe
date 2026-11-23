#include "commandmanager.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <vector>

namespace {

QString compact(const QString &text)
{
    return text.toLower().remove(' ');
}

int levenshtein(const QString &a, const QString &b)
{
    std::vector<int> previous(b.size() + 1), current(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        previous[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
        }
        std::swap(previous, current);
    }
    return previous[b.size()];
}

CommandParam readParam(const QJsonValue &value)
{
    CommandParam p;
    if (value.isString()) {
        p.name = value.toString();
        return p;
    }
    const QJsonObject o = value.toObject();
    p.name = o.value("name").toString(QStringLiteral("value"));
    p.type = o.value("type").toString(QStringLiteral("text")).toLower();
    for (const QJsonValue &word : o.value("values").toArray())
        p.values << word.toString().toLower();
    p.pattern = o.value("pattern").toString();
    p.optional = o.value("optional").toBool(false);
    if (o.contains("min")) {
        p.hasMin = true;
        p.min = o.value("min").toDouble();
    }
    if (o.contains("max")) {
        p.hasMax = true;
        p.max = o.value("max").toDouble();
    }
    return p;
}

} // namespace

// ================================================================
//  CommandDefinition
// ================================================================

QString CommandDefinition::usage() const
{
    QString text = name.toLower();
    for (const CommandParam &p : params) {
        const QString part = !p.values.isEmpty() && p.values.size() <= 3 ? p.values.join('|') : p.name;
        text += p.optional ? QStringLiteral(" [%1]").arg(part) : QStringLiteral(" <%1>").arg(part);
    }
    return text;
}

// ================================================================
//  Loading
// ================================================================

CommandManager::CommandManager(QObject *parent)
    : QObject(parent)
{
}

bool CommandManager::loadCommandsFromFile(const QString &filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Commands] Could not open file:" << filename;
        return false;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (doc.isNull()) {
        qWarning() << "[Commands] Invalid JSON:" << error.errorString() << "at offset" << error.offset;
        return false;
    }

    m_definitions.clear();
    const QJsonObject categories = doc.object().value("commands").toObject();
    for (auto category = categories.begin(); category != categories.end(); ++category) {
        const QJsonObject subcommands = category.value().toObject().value("subcommands").toObject();
        for (auto it = subcommands.begin(); it != subcommands.end(); ++it) {
            const QJsonObject o = it.value().toObject();
            CommandDefinition def;
            def.key = it.key();
            def.name = o.value("name").toString(it.key()).simplified();
            def.category = category.key();
            for (const QJsonValue &param : o.value("params").toArray())
                def.params << readParam(param);

            // Legacy format: "modes": ["FTL", "Conventional"]
            if (!o.contains("params") && o.contains("modes")) {
                CommandParam mode;
                mode.name = QStringLiteral("mode");
                for (const QJsonValue &m : o.value("modes").toArray())
                    mode.values << m.toString().toLower();
                def.params << mode;
            }
            m_definitions << def;
        }
    }
    return !m_definitions.isEmpty();
}

// ================================================================
//  Parsing
// ================================================================

ParsedCommand CommandManager::parse(const QString &input) const
{
    ParsedCommand result;
    const QStringList tokens = input.simplified().split(' ', Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        result.error = tr("Empty command");
        return result;
    }

    // Every definition whose name matches the beginning of the input,
    // with the number of words its name consumed
    std::vector<std::pair<const CommandDefinition *, int>> candidates;
    for (const CommandDefinition &def : m_definitions) {
        const QStringList nameWords = def.name.toLower().split(' ', Qt::SkipEmptyParts);
        int consumed = 0;
        if (tokens.size() >= nameWords.size()
            && std::equal(nameWords.begin(), nameWords.end(), tokens.begin(),
                          [](const QString &word, const QString &token) { return word == token.toLower(); }))
            consumed = int(nameWords.size());
        else if (tokens.first().toLower() == compact(def.name))
            consumed = 1;
        if (consumed > 0)
            candidates.emplace_back(&def, consumed);
    }

    if (candidates.empty()) {
        result.error = tr("Unknown command '%1'").arg(tokens.first()) + suggestion(tokens.first());
        return result;
    }

    // Longest name first ("close door" before a hypothetical "close")
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto &a, const auto &b) { return a.second > b.second; });

    QString firstError;
    for (const auto &[def, consumed] : candidates) {
        QStringList args = tokens.mid(consumed);
        const QString error = checkParameters(*def, &args);
        if (error.isEmpty()) {
            result.valid = true;
            result.name = compact(def->name);
            result.category = def->category;
            result.args = args;
            return result;
        }
        if (firstError.isEmpty())
            firstError = QStringLiteral("%1: %2").arg(def->name.toLower(), error);
    }
    result.error = firstError;
    return result;
}

QString CommandManager::checkParameters(const CommandDefinition &def, QStringList *args) const
{
    const int maxCount = int(def.params.size());
    const int required = int(std::count_if(def.params.begin(), def.params.end(),
                                           [](const CommandParam &p) { return !p.optional; }));
    if (args->size() < required || args->size() > maxCount) {
        const QString expected = required == maxCount ? QString::number(maxCount)
                                                      : QStringLiteral("%1 to %2").arg(required).arg(maxCount);
        return tr("expected %1 parameter(s), got %2 - usage: %3")
            .arg(expected).arg(args->size()).arg(def.usage());
    }

    for (int i = 0; i < args->size(); ++i) {
        const CommandParam &p = def.params.at(i);
        QString &arg = (*args)[i];

        if (!p.values.isEmpty()) {
            if (!p.values.contains(arg.toLower()))
                return tr("'%1' is not a valid %2 - expected: %3").arg(arg, p.name, p.values.join(", "));
            arg = arg.toLower();
        }

        if (p.type == "int" || p.type == "number") {
            bool ok = false;
            const double value = p.type == "int" ? double(arg.toInt(&ok)) : arg.toDouble(&ok);
            if (!ok)
                return tr("'%1' must be a %2 (%3)")
                    .arg(arg, p.type == "int" ? tr("whole number") : tr("number"), p.name);
            if (p.hasMin && value < p.min)
                return tr("%1 must be at least %2").arg(p.name).arg(p.min);
            if (p.hasMax && value > p.max)
                return tr("%1 must be at most %2").arg(p.name).arg(p.max);
        }

        if (!p.pattern.isEmpty() && !QRegularExpression(p.pattern).match(arg).hasMatch())
            return tr("'%1' is not a valid %2 - usage: %3").arg(arg, p.name, def.usage());
    }
    return QString();
}

QString CommandManager::suggestion(const QString &word) const
{
    const QString typed = word.toLower();
    QString best;
    int bestDistance = 3;     // suggest only close matches
    for (const CommandDefinition &def : m_definitions) {
        const QString firstWord = def.name.toLower().section(' ', 0, 0);
        const int distance = std::min(levenshtein(typed, compact(def.name)), levenshtein(typed, firstWord));
        if (distance < bestDistance) {
            bestDistance = distance;
            best = def.name.toLower();
        }
    }
    return best.isEmpty() ? QString() : tr(" - did you mean '%1'?").arg(best);
}

// ================================================================
//  Compatibility + listings
// ================================================================

bool CommandManager::validateCommand(const QString &commandStr)
{
    const ParsedCommand cmd = parse(commandStr);
    emit commandValidated(cmd.valid, cmd.valid ? tr("Valid command: %1").arg(cmd.name) : cmd.error);
    return cmd.valid;
}

QStringList CommandManager::getAvailableCommands() const
{
    QStringList names;
    for (const CommandDefinition &def : m_definitions)
        names << def.name;
    names.removeDuplicates();
    return names;
}

QStringList CommandManager::usages(const QString &category) const
{
    QStringList list;
    for (const CommandDefinition &def : m_definitions)
        if (category.isEmpty() || def.category == category)
            list << def.usage();
    list.removeDuplicates();
    return list;
}

QStringList CommandManager::categories() const
{
    QStringList list;
    for (const CommandDefinition &def : m_definitions)
        list << def.category;
    list.removeDuplicates();
    return list;
}
