#include "soundsystem.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioDevice>
#include <QAudioSink>
#include <QDebug>
#include <QIODevice>
#include <QMediaDevices>
#include <QMutex>
#include <QRandomGenerator>
#include <QSettings>
#include <QUrl>

#include <algorithm>
#include <cmath>

#include "src/audio/musicplayer.h"
#include "src/core/assets.h"

// ================================================================
//  AudioMixer
//  Pulled by QAudioSink (possibly from the audio thread): sums the
//  active voices, applies the master volume and a soft limiter.
// ================================================================

class AudioMixer : public QIODevice
{
public:
    struct Voice
    {
        SoundSystem::Samples clip;
        double pos = 0;             // in clip samples
        float rate = 1;
        float gainL = 1, gainR = 1;
        qint64 wait = 0;            // output frames before the start
    };

    AudioMixer(const QAudioFormat &format, QObject *parent)
        : QIODevice(parent)
        , m_format(format)
    {
        open(QIODevice::ReadOnly);
    }

    void add(Voice voice)
    {
        QMutexLocker lock(&m_mutex);
        if (m_voices.size() < kMaxVoices)
            m_voices.push_back(std::move(voice));
    }

    void setMaster(float gain)
    {
        QMutexLocker lock(&m_mutex);
        m_master = gain;
    }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (1 << 20) + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const int frameBytes = m_format.bytesPerFrame();
        const int channels = m_format.channelCount();
        const qint64 frames = frameBytes > 0 ? maxlen / frameBytes : 0;
        if (frames <= 0)
            return 0;
        m_mix.assign(size_t(frames) * 2, 0.0f);

        float master;
        {
            QMutexLocker lock(&m_mutex);
            master = m_master;
            for (Voice &v : m_voices) {
                const std::vector<float> &s = *v.clip;
                const double last = double(s.size()) - 1.0;
                for (qint64 f = 0; f < frames; ++f) {
                    if (v.wait > 0) {
                        --v.wait;
                        continue;
                    }
                    if (v.pos >= last)
                        break;
                    const size_t i = size_t(v.pos);
                    const float frac = float(v.pos - double(i));
                    const float x = s[i] + (s[i + 1] - s[i]) * frac;
                    m_mix[size_t(f) * 2] += x * v.gainL;
                    m_mix[size_t(f) * 2 + 1] += x * v.gainR;
                    v.pos += v.rate;
                }
            }
            m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                                          [](const Voice &v) { return v.wait <= 0 && v.pos >= double(v.clip->size()) - 1.0; }),
                           m_voices.end());
        }

        const bool isFloat = m_format.sampleFormat() == QAudioFormat::Float;
        for (qint64 f = 0; f < frames; ++f) {
            const float l = std::tanh(m_mix[size_t(f) * 2] * master);
            const float r = std::tanh(m_mix[size_t(f) * 2 + 1] * master);
            for (int c = 0; c < channels; ++c) {
                const float value = channels == 1 ? 0.5f * (l + r) : c == 0 ? l : c == 1 ? r : 0.0f;
                char *out = data + f * frameBytes + c * m_format.bytesPerSample();
                if (isFloat)
                    *reinterpret_cast<float *>(out) = value;
                else
                    *reinterpret_cast<qint16 *>(out) = qint16(value * 32767.0f);
            }
        }
        return frames * frameBytes;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    static constexpr int kMaxVoices = 40;

    QAudioFormat m_format;
    QMutex m_mutex;
    std::vector<Voice> m_voices;
    std::vector<float> m_mix;
    float m_master = 0.7f;
};

// ================================================================
//  Synthesized sounds (no asset needed)
// ================================================================

