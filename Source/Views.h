// Views.h — los módulos de la pantalla.
#pragma once

#include "Ui.h"

namespace ct
{

// Botón con acciones al apretar / soltar (para comandos directos del deck).
class PressButton : public juce::TextButton, public Refreshable
{
public:
    explicit PressButton (const juce::String& t, juce::Colour on = Colours::amber) : juce::TextButton (t)
    {
        setColour (juce::TextButton::buttonOnColourId, on);
        setWantsKeyboardFocus (false);
    }
    std::function<void()> onPress, onRelease, onRightClick;
    std::function<bool()> litWhen;
    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseDown (e);
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        if (onPress) onPress();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseUp (e);
        if (! e.mods.isPopupMenu() && onRelease) onRelease();
    }
    void refresh() override
    {
        const bool st = litWhen ? litWhen() : false;
        if (st != getToggleState()) setToggleState (st, juce::dontSendNotification);
    }
};

// Punto "ENTRANDO" junto al título: se prende sólo mientras entra señal al efecto.
inline void paintSendingDot (juce::Graphics& g, bool on, juce::Colour c, int x)
{
    const juce::Rectangle<float> dot ((float) x, 9.0f, 8.0f, 8.0f);
    g.setColour (on ? c : Colours::line);
    g.fillEllipse (dot);
    if (on)
    {
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("ENTRANDO", juce::Rectangle<float> ((float) x + 12.0f, 5.0f, 70.0f, 16.0f), juce::Justification::centredLeft);
    }
}

inline juce::String formatTime (double s, bool tenths = true)
{
    if (s < 0.0) s = 0.0;
    const int m = (int) (s / 60.0);
    const double sec = s - m * 60.0;
    return juce::String (m) + ":" + juce::String ((int) sec).paddedLeft ('0', 2)
         + (tenths ? "." + juce::String ((int) ((sec - std::floor (sec)) * 10.0)) : juce::String());
}

// ---------------------------------------------------------------------------
// Forma de onda: modo "zoom" (alrededor del cabezal, se desplaza) o "overview" (tema entero).
class WaveView : public juce::Component
{
public:
    WaveView (Engine& e, int deckIndex, bool zoomed, juce::Colour c) : engine (e), d (deckIndex), zoom (zoomed), colour (c) {}

