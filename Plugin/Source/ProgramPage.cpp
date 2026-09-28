#include "ProgramPage.h"
#include "NativeFileDialog.h"

using s950::KeygroupParam;
using s950::KeygroupParamInfo;

namespace
{
    /// The Studio's names: middle C, note 60, is C3.
    juce::String noteName (int note)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 2);
    }

    // Cyan for the "sounding" readouts - they are the Perform OFFSETS showing through - and
    // amber for the chosen keygroup, which is on the disk. The same two meanings as Look.h.
    const juce::Colour accent      = look::perform;
    const juce::Colour selectedBar = look::program;

    /// How a value reads under its knob.
    juce::String describe (KeygroupParam p, int v)
    {
        const auto& info = s950::keygroupParamInfo (p);

        switch (p)
        {
            case KeygroupParam::HighKey:
            case KeygroupParam::LowKey:
                return noteName (v) + " (" + juce::String (v) + ")";

            case KeygroupParam::VelocitySwitch:
                return v >= 128 ? juce::String ("off") : juce::String (v);

            // Transpose and fine are the high and low bytes of one signed count of sixteenths
            // of a semitone (Disk::Zone::pitchOffset), so each knob says what it is worth.
            case KeygroupParam::Zone1Fine:
            case KeygroupParam::Zone2Fine:
                return juce::String (v) + " (+" + juce::String (v / 16.0, 2) + " st)";

            case KeygroupParam::Zone1Transpose:
            case KeygroupParam::Zone2Transpose:
                return (v > 0 ? "+" : "") + juce::String (v) + " (" + (v > 0 ? "+" : "") + juce::String (v * 16) + " st)";

            default:
                break;
        }

        if (info.kind == KeygroupParamInfo::Signed)
            return (v > 0 ? "+" : "") + juce::String (v);

        return juce::String (v);
    }
}

// ============================================================================ the strip

void KeygroupStrip::setKeygroups (std::vector<Range> r)
{
    ranges = std::move (r);

    /*
     * Lanes, greedily: each keygroup goes in the first lane where it overlaps nothing
     * already there. Keygroups that share keys - a crossfade, a layer - end up one above
     * the other instead of one hiding the other.
     */
    lane.assign (ranges.size(), 0);
    lanes = 1;

    for (size_t i = 0; i < ranges.size(); ++i)
    {
        for (int l = 0;; ++l)
        {
            bool clash = false;

            for (size_t j = 0; j < i; ++j)
                if (lane[j] == l && ranges[j].low <= ranges[i].high && ranges[i].low <= ranges[j].high)
                    clash = true;

            if (! clash) { lane[i] = l; lanes = std::max (lanes, l + 1); break; }
        }
    }

    repaint();
}

void KeygroupStrip::setSelected (int k)
{
    selected = k;
    repaint();
}

void KeygroupStrip::setActivity (std::vector<bool> nowLit, std::vector<int> nowNotes)
{
    if (nowLit == lit && nowNotes == notes) return;     // nothing moved: no repaint

    lit   = std::move (nowLit);
    notes = std::move (nowNotes);
    repaint();
}

void KeygroupStrip::setHeld (std::vector<bool> nowHeld)
{
    if (nowHeld == held) return;
    held = std::move (nowHeld);
    repaint();
}

/*
 * The keyboard runs from C0 (note 24) to G8 (127), which is the keyboard the S950 has: Simon
 * reports it plays nothing below C0, and the two octaves under it that MIDI allows were a
 * fifth of the strip spent on keys the machine has no answer for. A keygroup whose stored low
 * key is under 24 is drawn from the left edge.
 *
 * The strip's height, top to bottom: the keygroup bars, the keys, the octave names.
 */
namespace
{
    constexpr float labelH    = 14.0f;
    constexpr float keysH     = 44.0f;
    constexpr float barsGap   = 4.0f;
    constexpr float blackFrac = 0.6f;    // a black key's width, of a white key's
    constexpr float blackLen  = 0.62f;   // and its length, of the keyboard's height

    const juce::Colour whiteKey { 0xffc9ced6 };
    const juce::Colour blackKey { 0xff15171b };
    const juce::Colour heldDot  { 0xff3b82f6 };    // blue: a key held over MIDI
}

