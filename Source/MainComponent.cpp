#include "MainComponent.h"
#include "FactorySamples.h"
#include "GuideText.h"

namespace ct
{

static bool isFlx4 (const juce::String& n) { return n.containsIgnoreCase ("DDJ-FLX4") || n.containsIgnoreCase ("DDJ FLX4"); }

MainComponent::MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    juce::PropertiesFile::Options opts;
    opts.applicationName     = "Control Tower";
    opts.folderName          = "Control Tower";
    opts.filenameSuffix      = ".settings";
    opts.osxLibrarySubFolder = "Application Support";
    props.setStorageParameters (opts);
    auto* settings = props.getUserSettings();

    // ----- estado guardado
    engine.params.fromVar (juce::JSON::parse (settings->getValue ("params")));
    // los envíos fijos y kills no arrancan prendidos por sorpresa
    for (auto p : { P::EchoLatch, P::RevLatch, P::Kill1, P::Kill2, P::Kill3, P::Kill4, P::Kill5, P::CueA, P::CueB, P::InOn })
        engine.params.set (p, 0.0f);
    controller.mappingsFromVar (juce::JSON::parse (settings->getValue ("midiMap")));
    controller.setFlx4MappingOn (settings->getBoolValue ("flx4Mapping", true));
    controller.setTempoInverted (settings->getBoolValue ("tempoInvert", false));
    controller.setNanoMappingOn (settings->getBoolValue ("nanoMapping", true));
    engine.setOutputPairs (settings->getIntValue ("masterPair", 0), settings->getIntValue ("phonesPair", 1));
    lastDeviceName = settings->getValue ("lastDevice");
    {
        juce::StringArray roots;
        roots.addTokens (settings->getValue ("libraryRoots"), "\n", "");
        roots.removeEmptyStrings();
        if (! roots.isEmpty()) library.setRoots (roots);
    }
    for (int i = 0; i < Sampler::kSlots; ++i)
    {
        const juce::File f (settings->getValue ("sample" + juce::String (i)));
        if (f.existsAsFile()) engine.loadSample (i, f);
    }
    // la primera vez: 8 samples de dub de fábrica en las ranuras 1 a 8
    if (! settings->getBoolValue ("factorySamples", false))
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                             .getChildFile ("Control Tower").getChildFile ("Samples de fabrica");
        const auto files = factory::install (dir);
        for (int i = 0; i < files.size() && i < 8; ++i)
            if (engine.getSampleName (i).isEmpty()) engine.loadSample (i, files[i]);
        settings->setValue ("factorySamples", true);
    }

    // ----- interfaz
    title.setText ("CONTROL TOWER", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, Colours::amber);
    subtitle.setText ("preamp de sound system", juce::dontSendNotification);
    subtitle.setFont (juce::FontOptions (12.0f));
    subtitle.setColour (juce::Label::textColourId, Colours::textDim);
    status.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    status.setJustificationType (juce::Justification::centred);
    info.setFont (juce::FontOptions (11.0f));
    info.setColour (juce::Label::textColourId, Colours::textDim);
    info.setJustificationType (juce::Justification::centredRight);
    for (auto* l : { &title, &subtitle, &status, &info }) addAndMakeVisible (l);

    audioButton.onClick = [this] { showAudioSettings(); };
    midiButton.onClick = [this] { showMidiMap(); };
    guideButton.onClick = [this] { showGuide(); };
    for (auto* b : { &audioButton, &midiButton, &guideButton }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (b); }

    for (juce::Component* c : { (juce::Component*) &deck1, (juce::Component*) &deck2, (juce::Component*) &mixer,
                                (juce::Component*) &isolator, (juce::Component*) &echoView, (juce::Component*) &springView,
                                (juce::Component*) &sirenView, (juce::Component*) &masterView, (juce::Component*) &samplerView,
                                (juce::Component*) &libraryView })
        addAndMakeVisible (c);
    for (Refreshable* r : std::initializer_list<Refreshable*> { &deck1, &deck2, &mixer, &isolator, &echoView, &springView, &sirenView, &masterView, &samplerView })
        refreshables.add (r);

    deck1.onLoadFile = [this] (const juce::File& f) { loadFileInto (0, f); };
    deck2.onLoadFile = [this] (const juce::File& f) { loadFileInto (1, f); };
    libraryView.onLoad = [this] (int d) { loadSelectedInto (d); };
    samplerView.onChooseSample = [this] (int s) { chooseSample (s); };
    samplerView.onDropSample = [this] (int s, const juce::File& f)
    {
        if (! engine.loadSample (s, f)) showStatus ("No se pudo abrir " + f.getFileName(), true);
    };
    masterView.rec.onClick = [this] { toggleRecording(); };

    controller.onLoadRequest = [this] (int d) { loadSelectedInto (d); };
    controller.onStatus = [this] (const juce::String& s) { showStatus (s); };

    // teclas "mantener"
    auto siren = [this] (int i) { return [this, i] (bool down) { controller.perform ((A) ((int) A::Siren1 + i), Hold::Keyboard, down); }; };
    keyHolds = {
        { 'z', siren (0) }, { 'x', siren (1) }, { 'c', siren (2) }, { 'v', siren (3) }, { 'b', siren (4) },
        { 'e', [this] (bool d) { controller.perform (A::EchoThrow, Hold::Keyboard, d); } },
        { 'r', [this] (bool d) { controller.perform (A::RevThrow, Hold::Keyboard, d); } },
        { 'f', [this] (bool d) { controller.perform (A::EchoFreeze, Hold::Keyboard, d); } },
    };

    // ----- audio
    auto saved = settings->getXmlValue ("audioDeviceState");
    deviceManager.initialise (2, 4, saved.get(), true);
    if (saved == nullptr)
    {
        // primera vez: si está la FLX4, usarla
        if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            auto setup = deviceManager.getAudioDeviceSetup();
            for (auto& n : type->getDeviceNames (false)) if (isFlx4 (n)) setup.outputDeviceName = n;
            for (auto& n : type->getDeviceNames (true))  if (isFlx4 (n)) setup.inputDeviceName = n;
            setup.bufferSize = 512;
            setup.useDefaultInputChannels = setup.useDefaultOutputChannels = true;
            deviceManager.setAudioDeviceSetup (setup, true);
        }
    }
    deviceManager.addAudioCallback (&engine);
    deviceManager.addMidiInputDeviceCallback ({}, &controller);
    deviceManager.addChangeListener (this);
    autoRouteOutputs (false);
    watchMidiDevices();

    setWantsKeyboardFocus (true);
    setSize (1260, 760);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveSettings();
    engine.stopRecording();
    deviceManager.removeChangeListener (this);
    deviceManager.removeMidiInputDeviceCallback ({}, &controller);
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
void MainComponent::saveSettings()
{
    auto* s = props.getUserSettings();
    if (auto xml = deviceManager.createStateXml()) s->setValue ("audioDeviceState", xml.get());
    s->setValue ("params", juce::JSON::toString (engine.params.toVar(), true));
    s->setValue ("midiMap", juce::JSON::toString (controller.mappingsToVar(), true));
    s->setValue ("flx4Mapping", controller.isFlx4MappingOn());
    s->setValue ("tempoInvert", controller.isTempoInverted());
    s->setValue ("nanoMapping", controller.isNanoMappingOn());
    s->setValue ("masterPair", engine.getMasterPair());
    s->setValue ("phonesPair", engine.getPhonesPair());
    s->setValue ("lastDevice", lastDeviceName);
    s->setValue ("libraryRoots", library.getRoots().joinIntoString ("\n"));
    for (int i = 0; i < Sampler::kSlots; ++i)
        s->setValue ("sample" + juce::String (i), engine.getSampleFile (i).getFullPathName());
    s->saveIfNeeded();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    autoRouteOutputs (false);
}

