#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>
#include <set>

AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();

    for (int l = 0; l < kNumLayers; ++l)
    {
        volParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "vol"));
        pitchParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "pitch"));
        stretchParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "stretch"));
        startParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "start"));
        randParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "startrand"));
        muteParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "mute"));
        reverseParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "reverse"));
        attackParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "attack"));
        decayParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "decay"));
        sustainParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "sustain"));
        releaseParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "release"));
        glideParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "glide"));
        glideTimeParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "glidetime"));
        legatoParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "legato"));
        loopParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "loop"));
        loopStartParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "loopstart"));
        loopEndParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "loopend"));
        for (int j = 0; j < kNumLayers; ++j)
            if (j != l)
                linkParam[(size_t)l][(size_t)j] = apvts.getRawParameterValue(linkParamID(l, j));
        arpParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "arp"));
        arpRateParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "arprate"));
        arpPatternParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "arppattern"));
        arpOctParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "arpoct"));
        arpGateParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "arpgate"));
        arpSwingParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "arpswing"));
        arpVelParam[(size_t)l] = apvts.getRawParameterValue(layerParamID(l, "arpvel"));

        auto get = [&](const char* id) { return apvts.getRawParameterValue(layerParamID(l, id)); };
        fxp[(size_t)l] = { get("dist_on"), get("dist_type"), get("dist_drive"), get("dist_tone"), get("dist_mix"),
                            get("gr_on"), get("gr_size"), get("gr_density"), get("gr_spray"), get("gr_pitch"), get("gr_mix"),
                            get("dly_on"), get("dly_time"), get("dly_fb"), get("dly_mix"), get("dly_ping"),
                            get("rev_on"), get("rev_size"), get("rev_damp"), get("rev_width"), get("rev_mix"),
                            get("hpf"), get("lpf") };
    }
    masterParam = apvts.getRawParameterValue("master");
    keyArps.fill(0);

    startTimer(1000); // cleans up retired samples
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    // Stop the cleanup timer FIRST: otherwise it can fire while the members it uses (the retired
    // list and its lock) are already being destroyed - a hang if the host deletes us off the UI thread.
    stopTimer();
    const juce::ScopedLock sl(retiredLock); // wait out a callback that's mid-flight on another thread
}

juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using Range = juce::NormalisableRange<float>;

    auto addFloat = [&](const juce::String& id, const juce::String& name, Range range, float def)
        {
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ id, 1 }, name, range, def));
        };

    for (int l = 0; l < kNumLayers; ++l)
    {
        const auto name = "Slot " + juce::String(l + 1) + " ";
        addFloat(layerParamID(l, "vol"), name + "Volume", Range(-60.0f, 12.0f, 0.1f), 0.0f);
        addFloat(layerParamID(l, "pitch"), name + "Pitch", Range(-24.0f, 24.0f, 0.01f), 0.0f);
        // Time-stretch, 25 %..400 % of the original length. Log scale with 100 % in the middle of the knob.
        // Filters: 20 Hz - 20 kHz on a log scale; fully left (high-pass) / fully right (low-pass) = off
        auto freqRange = [] { return Range(20.0f, 20000.0f,
            [](float, float, float x) { return 20.0f * std::pow(1000.0f, x); },
            [](float, float, float v) { return std::log(v / 20.0f) / std::log(1000.0f); }); };
        auto freqText = [](bool isHighPass)
            {
                return [isHighPass](float v, int)
                    {
                        if (isHighPass ? v <= 20.5f : v >= 19990.0f) return juce::String("off");
                        return v >= 1000.0f ? juce::String(v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String(juce::roundToInt(v)) + " Hz";
                    };
            };
        auto freqParse = [](const juce::String& t)
            {
                const auto s = t.trim().toLowerCase();
                if (s.startsWith("off")) return -1.0f; // clamped to the "off" end below
                return s.getFloatValue() * (s.containsChar('k') ? 1000.0f : 1.0f);
            };
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ layerParamID(l, "hpf"), 1 }, name + "High-pass", freqRange(), 20.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(freqText(true))
            .withValueFromStringFunction([freqParse](const juce::String& t) { const float v = freqParse(t); return v < 0.0f ? 20.0f : v; })));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ layerParamID(l, "lpf"), 1 }, name + "Low-pass", freqRange(), 20000.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(freqText(false))
            .withValueFromStringFunction([freqParse](const juce::String& t) { const float v = freqParse(t); return v < 0.0f ? 20000.0f : v; })));

        addFloat(layerParamID(l, "stretch"), name + "Stretch",
            Range(25.0f, 400.0f,
                [](float, float, float x) { return 25.0f * std::pow(16.0f, x); },
                [](float, float, float v) { return std::log(v / 25.0f) / std::log(16.0f); },
                [](float, float, float v) { return juce::jlimit(25.0f, 400.0f, std::round(v)); }),
            100.0f);
        addFloat(layerParamID(l, "start"), name + "Start", Range(0.0f, 1.0f, 0.001f), 0.0f);
        addFloat(layerParamID(l, "startrand"), name + "Start Rand", Range(0.0f, 1.0f, 0.001f), 0.0f);
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ layerParamID(l, "mute"), 1 }, name + "Mute", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ layerParamID(l, "reverse"), 1 }, name + "Reverse", false));

        addFloat(layerParamID(l, "attack"), name + "Attack", Range(0.0f, 5000.0f, 0.1f, 0.3f), 1.0f);   // ms
        addFloat(layerParamID(l, "decay"), name + "Decay", Range(1.0f, 5000.0f, 0.1f, 0.3f), 300.0f); // ms
        addFloat(layerParamID(l, "sustain"), name + "Sustain", Range(0.0f, 1.0f, 0.001f), 1.0f);
        addFloat(layerParamID(l, "release"), name + "Release", Range(1.0f, 5000.0f, 0.1f, 0.3f), 50.0f);  // ms

        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ layerParamID(l, "glide"), 1 }, name + "Glide", false));
        addFloat(layerParamID(l, "glidetime"), name + "Glide Time", Range(1.0f, 2000.0f, 0.1f, 0.4f), 100.0f); // ms
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ layerParamID(l, "legato"), 1 }, name + "Legato", false));

        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ layerParamID(l, "loop"), 1 }, name + "Loop", false));
        addFloat(layerParamID(l, "loopstart"), name + "Loop Start", Range(0.0f, 1.0f, 0.001f), 0.0f);
        addFloat(layerParamID(l, "loopend"), name + "Loop End", Range(0.0f, 1.0f, 0.001f), 1.0f);

        for (int j = 0; j < kNumLayers; ++j) // layer links, one way: picking sound l also plays sound j
            if (j != l)
                layout.add(std::make_unique<juce::AudioParameterBool>(
                    juce::ParameterID{ linkParamID(l, j), 1 }, name + "Layers Slot " + juce::String(j + 1), false));

        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ layerParamID(l, "arp"), 1 }, name + "Arp", false));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ layerParamID(l, "arprate"), 1 }, name + "Arp Rate", kArpRates, 3));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ layerParamID(l, "arppattern"), 1 }, name + "Arp Pattern", kArpPatterns, 0));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{ layerParamID(l, "arpoct"), 1 }, name + "Arp Octaves", 1, 4, 1));
        addFloat(layerParamID(l, "arpgate"), name + "Arp Gate", Range(5.0f, 100.0f, 1.0f), 50.0f); // % of a step
        addFloat(layerParamID(l, "arpswing"), name + "Arp Swing", Range(0.0f, 100.0f, 1.0f), 0.0f);
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{ layerParamID(l, "arpvel"), 1 }, name + "Arp Velocity", 0, 127, 0,
            juce::AudioParameterIntAttributes().withStringFromValueFunction(
                [](int v, int) { return v == 0 ? juce::String("Played") : juce::String(v); })));

        // Effects
        auto addBool = [&](const char* id, const juce::String& n, bool def)
            { layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ layerParamID(l, id), 1 }, name + n, def)); };
        auto addChoice = [&](const char* id, const juce::String& n, const juce::StringArray& items, int def)
            { layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ layerParamID(l, id), 1 }, name + n, items, def)); };

        addBool("dist_on", "Dist On", false);
        addChoice("dist_type", "Dist Type", kDistTypes, 0);
        addFloat(layerParamID(l, "dist_drive"), name + "Dist Drive", Range(0.0f, 48.0f, 0.1f), 12.0f);        // dB
        addFloat(layerParamID(l, "dist_tone"), name + "Dist Tone", Range(200.0f, 20000.0f, 1.0f, 0.3f), 20000.0f); // Hz
        addFloat(layerParamID(l, "dist_mix"), name + "Dist Mix", Range(0.0f, 100.0f, 1.0f), 100.0f);

        addBool("gr_on", "Granular On", false);
        addFloat(layerParamID(l, "gr_size"), name + "Grain Size", Range(10.0f, 500.0f, 0.1f, 0.5f), 80.0f);  // ms
        addFloat(layerParamID(l, "gr_density"), name + "Grain Density", Range(1.0f, 100.0f, 0.1f, 0.5f), 20.0f);  // grains/s
        addFloat(layerParamID(l, "gr_spray"), name + "Grain Spray", Range(0.0f, 1000.0f, 0.1f, 0.5f), 200.0f); // ms
        addFloat(layerParamID(l, "gr_pitch"), name + "Grain Pitch", Range(-24.0f, 24.0f, 0.01f), 0.0f);        // st
        addFloat(layerParamID(l, "gr_mix"), name + "Grain Mix", Range(0.0f, 100.0f, 1.0f), 50.0f);

        addBool("dly_on", "Delay On", false);
        addChoice("dly_time", "Delay Time", kDelayTimes, 3);
        addFloat(layerParamID(l, "dly_fb"), name + "Delay Feedback", Range(0.0f, 95.0f, 1.0f), 35.0f);
        addFloat(layerParamID(l, "dly_mix"), name + "Delay Mix", Range(0.0f, 100.0f, 1.0f), 30.0f);
        addBool("dly_ping", "Delay Ping-Pong", false);

        addBool("rev_on", "Reverb On", false);
        addFloat(layerParamID(l, "rev_size"), name + "Reverb Size", Range(0.0f, 100.0f, 1.0f), 50.0f);
        addFloat(layerParamID(l, "rev_damp"), name + "Reverb Damp", Range(0.0f, 100.0f, 1.0f), 50.0f);
        addFloat(layerParamID(l, "rev_width"), name + "Reverb Width", Range(0.0f, 100.0f, 1.0f), 100.0f);
        addFloat(layerParamID(l, "rev_mix"), name + "Reverb Mix", Range(0.0f, 100.0f, 1.0f), 30.0f);
    }

    addFloat("master", "Master", Range(-60.0f, 6.0f, 0.1f), -6.0f);

    return layout;
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    for (int l = 0; l < kNumLayers; ++l)
    {
        buses[(size_t)l].setSize(2, samplesPerBlock);
        auto& f = fx[(size_t)l];
        f.dist.prepare(sampleRate, samplesPerBlock);
        f.granular.prepare(sampleRate, samplesPerBlock);
        f.delay.prepare(sampleRate, samplesPerBlock);
        f.reverb.setSampleRate(sampleRate);
        f.reverb.reset();

        const juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)samplesPerBlock, 2 };
        f.hp.prepare(spec);
        f.hp.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        f.lp.prepare(spec);
        f.lp.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        f.hpFreq.reset(sampleRate, 0.03);
        f.lpFreq.reset(sampleRate, 0.03);
        f.hpFreq.setCurrentAndTargetValue(fxp[(size_t)l].hpf->load());
        f.lpFreq.setCurrentAndTargetValue(fxp[(size_t)l].lpf->load());
        f.hpMix.reset(sampleRate, 0.005);
        f.lpMix.reset(sampleRate, 0.005);
        f.hpMix.setCurrentAndTargetValue(0.0f);
        f.lpMix.setCurrentAndTargetValue(0.0f);
        f.hpActive = f.lpActive = false;
    }
    for (int l = 0; l < kNumLayers; ++l)
        stopArp(l);
    for (auto& v : voices)
    {
        v.active = false;
        v.env.setSampleRate(sampleRate);
        v.env.reset();
    }
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void AudioPluginAudioProcessor::handleNoteOn(int note, float velocity, const SlotParams* sp)
{
    const int prevNote = lastNote;
    lastNote = note;

    // Legato is per sound: each sound with legato on that's still being held bends its newest voice
    // to the new key (no restart). It only affects those sounds - the key still picks a sound below
    // as usual, and sounds without legato behave exactly as if legato didn't exist.
    std::array<bool, kNumLayers> bent{};
    for (int s = 0; s < kNumLayers; ++s)
    {
        if (!sp[s].legato) continue;

        Voice* held = nullptr;
        for (auto& v : voices)
            if (v.active && !v.released && !v.fromArp && v.slot == s && (held == nullptr || v.age < held->age))
                held = &v;
        if (held == nullptr) continue;

        auto& v = *held;
        const double currentOffset = (v.glideFrom != 0.0f && v.glideElapsed < v.glideSamples)
            ? v.glideFrom * (1.0 - v.glideElapsed / v.glideSamples) : 0.0;
        // Glide on: slide from wherever the pitch is right now. Glide off: jump instantly.
        v.glideFrom = sp[s].glide ? (float)(v.note + currentOffset - note) : 0.0f;
        v.glideSamples = sp[s].glideMs * 0.001 * hostSampleRate;
        v.glideElapsed = 0.0;
        v.note = note; // the voice now belongs to the new key; releasing the old key won't stop it
        bent[(size_t)s] = true;
    }

    // Pick a random sound from the pool: loaded and not muted (muted = "only plays as a layer").
    // Avoid an immediate repeat when there's a choice.
    int candidates[kNumLayers];
    int numCandidates = 0;
    for (int l = 0; l < kNumLayers; ++l)
        if (sp[l].playable)
            candidates[numCandidates++] = l;

    if (numCandidates == 0) return;

    int slot = candidates[random.nextInt(numCandidates)];
    if (numCandidates > 1)
        while (slot == lastSlot)
            slot = candidates[random.nextInt(numCandidates)];
    lastSlot = slot;

    removeKeyFromArps(note); // a repeated note-on for a key that's already held

    // The picked sound plus everything it links to (direct links only, no chaining). Each one is
    // simply triggered by this key, then follows only its own settings: its own arp, glide, start...
    juce::uint32 group = 0;
    auto play = [&](int s)
        {
            if (bent[(size_t)s] && !sp[s].arp)
                return; // picked a legato sound that's already playing: it just bent to this key above

            if (sp[s].arp)
            {
                // Key feeds this sound's own arpeggiator for as long as it's held
                auto& lane = arps[(size_t)s];
                if (lane.numHeld < kMaxArpNotes)
                {
                    lane.held[(size_t)lane.numHeld] = note;
                    lane.heldVel[(size_t)lane.numHeld] = velocity;
                    ++lane.numHeld;
                    keyArps[(size_t)note] |= 1u << s;
                }
                if (!lane.running)
                {
                    lane.running = true;
                    lane.step = 0;
                    lane.toNextStep = 0.0; // first step fires right away
                }
            }
            else
            {
                const int vi = startVoiceOnSlot(s, note, velocity, sp, prevNote, false, group);
                if (vi >= 0 && group == 0)
                    group = voices[(size_t)vi].group; // one-shots from this key share a group (for legato)
            }
        };

    play(slot);
    for (int j = 0; j < kNumLayers; ++j)
        if (j != slot && sp[slot].links[(size_t)j] && sp[j].loaded) // layers play even when muted
        {
            play(j);
            linkFireCount[(size_t)(slot * kNumLayers + j)]++;
        }
}

