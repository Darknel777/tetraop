#include "Engine/MinimalVoiceCore.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <cstring>

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

float flushDenormal(float value) noexcept
{
    return std::abs(value) < 1.0e-20f ? 0.0f : value;
}

float svfTick(float& ic1, float& ic2, float input, float g, float k, int mode) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    const float v3 = input - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = flushDenormal(2.0f * v1 - ic1);
    ic2 = flushDenormal(2.0f * v2 - ic2);

    if (mode == 0)
        return v2;
    if (mode == 1)
        return v1;
    if (mode == 2)
        return input - k * v1 - v2;
    return input - k * v1;
}
} // namespace

MinimalVoiceCore::MinimalVoiceCore()
{
    combBuffer.assign(static_cast<std::size_t>(partCount) * static_cast<std::size_t>(voicesPerPart + tailsPerPart)
                          * static_cast<std::size_t>(combCapacity),
                      0.0f);
}

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
        pool.modWheel = {};
        pool.lfo = {};
    }
    std::fill(combBuffer.begin(), combBuffer.end(), 0.0f);
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

void MinimalVoiceCore::noteOn(int partIndex, int channel, int noteNumber, float velocity, bool mono, bool legato, const std::array<bool, lfoCount>& lfoRetrig) noexcept
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
                    beginRelease(voice, partIndex);
        }
        assign(*existing, partIndex, channel, noteNumber, level, ! legato, false, false, lfoRetrig);
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
                beginRelease(voice, partIndex);
        if (keeper != nullptr)
        {
            pushTail(pool, partIndex, *keeper);
            assign(*keeper, partIndex, channel, noteNumber, level, ! legato, ! legato, legato, lfoRetrig);
            return;
        }
    }

    Voice* slot = nullptr;
    if (countPart(pool) >= voicesPerPart)
    {
        slot = stealWithin(pool);
        if (slot != nullptr)
            pushTail(pool, partIndex, *slot);
    }
    else
    {
        slot = idleVoice(pool);
    }

    if (slot == nullptr)
        return;

    assign(*slot, partIndex, channel, noteNumber, level, true, true, false, lfoRetrig);
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

    beginRelease(*voice, partIndex);
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
            beginRelease(voice, partIndex);
}

void MinimalVoiceCore::setModWheel(int partIndex, int channel, float value) noexcept
{
    if (! validPart(partIndex) || ! validChannel(channel))
        return;

    pools[static_cast<std::size_t>(partIndex)].modWheel[static_cast<std::size_t>(channel - 1)] =
        std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value) : 0.0f;
}

void MinimalVoiceCore::publishMsegPoints(const juce::var& unknown)
{
    const MsegBank next = parseMsegBank(unknown);
    const auto sequence = msegSequence.load(std::memory_order_relaxed);
    msegSequence.store(sequence + 1, std::memory_order_release);
    published = next;
    msegSequence.store(sequence + 2, std::memory_order_release);
}

void MinimalVoiceCore::latchModulation(const int* modes) noexcept
{
    if (modes != nullptr)
    {
        for (int index = 0; index < msegModuleCount; ++index)
            latchedModes[static_cast<std::size_t>(index)] = juce::jlimit(0, 2, modes[index]);
    }

    for (;;)
    {
        const auto sequence = msegSequence.load(std::memory_order_acquire);
        if ((sequence & 1u) != 0u)
            continue;

        latched = published;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (msegSequence.load(std::memory_order_acquire) == sequence)
            break;
    }
}

