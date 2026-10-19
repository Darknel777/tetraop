#pragma once

#include <array>

namespace xenmilenium
{

struct AmplitudeControls
{
    float attackSeconds = 0.01f;
    float decaySeconds = 0.2f;
    float sustain = 0.8f;
    float releaseSeconds = 0.4f;
    float masterGain = 0.8f;
    int wave = 0;
};

// Один пул этапа 1.3. Это не движок восьми партий и не алгоритм кражи.
class MinimalVoiceCore
{
public:
    static constexpr int voiceCapacity = 8;

    void prepare(double newSampleRate) noexcept;
    void reset() noexcept;
    void setPartEnabled(bool enabled) noexcept;
    void noteOn(int noteNumber, float velocity) noexcept;
    void noteOff(int noteNumber) noexcept;
    void releaseAll() noexcept;
    float renderSample(const AmplitudeControls& controls) noexcept;
    int activeVoiceCount() const noexcept;

private:
    enum class Stage
    {
        idle,
        attack,
        decay,
        sustain,
        release
    };

    struct Voice
    {
        bool active = false;
        int note = -1;
        float velocity = 0.0f;
        double phase = 0.0;
        float envelope = 0.0f;
        float releaseOrigin = 0.0f;
        Stage stage = Stage::idle;
    };

    Voice* voiceForNote(int noteNumber) noexcept;
    Voice* idleVoice() noexcept;
    void freeVoice(Voice& voice) noexcept;
    void advance(Voice& voice, const AmplitudeControls& controls) noexcept;
    static float waveform(int wave, double phase) noexcept;
    static float secondsOrMinimum(float seconds) noexcept;

    std::array<Voice, voiceCapacity> voices {};
    double sampleRate = 0.0;
    bool partEnabled = true;
};

} // namespace xenmilenium
