#pragma once

#include "Engine/Modulation.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace xenmilenium
{

struct LfoControls
{
    int shape = 0;
    bool retrig = false;
    bool unipolar = false;
    bool sync = false;
    float rateHz = 1.0f;
    int rateSync = 6;
    float freeVariation = 0.0f;
};

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
    int filterType = 0;
    float cutoffHz = 2000.0f;
    float resonance = 0.0f;
    float drive = 0.0f;
    float keytrack = 0.0f;
    float filterMix = 0.0f;
    float filterAttackSeconds = 0.01f;
    float filterDecaySeconds = 0.2f;
    float filterSustain = 0.8f;
    float filterReleaseSeconds = 0.4f;
    float filterAmountSemitones = 0.0f;
    float lfo1ToCutoffSemitones = 0.0f;
    float lfo2ToPitchSemitones = 0.0f;
    std::array<LfoControls, 4> lfo {};
};

struct EngineControls
{
    std::array<PartControls, 8> parts {};
    float masterGain = 0.8f;
    float tempoBpm = 120.0f;
    // Снимок слотов на блок: 8 партий по 32, партия за партией. Нулевой указатель значит нули.
    const MatrixSlotControl* matrix = nullptr;
    std::array<bool, modulationPartCount> matrixActive {};
};

// Этап 1.5: у каждой партии свой пул. Общего массива на 32 голоса нет.
// 8 голосов на партию — внутренний предел, не параметр. 64 — сумма восьми пулов.
// Хвост кражи не занимает голос MIDI и не считается в этих восьми.
// Этап 1.6 добавляет фильтр, огибающую фильтра и четыре LFO. При mix 0 и нулевых
// глубинах модуляции сухой путь остаётся прежним.
// Этап 1.7 добавляет четыре MSEG на голос и матрицу. Жёсткие маршруты не переносятся
// в слоты: нулевой слот к ним ничего не прибавляет. MSEG голос не освобождает.
class MinimalVoiceCore
{
public:
    static constexpr int partCount = 8;
    static constexpr int voicesPerPart = 8;
    static constexpr int tailsPerPart = 8;
    static constexpr int oscillatorCount = 3;
    static constexpr int lfoCount = 4;
    static constexpr int filterTypeCount = 14;
    static constexpr int lfoShapeCount = 6;
    static constexpr int syncDivisionCount = 11;
    static constexpr int combCapacity = 8192;
    static constexpr float stealFadeSeconds = 0.004f;

    MinimalVoiceCore();

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    void setPartEnabled(int partIndex, bool enabled) noexcept;
    void noteOn(int partIndex, int channel, int noteNumber, float velocity, bool mono, bool legato, const std::array<bool, lfoCount>& lfoRetrig) noexcept;
    void noteOff(int partIndex, int channel, int noteNumber) noexcept;
    void setSustain(int partIndex, int channel, bool down) noexcept;
    void setModWheel(int partIndex, int channel, float value) noexcept;
    void publishMsegPoints(const juce::var& unknown);
    void latchModulation(const int* modes) noexcept;
    float renderSample(const EngineControls& controls) noexcept;
    int activeVoiceCount() const noexcept;
    int activeVoiceCountForPart(int partIndex) const noexcept;
    int activeTailCount() const noexcept;
    bool isNoteActive(int partIndex, int channel, int noteNumber) const noexcept;

    static float lfoBipolar(int shape, double phase, float held) noexcept;
    static float lfoPolarity(float bipolarValue, bool unipolar) noexcept;
    static float randomHold(std::uint32_t& state) noexcept;
    static double syncFrequencyHz(int divisionIndex, double bpm) noexcept;

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

    struct SvfStage
    {
        float ic1 = 0.0f;
        float ic2 = 0.0f;
    };

    struct LfoRuntime
    {
        double phase = 0.0;
        float held = 0.0f;
        std::uint32_t rng = 1;
    };

    struct FilterMemory
    {
        std::array<SvfStage, 3> svf {};
        int combCursor = 0;
        float cutoffLog = 0.0f;
        bool cutoffReady = false;
        float pitchSemitones = 0.0f;
        bool pitchReady = false;
        float envelope = 0.0f;
        float releaseOrigin = 0.0f;
        Stage stage = Stage::attack;
    };

    struct MsegRuntime
    {
        double time = 0.0;
        float output = 0.0f;
        int activity = msegActivityHold;
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
        FilterMemory filter {};
        std::array<LfoRuntime, lfoCount> lfo {};
        std::array<MsegRuntime, msegCount> mseg {};
    };