juce::Rectangle<float> KeygroupStrip::barsArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (labelH + keysH + barsGap).reduced (2.0f, 2.0f);
}

juce::Rectangle<float> KeygroupStrip::keysArea() const
{
    auto a = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
    return { a.getX(), a.getBottom() - labelH - keysH, a.getWidth(), keysH };
}

bool KeygroupStrip::isBlack (int note)
{
    switch (note % 12) { case 1: case 3: case 6: case 8: case 10: return true; default: return false; }
}

int KeygroupStrip::whiteCount() const
{
    int n = 0;
    for (int k = firstKey; k <= lastKey; ++k) if (! isBlack (k)) ++n;
    return n;
}

float KeygroupStrip::whiteWidth() const
{
    return keysArea().getWidth() / (float) whiteCount();
}

/// Where a key starts and ends along the axis: a white key its slot, a black key the narrow
/// span centred on the join between the white keys either side of it.
float KeygroupStrip::keyLeft (int note) const
{
    note = juce::jlimit (firstKey, lastKey, note);
    int whitesBefore = 0;
    for (int k = firstKey; k < note; ++k) if (! isBlack (k)) ++whitesBefore;

    const float ww = whiteWidth(), x0 = keysArea().getX();
    return isBlack (note) ? x0 + whitesBefore * ww - ww * blackFrac * 0.5f
                          : x0 + whitesBefore * ww;
}

float KeygroupStrip::keyRight (int note) const
{
    const float ww = whiteWidth();
    return keyLeft (note) + (isBlack (juce::jlimit (firstKey, lastKey, note)) ? ww * blackFrac : ww);
}

juce::Rectangle<float> KeygroupStrip::keyRect (int note) const
{
    const auto a = keysArea();
    const float l = keyLeft (note), r = keyRight (note);
    return isBlack (note) ? juce::Rectangle<float> (l, a.getY(), r - l, a.getHeight() * blackLen)
                          : juce::Rectangle<float> (l, a.getY(), r - l, a.getHeight());
}

juce::Rectangle<float> KeygroupStrip::barFor (int k) const
{
    const auto area  = barsArea();
    const float laneH = area.getHeight() / (float) lanes;
    const auto& r     = ranges[(size_t) k];

    const int lo = juce::jlimit (firstKey, lastKey, std::min (r.low, r.high));
    const int hi = juce::jlimit (firstKey, lastKey, std::max (r.low, r.high));

    const float left = keyLeft (lo), right = keyRight (hi);
    return { left, area.getY() + lane[(size_t) k] * laneH, right - left, laneH - 2.0f };
}

int KeygroupStrip::keygroupAt (juce::Point<float> where) const
{
    for (int k = 0; k < (int) ranges.size(); ++k)
        if (barFor (k).contains (where))
            return k;

    // On the keys: the keygroup that plays that key - the one being edited if it does,
    // otherwise the first that does.
    const int key = keyAt (where);
    if (key < 0) return -1;

    auto covers = [&] (int k)
    {
        const auto& r = ranges[(size_t) k];
        return key >= std::min (r.low, r.high) && key <= std::max (r.low, r.high);
    };

    if (selected >= 0 && selected < (int) ranges.size() && covers (selected)) return selected;
    for (int k = 0; k < (int) ranges.size(); ++k) if (covers (k)) return k;
    return -1;
}

int KeygroupStrip::keyAt (juce::Point<float> where) const
{
    if (! keysArea().contains (where)) return -1;

    // black keys lie on top, so they are asked first
    for (int n = firstKey; n <= lastKey; ++n)
        if (isBlack (n) && keyRect (n).contains (where)) return n;
    for (int n = firstKey; n <= lastKey; ++n)
        if (! isBlack (n) && keyRect (n).contains (where)) return n;
    return -1;
}

