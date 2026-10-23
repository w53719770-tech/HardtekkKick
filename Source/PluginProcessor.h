#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

class HardtekkKickProcessor : public juce::AudioProcessor
{
public:
    HardtekkKickProcessor();
    ~HardtekkKickProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Hardtekk Kick"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // kick voice (monophonic, retriggers on every note)
    bool   active = false;
    double phase = 0.0, tSec = 0.0, sr = 44100.0;
    float  baseFreq = 50.0f, velocity = 1.0f;
    juce::Random rng;

    // rumble
    juce::Reverb reverb;
    float lpL = 0.0f, lpR = 0.0f;
    double sinceHit = 10.0;

    std::vector<float> kickBuf, rumL, rumR, duckBuf;

    void trigger (int note, float vel);
    float fp (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HardtekkKickProcessor)
};
