// MainComponent.h — la ventana: arma el motor, la biblioteca, el MIDI y todos los módulos.
#pragma once

#include "Views.h"
#include "Dialogs.h"

namespace ct
{

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override { releaseKeys(); }

    void saveSettings();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void loadSelectedInto (int deck);
    void loadFileInto (int deck, const juce::File&);
    void chooseSample (int slot);
    void showStatus (const juce::String&, bool error = false);
    void watchMidiDevices();
    void autoRouteOutputs (bool force);
    void showAudioSettings();
    void showMidiMap();
    void toggleRecording();
    void releaseKeys();

    CtLookAndFeel lnf;
    juce::ApplicationProperties props;
    juce::AudioDeviceManager deviceManager;
    Engine engine;
    Library library;
    Controller controller { engine, library, deviceManager };

    juce::Label title, subtitle, status, info;
    juce::TextButton audioButton { "AUDIO / SALIDAS" }, midiButton { "MAPEO MIDI" };
    DeckView deck1 { engine, controller, 0 }, deck2 { engine, controller, 1 };
    MixerView mixer { engine };
    IsolatorView isolator { engine, controller };
    EchoView echoView { engine, controller };
    SpringView springView { engine, controller };
    SirenView sirenView { engine, controller };
    MasterView masterView { engine };
    SamplerView samplerView { engine, controller };
    LibraryView libraryView { library };
    juce::Array<Refreshable*> refreshables;

    std::unique_ptr<juce::FileChooser> chooser;
    std::map<juce::String, bool> midiSeen;
    juce::String lastDeviceName;
    double statusUntil = 0.0;
    int ticks = 0;

    // teclas "mantener": Z X C V B sirenas, E echo throw, R spring throw, F freeze
    struct KeyHold { int key; std::function<void (bool)> fn; bool down = false; };
    std::vector<KeyHold> keyHolds;

    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace ct