float MinimalVoiceCore::renderSample(const EngineControls& controls) noexcept
{
    if (! (sampleRate > 0.0))
        return 0.0f;

    const float master = std::isfinite(controls.masterGain) ? juce::jlimit(0.0f, 1.0f, controls.masterGain) : 0.0f;
    const float fadeStep = 1.0f / (stealFadeSeconds * static_cast<float>(sampleRate));
    const float smooth = 1.0f - std::exp(-1.0f / (0.001f * static_cast<float>(sampleRate)));
    const float tempo = std::isfinite(controls.tempoBpm) && controls.tempoBpm > 0.0f ? controls.tempoBpm : 120.0f;
    double sum = 0.0;

    for (int partIndex = 0; partIndex < partCount; ++partIndex)
    {
        auto& pool = pools[static_cast<std::size_t>(partIndex)];
        const auto& part = controls.parts[static_cast<std::size_t>(partIndex)];
        advanceSharedLfos(pool, part, tempo);

        for (int slot = 0; slot < voicesPerPart; ++slot)
        {
            auto& voice = pool.voices[static_cast<std::size_t>(slot)];
            if (! voice.active)
                continue;

            std::array<float, lfoCount> lfoValues {};
            for (int index = 0; index < lfoCount; ++index)
            {
                const auto& spec = part.lfo[static_cast<std::size_t>(index)];
                auto& runtime = spec.retrig ? voice.lfo[static_cast<std::size_t>(index)] : pool.lfo[static_cast<std::size_t>(index)];
                if (spec.retrig)
                    stepLfo(runtime, spec.shape, lfoHz(spec, tempo, true, partIndex, voice.channel, voice.note, voice.age, index), sampleRate);
                lfoValues[static_cast<std::size_t>(index)] = lfoPolarity(lfoBipolar(spec.shape, runtime.phase, runtime.held), spec.unipolar);
            }

            std::array<float, msegCount> msegOutputs {};
            for (int index = 0; index < msegCount; ++index)
                msegOutputs[static_cast<std::size_t>(index)] = voice.mseg[static_cast<std::size_t>(index)].output;
            const auto offsets = offsetsFor(partIndex, false, voice.channel, voice.note, voice.envelope, voice.filter.envelope, voice.velocity, msegOutputs, lfoValues, controls);
            float dry = sourceSample(partIndex, slot, false, voice.note, voice.phase, voice.noiseState, voice.filter, lfoValues, part, offsets, smooth);
            if (voice.entryFading)
            {
                dry *= 1.0f - hannDown(voice.entryPosition);
                voice.entryPosition += fadeStep;
                if (voice.entryPosition >= 1.0f)
                    voice.entryFading = false;
            }

            sum += static_cast<double>(dry) * voice.envelope * voice.velocity;
            advanceAmp(voice, part);
            if (! voice.active)
                continue;
            advanceFilterEnvelope(voice.filter, part);
            advanceMseg(voice, partIndex);
        }

        for (int slot = 0; slot < tailsPerPart; ++slot)
        {
            auto& tail = pool.tails[static_cast<std::size_t>(slot)];
            if (! tail.active)
                continue;

            std::array<float, lfoCount> lfoValues {};
            for (int index = 0; index < lfoCount; ++index)
            {
                const auto& spec = part.lfo[static_cast<std::size_t>(index)];
                auto& runtime = spec.retrig ? tail.lfo[static_cast<std::size_t>(index)] : pool.lfo[static_cast<std::size_t>(index)];
                if (spec.retrig)
                    stepLfo(runtime, spec.shape, lfoHz(spec, tempo, true, partIndex, tail.channel, tail.note, tail.age, index), sampleRate);
                lfoValues[static_cast<std::size_t>(index)] = lfoPolarity(lfoBipolar(spec.shape, runtime.phase, runtime.held), spec.unipolar);
            }

            const auto offsets = offsetsFor(partIndex, true, tail.channel, tail.note, tail.ampEnvelope, tail.filter.envelope, tail.velocity, tail.msegOutput, lfoValues, controls);
            const float dry = sourceSample(partIndex, slot, true, tail.note, tail.phase, tail.noiseState, tail.filter, lfoValues, part, offsets, smooth);
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

void MinimalVoiceCore::pushTail(PartPool& part, int partIndex, const Voice& voice) noexcept
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

    const int fromSlot = voiceIndex(part, voice);
    const int toSlot = static_cast<int>(slot - part.tails.data());
    *slot = {};
    slot->active = true;
    slot->channel = voice.channel;
    slot->note = voice.note;
    slot->level = voice.envelope * voice.velocity;
    slot->ampEnvelope = voice.envelope;
    slot->velocity = voice.velocity;
    for (int index = 0; index < msegCount; ++index)
        slot->msegOutput[static_cast<std::size_t>(index)] = voice.mseg[static_cast<std::size_t>(index)].output;
    slot->phase = voice.phase;
    slot->noiseState = voice.noiseState;
    slot->fadePosition = 0.0f;
    slot->age = voice.age;
    slot->filter = voice.filter;
    slot->lfo = voice.lfo;
    if (! combBuffer.empty())
    {
        std::memcpy(combBuffer.data() + combOffset(partIndex, toSlot, true),
                    combBuffer.data() + combOffset(partIndex, fromSlot, false),
                    static_cast<std::size_t>(combCapacity) * sizeof(float));
    }
}

void MinimalVoiceCore::assign(Voice& voice, int partIndex, int channel, int noteNumber, float velocity, bool restartEnvelope, bool fresh, bool crossfade, const std::array<bool, lfoCount>& lfoRetrig) noexcept
{
    auto& pool = pools[static_cast<std::size_t>(partIndex)];
    const int slot = voiceIndex(pool, voice);
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
        voice.filter = {};
        voice.lfo = {};
        clearComb(partIndex, slot, false);
        resetRetriggers(voice.lfo, partIndex, channel, noteNumber, voice.age, lfoRetrig);
        restartMseg(voice, partIndex);
        return;
    }

    if (crossfade)
    {
        const float filterLevel = voice.filter.envelope;
        const float filterOrigin = voice.filter.releaseOrigin;
        Stage filterStage = voice.filter.stage;
        if (filterStage == Stage::release || filterStage == Stage::idle)
            filterStage = Stage::hold;
        voice.phase = {};
        voice.entryFading = true;
        voice.entryPosition = 0.0f;
        voice.noiseState = noiseSeed(partIndex, channel, noteNumber, voice.age);
        if (voice.stage == Stage::release || voice.stage == Stage::idle)
            voice.stage = Stage::hold;
        voice.filter = {};
        voice.filter.envelope = filterLevel;
        voice.filter.releaseOrigin = filterOrigin;
        voice.filter.stage = filterStage;
        clearComb(partIndex, slot, false);
        resetRetriggers(voice.lfo, partIndex, channel, noteNumber, voice.age, lfoRetrig);
        restartMseg(voice, partIndex);
        return;
    }

    voice.entryFading = false;
    if (restartEnvelope)
    {
        voice.stage = Stage::attack;
        voice.filter.stage = Stage::attack;
    }
    else
    {
        if (voice.stage == Stage::release || voice.stage == Stage::idle)
            voice.stage = Stage::hold;
        if (voice.filter.stage == Stage::release || voice.filter.stage == Stage::idle)
            voice.filter.stage = Stage::hold;
    }
    resetRetriggers(voice.lfo, partIndex, channel, noteNumber, voice.age, lfoRetrig);
    restartMseg(voice, partIndex);
}

void MinimalVoiceCore::beginRelease(Voice& voice, int partIndex) noexcept
{
    if (! voice.active || voice.stage == Stage::release)
        return;

    voice.waitingForPedal = false;
    beginFilterRelease(voice.filter);
    releaseMseg(voice, partIndex);
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
            beginRelease(voice, static_cast<int>(&part - pools.data()));
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

void MinimalVoiceCore::advanceAmp(Voice& voice, const PartControls& controls) noexcept
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

void MinimalVoiceCore::restartMseg(Voice& voice, int partIndex) noexcept
{
    for (int index = 0; index < msegCount; ++index)
    {
        const int bank = partIndex * msegCount + index;
        auto& runtime = voice.mseg[static_cast<std::size_t>(index)];
        runtime.time = 0.0;
        if (latchedModes[static_cast<std::size_t>(bank)] == msegModeNoteOff)
        {
            runtime.activity = msegActivityWait;
            runtime.output = 0.0f;
            continue;
        }

        runtime.activity = msegActivityPlay;
        runtime.output = evaluateMseg(latched.shapes[static_cast<std::size_t>(bank)], 0.0).value;
        if (! std::isfinite(runtime.output))
            runtime.output = 0.0f;
    }
}

void MinimalVoiceCore::releaseMseg(Voice& voice, int partIndex) noexcept
{
    for (int index = 0; index < msegCount; ++index)
    {
        const int bank = partIndex * msegCount + index;
        auto& runtime = voice.mseg[static_cast<std::size_t>(index)];
        const int mode = latchedModes[static_cast<std::size_t>(bank)];
        if (mode == msegModeNoteOff)
        {
            runtime.time = 0.0;
            runtime.activity = msegActivityPlay;
            runtime.output = evaluateMseg(latched.shapes[static_cast<std::size_t>(bank)], 0.0).value;
        }
        else if (mode == msegModeLoop)
        {
            runtime.activity = msegActivityHold;
        }

        if (! std::isfinite(runtime.output))
            runtime.output = 0.0f;
    }
}

void MinimalVoiceCore::advanceMseg(Voice& voice, int partIndex) noexcept
{
    if (! (sampleRate > 0.0))
        return;

    const double step = 1.0 / sampleRate;
    for (int index = 0; index < msegCount; ++index)
    {
        auto& runtime = voice.mseg[static_cast<std::size_t>(index)];
        if (runtime.activity != msegActivityPlay)
            continue;

        const int bank = partIndex * msegCount + index;
        const auto& shape = latched.shapes[static_cast<std::size_t>(bank)];
        const int mode = latchedModes[static_cast<std::size_t>(bank)];
        if (mode == msegModeLoop)
        {
            const double total = msegDuration(shape);
            if (! (total > 1.0e-8))
            {
                runtime.output = evaluateMseg(shape, 0.0).value;
                runtime.activity = msegActivityHold;
            }
            else
            {
                runtime.time += step;
                runtime.time = std::fmod(runtime.time, total);
                if (! std::isfinite(runtime.time) || runtime.time < 0.0)
                    runtime.time = 0.0;
                runtime.output = evaluateMseg(shape, runtime.time).value;
            }
        }
        else
        {
            runtime.time += step;
            const auto cursor = evaluateMseg(shape, runtime.time);
            runtime.output = cursor.value;
            if (cursor.finished)
                runtime.activity = msegActivityHold;
        }

        if (! std::isfinite(runtime.output))
            runtime.output = 0.0f;
    }
}

ModOffsets MinimalVoiceCore::offsetsFor(int partIndex, bool tail, int channel, int noteNumber, float ampEnvelope, float filterEnvelope, float velocity, const std::array<float, msegCount>& msegOutputs, const std::array<float, lfoCount>& lfoValues, const EngineControls& controls) const noexcept
{
    juce::ignoreUnused(tail);
    if (controls.matrix == nullptr || ! controls.matrixActive[static_cast<std::size_t>(partIndex)])
        return {};

    ModSourceValues sources;
    sources.ampEnv = std::isfinite(ampEnvelope) ? juce::jlimit(0.0f, 1.0f, ampEnvelope) : 0.0f;
    sources.filterEnv = std::isfinite(filterEnvelope) ? juce::jlimit(0.0f, 1.0f, filterEnvelope) : 0.0f;
    sources.lfo = lfoValues;
    sources.mseg = msegOutputs;
    sources.velocity = std::isfinite(velocity) ? juce::jlimit(0.0f, 1.0f, velocity) : 0.0f;
    sources.noteKey = noteKeyValue(noteNumber);
    if (validChannel(channel))
        sources.modWheel = pools[static_cast<std::size_t>(partIndex)].modWheel[static_cast<std::size_t>(channel - 1)];
    return sumMatrix(controls.matrix + partIndex * matrixSlotsPerPart, matrixSlotsPerPart, sources);
}

float MinimalVoiceCore::sourceSample(int partIndex, int slot, bool tail, int noteNumber, std::array<double, oscillatorCount>& phase, std::uint32_t& noiseState, FilterMemory& filter, const std::array<float, lfoCount>& lfoValues, const PartControls& controls, const ModOffsets& offsets, float smooth) noexcept
{
    float pitch = 0.0f;
    float targetPitch = lfoValues[1] * controls.lfo2ToPitchSemitones;
    if (std::abs(offsets.pitchSemitones) > 0.0f)
        targetPitch += offsets.pitchSemitones;
    if (std::abs(controls.lfo2ToPitchSemitones) > 0.0f || std::abs(offsets.pitchSemitones) > 0.0f || std::abs(filter.pitchSemitones) > 0.0f)
    {
        if (! filter.pitchReady)
        {
            filter.pitchSemitones = targetPitch;
            filter.pitchReady = true;
        }
        else
        {
            filter.pitchSemitones += (targetPitch - filter.pitchSemitones) * smooth;
        }
        pitch = filter.pitchSemitones;
    }

    const float waveOffset[oscillatorCount] = { offsets.wave1, offsets.wave2, offsets.wave3 };
    int waveIndex[oscillatorCount];
    for (int index = 0; index < oscillatorCount; ++index)
    {
        const int baseWave = controls.wave[static_cast<std::size_t>(index)];
        waveIndex[index] = baseWave;
        if (std::abs(waveOffset[index]) > 0.0f)
            waveIndex[index] = juce::jlimit(0, 3, baseWave + static_cast<int>(std::lround(waveOffset[index])));
    }

    float oscillator[oscillatorCount];
    double increment = static_cast<double>(equalTemperamentFrequency(noteNumber)) / sampleRate;
    if (std::abs(pitch) > 0.0f)
        increment *= std::exp2(static_cast<double>(pitch) / 12.0);
    for (int index = 0; index < oscillatorCount; ++index)
    {
        const double cycle = phase[static_cast<std::size_t>(index)] - std::floor(phase[static_cast<std::size_t>(index)]);
        oscillator[index] = waveform(waveIndex[index], cycle);
        phase[static_cast<std::size_t>(index)] += increment;
        if (phase[static_cast<std::size_t>(index)] >= 1.0)
            phase[static_cast<std::size_t>(index)] -= std::floor(phase[static_cast<std::size_t>(index)]);
    }

    const auto modulatedLevel = [](float base, float offset) noexcept
    {
        const float value = std::abs(offset) > 0.0f ? base + offset : base;
        if (! std::isfinite(value))
            return 0.0f;
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    };

    const float noise = nextNoise(noiseState);
    const float osc1 = oscillator[0];
    const float osc2 = oscillator[1];
    const float mixer = osc1 * modulatedLevel(controls.osc1Level, offsets.osc1)
                      + osc2 * modulatedLevel(controls.osc2Level, offsets.osc2)
                      + (osc1 * osc2) * modulatedLevel(controls.ringLevel, offsets.ring)
                      + noise * modulatedLevel(controls.noiseLevel, offsets.noise);
    const float direct = oscillator[2] * modulatedLevel(controls.directOut, offsets.directOut);
    float mix = unitLevel(controls.filterMix);
    if (std::abs(offsets.mix) > 0.0f)
    {
        const float shifted = controls.filterMix + offsets.mix;
        mix = std::isfinite(shifted) ? unitLevel(shifted) : mix;
    }
    if (! (mix > 0.0f))
        return mixer + direct;

    const float wet = filtered(partIndex, slot, tail, filter, mixer, noteNumber, lfoValues[0], controls, offsets, smooth);
    return mixer * (1.0f - mix) + wet * mix + direct;
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

void MinimalVoiceCore::advanceFilterEnvelope(FilterMemory& filter, const PartControls& controls) noexcept
{
    const float sustain = std::isfinite(controls.filterSustain) ? juce::jlimit(0.0f, 1.0f, controls.filterSustain) : 0.0f;
    const float sampleRateF = static_cast<float>(sampleRate);

    switch (filter.stage)
    {
        case Stage::attack:
        {
            filter.envelope += 1.0f / (secondsOrMinimum(controls.filterAttackSeconds) * sampleRateF);
            if (filter.envelope >= 1.0f)
            {
                filter.envelope = 1.0f;
                filter.stage = Stage::decay;
            }
            break;
        }

        case Stage::decay:
        {
            const float distance = 1.0f - sustain;
            if (distance <= silenceThreshold || filter.envelope <= sustain)
            {
                filter.envelope = sustain;
                filter.stage = Stage::sustain;
                break;
            }

            filter.envelope -= distance / (secondsOrMinimum(controls.filterDecaySeconds) * sampleRateF);
            if (filter.envelope <= sustain)
            {
                filter.envelope = sustain;
                filter.stage = Stage::sustain;
            }
            break;
        }

        case Stage::sustain:
            filter.envelope = sustain;
            break;

        case Stage::hold:
            break;

        case Stage::release:
        {
            filter.envelope -= filter.releaseOrigin / (secondsOrMinimum(controls.filterReleaseSeconds) * sampleRateF);
            if (filter.envelope <= 0.0f)
                filter.envelope = 0.0f;
            break;
        }

        case Stage::idle:
            break;
    }

    if (! std::isfinite(filter.envelope))
        filter.envelope = 0.0f;
    filter.envelope = juce::jlimit(0.0f, 1.0f, filter.envelope);
}

void MinimalVoiceCore::beginFilterRelease(FilterMemory& filter) noexcept
{
    if (filter.stage == Stage::release)
        return;
    filter.releaseOrigin = filter.envelope;
    filter.stage = Stage::release;
}

void MinimalVoiceCore::resetRetriggers(std::array<LfoRuntime, lfoCount>& lfo, int partIndex, int channel, int noteNumber, std::uint64_t age, const std::array<bool, lfoCount>& lfoRetrig) noexcept
{
    for (int index = 0; index < lfoCount; ++index)
    {
        if (! lfoRetrig[static_cast<std::size_t>(index)])
            continue;
        auto& runtime = lfo[static_cast<std::size_t>(index)];
        runtime = {};
        runtime.rng = noiseSeed(partIndex, channel + 17 + index, noteNumber, age);
        runtime.held = randomHold(runtime.rng);
    }
}

void MinimalVoiceCore::advanceSharedLfos(PartPool& pool, const PartControls& controls, float tempoBpm) noexcept
{
    for (int index = 0; index < lfoCount; ++index)
    {
        const auto& spec = controls.lfo[static_cast<std::size_t>(index)];
        const int partIndex = static_cast<int>(&pool - pools.data());
        stepLfo(pool.lfo[static_cast<std::size_t>(index)], spec.shape, lfoHz(spec, tempoBpm, false, partIndex, 0, 0, 0, index), sampleRate);
    }
}

float MinimalVoiceCore::filtered(int partIndex, int slot, bool tail, FilterMemory& filter, float mixer, int noteNumber, float lfoCutoff, const PartControls& controls, const ModOffsets& offsets, float smooth) noexcept
{
    const float env = juce::jlimit(0.0f, 1.0f, filter.envelope);
    const float key = std::isfinite(controls.keytrack) ? juce::jlimit(-1.0f, 1.0f, controls.keytrack) : 0.0f;
    const float amount = std::isfinite(controls.filterAmountSemitones) ? controls.filterAmountSemitones : 0.0f;
    const float lfoAmount = std::isfinite(controls.lfo1ToCutoffSemitones) ? controls.lfo1ToCutoffSemitones : 0.0f;
    float semis = key * static_cast<float>(noteNumber - 60) + env * amount + lfoCutoff * lfoAmount;
    if (std::abs(offsets.cutoffSemitones) > 0.0f)
        semis += offsets.cutoffSemitones;
    const float base = std::isfinite(controls.cutoffHz) ? controls.cutoffHz : 2000.0f;
    const float ceiling = std::min(20000.0f, static_cast<float>(0.45 * sampleRate));
    float cutoff = base * std::exp2(semis / 12.0f);
    if (! std::isfinite(cutoff))
        cutoff = 20.0f;
    cutoff = juce::jlimit(20.0f, ceiling, cutoff);
    const float targetLog = std::log2(cutoff);
    if (! filter.cutoffReady)
    {
        filter.cutoffLog = targetLog;
        filter.cutoffReady = true;
    }
    else
    {
        filter.cutoffLog += (targetLog - filter.cutoffLog) * smooth;
    }

    float fc = std::exp2(filter.cutoffLog);
    if (! std::isfinite(fc))
        fc = 20.0f;
    fc = juce::jlimit(20.0f, ceiling, fc);

    float drive = controls.drive;
    if (std::abs(offsets.drive) > 0.0f)
        drive += offsets.drive;
    drive = std::isfinite(drive) ? juce::jlimit(0.0f, 1.0f, drive) : 0.0f;
    float driven = mixer;
    if (drive > 1.0e-6f)
        driven = std::tanh(mixer * (1.0f + 4.0f * drive));

    const int type = juce::jlimit(0, filterTypeCount - 1, controls.filterType);
    float resonance = std::isfinite(controls.resonance) ? controls.resonance : 0.0f;
    if (std::abs(offsets.resonance) > 0.0f)
        resonance += offsets.resonance;
    resonance = std::isfinite(resonance) ? juce::jlimit(0.0f, 1.0f, resonance) : 0.0f;
    constexpr float q0 = 0.70710678118f;
    constexpr float q1 = 18.0f;
    const float q = q0 * std::exp(resonance * std::log(q1 / q0));
    const float k = 1.0f / q;
    const float g = std::tan(juce::MathConstants<float>::pi * fc / static_cast<float>(sampleRate));

    auto tick = [&](int stageIndex, float input, float stageG, float stageK, int mode) noexcept
    {
        auto& stage = filter.svf[static_cast<std::size_t>(stageIndex)];
        return svfTick(stage.ic1, stage.ic2, input, stageG, stageK, mode);
    };

    if (type <= 8)
    {
        const int family = type / 3;
        const int stages = type % 3 + 1;
        const int mode = family;
        float y = driven;
        for (int stage = 0; stage < stages; ++stage)
            y = tick(stage, y, g, k, mode);
        return std::isfinite(y) ? y : 0.0f;
    }

    if (type == 9)
    {
        const float y = tick(0, driven, g, k, 3);
        return std::isfinite(y) ? y : 0.0f;
    }

    if (type == 10)
    {
        const float band = tick(0, driven, g, k, 1);
        const float y = driven + band * (resonance * 4.0f);
        return std::isfinite(y) ? y : 0.0f;
    }

    if (type == 11 || type == 12)
    {
        if (combBuffer.empty())
            return driven;
        float* buffer = combBuffer.data() + combOffset(partIndex, slot, tail);
        float delay = static_cast<float>(sampleRate) / fc;
        delay = juce::jlimit(1.0f, static_cast<float>(combCapacity - 2), delay);
        const int whole = static_cast<int>(delay);
        const float fraction = delay - static_cast<float>(whole);
        const auto wrap = [](int index) noexcept
        {
            index %= combCapacity;
            return index < 0 ? index + combCapacity : index;
        };
        const int cursor = juce::jlimit(0, combCapacity - 1, filter.combCursor);
        const float newer = buffer[wrap(cursor - whole)];
        const float older = buffer[wrap(cursor - whole - 1)];
        const float delayed = newer + (older - newer) * fraction;
        const float feedback = resonance * 0.9f;
        const float sign = type == 12 ? -1.0f : 1.0f;
        float y = driven + sign * feedback * delayed;
        if (! std::isfinite(y))
            y = 0.0f;
        y = juce::jlimit(-16.0f, 16.0f, y);
        y = flushDenormal(y);
        buffer[cursor] = y;
        filter.combCursor = cursor + 1;
        if (filter.combCursor >= combCapacity)
            filter.combCursor = 0;
        return y;
    }

    constexpr float ratios[3] = { 1.0f, 2.7f, 4.5f };
    const float formantQ = 2.0f * std::exp(resonance * std::log(6.0f));
    const float formantK = 1.0f / formantQ;
    float sum = 0.0f;
    for (int stage = 0; stage < 3; ++stage)
    {
        const float bandHz = juce::jlimit(20.0f, ceiling, fc * ratios[stage]);
        const float bandG = std::tan(juce::MathConstants<float>::pi * bandHz / static_cast<float>(sampleRate));
        sum += tick(stage, driven, bandG, formantK, 1);
    }
    const float y = sum / 3.0f;
    return std::isfinite(y) ? y : 0.0f;
}

void MinimalVoiceCore::clearComb(int partIndex, int slot, bool tail) noexcept
{
    if (combBuffer.empty())
        return;
    float* start = combBuffer.data() + combOffset(partIndex, slot, tail);
    std::fill(start, start + combCapacity, 0.0f);
}

int MinimalVoiceCore::combOffset(int partIndex, int slot, bool tail) const noexcept
{
    const int perPart = voicesPerPart + tailsPerPart;
    const int index = partIndex * perPart + (tail ? voicesPerPart : 0) + slot;
    return index * combCapacity;
}

int MinimalVoiceCore::voiceIndex(const PartPool& part, const Voice& voice) const noexcept
{
    return static_cast<int>(&voice - part.voices.data());
}

float MinimalVoiceCore::lfoBipolar(int shape, double phase, float held) noexcept
{
    const double wrapped = phase - std::floor(phase);
    float value = 0.0f;
    switch (shape)
    {
        case 1:
            value = wrapped < 0.5 ? static_cast<float>(4.0 * wrapped - 1.0)
                                  : static_cast<float>(3.0 - 4.0 * wrapped);
            break;
        case 2:
            value = static_cast<float>(2.0 * wrapped - 1.0);
            break;
        case 3:
            value = static_cast<float>(1.0 - 2.0 * wrapped);
            break;
        case 4:
            value = wrapped < 0.5 ? 1.0f : -1.0f;
            break;
        case 5:
            value = held;
            break;
        default:
            value = static_cast<float>(std::sin(wrapped * juce::MathConstants<double>::twoPi));
            break;
    }
    return std::isfinite(value) ? juce::jlimit(-1.0f, 1.0f, value) : 0.0f;
}

float MinimalVoiceCore::lfoPolarity(float bipolarValue, bool unipolar) noexcept
{
    const float limited = std::isfinite(bipolarValue) ? juce::jlimit(-1.0f, 1.0f, bipolarValue) : 0.0f;
    if (! unipolar)
        return limited;
    return juce::jlimit(0.0f, 1.0f, 0.5f * (limited + 1.0f));
}

float MinimalVoiceCore::randomHold(std::uint32_t& state) noexcept
{
    return nextNoise(state);
}

double MinimalVoiceCore::syncFrequencyHz(int divisionIndex, double bpm) noexcept
{
    static constexpr int numerators[] = { 16, 8, 4, 2, 1, 1, 1, 1, 1, 1, 1 };
    static constexpr int denominators[] = { 1, 1, 1, 1, 1, 2, 4, 8, 16, 32, 64 };
    const int index = juce::jlimit(0, syncDivisionCount - 1, divisionIndex);
    const double safeBpm = std::isfinite(bpm) && bpm > 0.0 ? bpm : 120.0;
    return safeBpm * static_cast<double>(denominators[index]) / (240.0 * static_cast<double>(numerators[index]));
}

void MinimalVoiceCore::stepLfo(LfoRuntime& lfo, int shape, double hz, double sampleRate) noexcept
{
    if (! (sampleRate > 0.0) || ! std::isfinite(hz) || hz < 0.0)
        return;
    lfo.phase += hz / sampleRate;
    if (lfo.phase < 1.0)
        return;
    const double wraps = std::floor(lfo.phase);
    lfo.phase -= wraps;
    if (shape != 5)
        return;
    int steps = static_cast<int>(wraps);
    if (steps < 1)
        steps = 1;
    if (steps > 8)
        steps = 8;
    for (int step = 0; step < steps; ++step)
        lfo.held = randomHold(lfo.rng);
}

double MinimalVoiceCore::lfoHz(const LfoControls& lfo, float tempoBpm, bool perVoice, int partIndex, int channel, int noteNumber, std::uint64_t age, int lfoIndex) const noexcept
{
    if (lfo.sync)
        return syncFrequencyHz(lfo.rateSync, tempoBpm);

    double hz = std::isfinite(lfo.rateHz) ? juce::jlimit(0.01, 40.0, static_cast<double>(lfo.rateHz)) : 1.0;
    if (! perVoice || ! lfo.retrig || ! (lfo.freeVariation > 0.0f))
        return hz;

    std::uint32_t hash = noiseSeed(partIndex, channel, noteNumber, age);
    hash ^= static_cast<std::uint32_t>(lfoIndex + 1) * 0x27D4EB2Du;
    const float unit = static_cast<float>(hash & 0x00FFFFFFu) / 16777216.0f;
    const float scale = 1.0f + juce::jlimit(0.0f, 1.0f, lfo.freeVariation) * (2.0f * unit - 1.0f);
    return juce::jlimit(0.01, 40.0, hz * static_cast<double>(scale));
}

} // namespace xenmilenium
