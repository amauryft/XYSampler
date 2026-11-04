#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout HitPadProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<P> (juce::ParameterID { "decay", 1 }, "Decay",      R (0.0f, 1.0f, 0.0001f), 0.55f));
    layout.add (std::make_unique<P> (juce::ParameterID { "sat",   1 }, "Saturation", R (0.0f, 1.0f, 0.0001f), 0.25f));
    layout.add (std::make_unique<P> (juce::ParameterID { "level", 1 }, "Level",      R (-36.0f, 6.0f, 0.01f, 2.0f), 0.0f,
                                     juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

HitPadProcessor::HitPadProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    formatManager.registerBasicFormats();
    decayParam = apvts.getRawParameterValue ("decay");
    satParam   = apvts.getRawParameterValue ("sat");
    levelParam = apvts.getRawParameterValue ("level");
}

//==============================================================================
bool HitPadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void HitPadProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    engine.setParams (*decayParam, *satParam, *levelParam);
    engine.snapParams();
}

void HitPadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // New sample waiting? (try-lock: never block the audio thread)
    if (samplePending.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (sampleLock);
        if (lock.isLocked())
        {
            engine.setSample (pendingSample.get());
            samplePending.store (false);
        }
    }

    engine.setParams (*decayParam, *satParam, *levelParam);

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    // Hit from the on-screen pad / HIT button
    if (const int t = uiTrigger.exchange (0); t > 0)
    {
        engine.noteOn ((float) (t - 1) / 127.0f);
        hitCount.fetch_add (1);
    }

    // Render sample-accurately between MIDI events. Any note triggers the one-shot;
    // note-offs are ignored (drum-machine behaviour).
    int pos = 0;
    const int total = buffer.getNumSamples();
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int at = juce::jlimit (pos, total, meta.samplePosition);

        if (at > pos)
        {
            engine.render (left + pos, right + pos, at - pos);
            pos = at;
        }

        if (msg.isNoteOn())
        {
            engine.noteOn (msg.getFloatVelocity());
            hitCount.fetch_add (1);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            engine.killAll();
        }
    }

    if (pos < total)
        engine.render (left + pos, right + pos, total - pos);
}

//==============================================================================
bool HitPadProcessor::loadSample (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->numChannels < 1)
        return false;

    const int maxFrames = (int) (reader->sampleRate * 30.0); // one-shots only: cap at 30 s
    const int n = (int) juce::jmin ((juce::int64) maxFrames, reader->lengthInSamples);

    juce::AudioBuffer<float> buf ((int) juce::jmin ((unsigned int) 2, reader->numChannels), n);
    reader->read (&buf, 0, n, 0, true, true);

    auto data = std::make_shared<hit::SampleData>();
    data->sampleRate = reader->sampleRate;
    data->name = file.getFileNameWithoutExtension().toStdString();
    data->left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + n);
    const int rc = buf.getNumChannels() > 1 ? 1 : 0; // mono -> both sides
    data->right.assign (buf.getReadPointer (rc), buf.getReadPointer (rc) + n);

    previousSample = currentSample;
    currentSample  = data;
    sampleFile     = file;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        pendingSample = data;
        samplePending.store (true);
    }
    sampleVersion.fetch_add (1);
    return true;
}

//==============================================================================
void HitPadProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("samplePath", sampleFile.getFullPathName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void HitPadProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);

            const juce::File f (tree.getProperty ("samplePath").toString());
            if (f.existsAsFile())
                loadSample (f);
        }
}

juce::AudioProcessorEditor* HitPadProcessor::createEditor() { return new HitPadEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HitPadProcessor(); }
