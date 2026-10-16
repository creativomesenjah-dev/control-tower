// Dialogs.h — ventanas de configuración: audio y salidas, y mapeo MIDI.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Ui.h"

namespace ct
{

class AudioSettingsPanel : public juce::Component, private juce::Timer
{
public:
    AudioSettingsPanel (juce::AudioDeviceManager& dm, Engine& e, Controller& c)
        : engine (e), ctrl (c), selector (dm, 0, 2, 2, 8, true, false, false, false)
    {
        addAndMakeVisible (selector);

        auto setupCombo = [this] (juce::ComboBox& cb, juce::Label& l, const juce::String& text, bool allowNone)
        {
            l.setText (text, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, Colours::text);
            addAndMakeVisible (l);
            if (allowNone) cb.addItem ("ninguna", 1);
            for (int p = 0; p < 4; ++p) cb.addItem ("salidas " + juce::String (p * 2 + 1) + "-" + juce::String (p * 2 + 2), p + 2);
            addAndMakeVisible (cb);
        };
        setupCombo (masterCombo, masterLabel, "Master (a la mesa)", false);
        setupCombo (phonesCombo, phonesLabel, "Auriculares (cue)", true);
        masterCombo.setSelectedId (engine.getMasterPair() + 2, juce::dontSendNotification);
        phonesCombo.setSelectedId (engine.getPhonesPair() < 0 ? 1 : engine.getPhonesPair() + 2, juce::dontSendNotification);
        masterCombo.onChange = phonesCombo.onChange = [this]
        {
            engine.setOutputPairs (masterCombo.getSelectedId() - 2, phonesCombo.getSelectedId() == 1 ? -1 : phonesCombo.getSelectedId() - 2);
        };

        flx4.setButtonText (u8 ("Usar el mapeo de fábrica de la DDJ-FLX4"));
        flx4.setToggleState (ctrl.isFlx4MappingOn(), juce::dontSendNotification);
        flx4.onClick = [this] { ctrl.setFlx4MappingOn (flx4.getToggleState()); };
        invert.setButtonText (u8 ("Invertir el fader de tempo de la FLX4"));
        invert.setToggleState (ctrl.isTempoInverted(), juce::dontSendNotification);
        invert.onClick = [this] { ctrl.setTempoInverted (invert.getToggleState()); };
        nano.setButtonText (u8 ("Usar el mapeo de fábrica del nanoPAD2 (4 escenas)"));
        nano.setToggleState (ctrl.isNanoMappingOn(), juce::dontSendNotification);
        nano.onClick = [this] { ctrl.setNanoMappingOn (nano.getToggleState()); };
        addAndMakeVisible (flx4); addAndMakeVisible (invert); addAndMakeVisible (nano);

        hint.setText (u8 ("Con la DDJ-FLX4: salidas 1-2 = MASTER OUT (a la mesa), salidas 3-4 = auriculares.\n"
                          "Activá la FLX4 (y cualquier otro controlador) en \"Active MIDI inputs\"."),
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Colours::textDim);
        hint.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (hint);
        setSize (560, 680);
        startTimer (1000);
    }

    void paint (juce::Graphics& g) override { g.fillAll (Colours::bg); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        hint.setBounds (r.removeFromBottom (40));
        nano.setBounds (r.removeFromBottom (24));
        invert.setBounds (r.removeFromBottom (24));
        flx4.setBounds (r.removeFromBottom (24));
        r.removeFromBottom (6);
        auto row2 = r.removeFromBottom (26), row1 = r.removeFromBottom (26);
        phonesLabel.setBounds (row2.removeFromLeft (180)); phonesCombo.setBounds (row2.removeFromLeft (180));
        masterLabel.setBounds (row1.removeFromLeft (180)); masterCombo.setBounds (row1.removeFromLeft (180));
        r.removeFromBottom (8);
        selector.setBounds (r);
    }

private:
    void timerCallback() override
    {
        // deshabilitar pares que la placa no tiene
        const int outs = engine.getNumOutputs();
        for (int p = 0; p < 4; ++p)
        {
            masterCombo.setItemEnabled (p + 2, p * 2 < outs);
            phonesCombo.setItemEnabled (p + 2, p * 2 < outs);
        }
    }

