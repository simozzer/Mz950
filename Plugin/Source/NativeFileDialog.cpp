#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shobjidl.h>
 #pragma comment (lib, "ole32.lib")
 #pragma comment (lib, "shell32.lib")
 #pragma comment (lib, "uuid.lib")
#endif

#include "NativeFileDialog.h"

#include <memory>

namespace nativeDialog
{
    namespace
    {
        /*
         * JUCE's own browser, inside the editor - what both of these used to be, kept for
         * anywhere the system dialog cannot be shown. Held here because launchAsync returns
         * at once and a chooser that goes out of scope takes its window with it.
         */
        std::unique_ptr<juce::FileChooser> fallbackChooser;

        void fallback (juce::Component& over, const juce::String& title, const juce::File& start,
                       const Filter& filter, bool save, const juce::String& extension,
                       std::function<void (const juce::File&)> done)
        {
            fallbackChooser = std::make_unique<juce::FileChooser> (title, start, filter.patterns,
                                                                   false, false, &over);

            const int flags = save ? (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting)
                                   : (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles);

            fallbackChooser->launchAsync (flags, [save, extension, done] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (save && file != juce::File() && extension.isNotEmpty() && ! file.hasFileExtension (extension))
                    file = file.withFileExtension (extension);
                done (file);
            });
        }

       #if defined (_WIN32)
        /// The window that owns the dialog: the top of the plugin window's chain, which is
        /// what the host floats and what the dialog must stay above and centre on.
        HWND ownerOf (juce::Component& c)
        {
            if (auto* peer = c.getPeer())
                if (auto* handle = static_cast<HWND> (peer->getNativeHandle()))
                    return GetAncestor (handle, GA_ROOT);

            return nullptr;
        }

        /*
         * The Windows common item dialog, run modally over `owner`. `shown` says whether it
         * came up at all: false sends the caller to the fallback, while true with an empty
         * File is simply a cancel.
         */
        juce::File run (bool save, HWND owner, const juce::String& title, const juce::File& folder,
                        const juce::String& fileName, const Filter& filter, const juce::String& extension,
                        bool& shown)
        {
            shown = false;
            juce::File result;

            const HRESULT init = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            const bool uninitialise = SUCCEEDED (init);     // S_OK and S_FALSE both want a CoUninitialize

            IFileDialog* dialog = nullptr;
            const HRESULT made = CoCreateInstance (save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
                                                   nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&dialog));
            if (SUCCEEDED (made) && dialog != nullptr)
            {
                DWORD options = 0;
                dialog->GetOptions (&options);
                options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
                options |= save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST;
                dialog->SetOptions (options);

                dialog->SetTitle (title.toWideCharPointer());

                const COMDLG_FILTERSPEC types[] =
                {
                    { filter.description.toWideCharPointer(), filter.patterns.toWideCharPointer() },
                    { L"All files", L"*.*" }
                };
                dialog->SetFileTypes (2, types);
                dialog->SetFileTypeIndex (1);

                if (extension.isNotEmpty())
                    dialog->SetDefaultExtension (extension.toWideCharPointer());

                if (fileName.isNotEmpty())
                    dialog->SetFileName (fileName.toWideCharPointer());

                // SetFolder rather than SetDefaultFolder: the start folder is chosen with care
                // (beside the open disk, then the last place a disk came from), and Windows'
                // memory of the last folder any program used should not override it.
                if (folder.isDirectory())
                {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED (SHCreateItemFromParsingName (folder.getFullPathName().toWideCharPointer(),
                                                                nullptr, IID_PPV_ARGS (&item))) && item != nullptr)
                    {
                        dialog->SetFolder (item);
                        item->Release();
                    }
                }

                const HRESULT answer = dialog->Show (owner);

                if (SUCCEEDED (answer))
                {
                    shown = true;

                    IShellItem* picked = nullptr;
                    if (SUCCEEDED (dialog->GetResult (&picked)) && picked != nullptr)
                    {
                        PWSTR path = nullptr;
                        if (SUCCEEDED (picked->GetDisplayName (SIGDN_FILESYSPATH, &path)) && path != nullptr)
                        {
                            result = juce::File (juce::String (path));
                            CoTaskMemFree (path);
                        }
                        picked->Release();
                    }
                }
                else if (answer == HRESULT_FROM_WIN32 (ERROR_CANCELLED))
                {
                    shown = true;        // it came up, and the answer was no
                }

                dialog->Release();
            }

            if (uninitialise)
                CoUninitialize();

            return result;
        }
       #endif

        void choose (juce::Component& over, bool save, const juce::String& title, const juce::File& folder,
                     const juce::String& fileName, const Filter& filter, const juce::String& extension,
                     std::function<void (const juce::File&)> done)
        {
           #if defined (_WIN32)
            /*
             * Shown from a fresh message rather than from inside the click that asked for it:
             * the dialog runs its own modal loop, and a button is happier finishing its mouse-up
             * before that starts.
             */
            juce::Component::SafePointer<juce::Component> safe (&over);

            juce::MessageManager::callAsync ([safe, save, title, folder, fileName, filter, extension, done]
            {
                if (safe == nullptr) return;

                bool shown = false;
                const auto file = run (save, ownerOf (*safe), title, folder, fileName, filter, extension, shown);

                // The host may have closed the plugin window while the dialog was up; then
                // there is nobody left to give the answer to.
                if (safe == nullptr) return;

                if (shown)
                {
                    done (file);
                    return;
                }

                if (safe != nullptr)
                    fallback (*safe, title, folder.getChildFile (fileName), filter, save, extension, done);
            });
           #else
            fallback (over, title, folder.getChildFile (fileName), filter, save, extension, done);
           #endif
        }
    }

    void chooseFileToOpen (juce::Component& over, const juce::String& title,
                           const juce::File& startFolder, const Filter& filter,
                           std::function<void (const juce::File&)> done)
    {
        choose (over, false, title, startFolder, {}, filter, {}, std::move (done));
    }

    void chooseFileToSave (juce::Component& over, const juce::String& title,
                           const juce::File& suggested, const Filter& filter,
                           const juce::String& extension,
                           std::function<void (const juce::File&)> done)
    {
        choose (over, true, title, suggested.getParentDirectory(), suggested.getFileName(),
                filter, extension, std::move (done));
    }
}
