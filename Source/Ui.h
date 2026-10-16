// Ui.h — estética y controles básicos de la interfaz, enlazados a Params y a las acciones.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Controller.h"

namespace ct
{

namespace Colours
{
    const juce::Colour bg       { 0xff121315 };
    const juce::Colour panel    { 0xff1b1c20 };
    const juce::Colour panelHi  { 0xff26282d };
    const juce::Colour line     { 0xff34363c };
    const juce::Colour text     { 0xffeae6dd };
    const juce::Colour textDim  { 0xff8e8b84 };
    const juce::Colour amber    { 0xffe8a33d };
    const juce::Colour amberDim { 0xff6e4f22 };
    const juce::Colour teal     { 0xff4fb3a5 };
    const juce::Colour red      { 0xffd9534f };
    const juce::Colour ice      { 0xff8fc3e8 };
    const juce::Colour green    { 0xff7cc26a };
    const juce::Colour deckA    { 0xffe8a33d };
    const juce::Colour deckB    { 0xff4fb3a5 };
}

class CtLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CtLookAndFeel()
    {
        using namespace juce;
        auto scheme = getCurrentColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground, Colours::bg);
        scheme.setUIColour (ColourScheme::widgetBackground, Colours::panelHi);
        scheme.setUIColour (ColourScheme::menuBackground, Colours::panel);
        scheme.setUIColour (ColourScheme::outline, Colours::line);
        scheme.setUIColour (ColourScheme::defaultText, Colours::text);
        scheme.setUIColour (ColourScheme::defaultFill, Colours::amberDim);
        scheme.setUIColour (ColourScheme::highlightedText, Colours::bg);
        scheme.setUIColour (ColourScheme::highlightedFill, Colours::amber);
        scheme.setUIColour (ColourScheme::menuText, Colours::text);
        setColourScheme (scheme);

        setColour (ResizableWindow::backgroundColourId, Colours::bg);
        setColour (Label::textColourId, Colours::text);
        setColour (Label::outlineColourId, Colours::bg.withAlpha (0.0f));
        setColour (TextButton::buttonColourId, Colours::panelHi);
        setColour (TextButton::buttonOnColourId, Colours::amber);
        setColour (TextButton::textColourOffId, Colours::text);
        setColour (TextButton::textColourOnId, Colours::bg);
        setColour (Slider::rotarySliderFillColourId, Colours::amber);
        setColour (Slider::rotarySliderOutlineColourId, Colours::line);
        setColour (Slider::trackColourId, Colours::amber);
        setColour (Slider::backgroundColourId, Colours::panelHi);
        setColour (Slider::thumbColourId, Colours::text);
        setColour (Slider::textBoxOutlineColourId, Colours::bg.withAlpha (0.0f));
        setColour (ListBox::backgroundColourId, Colours::panel);
        setColour (ListBox::outlineColourId, Colours::line);
        setColour (TextEditor::backgroundColourId, Colours::panelHi);
        setColour (TextEditor::outlineColourId, Colours::line);
        setColour (TextEditor::focusedOutlineColourId, Colours::amber);
        setColour (TextEditor::textColourId, Colours::text);
        setColour (ComboBox::backgroundColourId, Colours::panelHi);
        setColour (ComboBox::outlineColourId, Colours::line);
        setColour (ComboBox::textColourId, Colours::text);
        setColour (PopupMenu::backgroundColourId, Colours::panel);
        setColour (PopupMenu::highlightedBackgroundColourId, Colours::amberDim);
        setColour (ToggleButton::textColourId, Colours::text);
        setColour (ToggleButton::tickColourId, Colours::amber);
        setColour (ScrollBar::thumbColourId, Colours::line);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace juce;
        const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float r = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float stroke = jmax (2.5f, r * 0.14f);
        const auto col = s.findColour (Slider::rotarySliderFillColourId);
        const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);

        Path track; track.addCentredArc (c.x, c.y, r - stroke * 0.5f, r - stroke * 0.5f, 0, startAngle, endAngle, true);
        g.setColour (Colours::line);
        g.strokePath (track, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));

        const float angle = startAngle + pos * (endAngle - startAngle);
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        Path value; value.addCentredArc (c.x, c.y, r - stroke * 0.5f, r - stroke * 0.5f, 0, jmin (from, angle), jmax (from, angle), true);
        g.setColour (col);
        g.strokePath (value, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));

        const float kr = r - stroke * 1.8f;
        g.setColour (Colours::panelHi);
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
        g.setColour (Colours::text);
        g.drawLine ({ c.getPointOnCircumference (kr * 0.2f, angle), c.getPointOnCircumference (kr * 0.85f, angle) }, 1.8f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        using namespace juce;
        const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const bool vertical = style == Slider::LinearVertical;
        const auto col = s.findColour (Slider::trackColourId);
        if (vertical)
        {
            const auto trk = b.withSizeKeepingCentre (4.0f, b.getHeight());
            g.setColour (Colours::panelHi); g.fillRoundedRectangle (trk, 2.0f);
            g.setColour (col.withAlpha (0.8f));
            g.fillRoundedRectangle (trk.withTop (sliderPos), 2.0f);
            const auto thumb = Rectangle<float> (b.getCentreX() - jmin (14.0f, b.getWidth() * 0.45f), sliderPos - 5.0f,
                                                 jmin (28.0f, b.getWidth() * 0.9f), 10.0f);
            g.setColour (Colours::text); g.fillRoundedRectangle (thumb, 2.0f);
            g.setColour (Colours::bg); g.drawHorizontalLine ((int) sliderPos, thumb.getX() + 2, thumb.getRight() - 2);
        }
        else
        {
            const auto trk = b.withSizeKeepingCentre (b.getWidth(), 4.0f);
            g.setColour (Colours::panelHi); g.fillRoundedRectangle (trk, 2.0f);
            const auto thumb = Rectangle<float> (sliderPos - 6.0f, b.getCentreY() - jmin (11.0f, b.getHeight() * 0.45f),
                                                 12.0f, jmin (22.0f, b.getHeight() * 0.9f));
            g.setColour (Colours::text); g.fillRoundedRectangle (thumb, 2.0f);
            g.setColour (Colours::bg); g.drawVerticalLine ((int) sliderPos, thumb.getY() + 2, thumb.getBottom() - 2);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgColour, bool hi, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        auto col = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bgColour;
        if (down) col = col.brighter (0.15f); else if (hi) col = col.brighter (0.06f);
        g.setColour (col);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (b.getToggleState() ? col.brighter (0.3f) : Colours::line);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) h * 0.42f), juce::Font::bold));
    }
};