namespace {

constexpr float kTwoPi = 6.2831853f;

void normalize(std::vector<float> &s, float peak)
{
    float max = 0;
    for (float x : s)
        max = std::max(max, std::abs(x));
    if (max > 0)
        for (float &x : s)
            x *= peak / max;
}

// Low-passed noise rumble with a falling low "thump"
SoundSystem::Samples explosion(int rate, float seconds, float decay, float smooth, float thumpHz, quint32 seed)
{
    QRandomGenerator rng(seed);
    std::vector<float> s(size_t(rate * seconds));
    float y1 = 0, y2 = 0, phase = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const float t = float(i) / float(rate);
        const float white = float(rng.generateDouble()) * 2.0f - 1.0f;
        y1 += smooth * (white - y1);
        y2 += smooth * (y1 - y2);
        const float attack = 1.0f - std::exp(-t / 0.004f);
        const float rumble = y2 * attack * std::exp(-t / decay);
        phase += thumpHz * (1.0f + 2.0f * std::exp(-t / 0.05f)) / float(rate);
        const float thump = std::sin(kTwoPi * phase) * attack * std::exp(-t / (decay * 0.5f));
        s[i] = rumble * 4.0f + thump * 0.5f;
    }
    normalize(s, 0.9f);
    return std::make_shared<const std::vector<float>>(std::move(s));
}

// Bright falling "zap" of energy absorbed by the shield
SoundSystem::Samples shieldZap(int rate, float startHz, quint32 seed)
{
    QRandomGenerator rng(seed);
    std::vector<float> s(size_t(rate * 0.4f));
    float phase = 0, noise = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const float t = float(i) / float(rate);
        const float hz = startHz * (0.45f + 0.55f * std::exp(-t / 0.08f));
        phase += hz / float(rate);
        const float tone = std::sin(kTwoPi * phase) + 0.4f * std::sin(kTwoPi * phase * 1.5f);
        const float white = float(rng.generateDouble()) * 2.0f - 1.0f;
        noise += 0.5f * (white - noise);
        const float attack = 1.0f - std::exp(-t / 0.002f);
        s[i] = attack * (tone * std::exp(-t / 0.06f) + 0.5f * noise * std::exp(-t / 0.02f));
    }
    normalize(s, 0.8f);
    return std::make_shared<const std::vector<float>>(std::move(s));
}

} // namespace

// ================================================================
//  SoundSystem
// ================================================================

SoundSystem::SoundSystem(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_volume = std::clamp(QSettings("Stargate", "stargate").value("audio/volume", 70).toInt(), 0, 100);
    m_music = new MusicPlayer(this);
    openOutput();
    if (!m_sink)
        return;

    addSynthSounds();

    const QStringList turret{"Weapon_AncientsDestinyTurret_Fire", "Weapon_AncientsDestinyTurret_Fire_alt1",
                             "Weapon_AncientsDestinyTurret_Fire_alt2", "Weapon_AncientsDestinyTurret_Fire_alt3"};
    const QStringList staff{"Weapon_Goauld_Plasma_Staff_Muzzle", "Weapon_Goauld_Plasma_Staff_Muzzle_alt1",
                            "Weapon_Goauld_Plasma_Staff_Muzzle_alt2"};
    const QStringList goauldHeavy{"Weapon_Goauld_Plasma_Heavy_Muzzle", "Weapon_Goauld_Plasma_Heavy_Muzzle_alt1",
                                  "Weapon_Goauld_Plasma_Heavy_Muzzle_alt2"};
    const QStringList wraithLight{"Weapon_Wraith_Plasma_Light_Muzzle", "Weapon_Wraith_Plasma_Light_Muzzle_alt1"};
    const QStringList wraithHeavy{"Weapon_Wraith_Plasma_Heavy_Muzzle", "Weapon_Wraith_Plasma_Heavy_Muzzle_alt1",
                                  "Weapon_Wraith_Plasma_Heavy_Muzzle_alt2"};
    const QStringList titan{"Weapon_Wraith_Plasma_Titan_Muzzle", "Weapon_Wraith_Plasma_Titan_Muzzle_alt1",
                            "Weapon_Wraith_Plasma_Titan_Muzzle_alt2"};

    using C = SoundCue;
    addModSound(C::DestinyMainGun, {}, {"Weapon_AncientsDestinyMainGun_Fire", "Weapon_AncientsDestinyMainGun_Fire_alt1"},
                0.9f, 1.0f, 0.0f);
    addModSound(C::DestinyTurret, {}, turret, 0.4f, 1.0f, 0.07f);
    addModSound(C::DestinyRailgun, {}, turret, 0.22f, 1.6f, 0.09f);
    // Goa'uld weapons are the default (the Lucian Alliance flies Goa'uld ships)
    addModSound(C::EnemyLightFire, {}, staff, 0.35f, 1.0f, 0.06f);
    addModSound(C::EnemyLightFire, "Wraith", wraithLight, 0.35f, 1.0f, 0.06f);
    addModSound(C::EnemyLightFire, "Drones", wraithLight, 0.3f, 1.35f, 0.06f);
    addModSound(C::EnemyHeavyFire, {}, goauldHeavy, 0.5f, 1.0f, 0.08f);
    addModSound(C::EnemyHeavyFire, "Wraith", wraithHeavy, 0.5f, 1.0f, 0.08f);
    addModSound(C::EnemyHeavyFire, "Drones", titan, 0.5f, 1.2f, 0.08f);
    addModSound(C::EnemyMissile, {}, {"Weapon_Goauld_Plasma_HeavyBomb_Muzzle"}, 0.45f, 1.0f, 0.15f);
    addModSound(C::EnemyBeam, {}, {"weapon_asgard_beam_travel", "Weapon_AncientsBeamHeavy_Travel"}, 0.6f, 1.0f, 0.3f);
    addModSound(C::Launch, {}, {"Weapon_Goauld_Plasma_HeavyBomb_Muzzle"}, 0.3f, 0.7f, 0.5f);
    decodeNext();
}

