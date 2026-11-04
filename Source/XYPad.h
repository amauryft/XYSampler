#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Two-parameter pad. X = horizontal (decay), Y = vertical (saturation, up = more).
    - click / drag: set both, and trigger the sample on mouse-down (drum-pad feel)
    - shift-drag:   fine control (10x slower)
    - double-click: reset both to defaults */
class XYPad : public juce::Component, private juce::Timer
{
public:
    XYPad (juce::RangedAudioParameter& xParam, juce::RangedAudioParameter& yParam)
        : px (xParam), py (yParam),
          ax (xParam, [this] (float v) { vx = v; repaint(); }, nullptr),
          ay (yParam, [this] (float v) { vy = v; repaint(); }, nullptr)
    {
        ax.sendInitialUpdate();
        ay.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }

    std::function<void()> onHit;

    /** Visual "pad was hit" flash. */
    void flash() { flashAmount = 1.0f; startTimerHz (60); }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        const auto outer = getLocalBounds().toFloat();
        const auto area  = playArea();

        // body
        g.setColour (juce::Colour (0xffffffff));
        g.fillRoundedRectangle (outer, 14.0f);
        g.setColour (juce::Colour (0xffe3dfd8));
        g.drawRoundedRectangle (outer.reduced (0.5f), 14.0f, 1.0f);

        // grid
        g.setColour (juce::Colour (0xfff0ede7));
        for (int i = 1; i < 8; ++i)
        {
            const float fx = area.getX() + area.getWidth()  * (float) i / 8.0f;
            const float fy = area.getY() + area.getHeight() * (float) i / 8.0f;
            g.drawLine (fx, area.getY(), fx, area.getBottom(), i % 2 == 0 ? 1.0f : 0.6f);
            g.drawLine (area.getX(), fy, area.getRight(), fy, i % 2 == 0 ? 1.0f : 0.6f);
        }

        // axis labels
        g.setColour (juce::Colour (0xff9a948a));
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText ("SHORT", area.withHeight (14.0f).withY (area.getBottom() + 4.0f).withWidth (60.0f),
                    juce::Justification::centredLeft);
        g.drawText (juce::String (juce::CharPointer_UTF8 ("DECAY  \xe2\x86\x92")), juce::Rectangle<float> (area.getX(), area.getBottom() + 4.0f, area.getWidth(), 14.0f),
                    juce::Justification::centred);
        g.drawText ("LONG", juce::Rectangle<float> (area.getRight() - 60.0f, area.getBottom() + 4.0f, 60.0f, 14.0f),
                    juce::Justification::centredRight);
        {
            juce::Graphics::ScopedSaveState s (g);
            const float cx = 12.0f, cy = area.getCentreY();
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, cx, cy));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("SATURATION  \xe2\x86\x92")), juce::Rectangle<float> (cx - 90.0f, cy - 7.0f, 180.0f, 14.0f),
                        juce::Justification::centred);
        }

        // crosshair + puck
        const auto p = puckPosition();
        const auto orange = juce::Colour (0xfff26a1b);

        g.setColour (orange.withAlpha (0.35f));
        g.drawLine (area.getX(), p.y, area.getRight(), p.y, 1.0f);
        g.drawLine (p.x, area.getY(), p.x, area.getBottom(), 1.0f);

        if (flashAmount > 0.0f)
        {
            const float r = 13.0f + (1.0f - flashAmount) * 26.0f;
            g.setColour (orange.withAlpha (flashAmount * 0.55f));
            g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, 2.0f);
        }

        g.setColour (juce::Colour (0x22000000));
        g.fillEllipse (p.x - 12.0f, p.y - 10.0f, 24.0f, 24.0f); // soft drop shadow
        g.setColour (orange);
        g.fillEllipse (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f);
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        lastPos = e.position;
        ax.beginGesture();
        ay.beginGesture();
        if (! e.mods.isShiftDown()) setFromPosition (e.position);
        if (onHit) onHit();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isShiftDown())
        {
            const auto area = playArea();
            const auto d = (e.position - lastPos) * 0.1f;
            setNormalised (vx + d.x / area.getWidth(), vy - d.y / area.getHeight());
        }
        else
        {
            setFromPosition (e.position);
        }
        lastPos = e.position;
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        ax.endGesture();
        ay.endGesture();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        ax.setValueAsCompleteGesture (px.convertFrom0to1 (px.getDefaultValue()));
        ay.setValueAsCompleteGesture (py.convertFrom0to1 (py.getDefaultValue()));
    }

private:
    juce::Rectangle<float> playArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (28.0f).withTrimmedBottom (26.0f)
                               .withTrimmedTop (16.0f).withTrimmedRight (16.0f);
    }

    juce::Point<float> puckPosition() const
    {
        const auto a = playArea();
        return { a.getX() + a.getWidth() * vx, a.getBottom() - a.getHeight() * vy };
    }

    void setFromPosition (juce::Point<float> pos)
    {
        const auto a = playArea();
        setNormalised ((pos.x - a.getX()) / a.getWidth(), (a.getBottom() - pos.y) / a.getHeight());
    }

    void setNormalised (float nx, float ny)
    {
        nx = juce::jlimit (0.0f, 1.0f, nx);
        ny = juce::jlimit (0.0f, 1.0f, ny);
        ax.setValueAsPartOfGesture (px.convertFrom0to1 (nx));
        ay.setValueAsPartOfGesture (py.convertFrom0to1 (ny));
    }

    void timerCallback() override
    {
        flashAmount -= 0.07f;
        if (flashAmount <= 0.0f) { flashAmount = 0.0f; stopTimer(); }
        repaint();
    }

    juce::RangedAudioParameter& px;
    juce::RangedAudioParameter& py;
    juce::ParameterAttachment ax, ay;
    float vx = 0.5f, vy = 0.25f;   // normalised 0..1 (both params use a plain 0..1 range)
    float flashAmount = 0.0f;
    juce::Point<float> lastPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYPad)
};