void AudioPluginAudioProcessor::removeKeyFromArps(int note)
{
    const auto mask = keyArps[(size_t)note];
    keyArps[(size_t)note] = 0;

    for (int s = 0; s < kNumLayers; ++s)
    {
        if ((mask & (1u << s)) == 0) continue;

        auto& lane = arps[(size_t)s];
        for (int i = 0; i < lane.numHeld; ++i)
            if (lane.held[(size_t)i] == note)
            {
                for (int j = i; j < lane.numHeld - 1; ++j)
                {
                    lane.held[(size_t)j] = lane.held[(size_t)j + 1];
                    lane.heldVel[(size_t)j] = lane.heldVel[(size_t)j + 1];
                }
                --lane.numHeld;
                break;
            }

        if (lane.numHeld == 0)
            lane.running = false; // the last arp note still finishes its gate
    }
}

void AudioPluginAudioProcessor::handleNoteOff(int note)
{
    removeKeyFromArps(note); // stops feeding any arpeggiators this key was driving
    releaseVoices(note);     // and releases its one-shot voices
}

int AudioPluginAudioProcessor::startVoiceOnSlot(int slot, int note, float velocity, const SlotParams* sp,
    int glideFromNote, bool fromArp, juce::uint32 group)
{
    auto sample = std::atomic_load(&samples[(size_t)slot]);
    if (sample == nullptr) return -1; // slot was cleared between param snapshot and now

    // Free voice, or steal the oldest one.
    // ponytail: hard voice steal can click; add a short fade-out if it bothers you.
    int target = -1;
    for (int i = 0; i < kNumVoices; ++i)
    {
        const auto& candidate = voices[(size_t)i];
        if (!candidate.active) { target = i; break; }
        if (group != 0 && candidate.group == group) continue; // never steal from the hit we're building
        if (target < 0 || candidate.age > voices[(size_t)target].age) target = i;
    }
    if (target < 0) return -1;

    auto& v = voices[(size_t)target];
    v.active = true;
    v.released = false;
    v.fromArp = fromArp;
    v.id = nextVoiceId++;
    v.group = group != 0 ? group : v.id;
    v.looped = false;
    v.beforeLoopEnd = false; // set on the first sample if the start point is before the loop end
    v.reversed = sp[slot].reverse; // fixed per note so toggling mid-note doesn't jump
    v.note = note;
    v.slot = slot;
    v.velocity = velocity;
    v.age = 0;
    v.sample = std::move(sample);

    // Start somewhere in [start, start + rand * (rest of sample)], re-rolled every note
    const auto& s = sp[slot];
    const double startFrac = s.start + random.nextDouble() * s.startRand * (1.0 - s.start);
    v.startPos = juce::jmin(startFrac, 0.999) * (v.sample->buffer.getNumSamples() - 1);
    v.pos = v.startPos;

    // First grain starts at its window peak so the attack isn't faded in; the second starts right away
    v.grains[0] = { v.startPos, juce::roundToInt(kGrainSeconds * hostSampleRate) / 2 };
    v.nextGrain = 1;
    v.grainCountdown = 0.0;

    // Glide: start at the previous note's pitch and slide to this one (only if this slot has glide on)
    v.glideFrom = (s.glide && glideFromNote >= 0) ? (float)(glideFromNote - note) : 0.0f;
    v.glideSamples = s.glideMs * 0.001 * hostSampleRate;
    v.glideElapsed = 0.0;

    v.env.reset();
    v.env.setParameters(s.env);
    v.env.noteOn();

    triggerCount[(size_t)slot]++;
    return target;
}

void AudioPluginAudioProcessor::releaseVoices(int note)
{
    for (auto& v : voices)
        if (v.active && !v.released && (note < 0 || (v.note == note && !v.fromArp)))
        {
            v.released = true;
            v.env.noteOff();
        }
}

//==============================================================================
void AudioPluginAudioProcessor::arpStep(int slot, const SlotParams* sp)
{
    auto& lane = arps[(size_t)slot];
    const auto& p = sp[slot];

    // Note list: held keys (sorted unless "As Played"), repeated up each extra octave
    std::array<int, kMaxArpNotes> order{};
    for (int i = 0; i < lane.numHeld; ++i) order[(size_t)i] = i;
    if (p.arpPattern != 4)
        std::sort(order.begin(), order.begin() + lane.numHeld,
            [&](int a, int b) { return lane.held[(size_t)a] < lane.held[(size_t)b]; });

    std::array<int, kMaxArpNotes * 4> notes{};
    std::array<float, kMaxArpNotes * 4> vels{};
    int count = 0;
    for (int o = 0; o < p.arpOctaves; ++o)
        for (int k = 0; k < lane.numHeld; ++k)
        {
            notes[(size_t)count] = juce::jmin(127, lane.held[(size_t)order[(size_t)k]] + 12 * o);
            vels[(size_t)count] = lane.heldVel[(size_t)order[(size_t)k]];
            ++count;
        }

    int idx = 0;
    switch (p.arpPattern)
    {
    case 1:  idx = count - 1 - lane.step % count; break;                        // Down
    case 2: {
        const int period = juce::jmax(1, 2 * count - 2);                  // Up/Down (no repeated ends)
        const int k = lane.step % period;
        idx = k < count ? k : period - k; break;
    }
    case 3:  idx = random.nextInt(count); break;                                // Random
    default: idx = lane.step % count; break;                                     // Up, As Played
    }

    releaseArpVoice(lane); // previous step, in case its gate reached the next step

    const float vel = p.arpVelocity > 0 ? (float)p.arpVelocity / 127.0f : vels[(size_t)idx];
    const int vi = startVoiceOnSlot(slot, notes[(size_t)idx], vel, sp, lane.lastNote, true, 0);
    lane.voiceIndex = vi;
    lane.voiceId = vi >= 0 ? voices[(size_t)vi].group : 0;
    lane.lastNote = notes[(size_t)idx];

    const double stepLen = juce::jmax(1.0, 60.0 / bpm * p.arpBeats * hostSampleRate);
    lane.gateLeft = juce::jmax(1.0, (double)p.arpGate * stepLen);

    // Swing delays every second step; the pair still adds up to two steps so it stays on the grid
    const double swing = p.arpSwing * 0.5 * stepLen;
    lane.toNextStep += (lane.step % 2 == 0) ? stepLen + swing : stepLen - swing;
    ++lane.step;
}

