#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "HitCore.h"

class XYSamplerProcessor : public juce::AudioProcessor
{
public:
    XYSamplerProcessor();
    ~XYSamplerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return hit::kMaxT60; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Message thread
    bool loadSampleFile (const juce::File&);
    bool loadSampleBytes (const juce::String& name, const juce::MemoryBlock& bytes);
    void triggerFromUI (float velocity = 1.0f) { uiTrigger.store ((int) (juce::jlimit (0.0f, 1.0f, velocity) * 127.0f) + 1); }
    juce::var getSampleInfo() const;   // { name, dur, peaks[] } or void
    int  getSampleVersion() const { return sampleVersion.load(); }
    juce::File lastDirectory;

    // UI state saved with the session
    juce::String uiTheme { "dark" };
    double uiScale = 1.0;

    // Audio thread -> UI
    std::atomic<float> liveX { 0.5f }, liveY { 0.5f };
    std::atomic<float> lastX { 0.5f }, lastY { 0.5f }, lastT60 { 0.0f }, lastSt { 0.0f };
    std::atomic<int>   hitSerial { 0 };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    static const juce::StringArray patternNames, rateNames, clockNames;
    static constexpr double rateBeats[] = { 4.0, 2.0, 1.0, 0.5 };

private:
    hit::Params readParams() const;

    hit::Engine engine;
    juce::AudioFormatManager formatManager;

    std::shared_ptr<const hit::SampleData> currentSample, previousSample, pendingSample;
    juce::SpinLock sampleLock;
    std::atomic<bool> samplePending { false };
    std::atomic<int>  sampleVersion { 0 };
    juce::MemoryBlock sampleBytes;          // original file, stored in the session
    juce::String sampleName;
    juce::Array<juce::var> peaks;

    std::atomic<int> uiTrigger { 0 };

    std::atomic<float>* pDecay; std::atomic<float>* pPitch; std::atomic<float>* pXs; std::atomic<float>* pYs;
    std::atomic<float>* pDrive; std::atomic<float>* pLevel; std::atomic<float>* pMotion; std::atomic<float>* pPattern;
    std::atomic<float>* pDepth; std::atomic<float>* pRate; std::atomic<float>* pClock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYSamplerProcessor)
};
