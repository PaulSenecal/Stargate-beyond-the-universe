#ifndef SOUNDSYSTEM_H
#define SOUNDSYSTEM_H

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

#include "src/audio/soundcue.h"

class AudioMixer;
class QAudioDecoder;
class QAudioSink;

// ================================================================
//  SoundSystem
//  Battle sound effects. Every clip is decoded once at start-up and
//  mixed by AudioMixer into one audio output, so dozens of shots can
//  overlap without latency.
//   - weapon fire: .ogg clips of the local Stargate Reborn mod
//     (Assets::modFile), silent when the mod folder is missing
//   - explosions, shield and hull impacts: synthesized, always there
//  Volume (0-100) is kept in the settings.
// ================================================================

class SoundSystem : public QObject
{
    Q_OBJECT

public:
    using Samples = std::shared_ptr<const std::vector<float>>;     // mono, output rate

    explicit SoundSystem(QObject *parent = nullptr);
    ~SoundSystem() override;

    // delay: seconds before the sound starts (salvos)
    void play(SoundCue cue, const QString &faction = QString(), float delay = 0.0f);

    bool isActive() const { return m_sink != nullptr; }
    int volume() const { return m_volume; }
    void setVolume(int percent);
    QString status() const;

private:
    struct Sound
    {
        QList<Samples> clips;           // one is picked at random
        float gain = 1.0f;
        float rate = 1.0f;              // playback speed (pitch)
        float minGap = 0.06f;           // seconds between two starts
        double lastStart = -1e9;
    };

    void openOutput();
    void addModSound(SoundCue cue, const QString &faction, const QStringList &files, float gain, float rate,
                     float minGap);
    void addSynthSounds();
    void decodeNext();
    Sound *find(SoundCue cue, const QString &faction);
    static QString key(SoundCue cue, const QString &faction);

    AudioMixer *m_mixer = nullptr;
    QAudioSink *m_sink = nullptr;
    QAudioDecoder *m_decoder = nullptr;
    int m_rate = 48000;
    int m_volume = 70;

    QHash<QString, Sound> m_sounds;                 // key(cue, faction)
    QHash<QString, Samples> m_decoded;              // file -> samples
    QHash<QString, QStringList> m_waiting;          // file -> sound keys waiting for it
    QStringList m_queue;                            // files left to decode
    std::vector<float> m_buffer;                    // file being decoded
    int m_modClips = 0;
    QElapsedTimer m_clock;
};

#endif // SOUNDSYSTEM_H
