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
    for (auto& pool : pools)
    {
        for (auto& voice : pool.voices)
            freeVoice(voice);
        for (auto& tail : pool.tails)
            freeTail(tail);
        pool.enabled = false;
        pool.sustainDown = {};
    }
    nextAge = 1;
}

void MinimalVoiceCore::setPartEnabled(int partIndex, bool enabled) noexcept
{
    if (! validPart(partIndex))
        return;
    auto& pool = pools[static_cast<std::size_t>(partIndex)];
    if (pool.enabled && ! enabled)
        releasePart(pool);
    pool.enabled = enabled;
}

void MinimalVoiceCore::noteOn(int partIndex, int channel, int noteNumber, float velocity, bool mono, bool legato) noexcept
{
    if (! validPart(partIndex) || ! pools[static_cast<std::size_t>(partIndex)].enabled || ! validChannel(channel))
        return;
    if (! juce::isPositiveAndBelow(noteNumber, 128) || ! std::isfinite(velocity) || velocity <= 0.0f)
        return;

    auto& pool = pools[static_cast<std::size_t>(partIndex)];
    const float level = juce::jmin(velocity, 1.0f);
    const bool oneVoice = mono || legato;

    if (auto* existing = voiceFor(pool, channel, noteNumber))
    {
        if (oneVoice)
        {
            for (auto& voice : pool.voices)
                if (voice.active && &voice != existing)
                    beginRelease(voice);
        }
        assign(*existing, partIndex, channel, noteNumber, level, ! legato, false, false);
        return;
    }

    if (oneVoice)
    {
        Voice* keeper = nullptr;
        for (auto& voice : pool.voices)
            if (voice.active && older(voice, keeper))
                keeper = &voice;
        for (auto& voice : pool.voices)
            if (voice.active && &voice != keeper)
                beginRelease(voice);
        if (keeper != nullptr)
        {
            pushTail(pool, *keeper);
            assign(*keeper, partIndex, channel, noteNumber, level, ! legato, ! legato, legato);
            return;
        }
    }

    Voice* slot = nullptr;
    if (countPart(pool) >= voicesPerPart)
    {
        slot = stealWithin(pool);
        if (slot != nullptr)
            pushTail(pool, *slot);
    }
    else
    {
        slot = idleVoice(pool);
    }

    if (slot == nullptr)
        return;

    assign(*slot, partIndex, channel, noteNumber, level, true, true, false);
}

void MinimalVoiceCore::noteOff(int partIndex, int channel, int noteNumber) noexcept
{
    if (! validPart(partIndex))
        return;

    auto& pool = pools[static_cast<std::size_t>(partIndex)];
    auto* voice = voiceFor(pool, channel, noteNumber);
    if (voice == nullptr)
        return;

    voice->keyHeld = false;
    if (validChannel(channel) && pool.sustainDown[static_cast<std::size_t>(channel - 1)])
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

    auto& pool = pools[static_cast<std::size_t>(partIndex)];
    pool.sustainDown[static_cast<std::size_t>(channel - 1)] = down;
    if (down)
        return;

    for (auto& voice : pool.voices)
        if (voice.active && voice.channel == channel && voice.waitingForPedal)
            beginRelease(voice);
}

float MinimalVoiceCore::renderSample(const EngineControls& controls) noexcept
{
    if (! (sampleRate > 0.0))
        return 0.0f;

    const float master = std::isfinite(controls.masterGain) ? juce::jlimit(0.0f, 1.0f, controls.masterGain) : 0.0f;
    const float fadeStep = 1.0f / (stealFadeSeconds * static_cast<float>(sampleRate));
    double sum = 0.0;

    for (int partIndex = 0; partIndex < partCount; ++partIndex)
    {
        auto& pool = pools[static_cast<std::size_t>(partIndex)];
        const auto& part = controls.parts[static_cast<std::size_t>(partIndex)];

        for (auto& voice : pool.voices)
        {
            if (! voice.active)
                continue;

            float dry = sourceSample(voice.note, voice.phase, voice.noiseState, part);
            if (voice.entryFading)
            {
                dry *= 1.0f - hannDown(voice.entryPosition);
                voice.entryPosition += fadeStep;
                if (voice.entryPosition >= 1.0f)
                    voice.entryFading = false;
            }

            sum += static_cast<double>(dry) * voice.envelope * voice.velocity;
            advance(voice, part);
        }

        for (auto& tail : pool.tails)
        {
            if (! tail.active)
                continue;

            const float dry = sourceSample(tail.note, tail.phase, tail.noiseState, part);
            sum += static_cast<double>(dry) * tail.level * hannDown(tail.fadePosition);
            tail.fadePosition += fadeStep;
            if (tail.fadePosition >= 1.0f)
                freeTail(tail);
        }
    }

    const float sample = std::tanh(static_cast<float>(sum) * sumHeadroom) * master;
    if (! std::isfinite(sample) || ! (std::abs(sample) > 0.0f))
        return 0.0f;
    return sample;
}