void KeygroupStrip::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto bars = barsArea();
    const auto keys = keysArea();

    // --- the lane background, with a faint line up from every C so bars read against keys
    g.setColour (juce::Colour (0xff17191d));
    g.fillRoundedRectangle (bars.expanded (2.0f), 4.0f);

    for (int n = firstKey; n <= lastKey; n += 12)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawVerticalLine ((int) keyLeft (n), bars.getY(), bars.getBottom());
    }

    // --- the keyboard. How strongly a key is shaded: the selected keygroup's keys fully
    // (every keygroup's, with "All keygroups"), the other keygroups' faintly, none bare.
    auto shadeOf = [this] (int note)
    {
        float best = 0.0f;
        for (int k = 0; k < (int) ranges.size(); ++k)
        {
            const auto& r = ranges[(size_t) k];
            if (note < std::min (r.low, r.high) || note > std::max (r.low, r.high)) continue;
            best = std::max (best, (selected < 0 || selected == k) ? 1.0f : 0.28f);
        }
        return best;
    };

    for (int n = firstKey; n <= lastKey; ++n)              // white keys first, black on top
    {
        if (isBlack (n)) continue;
        const auto r = keyRect (n);
        const float s = shadeOf (n);
        g.setColour (s > 0.0f ? whiteKey.interpolatedWith (selectedBar, 0.62f * s) : whiteKey.darker (0.25f));
        g.fillRect (r.reduced (0.5f, 0.0f));
    }
    g.setColour (look::window);
    for (int n = firstKey; n <= lastKey; ++n)
        if (! isBlack (n)) g.drawVerticalLine ((int) keyLeft (n), keys.getY(), keys.getBottom());

    for (int n = firstKey; n <= lastKey; ++n)
    {
        if (! isBlack (n)) continue;
        const auto r = keyRect (n);
        const float s = shadeOf (n);
        g.setColour (s > 0.0f ? blackKey.interpolatedWith (selectedBar.darker (0.35f), 0.75f * s) : blackKey);
        g.fillRoundedRectangle (r.withTrimmedTop (-2.0f), 1.5f);
    }

    // --- a blue dot on every key held over MIDI, low on the key where a finger would be
    const float dotR = juce::jlimit (2.0f, 4.0f, whiteWidth() * 0.32f);
    for (int n = firstKey; n <= lastKey; ++n)
    {
        if (n >= (int) held.size() || ! held[(size_t) n]) continue;
        const auto r = keyRect (n);
        const juce::Point<float> c (r.getCentreX(), r.getBottom() - dotR - 3.0f);
        g.setColour (heldDot);
        g.fillEllipse (c.x - dotR, c.y - dotR, dotR * 2.0f, dotR * 2.0f);
        g.setColour (isBlack (n) ? juce::Colours::white.withAlpha (0.8f) : look::window.withAlpha (0.7f));
        g.drawEllipse (c.x - dotR, c.y - dotR, dotR * 2.0f, dotR * 2.0f, 1.0f);
    }

    // --- every C named under the keys
    g.setFont (look::font (10.0f));
    g.setColour (look::dim);
    for (int n = firstKey; n <= lastKey; n += 12)
        g.drawText (noteName (n), juce::Rectangle<float> (keyLeft (n), area.getBottom() - labelH, 30.0f, labelH),
                    juce::Justification::centredLeft, false);

    // --- the keygroup bars
    for (int k = 0; k < (int) ranges.size(); ++k)
    {
        const auto bar     = barFor (k);
        const bool on      = selected < 0 || selected == k;
        const bool playing = k < (int) lit.size() && lit[(size_t) k];

        // Playing: the same colour, brighter, with a light rim - so which keygroup a note
        // went to is visible whether or not it is the one being edited.
        const auto fill = on ? selectedBar : look::raisedEdge;
        g.setColour (playing ? fill.brighter (on ? 0.35f : 0.6f) : fill);
        g.fillRoundedRectangle (bar, 3.0f);

        if (playing)
        {
            g.setColour (look::text);
            g.drawRoundedRectangle (bar.reduced (1.0f), 3.0f, 2.0f);

            // and a marker at the key it answered
            const int note = k < (int) notes.size() ? notes[(size_t) k] : -1;
            if (note >= firstKey && note <= lastKey)
            {
                const float x = (keyLeft (note) + keyRight (note)) * 0.5f;
                g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY() + 3.0f, 2.0f, bar.getHeight() - 6.0f));
            }
        }

        g.setColour (on ? look::window : look::text);
        g.setFont (look::bold (11.0f));
        if (bar.getWidth() > 14.0f)
            g.drawText (juce::String (k + 1), bar, juce::Justification::centred, false);
    }
}

void KeygroupStrip::mouseDown (const juce::MouseEvent& e)
{
    const int k = keygroupAt (e.position);
    if (k >= 0 && onSelect != nullptr)
        onSelect (k);
}