SoundSystem::~SoundSystem()
{
    if (m_sink)
        m_sink->stop();
}

void SoundSystem::openOutput()
{
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return;

    QAudioFormat format = device.preferredFormat();
    format.setChannelCount(2);
    if (!device.isFormatSupported(format))
        format.setChannelCount(device.preferredFormat().channelCount());
    format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format))
        format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format))
        return;

    m_rate = format.sampleRate();
    m_mixer = new AudioMixer(format, this);
    m_mixer->setMaster(m_volume / 100.0f);
    m_sink = new QAudioSink(device, format, this);
    m_sink->setBufferSize(format.bytesForDuration(60000));     // 60 ms of latency
    m_sink->start(m_mixer);
}

void SoundSystem::setVolume(int percent)
{
    m_volume = std::clamp(percent, 0, 100);
    QSettings("Stargate", "stargate").setValue("audio/volume", m_volume);
    if (m_mixer)
        m_mixer->setMaster(m_volume / 100.0f);
}

QString SoundSystem::status() const
{
    if (!m_sink)
        return tr("Sound: no audio output available");
    QString text = tr("Sound: volume %1%").arg(m_volume);
    if (Assets::modDir().isEmpty() || m_modClips == 0)
        return text + tr(" - weapon sounds missing (mod folder not found)");
    return text + tr(" - %1 weapon clips from the mod").arg(m_modClips);
}

QString SoundSystem::key(SoundCue cue, const QString &faction)
{
    return QString::number(int(cue)) + '/' + faction;
}

SoundSystem::Sound *SoundSystem::find(SoundCue cue, const QString &faction)
{
    auto it = m_sounds.find(key(cue, faction));
    if (it == m_sounds.end() || it->clips.isEmpty())
        it = m_sounds.find(key(cue, {}));
    return it == m_sounds.end() || it->clips.isEmpty() ? nullptr : &*it;
}

void SoundSystem::play(SoundCue cue, const QString &faction, float delay)
{
    if (!m_mixer || m_volume == 0)
        return;
    Sound *sound = find(cue, faction);
    if (!sound)
        return;

    // Rapid fire and fast-forwarded battles: drop sounds started too close together
    const double start = m_clock.elapsed() / 1000.0 + delay;
    if (std::abs(start - sound->lastStart) < sound->minGap)
        return;
    sound->lastStart = start;

    auto *rng = QRandomGenerator::global();
    AudioMixer::Voice voice;
    voice.clip = sound->clips.at(rng->bounded(int(sound->clips.size())));
    voice.rate = sound->rate * float(0.94 + 0.12 * rng->generateDouble());   // small pitch variations
    const float pan = float(rng->generateDouble() - 0.5) * 0.5f;
    voice.gainL = sound->gain * std::min(1.0f, 1.0f - pan);
    voice.gainR = sound->gain * std::min(1.0f, 1.0f + pan);
    voice.wait = qint64(std::max(0.0f, delay) * m_rate);
    m_mixer->add(std::move(voice));
}