int MinimalVoiceCore::activeVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& pool : pools)
        count += countPart(pool);
    return count;
}

int MinimalVoiceCore::activeVoiceCountForPart(int partIndex) const noexcept
{
    if (! validPart(partIndex))
        return 0;
    return countPart(pools[static_cast<std::size_t>(partIndex)]);
}

int MinimalVoiceCore::activeTailCount() const noexcept
{
    int count = 0;
    for (const auto& pool : pools)
        for (const auto& tail : pool.tails)
            if (tail.active)
                ++count;
    return count;
}

bool MinimalVoiceCore::isNoteActive(int partIndex, int channel, int noteNumber) const noexcept
{
    if (! validPart(partIndex))
        return false;
    for (const auto& voice : pools[static_cast<std::size_t>(partIndex)].voices)
        if (voice.active && voice.channel == channel && voice.note == noteNumber)
            return true;
    return false;
}

MinimalVoiceCore::Voice* MinimalVoiceCore::voiceFor(PartPool& part, int channel, int noteNumber) noexcept
{
    for (auto& voice : part.voices)
        if (voice.active && voice.channel == channel && voice.note == noteNumber)
            return &voice;
    return nullptr;
}

MinimalVoiceCore::Voice* MinimalVoiceCore::idleVoice(PartPool& part) noexcept
{
    for (auto& voice : part.voices)
        if (! voice.active)
            return &voice;
    return nullptr;
}

bool MinimalVoiceCore::older(const Voice& candidate, const Voice* best) noexcept
{
    return best == nullptr || candidate.age < best->age;
}

