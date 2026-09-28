#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

/*
 * The operating system's own file dialogs, owned by the plugin's window.
 *
 * WHY NOT JUCE'S. JUCE's native chooser on Windows puts itself on the PRIMARY display, and
 * owns itself to a window of its own rather than to the plugin's, so on a machine with more
 * than one monitor it opened wherever the primary one was - often not where anyone was
 * looking - and a DAW that keeps its plugin windows always on top could cover it. The
 * browser that used to open inside the editor avoided both, at the price of being small,
 * unfamiliar and cramped.
 *
 * Owned by the plugin window's top-level ancestor, the Windows dialog is kept above that
 * window by the system - an owned window always sits above its owner, topmost owners
 * included - and it centres itself over its owner, on whichever monitor that is.
 *
 * Both calls return at once; the answer comes to `done` on the message thread, with an
 * empty File if the dialog was cancelled. Anywhere the Windows dialog cannot be shown, they
 * fall back to JUCE's own browser.
 */
namespace nativeDialog
{
    struct Filter { juce::String description, patterns; };   // e.g. "Disk images", "*.hfe;*.img"

    void chooseFileToOpen (juce::Component& over, const juce::String& title,
                           const juce::File& startFolder, const Filter& filter,
                           std::function<void (const juce::File&)> done);

    /// Asks before overwriting. `extension` is added when the name typed has none ("img").
    void chooseFileToSave (juce::Component& over, const juce::String& title,
                           const juce::File& suggested, const Filter& filter,
                           const juce::String& extension,
                           std::function<void (const juce::File&)> done);
}