// Al cambiar de placa: con 4 salidas o más (FLX4), master en 1-2 y auriculares en 3-4.
void MainComponent::autoRouteOutputs (bool force)
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) return;
    const auto name = dev->getName();
    if (name == lastDeviceName && ! force) return;
    lastDeviceName = name;
    const int outs = dev->getActiveOutputChannels().countNumberOfSetBits();
    engine.setOutputPairs (0, outs >= 4 ? 1 : -1);
    if (outs < 4) showStatus (u8 ("Esta placa tiene 2 salidas: el cue de auriculares queda desactivado."));
}

void MainComponent::watchMidiDevices()
{
    // habilitar cada controlador MIDI la primera vez que aparece (también al reconectarlo)
    std::map<juce::String, bool> now;
    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        if (midiSeen.find (d.identifier) == midiSeen.end() && ! deviceManager.isMidiInputDeviceEnabled (d.identifier))
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        now[d.identifier] = true;
    }
    midiSeen = now;
}

// ---------------------------------------------------------------------------
void MainComponent::loadSelectedInto (int d)
{
    const auto f = library.getSelectedTrackFile();
    if (f == juce::File()) { showStatus (u8 ("Elegí un tema en la biblioteca."), true); return; }
    loadFileInto (d, f);
}

void MainComponent::loadFileInto (int d, const juce::File& f)
{
    juce::String err;
    if (engine.loadTrack (d, f, err)) showStatus ("Deck " + juce::String (d + 1) + ": " + f.getFileNameWithoutExtension());
    else showStatus (err, true);
}