void AudioPluginAudioProcessor::releaseArpVoice(ArpLane& lane)
{
    if (lane.voiceIndex >= 0) // release the step's sound and its layers
        for (auto& v : voices)
            if (v.active && !v.released && v.group == lane.voiceId)
            {
                v.released = true;
                v.env.noteOff();
            }
    lane.voiceIndex = -1;
    lane.gateLeft = -1.0;
}

void AudioPluginAudioProcessor::stopArp(int slot)
{
    auto& lane = arps[(size_t)slot];
    releaseArpVoice(lane);
    lane.running = false;
    lane.numHeld = 0;
    lane.lastNote = -1;
    for (auto& k : keyArps)
        k &= ~(1u << slot);
}

double AudioPluginAudioProcessor::samplesToNextArpEvent() const
{
    double next = 1.0e12;
    for (const auto& lane : arps)
    {
        if (lane.running)      next = juce::jmin(next, lane.toNextStep);
        if (lane.gateLeft >= 0) next = juce::jmin(next, lane.gateLeft);
    }
    return next;
}

void AudioPluginAudioProcessor::advanceArps(int numSamples)
{
    for (auto& lane : arps)
    {
        if (lane.running)       lane.toNextStep -= numSamples;
        if (lane.gateLeft >= 0) lane.gateLeft = juce::jmax(0.0, lane.gateLeft - numSamples);
    }
}

void AudioPluginAudioProcessor::processDueArpEvents(const SlotParams* sp)
{
    for (int l = 0; l < kNumLayers; ++l)
    {
        auto& lane = arps[(size_t)l];

        if (!sp[l].arp && (lane.running || lane.numHeld > 0))
            stopArp(l); // arp switched off while playing

        if (lane.gateLeft >= 0 && lane.gateLeft <= 0.0)
            releaseArpVoice(lane);

        if (lane.running && lane.toNextStep <= 0.0)
            arpStep(l, sp);
    }
}

void AudioPluginAudioProcessor::renderVoices(int start, int num, const SlotParams* sp)
{
    if (num <= 0) return;
    const double fadeLen = 0.002 * hostSampleRate; // 2 ms fade-in when not starting at the very beginning
    const int grainLen = juce::roundToInt(kGrainSeconds * hostSampleRate);
    const double hop = grainLen * 0.5;             // 50 % overlap: two Hann windows sum to exactly 1

    for (auto& v : voices)
    {
        if (!v.active) continue;

        const auto& p = sp[v.slot];
        v.env.setParameters(p.env);
        auto* outL = buses[(size_t)v.slot].getWritePointer(0, start); // each slot renders into its own bus
        auto* outR = buses[(size_t)v.slot].getWritePointer(1, start);
        const auto& s = *v.sample;
        const int len = s.buffer.getNumSamples();
        const float* srcL = s.buffer.getReadPointer(0);
        const float* srcR = s.buffer.getNumChannels() > 1 ? s.buffer.getReadPointer(1) : srcL;

        const double rateRatio = s.sampleRate / hostSampleRate;
        const double basePitch = p.pitch + (float)(v.note - 60); // note 60 = original pitch
        const double baseSpeed = rateRatio * std::pow(2.0, basePitch / 12.0);
        const bool stretching = std::abs(p.stretch - 1.0) > 0.005;
        const float gain = p.gain * v.velocity;

        // Read the sound at a position in "playback space" (mirrored when reversed); silence outside it
        auto readAt = [&](double pos, float& a, float& b)
            {
                const int idx = (int)pos;
                if (pos < 0.0 || idx >= len - 1) { a = b = 0.0f; return; }
                const int i0 = v.reversed ? len - 1 - idx : idx;
                const int i1 = v.reversed ? i0 - 1 : i0 + 1;
                const float frac = (float)(pos - idx);
                a = srcL[i0] + frac * (srcL[i1] - srcL[i0]); // linear interpolation
                b = srcR[i0] + frac * (srcR[i1] - srcR[i0]);
            };

        // Loop region (playback space). Ignored unless it's at least 10 ms long.
        const double loopStartPos = p.loopStart * (len - 1), loopEndPos = p.loopEnd * (len - 1);
        const double loopLen = loopEndPos - loopStartPos;
        const bool looping = p.loop && loopLen >= 0.01 * s.sampleRate;
        // Seam crossfade (up to 4 ms): fade in the audio just before loop start as we near loop end;
        // if the loop starts at the very beginning, instead fade out what came after loop end once we've wrapped.
        const double xfade = juce::jmin(0.004 * s.sampleRate, loopLen * 0.5);
        const double tailFade = 0.002 * s.sampleRate; // in the sound's own samples
        const bool xfadeBefore = loopStartPos >= xfade;
        const bool xfadeAfter = !xfadeBefore && loopEndPos + xfade < len - 1;
        // No spare audio on either side (loop = whole file): crossfade inside the loop instead,
        // blending its last few ms into its first few and wrapping to just after them
        const bool xfadeInside = !xfadeBefore && !xfadeAfter;
        const double wrapLen = xfadeInside ? loopLen - xfade : loopLen;

        for (int i = 0; i < num; ++i)
        {
            const float envValue = v.env.getNextSample();
            v.envLevel = envValue;
            if (!v.env.isActive()) { v.active = false; break; }
            // Reached the loop end (coming from before it): back to the loop start. A note whose
            // (random) start is already past the loop end just plays on to the end of the sound.
            if (looping && v.beforeLoopEnd && v.pos >= loopEndPos)
            {
                do { v.pos -= wrapLen; } while (v.pos >= loopEndPos);
                v.looped = true;
            }
            v.beforeLoopEnd = v.pos < loopEndPos;
            if ((int)v.pos >= len - 1) { v.active = false; break; }

            // Current playback speed, including any glide in progress
            double speed = baseSpeed;
            if (v.glideFrom != 0.0f && v.glideElapsed < v.glideSamples) // linear slide in semitones
            {
                const double offset = v.glideFrom * (1.0 - v.glideElapsed / v.glideSamples);
                speed = rateRatio * std::pow(2.0, (basePitch + offset) / 12.0);
                v.glideElapsed += 1.0;
            }

            float a = 0.0f, b = 0.0f;
            if (!stretching)
            {
                readAt(v.pos, a, b); // classic sampler: higher notes play faster/shorter

                if (looping && xfade > 1.0)
                {
                    float a2, b2;
                    // Only in the last few ms *before* the loop end (a note that started past it doesn't loop)
                    if ((xfadeBefore || xfadeInside) && v.pos > loopEndPos - xfade && v.pos < loopEndPos)
                    {
                        const float w = juce::jlimit(0.0f, 1.0f, (float)((v.pos - (loopEndPos - xfade)) / xfade));
                        readAt(v.pos - wrapLen, a2, b2); // what we'll be playing right after the wrap
                        a += w * (a2 - a);
                        b += w * (b2 - b);
                    }
                    else if (xfadeAfter && v.looped && v.pos >= loopStartPos && v.pos < loopStartPos + xfade)
                    {
                        const float w = juce::jlimit(0.0f, 1.0f, (float)((v.pos - loopStartPos) / xfade));
                        readAt(v.pos + loopLen, a2, b2); // what would have played without the wrap
                        a = a2 + w * (a - a2);
                        b = b2 + w * (b - b2);
                    }
                }
                v.pos += speed;
            }
            else
            {
                // Stretch: grains carry the pitch, pos moves at the stretched speed (pitch-independent length)
                if ((v.grainCountdown -= 1.0) <= 0.0)
                {
                    // WSOLA: nudge the new grain (within +-8 ms) to where it lines up best with the grain
                    // that's fading out, so they don't cancel each other and cause "phasey" warble
                    double grainStart = v.pos;
                    const auto& fading = v.grains[(size_t)(v.nextGrain ^ 1)];
                    if (fading.age < grainLen)
                    {
                        const int tolerance = (int)(0.008 * s.sampleRate);
                        float bestScore = -1.0e30f;
                        for (int d = -tolerance; d <= tolerance; d += 2)
                        {
                            const double candidate = v.pos + d;
                            if (candidate < 0.0) continue;
                            float dot = 0.0f, energy = 1.0e-9f, ref, cand, unused;
                            for (int k = 0; k < 32; ++k)
                            {
                                readAt(fading.pos + k * 2 * speed, ref, unused);
                                readAt(candidate + k * 2 * speed, cand, unused);
                                dot += ref * cand;
                                energy += cand * cand;
                            }
                            if (const float score = dot / std::sqrt(energy); score > bestScore)
                            {
                                bestScore = score;
                                grainStart = candidate;
                            }
                        }
                    }
                    v.grains[(size_t)v.nextGrain] = { grainStart, 0 };
                    v.nextGrain ^= 1;
                    v.grainCountdown += hop;
                }
                for (auto& gr : v.grains)
                {
                    if (gr.age >= grainLen) continue;
                    const float w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float)gr.age / (float)grainLen);
                    float ga, gb;
                    readAt(gr.pos, ga, gb);
                    a += ga * w;
                    b += gb * w;
                    gr.pos += speed;
                    ++gr.age;
                }
                v.pos += rateRatio / p.stretch;
            }

            float g = gain * envValue;
            // 2 ms fade at the very end of the file, so sounds that end abruptly (chopped samples) don't click
            // (not when the loop is about to wrap us back instead)
            if (const double remaining = (len - 1) - v.pos; remaining < tailFade && !(looping && v.beforeLoopEnd))
                g *= (float)juce::jmax(0.0, remaining / tailFade);
            const double played = (double)(v.age + i);
            if ((v.startPos > 0.0 || v.reversed) && played < fadeLen) // reversed starts on the (maybe cut-off) tail
                g *= (float)(played / fadeLen);

            outL[i] += a * g;
            outR[i] += b * g;
        }

        v.age += num;
        // The voice keeps its SamplePtr until reused; the retired list guarantees we never free memory here.
    }
}