void KeygroupStrip::mouseMove (const juce::MouseEvent& e)
{
    const int k   = keygroupAt (e.position);
    const int key = keyAt (e.position);

    // over a key, its name first - and "no keygroup" if nothing plays it
    const juce::String keyText = key >= 0 ? noteName (key) + " (" + juce::String (key) + ")   -   " : juce::String();

    if (k < 0) { setTooltip (key >= 0 ? keyText + "no keygroup plays this key" : juce::String()); return; }

    const auto& r = ranges[(size_t) k];
    setTooltip (keyText + "Keygroup " + juce::String (k + 1) + ": " + noteName (r.low) + " to "
                + noteName (r.high) + (r.sample.isNotEmpty() ? "   -   " + r.sample : juce::String()));
}

// ============================================================================= the page

ProgramPage::ProgramPage (VirtualS950Processor& p) : processor (p)
{
    // The panels first, so they are behind everything added after them.
    addAndMakeVisible (keygroupPanel);
    addAndMakeVisible (pagePanel);

    using P = KeygroupParam;

    // --- envelopes: the amplitude one, then the filter one on a row of its own
    add (P::VcaAttack,  envelopes, "VCA Attack",  "vcaAttack");
    add (P::VcaDecay,   envelopes, "VCA Decay",   "vcaDecay");
    add (P::VcaSustain, envelopes, "VCA Sustain", "vcaSustain");
    add (P::VcaRelease, envelopes, "VCA Release", "vcaRelease");
    add (P::VcfAttack,  envelopes, "VCF Attack",  "vcfAttack");
    add (P::VcfDecay,   envelopes, "VCF Decay",   "vcfDecay");
    add (P::VcfSustain, envelopes, "VCF Sustain", "vcfSustain");
    add (P::VcfRelease, envelopes, "VCF Release", "vcfRelease");

    // --- filter. "Soft" and "hard" are the two zones: the sample under the velocity switch
    // and the one above it.
    add (P::Zone1Filter,  filter, "Soft filter", "vcfCutoff");
    add (P::Zone2Filter,  filter, "Hard filter", "vcfCutoff", true);
    add (P::VcfAmount,    filter, "VCF amount",  "vcfAmount");
    add (P::KeyToFilter,  filter, "Key track");
    add (P::VelToFilter,  filter, "Vel > filter", "velToFilter");

    // --- LFO
    add (P::LfoRate,       lfo, "Rate",       "lfoRate");
    add (P::LfoDepth,      lfo, "Depth",      "lfoDepth");
    add (P::LfoDelay,      lfo, "Delay",      "lfoDelay");
    add (P::LfoModwheel,   lfo, "Mod wheel");
    add (P::LfoAftertouch, lfo, "Aftertouch");
    add (P::LfoDesync,     lfo, "Desync");

    // --- velocity and loudness
    add (P::VelToLoudness,     velocity, "Vel > level",   "velToLoudness");
    add (P::VelToAttack,       velocity, "Vel > attack");
    add (P::VelToRelease,      velocity, "Vel > release");
    add (P::VelocityReleaseOn, velocity, "Release on");
    add (P::VelocitySwitch,    velocity, "Vel switch");
    add (P::Zone1Loudness,     velocity, "Soft level");
    add (P::Zone2Loudness,     velocity, "Hard level", nullptr, true);

    // --- tuning, and warp, which is a pitch bend at the strike
    add (P::Zone1Transpose, tuning, "Soft transp");
    add (P::Zone1Fine,      tuning, "Soft fine");
    add (P::Zone2Transpose, tuning, "Hard transp", nullptr, true);
    add (P::Zone2Fine,      tuning, "Hard fine",   nullptr, true);
    add (P::ConstantPitch,  tuning, "Const pitch");
    add (P::WarpDepth,      tuning, "Warp depth");
    add (P::WarpVelocity,   tuning, "Warp vel");
    add (P::WarpTime,       tuning, "Warp time");

    // --- which keys, and where it comes out
    add (P::LowKey,     keys, "Low key");
    add (P::HighKey,    keys, "High key");
    add (P::OutputPort, keys, "Output");
    add (P::OneShot,    keys, "One shot");

    for (const char* name : { "Envelopes", "Filter", "LFO", "Velocity", "Tuning", "Keys & output" })
        pages.addTab (name, look::program, -1);

    pages.setCurrentTabIndex (0, false);
    pages.addChangeListener (this);
    addAndMakeVisible (pages);

    strip.onSelect = [this] (int k) { choose (k); };
    strip.setTooltip ("The programme's keygroups across the keyboard. Click one to edit it.");
    addAndMakeVisible (strip);

    allButton.setClickingTogglesState (false);
    look::accent (allButton, look::program);
    look::accent (saveButton, look::program);
    allButton.setTooltip ("Edit every keygroup of the programme at once. A control then shows "
                          "keygroup 1's value, marked * where the others differ; moving it sets "
                          "them all to the same value.");
    allButton.onClick = [this] { choose (selected < 0 ? 0 : -1); };
    addAndMakeVisible (allButton);

    saveButton.setTooltip ("Write the disk, with every edit, as a plain .img - which the Studio, "
                           "this plugin, and a Gotek or HxC floppy emulator all open. Edits are "
                           "saved with the Live set anyway; this is for taking them elsewhere.");
    saveButton.onClick = [this] { saveDisk(); };
    addAndMakeVisible (saveButton);

    heading.setText ("What is on the disk: absolute settings, in the S950's own units, saved "
                     "with the set and written into the disk.",
                     juce::dontSendNotification);
    heading.setFont (look::font (11.5f));
    heading.setColour (juce::Label::textColourId, look::dim);
    addAndMakeVisible (heading);

    detail.setFont (look::font (12.5f));
    addAndMakeVisible (detail);

    blankNote.setText ("This keygroup has no filter envelope on the disk (an S900 programme). "
                       "Moving a VCF stage, or the amount, writes a flat one to start from.",
                       juce::dontSendNotification);
    blankNote.setFont (look::font (11.5f));
    blankNote.setColour (juce::Label::textColourId, look::dim);
    addChildComponent (blankNote);

    emptyNote.setText ("Load a disk to edit its programmes. The placeholder saw is not on a "
                       "disk, so there is nothing here to change.",
                       juce::dontSendNotification);
    emptyNote.setJustificationType (juce::Justification::centred);
    emptyNote.setFont (look::font (12.5f));
    emptyNote.setColour (juce::Label::textColourId, look::dim);
    addChildComponent (emptyNote);

    showPage (envelopes);
    refresh();
    startTimerHz (30);
}

