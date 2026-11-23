#include "questsystem.h"

#include <algorithm>
#include <tuple>

namespace {

// Console command that repairs a component (see BattleView::systemKey)
QString repairCommand(const QString &component)
{
    if (component == "Life Support") return QStringLiteral("repair lifesupport");
    if (component == "Sublight Engine") return QStringLiteral("repair engines");
    if (component == "Shield Generator") return QStringLiteral("repair shields");
    return QStringLiteral("repair weapons");
}

} // namespace

QuestSystem::QuestSystem(QObject *parent)
    : QObject(parent)
{
}

bool QuestSystem::load(int playerId)
{
    m_quests = QuestRepository::load(playerId);
    activateAvailable();
    return isLoaded();
}

const QuestData *QuestSystem::activeQuest() const
{
    for (const QuestData &q : m_quests)
        if (q.status == "ACTIVE")
            return &q;
    return nullptr;
}

int QuestSystem::completedCount() const
{
    int count = 0;
    for (const QuestData &q : m_quests)
        count += q.status == "COMPLETED";
    return count;
}

void QuestSystem::activateAvailable()
{
    for (QuestData &q : m_quests) {
        if (q.status != "LOCKED")
            continue;
        const bool unlocked = q.prerequisite == 0
            || std::any_of(m_quests.begin(), m_quests.end(), [&](const QuestData &other) {
                   return other.id == q.prerequisite && other.status == "COMPLETED";
               });
        if (!unlocked)
            continue;
        q.status = QStringLiteral("ACTIVE");
        QuestRepository::setStatus(q.playerQuestId, q.status, now());
        emit questStarted(q.title, q.description, &q == &m_quests.first());
    }
}

void QuestSystem::notify(const QString &type, const QString &target, int amount)
{
    // A slot reacting to our signals may report new events (quest
    // rewards are resources): queue them instead of re-entering
    m_pending.append({type, target, amount});
    if (m_busy)
        return;
    m_busy = true;
    while (!m_pending.isEmpty()) {
        const auto [t, tg, n] = m_pending.takeFirst();
        process(t, tg, n);
    }
    m_busy = false;
}

void QuestSystem::process(const QString &type, const QString &target, int amount)
{
    for (QuestData &q : m_quests) {
        if (q.status != "ACTIVE")
            continue;

        bool changed = false;
        for (QuestObjective &o : q.objectives) {
            if (o.done || o.type != type)
                continue;
            if (!o.target.isEmpty() && o.target.compare(target, Qt::CaseInsensitive) != 0)
                continue;
            o.current = std::min(o.targetQuantity, o.current + std::max(1, amount));
            o.done = o.current >= o.targetQuantity;
            QuestRepository::setProgress(q.playerQuestId, o);
            changed = true;
            if (o.done)
                emit objectiveCompleted(o.description);
        }

        const bool allDone = std::all_of(q.objectives.begin(), q.objectives.end(),
                                         [](const QuestObjective &o) { return o.done; });
        if (changed && allDone) {
            q.status = QStringLiteral("COMPLETED");
            QuestRepository::setStatus(q.playerQuestId, q.status, now());
            emit questCompleted(q.title, q.rewards);
            activateAvailable();
            if (completedCount() == questCount())
                emit campaignCompleted();
            return;
        }
    }
}

QStringList QuestSystem::objectiveLines() const
{
    const QuestData *q = activeQuest();
    if (!q)
        return {};

    QStringList lines;
    lines << tr("QUEST %1/%2  %3").arg(completedCount() + 1).arg(questCount()).arg(q->title.toUpper());
    for (const QuestObjective &o : q->objectives) {
        QString line = QStringLiteral("%1 %2").arg(o.done ? "[x]" : "[ ]", o.description);
        if (!o.done && o.targetQuantity > 1)
            line += QStringLiteral(" (%1/%2)").arg(o.current).arg(o.targetQuantity);
        lines << line;
    }
    const QString h = hint();
    if (!h.isEmpty())
        lines << QStringLiteral("> ") + h;
    return lines;
}

QString QuestSystem::hint() const
{
    const QuestData *q = activeQuest();
    if (!q)
        return completedCount() == questCount() && isLoaded() ? tr("Campaign complete - keep exploring") : QString();

    for (const QuestObjective &o : q->objectives) {
        if (o.done)
            continue;
        if (o.type == "REPAIR")
            return repairCommand(o.target);
        if (o.type == "STATUS")
            return QStringLiteral("destinystatus");
        if (o.type == "COLLECT")
            return tr("destroy ships or 'explore' planets rich in %1").arg(o.target == "naquadah" ? tr("energy") : o.target);
        if (o.type == "EXPLORE")
            return tr("scansector, then stargate <address> (Tab completes)");
        if (o.type == "SURVIVE")
            return tr("stargate destiny before the gate window closes");
    }
    return QString();
}

QStringList QuestSystem::journal() const
{
    QStringList lines;
    for (const QuestData &q : m_quests) {
        const QString state = q.status == "COMPLETED" ? tr("done")
                            : q.status == "ACTIVE"    ? tr("in progress")
                                                      : tr("locked");
        lines << QStringLiteral("%1 [%2] - %3").arg(q.title, state, q.description);
        if (q.status == "ACTIVE")
            for (const QuestObjective &o : q.objectives)
                lines << QStringLiteral("   %1 %2").arg(o.done ? "[x]" : "[ ]", o.description);
    }
    return lines;
}
