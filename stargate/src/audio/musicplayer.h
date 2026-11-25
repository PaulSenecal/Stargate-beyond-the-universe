#ifndef MUSICPLAYER_H
#define MUSICPLAYER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class QAudioOutput;
class QMediaPlayer;

// ================================================================
//  MusicPlayer
//  Background music from the local Stargate Reborn mod (show
//  soundtracks, silent without the mod folder). Two players take
//  turns so that a change of mood is a smooth cross-fade:
//   - Title  : title screen
//   - Calm   : exploration, no enemy around
//   - Battle : enemy wave (tracks depend on the enemy faction)
//   - Danger : the battle turns bad (hull low): darker tracks
//  At the end of a track, another one of the same mood follows.
//  Volume (0-100) is kept in the settings, apart from the effects.
// ================================================================

class MusicPlayer : public QObject
{
    Q_OBJECT

public:
    enum class Mood { None, Title, Calm, Battle, Danger };

    explicit MusicPlayer(QObject *parent = nullptr);

    // Cross-fades to a track of this mood (nothing if already playing it)
    void setMood(Mood mood, const QString &faction = QString());
    Mood mood() const { return m_mood; }

    int volume() const { return m_volume; }
    void setVolume(int percent);
    bool hasMusic() const { return !m_tracks.isEmpty(); }
    QString status() const;

private:
    struct Deck
    {
        QMediaPlayer *player = nullptr;
        QAudioOutput *output = nullptr;
        float level = 0;            // fade 0..1
    };

    void playNext();                // new track of the current mood on the other deck
    void fadeStep();
    void applyVolume(Deck &deck);
    QStringList tracksFor(Mood mood, const QString &faction) const;

    Deck m_decks[2];
    int m_active = 0;
    Mood m_mood = Mood::None;
    QString m_faction;
    QString m_current;
    int m_volume = 40;
    QHash<QString, QString> m_tracks;           // name -> file of the mod
    QTimer m_fade;
};

#endif // MUSICPLAYER_H