ProgramPage::~ProgramPage()
{
    stopTimer();
    pages.removeChangeListener (this);
}

void ProgramPage::timerCallback()
{
    if (! isShowing() || count <= 0)
        return;

    const double now   = juce::Time::getMillisecondCounterHiRes();
    const double flash = 180.0;

    if ((int) seenHits.size() != count)
    {
        // A new programme: start from its counts, or every keygroup would flash at once.
        seenHits.assign ((size_t) count, 0u);
        flashUntil.assign ((size_t) count, 0.0);
        for (int k = 0; k < count; ++k)
            seenHits[(size_t) k] = processor.getKeygroupActivity (k).hits;
    }

    std::vector<bool> lit ((size_t) count);
    std::vector<int>  notes ((size_t) count, -1);

    for (int k = 0; k < count; ++k)
    {
        const auto a = processor.getKeygroupActivity (k);
        auto& seen   = seenHits[(size_t) k];
        auto& until  = flashUntil[(size_t) k];

        if (a.hits != seen) { seen = a.hits; until = now + flash; }

        lit[(size_t) k]   = a.sounding || now < until;
        notes[(size_t) k] = a.lastNote;
    }

    strip.setActivity (std::move (lit), std::move (notes));

    // the keys held over MIDI, for the dots on the keyboard
    std::vector<bool> held (128);
    for (int n = 0; n < 128; ++n) held[(size_t) n] = processor.isNoteHeld (n);
    strip.setHeld (std::move (held));
}

