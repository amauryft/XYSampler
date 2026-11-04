#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "HitCore.h"

class HitPadProcessor : public juce::AudioProcessor
{
public:
    HitPadProcessor();
    ~HitPadProcessor() override = default;

    //==========================================================================
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
    // Message-thread API used by the editor
    bool loadSample (const juce::File& file);
    juce::File getSampleFile() const { return sampleFile; }
    std::shared_ptr<const hit::SampleData> getSample() const { return currentSample; }
    int  getSampleVersion() const { return sampleVersion.load(); }
    int  getHitCount() const { return hitCount.load(); }
    void triggerFromUI (float velocity = 1.0f) { uiTrigger.store ((int) (juce::jlimit (0.0f, 1.0f, velocity) * 127.0f) + 1); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

private:
    hit::Engine engine;
    juce::AudioFormatManager formatManager;

    // Sample hand-off: message thread -> audio thread.
    // `currentSample`/`previousSample` keep the data alive; the engine holds a raw pointer.
    // Two generations are retained so the audio thread never reads freed memory.
    std::shared_ptr<const hit::SampleData> currentSample, previousSample, pendingSample;
    juce::SpinLock sampleLock;
    std::atomic<bool> samplePending { false };
    std::atomic<int>  sampleVersion { 0 };
    juce::File sampleFile;

    std::atomic<int> uiTrigger { 0 };   // 0 = none, else velocity*127 + 1
    std::atomic<int> hitCount { 0 };

    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* satParam   = nullptr;
    std::atomic<float>* levelParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitPadProcessor)
};