    struct Tail
    {
        bool active = false;
        int channel = 0;
        int note = -1;
        float level = 0.0f;
        float ampEnvelope = 0.0f;
        float velocity = 0.0f;
        std::array<float, msegCount> msegOutput {};
        std::array<double, oscillatorCount> phase {};
        std::uint32_t noiseState = 1;
        float fadePosition = 0.0f;
        std::uint64_t age = 0;
        FilterMemory filter {};
        std::array<LfoRuntime, lfoCount> lfo {};
    };

    struct PartPool
    {
        std::array<Voice, voicesPerPart> voices {};
        std::array<Tail, tailsPerPart> tails {};
        bool enabled = false;
        std::array<bool, 16> sustainDown {};
        std::array<float, 16> modWheel {};
        std::array<LfoRuntime, lfoCount> lfo {};
    };

    Voice* voiceFor(PartPool& part, int channel, int noteNumber) noexcept;
    Voice* idleVoice(PartPool& part) noexcept;
    Voice* stealWithin(PartPool& part) noexcept;
    void pushTail(PartPool& part, int partIndex, const Voice& voice) noexcept;
    void assign(Voice& voice, int partIndex, int channel, int noteNumber, float velocity, bool restartEnvelope, bool fresh, bool crossfade, const std::array<bool, lfoCount>& lfoRetrig) noexcept;
    void beginRelease(Voice& voice, int partIndex) noexcept;
    void restartMseg(Voice& voice, int partIndex) noexcept;
    void releaseMseg(Voice& voice, int partIndex) noexcept;
    void advanceMseg(Voice& voice, int partIndex) noexcept;
    ModOffsets offsetsFor(int partIndex, bool tail, int channel, int noteNumber, float ampEnvelope, float filterEnvelope, float velocity, const std::array<float, msegCount>& msegOutputs, const std::array<float, lfoCount>& lfoValues, const EngineControls& controls) const noexcept;
    void releasePart(PartPool& part) noexcept;
    void freeVoice(Voice& voice) noexcept;
    void freeTail(Tail& tail) noexcept;
    void advanceAmp(Voice& voice, const PartControls& controls) noexcept;
    void advanceFilterEnvelope(FilterMemory& filter, const PartControls& controls) noexcept;
    void beginFilterRelease(FilterMemory& filter) noexcept;
    void resetRetriggers(std::array<LfoRuntime, lfoCount>& lfo, int partIndex, int channel, int noteNumber, std::uint64_t age, const std::array<bool, lfoCount>& lfoRetrig) noexcept;
    void advanceSharedLfos(PartPool& pool, const PartControls& controls, float tempoBpm) noexcept;
    float sourceSample(int partIndex, int slot, bool tail, int noteNumber, std::array<double, oscillatorCount>& phase, std::uint32_t& noiseState, FilterMemory& filter, const std::array<float, lfoCount>& lfoValues, const PartControls& controls, const ModOffsets& offsets, float smooth) noexcept;
    float filtered(int partIndex, int slot, bool tail, FilterMemory& filter, float mixer, int noteNumber, float lfoCutoff, const PartControls& controls, const ModOffsets& offsets, float smooth) noexcept;
    void clearComb(int partIndex, int slot, bool tail) noexcept;
    int combOffset(int partIndex, int slot, bool tail) const noexcept;
    int voiceIndex(const PartPool& part, const Voice& voice) const noexcept;
    int countPart(const PartPool& part) const noexcept;
    static bool older(const Voice& candidate, const Voice* best) noexcept;
    static float waveform(int wave, double phase) noexcept;
    static float nextNoise(std::uint32_t& state) noexcept;
    static float hannDown(float position) noexcept;
    static float secondsOrMinimum(float seconds) noexcept;
    static float unitLevel(float value) noexcept;
    static std::uint32_t noiseSeed(int partIndex, int channel, int noteNumber, std::uint64_t age) noexcept;
    static void stepLfo(LfoRuntime& lfo, int shape, double hz, double sampleRate) noexcept;
    double lfoHz(const LfoControls& lfo, float tempoBpm, bool perVoice, int partIndex, int channel, int noteNumber, std::uint64_t age, int lfoIndex) const noexcept;

    std::array<PartPool, partCount> pools {};
    std::vector<float> combBuffer;
    MsegBank published {};
    MsegBank latched {};
    std::array<int, msegModuleCount> latchedModes {};
    std::atomic<std::uint32_t> msegSequence { 0 };
    double sampleRate = 0.0;
    std::uint64_t nextAge = 1;
};

} // namespace xenmilenium