    Engine& engine;
    Controller& ctrl;
    juce::AudioDeviceSelectorComponent selector;
    juce::ComboBox masterCombo, phonesCombo;
    juce::Label masterLabel, phonesLabel, hint;
    juce::ToggleButton flx4, invert, nano;
};

// ---------------------------------------------------------------------------
class MidiMapPanel : public juce::Component, private juce::Timer, private juce::ListBoxModel
{
public:
    explicit MidiMapPanel (Controller& c) : ctrl (c)
    {
        for (int i = 0; i < kNumActions; ++i) targets.add (kActionBase + i);
        for (int i = 0; i < kNumParams; ++i)
            if (paramInfo ((P) i).unit != Unit::Choice) targets.add (i);

        intro.setText (u8 ("La DDJ-FLX4 ya viene mapeada. Para otro controlador (o para cambiar algo): "
                           "tocá APRENDER y mové el control o apretá el botón. Las perillas y faders escuchan medio segundo "
                           "y se quedan con el control que más se movió."), juce::dontSendNotification);
        intro.setColour (juce::Label::textColourId, Colours::textDim);
        intro.setFont (juce::FontOptions (12.5f));
        addAndMakeVisible (intro);

        list.setModel (this);
        list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, Colours::bg);
        addAndMakeVisible (list);

        monitor.setColour (juce::Label::textColourId, Colours::teal);
        monitor.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        addAndMakeVisible (monitor);
        setSize (620, 640);
        startTimerHz (10);
    }

    ~MidiMapPanel() override { ctrl.cancelLearn(); }

    void paint (juce::Graphics& g) override { g.fillAll (Colours::bg); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        intro.setBounds (r.removeFromTop (54));
        monitor.setBounds (r.removeFromBottom (26));
        r.removeFromBottom (6);
        list.setBounds (r);
    }

private:
    int getNumRows() override { return targets.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (! juce::isPositiveAndBelow (row, targets.size())) return;
        const int t = targets[row];
        const bool learning = ctrl.getLearnTarget() == t;
        if (learning) { g.setColour (Colours::amberDim); g.fillRect (0, 0, w, h); }
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (12.5f));
        const auto name = u8 (t >= kActionBase ? actionInfo ((A) (t - kActionBase)).label : paramInfo ((P) t).label);
        g.drawText (name, 8, 0, w * 45 / 100, h, juce::Justification::centredLeft, true);
        g.setColour (Colours::amber);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        const auto m = learning ? juce::String ("esperando...") : ctrl.describeMappingFor (t);
        g.drawText (m.isEmpty() ? u8 ("—") : m, w * 45 / 100, 0, w * 35 / 100, h, juce::Justification::centredLeft, true);
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("APRENDER", w - 150, 0, 80, h, juce::Justification::centred);
        g.drawText ("BORRAR", w - 70, 0, 64, h, juce::Justification::centred);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, targets.size())) return;
        const int t = targets[row];
        const int w = list.getVisibleRowWidth();
        if (e.x > w - 70) ctrl.clearMappingFor (t);
        else if (e.x > w - 150) { if (ctrl.getLearnTarget() == t) ctrl.cancelLearn(); else ctrl.startLearn (t); }
        list.repaint();
    }

    void timerCallback() override
    {
        monitor.setText (u8 ("Último MIDI: ") + ctrl.getLastMidi(), juce::dontSendNotification);
        list.repaint();
    }

    Controller& ctrl;
    juce::Array<int> targets;
    juce::Label intro, monitor;
    juce::ListBox list;
};

} // namespace ct
