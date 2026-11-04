#pragma once

#include "PluginProcessor.h"
#include "XYPad.h"

//==============================================================================
/** Waveform of the loaded sample (grey) with the current decay envelope applied (orange).
    Click to load a file; you can also drop one anywhere on the plugin window. */
class WaveStrip : public juce::Component
{
public:
    std::function<void()> onClick;

    void setSample (std::shared_ptr<const hit::SampleData> s);
    void setDecay (float x) { if (std::abs (x - decayX) > 1.0e-6f) { decayX = x; repaint(); } }

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }

private:
    std::shared_ptr<const hit::SampleData> sample;
    std::vector<float> peaks;   // per-column peak (0..1)
    float decayX = 0.55f;
};

//==============================================================================
class HitPadEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
{
public:
    explicit HitPadEditor (HitPadProcessor&);
    ~HitPadEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void chooseFile();
    void refreshSample();

    HitPadProcessor& proc;

    WaveStrip wave;
    XYPad pad;
    juce::TextButton hitButton { "HIT" };
    juce::Slider levelSlider;
    juce::SliderParameterAttachment levelAttach;

    std::unique_ptr<juce::FileChooser> chooser;
    int lastSampleVersion = -1, lastHitCount = 0;
    float lastDecay = -1.0f, lastSat = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitPadEditor)
};
