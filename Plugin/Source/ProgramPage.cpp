#include "ProgramPage.h"

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

    const juce::Colour accent   { 0xff5fb4ff };   // the Perform offsets, wherever they show
    const juce::Colour selectedBar { 0xffd9a441 };

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

            case KeygroupParam::Zone1Fine:
            case KeygroupParam::Zone2Fine:
                return juce::String (v) + " (+" + juce::String (juce::roundToInt (v * 100.0 / 256.0)) + " ct)";

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

juce::Rectangle<float> KeygroupStrip::barFor (int k) const
{
    auto area = getLocalBounds().toFloat().withTrimmedBottom (16.0f).reduced (2.0f, 2.0f);

    const float keyWidth = area.getWidth() / 128.0f;
    const float laneH    = area.getHeight() / (float) lanes;
    const auto& r        = ranges[(size_t) k];

    const int lo = juce::jlimit (0, 127, std::min (r.low, r.high));
    const int hi = juce::jlimit (0, 127, std::max (r.low, r.high));

    return { area.getX() + lo * keyWidth,
             area.getY() + lane[(size_t) k] * laneH,
             (hi - lo + 1) * keyWidth,
             laneH - 2.0f };
}

int KeygroupStrip::keygroupAt (juce::Point<float> where) const
{
    for (int k = 0; k < (int) ranges.size(); ++k)
        if (barFor (k).contains (where))
            return k;

    return -1;
}

void KeygroupStrip::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (area.withTrimmedBottom (16.0f), 4.0f);

    // Every C along the bottom, so a bar's position can be read as keys.
    const auto keys = area.withTrimmedBottom (16.0f).reduced (2.0f, 2.0f);
    const float keyWidth = keys.getWidth() / 128.0f;

    g.setFont (juce::FontOptions (10.0f));
    for (int n = 0; n < 128; n += 12)
    {
        const float x = keys.getX() + n * keyWidth;
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawVerticalLine ((int) x, keys.getY(), keys.getBottom());
        g.setColour (juce::Colours::grey);
        g.drawText (noteName (n), juce::Rectangle<float> (x - 1.0f, area.getBottom() - 15.0f, 30.0f, 14.0f),
                    juce::Justification::centredLeft, false);
    }

    for (int k = 0; k < (int) ranges.size(); ++k)
    {
        const auto bar = barFor (k);
        const bool on  = selected < 0 || selected == k;

        g.setColour (on ? selectedBar : juce::Colours::grey.withAlpha (0.55f));
        g.fillRoundedRectangle (bar, 3.0f);

        g.setColour (on ? juce::Colours::black : juce::Colours::white);
        g.setFont (juce::FontOptions (11.0f));
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
    const int k = keygroupAt (e.position);

    if (k < 0) { setTooltip ({}); return; }

    const auto& r = ranges[(size_t) k];
    setTooltip ("Keygroup " + juce::String (k + 1) + ": " + noteName (r.low) + " to "
                + noteName (r.high) + (r.sample.isNotEmpty() ? "   -   " + r.sample : juce::String()));
}

// ============================================================================= the page

ProgramPage::ProgramPage (VirtualS950Processor& p) : processor (p)
{
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
        pages.addTab (name, juce::Colours::transparentBlack, -1);

    pages.setCurrentTabIndex (0, false);
    pages.addChangeListener (this);
    addAndMakeVisible (pages);

    strip.onSelect = [this] (int k) { choose (k); };
    strip.setTooltip ("The programme's keygroups across the keyboard. Click one to edit it.");
    addAndMakeVisible (strip);

    allButton.setClickingTogglesState (false);
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

    heading.setText ("PROGRAM   -   what is on the disk. Absolute settings, in the S950's own "
                     "units, saved with the set and in the disk.",
                     juce::dontSendNotification);
    heading.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (heading);

    detail.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (detail);

    blankNote.setText ("This keygroup has no filter envelope on the disk (an S900 programme). "
                       "Moving a VCF stage, or the amount, writes a flat one to start from.",
                       juce::dontSendNotification);
    blankNote.setColour (juce::Label::textColourId, juce::Colours::grey);
    addChildComponent (blankNote);

    emptyNote.setText ("Load a disk to edit its programmes. The placeholder saw is not on a "
                       "disk, so there is nothing here to change.",
                       juce::dontSendNotification);
    emptyNote.setJustificationType (juce::Justification::centred);
    emptyNote.setColour (juce::Label::textColourId, juce::Colours::grey);
    addChildComponent (emptyNote);

    showPage (envelopes);
    refresh();
}

