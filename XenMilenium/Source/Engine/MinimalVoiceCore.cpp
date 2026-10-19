#include "Engine/MinimalVoiceCore.h"

#include <JuceHeader.h>

#include <cmath>

namespace xenmilenium
{
namespace
{
constexpr float silenceThreshold = 1.0e-4f;
constexpr float minimumSeconds = 0.001f;

float equalTemperamentFrequency(int noteNumber) noexcept
{
    return static_cast<float>(440.0 * std::exp2((static_cast<double>(noteNumber) - 69.0) / 12.0));
}
} // namespace

void MinimalVoiceCore::prepare(double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 0.0;
    reset();
}

void MinimalVoiceCore::reset() noexcept
{
    for (auto& voice : voices)
        freeVoice(voice);
    partEnabled = true;
}

void MinimalVoiceCore::setPartEnabled(bool enabled) noexcept
{
    if (partEnabled && ! enabled)
        releaseAll();
    partEnabled = enabled;
}

void MinimalVoiceCore::noteOn(int noteNumber, float velocity) noexcept
{
    if (! partEnabled || ! juce::isPositiveAndBelow(noteNumber, 128))
        return;
    if (! std::isfinite(velocity) || velocity <= 0.0f)
        return;

    const float level = juce::jmin(velocity, 1.0f);
    if (auto* existing = voiceForNote(noteNumber))
    {
        existing->velocity = level;
        existing->stage = Stage::attack;
        return;
    }

    auto* voice = idleVoice();
    if (voice == nullptr)
        return;

    voice->active = true;
    voice->note = noteNumber;
    voice->velocity = level;
    voice->phase = 0.0;
    voice->envelope = 0.0f;
    voice->releaseOrigin = 0.0f;
    voice->stage = Stage::attack;
}

void MinimalVoiceCore::noteOff(int noteNumber) noexcept
{
    auto* voice = voiceForNote(noteNumber);
    if (voice == nullptr || voice->stage == Stage::release)
        return;

    if (voice->envelope <= silenceThreshold)
    {
        freeVoice(*voice);
        return;
    }

    voice->releaseOrigin = voice->envelope;
    voice->stage = Stage::release;
}

void MinimalVoiceCore::releaseAll() noexcept
{
    for (const auto& voice : voices)
    {
        if (voice.active && voice.stage != Stage::release)
            noteOff(voice.note);
    }
}

float MinimalVoiceCore::renderSample(const AmplitudeControls& controls) noexcept
{
    if (! (sampleRate > 0.0))
        return 0.0f;

    const float master = std::isfinite(controls.masterGain) ? juce::jlimit(0.0f, 1.0f, controls.masterGain) : 0.0f;
    double sum = 0.0;

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        const float cycle = static_cast<float>(voice.phase - std::floor(voice.phase));
        sum += static_cast<double>(waveform(controls.wave, cycle)) * voice.envelope * voice.velocity;

        const double increment = static_cast<double>(equalTemperamentFrequency(voice.note)) / sampleRate;
        voice.phase += increment;
        if (voice.phase >= 1.0)
            voice.phase -= std::floor(voice.phase);

        advance(voice, controls);
    }

    const float sample = static_cast<float>(sum / static_cast<double>(voiceCapacity)) * master;
    if (! std::isfinite(sample) || ! (std::abs(sample) > 0.0f))
        return 0.0f;
    return sample;
}

int MinimalVoiceCore::activeVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.active)
            ++count;
    return count;
}

MinimalVoiceCore::Voice* MinimalVoiceCore::voiceForNote(int noteNumber) noexcept
{
    for (auto& voice : voices)
        if (voice.active && voice.note == noteNumber)
            return &voice;
    return nullptr;
}

MinimalVoiceCore::Voice* MinimalVoiceCore::idleVoice() noexcept
{
    for (auto& voice : voices)
        if (! voice.active)
            return &voice;
    return nullptr;
}

void MinimalVoiceCore::freeVoice(Voice& voice) noexcept
{
    voice.active = false;
    voice.note = -1;
    voice.velocity = 0.0f;
    voice.phase = 0.0;
    voice.envelope = 0.0f;
    voice.releaseOrigin = 0.0f;
    voice.stage = Stage::idle;
}

void MinimalVoiceCore::advance(Voice& voice, const AmplitudeControls& controls) noexcept
{
    const float sustain = std::isfinite(controls.sustain) ? juce::jlimit(0.0f, 1.0f, controls.sustain) : 0.0f;
    const float sampleRateF = static_cast<float>(sampleRate);

    switch (voice.stage)
    {
        case Stage::attack:
        {
            voice.envelope += 1.0f / (secondsOrMinimum(controls.attackSeconds) * sampleRateF);
            if (voice.envelope >= 1.0f)
            {
                voice.envelope = 1.0f;
                voice.stage = Stage::decay;
            }
            break;
        }

        case Stage::decay:
        {
            const float distance = 1.0f - sustain;
            if (distance <= silenceThreshold || voice.envelope <= sustain)
            {
                voice.envelope = sustain;
                voice.stage = Stage::sustain;
                break;
            }

            voice.envelope -= distance / (secondsOrMinimum(controls.decaySeconds) * sampleRateF);
            if (voice.envelope <= sustain)
            {
                voice.envelope = sustain;
                voice.stage = Stage::sustain;
            }
            break;
        }

        case Stage::sustain:
            voice.envelope = sustain;
            break;

        case Stage::release:
        {
            voice.envelope -= voice.releaseOrigin / (secondsOrMinimum(controls.releaseSeconds) * sampleRateF);
            if (voice.envelope <= silenceThreshold)
                freeVoice(voice);
            break;
        }

        case Stage::idle:
            break;
    }
}

float MinimalVoiceCore::waveform(int wave, double phase) noexcept
{
    const double wrapped = phase - std::floor(phase);
    switch (wave)
    {
        case 1:
            return static_cast<float>(2.0 * wrapped - 1.0);
        case 2:
            return wrapped < 0.5 ? static_cast<float>(4.0 * wrapped - 1.0)
                                 : static_cast<float>(3.0 - 4.0 * wrapped);
        case 3:
            return wrapped < 0.5 ? 1.0f : -1.0f;
        default:
            return static_cast<float>(std::sin(wrapped * juce::MathConstants<double>::twoPi));
    }
}

float MinimalVoiceCore::secondsOrMinimum(float seconds) noexcept
{
    return std::isfinite(seconds) && seconds > minimumSeconds ? seconds : minimumSeconds;
}

} // namespace xenmilenium