void SoundSystem::addSynthSounds()
{
    auto add = [this](SoundCue cue, float gain, float minGap, QList<Samples> clips) {
        Sound &sound = m_sounds[key(cue, {})];
        sound.clips = std::move(clips);
        sound.gain = gain;
        sound.minGap = minGap;
    };
    const int r = m_rate;
    add(SoundCue::Explosion, 0.7f, 0.05f,
        {explosion(r, 1.0f, 0.22f, 0.06f, 70, 11), explosion(r, 1.0f, 0.2f, 0.07f, 60, 12),
         explosion(r, 1.1f, 0.25f, 0.05f, 80, 13)});
    add(SoundCue::BigExplosion, 1.0f, 0.2f,
        {explosion(r, 2.6f, 0.7f, 0.02f, 45, 21), explosion(r, 2.6f, 0.65f, 0.025f, 40, 22)});
    add(SoundCue::HullHit, 0.55f, 0.08f,
        {explosion(r, 0.5f, 0.07f, 0.2f, 110, 31), explosion(r, 0.5f, 0.08f, 0.18f, 95, 32)});
    add(SoundCue::ShieldHit, 0.3f, 0.1f, {shieldZap(r, 1400, 41), shieldZap(r, 1200, 42), shieldZap(r, 1600, 43)});
}

void SoundSystem::addModSound(SoundCue cue, const QString &faction, const QStringList &files, float gain,
                              float rate, float minGap)
{
    const QString soundKey = key(cue, faction);
    Sound &sound = m_sounds[soundKey];
    sound.gain = gain;
    sound.rate = rate;
    sound.minGap = minGap;
    for (const QString &name : files) {
        const QString path = Assets::modFile(QStringLiteral("sounds/%1.ogg").arg(name));
        if (path.isEmpty())
            continue;
        if (!m_waiting.contains(path) && !m_decoded.contains(path))
            m_queue << path;
        m_waiting[path] << soundKey;
    }
}

// Clips are decoded one after the other in the background, each with
// its own decoder (a QAudioDecoder is not reliable when reused)
void SoundSystem::decodeNext()
{
    if (m_queue.isEmpty() || m_decoder)
        return;

    const QString path = m_queue.first();
    auto *decoder = new QAudioDecoder(this);
    m_decoder = decoder;
    QAudioFormat format;
    format.setSampleRate(m_rate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    decoder->setAudioFormat(format);
    m_buffer.clear();

    connect(decoder, &QAudioDecoder::bufferReady, this, [this, decoder] {
        const QAudioBuffer buffer = decoder->read();
        const QAudioFormat f = buffer.format();
        const auto *bytes = buffer.constData<char>();
        for (qsizetype frame = 0; frame < buffer.frameCount(); ++frame) {
            float sum = 0;
            for (int c = 0; c < f.channelCount(); ++c)
                sum += f.normalizedSampleValue(bytes + frame * f.bytesPerFrame() + c * f.bytesPerSample());
            m_buffer.push_back(sum / float(std::max(1, f.channelCount())));
        }
    });
    // finished and error may both come, possibly from inside the decoder:
    // only the first one counts and the next file starts from the event loop
    auto done = [this, decoder, path](bool ok) {
        if (m_decoder != decoder)
            return;
        m_decoder = nullptr;
        if (ok && m_buffer.size() > 1) {
            const Samples samples = std::make_shared<const std::vector<float>>(std::move(m_buffer));
            m_decoded.insert(path, samples);
            for (const QString &soundKey : m_waiting.value(path))
                m_sounds[soundKey].clips << samples;
            ++m_modClips;
        } else {
            qWarning() << "[Audio] Could not decode" << path << decoder->errorString();
        }
        m_buffer = {};
        m_waiting.remove(path);
        m_queue.removeOne(path);
        decoder->deleteLater();
        QMetaObject::invokeMethod(this, &SoundSystem::decodeNext, Qt::QueuedConnection);
    };
    connect(decoder, &QAudioDecoder::finished, this, [done] { done(true); });
    connect(decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [done] { done(false); });
    decoder->setSource(QUrl::fromLocalFile(path));
    decoder->start();
}