ProgramPage::~ProgramPage()
{
    pages.removeChangeListener (this);
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
        c->slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 13);
        c->slider->textFromValueFunction = [p] (double v) { return describe (p, juce::roundToInt (v)); };
        c->slider->valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
        c->slider->setTooltip (tip);
        c->slider->setTitle (name);        // what a screen reader says, rather than "slider"
        c->slider->onValueChange = [this, raw] { write (*raw, juce::roundToInt (raw->slider->getValue())); };
        addChildComponent (*c->slider);
    }

    c->label = std::make_unique<juce::Label>();
    c->label->setText (name, juce::dontSendNotification);
    c->label->setJustificationType (juce::Justification::centred);
    c->label->setTooltip (tip);
    addChildComponent (*c->label);

    c->sounding = std::make_unique<juce::Label>();
    c->sounding->setJustificationType (juce::Justification::centred);
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
                                                             &heading, &detail, &pages })
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

    allButton.setToggleState (selected < 0, juce::dontSendNotification);
    allButton.setColour (juce::TextButton::buttonColourId,
                         selected < 0 ? selectedBar.withAlpha (0.6f)
                                      : getLookAndFeel().findColour (juce::TextButton::buttonColourId));

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

        c->label->setText (juce::String (c->name) + (c->varies ? " *" : ""), juce::dontSendNotification);
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

    // In this window rather than as a native dialog, for the reason openDisk gives.
    chooser = std::make_unique<juce::FileChooser> ("Save the disk, with its edits, as an image",
                                                   folder.getChildFile (name + "-edited.img"),
                                                   "*.img", false, false, this);

    const auto mode = juce::FileBrowserComponent::saveMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync (mode, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File()) return;

        if (! file.hasFileExtension ("img"))
            file = file.withFileExtension ("img");

        juce::String error;
        if (! processor.saveDiskAs (file, error))
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                         "Could not save the disk", error);
    });
}

void ProgramPage::paint (juce::Graphics&) {}

void ProgramPage::resized()
{
    auto r = getLocalBounds();

    emptyNote.setBounds (r);

    heading.setBounds (r.removeFromTop (18));
    r.removeFromTop (6);

    auto top = r.removeFromTop (28);
    saveButton.setBounds (top.removeFromRight (120));
    top.removeFromRight (8);
    allButton.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (8);
    detail.setBounds (top);

    r.removeFromTop (6);
    strip.setBounds (r.removeFromTop (84));

    r.removeFromTop (10);
    pages.setBounds (r.removeFromTop (28));
    r.removeFromTop (12);

    blankNote.setBounds (r.removeFromBottom (36));

    /*
     * The page's controls in rows of cells, left to right. The filter envelope starts a row
     * of its own on the Envelopes page, so the two envelopes read as two envelopes.
     */
    const int cellW = 84, cellH = 118, gap = 6;
    int x = r.getX(), y = r.getY();

    for (auto& c : controls)
    {
        if (c->page != page) continue;

        const bool newRow = (c->param == KeygroupParam::VcfAttack)
                         || (x + cellW > r.getRight());
        if (newRow && x != r.getX()) { x = r.getX(); y += cellH + gap; }

        juce::Rectangle<int> cell (x, y, cellW, cellH);

        c->sounding->setBounds (cell.removeFromBottom (14));
        c->label->setBounds (cell.removeFromBottom (15));

        if (c->slider != nullptr) c->slider->setBounds (cell.reduced (2));
        if (c->toggle != nullptr) c->toggle->setBounds (cell.withSizeKeepingCentre (60, 24));
        if (c->combo  != nullptr) c->combo->setBounds (cell.withSizeKeepingCentre (80, 24));

        x += cellW + gap;
    }
}