void MainComponent::chooseSample (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Sample para la ranura " + juce::String (slot + 1),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   engine.getLoader().getWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile() && ! engine.loadSample (slot, f))
                                  showStatus ("No se pudo abrir " + f.getFileName(), true);
                          });
}

void MainComponent::toggleRecording()
{
    if (engine.isRecording())
    {
        engine.stopRecording();
        showStatus (u8 ("Grabación guardada en Música / Control Tower"));
        return;
    }
    juce::File f;
    if (engine.startRecording (f)) showStatus ("Grabando: " + f.getFileName());
    else showStatus (u8 ("No se pudo empezar a grabar."), true);
}

void MainComponent::showStatus (const juce::String& s, bool error)
{
    status.setText (s, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, error ? Colours::red : Colours::text);
    statusUntil = juce::Time::getMillisecondCounterHiRes() + 4000.0;
}

void MainComponent::showAudioSettings()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new AudioSettingsPanel (deviceManager, engine, controller));
    o.dialogTitle = "Audio y salidas";
    o.dialogBackgroundColour = Colours::bg;
    o.componentToCentreAround = this;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::showGuide()
{
    auto* ed = new juce::TextEditor();
    ed->setMultiLine (true);
    ed->setReadOnly (true);
    ed->setScrollbarsShown (true);
    ed->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    ed->setColour (juce::TextEditor::backgroundColourId, Colours::bg);
    ed->setText (juce::String (juce::CharPointer_UTF8 (kGuideText)));
    ed->setSize (720, 680);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (ed);
    o.dialogTitle = u8 ("Guía de controles");
    o.dialogBackgroundColour = Colours::bg;
    o.componentToCentreAround = this;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::showMidiMap()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new MidiMapPanel (controller));
    o.dialogTitle = "Mapeo MIDI";
    o.dialogBackgroundColour = Colours::bg;
    o.componentToCentreAround = this;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

// ---------------------------------------------------------------------------
void MainComponent::timerCallback()
{
    ++ticks;
    for (auto* r : refreshables) r->refresh();

    if (ticks % 15 == 0)
    {
        auto* dev = deviceManager.getCurrentAudioDevice();
        juce::String s = dev != nullptr ? dev->getName() + u8 (" · ") + juce::String (juce::roundToInt (dev->getCurrentSampleRate())) + " Hz"
                                          + u8 (" · buffer ") + juce::String (dev->getCurrentBufferSizeSamples())
                                        : juce::String ("sin placa de audio");
        s << u8 (" · CPU ") << juce::roundToInt (deviceManager.getCpuUsage() * 100.0) << "%";
        s << u8 (" · FLX4 ") << (controller.isFlx4Connected() ? "conectada" : "no detectada");
        if (controller.isNanoConnected()) s << u8 (" · nanoPAD2");
        info.setText (s, juce::dontSendNotification);
        info.setColour (juce::Label::textColourId, dev == nullptr ? Colours::red : Colours::textDim);
    }
    if (statusUntil > 0.0 && juce::Time::getMillisecondCounterHiRes() > statusUntil)
    {
        status.setText ({}, juce::dontSendNotification);
        statusUntil = 0.0;
    }
    if (ticks % 60 == 0) { watchMidiDevices(); engine.purgeOldTracks(); }
    if (ticks % 900 == 0) saveSettings();   // cada 30 s

    // devolverle el teclado a la ventana (salvo mientras se escribe en el buscador)
    if (auto* peer = getPeer())
        if (peer->isFocused() && ! hasKeyboardFocus (true) && ! libraryView.isSearchFocused())
            grabKeyboardFocus();
}

