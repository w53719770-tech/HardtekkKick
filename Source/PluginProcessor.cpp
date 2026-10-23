#include "PluginProcessor.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using NR  = juce::NormalisableRange<float>;

juce::AudioProcessorValueTreeState::ParameterLayout HardtekkKickProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto add = [&] (const char* id, const char* name, float lo, float hi, float def, float skew = 1.0f)
    {
        NR r (lo, hi); r.setSkewForCentre (skew == 1.0f ? (lo + hi) * 0.5f : skew);
        p.push_back (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def));
    };

    // Kick body
    add ("tune",       "Tune (Hz)",          30.f,  120.f,  48.f);
    add ("pitchStart", "Pitch Start (Hz)",   100.f, 1200.f, 380.f, 350.f);
    add ("pitchDecay", "Pitch Decay (ms)",   3.f,   250.f,  32.f,  40.f);
    add ("ampDecay",   "Amp Decay (ms)",     40.f,  800.f,  260.f, 250.f);
    add ("click",      "Click",              0.f,   1.f,    0.35f);
    p.push_back (std::make_unique<APB> (juce::ParameterID { "keytrack", 1 }, "Keytrack", false));

    // Distortion
    add ("drive",      "Drive",              0.f,   1.f,    0.75f);
    add ("clip",       "Clip Ceiling",       0.2f,  1.f,    0.7f);

    // Rumble (reverb -> LP -> distortion -> ducked by kick)
    add ("rumble",     "Rumble Amount",      0.f,   1.f,    0.55f);
    add ("rumbleSize", "Rumble Size",        0.2f,  1.f,    0.85f);
    add ("rumbleLP",   "Rumble LP (Hz)",     60.f,  1000.f, 180.f, 250.f);
    add ("rumbleDrive","Rumble Drive",       0.f,   1.f,    0.6f);
    add ("duck",       "Rumble Duck (ms)",   20.f,  400.f,  150.f, 120.f);

    add ("out",        "Output (dB)",       -24.f,  6.f,   -3.f,  -6.f);
    return { p.begin(), p.end() };
}

HardtekkKickProcessor::HardtekkKickProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool HardtekkKickProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void HardtekkKickProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    sr = sampleRate;
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    lpL = lpR = 0.0f;
    active = false;
    sinceHit = 10.0;
    auto n = (size_t) juce::jmax (blockSize, 1);
    kickBuf.assign (n, 0.f); rumL.assign (n, 0.f); rumR.assign (n, 0.f); duckBuf.assign (n, 1.f);
}

void HardtekkKickProcessor::trigger (int note, float vel)
{
    active = true;
    phase = 0.0;
    tSec = 0.0;
    sinceHit = 0.0;
    velocity = vel;
    const float tune = fp ("tune");
    baseFreq = fp ("keytrack") > 0.5f ? tune * std::pow (2.0f, (note - 36) / 12.0f) : tune;
}

void HardtekkKickProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if ((int) kickBuf.size() < n)
    {
        kickBuf.resize ((size_t) n); rumL.resize ((size_t) n); rumR.resize ((size_t) n); duckBuf.resize ((size_t) n);
    }

    const float pitchStart = fp ("pitchStart");
    const float pitchTau   = fp ("pitchDecay") * 0.001f;
    const float ampTau     = fp ("ampDecay") * 0.001f;
    const float click      = fp ("click");
    const float driveG     = 1.0f + fp ("drive") * 39.0f;
    const float clip       = fp ("clip");
    const float rumbleAmt  = fp ("rumble");
    const float rDriveG    = 1.0f + fp ("rumbleDrive") * 19.0f;
    const float duckTau    = fp ("duck") * 0.001f;
    const float outGain    = juce::Decibels::decibelsToGain (fp ("out"));
    const float lpCoef     = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fp ("rumbleLP") / (float) sr);
    const double dt        = 1.0 / sr;

    juce::Reverb::Parameters rp;
    rp.roomSize = fp ("rumbleSize"); rp.damping = 0.55f;
    rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f; rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    auto midiIt = midi.begin();

    for (int i = 0; i < n; ++i)
    {
        while (midiIt != midi.end() && (*midiIt).samplePosition <= i)
        {
            auto m = (*midiIt).getMessage();
            if (m.isNoteOn()) trigger (m.getNoteNumber(), m.getFloatVelocity());
            ++midiIt;
        }

        float k = 0.0f;
        if (active)
        {
            const float pEnv = std::exp (-(float) tSec / pitchTau);
            const float freq = baseFreq + (pitchStart - baseFreq) * pEnv;
            phase += freq * dt;
            phase -= std::floor (phase);

            const float attack = juce::jmin (1.0f, (float) tSec / 0.0008f);   // 0.8ms anti-pop
            const float aEnv   = std::exp (-(float) tSec / ampTau) * attack;

            float body = std::sin ((float) phase * juce::MathConstants<float>::twoPi);
            float clk  = (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) tSec / 0.0015f) * click;
            k = (body * aEnv + clk) * velocity;

            // the hardtekk crunch: heavy tanh saturation then hard ceiling
            k = std::tanh (k * driveG);
            k = juce::jlimit (-clip, clip, k) / clip;

            tSec += dt;
            if (aEnv < 0.0005f && tSec > 0.01) active = false;
        }
        kickBuf[(size_t) i] = k;

        duckBuf[(size_t) i] = 1.0f - std::exp (-(float) sinceHit / duckTau);
        sinceHit += dt;
    }

    // rumble: reverb of the distorted kick, low-passed, re-distorted, ducked by the kick
    for (int i = 0; i < n; ++i) { rumL[(size_t) i] = rumR[(size_t) i] = kickBuf[(size_t) i]; }
    reverb.processStereo (rumL.data(), rumR.data(), n);

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        lpL += lpCoef * (rumL[(size_t) i] - lpL);
        lpR += lpCoef * (rumR[(size_t) i] - lpR);
        const float d = duckBuf[(size_t) i];
        const float rl = std::tanh (lpL * rDriveG * 2.0f) * d * rumbleAmt;
        const float rr = std::tanh (lpR * rDriveG * 2.0f) * d * rumbleAmt;

        const float k = kickBuf[(size_t) i];
        outL[i] = std::tanh ((k + rl) * 1.1f) * outGain;
        outR[i] = std::tanh ((k + rr) * 1.1f) * outGain;
    }
    midi.clear();
}

juce::AudioProcessorEditor* HardtekkKickProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void HardtekkKickProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void HardtekkKickProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HardtekkKickProcessor(); }
