#include "PluginEditor.h"

namespace
{
const juce::Colour kBg      { 0xfff6f4ef };
const juce::Colour kInk     { 0xff1b1b1b };
const juce::Colour kMuted   { 0xff9a948a };
const juce::Colour kOrange  { 0xfff26a1b };
const juce::Colour kHairline{ 0xffe3dfd8 };

juce::Font bold (float h) { return juce::Font (juce::FontOptions (h, juce::Font::bold)); }

juce::String decayText (double x)
{
    const double t = hit::effectiveT60 (x);
    if (t > 20.0) return "OPEN";
    return t < 1.0 ? juce::String (juce::roundToInt (t * 1000.0)) + " ms" : juce::String (t, 2) + " s";
}

juce::String driveText (double y)
{
    const double db = hit::driveDbFor (y);
    return (db >= 0 ? "+" : "") + juce::String (db, 1) + " dB";
}
} // namespace

//==============================================================================
void WaveStrip::setSample (std::shared_ptr<const hit::SampleData> s)
{
    sample = std::move (s);
    peaks.clear();

    if (sample != nullptr && sample->numFrames() > 0)
    {
        const int cols = 220;
        peaks.assign ((size_t) cols, 0.0f);
        const int n = sample->numFrames();
        for (int c = 0; c < cols; ++c)
        {
            const int a = (int) ((int64_t) n * c / cols), b = juce::jmax (a + 1, (int) ((int64_t) n * (c + 1) / cols));
            float pk = 0.0f;
            for (int i = a; i < b && i < n; ++i)
                pk = juce::jmax (pk, std::abs (sample->left[(size_t) i]), std::abs (sample->right[(size_t) i]));
            peaks[(size_t) c] = pk;
        }
        const float mx = *std::max_element (peaks.begin(), peaks.end());
        if (mx > 0.0f) for (auto& p : peaks) p /= mx;   // display-normalised
    }
    repaint();
}

