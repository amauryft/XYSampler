#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class XYSamplerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kW = 780, kH = 560;

    explicit XYSamplerEditor (XYSamplerProcessor&);
    ~XYSamplerEditor() override;
    void resized() override;

private:
    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);
    juce::WebBrowserComponent::Options makeOptions();

    XYSamplerProcessor& proc;
    std::vector<std::unique_ptr<juce::WebSliderRelay>> sliderRelays;
    std::vector<std::unique_ptr<juce::WebToggleButtonRelay>> toggleRelays;
    std::vector<juce::RangedAudioParameter*> sliderParams, toggleParams;
    juce::WebBrowserComponent web;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::WebToggleButtonParameterAttachment>> toggleAttachments;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYSamplerEditor)
};