    std::function<void (double frac)> onSeek;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::bg);
        g.fillRoundedRectangle (r, 3.0f);
        auto track = engine.getTrack (d);
        if (track == nullptr) return;

        const double pos = engine.deck (d).getPositionSeconds();
        const double sr = track->sampleRate;
        const double len = (double) track->lengthFrames.load() / sr;
        const auto decodedPeaks = (int64_t) (track->decodedFrames.load() / TrackAudio::kPeakFrames);
        const int w = getWidth(), h = getHeight();
        const float mid = (float) h * 0.5f;
        const double peakSec = TrackAudio::kPeakFrames / sr;

        const double t0 = zoom ? pos - kZoomSeconds * 0.5 : 0.0;
        const double spp = zoom ? kZoomSeconds / w : len / juce::jmax (1, w);

        auto& dk = engine.deck (d);
        // loop
        if (dk.isLoopActive())
        {
            const float x1 = (float) ((dk.getLoopInSeconds() - t0) / spp), x2 = (float) ((dk.getLoopOutSeconds() - t0) / spp);
            g.setColour (Colours::green.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmax (0.0f, x1), 0.0f, juce::jmin ((float) w, x2), (float) h));
        }

        const auto* pk = track->peaks.data();
        const auto* pl = track->peaksLow.data();
        const auto full = colour.withAlpha (zoom ? 0.45f : 0.55f);
        const auto low = colour.brighter (0.2f);
        for (int x = 0; x < w; ++x)
        {
            const double ta = t0 + x * spp, tb = ta + spp;
            auto ia = (int64_t) (ta / peakSec), ib = (int64_t) (tb / peakSec);
            if (ib <= ia) ib = ia + 1;
            if (ia < 0 || ia >= decodedPeaks) continue;
            ib = juce::jmin (ib, decodedPeaks);
            int m = 0, ml = 0;
            for (auto i = ia; i < ib; ++i) { m = juce::jmax (m, (int) pk[i]); ml = juce::jmax (ml, (int) pl[i]); }
            const float hh = (float) m / 255.0f * mid, hl = (float) ml / 255.0f * mid;
            const bool played = ! zoom && ta < pos;
            g.setColour (played ? full.withMultipliedBrightness (0.55f) : full);
            g.drawVerticalLine (x, mid - hh, mid + hh);
            g.setColour (played ? low.withMultipliedBrightness (0.55f) : low);
            g.drawVerticalLine (x, mid - hl, mid + hl);
        }

        auto marker = [&] (double sec, juce::Colour c, float width)
        {
            if (sec < 0.0) return;
            const float x = (float) ((sec - t0) / spp);
            if (x < 0.0f || x > (float) w) return;
            g.setColour (c);
            g.fillRect (x - width * 0.5f, 0.0f, width, (float) h);
        };
        for (int i = 0; i < Deck::kNumHotcues; ++i) marker (dk.getHotcueSeconds (i), Colours::ice.withAlpha (0.8f), 1.5f);
        marker (dk.getCueSeconds(), Colours::amber, 1.5f);
        if (zoom) { g.setColour (Colours::text); g.fillRect ((float) w * 0.5f - 1.0f, 0.0f, 2.0f, (float) h); }
        else marker (pos, Colours::text, 2.0f);

        if (! track->finished.load())
        {
            g.setColour (Colours::textDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("cargando " + juce::String (juce::roundToInt (100.0 * track->decodedFrames.load() / juce::jmax ((int64_t) 1, track->lengthFrames.load()))) + "%",
                        getLocalBounds().reduced (4, 2), juce::Justification::topRight);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { if (! zoom && onSeek) onSeek (juce::jlimit (0.0, 1.0, (double) e.x / getWidth())); }
    void mouseDrag (const juce::MouseEvent& e) override { mouseDown (e); }

private:
    static constexpr double kZoomSeconds = 8.0;
    Engine& engine;
    int d;
    bool zoom;
    juce::Colour colour;
};

// ---------------------------------------------------------------------------
class DeckView : public Panel, public Refreshable, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget
{
public:
    DeckView (Engine& e, Controller& c, int deckIndex)
        : Panel ("DECK " + juce::String (deckIndex + 1), deckIndex == 0 ? Colours::deckA : Colours::deckB),
          engine (e), ctrl (c), d (deckIndex),
          zoomWave (e, deckIndex, true, deckIndex == 0 ? Colours::deckA : Colours::deckB),
          overview (e, deckIndex, false, deckIndex == 0 ? Colours::deckA : Colours::deckB),
          cue (c, deckIndex == 0 ? A::Cue1 : A::Cue2, "CUE", Colours::amber),
          play (c, deckIndex == 0 ? A::Play1 : A::Play2, u8 ("▶ ⏸"), Colours::green)
    {
        overview.onSeek = [this] (double f) { engine.deck (d).pushCommand ({ DeckCmd::SeekFrac, 0, f }); };
        addAndMakeVisible (zoomWave);
        addAndMakeVisible (overview);

        for (auto* l : { &titleLabel, &timeLabel, &pitchLabel })
        {
            l->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (l);
        }
        titleLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        timeLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        timeLabel.setJustificationType (juce::Justification::centredRight);
        pitchLabel.setFont (juce::FontOptions (11.5f));
        pitchLabel.setColour (juce::Label::textColourId, Colours::textDim);
        pitchLabel.setJustificationType (juce::Justification::centredRight);

        cue.litWhen = [this] { return engine.deck (d).getTrack() != nullptr && ! engine.deck (d).isPlaying(); };
        play.litWhen = [this] { return engine.deck (d).isPlaying(); };
        addAndMakeVisible (cue);
        addAndMakeVisible (play);

        auto mk = [this] (const juce::String& t, DeckCmd cmd, std::function<bool()> lit = {})
        {
            auto* b = buttons.add (new PressButton (t, Colours::green));
            b->onPress = [this, cmd] { engine.deck (d).pushCommand ({ cmd, 0, 1.0 }); };
            b->litWhen = std::move (lit);
            addAndMakeVisible (b);
        };
        mk ("IN", DeckCmd::LoopIn);
        mk ("OUT", DeckCmd::LoopOut, [this] { return engine.deck (d).isLoopActive(); });
        mk ("RELOOP", DeckCmd::Reloop, [this] { return engine.deck (d).isLoopActive(); });
        mk (u8 ("½"), DeckCmd::LoopHalve);
        mk (u8 ("2×"), DeckCmd::LoopDouble);

        for (int i = 0; i < Deck::kNumHotcues; ++i)
        {
            auto* b = hot.add (new PressButton (juce::String (i + 1), Colours::ice));
            b->onPress = [this, i] { engine.deck (d).pushCommand ({ DeckCmd::Hotcue, i, 1.0 }); };
            b->onRelease = [this, i] { engine.deck (d).pushCommand ({ DeckCmd::Hotcue, i, 0.0 }); };
            b->onRightClick = [this, i] { engine.deck (d).pushCommand ({ DeckCmd::HotcueClear, i, 1.0 }); };
            b->litWhen = [this, i] { return engine.deck (d).getHotcueSeconds (i) >= 0.0; };
            b->setTooltip ("Clic: marcar / saltar. Clic derecho: borrar.");
            addAndMakeVisible (b);
        }

        rangeButton.setWantsKeyboardFocus (false);
        rangeButton.onClick = [this]
        {
            const P r = d == 0 ? P::RangeA : P::RangeB;
            engine.params.set (r, (float) ((engine.params.choice (r) + 1) % 3));
        };
        addAndMakeVisible (rangeButton);
    }

    void refresh() override
    {
        auto& dk = engine.deck (d);
        auto t = engine.getTrack (d);
        const juce::String name = t != nullptr ? juce::String (juce::CharPointer_UTF8 (t->title.c_str())) : u8 ("— sin tema —");
        if (titleLabel.getText() != name) titleLabel.setText (name, juce::dontSendNotification);

        const double pos = dk.getPositionSeconds();
        const double len = t != nullptr ? (double) t->lengthFrames.load() / t->sampleRate : 0.0;
        const auto time = t != nullptr ? formatTime (pos) + "   -" + formatTime (len - pos, false) : juce::String();
        if (timeLabel.getText() != time) timeLabel.setText (time, juce::dontSendNotification);
        // la cuenta regresiva se pone roja en los últimos 30 s
        timeLabel.setColour (juce::Label::textColourId, t != nullptr && len - pos < 30.0 && dk.isPlaying() ? Colours::red : Colours::text);

        static const char* rn[] = { u8"±8", u8"±16", u8"±50" };
        const P r = d == 0 ? P::RangeA : P::RangeB;
        const float pct = (dk.getRate() - 1.0f) * 100.0f;
        const auto pitch = (pct >= 0.0f ? "+" : "") + juce::String (pct, 2) + "%";
        if (pitchLabel.getText() != pitch) pitchLabel.setText (pitch, juce::dontSendNotification);
        const auto rt = juce::String (juce::CharPointer_UTF8 (rn[engine.params.choice (r)])) + "%";
        if (rangeButton.getButtonText() != rt) rangeButton.setButtonText (rt);

        cue.refresh(); play.refresh();
        for (auto* b : buttons) b->refresh();
        for (auto* b : hot) b->refresh();
        zoomWave.repaint();
        overview.repaint();
    }

    void resized() override
    {
        auto r = content();
        auto top = r.removeFromTop (20);
        rangeButton.setBounds (top.removeFromRight (44).reduced (0, 1));
        pitchLabel.setBounds (top.removeFromRight (70));
        timeLabel.setBounds (top.removeFromRight (150));
        titleLabel.setBounds (top);
        r.removeFromTop (3);
        zoomWave.setBounds (r.removeFromTop (juce::jmax (40, r.getHeight() - 26 - 26 - 30 - 8)));
        r.removeFromTop (3);
        overview.setBounds (r.removeFromTop (26));
        r.removeFromTop (4);
        auto row = r.removeFromTop (30);
        cue.setBounds (row.removeFromLeft (64).reduced (1));
        play.setBounds (row.removeFromLeft (64).reduced (1));
        row.removeFromLeft (8);
        const int bw = row.getWidth() / buttons.size();
        for (auto* b : buttons) b->setBounds (row.removeFromLeft (bw).reduced (1));
        r.removeFromTop (2);
        auto hrow = r.removeFromTop (24);
        const int hw = hrow.getWidth() / hot.size();
        for (auto* b : hot) b->setBounds (hrow.removeFromLeft (hw).reduced (1));
    }

    // arrastrar archivos desde el Finder o desde la biblioteca
    bool isInterestedInFileDrag (const juce::StringArray& files) override { return files.size() == 1 && Library::isAudioFile (juce::File (files[0])); }
    void filesDropped (const juce::StringArray& files, int, int) override { if (onLoadFile) onLoadFile (juce::File (files[0])); }
    bool isInterestedInDragSource (const SourceDetails& s) override { return s.description.toString().startsWith ("track:"); }
    void itemDropped (const SourceDetails& s) override { if (onLoadFile) onLoadFile (juce::File (s.description.toString().fromFirstOccurrenceOf ("track:", false, false))); }

    std::function<void (const juce::File&)> onLoadFile;

private:
    Engine& engine;
    Controller& ctrl;
    int d;
    WaveView zoomWave, overview;
    juce::Label titleLabel, timeLabel, pitchLabel;
    ActionButton cue, play;
    juce::OwnedArray<PressButton> buttons, hot;
    juce::TextButton rangeButton;
};

// ---------------------------------------------------------------------------
class MixerView : public Panel, public Refreshable
{
public:
    MixerView (Engine& e) : Panel ("MIXER", Colours::text), engine (e)
    {
        auto& p = e.params;
        const P trims[2] = { P::TrimA, P::TrimB }, his[2] = { P::HiA, P::HiB }, mids[2] = { P::MidA, P::MidB },
                lows[2] = { P::LowA, P::LowB }, fils[2] = { P::FilterA, P::FilterB }, fads[2] = { P::FaderA, P::FaderB },
                cues[2] = { P::CueA, P::CueB };
        for (int c = 0; c < 2; ++c)
        {
            const auto col = c == 0 ? Colours::deckA : Colours::deckB;
            auto& s = strips[(size_t) c];
            s.knobs.add (new ParamKnob (p, trims[c], "TRIM", col));
            s.knobs.add (new ParamKnob (p, his[c], "HI", col));
            s.knobs.add (new ParamKnob (p, mids[c], "MID", col));
            s.knobs.add (new ParamKnob (p, lows[c], "LOW", col));
            s.knobs.add (new ParamKnob (p, fils[c], "FILTER", col));
            for (auto* k : s.knobs) { addAndMakeVisible (k); items.add (k); }
            s.fader = std::make_unique<ParamFader> (p, fads[c], true, col);
            s.cue = std::make_unique<ParamToggle> (p, cues[c], "CUE", Colours::amber);
            addAndMakeVisible (*s.fader); addAndMakeVisible (*s.cue); addAndMakeVisible (s.meter);
            items.add (s.fader.get()); items.add (s.cue.get());
        }
        xfader = std::make_unique<ParamFader> (p, P::Crossfader, false, Colours::text);
        addAndMakeVisible (*xfader);
        items.add (xfader.get());
    }

    void refresh() override
    {
        for (auto* i : items) i->refresh();
        for (int c = 0; c < 2; ++c) strips[(size_t) c].meter.setLevel (engine.getChannelLevel (c));
    }

    void resized() override
    {
        auto r = content();
        xfader->setBounds (r.removeFromBottom (24).reduced (r.getWidth() / 4, 0));
        r.removeFromBottom (2);
        const int half = r.getWidth() / 2;
        for (int c = 0; c < 2; ++c)
        {
            auto col = c == 0 ? r.removeFromLeft (half) : r;
            auto& s = strips[(size_t) c];
            // fader y medidor del lado de adentro
            auto inner = c == 0 ? col.removeFromRight (40) : col.removeFromLeft (40);
            s.cue->setBounds (inner.removeFromBottom (20).reduced (1, 0));
            auto m = c == 0 ? inner.removeFromRight (6) : inner.removeFromLeft (6);
            s.meter.setBounds (m.reduced (0, 4));
            s.fader->setBounds (inner);
            // perillas 2 x 3
            const int kw = col.getWidth() / 2, kh = col.getHeight() / 3;
            for (int i = 0; i < s.knobs.size(); ++i)
                s.knobs[i]->setBounds (col.getX() + (i % 2) * kw, col.getY() + (i / 2) * kh, kw, kh);
        }
    }

private:
    struct Strip
    {
        juce::OwnedArray<ParamKnob> knobs;
        std::unique_ptr<ParamFader> fader;
        std::unique_ptr<ParamToggle> cue;
        LevelMeter meter;
    };
    Engine& engine;
    std::array<Strip, 2> strips;
    std::unique_ptr<ParamFader> xfader;
    juce::Array<Refreshable*> items;
};

// ---------------------------------------------------------------------------
class IsolatorView : public Panel, public Refreshable
{
public:
    IsolatorView (Engine& e, Controller& c) : Panel ("AISLADOR", Colours::amber), engine (e)
    {
        auto& p = e.params;
        for (int m = 0; m < 3; ++m)
        {
            auto* b = modes.add (new ParamToggle (p, P::IsoMode, juce::String (m + 3) + u8 (" V"), Colours::amber, m));
            addAndMakeVisible (b);
        }
        flat = std::make_unique<ActionButton> (c, A::Flat, "FLAT", Colours::text);
        sub = std::make_unique<ParamToggle> (p, P::SubBoost, "SUB BOOST", Colours::red);
        addAndMakeVisible (*flat); addAndMakeVisible (*sub);
        for (int b = 0; b < 5; ++b)
        {
            auto& band = bands[(size_t) b];
            band.fader = std::make_unique<ParamFader> (p, (P) ((int) P::Iso1 + b), true, Colours::amber);
            band.kill = std::make_unique<ParamToggle> (p, (P) ((int) P::Kill1 + b), "KILL", Colours::red);
            band.name.setJustificationType (juce::Justification::centred);
            band.name.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            band.name.setColour (juce::Label::textColourId, Colours::textDim);
            band.value.setJustificationType (juce::Justification::centred);
            band.value.setFont (juce::FontOptions (10.0f));
            addAndMakeVisible (*band.fader); addAndMakeVisible (*band.kill);
            addAndMakeVisible (band.name); addAndMakeVisible (band.value);
        }
    }

    void refresh() override
    {
        auto& p = engine.params;
        const int mode = p.choice (P::IsoMode);
        for (auto* m : modes) m->refresh();
        sub->refresh(); flat->refresh();
        if (mode != shownMode) { shownMode = mode; resized(); }
        for (int b = 0; b < 5; ++b)
        {
            auto& band = bands[(size_t) b];
            const bool vis = b < isoBands (mode);
            band.fader->setVisible (vis); band.kill->setVisible (vis); band.name.setVisible (vis); band.value.setVisible (vis);
            if (! vis) continue;
            band.fader->refresh(); band.kill->refresh();
            const auto nm = juce::String (isoBandName (mode, b));
            if (band.name.getText() != nm) band.name.setText (nm, juce::dontSendNotification);
            const auto v = p.on ((P) ((int) P::Kill1 + b)) ? juce::String ("KILL") : p.format ((P) ((int) P::Iso1 + b));
            if (band.value.getText() != v) band.value.setText (v, juce::dontSendNotification);
        }
    }

    void resized() override
    {
        auto r = content();
        auto top = r.removeFromTop (22);
        const int mw = top.getWidth() * 19 / 100;
        for (auto* m : modes) m->setBounds (top.removeFromLeft (mw).reduced (1));
        top.removeFromLeft (4);
        flat->setBounds (top.removeFromLeft (top.getWidth() * 40 / 100).reduced (1));
        sub->setBounds (top.reduced (1));
        r.removeFromTop (4);
        const int n = isoBands (juce::jmax (0, shownMode));
        const int w = r.getWidth() / n;
        for (int b = 0; b < n; ++b)
        {
            auto col = r.removeFromLeft (w).reduced (2, 0);
            auto& band = bands[(size_t) b];
            band.name.setBounds (col.removeFromTop (14));
            band.kill->setBounds (col.removeFromBottom (20));
            band.value.setBounds (col.removeFromBottom (14));
            band.fader->setBounds (col);
        }
    }

private:
    struct Band { std::unique_ptr<ParamFader> fader; std::unique_ptr<ParamToggle> kill; juce::Label name, value; };
    Engine& engine;
    juce::OwnedArray<ParamToggle> modes;
    std::unique_ptr<ActionButton> flat;
    std::unique_ptr<ParamToggle> sub;
    std::array<Band, 5> bands;
    int shownMode = 1;
};

// ---------------------------------------------------------------------------
class EchoView : public Panel, public Refreshable
{
public:
    EchoView (Engine& e, Controller& c) : Panel ("TAPE ECHO", Colours::amber), engine (e)
    {
        auto& p = e.params;
        auto knob = [&] (P param, const char* name) { auto* k = knobs.add (new ParamKnob (p, param, u8 (name))); addAndMakeVisible (k); };
        knob (P::EchoTime, "TIEMPO");
        knob (P::EchoFb, "REPETIC.");
        knob (P::EchoSend, "NIV. ENVÍO");
        knob (P::EchoRet, "RETORNO");
        knob (P::EchoIner, "INERCIA");
        knob (P::EchoLow, "GRAVES");
        knob (P::EchoHigh, "AGUDOS");
        knob (P::EchoWow, "WOW");
        knob (P::EchoSat, "SATUR.");

        for (int i = 0; i < kNumEchoDivisions; ++i)
        {
            auto* b = divs.add (new ActionButton (c, (A) ((int) A::Div1 + i), u8 (echoDivisionLabel (i)), Colours::amber));
            b->litWhen = [this, i] { return engine.getEchoDivision() == i && engine.echoTimeIsSynced(); };
            addAndMakeVisible (b);
        }
        tapB = std::make_unique<ActionButton> (c, A::Tap, "TAP", Colours::text);
        addAndMakeVisible (*tapB);

        throwB = std::make_unique<ActionButton> (c, A::EchoThrow, "THROW", Colours::amber);
        throwB->litWhen = [this] { return engine.echoThrow.held(); };
        latch = std::make_unique<ParamToggle> (p, P::EchoLatch, u8 ("ENVÍO FIJO"), Colours::teal);
        freeze = std::make_unique<ActionButton> (c, A::EchoFreeze, "FREEZE", Colours::ice);
        freeze->litWhen = [this] { return engine.echoFreeze.held(); };
        kill = std::make_unique<ActionButton> (c, A::EchoKill, "KILL", Colours::red);
        for (juce::Component* comp : { (juce::Component*) throwB.get(), (juce::Component*) latch.get(), (juce::Component*) freeze.get(), (juce::Component*) kill.get() })
            addAndMakeVisible (comp);
        kill->setColour (juce::TextButton::buttonColourId, Colours::red.withAlpha (0.3f));

        for (int s = 0; s < 3; ++s)
        {
            static const char* names[] = { "CANAL 1", "CANAL 2", "AMBOS" };
            auto* b = sources.add (new ParamToggle (p, P::EchoSrc, names[s], Colours::text, s));
            addAndMakeVisible (b);
        }

        readout.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        readout.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (readout);
    }

    void refresh() override
    {
        for (auto* k : knobs) k->refresh();
        for (auto* b : divs) b->refresh();
        for (auto* b : sources) b->refresh();
        throwB->refresh(); latch->refresh(); freeze->refresh(); kill->refresh(); tapB->refresh();
        juce::String t = juce::String (juce::roundToInt (engine.params.get (P::EchoTime))) + " ms";
        if (engine.echoTimeIsSynced())
            t << u8 ("  ·  ") << u8 (echoDivisionLabel (engine.getEchoDivision())) << " a " << juce::roundToInt (engine.getBpm()) << " BPM";
        if (readout.getText() != t) readout.setText (t, juce::dontSendNotification);
        if (engine.isEchoSending() != sendingShown) { sendingShown = ! sendingShown; repaint(); }
    }

    void paintExtra (juce::Graphics& g) override { paintSendingDot (g, sendingShown, Colours::amber, 92); }

    void resized() override
    {
        auto r = content();
        readout.setBounds (getLocalBounds().reduced (10, 4).removeFromTop (16));
        auto divRow = r.removeFromTop (22);
        const int dw = divRow.getWidth() / (kNumEchoDivisions + 1);
        for (auto* b : divs) b->setBounds (divRow.removeFromLeft (dw).reduced (1));
        tapB->setBounds (divRow.reduced (1));
        r.removeFromTop (2);
        auto btns = r.removeFromBottom (24);
        const int bw = btns.getWidth() / 4;
        throwB->setBounds (btns.removeFromLeft (bw).reduced (1));
        latch->setBounds (btns.removeFromLeft (bw).reduced (1));
        freeze->setBounds (btns.removeFromLeft (bw).reduced (1));
        kill->setBounds (btns.reduced (1));
        r.removeFromBottom (2);
        auto srcRow = r.removeFromBottom (20);
        const int sw = srcRow.getWidth() / 3;
        for (auto* b : sources) b->setBounds (srcRow.removeFromLeft (sw).reduced (1));
        // 9 perillas: 5 + 4
        const int kh = r.getHeight() / 2;
        auto row1 = r.removeFromTop (kh), row2 = r;
        const int kw = row1.getWidth() / 5;
        for (int i = 0; i < 5; ++i) knobs[i]->setBounds (row1.removeFromLeft (kw));
        row2.removeFromLeft (kw / 2);
        for (int i = 5; i < 9; ++i) knobs[i]->setBounds (row2.removeFromLeft (kw));
    }

private:
    Engine& engine;
    juce::OwnedArray<ParamKnob> knobs;
    juce::OwnedArray<ActionButton> divs;
    juce::OwnedArray<ParamToggle> sources;
    std::unique_ptr<ActionButton> tapB, throwB, freeze, kill;
    std::unique_ptr<ParamToggle> latch;
    juce::Label readout;
    bool sendingShown = false;
};

// ---------------------------------------------------------------------------
class SpringView : public Panel, public Refreshable
{
public:
    SpringView (Engine& e, Controller& c) : Panel ("SPRING", Colours::teal), engine (e)
    {
        auto& p = e.params;
        auto knob = [&] (P param, const char* name) { auto* k = knobs.add (new ParamKnob (p, param, u8 (name), Colours::teal)); addAndMakeVisible (k); };
        knob (P::RevSend, "NIV. ENVÍO");
        knob (P::RevRet, "RETORNO");
        knob (P::EchoToRev, "DEL ECHO");
        knob (P::RevSize, "CUERPO");
        knob (P::RevDecay, "COLA");
        knob (P::RevTone, "TONO");
        throwB = std::make_unique<ActionButton> (c, A::RevThrow, "THROW", Colours::teal);
        throwB->litWhen = [this] { return engine.revThrow.held(); };
        latch = std::make_unique<ParamToggle> (p, P::RevLatch, "FIJO", Colours::teal);
        addAndMakeVisible (*throwB); addAndMakeVisible (*latch);
    }
    void refresh() override
    {
        for (auto* k : knobs) k->refresh();
        throwB->refresh(); latch->refresh();
        if (engine.isSpringSending() != sendingShown) { sendingShown = ! sendingShown; repaint(); }
    }
    void paintExtra (juce::Graphics& g) override { paintSendingDot (g, sendingShown, Colours::teal, 64); }
    void resized() override
    {
        auto r = content();
        auto btns = r.removeFromBottom (24);
        throwB->setBounds (btns.removeFromLeft (btns.getWidth() / 2).reduced (1));
        latch->setBounds (btns.reduced (1));
        r.removeFromBottom (2);
        const int kw = r.getWidth() / 3, kh = r.getHeight() / 2;
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (r.getX() + (i % 3) * kw, r.getY() + (i / 3) * kh, kw, kh);
    }
private:
    Engine& engine;
    juce::OwnedArray<ParamKnob> knobs;
    std::unique_ptr<ActionButton> throwB;
    std::unique_ptr<ParamToggle> latch;
    bool sendingShown = false;
};

// ---------------------------------------------------------------------------
class SirenView : public Panel, public Refreshable
{
public:
    SirenView (Engine& e, Controller& c) : Panel ("SIRENAS", Colours::red), engine (e)
    {
        auto& p = e.params;
        for (int i = 0; i < kNumSirenPresets; ++i)
        {
            auto* b = sirens.add (new ActionButton (c, (A) ((int) A::Siren1 + i), u8 (sirenPreset (i).name), Colours::red));
            b->litWhen = [this, i] { return engine.getSirenPlaying() == i; };
            addAndMakeVisible (b);
        }
        auto knob = [&] (P param, const char* name) { auto* k = knobs.add (new ParamKnob (p, param, u8 (name), Colours::red)); addAndMakeVisible (k); };
        knob (P::SirenPitch, "TONO");
        knob (P::SirenRate, "VELOC.");
        knob (P::SirenLevel, "VOLUMEN");
        knob (P::SirenEcho, "AL ECHO");
    }
    void refresh() override { for (auto* b : sirens) b->refresh(); for (auto* k : knobs) k->refresh(); }
    void resized() override
    {
        auto r = content();
        auto b1 = r.removeFromTop (24), b2 = r.removeFromTop (24);
        const int w3 = b1.getWidth() / 3;
        for (int i = 0; i < 3; ++i) sirens[i]->setBounds (b1.removeFromLeft (w3).reduced (1));
        const int w2 = b2.getWidth() / 2;
        for (int i = 3; i < 5; ++i) sirens[i]->setBounds (b2.removeFromLeft (w2).reduced (1));
        r.removeFromTop (4);
        const int kw = r.getWidth() / 2, kh = r.getHeight() / 2;
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (r.getX() + (i % 2) * kw, r.getY() + (i / 2) * kh, kw, kh);
    }
private:
    Engine& engine;
    juce::OwnedArray<ActionButton> sirens;
    juce::OwnedArray<ParamKnob> knobs;
};

// ---------------------------------------------------------------------------
class MasterView : public Panel, public Refreshable
{
public:
    MasterView (Engine& e) : Panel ("MASTER", Colours::text), engine (e)
    {
        auto& p = e.params;
        auto knob = [&] (P param, const char* name, juce::Colour c = Colours::text) { auto* k = knobs.add (new ParamKnob (p, param, u8 (name), c)); addAndMakeVisible (k); };
        knob (P::MFilter, "FILTRO");
        knob (P::MRes, "RESON.");
        knob (P::Drive, "DRIVE", Colours::red);
        knob (P::MLevel, "NIVEL");
        knob (P::HeadMix, "AURIC. MIX", Colours::amber);
        knob (P::HeadLevel, "AURIC. NIV", Colours::amber);
        limiter = std::make_unique<ParamToggle> (p, P::Limiter, "LIMIT", Colours::green);
        addAndMakeVisible (*limiter);
        addAndMakeVisible (meterL); addAndMakeVisible (meterR);
        rec.setColour (juce::TextButton::buttonOnColourId, Colours::red);
        rec.setWantsKeyboardFocus (false);
        addAndMakeVisible (rec);
        gr.setFont (juce::FontOptions (10.0f));
        gr.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (gr);
    }

    juce::TextButton rec { "REC" };

    void refresh() override
    {
        for (auto* k : knobs) k->refresh();
        limiter->refresh();
        meterL.setLevel (engine.getMasterLevel (0));
        meterR.setLevel (engine.getMasterLevel (1));
        const float g = engine.getLimiterReductionDb();
        const auto t = g < -0.5f ? juce::String (g, 1) + " dB" : juce::String();
        if (gr.getText() != t) gr.setText (t, juce::dontSendNotification);
        const bool r = engine.isRecording();
        rec.setToggleState (r, juce::dontSendNotification);
        const auto rt = r ? "REC " + formatTime (engine.getRecordingSeconds(), false) : juce::String ("REC");
        if (rec.getButtonText() != rt) rec.setButtonText (rt);
    }

    void resized() override
    {
        auto r = content();
        auto right = r.removeFromRight (44);
        rec.setBounds (r.removeFromBottom (24).reduced (1));
        limiter->setBounds (right.removeFromBottom (24).reduced (1));
        gr.setBounds (right.removeFromBottom (14));
        auto m = right.reduced (6, 2);
        meterL.setBounds (m.removeFromLeft (m.getWidth() / 2).reduced (2, 0));
        meterR.setBounds (m.reduced (2, 0));
        r.removeFromBottom (2);
        const int kw = r.getWidth() / 2, kh = r.getHeight() / 3;
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (r.getX() + (i % 2) * kw, r.getY() + (i / 2) * kh, kw, kh);
    }

private:
    Engine& engine;
    juce::OwnedArray<ParamKnob> knobs;
    std::unique_ptr<ParamToggle> limiter;
    LevelMeter meterL, meterR;
    juce::Label gr;
};

// ---------------------------------------------------------------------------
class SamplerView : public Panel, public Refreshable, public juce::FileDragAndDropTarget
{
public:
    SamplerView (Engine& e, Controller& c) : Panel (u8 ("SAMPLES  ·  ENTRADA EN VIVO"), Colours::ice), engine (e)
    {
        auto& p = e.params;
        for (int i = 0; i < Sampler::kSlots; ++i)
        {
            auto* b = pads.add (new PressButton (juce::String (i + 1), Colours::ice));
            b->onPress = [&c, i] { c.perform ((A) ((int) A::Smp1 + i), Hold::Mouse, true); };
            b->onRightClick = [this, i] { if (onChooseSample) onChooseSample (i); };
            b->litWhen = [this, i] { return engine.getSampler().isPlaying (i); };
            b->setTooltip (u8 ("Clic: disparar. Clic derecho o arrastrar un archivo: cargar sample."));
            addAndMakeVisible (b);
        }
        auto knob = [&] (P param, const char* name) { auto* k = knobs.add (new ParamKnob (p, param, u8 (name), Colours::ice)); addAndMakeVisible (k); };
        knob (P::SampLevel, "VOLUMEN");
        knob (P::SampEcho, "AL ECHO");
        knob (P::InGain, "ENTRADA");
        knob (P::InEcho, "AL ECHO");
        stop = std::make_unique<ActionButton> (c, A::SmpStop, "STOP", Colours::red);
        inOn = std::make_unique<ParamToggle> (p, P::InOn, "MIC / LINE", Colours::green);
        addAndMakeVisible (*stop); addAndMakeVisible (*inOn);
    }

    std::function<void (int slot)> onChooseSample;
    std::function<void (int slot, const juce::File&)> onDropSample;

    void refresh() override
    {
        for (int i = 0; i < pads.size(); ++i)
        {
            pads[i]->refresh();
            auto name = engine.getSampleName (i);
            const auto label = name.isEmpty() ? juce::String (i + 1) : juce::String (i + 1) + " " + name;
            if (pads[i]->getButtonText() != label) pads[i]->setButtonText (label);
            pads[i]->setColour (juce::TextButton::textColourOffId, name.isEmpty() ? Colours::textDim : Colours::text);
        }
        for (auto* k : knobs) k->refresh();
        stop->refresh(); inOn->refresh();
    }

    void resized() override
    {
        auto r = content();
        auto right = r.removeFromRight (120);
        auto top = right.removeFromTop (right.getHeight() / 2);
        stop->setBounds (top.removeFromBottom (20).reduced (1));
        knobs[0]->setBounds (top.removeFromLeft (60)); knobs[1]->setBounds (top);
        inOn->setBounds (right.removeFromBottom (20).reduced (1));
        knobs[2]->setBounds (right.removeFromLeft (60)); knobs[3]->setBounds (right);
        r.removeFromRight (6);
        const int pw = r.getWidth() / 4, ph = r.getHeight() / 4;
        for (int i = 0; i < pads.size(); ++i)
            pads[i]->setBounds (juce::Rectangle<int> (r.getX() + (i % 4) * pw, r.getY() + (i / 4) * ph, pw, ph).reduced (2));
    }

    bool isInterestedInFileDrag (const juce::StringArray& f) override { return Library::isAudioFile (juce::File (f[0])); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        int slot = -1;
        for (int i = 0; i < pads.size(); ++i) if (pads[i]->getBounds().contains (x, y)) slot = i;
        if (slot < 0) slot = 0;
        for (int i = 0; i < files.size() && slot + i < Sampler::kSlots; ++i)
            if (onDropSample) onDropSample (slot + i, juce::File (files[i]));
    }

private:
    Engine& engine;
    juce::OwnedArray<PressButton> pads;
    juce::OwnedArray<ParamKnob> knobs;
    std::unique_ptr<ActionButton> stop;
    std::unique_ptr<ParamToggle> inOn;
};

// ---------------------------------------------------------------------------
class LibraryView : public Panel, private juce::ChangeListener
{
public:
    LibraryView (Library& l) : Panel ("BIBLIOTECA", Colours::text), library (l), folderModel (*this), trackModel (*this)
    {
        library.addChangeListener (this);
        folderList.setModel (&folderModel);
        trackList.setModel (&trackModel);
        for (auto* lb : { &folderList, &trackList })
        {
            lb->setRowHeight (22);
            lb->setColour (juce::ListBox::backgroundColourId, Colours::bg);
            lb->setWantsKeyboardFocus (false);
            addAndMakeVisible (lb);
        }
        search.setTextToShowWhenEmpty (u8 ("buscar…"), Colours::textDim);
        search.onTextChange = [this] { library.setSearch (search.getText()); };
        search.onEscapeKey = [this] { search.clear(); library.setSearch ({}); unfocusAllComponents(); };
        search.onReturnKey = [this] { unfocusAllComponents(); };
        addAndMakeVisible (search);

        sortB.onClick = [this] { library.setSortByDate (! library.isSortByDate()); };
        for (auto* b : { &addB, &removeB, &rescanB, &load1, &load2, &sortB })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        load1.setColour (juce::TextButton::buttonColourId, Colours::deckA.withAlpha (0.35f));
        load2.setColour (juce::TextButton::buttonColourId, Colours::deckB.withAlpha (0.35f));
        addB.onClick = [this] { chooseFolder(); };
        rescanB.onClick = [this] { library.rescan(); };
        removeB.onClick = [this]
        {
            juce::PopupMenu m;
            const auto roots = library.getRoots();
            for (int i = 0; i < roots.size(); ++i) m.addItem (i + 1, roots[i]);
            if (roots.isEmpty()) m.addItem (-1, "No hay carpetas", false);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&removeB),
                             [this, roots] (int r) { if (r > 0) library.removeRoot (roots[r - 1]); });
        };
        load1.onClick = [this] { if (onLoad) onLoad (0); };
        load2.onClick = [this] { if (onLoad) onLoad (1); };
        status.setFont (juce::FontOptions (10.5f));
        status.setColour (juce::Label::textColourId, Colours::textDim);
        status.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (status);
        changeListenerCallback (nullptr);
    }

    ~LibraryView() override { library.removeChangeListener (this); }

    std::function<void (int deck)> onLoad;
    bool isSearchFocused() const { return search.hasKeyboardFocus (true); }

    void resized() override
    {
        auto r = content();
        auto top = r.removeFromTop (24);
        load2.setBounds (top.removeFromRight (64).reduced (1));
        load1.setBounds (top.removeFromRight (64).reduced (1));
        top.removeFromRight (8);
        rescanB.setBounds (top.removeFromRight (30).reduced (1));
        sortB.setBounds (top.removeFromRight (110).reduced (1));
        removeB.setBounds (top.removeFromRight (64).reduced (1));
        addB.setBounds (top.removeFromRight (96).reduced (1));
        top.removeFromRight (8);
        search.setBounds (top.removeFromLeft (juce::jmin (260, top.getWidth())).reduced (0, 1));
        status.setBounds (getLocalBounds().reduced (10, 4).removeFromTop (16));
        r.removeFromTop (4);
        folderList.setBounds (r.removeFromLeft (r.getWidth() * 34 / 100));
        r.removeFromLeft (4);
        trackList.setBounds (r);
    }

    void paintExtra (juce::Graphics& g) override
    {
        // marco de foco: la lista que mueve el pote BROWSE
        g.setColour (Colours::amber.withAlpha (0.7f));
        const auto& lb = library.isTrackFocus() || library.getSearch().isNotEmpty() ? trackList : folderList;
        g.drawRoundedRectangle (lb.getBounds().toFloat().expanded (1.5f), 3.0f, 1.5f);
    }