void ProgramPage::add (KeygroupParam p, int onPage, const char* name, const char* trimId, bool zone2)
{
    auto c = std::make_unique<Control>();
    c->param  = p;
    c->page   = onPage;
    c->name   = name;
    c->trimId = trimId;
    c->zone2  = zone2;

    const auto& info = s950::keygroupParamInfo (p);
    Control* raw = c.get();

    const juce::String tip = juce::String (info.name) + ", as stored in the keygroup: "
                           + juce::String (info.lo) + " to " + juce::String (info.hi) + "."
                           + (trimId != nullptr
                                ? juce::String (" The matching Perform offset adds to it while "
                                                "you play; what sounds shows beside it in blue.")
                                : juce::String());

    if (info.kind == KeygroupParamInfo::Bit)
    {
        c->toggle = std::make_unique<juce::ToggleButton> ("On");
        c->toggle->setTooltip (tip);
        c->toggle->setTitle (name);
        look::accent (*c->toggle, look::program);
        c->toggle->onClick = [this, raw] { write (*raw, raw->toggle->getToggleState() ? 1 : 0); };
        addChildComponent (*c->toggle);
    }
    else if (info.kind == KeygroupParamInfo::Port)
    {
        c->combo = std::make_unique<juce::ComboBox>();
        c->combo->addItem ("ALL", 1);
        for (int m = 1; m <= 8; ++m) c->combo->addItem ("MONO " + juce::String (m), m + 1);
        c->combo->addItem ("LEFT", 10);
        c->combo->addItem ("RIGHT", 11);
        c->combo->setTooltip (tip);
        c->combo->setTitle (name);
        c->combo->onChange = [this, raw] { write (*raw, raw->combo->getSelectedId() - 1); };
        addChildComponent (*c->combo);
    }
    else
    {
        c->slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                    juce::Slider::TextBoxBelow);
        c->slider->setRange (info.lo, info.hi, 1.0);
        c->slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 14);
        c->slider->textFromValueFunction = [p] (double v) { return describe (p, juce::roundToInt (v)); };
        c->slider->valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
        c->slider->setTooltip (tip);
        c->slider->setTitle (name);        // what a screen reader says, rather than "slider"
        look::accent (*c->slider, look::program);
        c->slider->onValueChange = [this, raw] { write (*raw, juce::roundToInt (raw->slider->getValue())); };
        addChildComponent (*c->slider);
    }

    c->label = std::make_unique<juce::Label>();
    c->label->setText (name, juce::dontSendNotification);
    c->label->setTooltip (tip);
    look::styleCaption (*c->label);
    addChildComponent (*c->label);

    c->sounding = std::make_unique<juce::Label>();
    c->sounding->setJustificationType (juce::Justification::centred);
    c->sounding->setFont (look::bold (11.0f));
    c->sounding->setColour (juce::Label::textColourId, accent);
    c->sounding->setTooltip ("What is actually sounding: this setting plus the Perform offset.");
    addChildComponent (*c->sounding);

    controls.push_back (std::move (c));
}

void ProgramPage::write (Control& c, int value)
{
    if (updating) return;

    processor.setKeygroupValue (selected, c.param, value);

    // Read straight back rather than waiting for the editor's timer, so the control and
    // its readouts never show a moment of the old value.
    refresh();
}

void ProgramPage::choose (int keygroup)
{
    selected = keygroup;
    refresh();
}

void ProgramPage::showPage (int p)
{
    page = juce::jlimit (0, pageCount - 1, p);
    resized();
    refresh();
}