static double arpBeatsFor(int rateIndex) // quarter notes per step, matching kArpRates
{
    constexpr double beats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return beats[juce::jlimit(0, 5, rateIndex)];
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    SlotParams sp[kNumLayers];
    for (size_t l = 0; l < kNumLayers; ++l)
    {
        sp[l].loaded = std::atomic_load(&samples[l]) != nullptr;
        sp[l].playable = sp[l].loaded && muteParam[l]->load() < 0.5f; // can be picked; muted = layer only
        sp[l].gain = juce::Decibels::decibelsToGain(volParam[l]->load(), -60.0f);
        sp[l].pitch = pitchParam[l]->load();
        sp[l].stretch = stretchParam[l]->load() * 0.01;
        sp[l].start = startParam[l]->load();
        sp[l].startRand = randParam[l]->load();
        sp[l].reverse = reverseParam[l]->load() > 0.5f;
        sp[l].glide = glideParam[l]->load() > 0.5f;
        sp[l].glideMs = glideTimeParam[l]->load();
        sp[l].legato = legatoParam[l]->load() > 0.5f;
        sp[l].loop = loopParam[l]->load() > 0.5f;
        sp[l].loopStart = loopStartParam[l]->load();
        sp[l].loopEnd = loopEndParam[l]->load();
        for (size_t j = 0; j < kNumLayers; ++j)
            sp[l].links[j] = j != l && linkParam[l][j]->load() > 0.5f;
        sp[l].arp = arpParam[l]->load() > 0.5f;
        sp[l].arpBeats = arpBeatsFor((int)arpRateParam[l]->load());
        sp[l].arpPattern = (int)arpPatternParam[l]->load();
        sp[l].arpOctaves = (int)arpOctParam[l]->load();
        sp[l].arpGate = arpGateParam[l]->load() * 0.01f;
        sp[l].arpSwing = arpSwingParam[l]->load() * 0.01f;
        sp[l].arpVelocity = (int)arpVelParam[l]->load();
        sp[l].env = { attackParam[l]->load() * 0.001f, decayParam[l]->load() * 0.001f,
                      sustainParam[l]->load(), releaseParam[l]->load() * 0.001f };
    }

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto hostBpm = position->getBpm())
                bpm = juce::jlimit(20.0, 999.0, *hostBpm);

    // Sample-accurate: render up to the next MIDI event or arp step/gate-off, handle it, repeat
    const int numSamples = buffer.getNumSamples();
    for (auto& bus : buses)
    {
        if (bus.getNumSamples() < numSamples) // host sent a bigger block than promised (rare)
            bus.setSize(2, numSamples, false, false, true);
        bus.clear(0, numSamples);
    }

    auto midiIt = midi.begin();
    int pos = 0;
    for (int guard = 0; guard < numSamples + midi.getNumEvents() + 4096; ++guard) // always terminates
    {
        int segEnd = midiIt != midi.end() ? juce::jlimit(pos, numSamples, (*midiIt).samplePosition) : numSamples;
        segEnd = juce::jmin(segEnd, pos + (int)std::ceil(juce::jmax(0.0, juce::jmin(samplesToNextArpEvent(), 1.0e9))));

        renderVoices(pos, segEnd - pos, sp);
        advanceArps(segEnd - pos);
        pos = segEnd;

        for (; midiIt != midi.end() && juce::jmin((*midiIt).samplePosition, numSamples) <= pos; ++midiIt)
        {
            const auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn())
                handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity(), sp);
            else if (msg.isNoteOff())
                handleNoteOff(msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                for (int l = 0; l < kNumLayers; ++l) stopArp(l);
                releaseVoices(-1);
            }
        }

        processDueArpEvents(sp);

        if (pos >= numSamples && midiIt == midi.end())
            break;
    }

    for (size_t i = 0; i < kNumVoices; ++i)
    {
        const auto& v = voices[i];
        const bool sounding = v.active;
        playheadSlot[i] = v.slot;
        playheadPos[i] = sounding ? (float)(v.pos / v.sample->buffer.getNumSamples()) : 0.0f;
        playheadLevel[i] = sounding ? v.envLevel : 0.0f;
    }

    // Effects per slot, then mix the slots down
    for (int l = 0; l < kNumLayers; ++l)
    {
        processSlotFx(l, numSamples);
        const auto& bus = buses[(size_t)l];
        if (buffer.getNumChannels() > 1)
        {
            buffer.addFrom(0, 0, bus, 0, 0, numSamples);
            buffer.addFrom(1, 0, bus, 1, 0, numSamples);
        }
        else
        {
            buffer.addFrom(0, 0, bus, 0, 0, numSamples, 0.5f);
            buffer.addFrom(0, 0, bus, 1, 0, numSamples, 0.5f);
        }
    }

    buffer.applyGain(juce::Decibels::decibelsToGain(masterParam->load(), -60.0f));
}

