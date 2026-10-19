#pragma once

#include <array>
#include <cstdint>

namespace xenmilenium
{

struct PartControls
{
    bool enabled = false;
    int midiChannel = 0;
    bool mono = false;
    bool legato = false;
    int wave = 0;
    float attackSeconds = 0.01f;
    float decaySeconds = 0.2f;
    float sustain = 0.8f;
    float releaseSeconds = 0.4f;
};

struct EngineControls
{
    std::array<PartControls, 8> parts {};
    float masterGain = 0.8f;
};

// Пул этапа 1.4: восемь партий на общем массиве голосов.
// 8 и 32 — числа стенда, не обещание продукта.
class MinimalVoiceCore
{
public:
    static constexpr int partCount = 8;
    static constexpr int voicesPerPart = 8;
    static constexpr int voiceCapacity = 32;

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    void setPartEnabled(int partIndex, bool enabled) noexcept;
    void noteOn(int partIndex, int channel, int noteNumber, float velocity, bool mono, bool legato) noexcept;
    void noteOff(int partIndex, int channel, int noteNumber) noexcept;
    void setSustain(int partIndex, int channel, bool down) noexcept;
    float renderSample(const EngineControls& controls) noexcept;
    int activeVoiceCount() const noexcept;
    int activeVoiceCountForPart(int partIndex) const noexcept;
    bool isNoteActive(int partIndex, int channel, int noteNumber) const noexcept;

private:
    enum class Stage
    {
        idle,
        attack,
        decay,
        sustain,
        hold,
        release
    };

    struct Voice
    {
        bool active = false;
        int part = -1;
        int channel = 0;
        int note = -1;
        float velocity = 0.0f;
        double phase = 0.0;
        float envelope = 0.0f;
        float releaseOrigin = 0.0f;
        Stage stage = Stage::idle;
        std::uint64_t age = 0;
        bool keyHeld = false;
        bool waitingForPedal = false;
    };

    Voice* voiceFor(int partIndex, int channel, int noteNumber) noexcept;
    Voice* idleVoice() noexcept;
    Voice* stealWithin(int partIndex) noexcept;
    Voice* stealGloballyPreferringOthers(int partIndex) noexcept;
    void assign(Voice& voice, int partIndex, int channel, int noteNumber, float velocity, bool restartEnvelope, bool fresh) noexcept;
    void beginRelease(Voice& voice) noexcept;
    void releasePart(int partIndex) noexcept;
    void freeVoice(Voice& voice) noexcept;
    void advance(Voice& voice, const PartControls& controls) noexcept;
    int countPart(int partIndex) const noexcept;
    static bool older(const Voice& candidate, const Voice* best) noexcept;
    static float waveform(int wave, double phase) noexcept;
    static float secondsOrMinimum(float seconds) noexcept;

    std::array<Voice, voiceCapacity> voices {};
    std::array<std::array<bool, 16>, partCount> sustainDown {};
    std::array<bool, partCount> partEnabled {};
    double sampleRate = 0.0;
    std::uint64_t nextAge = 1;
};

} // namespace xenmilenium
