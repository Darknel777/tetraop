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
    std::array<int, 3> wave { 0, 0, 0 };
    float osc1Level = 1.0f;
    float osc2Level = 0.0f;
    float ringLevel = 0.0f;
    float noiseLevel = 0.0f;
    float directOut = 0.0f;
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

// Этап 1.5: у каждой партии свой пул. Общего массива на 32 голоса нет.
// 8 голосов на партию — внутренний предел, не параметр. 64 — сумма восьми пулов.
// Хвост кражи не занимает голос MIDI и не считается в этих восьми.
class MinimalVoiceCore
{
public:
    static constexpr int partCount = 8;
    static constexpr int voicesPerPart = 8;
    static constexpr int tailsPerPart = 8;
    static constexpr int oscillatorCount = 3;
    static constexpr float stealFadeSeconds = 0.004f;

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    void setPartEnabled(int partIndex, bool enabled) noexcept;
    void noteOn(int partIndex, int channel, int noteNumber, float velocity, bool mono, bool legato) noexcept;
    void noteOff(int partIndex, int channel, int noteNumber) noexcept;
    void setSustain(int partIndex, int channel, bool down) noexcept;
    float renderSample(const EngineControls& controls) noexcept;
    int activeVoiceCount() const noexcept;
    int activeVoiceCountForPart(int partIndex) const noexcept;
    int activeTailCount() const noexcept;
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
        int channel = 0;
        int note = -1;
        float velocity = 0.0f;
        std::array<double, oscillatorCount> phase {};
        std::uint32_t noiseState = 1;
        float envelope = 0.0f;
        float releaseOrigin = 0.0f;
        Stage stage = Stage::idle;
        std::uint64_t age = 0;
        bool keyHeld = false;
        bool waitingForPedal = false;
        bool entryFading = false;
        float entryPosition = 0.0f;
    };

    struct Tail
    {
        bool active = false;
        int channel = 0;
        int note = -1;
        float level = 0.0f;
        std::array<double, oscillatorCount> phase {};
        std::uint32_t noiseState = 1;
        float fadePosition = 0.0f;
    };

    struct PartPool
    {
        std::array<Voice, voicesPerPart> voices {};
        std::array<Tail, tailsPerPart> tails {};
        bool enabled = false;
        std::array<bool, 16> sustainDown {};
    };

    Voice* voiceFor(PartPool& part, int channel, int noteNumber) noexcept;
    Voice* idleVoice(PartPool& part) noexcept;
    Voice* stealWithin(PartPool& part) noexcept;
    void pushTail(PartPool& part, const Voice& voice) noexcept;
    void assign(Voice& voice, int partIndex, int channel, int noteNumber, float velocity, bool restartEnvelope, bool fresh, bool crossfade) noexcept;
    void beginRelease(Voice& voice) noexcept;
    void releasePart(PartPool& part) noexcept;
    void freeVoice(Voice& voice) noexcept;
    void freeTail(Tail& tail) noexcept;
    void advance(Voice& voice, const PartControls& controls) noexcept;
    float sourceSample(int noteNumber, std::array<double, oscillatorCount>& phase, std::uint32_t& noiseState, const PartControls& controls) noexcept;
    int countPart(const PartPool& part) const noexcept;
    static bool older(const Voice& candidate, const Voice* best) noexcept;
    static float waveform(int wave, double phase) noexcept;
    static float nextNoise(std::uint32_t& state) noexcept;
    static float hannDown(float position) noexcept;
    static float secondsOrMinimum(float seconds) noexcept;
    static float unitLevel(float value) noexcept;
    static std::uint32_t noiseSeed(int partIndex, int channel, int noteNumber, std::uint64_t age) noexcept;

    std::array<PartPool, partCount> pools {};
    double sampleRate = 0.0;
    std::uint64_t nextAge = 1;
};

} // namespace xenmilenium