static double delayBeatsFor(int timeIndex) // quarter notes, matching kDelayTimes
{
    constexpr double beats[] = { 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 };
    return beats[juce::jlimit(0, 8, timeIndex)];
}

void AudioPluginAudioProcessor::processSlotFx(int slot, int n)
{
    auto& f = fx[(size_t)slot];
    const auto& p = fxp[(size_t)slot];
    auto& bus = buses[(size_t)slot];

    // Effects always run while on (even with no notes) so tails ring out naturally
    auto enabled = [&](int which, std::atomic<float>* onParam, auto&& resetFn)
        {
            const bool on = onParam->load() > 0.5f;
            if (on && !f.wasOn[(size_t)which]) resetFn(); // fresh start, no stale tail from last time
            f.wasOn[(size_t)which] = on;
            return on;
        };

    // --- High-pass / low-pass (first in the chain). Fully off at the ends of their ranges.
    // Switching a filter on/off crossfades over 5 ms (a filter starting mid-sound rings briefly).
    {
        const float nyquistSafe = (float)hostSampleRate * 0.45f;
        f.hpFreq.setTargetValue(p.hpf->load());
        f.lpFreq.setTargetValue(juce::jmin(p.lpf->load(), 20000.0f));
        f.hpMix.setTargetValue(f.hpFreq.getTargetValue() > 20.5f ? 1.0f : 0.0f);
        f.lpMix.setTargetValue(f.lpFreq.getTargetValue() < 19990.0f ? 1.0f : 0.0f);

        const bool runHp = f.hpMix.isSmoothing() || f.hpMix.getTargetValue() > 0.0f;
        const bool runLp = f.lpMix.isSmoothing() || f.lpMix.getTargetValue() > 0.0f;
        if (runHp && !f.hpActive) f.hp.reset(); // fresh start (faded in, so its first moments are hidden)
        if (runLp && !f.lpActive) f.lp.reset();
        f.hpActive = runHp;
        f.lpActive = runLp;

        if (runHp || runLp)
        {
            auto* l = bus.getWritePointer(0);
            auto* r = bus.getWritePointer(1);
            for (int start = 0; start < n; start += 32) // update the cutoff every 32 samples while gliding
            {
                const int len = juce::jmin(32, n - start);
                const float hpHz = f.hpFreq.skip(len), lpHz = f.lpFreq.skip(len);
                if (runHp) f.hp.setCutoffFrequency(juce::jlimit(20.0f, nyquistSafe, hpHz));
                if (runLp) f.lp.setCutoffFrequency(juce::jlimit(20.0f, nyquistSafe, lpHz));
                for (int i = start; i < start + len; ++i)
                {
                    float a = l[i], b = r[i];
                    if (runHp)
                    {
                        const float m = f.hpMix.getNextValue();
                        a += m * (f.hp.processSample(0, a) - a);
                        b += m * (f.hp.processSample(1, b) - b);
                    }
                    if (runLp)
                    {
                        const float m = f.lpMix.getNextValue();
                        a += m * (f.lp.processSample(0, a) - a);
                        b += m * (f.lp.processSample(1, b) - b);
                    }
                    l[i] = a; r[i] = b;
                }
            }
        }
        else
        {
            f.hpFreq.skip(n);
            f.lpFreq.skip(n);
        }
    }

    if (enabled(0, p.distOn, [&] { f.dist.reset(); }))
        f.dist.process(bus, n, (int)p.distType->load(), p.distDrive->load(), p.distTone->load(), p.distMix->load() * 0.01f);

    if (enabled(1, p.grOn, [&] { f.granular.reset(); }))
        f.granular.process(bus, n, p.grSize->load(), p.grDensity->load(), p.grSpray->load(), p.grPitch->load(),
            p.grMix->load() * 0.01f);

    if (enabled(2, p.dlyOn, [&] { f.delay.reset(); }))
        f.delay.process(bus, n, 60.0 / bpm * delayBeatsFor((int)p.dlyTime->load()) * hostSampleRate,
            p.dlyFb->load() * 0.01f, p.dlyMix->load() * 0.01f, p.dlyPing->load() > 0.5f);

    if (enabled(3, p.revOn, [&] { f.reverb.reset(); }))
    {
        const float mix = p.revMix->load() * 0.01f;
        juce::Reverb::Parameters rp;
        rp.roomSize = p.revSize->load() * 0.01f;
        rp.damping = p.revDamp->load() * 0.01f;
        rp.width = p.revWidth->load() * 0.01f;
        rp.wetLevel = mix * 0.5f;          // juce::Reverb scales wet by 3 and dry by 2 internally
        rp.dryLevel = (1.0f - mix) * 0.5f; // so dry 0.5 = unity
        f.reverb.setParameters(rp);
        f.reverb.processStereo(bus.getWritePointer(0), bus.getWritePointer(1), n);
    }
}

//==============================================================================
bool AudioPluginAudioProcessor::loadSample(int layer, const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples < 2)
        return false;

    // ponytail: 30 s cap keeps memory sane for "one-shots"; raise it if you load loops.
    const int len = (int)juce::jmin(reader->lengthInSamples, (juce::int64)(reader->sampleRate * 30.0));

    auto s = std::make_shared<Sample>();
    s->buffer.setSize(juce::jmin(2, (int)reader->numChannels), len);
    reader->read(&s->buffer, 0, len, 0, true, true);
    s->sampleRate = reader->sampleRate;
    s->path = file.getFullPathName();

    setSample(layer, std::move(s));
    return true;
}

