#include "PluginProcessor.h"
#include "PluginEditor.h"

const juce::StringArray XYSamplerProcessor::patternNames { "Circle", "Figure-8", "Decay only", "Pitch only", "Corners", "Drift" };
const juce::StringArray XYSamplerProcessor::rateNames    { "1 bar", "1/2", "1/4", "1/8" };
const juce::StringArray XYSamplerProcessor::clockNames   { "Time", "Step" };

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout XYSamplerProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    using A = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<F> (juce::ParameterID { "decay", 1 }, "Decay", R (0.0f, 1.0f, 0.0001f), 0.55f));
    l.add (std::make_unique<F> (juce::ParameterID { "pitch", 1 }, "Pitch", R (0.0f, 1.0f, 0.0001f), 0.5f));
    l.add (std::make_unique<F> (juce::ParameterID { "xsens", 1 }, "Decay Sensitivity", R (10.0f, 100.0f, 1.0f), 100.0f, A().withLabel ("%")));
    l.add (std::make_unique<F> (juce::ParameterID { "ysens", 1 }, "Pitch Range", R (2.0f, 24.0f, 1.0f), 12.0f, A().withLabel ("st")));
    l.add (std::make_unique<F> (juce::ParameterID { "drive", 1 }, "Drive", R (0.0f, 1.0f, 0.0001f), 0.25f,
                                A().withStringFromValueFunction ([] (float v, int) { return juce::String (hit::driveDbFor (v), 1) + " dB"; })));
    l.add (std::make_unique<F> (juce::ParameterID { "level", 1 }, "Level", R (-36.0f, 6.0f, 0.01f, 2.0f), 0.0f, A().withLabel ("dB")));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "motion", 1 }, "Motion", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "pattern", 1 }, "Motion Pattern", patternNames, 0));
    l.add (std::make_unique<F> (juce::ParameterID { "depth", 1 }, "Motion Depth", R (0.0f, 50.0f, 1.0f), 18.0f, A().withLabel ("%")));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "rate", 1 }, "Motion Rate", rateNames, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "clock", 1 }, "Motion Clock", clockNames, 0));
    return l;
}

XYSamplerProcessor::XYSamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    formatManager.registerBasicFormats();
    pDecay = apvts.getRawParameterValue ("decay");   pPitch = apvts.getRawParameterValue ("pitch");
    pXs    = apvts.getRawParameterValue ("xsens");   pYs    = apvts.getRawParameterValue ("ysens");
    pDrive = apvts.getRawParameterValue ("drive");   pLevel = apvts.getRawParameterValue ("level");
    pMotion = apvts.getRawParameterValue ("motion"); pPattern = apvts.getRawParameterValue ("pattern");
    pDepth = apvts.getRawParameterValue ("depth");   pRate  = apvts.getRawParameterValue ("rate");
    pClock = apvts.getRawParameterValue ("clock");
    lastDirectory = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
}

hit::Params XYSamplerProcessor::readParams() const
{
    hit::Params p;
    p.x = *pDecay;  p.y = *pPitch;
    p.xSens = *pXs / 100.0;  p.ySens = *pYs;
    p.sat = *pDrive; p.levelDb = *pLevel;
    p.motion = *pMotion > 0.5f;
    p.pattern = juce::jlimit (0, (int) hit::kNumPatterns - 1, (int) std::lround (pPattern->load()));
    p.depth = *pDepth / 100.0;
    p.cycleBeats = rateBeats[juce::jlimit (0, 3, (int) std::lround (pRate->load()))];
    p.stepClock = std::lround (pClock->load()) == 1;
    return p;
}

bool XYSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void XYSamplerProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    engine.setParams (readParams());
    engine.snapParams();
}

void XYSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (samplePending.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (sampleLock);
        if (lock.isLocked()) { engine.setSample (pendingSample.get()); samplePending.store (false); }
    }

    engine.setParams (readParams());

    bool playing = false; double ppq = -1.0, bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    engine.setTransport (playing, playing ? ppq : -1.0, bpm);

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    if (const int t = uiTrigger.exchange (0); t > 0)
        engine.noteOn ((float) (t - 1) / 127.0f);

    int pos = 0;
    const int total = buffer.getNumSamples();
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int at = juce::jlimit (pos, total, meta.samplePosition);
        if (at > pos) { engine.render (left + pos, right + pos, at - pos); pos = at; }
        if (msg.isNoteOn())                                  engine.noteOn (msg.getFloatVelocity());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.killAll();
    }
    if (pos < total) engine.render (left + pos, right + pos, total - pos);

    double lx, ly;
    engine.currentPoint (lx, ly);
    liveX.store ((float) lx); liveY.store ((float) ly);
    const auto& h = engine.lastHit();
    lastX.store ((float) h.padX); lastY.store ((float) h.padY);
    lastT60.store ((float) (std::isfinite (h.t60) ? std::min (h.t60, 999.0) : 999.0)); lastSt.store ((float) h.semitones);
    hitSerial.store ((int) engine.hitCount());
}

//==============================================================================
bool XYSamplerProcessor::loadSampleFile (const juce::File& file)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) return false;
    lastDirectory = file.getParentDirectory();
    return loadSampleBytes (file.getFileName(), mb);
}

bool XYSamplerProcessor::loadSampleBytes (const juce::String& name, const juce::MemoryBlock& bytes)
{
    if (bytes.getSize() < 16 || bytes.getSize() > 64 * 1024 * 1024) return false;

    auto stored = std::make_unique<juce::MemoryBlock> (bytes);
    std::unique_ptr<juce::AudioFormatReader> reader (
        formatManager.createReaderFor (std::make_unique<juce::MemoryInputStream> (*stored, false)));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->numChannels < 1) return false;

    const int n = (int) juce::jmin ((juce::int64) (reader->sampleRate * 30.0), reader->lengthInSamples);
    juce::AudioBuffer<float> buf ((int) juce::jmin ((unsigned int) 2, reader->numChannels), n);
    reader->read (&buf, 0, n, 0, true, true);

    auto data = std::make_shared<hit::SampleData>();
    data->sampleRate = reader->sampleRate;
    data->name = juce::File::createFileWithoutCheckingPath (name).getFileNameWithoutExtension().toStdString();
    data->left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + n);
    const int rc = buf.getNumChannels() > 1 ? 1 : 0;
    data->right.assign (buf.getReadPointer (rc), buf.getReadPointer (rc) + n);

    // display peaks
    juce::Array<juce::var> pk;
    const int cols = 120;
    float mx = 0.0f;
    std::vector<float> raw ((size_t) cols, 0.0f);
    for (int c = 0; c < cols; ++c)
    {
        const int a = (int) ((juce::int64) n * c / cols), b = juce::jmax (a + 1, (int) ((juce::int64) n * (c + 1) / cols));
        for (int i = a; i < b && i < n; ++i)
            raw[(size_t) c] = juce::jmax (raw[(size_t) c], std::abs (data->left[(size_t) i]), std::abs (data->right[(size_t) i]));
        mx = juce::jmax (mx, raw[(size_t) c]);
    }
    for (auto v : raw) pk.add (mx > 0.0f ? v / mx : 0.0f);

    previousSample = currentSample;
    currentSample  = data;
    sampleBytes    = *stored;
    sampleName     = juce::String (data->name);
    peaks          = pk;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        pendingSample = data;
        samplePending.store (true);
    }
    sampleVersion.fetch_add (1);
    return true;
}

juce::var XYSamplerProcessor::getSampleInfo() const
{
    if (currentSample == nullptr) return {};
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", sampleName);
    o->setProperty ("dur", currentSample->durationSeconds());
    o->setProperty ("peaks", peaks);
    return juce::var (o);
}

//==============================================================================
void XYSamplerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("uiTheme", uiTheme, nullptr);
    state.setProperty ("uiScale", uiScale, nullptr);
    state.setProperty ("sampleName", sampleName, nullptr);
    if (sampleBytes.getSize() > 0) state.setProperty ("sampleData", juce::var (sampleBytes), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void XYSamplerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            uiTheme = tree.getProperty ("uiTheme", "dark").toString();
            if (uiTheme == "color") uiTheme = "dark";   // Tropic was removed in 0.2.4
            uiScale = (double) tree.getProperty ("uiScale", 1.0);
            if (auto* mb = tree.getProperty ("sampleData").getBinaryData())
                loadSampleBytes (tree.getProperty ("sampleName").toString(), *mb);
            tree.removeProperty ("sampleData", nullptr);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* XYSamplerProcessor::createEditor() { return new XYSamplerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new XYSamplerProcessor(); }