// ---------------------------------------------------------------------------
// Algo que se refresca desde los parámetros (lo llama el timer de la ventana).
struct Refreshable { virtual ~Refreshable() = default; virtual void refresh() = 0; };

// Perilla enlazada a un parámetro: nombre arriba, valor abajo.
class ParamKnob : public juce::Component, public Refreshable
{
public:
    ParamKnob (Params& ps, P param, const juce::String& name, juce::Colour colour = Colours::amber)
        : params (ps), p (param)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange (0.0, 1.0);
        slider.setMouseDragSensitivity (140);
        slider.setWantsKeyboardFocus (false);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        const auto u = paramInfo (p).unit;
        slider.getProperties().set ("bipolar", u == Unit::Eq || u == Unit::Bi || (paramInfo (p).min < 0.0f && paramInfo (p).max > 0.0f && u != Unit::Db));
        slider.setDoubleClickReturnValue (true, params.range (p).convertTo0to1 (paramInfo (p).def));
        slider.onValueChange = [this] { if (! updating) params.setNorm (p, (float) slider.getValue()); };
        addAndMakeVisible (slider);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, Colours::textDim);
        label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        value.setJustificationType (juce::Justification::centred);
        value.setFont (juce::FontOptions (10.5f));
        value.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (value);
        refresh();
    }

    void refresh() override
    {
        const float n = params.getNorm (p);
        if (std::abs (n - (float) slider.getValue()) > 1.0e-4f)
        {
            updating = true;
            slider.setValue (n, juce::dontSendNotification);
            updating = false;
        }
        const auto txt = params.format (p);
        if (txt != value.getText()) value.setText (txt, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (13));
        value.setBounds (r.removeFromBottom (13));
        slider.setBounds (r);
    }

private:
    Params& params;
    P p;
    juce::Slider slider;
    juce::Label label, value;
    bool updating = false;
};