void AudioPluginAudioProcessor::clearSample(int layer)
{
    setSample(layer, nullptr);
}

void AudioPluginAudioProcessor::setSample(int layer, SamplePtr newSample)
{
    const juce::ScopedLock sl(retiredLock);
    if (auto old = std::atomic_load(&samples[(size_t)layer]))
        retired.push_back(std::move(old)); // never let the audio thread hold the last reference
    std::atomic_store(&samples[(size_t)layer], std::move(newSample));
}

void AudioPluginAudioProcessor::timerCallback()
{
    const juce::ScopedLock sl(retiredLock);
    retired.erase(std::remove_if(retired.begin(), retired.end(),
        [](const SamplePtr& p) { return p.use_count() <= 1; }),
        retired.end());
}

//==============================================================================
// Full state = parameters + sample paths + preset name. Shared by the FL project save and preset files.
// Paths are stored absolute, plus relative to `baseDir` when given, so a preset folder that's
// moved together with its samples still loads.
juce::ValueTree AudioPluginAudioProcessor::createStateTree(const juce::File& baseDir)
{
    auto state = apvts.copyState();

    if (auto old = state.getChildWithName("Samples"); old.isValid())
        state.removeChild(old, nullptr);

    juce::ValueTree paths("Samples");
    for (int l = 0; l < kNumLayers; ++l)
        if (auto s = getSample(l))
        {
            paths.setProperty("layer" + juce::String(l), s->path, nullptr);
            if (baseDir != juce::File()) // always stored with '/' so presets work on both Windows and Mac
                paths.setProperty("layer" + juce::String(l) + "_rel",
                    juce::File(s->path).getRelativePathFrom(baseDir).replaceCharacter('\\', '/'), nullptr);
        }
    state.appendChild(paths, nullptr);
    state.setProperty("presetName", currentPresetName, nullptr);
    return state;
}

bool AudioPluginAudioProcessor::applyStateTree(const juce::ValueTree& tree, const juce::File& baseDir)
{
    if (!tree.hasType(apvts.state.getType()))
        return false;

    const auto paths = tree.getChildWithName("Samples");
    for (int l = 0; l < kNumLayers; ++l)
    {
        const auto key = "layer" + juce::String(l);
        const auto absPath = paths.getProperty(key).toString();
        const auto relPath = paths.getProperty(key + "_rel").toString();

        bool loaded = absPath.isNotEmpty() && loadSample(l, juce::File(absPath));
        if (!loaded && relPath.isNotEmpty() && baseDir != juce::File())
        {
            // Accept either separator (older Windows presets used '\\'), then use this system's own
            const auto nativeRel = relPath.replaceCharacter('\\', '/')
                .replaceCharacter('/', juce::File::getSeparatorChar());
            loaded = loadSample(l, baseDir.getChildFile(nativeRel));
        }
        if (!loaded)
            clearSample(l); // missing samples come back empty
    }

    currentPresetName = tree.getProperty("presetName").toString();
    apvts.replaceState(tree.createCopy());
    return true;
}

void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = createStateTree({}).createXml())
        copyXmlToBinary(*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        applyStateTree(juce::ValueTree::fromXml(*xml), {});
}

//==============================================================================
// The presets-folder choice lives in a small settings file (AppData on Windows, Application Support on
// Mac). It's opened briefly when needed rather than kept in a static, so nothing outlives the plugin.
static juce::PropertiesFile::Options settingsOptions(const juce::String& pluginName = JucePlugin_Name)
{
    juce::PropertiesFile::Options o;
    o.applicationName = pluginName;
    o.folderName = pluginName;
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

// Names this plugin was built under before it became crayonic. Their presets folders and settings
// are carried over once, the first time the new name is used, so nobody's presets "disappear".
static const juce::StringArray& previousNames()
{
    static const juce::StringArray names{ "TEST", "My Plugin" }; // plain strings: nothing to clean up at unload
    return names;
}

static void carryOverFromPreviousName(juce::PropertiesFile& settings)
{
    if (settings.getBoolValue("carriedOverPreviousName", false))
        return;
    settings.setValue("carriedOverPreviousName", true);

    const auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    const auto newPresets = docs.getChildFile(JucePlugin_Name).getChildFile("Presets");

    for (const auto& oldName : previousNames())
    {
        if (oldName == juce::String(JucePlugin_Name)) continue;

        // A custom presets folder chosen in the old version (another drive, say)
        juce::PropertiesFile oldSettings(settingsOptions(oldName));
        const auto oldCustom = oldSettings.getValue("presetsFolder");
        if (oldCustom.isNotEmpty() && !settings.containsKey("presetsFolder"))
            settings.setValue("presetsFolder", oldCustom);

        // The old default presets folder: copied (not moved), so the old version keeps working too
        const auto oldPresets = docs.getChildFile(oldName).getChildFile("Presets");
        if (oldPresets.isDirectory() && !newPresets.exists())
            oldPresets.copyDirectoryTo(newPresets);
    }
    settings.saveIfNeeded();
}

juce::File AudioPluginAudioProcessor::getDefaultPresetFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile(JucePlugin_Name).getChildFile("Presets");
}

juce::File AudioPluginAudioProcessor::getPresetFolder()
{
    juce::PropertiesFile settings(settingsOptions());
    carryOverFromPreviousName(settings);
    const auto custom = settings.getValue("presetsFolder");
    if (custom.isNotEmpty() && juce::File::isAbsolutePath(custom) && juce::File(custom).isDirectory())
        return juce::File(custom);

    auto dir = getDefaultPresetFolder(); // also the fallback if a chosen drive is unplugged
    dir.createDirectory();
    return dir;
}

void AudioPluginAudioProcessor::setPresetFolder(const juce::File& folder)
{
    juce::PropertiesFile settings(settingsOptions());
    if (folder == juce::File() || folder == getDefaultPresetFolder())
        settings.removeValue("presetsFolder");
    else
        settings.setValue("presetsFolder", folder.getFullPathName());
    settings.saveIfNeeded();
}

bool AudioPluginAudioProcessor::usingCustomPresetFolder()
{
    return getPresetFolder() != getDefaultPresetFolder();
}

bool AudioPluginAudioProcessor::savePreset(const juce::File& file)
{
    currentPresetName = file.getFileNameWithoutExtension();
    auto xml = createStateTree(file.getParentDirectory()).createXml();
    const bool ok = xml != nullptr && xml->writeTo(file);
    if (ok) currentPresetFile = file;
    return ok;
}

bool AudioPluginAudioProcessor::loadPreset(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (xml == nullptr || !applyStateTree(juce::ValueTree::fromXml(*xml), file.getParentDirectory()))
        return false;
    currentPresetName = file.getFileNameWithoutExtension(); // file name wins if it was renamed
    currentPresetFile = file;
    return true;
}

void AudioPluginAudioProcessor::initPreset()
{
    for (auto* param : getParameters())
        param->setValueNotifyingHost(param->getDefaultValue());
    for (int l = 0; l < kNumLayers; ++l)
        clearSample(l);
    currentPresetName = {};
    currentPresetFile = juce::File();
}

