#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
std::vector<std::byte> toBytes (const char* data, int size)
{
    std::vector<std::byte> v ((size_t) size);
    std::memcpy (v.data(), data, (size_t) size);
    return v;
}

juce::String mimeFor (const juce::String& file)
{
    if (file.endsWithIgnoreCase (".html")) return "text/html; charset=utf-8";
    if (file.endsWithIgnoreCase (".js"))   return "text/javascript";
    if (file.endsWithIgnoreCase (".ttf"))  return "font/ttf";
    return "application/octet-stream";
}
} // namespace

juce::WebBrowserComponent::Options XYSamplerEditor::makeOptions()
{
    for (auto* p : proc.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr) continue;
        if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
        {
            toggleRelays.push_back (std::make_unique<juce::WebToggleButtonRelay> (rp->getParameterID()));
            toggleParams.push_back (rp);
        }
        else
        {
            sliderRelays.push_back (std::make_unique<juce::WebSliderRelay> (rp->getParameterID()));
            sliderParams.push_back (rp);
        }
    }

    auto opts = juce::WebBrowserComponent::Options{}
        .withBackend (juce::WebBrowserComponent::Options::Backend::defaultBackend)
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const auto& url) { return getResource (url); })
        .withNativeFunction ("hit", [this] (const juce::Array<juce::var>&, auto complete)
        {
            proc.triggerFromUI();
            complete ({});
        })
        .withNativeFunction ("chooseSample", [this] (const juce::Array<juce::var>&, auto complete)
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a sample", proc.lastDirectory,
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f.existsAsFile() && proc.loadSampleFile (f)) proc.triggerFromUI();
                                  });
            complete ({});
        })
        .withNativeFunction ("loadSampleData", [this] (const juce::Array<juce::var>& args, auto complete)
        {
            bool ok = false;
            if (args.size() >= 2)
            {
                juce::MemoryOutputStream decoded;
                if (juce::Base64::convertFromBase64 (decoded, args[1].toString()))
                    ok = proc.loadSampleBytes (args[0].toString(), decoded.getMemoryBlock());
                if (ok) proc.triggerFromUI();
            }
            complete (juce::var (ok));
        })
        .withNativeFunction ("getSample", [this] (const juce::Array<juce::var>&, auto complete)
        {
            complete (proc.getSampleInfo());
        })
        .withNativeFunction ("setTheme", [this] (const juce::Array<juce::var>& args, auto complete)
        {
            if (! args.isEmpty()) proc.uiTheme = args[0].toString();
            complete (juce::var (proc.uiTheme));
        })
        .withNativeFunction ("getTheme", [this] (const juce::Array<juce::var>&, auto complete)
        {
            complete (juce::var (proc.uiTheme));
        });

    for (auto& r : sliderRelays) opts = opts.withOptionsFrom (*r);
    for (auto& r : toggleRelays) opts = opts.withOptionsFrom (*r);
    return opts;
}

XYSamplerEditor::XYSamplerEditor (XYSamplerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), web (makeOptions())
{
    for (size_t i = 0; i < sliderRelays.size(); ++i)
        sliderAttachments.push_back (std::make_unique<juce::WebSliderParameterAttachment> (*sliderParams[i], *sliderRelays[i], nullptr));
    for (size_t i = 0; i < toggleRelays.size(); ++i)
        toggleAttachments.push_back (std::make_unique<juce::WebToggleButtonParameterAttachment> (*toggleParams[i], *toggleRelays[i], nullptr));

    const double s = juce::jlimit (0.5, 2.0, proc.uiScale);   // read before setResizeLimits() triggers resized()
    addAndMakeVisible (web);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (kW / 2, kH / 2, kW * 2, kH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / kH);
    setSize (juce::roundToInt (kW * s), juce::roundToInt (kH * s));
    startTimerHz (30);
}

XYSamplerEditor::~XYSamplerEditor() { stopTimer(); }

void XYSamplerEditor::resized()
{
    web.setBounds (getLocalBounds());
    proc.uiScale = getWidth() / (double) kW;
}

std::optional<juce::WebBrowserComponent::Resource> XYSamplerEditor::getResource (const juce::String& url)
{
    auto path = url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (path.isEmpty()) path = "index.html";
    const auto file = path.fromLastOccurrenceOf ("/", false, false);

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (file == BinaryData::originalFilenames[i])
        {
            int size = 0;
            if (auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                return juce::WebBrowserComponent::Resource { toBytes (data, size), mimeFor (file) };
        }
    return std::nullopt;
}

void XYSamplerEditor::timerCallback()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("lx", proc.liveX.load());
    o->setProperty ("ly", proc.liveY.load());
    o->setProperty ("serial", proc.hitSerial.load());
    o->setProperty ("hx", proc.lastX.load());
    o->setProperty ("hy", proc.lastY.load());
    o->setProperty ("t60", proc.lastT60.load());
    o->setProperty ("st", proc.lastSt.load());
    o->setProperty ("ver", proc.getSampleVersion());
    web.emitEventIfBrowserIsVisible ("live", juce::var (o));
}