// Fader (vertical u horizontal) enlazado a un parámetro.
class ParamFader : public juce::Component, public Refreshable
{
public:
    ParamFader (Params& ps, P param, bool vertical, juce::Colour colour = Colours::amber)
        : params (ps), p (param)
    {
        slider.setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange (0.0, 1.0);
        slider.setWantsKeyboardFocus (false);
        slider.setColour (juce::Slider::trackColourId, colour);
        slider.setDoubleClickReturnValue (true, params.range (p).convertTo0to1 (paramInfo (p).def));
        slider.onValueChange = [this] { if (! updating) params.setNorm (p, (float) slider.getValue()); };
        addAndMakeVisible (slider);
        refresh();
    }
    void refresh() override
    {
        const float n = params.getNorm (p);
        if (std::abs (n - (float) slider.getValue()) > 1.0e-4f)
        {
            updating = true; slider.setValue (n, juce::dontSendNotification); updating = false;
        }
    }
    void resized() override { slider.setBounds (getLocalBounds()); }
private:
    Params& params;
    P p;
    juce::Slider slider;
    bool updating = false;
};

// Botón de encendido/apagado enlazado a un parámetro Toggle (o a un valor de un Choice).
class ParamToggle : public juce::TextButton, public Refreshable
{
public:
    ParamToggle (Params& ps, P param, const juce::String& buttonText, juce::Colour on = Colours::amber, int choiceValue = -1)
        : juce::TextButton (buttonText), params (ps), p (param), choice (choiceValue)
    {
        setColour (juce::TextButton::buttonOnColourId, on);
        setWantsKeyboardFocus (false);
        onClick = [this]
        {
            if (choice >= 0) params.set (p, (float) choice);
            else params.toggle (p);
            refresh();
        };
        refresh();
    }
    void refresh() override
    {
        const bool st = choice >= 0 ? params.choice (p) == choice : params.on (p);
        if (st != getToggleState()) setToggleState (st, juce::dontSendNotification);
    }
private:
    Params& params;
    P p;
    int choice;
};

// Botón de acción: "mantener" o disparo. Se ilumina según litWhen.
class ActionButton : public juce::TextButton, public Refreshable
{
public:
    ActionButton (Controller& c, A action, const juce::String& buttonText, juce::Colour on = Colours::amber)
        : juce::TextButton (buttonText), ctrl (c), a (action)
    {
        setColour (juce::TextButton::buttonOnColourId, on);
        setWantsKeyboardFocus (false);
    }
    std::function<bool()> litWhen;

    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseDown (e);
        if (e.mods.isPopupMenu()) return;
        ctrl.perform (a, Hold::Mouse, true);
        held = true;
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseUp (e);
        if (held) ctrl.perform (a, Hold::Mouse, false);
        held = false;
    }
    void refresh() override
    {
        const bool st = litWhen ? litWhen() : false;
        if (st != getToggleState()) setToggleState (st, juce::dontSendNotification);
    }
private:
    Controller& ctrl;
    A a;
    bool held = false;
};

// Medidor vertical.
class LevelMeter : public juce::Component
{
public:
    void setLevel (float linear)
    {
        const float db = juce::Decibels::gainToDecibels (linear, -60.0f);
        if (std::abs (db - level) > 0.3f) { level = db; repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::panelHi);
        g.fillRoundedRectangle (r, 2.0f);
        const float n = juce::jlimit (0.0f, 1.0f, (level + 48.0f) / 48.0f);
        auto fill = r.withTop (r.getBottom() - r.getHeight() * n);
        g.setColour (level > -1.0f ? Colours::red : level > -9.0f ? Colours::amber : Colours::green);
        g.fillRoundedRectangle (fill, 2.0f);
    }
private:
    float level = -60.0f;
};

// Panel con título.
class Panel : public juce::Component
{
public:
    explicit Panel (juce::String t, juce::Colour accent = Colours::amber) : title (std::move (t)), colour (accent) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (colour);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (title, r.reduced (10, 4).removeFromTop (16), juce::Justification::centredLeft);
        paintExtra (g);
    }
    virtual void paintExtra (juce::Graphics&) {}
    juce::Rectangle<int> content() const { return getLocalBounds().reduced (8, 4).withTrimmedTop (18); }
protected:
    juce::String title;
    juce::Colour colour;
};

} // namespace ct