void WaveStrip::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (kHairline);
    g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.0f);

    if (sample == nullptr || peaks.empty())
    {
        g.setColour (kMuted);
        g.setFont (bold (12.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("DROP A SAMPLE HERE  \xc2\xb7  OR CLICK TO LOAD")), getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto area = b.reduced (14.0f, 20.0f);
    const float cols = (float) peaks.size();
    const float colW = area.getWidth() / cols;
    const float mid  = area.getCentreY();
    const double dur = sample->durationSeconds();

    for (int pass = 0; pass < 2; ++pass)
    {
        g.setColour (pass == 0 ? juce::Colour (0xffd9d4ca) : kOrange);
        for (int c = 0; c < (int) peaks.size(); ++c)
        {
            float h = peaks[(size_t) c] * area.getHeight() * 0.5f;
            if (pass == 1)
                h *= (float) hit::envelopeAt (((double) c + 0.5) / cols * dur, decayX);
            h = juce::jmax (h, 0.6f);
            g.fillRect (area.getX() + c * colW + 0.4f, mid - h, juce::jmax (1.0f, colW - 0.8f), h * 2.0f);
        }
    }

    g.setColour (kMuted);
    g.setFont (bold (10.5f));
    g.drawText (juce::String (sample->name).toUpperCase(), b.withHeight (20.0f).withTrimmedLeft (14.0f).withTrimmedRight (90.0f),
                juce::Justification::centredLeft, true);
    g.drawText (juce::String (dur, 2) + " s", b.withHeight (20.0f).withTrimmedRight (14.0f), juce::Justification::centredRight);
}

//==============================================================================
HitPadEditor::HitPadEditor (HitPadProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      pad (*p.apvts.getParameter ("decay"), *p.apvts.getParameter ("sat")),
      levelAttach (*p.apvts.getParameter ("level"), levelSlider)
{
    setSize (460, 700);

    addAndMakeVisible (wave);
    wave.onClick = [this] { chooseFile(); };

    addAndMakeVisible (pad);
    pad.onHit = [this] { proc.triggerFromUI(); };

    hitButton.setColour (juce::TextButton::buttonColourId, kInk);
    hitButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    hitButton.onClick = [this] { proc.triggerFromUI(); };
    addAndMakeVisible (hitButton);

    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    levelSlider.setColour (juce::Slider::trackColourId, kOrange);
    levelSlider.setColour (juce::Slider::backgroundColourId, kHairline);
    levelSlider.setColour (juce::Slider::thumbColourId, kInk);
    levelSlider.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (levelSlider);

    refreshSample();
    startTimerHz (30);
}

HitPadEditor::~HitPadEditor() { stopTimer(); }

void HitPadEditor::refreshSample()
{
    lastSampleVersion = proc.getSampleVersion();
    wave.setSample (proc.getSample());
}

void HitPadEditor::timerCallback()
{
    if (proc.getSampleVersion() != lastSampleVersion)
        refreshSample();

    if (const int h = proc.getHitCount(); h != lastHitCount)
    {
        lastHitCount = h;
        pad.flash();
    }

    const float d = proc.apvts.getRawParameterValue ("decay")->load();
    const float s = proc.apvts.getRawParameterValue ("sat")->load();
    wave.setDecay (d);
    if (std::abs (d - lastDecay) > 1.0e-6f || std::abs (s - lastSat) > 1.0e-6f)
    {
        lastDecay = d;
        lastSat = s;
        repaint (getLocalBounds().removeFromBottom (110));
    }
}

//==============================================================================
void HitPadEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // wordmark
    g.setFont (bold (26.0f));
    const juce::String hitStr ("HIT"), padStr ("PAD");
    juce::GlyphArrangement ga;
    ga.addLineOfText (bold (26.0f), hitStr, 0.0f, 0.0f);
    const float wHit = ga.getBoundingBox (0, -1, true).getWidth();
    g.setColour (kOrange);
    g.drawText (hitStr, juce::Rectangle<float> (24.0f, 16.0f, wHit + 4.0f, 34.0f), juce::Justification::centredLeft);
    g.setColour (kInk);
    g.drawText (padStr, juce::Rectangle<float> (24.0f + wHit, 16.0f, 100.0f, 34.0f), juce::Justification::centredLeft);

    g.setColour (kMuted);
    g.setFont (bold (10.5f));
    g.drawText ("ONE-SHOT SAMPLE PLAYER", getLocalBounds().removeFromTop (50).withTrimmedRight (24).toFloat(),
                juce::Justification::centredRight);

    // readouts
    const double x = proc.apvts.getRawParameterValue ("decay")->load();
    const double y = proc.apvts.getRawParameterValue ("sat")->load();
    const auto foot = juce::Rectangle<float> (24.0f, (float) getHeight() - 98.0f, (float) getWidth() - 48.0f, 74.0f);

    auto readout = [&] (juce::Rectangle<float> r, const juce::String& label, const juce::String& value)
    {
        g.setColour (kMuted);
        g.setFont (bold (10.5f));
        g.drawText (label, r.removeFromTop (16.0f), juce::Justification::centredLeft);
        g.setColour (kInk);
        g.setFont (bold (22.0f));
        g.drawText (value, r, juce::Justification::centredLeft);
    };
    readout (foot.withWidth (130.0f), "DECAY  (T60)", decayText (x));
    readout (foot.withX (foot.getX() + 140.0f).withWidth (140.0f), "SATURATION  (DRIVE)", driveText (y));

    g.setColour (kMuted);
    g.setFont (bold (10.5f));
    g.drawText ("LEVEL", juce::Rectangle<float> (foot.getRight() - 132.0f, foot.getY(), 132.0f, 16.0f),
                juce::Justification::centredLeft);
}

void HitPadEditor::resized()
{
    auto r = getLocalBounds().reduced (24, 0);
    r.removeFromTop (62);
    wave.setBounds (r.removeFromTop (100));
    r.removeFromTop (14);

    auto foot = getLocalBounds().reduced (24, 0).removeFromBottom (98).withTrimmedBottom (24);
    pad.setBounds (r.withBottom (getHeight() - 112));

    auto right = foot.removeFromRight (132);
    levelSlider.setBounds (right.removeFromTop (46).withTrimmedTop (16).withHeight (24));
    hitButton.setBounds (right.withTrimmedTop (4).withHeight (28));
}

//==============================================================================
void HitPadEditor::chooseFile()
{
    const auto start = proc.getSampleFile().existsAsFile() ? proc.getSampleFile().getParentDirectory()
                                                          : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", start, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile() && proc.loadSample (f))
                                  proc.triggerFromUI();   // audition on load
                          });
}

bool HitPadEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a")) return true;
    return false;
}

void HitPadEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (proc.loadSample (juce::File (f)))
        {
            proc.triggerFromUI();
            break;
        }
}
