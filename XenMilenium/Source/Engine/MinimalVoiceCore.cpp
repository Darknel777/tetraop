#include "Engine/MinimalVoiceCore.h"

#include <JuceHeader.h>

#include <cmath>

namespace xenmilenium
{
namespace
{
constexpr float silenceThreshold = 1.0e-4f;
constexpr float minimumSeconds = 0.001f;
constexpr float sumHeadroom = 0.25f;

float equalTemperamentFrequency(int noteNumber) noexcept
{
    return static_cast<float>(440.0 * std::exp2((static_cast<double>(noteNumber) - 69.0) / 12.0));
}

bool validPart(int partIndex) noexcept
{
    return juce::isPositiveAndBelow(partIndex, MinimalVoiceCore::partCount);
}

bool validChannel(int channel) noexcept
{
    return channel >= 1 && channel <= 16;
}
} // namespace

void MinimalVoiceCore::prepare(double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 0.0;
}

void MinimalVoiceCore::reset() noexcept
{
    for (auto& voice : voices)
        freeVoice(voice);
    sustainDown = {};
    partEnabled = {};
    nextAge = 1;
}

void MinimalVoiceCore::setPartEnabled(int partIndex, bool enabled) noexcept
{
    if (! validPart(partIndex))
        return;
    if (partEnabled[static_cast<std::size_t>(partIndex)] && ! enabled)
        releasePart(partIndex);
    partEnabled[static_cast<std::size_t>(partIndex)] = enabled;
}

void MinimalVoiceCore::noteOn(int partIndex, int channel, int noteNumber, float velocity, bool mono, bool legato) noexcept
{
    if (! validPart(partIndex) || ! partEnabled[static_cast<std::size_t>(partIndex)] || ! validChannel(channel))
        return;
    if (! juce::isPositiveAndBelow(noteNumber, 128) || ! std::isfinite(velocity) || velocity <= 0.0f)
        return;

    const float level = juce::jmin(velocity, 1.0f);
    const bool oneVoice = mono || legato;

    if (auto* existing = voiceFor(partIndex, channel, noteNumber))
    {
        if (oneVoice)
        {
            for (auto& voice : voices)
                if (voice.active && voice.part == partIndex && &voice != existing)
                    beginRelease(voice);
        }
        assign(*existing, partIndex, channel, noteNumber, level, ! legato, false);
        return;
    }

    if (oneVoice)
    {
        Voice* keeper = nullptr;
        for (auto& voice : voices)
            if (voice.active && voice.part == partIndex && older(voice, keeper))
                keeper = &voice;
        for (auto& voice : voices)
            if (voice.active && voice.part == partIndex && &voice != keeper)
                beginRelease(voice);
        if (keeper != nullptr)
        {
            assign(*keeper, partIndex, channel, noteNumber, level, ! legato, false);
            return;
        }
    }

    const int used = countPart(partIndex);
    Voice* slot = nullptr;
    bool fresh = false;
    if (used >= voicesPerPart)
    {
        slot = stealWithin(partIndex);
    }
    else if (auto* idle = idleVoice())
    {
        slot = idle;
        fresh = true;
    }
    else
    {
        slot = stealGloballyPreferringOthers(partIndex);
    }

    if (slot == nullptr)
        return;

    assign(*slot, partIndex, channel, noteNumber, level, true, fresh);
}

void MinimalVoiceCore::noteOff(int partIndex, int channel, int noteNumber) noexcept
{
    auto* voice = voiceFor(partIndex, channel, noteNumber);
    if (voice == nullptr)
        return;

    voice->keyHeld = false;
    if (validPart(partIndex) && validChannel(channel)
        && sustainDown[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(channel - 1)])
    {
        voice->waitingForPedal = true;
        return;
    }

    beginRelease(*voice);
}

void MinimalVoiceCore::setSustain(int partIndex, int channel, bool down) noexcept
{
    if (! validPart(partIndex) || ! validChannel(channel))
        return;

    sustainDown[static_cast<std::size_t>(partIndex)][static_cast<std::size_t>(channel - 1)] = down;
    if (down)
        return;

    for (auto& voice : voices)
        if (voice.active && voice.part == partIndex && voice.channel == channel && voice.waitingForPedal)
            beginRelease(voice);
}

float MinimalVoiceCore::renderSample(const EngineControls& controls) noexcept
{
    if (! (sampleRate > 0.0))
        return 0.0f;

    const float master = std::isfinite(controls.masterGain) ? juce::jlimit(0.0f, 1.0f, controls.masterGain) : 0.0f;
    double sum = 0.0;

    for (auto& voice : voices)
    {
        if (! voice.active || ! validPart(voice.part))
            continue;

        const auto& part = controls.parts[static_cast<std::size_t>(voice.part)];
        const float cycle = static_cast<float>(voice.phase - std::floor(voice.phase));
        sum += static_cast<double>(waveform(part.wave, cycle)) * voice.envelope * voice.velocity;

        const double increment = static_cast<double>(equalTemperamentFrequency(voice.note)) / sampleRate;
        voice.phase += increment;
        if (voice.phase >= 1.0)
            voice.phase -= std::floor(voice.phase);

        advance(voice, part);
    }

    const float sample = std::tanh(static_cast<float>(sum) * sumHeadroom) * master;
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

int MinimalVoiceCore::activeVoiceCountForPart(int partIndex) const noexcept
{
    return countPart(partIndex);
}

bool MinimalVoiceCore::isNoteActive(int partIndex, int channel, int noteNumber) const noexcept
{
    for (const auto& voice : voices)
        if (voice.active && voice.part == partIndex && voice.channel == channel && voice.note == noteNumber)
            return true;
    return false;
}

MinimalVoiceCore::Voice* MinimalVoiceCore::voiceFor(int partIndex, int channel, int noteNumber) noexcept
{
    for (auto& voice : voices)
        if (voice.active && voice.part == partIndex && voice.channel == channel && voice.note == noteNumber)
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

bool MinimalVoiceCore::older(const Voice& candidate, const Voice* best) noexcept
{
    return best == nullptr || candidate.age < best->age;
}

MinimalVoiceCore::Voice* MinimalVoiceCore::stealWithin(int partIndex) noexcept
{
    Voice* releasing = nullptr;
    Voice* any = nullptr;
    for (auto& voice : voices)
    {
        if (! voice.active || voice.part != partIndex)
            continue;
        if (voice.stage == Stage::release)
        {
            if (older(voice, releasing))
                releasing = &voice;
        }
        else if (older(voice, any))
        {
            any = &voice;
        }
    }
    return releasing != nullptr ? releasing : any;
}

MinimalVoiceCore::Voice* MinimalVoiceCore::stealGloballyPreferringOthers(int partIndex) noexcept
{
    Voice* otherRelease = nullptr;
    Voice* otherAny = nullptr;
    for (auto& voice : voices)
    {
        if (! voice.active || voice.part == partIndex)
            continue;
        if (voice.stage == Stage::release)
        {
            if (older(voice, otherRelease))
                otherRelease = &voice;
        }
        else if (older(voice, otherAny))
        {
            otherAny = &voice;
        }
    }
    if (otherRelease != nullptr)
        return otherRelease;
    if (otherAny != nullptr)
        return otherAny;
    return stealWithin(partIndex);
}

void MinimalVoiceCore::assign(Voice& voice, int partIndex, int channel, int noteNumber, float velocity, bool restartEnvelope, bool fresh) noexcept
{
    voice.active = true;
    voice.part = partIndex;
    voice.channel = channel;
    voice.note = noteNumber;
    voice.velocity = velocity;
    voice.keyHeld = true;
    voice.waitingForPedal = false;
    voice.age = nextAge++;

    if (fresh)
    {
        voice.phase = 0.0;
        voice.envelope = 0.0f;
        voice.releaseOrigin = 0.0f;
        voice.stage = Stage::attack;
        return;
    }

    if (restartEnvelope)
        voice.stage = Stage::attack;
    else if (voice.stage == Stage::release || voice.stage == Stage::idle)
        voice.stage = Stage::hold;
}

void MinimalVoiceCore::beginRelease(Voice& voice) noexcept
{
    if (! voice.active || voice.stage == Stage::release)
        return;

    voice.waitingForPedal = false;
    if (voice.envelope <= silenceThreshold)
    {
        freeVoice(voice);
        return;
    }

    voice.releaseOrigin = voice.envelope;
    voice.stage = Stage::release;
}

void MinimalVoiceCore::releasePart(int partIndex) noexcept
{
    for (auto& voice : voices)
        if (voice.active && voice.part == partIndex)
            beginRelease(voice);
}

void MinimalVoiceCore::freeVoice(Voice& voice) noexcept
{
    voice = {};
}

int MinimalVoiceCore::countPart(int partIndex) const noexcept
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.active && voice.part == partIndex)
            ++count;
    return count;
}

void MinimalVoiceCore::advance(Voice& voice, const PartControls& controls) noexcept
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

        case Stage::hold:
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
