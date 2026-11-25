#include "musicplayer.h"

#include <QAudio>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

#include <algorithm>

#include "src/core/assets.h"

namespace {

constexpr int kFadeTickMs = 40;
constexpr float kFadeSeconds = 2.5f;

// Tracks of the mod (sounds/<name>.ogg) for each mood
const QStringList kTitle{"SGU_DestinysBridge", "SGU_GauntletEndingTheme"};
const QStringList kCalm{"SGU_AHauntingFarewell", "SGU_AHeavyBurden", "SGU_APleaForTheMission", "SGU_Light",
                        "SGU_NickAndMandy", "SGU_RequitedUnrequited", "SGU_SeekingJustice",
                        "SGU_SittingInTheBackyard", "SGU_TheChair", "SGU_TwoGraves", "SGU_CountdownToDestiny",
                        "SGU_DialingPittsburg"};
const QStringList kBattle{"SGAOT_TheBattleBegins", "SGU_AwaitingTheAttack", "SGA_DartBattle",
                          "SG_BattleAtThePyramid_nf"};
const QStringList kBattleWraith{"SGA_DartBattle", "SGA_WraithLair", "SGAOT_TheBattleBegins"};
const QStringList kBattleGoauld{"SG_BattleAtThePyramid_nf", "SG1_ThorsHammer_e1", "SGAOT_TheBattleBegins"};

// "SGU_GauntletEndingTheme" -> "Gauntlet Ending Theme (SGU)"
QString displayName(const QString &file)
{
    QString show = file.section('_', 0, 0);
    QString title = file.section('_', 1, 1);
    title.replace(QRegularExpression("([a-z])([A-Z0-9])"), "\\1 \\2");
    if (show == "SGAOT")
        show = "SGA";
    return QStringLiteral("%1 (%2)").arg(title, show);
}

} // namespace

MusicPlayer::MusicPlayer(QObject *parent)
    : QObject(parent)
{
    m_volume = std::clamp(QSettings("Stargate", "stargate").value("audio/music", 40).toInt(), 0, 100);

    for (const QStringList *list : {&kTitle, &kCalm, &kBattle, &kBattleWraith, &kBattleGoauld})
        for (const QString &name : *list) {
            const QString path = Assets::modFile(QStringLiteral("sounds/%1.ogg").arg(name));
            if (!path.isEmpty())
                m_tracks.insert(name, path);
        }
    if (m_tracks.isEmpty())
        return;

    for (Deck &deck : m_decks) {
        deck.player = new QMediaPlayer(this);
        deck.output = new QAudioOutput(this);
        deck.player->setAudioOutput(deck.output);
        applyVolume(deck);
        connect(deck.player, &QMediaPlayer::mediaStatusChanged, this, [this, &deck](QMediaPlayer::MediaStatus s) {
            if (s == QMediaPlayer::EndOfMedia && &deck == &m_decks[m_active])
                playNext();
        });
    }
    m_fade.setInterval(kFadeTickMs);
    connect(&m_fade, &QTimer::timeout, this, &MusicPlayer::fadeStep);
}

QStringList MusicPlayer::tracksFor(Mood mood, const QString &faction) const
{
    QStringList names;
    switch (mood) {
    case Mood::Title: names = kTitle; break;
    case Mood::Calm: names = kCalm; break;
    case Mood::Danger: names = QStringList{"SGA_WraithLair", "SGU_AwaitingTheAttack", "SGU_AHeavyBurden"}; break;
    case Mood::Battle:
        names = faction == "Wraith" ? kBattleWraith
              : faction == "Goa'uld" || faction == "Lucian Alliance" ? kBattleGoauld
                                                                      : kBattle;
        break;
    case Mood::None: break;
    }
    QStringList available;
    for (const QString &name : names)
        if (m_tracks.contains(name))
            available << name;
    return available;
}

void MusicPlayer::setMood(Mood mood, const QString &faction)
{
    if (mood == m_mood && (mood != Mood::Battle || faction == m_faction))
        return;
    // A battle against the same faction keeps its track
    m_mood = mood;
    m_faction = faction;
    if (!hasMusic())
        return;
    if (tracksFor(mood, faction).contains(m_current))
        return;
    playNext();
}

void MusicPlayer::playNext()
{
    QStringList names = tracksFor(m_mood, m_faction);
    if (names.size() > 1)
        names.removeAll(m_current);         // no immediate repeat
    if (names.isEmpty()) {
        m_current.clear();                  // fade out
        m_fade.start();
        return;
    }
    m_current = names.at(QRandomGenerator::global()->bounded(int(names.size())));

    m_active = 1 - m_active;
    Deck &deck = m_decks[m_active];
    deck.player->setSource(QUrl::fromLocalFile(m_tracks.value(m_current)));
    deck.level = 0;
    applyVolume(deck);
    deck.player->play();
    m_fade.start();
}

// The active deck fades in, the other one fades out
void MusicPlayer::fadeStep()
{
    const float step = kFadeTickMs / 1000.0f / kFadeSeconds;
    bool moving = false;
    for (int i = 0; i < 2; ++i) {
        Deck &deck = m_decks[i];
        const float target = i == m_active && !m_current.isEmpty() ? 1.0f : 0.0f;
        if (deck.level != target) {
            deck.level = target > deck.level ? std::min(target, deck.level + step)
                                             : std::max(target, deck.level - step);
            moving = true;
        }
        applyVolume(deck);
        if (deck.level <= 0.0f && deck.player->playbackState() == QMediaPlayer::PlayingState)
            deck.player->stop();
    }
    if (!moving)
        m_fade.stop();
}

void MusicPlayer::applyVolume(Deck &deck)
{
    const float linear = QAudio::convertVolume(m_volume / 100.0f, QAudio::LogarithmicVolumeScale,
                                               QAudio::LinearVolumeScale);
    deck.output->setVolume(linear * deck.level);
}

void MusicPlayer::setVolume(int percent)
{
    m_volume = std::clamp(percent, 0, 100);
    QSettings("Stargate", "stargate").setValue("audio/music", m_volume);
    if (hasMusic())
        for (Deck &deck : m_decks)
            applyVolume(deck);
}

QString MusicPlayer::status() const
{
    if (!hasMusic())
        return tr("Music: no track (mod folder not found)");
    if (m_volume == 0)
        return tr("Music: off");
    return m_current.isEmpty() ? tr("Music: volume %1%").arg(m_volume)
                               : tr("Music: volume %1% - %2").arg(m_volume).arg(displayName(m_current));
}
