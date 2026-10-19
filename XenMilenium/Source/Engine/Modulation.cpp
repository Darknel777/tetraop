#include "Engine/Modulation.h"

#include <cmath>

namespace xenmilenium
{
namespace
{
constexpr double instantDuration = 1.0e-8;

bool numeric(const juce::var& value) noexcept
{
    return ! value.isBool() && (value.isDouble() || value.isInt() || value.isInt64());
}

float finiteNumber(const juce::var& value) noexcept
{
    const double number = static_cast<double>(value);
    return std::isfinite(number) ? static_cast<float>(number) : 0.0f;
}

float clampedUnit(double value, float limit) noexcept
{
    if (! std::isfinite(value))
        return 0.0f;
    return juce::jlimit(-limit, limit, static_cast<float>(value));
}

bool parseMsegPath(const juce::String& key, int& partNumber, int& msegNumber)
{
    if (! key.startsWith("part/") || ! key.endsWith("/points"))
        return false;

    const auto rest = key.substring(5);
    const int partSlash = rest.indexOfChar('/');
    if (partSlash <= 0)
        return false;

    const int part = rest.substring(0, partSlash).getIntValue();
    const auto afterPart = rest.substring(partSlash + 1);
    if (! afterPart.startsWith("mseg/"))
        return false;

    const auto indexAndTail = afterPart.substring(5);
    const int indexSlash = indexAndTail.indexOfChar('/');
    if (indexSlash <= 0 || indexAndTail.substring(indexSlash) != "/points")
        return false;

    const int mseg = indexAndTail.substring(0, indexSlash).getIntValue();
    if (part < 1 || part > modulationPartCount || mseg < 1 || mseg > msegCount)
        return false;

    partNumber = part;
    msegNumber = mseg;
    return true;
}

void readShape(MsegShape& shape, const juce::var& points)
{
    shape = {};
    if (! points.isArray())
        return;

    for (const auto& point : *points.getArray())
    {
        if (shape.count >= msegSegmentCapacity)
            break;
        const auto* object = point.getDynamicObject();
        if (object == nullptr)
            continue;

        const auto duration = object->getProperty("duration");
        const auto level = object->getProperty("level");
        if (! numeric(duration) || ! numeric(level))
            continue;

        const float seconds = finiteNumber(duration);
        const float height = finiteNumber(level);
        if (! std::isfinite(seconds) || ! std::isfinite(height))
            continue;

        auto& segment = shape.segments[static_cast<std::size_t>(shape.count++)];
        segment.duration = juce::jlimit(0.0f, 10.0f, seconds);
        segment.level = juce::jlimit(-1.0f, 1.0f, height);
    }
}
} // namespace

MsegCursor evaluateMseg(const MsegShape& shape, double time) noexcept
{
    MsegCursor cursor;
    if (shape.count <= 0 || ! std::isfinite(time) || time < 0.0)
        return cursor;

    float previous = 0.0f;
    double at = 0.0;
    const int count = juce::jlimit(0, msegSegmentCapacity, shape.count);
    for (int index = 0; index < count; ++index)
    {
        const auto& segment = shape.segments[static_cast<std::size_t>(index)];
        const float level = std::isfinite(segment.level) ? juce::jlimit(-1.0f, 1.0f, segment.level) : previous;
        const float duration = std::isfinite(segment.duration) ? segment.duration : 0.0f;
        if (! (static_cast<double>(duration) > instantDuration))
        {
            previous = level;
            continue;
        }

        if (time < at + static_cast<double>(duration))
        {
            const double fraction = (time - at) / static_cast<double>(duration);
            cursor.value = previous + (level - previous) * static_cast<float>(fraction);
            cursor.finished = false;
            if (! std::isfinite(cursor.value))
                cursor.value = 0.0f;
            return cursor;
        }

        at += static_cast<double>(duration);
        previous = level;
    }

    cursor.value = previous;
    cursor.finished = true;
    return cursor;
}

double msegDuration(const MsegShape& shape) noexcept
{
    double sum = 0.0;
    const int count = juce::jlimit(0, msegSegmentCapacity, shape.count);
    for (int index = 0; index < count; ++index)
    {
        const float duration = shape.segments[static_cast<std::size_t>(index)].duration;
        if (std::isfinite(duration) && static_cast<double>(duration) > instantDuration)
            sum += static_cast<double>(duration);
    }
    return sum;
}

float noteKeyValue(int noteNumber) noexcept
{
    if (! juce::isPositiveAndBelow(noteNumber, 128))
        return 0.0f;
    return juce::jlimit(-1.0f, 1.0f, static_cast<float>(noteNumber - 60) / 60.0f);
}

float modulationSourceValue(const ModSourceValues& sources, int source) noexcept
{
    switch (source)
    {
        case static_cast<int>(ModSource::ampEnv):
            return sources.ampEnv;
        case static_cast<int>(ModSource::filterEnv):
            return sources.filterEnv;
        case static_cast<int>(ModSource::lfo1):
            return sources.lfo[0];
        case static_cast<int>(ModSource::lfo2):
            return sources.lfo[1];
        case static_cast<int>(ModSource::lfo3):
            return sources.lfo[2];
        case static_cast<int>(ModSource::lfo4):
            return sources.lfo[3];
        case static_cast<int>(ModSource::mseg1):
            return sources.mseg[0];
        case static_cast<int>(ModSource::mseg2):
            return sources.mseg[1];
        case static_cast<int>(ModSource::mseg3):
            return sources.mseg[2];
        case static_cast<int>(ModSource::mseg4):
            return sources.mseg[3];
        case static_cast<int>(ModSource::noteVelocity):
            return sources.velocity;
        case static_cast<int>(ModSource::noteKey):
            return sources.noteKey;
        case static_cast<int>(ModSource::modWheel):
            return sources.modWheel;
        default:
            return 0.0f;
    }
}

ModOffsets sumMatrix(const MatrixSlotControl* slots, int count, const ModSourceValues& sources) noexcept
{
    ModOffsets offsets;
    if (slots == nullptr || count <= 0)
        return offsets;

    double cutoff = 0.0;
    double pitch = 0.0;
    double resonance = 0.0;
    double drive = 0.0;
    double mix = 0.0;
    double osc1 = 0.0;
    double osc2 = 0.0;
    double ring = 0.0;
    double noise = 0.0;
    double direct = 0.0;
    double wave1 = 0.0;
    double wave2 = 0.0;
    double wave3 = 0.0;

    for (int index = 0; index < count; ++index)
    {
        const auto& slot = slots[index];
        if (! slot.enabled || ! (std::abs(slot.amount) > 0.0f) || ! std::isfinite(slot.amount))
            continue;
        if (slot.source <= static_cast<int>(ModSource::off) || slot.source >= static_cast<int>(ModSource::count))
            continue;
        if (slot.destination <= static_cast<int>(ModDest::off) || slot.destination >= static_cast<int>(ModDest::count))
            continue;

        const float source = modulationSourceValue(sources, slot.source);
        if (! std::isfinite(source))
            continue;

        const double contribution = static_cast<double>(source) * static_cast<double>(slot.amount);
        switch (slot.destination)
        {
            case static_cast<int>(ModDest::pitch):
                pitch += contribution;
                break;
            case static_cast<int>(ModDest::filterCutoff):
                cutoff += contribution;
                break;
            case static_cast<int>(ModDest::filterResonance):
                resonance += contribution;
                break;
            case static_cast<int>(ModDest::filterDrive):
                drive += contribution;
                break;
            case static_cast<int>(ModDest::filterMix):
                mix += contribution;
                break;
            case static_cast<int>(ModDest::mixerOsc1):
                osc1 += contribution;
                break;
            case static_cast<int>(ModDest::mixerOsc2):
                osc2 += contribution;
                break;
            case static_cast<int>(ModDest::mixerRing):
                ring += contribution;
                break;
            case static_cast<int>(ModDest::mixerNoise):
                noise += contribution;
                break;
            case static_cast<int>(ModDest::osc3Direct):
                direct += contribution;
                break;
            case static_cast<int>(ModDest::osc1Wave):
                wave1 += contribution;
                break;
            case static_cast<int>(ModDest::osc2Wave):
                wave2 += contribution;
                break;
            case static_cast<int>(ModDest::osc3Wave):
                wave3 += contribution;
                break;
            default:
                break;
        }
    }

    offsets.cutoffSemitones = clampedUnit(cutoff * 48.0, 96.0f);
    offsets.pitchSemitones = clampedUnit(pitch * 12.0, 48.0f);
    offsets.resonance = clampedUnit(resonance, 1.0f);
    offsets.drive = clampedUnit(drive, 1.0f);
    offsets.mix = clampedUnit(mix, 1.0f);
    offsets.osc1 = clampedUnit(osc1, 1.0f);
    offsets.osc2 = clampedUnit(osc2, 1.0f);
    offsets.ring = clampedUnit(ring, 1.0f);
    offsets.noise = clampedUnit(noise, 1.0f);
    offsets.directOut = clampedUnit(direct, 1.0f);
    offsets.wave1 = clampedUnit(wave1, 3.0f);
    offsets.wave2 = clampedUnit(wave2, 3.0f);
    offsets.wave3 = clampedUnit(wave3, 3.0f);
    return offsets;
}

MsegBank parseMsegBank(const juce::var& unknown)
{
    MsegBank bank;
    const auto* root = unknown.getDynamicObject();
    if (root == nullptr)
        return bank;

    const auto* parameters = root->getProperty("parameters").getDynamicObject();
    if (parameters == nullptr)
        return bank;

    for (int index = 0; index < parameters->getProperties().size(); ++index)
    {
        int partNumber = 0;
        int msegNumber = 0;
        if (! parseMsegPath(parameters->getProperties().getName(index).toString(), partNumber, msegNumber))
            continue;

        const int bankIndex = (partNumber - 1) * msegCount + (msegNumber - 1);
        readShape(bank.shapes[static_cast<std::size_t>(bankIndex)], parameters->getProperties().getValueAt(index));
    }

    return bank;
}

} // namespace xenmilenium
