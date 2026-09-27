using System;
using System.Windows.Forms;
using AkaiS950List;

namespace AkaiS950Studio
{
    public sealed partial class MainForm
    {
        /*
         * The strip of buttons under the menu.
         *
         * Everything here is already on a menu and keeps its shortcut; this is the web
         * version's row of buttons, in the web version's order, so that the two programs
         * open the same way. Nothing new happens here - each button calls the handler the
         * menu item calls, and the two stay enabled and disabled together through
         * UpdateCommands.
         *
         * What is not here: the web's "Download source", which is a browser's answer to a
         * problem a program on disk does not have, and the disk and file counts, which are
         * in the status bar where a desktop program puts them.
         */

        readonly ToolStrip _toolbar = new ToolStrip();

        ToolStripButton _tbUndo, _tbRedo, _tbAddSample, _tbNewProgram, _tbSave;
        ToolStripComboBox _tbMidiIn, _tbMidiChannel;

        bool _toolbarLoading;

        static ToolStripButton Tool(string text, EventHandler onClick)
        {
            var b = new ToolStripButton(text)
            {
                DisplayStyle = ToolStripItemDisplayStyle.Text,
                // Padding so the text is not jammed against the border, the way the web's
                // buttons sit.
                Padding = new Padding(6, 0, 6, 0)
            };
            b.Click += onClick;
            return b;
        }

        ToolStrip BuildToolbar()
        {
            _tbAddSample = Tool("Add sample", OnImportSample);
            _tbNewProgram = Tool("New program", (s, e) => CreateProgram(TargetDisk));
            _tbSave = Tool("Save image", OnSaveAs);
            _tbUndo = Tool("Undo", OnUndo);
            _tbRedo = Tool("Redo", OnRedo);

            _tbMidiIn = new ToolStripComboBox { AutoSize = false, Width = 210 };
            _tbMidiIn.DropDownStyle = ComboBoxStyle.DropDownList;
            _tbMidiIn.DropDown += (s, e) => FillMidiCombo();
            _tbMidiIn.SelectedIndexChanged += (s, e) =>
            {
                if (_toolbarLoading) return;
                ChooseMidi(_tbMidiIn.SelectedIndex - 1);       // row 0 is "off"
            };

            _tbMidiChannel = new ToolStripComboBox { AutoSize = false, Width = 96 };
            _tbMidiChannel.DropDownStyle = ComboBoxStyle.DropDownList;
            _tbMidiChannel.Items.Add("all channels");
            for (int i = 1; i <= 16; i++) _tbMidiChannel.Items.Add("channel " + i);
            _tbMidiChannel.SelectedIndex = 0;
            _tbMidiChannel.SelectedIndexChanged += (s, e) =>
            {
                if (_toolbarLoading || !_instrumentOk) return;
                _instrument.MidiChannel = _tbMidiChannel.SelectedIndex;
            };

            _toolbar.GripStyle = ToolStripGripStyle.Hidden;
            _toolbar.Items.AddRange(new ToolStripItem[]
            {
                Tool("Open images", OnOpenImage),
                _tbAddSample,
                Tool("New image", OnNewImage),
                _tbNewProgram,
                new ToolStripSeparator(),
                _tbUndo, _tbRedo,
                new ToolStripSeparator(),
                _tbSave,
                new ToolStripSeparator(),
                new ToolStripLabel("MIDI in"),
                _tbMidiIn,
                _tbMidiChannel
            });

            FillMidiCombo();
            return _toolbar;
        }

        /// <summary>
        /// The inputs, as they are now. Refilled when the list drops down, so a keyboard
        /// plugged in after the program started is there when you go looking for it.
        /// </summary>
        void FillMidiCombo()
        {
            _toolbarLoading = true;
            try
            {
                int open = _instrumentOk ? _instrument.MidiPort : -1;

                _tbMidiIn.Items.Clear();
                _tbMidiIn.Items.Add("off");

                try
                {
                    foreach (string p in Instrument.MidiPorts()) _tbMidiIn.Items.Add(p);
                }
                catch { /* no MIDI at all: "off" on its own says enough */ }

                int want = open + 1;
                _tbMidiIn.SelectedIndex = want >= 0 && want < _tbMidiIn.Items.Count ? want : 0;
            }
            finally { _toolbarLoading = false; }
        }

        /// <summary>Show the port the instrument actually opened, whoever chose it.</summary>
        void ShowMidiPortOnToolbar()
        {
            _toolbarLoading = true;
            try
            {
                int open = _instrumentOk ? _instrument.MidiPort : -1;
                int want = open + 1;
                if (want >= 0 && want < _tbMidiIn.Items.Count) _tbMidiIn.SelectedIndex = want;
            }
            finally { _toolbarLoading = false; }
        }

        /// <summary>
        /// A new, empty 800K image.
        ///
        /// An S950 disk is empty when its directory and its allocation table are zero, so
        /// an image of nothing but zeroes is already a formatted blank - which is what the
        /// web version makes too. It is not written anywhere until it is saved.
        /// </summary>
        void OnNewImage(object sender, EventArgs e) { NewImage(false); }

        /// <summary>
        /// A new, empty 1600K high-density image - the S950 formats both. Zeroes are a blank
        /// here too: the five-block header is the 800K one with the allocation table carried
        /// on to 1600 entries, and akaiutil's formatfloppyh9 writes the same.
        /// </summary>
        void OnNewHdImage(object sender, EventArgs e) { NewImage(true); }

        void NewImage(bool highDensity)
        {
            int n = 1;
            string stem = highDensity ? "new-hd-disk" : "new-disk";
            string name;
            do
            {
                name = n == 1 ? stem + ".img" : stem + "-" + n + ".img";
                n++;
            }
            while (_disks.Exists(d => string.Equals(
                System.IO.Path.GetFileName(d.Source), name, StringComparison.OrdinalIgnoreCase)));

            AkaiDisk made;
            try { made = AkaiDisk.LoadFromBytes(name, new byte[(highDensity ? AkaiDisk.HdBlocks : AkaiDisk.DdBlocks) * AkaiDisk.BlockSize]); }
            catch (Exception ex)
            {
                SetStatus("Could not make a blank image: " + ex.Message);
                return;
            }

            made.Modified = true;
            _disks.Add(made);

            RebuildTree();
            UpdateCommands();
            SetStatus(name + " created. It is not on disk until you save it.");
        }

        /// <summary>Keep the buttons in step with the menu items they stand for.</summary>
        void UpdateToolbar()
        {
            if (_tbUndo == null) return;

            _tbUndo.Enabled = _undoItem.Enabled;
            _tbRedo.Enabled = _redoItem.Enabled;
            _tbAddSample.Enabled = _importItem.Enabled;
            _tbNewProgram.Enabled = _newProgramItem.Enabled;
            _tbSave.Enabled = _saveItem.Enabled;
        }
    }
}