private:
    struct FolderModel : juce::ListBoxModel
    {
        explicit FolderModel (LibraryView& v) : view (v) {}
        int getNumRows() override { return (int) view.library.getFolders().size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            const auto& fs = view.library.getFolders();
            if (! juce::isPositiveAndBelow (row, (int) fs.size())) return;
            const bool focus = ! view.library.isTrackFocus() && view.library.getSearch().isEmpty();
            if (selected)
            {
                g.setColour (focus ? Colours::amber : Colours::amberDim);
                g.fillRect (0, 0, w, h);
            }
            g.setColour (selected && focus ? Colours::bg : (row == 0 ? Colours::amber : Colours::text));
            g.setFont (juce::FontOptions (12.0f, row == 0 ? juce::Font::bold : juce::Font::plain));
            g.drawText (fs[(size_t) row].name, 8, 0, w - 50, h, juce::Justification::centredLeft, true);
            g.setColour (selected && focus ? Colours::bg : Colours::textDim);
            g.drawText (juce::String (fs[(size_t) row].tracks.size()), w - 46, 0, 40, h, juce::Justification::centredRight);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            view.library.setTrackFocus (false);
            if (view.library.getSearch().isNotEmpty()) { view.search.clear(); view.library.setSearch ({}); }
            view.library.selectFolder (row);
        }
        LibraryView& view;
    };

    struct TrackModel : juce::ListBoxModel
    {
        explicit TrackModel (LibraryView& v) : view (v) {}
        int getNumRows() override { return view.library.getVisibleTracks().size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            const auto& ts = view.library.getVisibleTracks();
            if (! juce::isPositiveAndBelow (row, ts.size())) return;
            const bool focus = view.library.isTrackFocus() || view.library.getSearch().isNotEmpty();
            if (selected)
            {
                g.setColour (focus ? Colours::amber : Colours::amberDim);
                g.fillRect (0, 0, w, h);
            }
            g.setColour (selected && focus ? Colours::bg : Colours::text);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (ts[row].getFileNameWithoutExtension(), 8, 0, w - 130, h, juce::Justification::centredLeft, true);
            g.setColour (selected && focus ? Colours::bg : Colours::textDim);
            g.setFont (juce::FontOptions (10.5f));
            const auto ms = view.library.getModTime (ts[row]);
            if (ms > 0)
                g.drawText (juce::Time (ms).formatted ("%d/%m/%y"), w - 120, 0, 66, h, juce::Justification::centredRight);
            g.drawText (ts[row].getFileExtension().substring (1).toUpperCase(), w - 50, 0, 44, h, juce::Justification::centredRight);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent&) override { view.library.setTrackFocus (true); view.library.selectTrack (row); }
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            view.library.selectTrack (row);
            if (view.onLoad) view.onLoad (0);
        }
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
        {
            const auto& ts = view.library.getVisibleTracks();
            if (rows.isEmpty() || ! juce::isPositiveAndBelow (rows[0], ts.size())) return {};
            return "track:" + ts[rows[0]].getFullPathName();
        }
        LibraryView& view;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        folderList.updateContent();
        trackList.updateContent();
        folderList.selectRow (library.getSelectedFolder(), false, true);
        folderList.scrollToEnsureRowIsOnscreen (library.getSelectedFolder());
        sortB.setButtonText (library.isSortByDate() ? u8 ("ORDEN: FECHA ↓") : u8 ("ORDEN: NOMBRE"));
        sortB.setToggleState (library.isSortByDate(), juce::dontSendNotification);
        if (library.getSelectedTrack() >= 0)
        {
            trackList.selectRow (library.getSelectedTrack(), false, true);
            trackList.scrollToEnsureRowIsOnscreen (library.getSelectedTrack());
        }
        else trackList.deselectAllRows();
        folderList.repaint(); trackList.repaint();

        juce::String s;
        if (library.isScanning()) s = "escaneando carpetas...";
        else if (library.getRoots().isEmpty()) s = u8 ("agregá una carpeta con tu música");
        else s = juce::String (library.getNumTracksTotal()) + " temas";
        status.setText (s, juce::dontSendNotification);
        repaint();
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Elegir carpeta de música",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.isDirectory()) library.addRoot (f);
                              });
    }

    Library& library;
    FolderModel folderModel;
    TrackModel trackModel;
    juce::ListBox folderList, trackList;
    juce::TextEditor search;
    juce::TextButton addB { "+ CARPETA" }, removeB { "QUITAR" }, rescanB { u8 ("↻") }, load1 { u8 ("◀ DECK 1") }, load2 { u8 ("DECK 2 ▶") }, sortB;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace ct