MinimalVoiceCore::Voice* MinimalVoiceCore::stealWithin(PartPool& part) noexcept
{
    Voice* releasing = nullptr;
    Voice* any = nullptr;
    for (auto& voice : part.voices)
    {
        if (! voice.active)
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

void MinimalVoiceCore::pushTail(PartPool& part, const Voice& voice) noexcept
{
    if (! voice.active || voice.envelope <= silenceThreshold)
        return;

    Tail* slot = nullptr;
    for (auto& tail : part.tails)
    {
        if (! tail.active)
        {
            slot = &tail;
            break;
        }
    }

    // Все восемь хвостов заняты: замещается самый затухший. Его остаток обрывается.
    if (slot == nullptr)
    {
        for (auto& tail : part.tails)
            if (slot == nullptr || tail.fadePosition > slot->fadePosition)
                slot = &tail;
    }

    if (slot == nullptr)
        return;

    *slot = {};
    slot->active = true;
    slot->channel = voice.channel;
    slot->note = voice.note;
    slot->level = voice.envelope * voice.velocity;
    slot->phase = voice.phase;
    slot->noiseState = voice.noiseState;
    slot->fadePosition = 0.0f;
}

void MinimalVoiceCore::assign(Voice& voice, int partIndex, int channel, int noteNumber, float velocity, bool restartEnvelope, bool fresh, bool crossfade) noexcept
{
    voice.active = true;
    voice.channel = channel;
    voice.note = noteNumber;
    voice.velocity = velocity;
    voice.keyHeld = true;
    voice.waitingForPedal = false;
    voice.age = nextAge++;

    if (fresh)
    {
        voice.phase = {};
        voice.envelope = 0.0f;
        voice.releaseOrigin = 0.0f;
        voice.stage = Stage::attack;
        voice.entryFading = false;
        voice.entryPosition = 0.0f;
        voice.noiseState = noiseSeed(partIndex, channel, noteNumber, voice.age);
        return;
    }

    if (crossfade)
    {
        voice.phase = {};
        voice.entryFading = true;
        voice.entryPosition = 0.0f;
        voice.noiseState = noiseSeed(partIndex, channel, noteNumber, voice.age);
        if (voice.stage == Stage::release || voice.stage == Stage::idle)
            voice.stage = Stage::hold;
        return;
    }

    voice.entryFading = false;
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

void MinimalVoiceCore::releasePart(PartPool& part) noexcept
{
    for (auto& voice : part.voices)
        if (voice.active)
            beginRelease(voice);
}

void MinimalVoiceCore::freeVoice(Voice& voice) noexcept
{
    voice = {};
}

void MinimalVoiceCore::freeTail(Tail& tail) noexcept
{
    tail = {};
}

int MinimalVoiceCore::countPart(const PartPool& part) const noexcept
{
    int count = 0;
    for (const auto& voice : part.voices)
        if (voice.active)
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

float MinimalVoiceCore::sourceSample(int noteNumber, std::array<double, oscillatorCount>& phase, std::uint32_t& noiseState, const PartControls& controls) noexcept
{
    float oscillator[oscillatorCount];
    const double increment = static_cast<double>(equalTemperamentFrequency(noteNumber)) / sampleRate;
    for (int index = 0; index < oscillatorCount; ++index)
    {
        const double cycle = phase[static_cast<std::size_t>(index)] - std::floor(phase[static_cast<std::size_t>(index)]);
        oscillator[index] = waveform(controls.wave[static_cast<std::size_t>(index)], cycle);
        phase[static_cast<std::size_t>(index)] += increment;
        if (phase[static_cast<std::size_t>(index)] >= 1.0)
            phase[static_cast<std::size_t>(index)] -= std::floor(phase[static_cast<std::size_t>(index)]);
    }

    const float noise = nextNoise(noiseState);
    const float osc1 = oscillator[0];
    const float osc2 = oscillator[1];
    // Микшер — граница отсутствующего фильтра. Direct Out OSC3 входит после неё, до Amp.
    const float mixer = osc1 * unitLevel(controls.osc1Level)
                      + osc2 * unitLevel(controls.osc2Level)
                      + (osc1 * osc2) * unitLevel(controls.ringLevel)
                      + noise * unitLevel(controls.noiseLevel);
    return mixer + oscillator[2] * unitLevel(controls.directOut);
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

float MinimalVoiceCore::nextNoise(std::uint32_t& state) noexcept
{
    if (state == 0u)
        state = 1u;
    std::uint32_t value = state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    state = value == 0u ? 1u : value;
    return static_cast<float>(static_cast<std::int32_t>(state)) * (1.0f / 2147483648.0f);
}

float MinimalVoiceCore::hannDown(float position) noexcept
{
    const float wrapped = juce::jlimit(0.0f, 1.0f, position);
    return 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * wrapped));
}

float MinimalVoiceCore::secondsOrMinimum(float seconds) noexcept
{
    return std::isfinite(seconds) && seconds > minimumSeconds ? seconds : minimumSeconds;
}

float MinimalVoiceCore::unitLevel(float value) noexcept
{
    return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value) : 0.0f;
}

std::uint32_t MinimalVoiceCore::noiseSeed(int partIndex, int channel, int noteNumber, std::uint64_t age) noexcept
{
    std::uint32_t seed = 0xA3C59AC3u;
    seed ^= static_cast<std::uint32_t>(partIndex) * 0x9E3779B9u;
    seed ^= static_cast<std::uint32_t>(channel) * 0x85EBCA6Bu;
    seed ^= static_cast<std::uint32_t>(noteNumber) * 0xC2B2AE35u;
    seed ^= static_cast<std::uint32_t>(age);
    return seed == 0u ? 1u : seed;
}

} // namespace xenmilenium