//==============================================================================
void AudioPluginAudioProcessor::copySettings(int from, int to, const juce::StringArray& names)
{
    if (from == to || from < 0 || to < 0 || from >= kNumLayers || to >= kNumLayers) return;

    auto copy = [](juce::RangedAudioParameter* src, juce::RangedAudioParameter* dst)
        {
            if (src == nullptr || dst == nullptr) return;
            dst->beginChangeGesture(); // one undo-able, automation-friendly change per setting
            dst->setValueNotifyingHost(src->getValue());
            dst->endChangeGesture();
        };

    for (const auto& name : names)
    {
        if (name == "@layers")
        {
            // "from plays X" becomes "to plays X", for every X other than the two sounds involved
            for (int j = 0; j < kNumLayers; ++j)
                if (j != from && j != to)
                    copy(apvts.getParameter(linkParamID(from, j)), apvts.getParameter(linkParamID(to, j)));
            continue;
        }
        copy(apvts.getParameter(layerParamID(from, name)), apvts.getParameter(layerParamID(to, name)));
    }
}

juce::StringArray AudioPluginAudioProcessor::settingNamesWithPrefix(int slot, const juce::String& prefix) const
{
    juce::StringArray names;
    const auto slotPrefix = layerParamID(slot, "");
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (const auto id = ranged->getParameterID(); id.startsWith(slotPrefix + prefix))
                names.add(id.fromFirstOccurrenceOf(slotPrefix, false, false));
    return names;
}

juce::Array<juce::File> AudioPluginAudioProcessor::getKitFolders()
{
    auto kits = getPresetFolder().findChildFiles(juce::File::findDirectories, false);
    kits.removeIf([](const juce::File& f) { return f.isHidden() || f.getFileName().startsWithChar('.'); });
    kits.sort();
    return kits;
}

juce::File AudioPluginAudioProcessor::createKit(const juce::String& name)
{
    auto clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty()) clean = "New Kit";
    auto folder = getPresetFolder().getNonexistentChildFile(clean, {}, true); // "Kit", "Kit (2)", ...
    folder.createDirectory();
    return folder;
}

AudioPluginAudioProcessor::KitExport AudioPluginAudioProcessor::exportKit(const juce::File& kitFolder, const juce::File& zipFile)
{
    KitExport result;
    const auto kitName = kitFolder.getFileName();
    auto presets = kitFolder.findChildFiles(juce::File::findFiles, false, "*" + presetExtension);
    presets.sort();

    juce::ZipFile::Builder zip;
    std::map<juce::String, juce::String> copied;  // original sound path -> file name inside the kit
    std::set<juce::String> usedNames;             // lower-case names already taken in Samples/

    for (const auto& presetFile : presets)
    {
        auto xml = juce::parseXML(presetFile);
        if (xml == nullptr) { result.missing.add(presetFile.getFileNameWithoutExtension() + ": couldn't read the preset"); continue; }

        if (auto* samples = xml->getChildByName("Samples"))
        {
            for (int i = 0; i < kNumLayers; ++i)
            {
                const auto absKey = "layer" + juce::String(i), relKey = absKey + "_rel";
                const auto abs = samples->getStringAttribute(absKey), rel = samples->getStringAttribute(relKey);
                if (abs.isEmpty() && rel.isEmpty()) continue;

                // Where's the sound? Its full path first, then relative to the preset (either separator)
                juce::File source;
                if (juce::File::isAbsolutePath(abs) && juce::File(abs).existsAsFile())
                    source = juce::File(abs);
                else if (rel.isNotEmpty())
                {
                    const auto candidate = presetFile.getParentDirectory().getChildFile(
                        rel.replaceCharacter('\\', '/').replaceCharacter('/', juce::File::getSeparatorChar()));
                    if (candidate.existsAsFile()) source = candidate;
                }

                samples->removeAttribute(absKey); // shared copies never contain your own folder paths
                if (source == juce::File())
                {
                    samples->removeAttribute(relKey);
                    result.missing.add(presetFile.getFileNameWithoutExtension() + ": sound " + juce::String(i + 1)
                        + " (" + (abs.isNotEmpty() ? abs : rel) + ")");
                    continue;
                }

                auto& name = copied[source.getFullPathName()];
                if (name.isEmpty()) // first time we see this sound: give it a unique name in Samples/
                {
                    name = source.getFileName();
                    for (int n = 2; usedNames.count(name.toLowerCase()) > 0; ++n)
                        name = source.getFileNameWithoutExtension() + " (" + juce::String(n) + ")" + source.getFileExtension();
                    usedNames.insert(name.toLowerCase());
                    zip.addFile(source, 9, kitName + "/Samples/" + name);
                }
                samples->setAttribute(relKey, "Samples/" + name);
            }
        }

        const auto text = xml->toString();
        zip.addEntry(new juce::MemoryInputStream(juce::MemoryBlock(text.toRawUTF8(), text.getNumBytesAsUTF8()), true),
            9, kitName + "/" + presetFile.getFileName(), presetFile.getLastModificationTime());
        ++result.presets;
    }

    const juce::String readme =
        "HOW TO INSTALL THIS KIT\n=======================\n\n"
        "Easiest: in " JucePlugin_Name ", click \"kits\" > \"Import kit...\" and pick this zip.\n\n"
        "Or by hand: unzip it and put the \"" + kitName + "\" folder inside " JucePlugin_Name "'s presets folder\n"
        "(\"kits\" > \"Show presets folder\" opens it). Keep its Samples folder with it.\n";
    zip.addEntry(new juce::MemoryInputStream(juce::MemoryBlock(readme.toRawUTF8(), readme.getNumBytesAsUTF8()), true),
        9, kitName + "/How to install this kit.txt", juce::Time::getCurrentTime());

    result.sounds = (int)copied.size();
    zipFile.deleteFile();
    juce::FileOutputStream out(zipFile);
    if (!out.openedOk() || !zip.writeToStream(out, nullptr))
        result.error = "Couldn't write " + zipFile.getFullPathName();
    return result;
}

juce::Result AudioPluginAudioProcessor::importKit(const juce::File& zipFile, juce::String& kitNameOut)
{
    juce::ZipFile zip(zipFile);
    if (zip.getNumEntries() == 0)
        return juce::Result::fail("That doesn't look like a kit zip.");

    // A kit zip has one top folder ("<kit>/..."). If it doesn't, unpack into a folder named after the zip.
    juce::String top;
    bool singleTopFolder = true;
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = zip.getEntry(i)->filename.replaceCharacter('\\', '/');
        const auto first = name.upToFirstOccurrenceOf("/", false, false);
        if (!name.containsChar('/') || (top.isNotEmpty() && first != top)) { singleTopFolder = false; break; }
        top = first;
    }

    const auto presetsFolder = getPresetFolder();
    const auto target = singleTopFolder ? presetsFolder : presetsFolder.getChildFile(zipFile.getFileNameWithoutExtension());
    kitNameOut = singleTopFolder ? top : zipFile.getFileNameWithoutExtension();
    return zip.uncompressTo(target, true); // JUCE refuses entries that would land outside the folder
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}