void ProgramPage::refresh()
{
    count = processor.getKeygroupCount();

    const bool haveDisk = count > 0;
    emptyNote.setVisible (! haveDisk);

    for (auto* c : std::initializer_list<juce::Component*> { &strip, &allButton, &saveButton,
                                                             &heading, &detail, &pages,
                                                             &keygroupPanel, &pagePanel })
        c->setVisible (haveDisk);

    if (! haveDisk)
    {
        blankNote.setVisible (false);
        for (auto& c : controls)
            for (juce::Component* part : std::initializer_list<juce::Component*> {
                     c->slider.get(), c->toggle.get(), c->combo.get(), c->label.get(), c->sounding.get() })
                if (part != nullptr) part->setVisible (false);
        return;
    }

    if (selected >= count) selected = 0;

    // --- the strip
    std::vector<KeygroupStrip::Range> ranges;
    for (int k = 0; k < count; ++k)
    {
        const auto soft = processor.getZoneSample (k, 1);
        const auto hard = processor.getZoneSample (k, 2);

        ranges.push_back ({ processor.getKeygroupValue (k, KeygroupParam::LowKey),
                            processor.getKeygroupValue (k, KeygroupParam::HighKey),
                            soft + (hard.isNotEmpty() && hard != soft ? " / " + hard : juce::String()) });
    }
    strip.setKeygroups (ranges);
    strip.setSelected (selected);

    allButton.setToggleState (selected < 0, juce::dontSendNotification);   // the look lights it

    // --- the line under it
    const int k = shown();
    const bool hasHard = processor.getZoneSample (k, 2).isNotEmpty();

    if (selected < 0)
        detail.setText ("All " + juce::String (count) + " keygroups   -   a change sets every one "
                        "to the same value", juce::dontSendNotification);
    else
    {
        const auto soft = processor.getZoneSample (k, 1);
        const auto hard = processor.getZoneSample (k, 2);

        detail.setText ("Keygroup " + juce::String (k + 1) + " of " + juce::String (count)
                        + "   -   " + noteName (ranges[(size_t) k].low) + " to "
                        + noteName (ranges[(size_t) k].high)
                        + "   -   soft: " + (soft.isNotEmpty() ? soft : juce::String ("-"))
                        + "   hard: " + (hard.isNotEmpty() ? hard : juce::String ("-")),
                        juce::dontSendNotification);
    }

    blankNote.setVisible (page == envelopes && processor.isVcfBlank (k) && selected >= 0);

    // --- every control on the page
    const juce::ScopedValueSetter<bool> filling (updating, true);

    for (auto& c : controls)
    {
        const bool onPage = c->page == page;

        c->value  = processor.getKeygroupValue (k, c->param);
        c->varies = false;

        if (selected < 0)
            for (int other = 1; other < count && ! c->varies; ++other)
                c->varies = processor.getKeygroupValue (other, c->param) != c->value;

        // A second-zone control on a keygroup with no second zone has nothing to act on.
        const bool usable = ! c->zone2 || hasHard || selected < 0;

        c->label->setText (juce::String (c->name).toUpperCase() + (c->varies ? " *" : ""),
                           juce::dontSendNotification);
        c->label->setVisible (onPage);
        c->sounding->setVisible (onPage);

        if (c->slider != nullptr)
        {
            c->slider->setValue (c->value, juce::dontSendNotification);

            // A Slider only redraws its text when the value CHANGES, so one that starts at the
            // value it already had - a low key of 0 - would keep reading "0" rather than
            // "C-2 (0)". Asking for the text again costs nothing.
            c->slider->updateText();
            c->slider->setEnabled (usable);
            c->slider->setVisible (onPage);
        }
        if (c->toggle != nullptr)
        {
            c->toggle->setToggleState (c->value != 0, juce::dontSendNotification);
            c->toggle->setVisible (onPage);
        }
        if (c->combo != nullptr)
        {
            c->combo->setSelectedId (c->value + 1, juce::dontSendNotification);
            c->combo->setVisible (onPage);
        }
        c->label->setEnabled (usable);
    }

    refreshSounding();
}

void ProgramPage::refreshSounding()
{
    for (auto& c : controls)
    {
        if (c->trimId == nullptr) { c->sounding->setText ({}, juce::dontSendNotification); continue; }

        const auto* trim = processor.parameters.getRawParameterValue (c->trimId);
        const int   by   = trim != nullptr ? juce::roundToInt (trim->load()) : 0;

        if (by == 0) { c->sounding->setText ({}, juce::dontSendNotification); continue; }

        const bool signedAmount = c->param == KeygroupParam::VcfAmount;
        const int  now = juce::jlimit (signedAmount ? -50 : 0, signedAmount ? 50 : 99, c->value + by);

        c->sounding->setText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + describe (c->param, now),
                              juce::dontSendNotification);
    }
}

void ProgramPage::saveDisk()
{
    auto start = juce::File (processor.getDiskPath());
    const auto name = processor.getDiskName().isNotEmpty()
                        ? juce::File (processor.getDiskName()).getFileNameWithoutExtension()
                        : juce::String ("disk");

    const auto folder = start.existsAsFile() ? start.getParentDirectory()
                                             : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    // The Windows dialog, owned by the plugin window - for the reason openDisk gives.
    nativeDialog::chooseFileToSave (*this, "Save the disk, with its edits, as an image",
                                    folder.getChildFile (name + "-edited.img"),
                                    { "Akai S900/S950 disk image (*.img)", "*.img" }, "img",
                                    [this] (const juce::File& chosen)
    {
        auto file = chosen;
        if (file == juce::File()) return;

        if (! file.hasFileExtension ("img"))
            file = file.withFileExtension ("img");

        juce::String error;
        if (! processor.saveDiskAs (file, error))
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                         "Could not save the disk", error, this);
    });
}

