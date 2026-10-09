#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Effects.h"
#include <array>
#include <memory>
#include <vector>

static constexpr int kNumLayers = 4;
static constexpr int kNumVoices = 32; // room for layered sounds
static constexpr int kMaxArpNotes = 16;
static constexpr double kGrainSeconds = 0.06; // time-stretch grain length

// Arp choices, shared with the UI so the combo boxes match
inline const juce::StringArray kArpRates{ "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
inline const juce::StringArray kArpPatterns{ "Up", "Down", "Up/Down", "Random", "As Played" };
inline const juce::StringArray kDistTypes{ "Soft", "Hard", "Fold", "Crush" };
inline const juce::StringArray kDelayTimes{ "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2" };

inline juce::String layerParamID(int layer, const juce::String& name)
{
    return "l" + juce::String(layer) + "_" + name;
}

inline juce::String linkParamID(int from, int to) // "picking `from` also plays `to`"
{
    return layerParamID(from, "link" + juce::String(to));
}

// A loaded one-shot. Immutable once created, shared between UI and audio thread.
struct Sample
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    juce::String path;
};
using SamplePtr = std::shared_ptr<const Sample>;

class AudioPluginAudioProcessor : public juce::AudioProcessor,
    private juce::Timer
{
public:
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; } // delay/reverb tails

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Message thread only
    bool loadSample(int layer, const juce::File& file);
    void clearSample(int layer);
    SamplePtr getSample(int layer) const { return std::atomic_load(&samples[(size_t)layer]); }

    // Presets (message thread). Files are XML with the ".preset" extension.
    static inline const juce::String presetExtension{ ".preset" };
    static juce::File getPresetFolder();        // the user's choice, else Documents/<plugin name>/Presets; created if missing
    static juce::File getDefaultPresetFolder();
    static void setPresetFolder(const juce::File& folder); // remembered across sessions; File() = back to default
    static bool usingCustomPresetFolder();
    bool savePreset(const juce::File& file);
    bool loadPreset(const juce::File& file);
    void initPreset();                     // all defaults, no samples
    juce::String currentPresetName;        // empty = unsaved / init
    juce::File currentPresetFile;          // the loaded/saved preset file, if known (message thread)

    // Kits: subfolders of the presets folder. An exported kit is a zip of "<kit>/<presets>" plus
    // "<kit>/Samples/<every sound they use>", with the presets re-pointed at those copies.
    // Copy settings from one sound to another (drag-to-copy). Names are parameter names without
    // the slot prefix ("attack", "dist_drive", ...); "@layers" copies outgoing layer links.
    void copySettings(int fromSlot, int toSlot, const juce::StringArray& names);
    juce::StringArray settingNamesWithPrefix(int slot, const juce::String& prefix) const; // e.g. every "dist_" setting

    static juce::Array<juce::File> getKitFolders();
    static juce::File createKit(const juce::String& name);                 // returns the new folder
    struct KitExport { int presets = 0, sounds = 0; juce::StringArray missing; juce::String error; };
    static KitExport exportKit(const juce::File& kitFolder, const juce::File& zipFile);
    static juce::Result importKit(const juce::File& zipFile, juce::String& kitNameOut);

    // Bumped on the audio thread each time a slot is picked; the UI uses it to flash that slot.
    std::array<std::atomic<int>, kNumLayers> triggerCount{};

    // Bumped each time a layer link fires ([from * kNumLayers + to]); the UI flashes that arrow.
    std::array<std::atomic<int>, kNumLayers* kNumLayers> linkFireCount{};

    // Per-voice playhead for the UI: which slot, where in the sample (0..1, playback direction), how loud (0 = not sounding)
    std::array<std::atomic<int>, kNumVoices>   playheadSlot{};
    std::array<std::atomic<float>, kNumVoices> playheadPos{}, playheadLevel{};

    juce::AudioFormatManager formatManager;
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void setSample(int layer, SamplePtr newSample);
    juce::ValueTree createStateTree(const juce::File& baseDir);
    bool applyStateTree(const juce::ValueTree& tree, const juce::File& baseDir);

    struct SlotParams
    {
        float gain = 1.0f, pitch = 0.0f, start = 0.0f, startRand = 0.0f;
        double stretch = 1.0;     // length multiplier; 1 = off (classic sampler: pitch changes length)
        bool loaded = false;      // has a sound
        bool playable = false;    // can be randomly picked (loaded and not muted)
        bool reverse = false, glide = false, legato = false;
        bool loop = false;
        float loopStart = 0.0f, loopEnd = 1.0f;     // fractions of the sound, in playback direction
        std::array<bool, kNumLayers> links{};      // links[j]: picking this sound also plays sound j
        float glideMs = 0.0f;
        juce::ADSR::Parameters env;

        bool arp = false;
        double arpBeats = 0.25;   // step length in quarter notes
        int arpPattern = 0, arpOctaves = 1, arpVelocity = 0; // velocity 0 = as played
        float arpGate = 0.5f, arpSwing = 0.0f;
    };

    // One arpeggiator per slot. Keys whose random pick lands on an arp slot feed its note list.
    struct ArpLane
    {
        int numHeld = 0;
        std::array<int, kMaxArpNotes> held{};        // in the order pressed
        std::array<float, kMaxArpNotes> heldVel{};
        bool running = false;
        double toNextStep = 0.0, gateLeft = -1.0;     // samples; gateLeft < 0 = no note to release
        int step = 0, voiceIndex = -1, lastNote = -1;
        juce::uint32 voiceId = 0;
    };

    struct Voice
    {
        bool active = false, released = false, reversed = false, fromArp = false;
        juce::uint32 id = 0;      // lets the arp check a voice wasn't stolen before releasing it
        juce::uint32 group = 0;   // a picked sound and its layers share a group (released/bent together)
        bool looped = false;      // has wrapped round the loop at least once
        bool beforeLoopEnd = false; // was before the loop end: the loop only engages when playback reaches it
        int note = 60, slot = 0;
        float velocity = 1.0f;
        juce::int64 age = 0;      // samples since note-on
        double pos = 0.0, startPos = 0.0;
        SamplePtr sample;         // snapshot: safe even if the UI swaps the slot mid-note
        juce::ADSR env;
        float envLevel = 0.0f;
        // Time-stretch: two overlapping windowed grains read at the note's pitch while
        // `pos` (the position in the sound) moves at the stretched speed
        struct Grain { double pos = 0.0; int age = 0; };
        std::array<Grain, 2> grains;
        int nextGrain = 0;
        double grainCountdown = 0.0;

        float glideFrom = 0.0f;   // semitones offset at the start of the glide (0 = no glide)
        double glideSamples = 0.0, glideElapsed = 0.0;
    };

    void handleNoteOn(int note, float velocity, const SlotParams* sp);
    void handleNoteOff(int note);
    void removeKeyFromArps(int note);
    int startVoiceOnSlot(int slot, int note, float velocity, const SlotParams* sp, int glideFromNote, bool fromArp,
        juce::uint32 group);
    void releaseVoices(int note); // note < 0 = release all

    void arpStep(int slot, const SlotParams* sp);
    void releaseArpVoice(ArpLane&);
    void stopArp(int slot);
    double samplesToNextArpEvent() const;
    void advanceArps(int numSamples);
    void processDueArpEvents(const SlotParams* sp);
    void renderVoices(int start, int num, const SlotParams* sp);

    std::array<SamplePtr, kNumLayers> samples;   // swapped atomically
    std::vector<SamplePtr> retired;              // keeps old samples alive until audio thread lets go
    juce::CriticalSection retiredLock;

    std::array<Voice, kNumVoices> voices;
    double hostSampleRate = 44100.0;

    juce::Random random;
    int lastSlot = -1;
    int lastNote = -1; // glides start from here
    juce::uint32 nextVoiceId = 1;
    double bpm = 120.0;

    std::array<ArpLane, kNumLayers> arps;

    // Per-slot audio bus + effect chain: Distortion -> Granular -> Delay -> Reverb
    struct SlotFx
    {
        DistortionFx dist;
        GranularFx granular;
        DelayFx delay;
        juce::Reverb reverb;
        std::array<bool, 4> wasOn{}; // reset an effect's memory when it's switched on

        // Per-sound high-pass / low-pass, before the effects. Frequencies glide so sweeps don't zipper.
        juce::dsp::StateVariableTPTFilter<float> hp, lp;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> hpFreq{ 20.0f }, lpFreq{ 20000.0f };
        juce::SmoothedValue<float> hpMix, lpMix; // 5 ms crossfade when a filter switches on/off: no clicks
        bool hpActive = false, lpActive = false;
    };
    struct FxParamPtrs
    {
        std::atomic<float>* distOn, * distType, * distDrive, * distTone, * distMix,
            * grOn, * grSize, * grDensity, * grSpray, * grPitch, * grMix,
            * dlyOn, * dlyTime, * dlyFb, * dlyMix, * dlyPing,
            * revOn, * revSize, * revDamp, * revWidth, * revMix,
            * hpf, * lpf;
    };
    void processSlotFx(int slot, int numSamples);

    std::array<juce::AudioBuffer<float>, kNumLayers> buses;
    std::array<SlotFx, kNumLayers> fx;
    std::array<FxParamPtrs, kNumLayers> fxp{};
    std::array<juce::uint32, 128> keyArps; // per held key: bit s set = it feeds sound s's arpeggiator

    std::array<std::atomic<float>*, kNumLayers> volParam{}, pitchParam{}, stretchParam{}, startParam{}, randParam{},
        muteParam{}, reverseParam{}, glideParam{}, glideTimeParam{}, legatoParam{},
        loopParam{}, loopStartParam{}, loopEndParam{},
        attackParam{}, decayParam{}, sustainParam{}, releaseParam{},
        arpParam{}, arpRateParam{}, arpPatternParam{}, arpOctParam{},
        arpGateParam{}, arpSwingParam{}, arpVelParam{};
    std::atomic<float>* masterParam = nullptr;
    std::array<std::array<std::atomic<float>*, kNumLayers>, kNumLayers> linkParam{}; // [from][to], diagonal unused

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};