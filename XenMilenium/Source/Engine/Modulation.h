#pragma once

#include <JuceHeader.h>

#include <array>

namespace xenmilenium
{

// Индексы совпадают с choice реестра. Новый источник или назначение
// добавляется в оба списка одновременно, иначе слот попадёт не туда.
inline constexpr int msegCount = 4;
inline constexpr int matrixSlotsPerPart = 32;
inline constexpr int modulationPartCount = 8;
inline constexpr int msegSegmentCapacity = 8;
inline constexpr int modulationSlotCount = modulationPartCount * matrixSlotsPerPart;
inline constexpr int msegModuleCount = modulationPartCount * msegCount;

inline constexpr int msegModeNoteOn = 0;
inline constexpr int msegModeNoteOff = 1;
inline constexpr int msegModeLoop = 2;

inline constexpr int msegActivityHold = 0;
inline constexpr int msegActivityPlay = 1;
inline constexpr int msegActivityWait = 2;

enum class ModSource : int
{
    off = 0,
    ampEnv,
    filterEnv,
    lfo1,
    lfo2,
    lfo3,
    lfo4,
    mseg1,
    mseg2,
    mseg3,
    mseg4,
    noteVelocity,
    noteKey,
    modWheel,
    count
};

enum class ModDest : int
{
    off = 0,
    pitch,
    filterCutoff,
    filterResonance,
    filterDrive,
    filterMix,
    mixerOsc1,
    mixerOsc2,
    mixerRing,
    mixerNoise,
    osc3Direct,
    osc1Wave,
    osc2Wave,
    osc3Wave,
    count
};

struct MsegSegment
{
    float duration = 0.0f;
    float level = 0.0f;
};

struct MsegShape
{
    int count = 0;
    std::array<MsegSegment, msegSegmentCapacity> segments {};
};

struct MsegBank
{
    std::array<MsegShape, msegModuleCount> shapes {};
};

struct MsegCursor
{
    float value = 0.0f;
    bool finished = true;
};

struct MatrixSlotControl
{
    int source = 0;
    int destination = 0;
    float amount = 0.0f;
    bool enabled = false;
};

struct ModSourceValues
{
    float ampEnv = 0.0f;
    float filterEnv = 0.0f;
    std::array<float, 4> lfo {};
    std::array<float, msegCount> mseg {};
    float velocity = 0.0f;
    float noteKey = 0.0f;
    float modWheel = 0.0f;
};

struct ModOffsets
{
    float cutoffSemitones = 0.0f;
    float pitchSemitones = 0.0f;
    float resonance = 0.0f;
    float drive = 0.0f;
    float mix = 0.0f;
    float osc1 = 0.0f;
    float osc2 = 0.0f;
    float ring = 0.0f;
    float noise = 0.0f;
    float directOut = 0.0f;
    float wave1 = 0.0f;
    float wave2 = 0.0f;
    float wave3 = 0.0f;
};

MsegCursor evaluateMseg(const MsegShape& shape, double time) noexcept;
double msegDuration(const MsegShape& shape) noexcept;
float noteKeyValue(int noteNumber) noexcept;
float modulationSourceValue(const ModSourceValues& sources, int source) noexcept;
ModOffsets sumMatrix(const MatrixSlotControl* slots, int count, const ModSourceValues& sources) noexcept;
MsegBank parseMsegBank(const juce::var& unknown);

} // namespace xenmilenium