void ProgramPage::paint (juce::Graphics&) {}

void ProgramPage::resized()
{
    auto r = getLocalBounds();

    emptyNote.setBounds (r);

    heading.setBounds (r.removeFromTop (16));
    r.removeFromTop (8);

    /*
     * The keygroups panel: the strip across the keyboard, and above it the buttons and the
     * line that says which keygroup is being edited.
     */
    // the strip: keygroup bars in lanes over a keyboard, which is why it is taller than a bar
    keygroupPanel.setBounds (r.removeFromTop (look::Panel::headerHeight + 28 + 6 + 112 + 8));
    {
        auto area = keygroupPanel.content();

        auto top = area.removeFromTop (26);
        saveButton.setBounds (top.removeFromRight (112));
        top.removeFromRight (8);
        allButton.setBounds (top.removeFromLeft (108));
        top.removeFromLeft (10);
        detail.setBounds (top);

        area.removeFromTop (6);
        strip.setBounds (area.removeFromTop (112));
    }

    /*
     * The settings panel, with the six pages as tabs along its top edge - the tab bar IS its
     * header, which is why this panel has no title of its own.
     */
    r.removeFromTop (10);
    pagePanel.setBounds (r);

    auto inner = pagePanel.getBounds().reduced (look::Panel::pad, 4);
    pages.setBounds (inner.removeFromTop (28));
    inner.removeFromTop (12);

    blankNote.setBounds (inner.removeFromBottom (32));
    r = inner;

    /*
     * The page's controls in rows of cells. The filter envelope starts a row of its own on
     * the Envelopes page, so the two envelopes read as two envelopes.
     *
     * The rows are laid out first and then placed as one block, centred in the panel both
     * ways, so a page of four knobs does not sit in the top-left corner of a panel sized for
     * fourteen. Every row starts at the block's left edge, so columns still line up.
     */
    const int cellW = 72, cellH = 92, colGap = 14, rowGap = 10;
    const int perRow = juce::jmax (1, (r.getWidth() + colGap) / (cellW + colGap));

    std::vector<std::vector<Control*>> rows (1);
    for (auto& c : controls)
    {
        if (c->page != page) continue;

        // Two deliberate breaks: the filter envelope under the amplitude one, and Warp under
        // the two zones' tuning - so each page reads as its groups, not as a queue of knobs.
        const bool newRow = (c->param == KeygroupParam::VcfAttack)
                         || (c->param == KeygroupParam::WarpDepth)
                         || (static_cast<int> (rows.back().size()) >= perRow);
        if (newRow && ! rows.back().empty()) rows.emplace_back();
        rows.back().push_back (c.get());
    }

    int columns = 0;
    for (const auto& row : rows) columns = juce::jmax (columns, static_cast<int> (row.size()));
    if (columns == 0) return;

    const int blockW = columns * cellW + (columns - 1) * colGap;
    const int blockH = static_cast<int> (rows.size()) * cellH + (static_cast<int> (rows.size()) - 1) * rowGap;
    const int left   = r.getX() + juce::jmax (0, (r.getWidth()  - blockW) / 2);
    int       y      = r.getY() + juce::jmax (0, (r.getHeight() - blockH) / 2);

    for (const auto& row : rows)
    {
        int x = left;
        for (auto* c : row)
        {
            juce::Rectangle<int> cell (x, y, cellW, cellH);

            c->sounding->setBounds (cell.removeFromBottom (14));
            c->label->setBounds (cell.removeFromBottom (14));

            if (c->slider != nullptr) c->slider->setBounds (cell.reduced (2));
            if (c->toggle != nullptr) c->toggle->setBounds (cell.withSizeKeepingCentre (70, 24));
            if (c->combo  != nullptr) c->combo->setBounds (cell.withSizeKeepingCentre (72, 26));

            x += cellW + colGap;
        }
        y += cellH + rowGap;
    }
}