// ---------------------------------------------------------------------------
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (libraryView.isSearchFocused()) return false;
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const int code = key.getKeyCode();

    for (auto& h : keyHolds) if (c == h.key) return true;      // se manejan en keyStateChanged
    if (c >= '1' && c <= '7') { engine.selectEchoDivision ((int) (c - '1')); return true; }
    if (c == 't') { engine.tap(); return true; }
    if (c == 'k') { engine.killEcho(); return true; }
    if (c == 'q') { controller.perform (A::Play1, Hold::Keyboard, true); return true; }
    if (c == 'p') { controller.perform (A::Play2, Hold::Keyboard, true); return true; }
    if (c == '[') { loadSelectedInto (0); return true; }
    if (c == ']') { loadSelectedInto (1); return true; }
    if (code == juce::KeyPress::upKey)    { library.moveSelection (-1); return true; }
    if (code == juce::KeyPress::downKey)  { library.moveSelection (1); return true; }
    if (code == juce::KeyPress::leftKey)  { library.setTrackFocus (false); return true; }
    if (code == juce::KeyPress::rightKey) { library.setTrackFocus (true); return true; }
    if (c == 'l' && key.getModifiers().isCommandDown()) { libraryView.grabKeyboardFocus(); return true; }
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    if (libraryView.isSearchFocused()) { releaseKeys(); return false; }
    bool any = false;
    for (auto& h : keyHolds)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown (h.key)
                       || juce::KeyPress::isKeyCurrentlyDown (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) h.key));
        if (down != h.down) { h.down = down; h.fn (down); }
        any = any || down;
    }
    return any;
}

void MainComponent::releaseKeys()
{
    for (auto& h : keyHolds) if (h.down) { h.down = false; h.fn (false); }
}

// ---------------------------------------------------------------------------
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (8);
    const int gap = 6;

    auto header = r.removeFromTop (34);
    auto tl = header.removeFromLeft (190);
    title.setBounds (tl.removeFromTop (21));
    subtitle.setBounds (tl);
    guideButton.setBounds (header.removeFromRight (70).reduced (1, 2));
    midiButton.setBounds (header.removeFromRight (110).reduced (1, 2));
    audioButton.setBounds (header.removeFromRight (130).reduced (1, 2));
    info.setBounds (header.removeFromRight (header.getWidth() / 2));
    status.setBounds (header);
    r.removeFromTop (gap);

    const int h = r.getHeight();
    auto deckRow = r.removeFromTop (juce::jmax (200, h * 30 / 100));
    r.removeFromTop (gap);
    auto fxRow = r.removeFromTop (juce::jmax (210, h * 31 / 100));
    r.removeFromTop (gap);
    auto bottom = r;

    const int mixW = juce::jmax (260, deckRow.getWidth() * 22 / 100);
    const int deckW = (deckRow.getWidth() - mixW - 2 * gap) / 2;
    deck1.setBounds (deckRow.removeFromLeft (deckW));
    deckRow.removeFromLeft (gap);
    mixer.setBounds (deckRow.removeFromLeft (mixW));
    deckRow.removeFromLeft (gap);
    deck2.setBounds (deckRow);

    // aislador | echo | spring | sirenas | master
    const int W = fxRow.getWidth() - 4 * gap;
    isolator.setBounds (fxRow.removeFromLeft (W * 22 / 100)); fxRow.removeFromLeft (gap);
    echoView.setBounds (fxRow.removeFromLeft (W * 29 / 100)); fxRow.removeFromLeft (gap);
    springView.setBounds (fxRow.removeFromLeft (W * 15 / 100)); fxRow.removeFromLeft (gap);
    sirenView.setBounds (fxRow.removeFromLeft (W * 16 / 100)); fxRow.removeFromLeft (gap);
    masterView.setBounds (fxRow);

    samplerView.setBounds (bottom.removeFromRight (juce::jmax (380, bottom.getWidth() * 34 / 100)));
    bottom.removeFromRight (gap);
    libraryView.setBounds (bottom);
}

} // namespace ct
