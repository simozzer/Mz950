using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using AkaiS950List;

namespace AkaiS950Studio
{
    /// <summary>Ties a directory entry back to the disk it came from.</summary>
    internal sealed class FileRef
    {
        public AkaiDisk Disk;
        public AkaiEntry Entry;
        public FileRef(AkaiDisk d, AkaiEntry e) { Disk = d; Entry = e; }
    }

    public sealed partial class MainForm : Form
    {
        readonly TreeView _tree = new TreeView();
        readonly ListView _details = new ListView();
        readonly ListView _keygroups = new ListView();
        readonly Label _kgHeader = new Label();
        readonly Panel _kgPanel = new Panel();
        readonly PianoKeyboard _piano = new PianoKeyboard();
        readonly VelocitySlider _velocity = new VelocitySlider();

        /// <summary>The keyboard and the velocity strip beside it, shown and hidden together.</summary>
        Panel _keyRow;
        readonly WaveformView _wave = new WaveformView();
        readonly Label _waveHeader = new Label();
        readonly Panel _wavePanel = new Panel();

        // The strip over the waveform and the column beside it. The work these do was
        // only ever on the right-click menu, which is a poor place for the five things
        // you reach for most while looking at a waveform.
        readonly Panel _waveBar = new Panel();
        readonly Panel _waveActions = new Panel();
        readonly Panel _waveSide = new Panel();

        Button _btnTrim, _btnHalve, _btnFit, _btnFindLoop, _btnSlice, _btnPlay, _btnStop;

        // What the waveform is showing, so the buttons over it act on the same thing
        // the eye is on - which is not always what the tree has selected, since
        // picking a keygroup shows that keygroup's sample.
        AkaiDisk _waveDisk;
        AkaiEntry _waveEntry;
        readonly StatusStrip _status = new StatusStrip();
        readonly ToolStripStatusLabel _statusText = new ToolStripStatusLabel();
        readonly ToolStripProgressBar _progress = new ToolStripProgressBar();
        readonly ToolStripMenuItem _exportItem = new ToolStripMenuItem("&Export Selected File...");
        readonly ToolStripMenuItem _exportWavItem = new ToolStripMenuItem("Export Sample as &WAV...");
        readonly ToolStripMenuItem _saveItem = new ToolStripMenuItem("&Save Disk Image As...");
        readonly ToolStripMenuItem _importItem = new ToolStripMenuItem("Add &Sample to Disk...");
        readonly ToolStripMenuItem _saveAllItem = new ToolStripMenuItem("Save A&ll Modified...");
        readonly ToolStripMenuItem _undoItem = new ToolStripMenuItem("&Undo");
        readonly ToolStripMenuItem _redoItem = new ToolStripMenuItem("&Redo");
        readonly ToolStripMenuItem _newProgramItem = new ToolStripMenuItem("New &Program...");
        readonly ToolStripMenuItem _sliceItem = new ToolStripMenuItem("S&lice into One-shots...");
        readonly ToolStripMenuItem _deleteItem = new ToolStripMenuItem("&Delete File...");

        readonly Panel _workArea = new Panel();
        Control _editArea;

        readonly EnvelopeEditor _vcaEnv = new EnvelopeEditor { Title = "VCA" };
        readonly EnvelopeEditor _vcfEnv = new EnvelopeEditor { Title = "VCF" };
        readonly TrackBar _vcfAmount = new TrackBar();
        readonly Label _vcfAmountLabel = new Label();
        // The envelopes, hidden when nothing with envelopes is selected.
        Panel _envPanel;

        /*
         * Two ways of making a noise, and the old one is still here on purpose.
         *
         * _instrument is the real one: the engine, the sound card and the MIDI port,
         * with the filter, both envelopes and the vibrato, eight voices at a time. It
         * needs WASAPI, which is there on anything since Vista but is not guaranteed -
         * a machine with no output device at all, or an audio engine not running in
         * float, will refuse to open.
         *
         * _audio is SoundPlayer, which cannot do any of that but works everywhere. It is
         * the fallback, and the status bar says when it is what you are hearing.
         */
        readonly SamplePlayer _audio = new SamplePlayer();
        readonly Instrument _instrument = new Instrument();
        bool _instrumentTried, _instrumentOk;

        /// <summary>
        /// Open the sound card, once, the first time something wants to be played.
        ///
        /// Not at startup: grabbing the output device just to look at a disk directory
        /// would be rude to whatever else is using it.
        /// </summary>
        bool EnsureInstrument()
        {
            if (_instrumentTried) return _instrumentOk;
            _instrumentTried = true;
            _instrumentOk = _instrument.Start();
            return _instrumentOk;
        }
        /*
         * Off. Selecting a file used to audition it, which is pleasant while browsing
         * samples and ruinous everywhere else: the audition is a note nobody pressed,
         * at note 60, with the filter wide open, and nothing will ever release it. A
         * looped sample auditioned that way runs until something else stops it.
         *
         * Clicking a key selects that key's keygroup, so every key press started one of
         * these underneath the note it was supposed to play. The loudest and
         * longest-lived of them is then what the ear hears on every key, whichever key
         * it was - which is why the waveform display and the audio disagreed.
         *
         * Playing is now something you ask for: double-click, Ctrl+P, or the keyboard.
         */
        readonly ToolStripMenuItem _midiMenu = new ToolStripMenuItem("&MIDI Input");

        readonly ToolStripMenuItem _autoPlay =
            new ToolStripMenuItem("Play on &Selection") { CheckOnClick = true, Checked = false };

        readonly List<AkaiDisk> _disks = new List<AkaiDisk>();
        CancellationTokenSource _cts;

        readonly string[] _startupPaths;

        public MainForm() : this(null) { }

        public MainForm(string[] startupPaths)
        {
            _startupPaths = startupPaths;
            Text = AppTitle;
            Width = 1100;
            Height = 720;
            StartPosition = FormStartPosition.CenterScreen;
            Font = SystemFonts.MessageBoxFont;

            // Docking is resolved from the last-added control backwards, so the
            // Fill body must be added first and the edge-docked strips after it.
            var body = BuildBody();
            var status = BuildStatus();
            var menu = BuildMenu();

            Controls.Add(body);
            Controls.Add(status);
            Controls.Add(BuildToolbar());
            Controls.Add(menu);
            MainMenuStrip = menu;

            SetStatus("Open a disk image, or a folder of them, to begin.");
            UpdateCommands();

            //
            // The icon, taken back out of this program's own executable.
            //
            // /win32icon gives the file its icon in Explorer and on the taskbar but says
            // nothing about the window, which keeps WinForms' default until it is told
            // otherwise. Reading it back from the exe means there is one icon rather than
            // two - no copy embedded as a managed resource to fall out of step with the
            // one the shell shows.
            //
            try { Icon = System.Drawing.Icon.ExtractAssociatedIcon(Application.ExecutablePath); }
            catch { /* a build without an icon in it is not worth a dialog */ }
        }

        // Splitter positions only stick once the controls have a real size.
        protected override void OnLoad(EventArgs e)
        {
            base.OnLoad(e);

            _editWatch.Tick += OnEditWatchTick;
            _editWatch.Start();

            TrySetSplitter(_mainSplit, 210, 120, 240);
            // the waveform keeps its height as the window grows; the number column its width
            TrySetSplitter(_outerSplit, Math.Max(240, _outerSplit.Height - 190), 200, 110);
            TrySetSplitter(_middleSplit, 118, 90, 240);
        }

        /// <summary>
        /// Panel minimums and splitter position can only be applied once the control
        /// has a real size; setting them earlier throws.
        /// </summary>
        static void TrySetSplitter(SplitContainer sc, int distance, int min1, int min2)
        {
            if (sc == null) return;
            int span = sc.Orientation == Orientation.Vertical ? sc.Width : sc.Height;
            if (span <= 0) return;

            int third = Math.Max(1, span / 3);
            min1 = Math.Min(min1, third);
            min2 = Math.Min(min2, third);

            try
            {
                sc.Panel1MinSize = min1;
                sc.Panel2MinSize = min2;
                int max = span - min2 - sc.SplitterWidth;
                if (max > min1) sc.SplitterDistance = Math.Max(min1, Math.Min(distance, max));
            }
            catch (InvalidOperationException) { /* leave the defaults in place */ }
        }

        /// <summary>
        /// A path on the command line (a folder, drive or image) loads at startup. With
        /// nothing named, the last session does - or, the first time, the sound library.
        /// </summary>
        protected override async void OnShown(EventArgs e)
        {
            base.OnShown(e);

            var files = new List<string>();
            if (_startupPaths != null)
            {
                foreach (var p in _startupPaths)
                {
                    if (Directory.Exists(p))
                        files.AddRange(Directory.GetFiles(p, "*.hfe").Concat(Directory.GetFiles(p, "*.img")));
                    else if (File.Exists(p))
                        files.Add(p);
                }
            }

            // An explicit path is an instruction and beats anything remembered.
            if (files.Count == 0)
            {
                await OpenRememberedOrStarter();
                return;
            }

            files.Sort(StringComparer.OrdinalIgnoreCase);
            await LoadFiles(files.ToArray());
        }

        /// <summary>
        /// What to open when the command line named nothing.
        ///
        /// Two cases wearing the same shape. Someone coming back gets whatever they had
        /// open, because quitting the program is not the same as deciding to close the
        /// disks. Someone arriving for the first time has no such history and would
        /// otherwise meet an empty window and a file dialog, so they get the bundled
        /// library instead: an instrument that makes a noise the moment it opens teaches
        /// more than a correct empty state does.
        /// </summary>
        async Task OpenRememberedOrStarter()
        {
            if (Session.HasRunBefore())
            {
                string[] remembered = Session.LastOpened();

                // Nothing open at the last exit. That was a choice, so honour it.
                if (remembered.Length == 0) return;

                string[] there = remembered.Where(File.Exists).ToArray();

                if (there.Length == 0)
                {
                    SetStatus("The disk image(s) open last time are no longer where they were."
                              + "  -  File > Open Disk Image to choose another.");
                    return;
                }

                await LoadFiles(there);

                // Say so rather than quietly opening fewer disks than were there before.
                if (there.Length < remembered.Length)
                    SetStatus(_statusText.Text + "  -  "
                              + (remembered.Length - there.Length) + " since moved or deleted");
                return;
            }

            string starter = Session.StarterDisk();
            if (starter == null) return;        // no library beside us; the empty state stands

            await LoadFiles(new string[] { starter });

            SetStatus(_statusText.Text + "  -  opened " + Path.GetFileName(starter)
                      + " to start you off; File > Open Disk Image for your own");
        }

        /// <summary>
        /// Write down what is open, so the next launch can pick it up.
        ///
        /// Called on every load and on the way out rather than only at exit, so that a
        /// program that is killed rather than closed still remembers the last thing it
        /// was asked to open.
        /// </summary>
        void RememberSession()
        {
            var paths = new List<string>();
            foreach (var d in _disks) paths.Add(FullPath(d.Source));
            Session.Remember(paths.ToArray());
        }

        // ---------------------------------------------------------------- menu

        MenuStrip BuildMenu()
        {
            var file = new ToolStripMenuItem("&File");
            var openImage = new ToolStripMenuItem("&Open Disk Image...", null, OnOpenImage)
            { ShortcutKeys = Keys.Control | Keys.O };
            var openFolder = new ToolStripMenuItem("Open &Folder...", null, OnOpenFolder)
            { ShortcutKeys = Keys.Control | Keys.Shift | Keys.O };
            var addImage = new ToolStripMenuItem("&Add Image...", null, OnAddImage);
            var newImage = new ToolStripMenuItem("&New Disk Image (800K)", null, OnNewImage);
            var newHdImage = new ToolStripMenuItem("New &HD Disk Image (1600K)", null, OnNewHdImage);
            var closeAll = new ToolStripMenuItem("&Close All", null, OnCloseAll);
            _importItem.Click += OnImportSample;
            _importItem.ShortcutKeys = Keys.Control | Keys.I;
            _saveItem.Click += OnSaveAs;
            _saveAllItem.Click += OnSaveAll;
            _saveAllItem.ShortcutKeys = Keys.Control | Keys.Shift | Keys.S;
            _saveItem.ShortcutKeys = Keys.Control | Keys.S;
            _exportItem.Click += OnExport;
            _exportItem.ShortcutKeys = Keys.Control | Keys.E;
            _exportWavItem.Click += OnExportWav;
            _exportWavItem.ShortcutKeys = Keys.Control | Keys.W;

            file.DropDownItems.AddRange(new ToolStripItem[]
            {
                openImage, openFolder, addImage, closeAll,
                new ToolStripSeparator(), newImage, newHdImage,
                new ToolStripSeparator(), _importItem, _exportItem, _exportWavItem,
                new ToolStripSeparator(),
                _saveItem, _saveAllItem,
                new ToolStripSeparator(),
                new ToolStripMenuItem("E&xit", null, (s, e) => Close())
            });

            var edit = new ToolStripMenuItem("&Edit");
            _undoItem.Click += OnUndo;
            _undoItem.ShortcutKeys = Keys.Control | Keys.Z;
            _redoItem.Click += OnRedo;
            _redoItem.ShortcutKeys = Keys.Control | Keys.Y;

            _newProgramItem.Click += (s, e) => CreateProgram(TargetDisk);
            _newProgramItem.ShortcutKeys = Keys.Control | Keys.N;
            _sliceItem.Click += (s, e) => { var f = SelectedFile; if (f != null) SliceSampleFile(f); };
            _sliceItem.ShortcutKeys = Keys.Control | Keys.L;
            _deleteItem.Click += OnDeleteFile;
            _deleteItem.ShortcutKeys = Keys.Control | Keys.Delete;

            edit.DropDownItems.AddRange(new ToolStripItem[]
            {
                _undoItem, _redoItem,
                new ToolStripSeparator(),
                _newProgramItem, _sliceItem,
                new ToolStripSeparator(),
                _deleteItem
            });

            var view = new ToolStripMenuItem("&View");
            view.DropDownItems.AddRange(new ToolStripItem[]
            {
                new ToolStripMenuItem("&Expand All", null, (s, e) => _tree.ExpandAll()),
                new ToolStripMenuItem("&Collapse All", null, (s, e) => _tree.CollapseAll())
            });

            var play = new ToolStripMenuItem("&Play");

            // Filled when it opens rather than now, so a keyboard plugged in after the
            // program started is there when you go looking for it.
            _midiMenu.DropDownOpening += (s, e) => BuildMidiMenu();
            BuildMidiMenu();

            var playNow = new ToolStripMenuItem("&Play Sample", null, OnPlay)
            { ShortcutKeys = Keys.Control | Keys.P };
            var stopNow = new ToolStripMenuItem("&Stop", null, OnStopPlay)
            {
                ShortcutKeys = Keys.Control | Keys.OemPeriod,
                ShortcutKeyDisplayString = "Ctrl+."
            };
            play.DropDownItems.AddRange(new ToolStripItem[]
            {
                playNow, stopNow, new ToolStripSeparator(), _autoPlay,
                new ToolStripSeparator(), _midiMenu
            });

            var help = new ToolStripMenuItem("&Help");
            help.DropDownItems.AddRange(new ToolStripItem[]
            {
                new ToolStripMenuItem("&Tutorial", null, OnTutorial) { ShortcutKeys = Keys.F1 },
                new ToolStripSeparator(),
                new ToolStripMenuItem("&About", null, OnAbout)
            });

            var strip = new MenuStrip();
            strip.Items.AddRange(new ToolStripItem[] { file, edit, view, play, help });
            return strip;
        }

        // ---------------------------------------------------------------- body

        Control BuildBody()
        {
            _tree.Dock = DockStyle.Fill;
            _tree.HideSelection = false;
            _tree.AfterSelect += (s, e) => ShowSelection();
            _tree.NodeMouseDoubleClick += (s, e) => PlayCurrent(true);
            _tree.NodeMouseClick += OnTreeRightClick;

            _details.Dock = DockStyle.Fill;
            _details.View = View.Details;
            _details.FullRowSelect = true;
            _details.GridLines = true;
            _details.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            _details.Columns.Add("Property", 190);
            _details.Columns.Add("Value", 420);

            _kgHeader.Dock = DockStyle.Top;
            _kgHeader.Height = 22;
            _kgHeader.TextAlign = ContentAlignment.MiddleLeft;
            _kgHeader.Padding = new Padding(4, 0, 0, 0);
            _kgHeader.Font = new Font(Font, FontStyle.Bold);
            _kgHeader.Text = "Keygroups";

            // Add and Delete under the list, where the web puts them. Both already
            // existed on the list's right-click menu, which is not where you look
            // for them.
            var kgButtons = new Panel { Dock = DockStyle.Bottom, Height = 30 };
            var addKg = new Button
            {
                Text = "Add", Width = 54, Height = 24, Location = new Point(2, 3),
                FlatStyle = FlatStyle.System, TabStop = false
            };
            var delKg = new Button
            {
                Text = "Delete", Width = 54, Height = 24, Location = new Point(60, 3),
                FlatStyle = FlatStyle.System, TabStop = false
            };
            addKg.Click += (s, e) => OnKeygroupButton(true);
            delKg.Click += (s, e) => OnKeygroupButton(false);
            kgButtons.Controls.Add(addKg);
            kgButtons.Controls.Add(delKg);
            _kgButtons = kgButtons;

            _keygroups.Dock = DockStyle.Fill;
            _keygroups.View = View.Details;
            _keygroups.FullRowSelect = true;
            // Ctrl and shift pick several, and every edit in the grid, both envelopes
            // and the VCF amount then reach all of them.
            _keygroups.MultiSelect = true;

            // The selection decides what an edit reaches, so it has to stay visible
            // when the focus moves to the grid, the envelopes or the keyboard. A list
            // view hides it by default, which would leave the one thing you need to
            // see before typing a value invisible at the moment you type it.
            _keygroups.HideSelection = false;
            _keygroups.GridLines = false;
            _keygroups.HeaderStyle = ColumnHeaderStyle.None;

            // Just the numbers. Every other column this list used to carry - key range,
            // envelopes, filter, zone samples - is a field in the grid beside it, so the
            // list stays narrow and the editor gets the width. What the grid cannot show
            // at a glance is on the row instead: the tooltip carries the detail, and a
            // keygroup whose sample is not on this disk stays marked.
            _keygroups.Columns.Add("Kg", 58);
            _keygroups.ShowItemToolTips = true;

            /*
             * The keyboard and, on the right of the same row, how hard it strikes.
             *
             * Docking resolves from the last-added control backwards, so the one that
             * fills goes in first and the strip on the edge after it.
             */
            _piano.Dock = DockStyle.Fill;
            _piano.KeyClicked += OnPianoKey;
            _piano.KeyReleased += OnPianoKeyUp;

            _velocity.Dock = DockStyle.Right;
            _velocity.ValueChanged += (s, e) =>
                SetStatus("Keyboard velocity " + _velocity.Value +
                          "  -  a softer strike closes the filter, a harder one opens it.");

            _keyRow = new Panel { Dock = DockStyle.Top, Height = 92 };
            _keyRow.Controls.Add(_piano);
            _keyRow.Controls.Add(_velocity);

            // The list names the column it is in, level with the editor beside it.
            // Docking resolves from the last-added control backwards, so the header goes
            // on top and the list fills the rest.
            _kgPanel.Dock = DockStyle.Fill;
            _kgPanel.Controls.Add(_keygroups);
            _kgPanel.Controls.Add(_kgButtons);
            _kgPanel.Controls.Add(_kgHeader);

            /*
             * No tabs. They existed because a programme's and a sample's few fields
             * needed somewhere to live, and those are on the header strip now - so
             * what is left is one area showing whichever thing is selected: the
             * keygroup editor for a programme, the detail list for a disk, and
             * nothing at all for a sample, whose waveform is already along the bottom.
             * The web version has no tabs either, and for the same reason.
             */
            _details.Dock = DockStyle.Fill;
            _details.Visible = false;

            _editArea = BuildEditPane();
            _editArea.Dock = DockStyle.Fill;
            _editArea.Visible = false;

            _workArea.Dock = DockStyle.Fill;
            _workArea.Controls.Add(_details);
            _workArea.Controls.Add(_editArea);

            // Keygroup numbers on the left, the editor filling what is left of the row.
            var middle = new SplitContainer
            {
                Dock = DockStyle.Fill,
                Orientation = Orientation.Vertical,
                FixedPanel = FixedPanel.Panel1
            };
            middle.Panel1.Controls.Add(_kgPanel);
            middle.Panel2.Controls.Add(_workArea);
            _middleSplit = middle;

            // The keyboard runs across the top of the working area, above both of them,
            // because a keygroup is a range of keys before it is anything else.
            var work = new Panel { Dock = DockStyle.Fill };
            work.Controls.Add(middle);
            work.Controls.Add(_keyRow);
            work.Controls.Add(BuildFileHeader());

            _keygroups.SelectedIndexChanged += (s, e) => OnKeygroupSelected();
            _piano.RangePicked += OnRangePicked;
            _keygroups.DoubleClick += (s, e) => PlayCurrent(true);
            _keygroups.MouseUp += OnKeygroupRightClick;

            _waveHeader.Dock = DockStyle.Fill;
            _waveHeader.TextAlign = ContentAlignment.MiddleLeft;
            _waveHeader.Padding = new Padding(4, 0, 0, 0);
            _waveHeader.Font = new Font(Font, FontStyle.Bold);
            _waveHeader.Text = "Waveform";

            BuildWaveButtons();

            // Title on the left of the strip, the actions on the right of it.
            _waveBar.Dock = DockStyle.Top;
            _waveBar.Height = 30;
            _waveBar.Controls.Add(_waveHeader);
            _waveBar.Controls.Add(_waveActions);

            _wave.Dock = DockStyle.Fill;
            _wavePanel.Dock = DockStyle.Fill;

            // Docking resolves from the last added backwards: the strip takes the full
            // width at the top, the column takes the right of what is left, and the
            // waveform fills the rest.
            _wavePanel.Controls.Add(_wave);
            _wavePanel.Controls.Add(_waveSide);
            _wavePanel.Controls.Add(_waveBar);

            // The waveform is docked along the bottom, under everything, so it stays
            // visible whichever keygroup is being worked on.
            var outer = new SplitContainer
            {
                Dock = DockStyle.Fill,
                Orientation = Orientation.Horizontal,
                FixedPanel = FixedPanel.Panel2
            };
            outer.Panel1.Controls.Add(work);
            outer.Panel2.Controls.Add(_wavePanel);
            outer.Panel2Collapsed = true;
            _outerSplit = outer;

            var main = new SplitContainer
            {
                Dock = DockStyle.Fill,
                Orientation = Orientation.Vertical
            };
            main.Panel1.Controls.Add(_tree);
            main.Panel2.Controls.Add(outer);

            // Once the user sets the width themselves, stop second-guessing it.
            // SplitterMoving is the drag; SplitterMoved would also fire on a resize.
            main.SplitterMoving += (s, e) => _treeWidthPinned = true;

            _mainSplit = main;
            return main;
        }

        /// <summary>
        /// The Edit tab: the property grid, with the two envelopes beside it. Four
        /// numbers each for VCA and VCF say much less than the shape does, so those
        /// eight properties are off the grid and live here instead - along with the
        /// filter envelope's amount, which only means anything next to its envelope.
        /// </summary>
        Control BuildEditPane()
        {

            _vcaEnv.Dock = DockStyle.Fill;
            _vcaEnv.Changed += OnVcaEnvelopeChanged;
            _vcaEnv.DragStarted += (s, e) => CaptureBaseline();

            _vcfEnv.Dock = DockStyle.Fill;
            _vcfEnv.Changed += OnVcfEnvelopeChanged;
            _vcfEnv.DragStarted += (s, e) => CaptureBaseline();

            // Vertical, beside the envelope it scales: up is more, down is less.
            _vcfAmount.Orientation = Orientation.Vertical;
            _vcfAmount.Minimum = -50;
            _vcfAmount.Maximum = 50;
            _vcfAmount.TickFrequency = 10;
            _vcfAmount.TickStyle = TickStyle.TopLeft;
            _vcfAmount.Dock = DockStyle.Fill;
            _vcfAmount.ValueChanged += OnVcfAmountChanged;
            _vcfAmount.MouseDown += (s, e) => CaptureBaseline();
            _vcfAmount.KeyDown += (s, e) => CaptureBaseline();

            _vcfAmountLabel.Dock = DockStyle.Bottom;
            _vcfAmountLabel.Height = 30;
            _vcfAmountLabel.TextAlign = ContentAlignment.MiddleCenter;
            _vcfAmountLabel.Text = "amt" + Environment.NewLine + "0";

            var amountColumn = new Panel { Dock = DockStyle.Right, Width = 62 };
            amountColumn.Controls.Add(_vcfAmount);
            amountColumn.Controls.Add(_vcfAmountLabel);

            var vcfRow = new Panel { Dock = DockStyle.Fill };
            vcfRow.Controls.Add(_vcfEnv);
            vcfRow.Controls.Add(amountColumn);

            var stack = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 1,
                RowCount = 2
            };
            stack.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
            stack.RowStyles.Add(new RowStyle(SizeType.Percent, 50));
            stack.Controls.Add(_vcaEnv, 0, 0);
            stack.Controls.Add(vcfRow, 0, 1);

            var envPanel = new Panel { Dock = DockStyle.Fill };
            envPanel.Controls.Add(stack);
            _envPanel = envPanel;

            /*
             * The keygroup editor, laid out as the web version lays it out: the
             * envelopes over the velocity and LFO sliders, with both zones and the
             * flags in a column beside them.
             *
             * The property grid stays for a programme or a sample, which have a handful
             * of fields each and no shape worth drawing. A keygroup has thirty, and a
             * grid turns them into a list of names you read one at a time rather than a
             * picture of a patch.
             */
            var keygroupPane = BuildKeygroupPane(envPanel);
            var zonePane = BuildZonePane();

            var keygroupSide = new Panel { Dock = DockStyle.Fill };
            keygroupSide.Controls.Add(keygroupPane);
            keygroupSide.Controls.Add(zonePane);

            return keygroupSide;
        }

        SplitContainer _outerSplit;      // working area over the waveform
        SplitContainer _middleSplit;     // keygroup numbers beside the editor
        SplitContainer _mainSplit;


        StatusStrip BuildStatus()
        {
            _progress.Visible = false;
            _progress.Width = 180;
            _statusText.Spring = true;
            _statusText.TextAlign = ContentAlignment.MiddleLeft;
            _status.Items.Add(_statusText);
            _status.Items.Add(_progress);
            return _status;
        }

        // ------------------------------------------------------------- loading

        async void OnOpenImage(object sender, EventArgs e)
        {
            if (!ConfirmDiscard()) return;
            using (var dlg = new OpenFileDialog())
            {
                dlg.Title = "Open Akai disk image";
                dlg.Filter = "Akai disk images (*.hfe;*.img)|*.hfe;*.img|All files (*.*)|*.*";
                dlg.Multiselect = true;
                if (dlg.ShowDialog(this) == DialogResult.OK)
                    await LoadFiles(dlg.FileNames);
            }
        }

        async void OnOpenFolder(object sender, EventArgs e)
        {
            if (!ConfirmDiscard()) return;
            using (var dlg = new FolderBrowserDialog())
            {
                dlg.Description = "Choose a folder or drive holding .hfe images";
                if (dlg.ShowDialog(this) != DialogResult.OK) return;

                var files = Directory.GetFiles(dlg.SelectedPath, "*.hfe")
                            .Concat(Directory.GetFiles(dlg.SelectedPath, "*.img"))
                            .OrderBy(f => f, StringComparer.OrdinalIgnoreCase).ToArray();
                if (files.Length == 0)
                {
                    MessageBox.Show(this, "No .hfe or .img images in that folder.",
                        "Nothing to open", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return;
                }
                await LoadFiles(files);
            }
        }

        /// <summary>Adds images to the ones already open, ignoring any that are.</summary>
        async void OnAddImage(object sender, EventArgs e)
        {
            using (var dlg = new OpenFileDialog())
            {
                dlg.Title = "Add disk image to the ones already open";
                dlg.Filter = "Akai disk images (*.hfe;*.img)|*.hfe;*.img|All files (*.*)|*.*";
                dlg.Multiselect = true;
                if (dlg.ShowDialog(this) == DialogResult.OK)
                    await LoadFiles(dlg.FileNames, false);
            }
        }

        Task LoadFiles(string[] files) { return LoadFiles(files, true); }

        async Task LoadFiles(string[] files, bool replace)
        {
            int skipped = 0;
            if (replace)
            {
                // Loading replaces what is open. Without this, opening the same folder
                // twice left two copies of every disk in the tree.
                ResetAll();
            }
            else
            {
                // Adding keeps what is open, so drop anything already loaded - and any
                // repeat within this selection, which HashSet.Add catches for free.
                var seen = new HashSet<string>(_disks.Select(d => FullPath(d.Source)),
                                               StringComparer.OrdinalIgnoreCase);
                var wanted = new List<string>();
                foreach (var f in files)
                {
                    if (seen.Add(FullPath(f))) wanted.Add(f);
                    else skipped++;
                }
                if (wanted.Count == 0)
                {
                    SetStatus(files.Length == 1
                        ? "That image is already open."
                        : "All " + files.Length + " selected images are already open.");
                    return;
                }
                files = wanted.ToArray();
            }

            if (_cts != null) { _cts.Cancel(); _cts.Dispose(); }
            _cts = new CancellationTokenSource();
            var token = _cts.Token;

            _progress.Visible = true;
            _progress.Minimum = 0;
            _progress.Maximum = files.Length;
            _progress.Value = 0;

            var loaded = new List<AkaiDisk>();
            var failures = new List<string>();
            int done = 0;

            try
            {
                await Task.Run(() =>
                {
                    foreach (var f in files)
                    {
                        if (token.IsCancellationRequested) return;
                        try { loaded.Add(AkaiDisk.Load(f)); }
                        catch (Exception ex) { failures.Add(Path.GetFileName(f) + ": " + ex.Message); }

                        int n = Interlocked.Increment(ref done);
                        string name = Path.GetFileName(f);
                        BeginInvoke((Action)(() =>
                        {
                            _progress.Value = Math.Min(n, _progress.Maximum);
                            SetStatus("Decoding " + name + "  (" + n + " of " + files.Length + ")");
                        }));
                    }
                }, token);
            }
            catch (OperationCanceledException) { return; }
            finally { _progress.Visible = false; }

            if (token.IsCancellationRequested) return;

            _disks.AddRange(loaded);
            RebuildTree();
            if (!replace && loaded.Count > 0) SelectDiskNode(loaded[0]);

            int bad = _disks.Sum(d => d.BadCrcSectors);
            int missing = _disks.Sum(d => d.MissingSectors);
            string msg = _disks.Count + " disk(s), " + _disks.Sum(d => d.Entries.Count) + " file(s)";
            msg += (bad == 0 && missing == 0)
                 ? "  -  all sectors read cleanly"
                 : "  -  " + bad + " bad-CRC, " + missing + " unreadable sectors";
            if (skipped > 0) msg += "  -  " + skipped + " already open, skipped";
            if (failures.Count > 0) msg += "  -  " + failures.Count + " image(s) failed";
            SetStatus(msg);

            RememberSession();

            if (failures.Count > 0)
                MessageBox.Show(this, string.Join(Environment.NewLine, failures.Take(20)),
                    "Some images could not be read", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        void OnCloseAll(object sender, EventArgs e)
        {
            if (!ConfirmDiscard()) return;
            ResetAll();
            RememberSession();
            SetStatus("Open a disk image, or a folder of them, to begin.");
        }

        /// <summary>
        /// Drops every loaded disk and everything that caches their contents - the
        /// tree, both list panes, the keyboard and the property grid.
        /// </summary>
        void ResetAll()
        {
            _disks.Clear();
            _editDisk = null;
            _undo.Clear();
            _redo.Clear();
            _baseline = null;
            _baselineDisk = null;
            _kgEntry = null;

            _suppressKgSelect = true;
            _keygroups.Items.Clear();
            _suppressKgSelect = false;

            _piano.Clear();
            _details.Items.Clear();
            ShowPanes(false, false);
            RebuildTree();
        }

        /// <summary>Opening or closing throws away unsaved edits, so ask first.</summary>
        bool ConfirmDiscard()
        {
            int dirty = _disks.Count(d => d.Modified);
            if (dirty == 0) return true;

            return MessageBox.Show(this,
                dirty + " disk image(s) have unsaved changes, which will be lost." +
                Environment.NewLine + Environment.NewLine + "Continue?",
                "Unsaved changes", MessageBoxButtons.OKCancel,
                MessageBoxIcon.Warning) == DialogResult.OK;
        }

        // ---------------------------------------------------------------- tree

        /// <summary>
        /// The disks whose names alone would not tell them apart.
        ///
        /// Add Image can bring in DSKA0000 from two different folders, and a menu offering
        /// two identical entries is no offer at all.
        /// </summary>
        HashSet<string> DiskNameClashes()
        {
            return new HashSet<string>(
                _disks.GroupBy(x => Path.GetFileNameWithoutExtension(x.Source),
                               StringComparer.OrdinalIgnoreCase)
                      .Where(g => g.Count() > 1).Select(g => g.Key),
                StringComparer.OrdinalIgnoreCase);
        }

        /// <summary>
        /// What a disk is called, in the tree and anywhere else that has to name one.
        /// The menus name a disk the way the tree does, which only holds while there is
        /// one rule rather than two.
        /// </summary>
        static string DiskLabel(AkaiDisk d, HashSet<string> clashes)
        {
            string label = Path.GetFileNameWithoutExtension(d.Source);
            if (clashes != null && clashes.Contains(label))
                label += "  [" + FolderLabel(d.Source) + "]";
            return label;
        }

        string DiskLabel(AkaiDisk d) { return DiskLabel(d, DiskNameClashes()); }

        /// <summary>
        /// Which disks and groups are open, so a rebuild can put them back.
        ///
        /// Every structural edit rebuilds the whole tree, because that is the only way to
        /// be certain the tree says what the disks say. But a tree that collapses itself
        /// every time a keygroup is copied is its own kind of wrong, and with a stick of
        /// disks open it throws away a good deal of work.
        ///
        /// Disks are matched by identity - they outlive the nodes that show them - and
        /// their groups by the label held in Name, since Text carries a count that the
        /// edit has usually just changed.
        /// </summary>
        internal sealed class TreeOpenState
        {
            public bool HadNodes;
            public readonly HashSet<AkaiDisk> Disks = new HashSet<AkaiDisk>();
            public readonly Dictionary<AkaiDisk, HashSet<string>> Groups =
                new Dictionary<AkaiDisk, HashSet<string>>();
        }

        /// <summary>
        /// One disk's node, with its two groups under it.
        ///
        /// Pulled out of RebuildTree so that the tree a check builds is the tree the
        /// program builds, rather than a copy of it that can drift.
        /// </summary>
        internal static TreeNode BuildDiskNode(AkaiDisk d, string label)
        {
            // With a whole stick loaded, the title bar cannot say which disk is dirty,
            // so the marker goes on the node. The unmarked text is kept in Name so it
            // can be recomposed without rebuilding the tree.
            var diskNode = new TreeNode { Tag = d, Name = label + "  (" + d.Entries.Count + ")" };
            MarkDiskNode(diskNode, d);

            // Programmes and samples only. A disk carries a drum set and an overall
            // record too, but neither can be edited or played here, so listing them
            // put two groups in the way of the two you actually work in. The summary
            // still counts them, so a disk that has them does not look empty of them.
            AddGroup(diskNode, d, "Programs", 'P');
            AddGroup(diskNode, d, "Samples", 'S');

            return diskNode;
        }

        internal static TreeOpenState CaptureTreeState(TreeView tree)
        {
            var state = new TreeOpenState();
            state.HadNodes = tree.Nodes.Count > 0;

            foreach (TreeNode diskNode in tree.Nodes)
            {
                var d = diskNode.Tag as AkaiDisk;
                if (d == null) continue;

                if (diskNode.IsExpanded) state.Disks.Add(d);

                foreach (TreeNode group in diskNode.Nodes)
                {
                    if (!group.IsExpanded || string.IsNullOrEmpty(group.Name)) continue;

                    if (!state.Groups.ContainsKey(d)) state.Groups[d] = new HashSet<string>();
                    state.Groups[d].Add(group.Name);
                }
            }
            return state;
        }

        internal static void RestoreTreeState(TreeView tree, TreeOpenState state)
        {
            foreach (TreeNode diskNode in tree.Nodes)
            {
                var d = diskNode.Tag as AkaiDisk;
                if (d == null) continue;

                if (state.Disks.Contains(d)) diskNode.Expand();

                HashSet<string> groups;
                if (!state.Groups.TryGetValue(d, out groups)) continue;

                foreach (TreeNode group in diskNode.Nodes)
                    if (group.Name != null && groups.Contains(group.Name)) group.Expand();
            }
        }

        void RebuildTree()
        {
            var open = CaptureTreeState(_tree);

            _tree.BeginUpdate();
            _tree.Nodes.Clear();

            // Add Image can bring in DSKA0000 from two different folders; those need
            // telling apart, so a clashing name carries its folder.
            var clashes = DiskNameClashes();

            foreach (var d in _disks.OrderBy(x => Path.GetFileNameWithoutExtension(x.Source),
                                             StringComparer.OrdinalIgnoreCase))
            {
                _tree.Nodes.Add(BuildDiskNode(d, DiskLabel(d, clashes)));
            }

            RestoreTreeState(_tree, open);

            _tree.EndUpdate();
            FitTreeWidth();
            if (_tree.Nodes.Count > 0)
            {
                // Opening a lone disk is a convenience for arriving at one, not a rule: a
                // rebuild must not reopen a disk that was deliberately closed, so this only
                // applies when there was no tree to have an opinion about.
                if (!open.HadNodes && _tree.Nodes.Count == 1) _tree.Nodes[0].Expand();

                _tree.SelectedNode = _tree.Nodes[0];   // so the detail pane is never blank
            }
            UpdateCommands();
        }

        bool _treeWidthPinned;      // the user has dragged the splitter

        /// <summary>
        /// Sizes the tree to its contents. Names are at most 10 characters - that is all
        /// the directory entry holds - so the panel only has to be wide enough for the
        /// disk labels and the indent, not the 320px a file browser would want.
        /// </summary>
        void FitTreeWidth()
        {
            if (_treeWidthPinned || _mainSplit == null || _tree.Nodes.Count == 0) return;

            int widest = 0;
            foreach (TreeNode disk in _tree.Nodes)
            {
                widest = Math.Max(widest, NodeWidth(disk, 0));
                foreach (TreeNode grp in disk.Nodes)
                {
                    widest = Math.Max(widest, NodeWidth(grp, 1));
                    foreach (TreeNode n in grp.Nodes) widest = Math.Max(widest, NodeWidth(n, 2));
                }
            }

            int want = Math.Max(150, Math.Min(340, widest + 20));   // room for a scrollbar
            TrySetSplitter(_mainSplit, want, 120, 240);
        }

        int NodeWidth(TreeNode n, int depth)
        {
            // Indent per level, plus the root expander and a little breathing room.
            return _tree.Indent * depth + 28 +
                   TextRenderer.MeasureText(n.Text, _tree.Font).Width;
        }

        static string FullPath(string p)
        {
            try { return Path.GetFullPath(p); }
            catch { return p; }   // an unusable path still has to compare as itself
        }

        /// <summary>The containing folder's name, or the drive when it sits in a root.</summary>
        static string FolderLabel(string path)
        {
            try
            {
                string dir = Path.GetDirectoryName(FullPath(path));
                if (string.IsNullOrEmpty(dir)) return "";
                string leaf = Path.GetFileName(dir);
                return leaf.Length > 0 ? leaf : dir.TrimEnd('\\');
            }
            catch { return ""; }
        }

        void SelectDiskNode(AkaiDisk d)
        {
            foreach (TreeNode n in _tree.Nodes)
                if (ReferenceEquals(n.Tag, d)) { _tree.SelectedNode = n; n.EnsureVisible(); return; }
        }

        static void MarkDiskNode(TreeNode node, AkaiDisk d)
        {
            node.Text = (d.Modified ? "* " : "") + node.Name;
            node.ForeColor = d.Modified ? Color.FromArgb(168, 56, 0) : SystemColors.WindowText;
        }

        /// <summary>
        /// Re-marks the disk nodes in place. An edit can make a disk dirty without the
        /// tree being rebuilt - dragging an envelope must not rebuild it - so the markers
        /// are refreshed rather than left to the next rebuild.
        /// </summary>
        void RefreshDiskMarkers()
        {
            foreach (TreeNode n in _tree.Nodes)
            {
                var d = n.Tag as AkaiDisk;
                if (d == null || string.IsNullOrEmpty(n.Name)) continue;

                string want = (d.Modified ? "* " : "") + n.Name;
                if (n.Text != want) MarkDiskNode(n, d);
            }
        }

        static void AddGroup(TreeNode parent, AkaiDisk d, string label, char type)
        {
            var items = d.Entries.Where(x => x.Type == type).ToList();
            if (items.Count == 0) return;

            // Name is the plain label where Text carries the count. The count changes
            // whenever anything is added or deleted, so it is the label a rebuild has to
            // match an open group by - and an empty group is not added at all, which rules
            // out matching them by position.
            var group = new TreeNode(label + "  (" + items.Count + ")") { Name = label };
            foreach (var e in items)
                group.Nodes.Add(new TreeNode(e.Name) { Tag = new FileRef(d, e) });
            parent.Nodes.Add(group);
        }

        // ------------------------------------------------------------- details

        void ShowSelection()
        {
            _details.BeginUpdate();
            _details.Items.Clear();
            ShowPanes(false, false);
            _piano.Clear();
            ShowWaveform(null, null);

            var tag = _tree.SelectedNode != null ? _tree.SelectedNode.Tag : null;

            // Moving the selection while auto-play is on cuts whatever is sounding,
            // rather than leaving the previous sample running over the new selection.
            if (_autoPlay.Checked) StopAudio();

            if (tag is AkaiDisk)
            {
                ShowDisk((AkaiDisk)tag);
                _editDisk = (AkaiDisk)tag;
                ShowFileHeader(null);
                _editArea.Visible = false;
                _details.Visible = true;          // the disk's own numbers
            }
            else if (tag is FileRef) { ShowFile((FileRef)tag); ShowEditor((FileRef)tag); }
            else { ShowFileHeader(null); _editArea.Visible = false; _details.Visible = false; }

            _details.EndUpdate();
            UpdateCommands();

            // A MIDI keyboard plays what is selected, so the engine has to be told the
            // selection moved. Without this it would still be holding whatever the last
            // on-screen key click loaded.
            SyncInstrumentProgram();

            PlayCurrent(false);
        }

        void Row(string name, string value)
        {
            _details.Items.Add(new ListViewItem(new[] { name, value }));
        }

        void ShowDisk(AkaiDisk d)
        {
            Row("Source", d.Source);
            Row("Image size", d.Image.Length.ToString("N0") + " bytes");
            Row("Blocks", d.TotalBlocks + " x " + AkaiDisk.BlockSize + " bytes");
            Row("Blocks used", d.UsedBlocks.ToString());
            Row("Blocks free", d.FreeBlocks.ToString());
            Row("", "");
            Row("Files", d.Entries.Count.ToString());
            Row("  Programs", d.Entries.Count(e => e.Type == 'P').ToString());
            Row("  Samples", d.Entries.Count(e => e.Type == 'S').ToString());
            Row("  Drum sets", d.Entries.Count(e => e.Type == 'D').ToString());
            Row("  Overall", d.Entries.Count(e => e.Type == 'O').ToString());
            Row("", "");
            Row("Bad-CRC sectors", d.BadCrcSectors.ToString());
            Row("Unreadable sectors", d.MissingSectors.ToString());
        }

        void ShowFile(FileRef f)
        {
            var e = f.Entry;
            Row("Name", e.Name);
            Row("Type", e.TypeName);
            Row("Directory slot", e.Slot.ToString());
            Row("Length", e.Length.ToString("N0") + " bytes");
            Row("Start block", e.StartBlock.ToString());
            Row("Blocks allocated", e.ChainBlocks.ToString());
            if (!e.ChainOk) Row("Warning", "allocation smaller than the declared length");

            if (e.Type == 'S')
            {
                Row("", "");
                Row("Sample words", e.SampleCount.ToString("N0"));
                Row("Sample rate", e.SampleRate.ToString("N0") + " Hz");
                Row("Duration", e.Seconds.ToString("0.000", CultureInfo.InvariantCulture) + " s");
                Row("Tuning", e.Tuning + "  (" + e.Semitones.ToString("0.00", CultureInfo.InvariantCulture)
                              + " semitones, C3 = 60)");
                Row("Loop mode", LoopModeName(e.LoopMode));
                Row("Start marker", e.LoopStart.ToString("N0"));
                Row("End marker", e.LoopEnd.ToString("N0"));
                Row("Loop length", e.LoopLength.ToString("N0"));

                // The keygroup pane has nothing to say about a sample, so the waveform
                // takes the whole lower panel.
                ShowWaveform(f.Disk, e);
                ShowPanes(false, true);
            }
            else if (e.Type == 'P')
            {
                int kg = AkaiDisk.KeygroupCount(e);
                Row("", "");
                Row("Keygroups", kg.ToString());
                Row("Layout", "38-byte header + " + kg + " x 70-byte keygroups");
                ShowKeygroups(f, kg);
            }
            else if (e.Type == 'D')
            {
                ShowDrumSet(f);
            }
        }

        void ShowKeygroups(FileRef f, int count)
        {
            if (count == 0) return;

            var onDisk = new HashSet<string>(f.Disk.Entries.Where(x => x.Type == 'S').Select(x => x.Name));
            var groups = f.Disk.Keygroups(f.Entry);

            // An edit rebuilds this list; keep the row selected so the keyboard
            // highlight and the property grid stay on the keygroup being edited.
            int keep = ReferenceEquals(_kgEntry, f.Entry) && _keygroups.SelectedIndices.Count > 0
                     ? _keygroups.SelectedIndices[0] : -1;
            _kgEntry = f.Entry;
            _suppressKgSelect = true;

            _keygroups.BeginUpdate();
            _keygroups.Items.Clear();
            foreach (var kg in groups)
            {
                bool here = onDisk.Contains(kg.Sample1);
                var item = new ListViewItem((kg.Index + 1).ToString());
                item.ToolTipText =
                    "Keys " + kg.KeyRange +
                    (kg.Sample1.Trim().Length > 0 ? "   " + kg.Sample1.Trim() : "") +
                    (kg.HasSecondZone ? " / " + kg.Zone2.Name.Trim() : "") +
                    (here ? "" : "   (sample not on this disk)");
                if (!here) item.ForeColor = Color.FromArgb(150, 90, 0);
                _keygroups.Items.Add(item);
            }
            _keygroups.EndUpdate();

            _piano.SetSpans(groups.Select(kg => new KeySpan(kg.Index, kg.LowKey, kg.HighKey, kg.Sample1)));

            // Arriving at a program with nothing picked leaves the editor beside the list
            // empty, which reads as broken rather than as waiting, so the first keygroup is
            // selected for you. Inside the suppressed block, so it shows without sounding:
            // choosing a program in the tree should not start playing a note.
            if (keep < 0 && _keygroups.Items.Count > 0) keep = 0;

            if (keep >= 0 && keep < _keygroups.Items.Count)
            {
                _keygroups.Items[keep].Selected = true;
                _keygroups.Items[keep].Focused = true;
            }
            _suppressKgSelect = false;
            _piano.SelectedIndex = keep;

            // The waveform follows it, but the grid does not: arriving at a program
            // should leave the program's own properties in the grid, and clicking a
            // number is what asks for the keygroup instead.
            if (keep >= 0) ShowWaveform(f.Disk, KeygroupSample(f));

            _kgHeader.Text = "Keygroups  " + groups.Count;
            ShowPanes(true, true);

            if (keep < 0) ShowWaveform(null, null);
        }

        /// <summary>
        /// Which panes a file needs. A program uses all of them; a sample has no keygroups
        /// and no key ranges, so the keyboard and the number column stand down and the
        /// editor takes the width; a disk has neither.
        /// </summary>
        void ShowPanes(bool keygroups, bool waveform)
        {
            // The velocity strip stands down with the keyboard it belongs to.
            if (_keyRow != null) _keyRow.Visible = keygroups;
            _middleSplit.Panel1Collapsed = !keygroups;
            _outerSplit.Panel2Collapsed = !waveform;
        }

        /// <summary>Draws a sample in the waveform pane, or clears it when there is none.</summary>
        /*
         * The buttons over and beside the waveform.
         *
         * Laid out by hand rather than with a FlowLayoutPanel: the strip is one fixed
         * row that never wraps, and a flow panel that is docked and auto-sizing is more
         * ways for that to go wrong than positions are to write down.
         *
         * Every one of them calls the same method the right-click menu calls. Two ways
         * to reach a thing is fine; two implementations of it is not.
         */
        void BuildWaveButtons()
        {
            _btnTrim = WaveButton("Trim silence", 92, (s, e) => OnWaveAction(TrimSilence));
            _btnHalve = WaveButton("Halve rate", 84, (s, e) => OnWaveAction(HalveSampleRate));
            _btnFit = WaveButton("Fit to tempo", 92, (s, e) => OnWaveAction(StretchSample));
            _btnFindLoop = WaveButton("Find loop", 78, (s, e) => OnWaveAction(FindLoopFor));
            _btnSlice = WaveButton("Slice", 60, (s, e) => OnWaveAction(SliceSampleFile));

            int x = 0;
            foreach (Button b in new[] { _btnTrim, _btnHalve, _btnFit, _btnFindLoop, _btnSlice })
            {
                b.Location = new Point(x, 3);
                _waveActions.Controls.Add(b);
                x += b.Width + 4;
            }
            _waveActions.Dock = DockStyle.Right;
            _waveActions.Width = x + 4;

            _btnPlay = WaveButton("Play", 96, OnPlay);
            _btnStop = WaveButton("Stop", 96, OnStopPlay);
            _btnPlay.Location = new Point(6, 6);
            _btnStop.Location = new Point(6, 36);

            _waveSide.Dock = DockStyle.Right;
            _waveSide.Width = 110;
            _waveSide.Controls.Add(_btnPlay);
            _waveSide.Controls.Add(_btnStop);
        }

        static Button WaveButton(string text, int width, EventHandler onClick)
        {
            var b = new Button
            {
                Text = text,
                Size = new Size(width, 24),
                FlatStyle = FlatStyle.System,

                // The keyboard and the waveform want the focus; a button that steals it
                // would eat the next key press meant for the instrument.
                TabStop = false
            };
            b.Click += onClick;
            return b;
        }

        /// <summary>Run one of the sample edits on whatever the waveform is showing.</summary>
        void OnWaveAction(Action<FileRef> what)
        {
            if (_waveDisk == null || _waveEntry == null || _waveEntry.Type != 'S') return;
            what(new FileRef(_waveDisk, _waveEntry));
        }

        /// <summary>Only a sample can be trimmed, halved, stretched, looped or sliced.</summary>
        void UpdateWaveButtons()
        {
            bool sample = _waveEntry != null && _waveEntry.Type == 'S';

            _btnTrim.Enabled = sample;
            _btnFit.Enabled = sample;
            _btnFindLoop.Enabled = sample;
            _btnSlice.Enabled = sample;
            _btnPlay.Enabled = sample;

            // The rate it would become is worth seeing before pressing it.
            _btnHalve.Enabled = sample;
            _btnHalve.Text = sample
                ? "Halve to " + (_waveEntry.SampleRate / 2000.0).ToString("0.#") + "k"
                : "Halve rate";
        }

        void ShowWaveform(AkaiDisk d, AkaiEntry e)
        {
            _waveDisk = d;
            _waveEntry = e;
            UpdateWaveButtons();

            if (d == null || e == null || e.Type != 'S')
            {
                _wave.Clear();
                _waveHeader.Text = "Waveform";
                return;
            }

            try
            {
                var pcm = d.SampleWords12(e);
                _wave.SetSample(pcm, e.SampleRate, e.LoopStart, e.LoopEnd, e.LoopLength,
                                e.LoopMode, LoopModeName(e.LoopMode));
                _waveHeader.Text = "Waveform  -  " + e.Name + "   " +
                                   e.SampleCount.ToString("N0") + " words at " +
                                   e.SampleRate.ToString("N0") + " Hz,  " +
                                   e.Seconds.ToString("0.00", CultureInfo.InvariantCulture) + " s";
            }
            catch (Exception ex)
            {
                _wave.Clear();
                _waveHeader.Text = "Waveform  -  " + e.Name + ": " + ex.Message;
            }
        }

        AkaiEntry _kgEntry;
        bool _suppressKgSelect;

        /// <summary>Clicking a mapped key on the keyboard picks that keygroup.</summary>
        /// <summary>
        /// Puts the selection on one keygroup and nothing else.
        ///
        /// A multiple selection is built in the list, with ctrl and shift, and nowhere
        /// else. The keyboard has no modifiers to offer and clicking keys is playing
        /// notes, not picking keygroups, so a key click replaces the selection. While the
        /// list was single-select this happened by itself; it has to be said now.
        /// </summary>
        void SelectKeygroupRow(int index)
        {
            if (index < 0 || index >= _keygroups.Items.Count) return;

            // clearing raises a selection change of its own, and an empty one would blank
            // the panes on the way past
            _suppressKgSelect = true;
            _keygroups.SelectedIndices.Clear();
            _suppressKgSelect = false;

            _keygroups.Items[index].Selected = true;
            _keygroups.Items[index].Focused = true;
            _keygroups.EnsureVisible(index);
        }

        /// <summary>
        /// True while a key strike is moving the keygroup selection to match.
        ///
        /// The selection still has to move - the editor, the waveform and the piano all
        /// follow it - but it must not audition on the way past, because the key is
        /// already sounding the note. Suppressing the selection itself would instead
        /// leave the panes describing the wrong keygroup.
        /// </summary>
        bool _selectingFromKey;

        void OnKeygroupSelected()
        {
            if (_suppressKgSelect) return;
            var picked = SelectedKeygroups();
            int i = picked.Count > 0 ? picked[0] : -1;
            _piano.SelectedIndex = i;
            _piano.AlsoSelected = picked.Count > 1 ? picked.GetRange(1, picked.Count - 1) : null;
            if (i < 0) { ShowWaveform(null, null); return; }

            ShowKeygroupEditor();

            var f = SelectedFile;
            if (f != null && f.Entry.Type == 'P') ShowWaveform(f.Disk, KeygroupSample(f));

            if (!_selectingFromKey) PlayCurrent(false);
        }

        /// <summary>
        /// A drum set holds eight voices, each answering to one MIDI note. Only the
        /// layout and the note are decoded, so the rest of each record is shown as
        /// bytes rather than invented names.
        /// </summary>
        void ShowDrumSet(FileRef f)
        {
            var set = f.Disk.ReadDrumSet(f.Entry);
            if (set == null) return;

            Row("", "");
            Row("Layout", AkaiDisk.DrumSet.HeaderBytes + "-byte header + " +
                          AkaiDisk.DrumSet.Voices + " x " + AkaiDisk.DrumSet.VoiceBytes +
                          "-byte voices");
            Row("In use", set.InUse ? "yes" : "no  -  left at defaults");
            Row("", "");

            foreach (var v in set.Records)
            {
                var hex = new System.Text.StringBuilder();
                for (int i = 3; i < v.Raw.Length; i++) hex.Append(v.Raw[i].ToString("x2")).Append(' ');

                Row("Voice " + v.Index + "  note " + v.Note + " (" + v.NoteName + ")",
                    hex.ToString().TrimEnd());
            }

            Row("", "");
            Row("Note", "Only the record layout and the note assignment are decoded. " +
                        "See the format notes.");
        }

        static string LoopModeName(char m)
        {
            switch (m)
            {
                case 'O': return "O  -  one-shot";
                case 'L': return "L  -  loop";
                case 'A': return "A  -  alternating";
                default: return m >= 0x20 && m < 0x7F ? m.ToString() : "unknown";
            }
        }

        // -------------------------------------------------------------- export

        FileRef SelectedFile
        {
            get
            {
                var n = _tree.SelectedNode;
                return n != null ? n.Tag as FileRef : null;
            }
        }

        /// <summary>The disk the current selection belongs to, or the only one loaded.</summary>
        AkaiDisk TargetDisk
        {
            get
            {
                var n = _tree.SelectedNode;
                while (n != null)
                {
                    if (n.Tag is AkaiDisk) return (AkaiDisk)n.Tag;
                    var fr = n.Tag as FileRef;
                    if (fr != null) return fr.Disk;
                    n = n.Parent;
                }
                return _disks.Count == 1 ? _disks[0] : null;
            }
        }

        void UpdateCommands()
        {
            _exportItem.Enabled = SelectedFile != null;
            UpdateToolbar();
            _importItem.Enabled = TargetDisk != null;

            bool dirty = _editDisk != null && _editDisk.Modified;
            _saveItem.Enabled = _editDisk != null;
            _saveItem.Text = dirty ? "&Save Disk Image As...  *" : "&Save Disk Image As...";

            int modified = _disks.Count(d => d.Modified);
            _saveAllItem.Enabled = modified > 0;
            _saveAllItem.Text = modified > 0
                ? "Save A&ll Modified...  (" + modified + ")"
                : "Save A&ll Modified...";

            RefreshDiskMarkers();

            _undoItem.Enabled = _undo.Count > 0;
            _undoItem.Text = _undo.Count > 0 ? "&Undo " + _undo[_undo.Count - 1].What : "&Undo";
            _redoItem.Enabled = _redo.Count > 0;
            _redoItem.Text = _redo.Count > 0 ? "&Redo " + _redo[_redo.Count - 1].What : "&Redo";

            var sel = SelectedFile;
            _newProgramItem.Enabled = TargetDisk != null;
            _sliceItem.Enabled = sel != null && sel.Entry.Type == 'S';

            // Only a sample has audio to put in a WAV; a programme is settings.
            _exportWavItem.Enabled = sel != null && sel.Entry.Type == 'S';

            // Only samples and programs can go: the overall settings and the drum set are
            // part of the disk's furniture rather than files anyone put there.
            bool deletable = sel != null && (sel.Entry.Type == 'S' || sel.Entry.Type == 'P');
            _deleteItem.Enabled = deletable;
            _deleteItem.Text = deletable
                ? "&Delete " + sel.Entry.TypeName + " " + sel.Entry.Name.Trim() + "..."
                : "&Delete File...";

            Text = AppTitle +
                   (modified == 0 ? "" :
                    modified == 1 ? "  -  1 disk with unsaved changes"
                                  : "  -  " + modified + " disks with unsaved changes");
        }

        /*
         * The name, and when this copy of it was built.
         *
         * The web version carries a build stamp beside its title so that a browser
         * running a cached copy is obvious at a glance rather than after an hour of
         * hunting a bug that was fixed. A program on disk has the same problem in a
         * different costume: the executable cannot be replaced while it is running,
         * so a build can quietly not happen and the window looks identical either
         * way. That has now happened twice.
         *
         * Taken from the file rather than from a constant somebody has to remember
         * to bump, because the whole point is that it cannot be out of date.
         */
        static string _appTitle;

        static string AppTitle
        {
            get
            {
                if (_appTitle != null) return _appTitle;

                _appTitle = "Mz950 Studio";
                try
                {
                    string exe = System.Reflection.Assembly.GetExecutingAssembly().Location;
                    if (!string.IsNullOrEmpty(exe) && File.Exists(exe))
                        _appTitle += "   " + File.GetLastWriteTime(exe).ToString("yyyy-MM-dd HH:mm");
                }
                catch { /* a title that throws is worse than a title without a date */ }

                return _appTitle;
            }
        }

        /// <summary>Delete whichever of the two deletable file types is selected.</summary>
        void OnDeleteFile(object sender, EventArgs e)
        {
            var f = SelectedFile;
            if (f == null) return;

            if (f.Entry.Type == 'S') DeleteSampleFile(f);
            else if (f.Entry.Type == 'P') DeleteProgramFile(f);
        }

        /// <summary>
        /// Saves the selected sample as a WAV, for anything that is not an S950.
        ///
        /// The raw export beside this one writes the file as the disk holds it, which is
        /// the right thing for putting back on another disk and no use at all in a DAW.
        /// This is the other half: the audio, at its own rate, with the loop described in
        /// the file rather than baked into it. See WavFile.
        /// </summary>
        void OnExportWav(object sender, EventArgs e)
        {
            var f = SelectedFile;
            if (f == null || f.Entry.Type != 'S') return;

            using (var dlg = new SaveFileDialog())
            {
                dlg.Title = "Export sample as WAV";
                dlg.Filter = "WAV audio (*.wav)|*.wav|All files (*.*)|*.*";
                dlg.FileName = MakeSafe(f.Entry.Name) + ".wav";
                if (dlg.ShowDialog(this) != DialogResult.OK) return;

                try
                {
                    var pcm = f.Disk.SamplePcm(f.Entry);
                    if (pcm.Length == 0)
                    {
                        MessageBox.Show(this, "That sample has no audio on the disk.",
                            "Nothing to export", MessageBoxButtons.OK, MessageBoxIcon.Information);
                        return;
                    }

                    var bytes = WavFile.Build(pcm, f.Entry.SampleRate, f.Entry.LoopMode, f.Entry.LoopStart,
                                              f.Entry.LoopEnd, f.Entry.LoopLength, f.Entry.Tuning);
                    File.WriteAllBytes(dlg.FileName, bytes);

                    bool looped = (f.Entry.LoopMode == 'L' || f.Entry.LoopMode == 'A')
                                  && f.Entry.LoopLength >= 2;

                    SetStatus("Exported " + f.Entry.Name.Trim() + " to " + dlg.FileName
                              + "  -  " + pcm.Length + " samples at " + f.Entry.SampleRate + " Hz"
                              + (looped ? ", loop written into the file" : ", one-shot"));
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, ex.Message, "Export failed",
                        MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
        }

        void OnExport(object sender, EventArgs e)
        {
            var f = SelectedFile;
            if (f == null) return;

            using (var dlg = new SaveFileDialog())
            {
                dlg.Title = "Export file as stored on disk";
                dlg.Filter = "Akai file (*.akai)|*.akai|All files (*.*)|*.*";
                dlg.FileName = MakeSafe(f.Entry.Name) + ".akai";
                if (dlg.ShowDialog(this) != DialogResult.OK) return;

                try
                {
                    File.WriteAllBytes(dlg.FileName, f.Disk.ReadFile(f.Entry));
                    SetStatus("Exported " + f.Entry.Name + " to " + dlg.FileName);
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, ex.Message, "Export failed",
                        MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
        }

        // -------------------------------------------------------------- importing

        void OnImportSample(object sender, EventArgs e)
        {
            var d = TargetDisk;
            if (d == null)
            {
                MessageBox.Show(this, "Select the disk to add the sample to first.",
                                "Add sample", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            // The picker decodes the file to preview it, so it hands the audio back rather
            // than leaving it to be read a second time here.
            string path;
            AudioClip clip;

            StopAudio();
            using (var dlg = new AudioFileDialog(
                       "Add a sample to " + Path.GetFileNameWithoutExtension(d.Source),
                       Path.GetDirectoryName(d.Source)))
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                path = dlg.ChosenPath;
                clip = dlg.ChosenClip;
            }

            if (clip == null || clip.Mono.Length == 0)
            {
                MessageBox.Show(this, "That file contains no audio.", "Add sample",
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            using (var dlg = new ImportDialog(clip, d, Path.GetFileNameWithoutExtension(path)))
            {
                if (dlg.ShowDialog(this) != DialogResult.OK || dlg.Words12 == null) return;

                try
                {
                    PushUndo(d, "add sample " + dlg.SampleName);
                    var added = d.AddSample(dlg.SampleName, dlg.Words12, dlg.TargetRate,
                                            dlg.NominalPitch, 0, dlg.LoopMode);

                    _editDisk = d;
                    RebuildTree();
                    SelectFileNode(d, added);
                    UpdateCommands();
                    SetStatus("Added " + added.Name + "  -  " + added.SampleCount.ToString("N0") +
                              " words, " + added.Length.ToString("N0") + " bytes, " +
                              d.FreeBlocks + " blocks still free.  Unsaved changes.");
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, ex.Message, "Could not add the sample",
                                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
        }

        // ------------------------------------------------- sample context menu

        /// <summary>Right-clicking a file selects it and offers what applies to it.</summary>
        void OnTreeRightClick(object sender, TreeNodeMouseClickEventArgs e)
        {
            if (e.Button != MouseButtons.Right) return;

            _tree.SelectedNode = e.Node;
            var f = e.Node.Tag as FileRef;
            if (f == null) return;

            var menu = new ContextMenuStrip();
            if (f.Entry.Type == 'S')
            {
                menu.Items.Add(new ToolStripMenuItem("&Play", null, (s, a) => PlayCurrent(true)));
                menu.Items.Add(new ToolStripSeparator());
                menu.Items.Add(new ToolStripMenuItem(
                    "&Halve Sample Rate  (" + f.Entry.SampleRate.ToString("N0") + " to " +
                    (f.Entry.SampleRate / 2).ToString("N0") + " Hz)", null,
                    (s, a) => HalveSampleRate(f)));
                menu.Items.Add(new ToolStripMenuItem("&Trim Leading Silence...", null,
                    (s, a) => TrimSilence(f)));
                menu.Items.Add(new ToolStripMenuItem("&Fit to Tempo...", null,
                    (s, a) => StretchSample(f)));
                menu.Items.Add(new ToolStripMenuItem("&Find Loop...", null,
                    (s, a) => FindLoopFor(f)));
                menu.Items.Add(new ToolStripMenuItem("S&lice into One-shots...", null,
                    (s, a) => SliceSampleFile(f)));
                menu.Items.Add(new ToolStripSeparator());
            }
            if (f.Entry.Type == 'P' || f.Entry.Type == 'S')
                menu.Items.Add(new ToolStripMenuItem("Re&name...", null, (s, a) => RenameFile(f)));
            menu.Items.Add(new ToolStripMenuItem("&Export...", null, OnExport));
            if (f.Entry.Type == 'S')
                menu.Items.Add(new ToolStripMenuItem("Export as &WAV...", null, OnExportWav));

            if (f.Entry.Type == 'S' || f.Entry.Type == 'P')
                menu.Items.Add(BuildCopyToMenu(f));

            if (f.Entry.Type == 'S' || f.Entry.Type == 'P')
            {
                menu.Items.Add(new ToolStripSeparator());
                menu.Items.Add(new ToolStripMenuItem(
                    "&Delete " + f.Entry.TypeName + "...", null,
                    (s, a) => { if (f.Entry.Type == 'S') DeleteSampleFile(f); else DeleteProgramFile(f); }));
            }

            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add(new ToolStripMenuItem("New &Program...", null,
                (s, a) => CreateProgram(f.Disk)));

            menu.Show(_tree, e.Location);
        }

        /// <summary>
        /// The lowest rate anywhere in the 101-image corpus. The sampler's own floor is
        /// not documented here, so going below what the hardware was seen to write is
        /// flagged rather than forbidden.
        /// </summary>
        const int LowestKnownRate = 11773;

        /// <summary>Well below anything plausible - halving past this is refused.</summary>
        const int UnusableRate = 4000;

        // ------------------------------------------------------ keygroup editing

        void OnKeygroupRightClick(object sender, MouseEventArgs e)
        {
            if (e.Button != MouseButtons.Right) return;

            var f = SelectedFile;
            if (f == null || f.Entry.Type != 'P') return;

            var hit = _keygroups.HitTest(e.Location);
            int row = hit.Item != null ? hit.Item.Index : -1;

            // Right-clicking outside the selection picks that row; right-clicking inside
            // one leaves it alone, so "Set Key Range of 5 Keygroups" still means five.
            if (row >= 0 && !_keygroups.Items[row].Selected) SelectKeygroupRow(row);

            int count = AkaiDisk.KeygroupCount(f.Entry);
            var menu = new ContextMenuStrip();

            menu.Items.Add(new ToolStripMenuItem(
                row >= 0 ? "&Add Keygroup  (copy of " + (row + 1) + ")" : "&Add Keygroup",
                null, (s, a) => AddKeygroup(f, row))
            { Enabled = count < AkaiDisk.MaxKeygroups });

            menu.Items.Add(new ToolStripMenuItem(
                row >= 0 ? "&Delete Keygroup " + (row + 1) : "&Delete Keygroup",
                null, (s, a) => DeleteKeygroup(f, row))
            { Enabled = row >= 0 && count > 1 });

            // Onto another program, here or on another disk, with the samples it names.
            menu.Items.Add(new ToolStripMenuItem(
                row >= 0 ? "&Copy Keygroup " + (row + 1) + " to..." : "&Copy Keygroup to...",
                null, (s, a) => CopyKeygroupTo(f, row))
            { Enabled = row >= 0 });

            menu.Items.Add(new ToolStripSeparator());
            int picked = _keygroups.SelectedIndices.Count;
            menu.Items.Add(new ToolStripMenuItem(
                picked > 1 ? "Set Key &Range of " + picked + " Keygroups from the Keyboard"
                           : "Set Key &Range from the Keyboard",
                null, (s, a) => ArmKeyRange())
            { Enabled = row >= 0 });

            menu.Show(_keygroups, e.Location);
        }

        void AddKeygroup(FileRef f, int copyFrom)
        {
            try
            {
                PushUndo(f.Disk, "add keygroup");
                int now = f.Disk.AddKeygroup(f.Entry, copyFrom);
                AfterKeygroupChange(f, now - 1,
                    "Added keygroup " + now + " to " + f.Entry.Name +
                    (copyFrom >= 0 ? " (copied from " + (copyFrom + 1) + ")" : "") + ".");
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Could not add a keygroup",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        void DeleteKeygroup(FileRef f, int index)
        {
            if (index < 0) return;

            var groups = f.Disk.Keygroups(f.Entry);
            string what = index < groups.Count
                ? "keygroup " + (index + 1) + "  (keys " + groups[index].KeyRange +
                  ", sample " + groups[index].Sample1 + ")"
                : "keygroup " + (index + 1);

            if (MessageBox.Show(this, "Delete " + what + " from " + f.Entry.Name + "?" +
                    Environment.NewLine + Environment.NewLine +
                    "This edits the loaded image only - the file on disk is not touched " +
                    "until you save.",
                    "Delete keygroup", MessageBoxButtons.OKCancel,
                    MessageBoxIcon.Question) != DialogResult.OK) return;

            try
            {
                PushUndo(f.Disk, "delete keygroup " + (index + 1));
                int now = f.Disk.DeleteKeygroup(f.Entry, index);
                AfterKeygroupChange(f, Math.Min(index, now - 1),
                    "Deleted keygroup " + (index + 1) + " from " + f.Entry.Name + ".");
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Could not delete the keygroup",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        /// <summary>The entry object is replaced by the reparse, so everything is rebuilt.</summary>
        void AfterKeygroupChange(FileRef f, int selectRow, string message)
        {
            _editDisk = f.Disk;
            int slot = f.Entry.Slot;

            foreach (var fresh in f.Disk.Entries)
                if (fresh.Slot == slot) { f.Entry = fresh; break; }

            ShowSelection();
            if (selectRow >= 0 && selectRow < _keygroups.Items.Count)
                _keygroups.Items[selectRow].Selected = true;

            UpdateCommands();
            SetStatus(message + "  " + AkaiDisk.KeygroupCount(f.Entry) + " keygroups, " +
                      f.Entry.Length.ToString("N0") + " bytes.  Unsaved changes.");
        }

        /// <summary>Renames a program or sample, in the directory and in the file.</summary>
        void RenameFile(FileRef f)
        {
            using (var dlg = new RenameDialog(f.Disk, f.Entry))
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;

                string was = f.Entry.Name;
                try
                {
                    PushUndo(f.Disk, "rename " + was);
                    int refs = f.Disk.RenameFile(f.Entry, dlg.ChosenName);

                    _editDisk = f.Disk;
                    int slot = f.Entry.Slot;
                    RebuildTree();

                    foreach (var fresh in f.Disk.Entries)
                        if (fresh.Slot == slot) { SelectFileNode(f.Disk, fresh); break; }

                    UpdateCommands();
                    SetStatus("Renamed " + was + " to " + dlg.ChosenName +
                              (refs > 0 ? "  -  " + refs + " keygroup reference" +
                                          (refs == 1 ? "" : "s") + " updated" : "") +
                              ".  Unsaved changes.");
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, ex.Message, "Could not rename",
                                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
        }

        /// <summary>
        /// Fits a sample to a tempo, either by stretching it or by varispeed. Stretching
        /// can make the file longer, so the dialog checks the free space before it lets
        /// the edit through and the disk layer checks again on the way in.
        /// </summary>
        void StretchSample(FileRef f)
        {
            var e = f.Entry;
            using (var dlg = new StretchDialog(f.Disk, e))
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;

                try
                {
                    Cursor = Cursors.WaitCursor;
                    _audio.Stop();

                    string what;
                    try
                    {
                        if (dlg.UseVarispeed)
                        {
                            PushUndo(f.Disk, "retune " + e.Name);
                            f.Disk.SetSampleRate(e, dlg.NewRate);
                            what = "now plays at " + dlg.NewRate.ToString("N0") + " Hz";
                        }
                        else
                        {
                            if (dlg.Words == null || dlg.Words.Length < 2)
                                throw new InvalidOperationException("The stretch produced no audio.");

                            PushUndo(f.Disk, "stretch " + e.Name);
                            f.Disk.ReplaceSampleAudio(e, dlg.Words, e.SampleRate);
                            what = "stretched to " + dlg.Words.Length.ToString("N0") + " words";
                        }
                    }
                    finally { Cursor = Cursors.Default; }

                    _editDisk = f.Disk;
                    int slot = e.Slot;
                    RebuildTree();
                    foreach (var fresh in f.Disk.Entries)
                        if (fresh.Slot == slot) { SelectFileNode(f.Disk, fresh); break; }

                    UpdateCommands();
                    SetStatus(e.Name + " " + what + "  -  " + f.Disk.FreeBlocks +
                              " blocks free.  Unsaved changes.");
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, ex.Message, "Could not fit the sample to that tempo",
                                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
        }

        /// <summary>
        /// Anything at or below this counts as silence: about -48 dB of the 12-bit range,
        /// low enough to leave a quiet attack alone.
        /// </summary>
        const int SilenceThreshold = 8;

        /// <summary>Removes the silence before a sample starts, freeing whole blocks of it.</summary>
        void TrimSilence(FileRef f)
        {
            var e = f.Entry;
            var plan = f.Disk.PlanTrim(e, SilenceThreshold);

            if (!plan.Anything)
            {
                MessageBox.Show(this, e.Name + " starts straight away - there is no leading " +
                    "silence above the noise floor to remove.",
                    "Trim leading silence", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            var ask = MessageBox.Show(this,
                e.Name + Environment.NewLine + Environment.NewLine +
                "Silence   " + plan.Front.ToString("N0") + " words  (" +
                plan.Seconds.ToString("0.000", CultureInfo.InvariantCulture) + " s)" +
                Environment.NewLine +
                "Words     " + e.SampleCount.ToString("N0") + "  ->  " + plan.NewWords.ToString("N0") +
                Environment.NewLine +
                "Frees     " + plan.BlocksFreed + " block" + (plan.BlocksFreed == 1 ? "" : "s") +
                Environment.NewLine + Environment.NewLine +
                "Only the start is trimmed. Loop and end markers move with the audio." +
                Environment.NewLine + Environment.NewLine +
                "Go ahead?",
                "Trim leading silence", MessageBoxButtons.OKCancel, MessageBoxIcon.Question);
            if (ask != DialogResult.OK) return;

            try
            {
                Cursor = Cursors.WaitCursor;
                _audio.Stop();

                PushUndo(f.Disk, "trim " + e.Name);
                int freed;
                try { freed = f.Disk.TrimSample(e, SilenceThreshold); }
                finally { Cursor = Cursors.Default; }

                _editDisk = f.Disk;
                int slot = e.Slot;
                RebuildTree();
                foreach (var fresh in f.Disk.Entries)
                    if (fresh.Slot == slot) { SelectFileNode(f.Disk, fresh); break; }

                UpdateCommands();
                SetStatus("Trimmed " + plan.Front.ToString("N0") + " words of silence from " +
                          e.Name + "  -  " + freed + " blocks freed, " + f.Disk.FreeBlocks +
                          " now free.  Unsaved changes.");
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Could not trim the sample",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        /// <summary>
        /// Halves a sample's rate in place, which halves the space it takes. The audio is
        /// low-passed on the way down, so it loses its top octave rather than aliasing.
        /// </summary>
        void HalveSampleRate(FileRef f)
        {
            var e = f.Entry;
            int newRate = e.SampleRate / 2;
            if (newRate < UnusableRate)
            {
                MessageBox.Show(this, e.Name + " is already at " + e.SampleRate.ToString("N0") +
                    " Hz. Halving it again would leave it unusable.",
                    "Halve sample rate", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            long words = e.SampleCount / 2 & ~1L;
            int newLength = AkaiDisk.HeaderSize + (int)(words * 3 / 2);
            int freed = AkaiDisk.BlocksFor(e.Length) - AkaiDisk.BlocksFor(newLength);

            var ask = MessageBox.Show(this,
                e.Name + Environment.NewLine + Environment.NewLine +
                "Rate     " + e.SampleRate.ToString("N0") + " Hz  ->  " + newRate.ToString("N0") + " Hz" +
                Environment.NewLine +
                "Words    " + e.SampleCount.ToString("N0") + "  ->  " + words.ToString("N0") +
                Environment.NewLine +
                "Size     " + e.Length.ToString("N0") + "  ->  " + newLength.ToString("N0") + " bytes" +
                Environment.NewLine +
                "Frees    " + freed + " block" + (freed == 1 ? "" : "s") +
                Environment.NewLine + Environment.NewLine +
                "Pitch and duration are unchanged; the sample loses its top octave of " +
                "bandwidth. This edits the loaded image only - the file on disk is not " +
                "touched until you save." + Environment.NewLine +
                (newRate < LowestKnownRate
                    ? Environment.NewLine + newRate.ToString("N0") + " Hz is below " +
                      LowestKnownRate.ToString("N0") + " Hz, the lowest rate found anywhere " +
                      "in this library, so it is untested on the hardware." + Environment.NewLine
                    : "") +
                Environment.NewLine + "Go ahead?",
                "Halve sample rate", MessageBoxButtons.OKCancel, MessageBoxIcon.Question);
            if (ask != DialogResult.OK) return;

            try
            {
                Cursor = Cursors.WaitCursor;
                _audio.Stop();

                PushUndo(f.Disk, "halve " + e.Name);
                int actuallyFreed;
                try
                {
                    var halved = AudioImport.HalveRate(f.Disk.SampleWords12(e));
                    actuallyFreed = f.Disk.ReplaceSampleData(e, halved, newRate);
                }
                finally { Cursor = Cursors.Default; }

                _editDisk = f.Disk;
                int slot = e.Slot;
                RebuildTree();

                foreach (var fresh in f.Disk.Entries)
                    if (fresh.Slot == slot) { SelectFileNode(f.Disk, fresh); break; }

                UpdateCommands();
                SetStatus("Halved " + e.Name + " to " + newRate.ToString("N0") + " Hz  -  " +
                          actuallyFreed + " blocks freed, " + f.Disk.FreeBlocks +
                          " now free.  Unsaved changes.");
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "Could not halve the sample",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        /// <summary>
        /// Writes every modified image into a folder, under its own name. Saving one disk
        /// at a time is how you come to lose the edits you made to the other four.
        /// </summary>
        void OnSaveAll(object sender, EventArgs e)
        {
            var dirty = _disks.Where(d => d.Modified).ToList();
            if (dirty.Count == 0) return;

            string folder;
            using (var dlg = new FolderBrowserDialog())
            {
                dlg.Description = "Save " + dirty.Count + " modified image(s) into which folder?";
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                folder = dlg.SelectedPath;
            }

            var done = new List<string>();
            var failed = new List<string>();

            Cursor = Cursors.WaitCursor;
            try
            {
                foreach (var d in dirty)
                {
                    string target = Path.Combine(folder, Path.GetFileName(d.Source));
                    try
                    {
                        if (string.Equals(Path.GetFullPath(target), Path.GetFullPath(d.Source),
                                          StringComparison.OrdinalIgnoreCase))
                        {
                            failed.Add(Path.GetFileName(d.Source) + ": that is where it came from");
                            continue;
                        }

                        d.SaveAs(target);

                        var check = AkaiDisk.Load(target);
                        if (check.BadCrcSectors > 0 || check.MissingSectors > 0)
                        {
                            failed.Add(Path.GetFileName(target) + ": does not verify - " +
                                       check.BadCrcSectors + " bad-CRC, " +
                                       check.MissingSectors + " unreadable");
                            continue;
                        }

                        d.Modified = false;
                        done.Add(Path.GetFileName(target));
                    }
                    catch (Exception ex) { failed.Add(Path.GetFileName(d.Source) + ": " + ex.Message); }
                }
            }
            finally { Cursor = Cursors.Default; }

            RebuildTree();
            UpdateCommands();
            SetStatus("Saved and verified " + done.Count + " image(s)" +
                      (failed.Count > 0 ? ", " + failed.Count + " failed" : "") + ".");

            if (failed.Count > 0)
                MessageBox.Show(this, string.Join(Environment.NewLine, failed.Take(20)),
                                "Some images were not saved", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        void SelectFileNode(AkaiDisk d, AkaiEntry entry)
        {
            foreach (TreeNode disk in _tree.Nodes)
                foreach (TreeNode grp in disk.Nodes)
                    foreach (TreeNode n in grp.Nodes)
                    {
                        var fr = n.Tag as FileRef;
                        if (fr != null && fr.Disk == d && fr.Entry.Slot == entry.Slot)
                        {
                            _tree.SelectedNode = n;
                            n.EnsureVisible();
                            return;
                        }
                    }
        }

        static string MakeSafe(string name)
        {
            var bad = Path.GetInvalidFileNameChars();
            var s = new string(name.Select(c => bad.Contains(c) ? '_' : c).ToArray()).Trim();
            return s.Length == 0 ? "akai-file" : s;
        }

        /// <summary>
        /// The walk-through, opened in whatever shows HTML here.
        ///
        /// It ships as a file rather than a link because this program has no other
        /// dependency on being online, and somebody reading it is quite likely to be at a
        /// sampler in a room with a Gotek and no wifi. The web copy is the fallback for a
        /// build running from somewhere the file did not come along to.
        /// </summary>
        void OnTutorial(object sender, EventArgs e)
        {
            const string OnTheWeb = "https://github.com/simozzer/AkaiS950Web/blob/main/docs/tutorial.md";

            // Beside the program when installed, and one level up when run from the
            // repository, where the exe is built at the root and docs sits beside it.
            string here = Path.GetDirectoryName(Application.ExecutablePath) ?? ".";

            var tried = new[]
            {
                Path.Combine(here, "docs", "tutorial.html"),
                Path.Combine(here, "tutorial.html")
            };

            foreach (string path in tried)
            {
                if (!File.Exists(path)) continue;

                try { Process.Start(path); return; }
                catch (Exception ex)
                {
                    MessageBox.Show(this, ex.Message, "Could not open the tutorial",
                                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
            }

            try { Process.Start(OnTheWeb); }
            catch (Exception ex)
            {
                MessageBox.Show(this,
                    "The tutorial was not installed beside the program, and the web copy " +
                    "could not be opened either." + Environment.NewLine + Environment.NewLine +
                    ex.Message + Environment.NewLine + Environment.NewLine + OnTheWeb,
                    "No tutorial to show",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        void OnAbout(object sender, EventArgs e)
        {
            MessageBox.Show(this,
                "Mz950 Studio" + Environment.NewLine + Environment.NewLine +
                "Reads Akai S900/S950 floppy images, 800K and 1600K HD (.hfe and raw .img)," +
                Environment.NewLine +
                "decoding the MFM bitstream, directory, allocation table," +
                Environment.NewLine +
                "sample headers and program keygroups." + Environment.NewLine + Environment.NewLine +
                "Parameters can be edited, samples and programs added, sliced" +
                Environment.NewLine +
                "and deleted, and the image written back out as either .hfe or" +
                Environment.NewLine +
                ".img; the original file is never overwritten." +
                Environment.NewLine + Environment.NewLine +

                // The licence asks an interactive program to say so where it can be seen.
                "Copyright (C) 2026 Simon Moscrop" + Environment.NewLine +
                "Licensed under the GNU Affero General Public License v3." +
                Environment.NewLine +
                "This program comes with ABSOLUTELY NO WARRANTY. It is free" +
                Environment.NewLine +
                "software, and you are welcome to redistribute it under" +
                Environment.NewLine +
                "those terms; see the LICENSE file, or gnu.org/licenses." +
                Environment.NewLine + Environment.NewLine +
                "Source: github.com/simozzer/Mz950" +
                Environment.NewLine + Environment.NewLine +

                // Compatible with Akai's disks; not Akai's product, and says so.
                "An independent project. Akai, S900 and S950 are trademarks" +
                Environment.NewLine +
                "of their respective owners; Mz950 is not affiliated with or" +
                Environment.NewLine +
                "endorsed by Akai Professional or inMusic." +
                Environment.NewLine + Environment.NewLine +
                AppTitle.Substring(AppTitle.IndexOf("Studio") + 6).Trim(),

                "About", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        // ---------------------------------------------------------------- edit

        AkaiDisk _editDisk;

        void ShowEditor(FileRef f)
        {
            _editDisk = f != null ? f.Disk : null;

            ShowFileHeader(f);
            LoadKeygroupPane(null, null, 0, 0);
            _envPanel.Visible = false;              // envelopes belong to a keygroup

            // A programme gets the keygroup editor. A sample has nothing here - its
            // header is on the strip and its waveform is along the bottom.
            bool programme = f != null && f.Entry.Type == 'P';
            _details.Visible = false;
            _editArea.Visible = programme;
        }

        // ---------------------------------------------------------- envelopes

        const int VcaAttackByte = 3, VcfAttackByte = 34, VcfAmountByte = 23;

        /// <summary>Loads the selected keygroup's two envelopes into the editors.</summary>
        void LoadEnvelopes(AkaiDisk.Keygroup kg)
        {
            if (kg == null) { _envPanel.Visible = false; return; }

            _loadingEnvelopes = true;
            try
            {
                _vcaEnv.SetValues(kg.VcaAttack, kg.VcaDecay, kg.VcaSustain, kg.VcaRelease);

                /*
                 * A VCF envelope that was never written is four spaces - 0x20 - left
                 * over from the padding, and drawing that as 32/32/32/32 is a shape
                 * the sampler will not play. AkaiS950Engine already ignores those
                 * bytes and runs the filter wide open, so show what will be heard:
                 * no attack, no decay, full sustain. The web version draws the same
                 * thing for the same reason.
                 */
                bool vcf = AkaiS950Engine.KeygroupPatch.LooksWritten(
                    kg.VcfAttack, kg.VcfDecay, kg.VcfSustain, kg.VcfRelease);

                if (vcf)
                    _vcfEnv.SetValues(kg.VcfAttack, kg.VcfDecay, kg.VcfSustain, kg.VcfRelease);
                else
                    _vcfEnv.SetValues(0, 0, 99, 0);
                _vcfAmount.Value = Math.Max(_vcfAmount.Minimum,
                                   Math.Min(_vcfAmount.Maximum, kg.VcfAmount));
                _vcfAmountLabel.Text = "amt" + Environment.NewLine + kg.VcfAmount.ToString("+0;-0;0");
            }
            finally { _loadingEnvelopes = false; }

            _envPanel.Visible = true;
        }

        bool _loadingEnvelopes;

        /// <summary>Writes one byte of the selected keygroup's record.</summary>
        /// <summary>
        /// The keygroups an edit reaches: every selected row, the focused one first since
        /// that is the one the grid, the envelopes and the keyboard are showing.
        /// </summary>
        List<int> SelectedKeygroups()
        {
            var all = new List<int>();
            var focus = _keygroups.FocusedItem;
            int lead = focus != null && focus.Selected ? focus.Index : -1;
            if (lead >= 0) all.Add(lead);
            foreach (int i in _keygroups.SelectedIndices) if (i != lead) all.Add(i);
            return all;
        }

        /// <summary>One byte into every selected keygroup - the envelopes and the VCF amount.</summary>
        void PokeKeygroup(int offset, int value)
        {
            var f = SelectedFile;
            if (f == null || f.Entry.Type != 'P') return;

            int count = AkaiDisk.KeygroupCount(f.Entry);
            foreach (int i in SelectedKeygroups())
            {
                if (i < 0 || i >= count) continue;
                f.Disk.PokeFile(f.Entry,
                    AkaiDisk.ProgHeaderSize + i * AkaiDisk.KeygroupSize + offset, (byte)value);
                _editDisk = f.Disk;
            }
        }

        void OnVcaEnvelopeChanged(object sender, EventArgs e)
        {
            if (_loadingEnvelopes) return;
            PokeKeygroup(VcaAttackByte, _vcaEnv.Attack);
            PokeKeygroup(VcaAttackByte + 1, _vcaEnv.Decay);
            PokeKeygroup(VcaAttackByte + 2, _vcaEnv.Sustain);
            PokeKeygroup(VcaAttackByte + 3, _vcaEnv.Release);
            AfterEnvelopeEdit("VCA  " + _vcaEnv.Attack + "/" + _vcaEnv.Decay + "/" +
                              _vcaEnv.Sustain + "/" + _vcaEnv.Release);
        }

        void OnVcfEnvelopeChanged(object sender, EventArgs e)
        {
            if (_loadingEnvelopes) return;
            PokeKeygroup(VcfAttackByte, _vcfEnv.Attack);
            PokeKeygroup(VcfAttackByte + 1, _vcfEnv.Decay);
            PokeKeygroup(VcfAttackByte + 2, _vcfEnv.Sustain);
            PokeKeygroup(VcfAttackByte + 3, _vcfEnv.Release);
            AfterEnvelopeEdit("VCF  " + _vcfEnv.Attack + "/" + _vcfEnv.Decay + "/" +
                              _vcfEnv.Sustain + "/" + _vcfEnv.Release);
        }

        void OnVcfAmountChanged(object sender, EventArgs e)
        {
            _vcfAmountLabel.Text = "amt" + Environment.NewLine + _vcfAmount.Value.ToString("+0;-0;0");
            if (_loadingEnvelopes) return;

            PokeKeygroup(VcfAmountByte, _vcfAmount.Value);
            AfterEnvelopeEdit("VCF amount  " + _vcfAmount.Value);
        }

        /// <summary>
        /// Updates the two list columns the envelopes feed, rather than rebuilding the
        /// list - a rebuild on every mouse move would fight with the drag.
        /// </summary>
        void AfterEnvelopeEdit(string what)
        {
            // One undo step per gesture: the baseline was taken when the handle was
            // grabbed, and PushUndo collapses the repeats while it is dragged.
            string gesture = what.Substring(0, Math.Min(3, what.Length));
            if (_baselineDisk == _editDisk) PushUndo(_editDisk, gesture + " envelope", _baseline, _baselineModified);

            if (_editDisk != null) _editDisk.Modified = true;

            // The list is a column of numbers now, so there is no envelope column to
            // keep in step - the envelopes themselves are what shows the change.

            UpdateCommands();
            SetStatus("Edited " + what + ".  Unsaved changes.");
        }

        void ShowKeygroupEditor()
        {
            var f = SelectedFile;
            if (f == null || f.Entry.Type != 'P') return;

            var picked = SelectedKeygroups();
            if (picked.Count == 0) return;

            int i = picked[0];
            if (i < 0 || i >= AkaiDisk.KeygroupCount(f.Entry)) return;

            _editDisk = f.Disk;

            var editor = new KeygroupEditor(f.Disk, f.Entry, i,
                                            picked.GetRange(1, picked.Count - 1));

            LoadKeygroupPane(editor, f.Disk, i, AkaiDisk.KeygroupCount(f.Entry));
            _editArea.Visible = true;

            var groups = f.Disk.Keygroups(f.Entry);
            LoadEnvelopes(i < groups.Count ? groups[i] : null);

            if (picked.Count > 1)
                SetStatus("Keygroup " + (i + 1) + " shown; edits reach all " + picked.Count +
                          " selected keygroups.");
        }

        void RefreshTreeLabels()
        {
            foreach (TreeNode disk in _tree.Nodes)
                foreach (TreeNode grp in disk.Nodes)
                    foreach (TreeNode n in grp.Nodes)
                    {
                        var fr = n.Tag as FileRef;
                        if (fr == null) continue;

                        // Re-reading the directory builds new entry objects, so the node's
                        // reference has to be refreshed or it shows the pre-edit name.
                        foreach (var e in fr.Disk.Entries)
                            if (e.Slot == fr.Entry.Slot) { fr.Entry = e; break; }

                        if (n.Text != fr.Entry.Name) n.Text = fr.Entry.Name;
                    }
        }

        void OnSaveAs(object sender, EventArgs e)
        {
            var d = _editDisk ?? (SelectedFile != null ? SelectedFile.Disk : null);
            if (d == null) { MessageBox.Show(this, "Select a disk first.", "Save"); return; }

            using (var dlg = new SaveFileDialog())
            {
                // Both containers can always be written: an HFE is patched when the disk came
                // from one and synthesised from nothing when it did not, and the raw image is
                // simply the bytes being edited. The filter starts on the container it arrived
                // in, and the chosen extension - not the filter - decides what is written.
                dlg.Title = "Save disk image as";
                dlg.Filter = "HxC floppy image (*.hfe)|*.hfe|Raw sector image (*.img)|*.img";
                dlg.FilterIndex = d.IsHfe ? 1 : 2;
                dlg.FileName = Path.GetFileNameWithoutExtension(d.Source) + "-edited" + (d.IsHfe ? ".hfe" : ".img");
                if (dlg.ShowDialog(this) != DialogResult.OK) return;

                string format = string.Equals(Path.GetExtension(dlg.FileName), ".img",
                                              StringComparison.OrdinalIgnoreCase) ? "img" : "hfe";

                if (string.Equals(Path.GetFullPath(dlg.FileName), Path.GetFullPath(d.Source),
                                  StringComparison.OrdinalIgnoreCase))
                {
                    MessageBox.Show(this, "Choose a different file - the original is never overwritten.",
                                    "Save", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                try
                {
                    d.SaveAs(dlg.FileName, format);

                    // Read it straight back and check it before telling the user it worked.
                    var check = AkaiDisk.Load(dlg.FileName);
                    if (check.BadCrcSectors > 0 || check.MissingSectors > 0)
                    {
                        MessageBox.Show(this,
                            "Written, but it does not verify: " + check.BadCrcSectors + " bad-CRC and " +
                            check.MissingSectors + " unreadable sectors. Do not use this image.",
                            "Verification failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
                        SetStatus("Save verification FAILED.");
                        return;
                    }

                    d.Modified = false;
                    UpdateCommands();
                    SetStatus("Saved and verified: " + Path.GetFileName(dlg.FileName) +
                              "  -  " + check.Entries.Count + " files, 0 bad sectors" +
                              (format == "hfe" && !d.IsHfe ? "  (built as HFE from a raw image)" : "") +
                              ".");
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, ex.Message, "Save failed",
                                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
        }

        // --------------------------------------------------------------- audio

        // ------------------------------------------------------------------ MIDI in

        /*
         * Playing the selected programme from a MIDI keyboard.
         *
         * The engine has always been able to do this - Instrument has had a MIDI port
         * and a note callback since it was written - and nothing in the window ever
         * offered it. This is that offer, and the one piece that was genuinely missing
         * with it: the selected programme has to be loaded into the engine when the
         * selection moves, because a MIDI keyboard gives no other opportunity. Clicking
         * an on-screen key used to be what loaded it.
         */

        void BuildMidiMenu()
        {
            _midiMenu.DropDownItems.Clear();

            int current = _instrumentOk ? _instrument.MidiPort : -1;

            var none = new ToolStripMenuItem("&None", null, (s, e) => ChooseMidi(-1));
            none.Checked = current < 0;
            _midiMenu.DropDownItems.Add(none);

            List<string> ports;
            try { ports = Instrument.MidiPorts(); }
            catch (Exception ex)
            {
                _midiMenu.DropDownItems.Add(new ToolStripMenuItem("(" + ex.Message + ")")
                                            { Enabled = false });
                return;
            }

            if (ports.Count == 0)
            {
                _midiMenu.DropDownItems.Add(new ToolStripMenuItem("(no MIDI inputs)")
                                            { Enabled = false });
                return;
            }

            _midiMenu.DropDownItems.Add(new ToolStripSeparator());

            for (int i = 0; i < ports.Count; i++)
            {
                int port = i;                       // captured per item, not per loop
                var item = new ToolStripMenuItem(ports[i], null, (s, e) => ChooseMidi(port));
                item.Checked = port == current;
                _midiMenu.DropDownItems.Add(item);
            }
        }

        void ChooseMidi(int port)
        {
            if (port < 0)
            {
                if (_instrumentOk) _instrument.CloseMidi();
                ShowMidiPortOnToolbar();
                SetStatus("MIDI input off.");
                return;
            }

            if (!EnsureInstrument())
            {
                SetStatus("No audio output, so there is nothing for MIDI to play. " +
                          _instrument.Error);
                return;
            }

            if (!_instrument.OpenMidi(port))
            {
                SetStatus("Could not open that MIDI input. " + _instrument.Error);
                return;
            }

            // Whatever is selected becomes what the keyboard plays, now rather than at
            // the first note.
            SyncInstrumentProgram();
            ShowMidiPortOnToolbar();

            var f = SelectedFile;
            SetStatus("MIDI in: " + _instrument.MidiPortName + "  -  playing " +
                      (f != null && f.Entry.Type == 'P' ? f.Entry.Name.Trim()
                                                         : "nothing yet, select a programme") +
                      "  -  " + _instrument.LatencyMs.ToString("0") + " ms");
        }

        /*
         * The engine finds out about edits by being watched, not by being told.
         *
         * Telling it means every edit site remembering to - the property grid, the
         * envelope handles, the loop dialog, slicing, importing, undo. There are more
         * of them than anyone will keep in their head, and the symptom when one forgets
         * is not an error but silence: the note goes on sounding exactly as it did, so
         * the edit looks like it did nothing. That has now been reported twice.
         *
         * So instead the disk counts its own revisions and this looks at the number.
         * It costs one integer comparison six times a second, it cannot be forgotten by
         * a new edit path, and it works when the notes are arriving over MIDI and
         * nothing in the window is being clicked at all - which is the case that first
         * showed it up, because only a key click or a change of selection used to load
         * the programme.
         */
        readonly System.Windows.Forms.Timer _editWatch =
            new System.Windows.Forms.Timer { Interval = 160 };

        int _seenRevision = -1;
        AkaiDisk _seenDisk;

        void OnEditWatchTick(object sender, EventArgs e)
        {
            if (!_instrumentOk) return;

            var f = SelectedFile;
            if (f == null || f.Disk == null || f.Entry.Type != 'P') return;

            if (ReferenceEquals(f.Disk, _seenDisk) && f.Disk.Revision == _seenRevision) return;

            _seenDisk = f.Disk;
            _seenRevision = f.Disk.Revision;

            // Rebuilds the patch and hands it to the engine, which passes it on to the
            // voices already sounding.
            _instrument.SetProgram(f.Disk, f.Entry);
        }

        /// <summary>
        /// Make the selected programme the one the engine plays.
        ///
        /// Cheap to call often: Instrument keeps the patch it built and hands the same
        /// one back when nothing has changed, so this does not disturb sounding voices.
        /// </summary>
        void SyncInstrumentProgram()
        {
            if (!_instrumentOk) return;

            var f = SelectedFile;
            if (f != null && f.Entry.Type == 'P') _instrument.SetProgram(f.Disk, f.Entry);
        }

        void OnPlay(object sender, EventArgs e) { PlayCurrent(true); }

        void OnStopPlay(object sender, EventArgs e)
        {
            _audio.Stop();
            SetStatus("Playback stopped.");
        }

        /// <summary>
        /// Silences playback and drops a "Playing ..." message with it, so the status
        /// bar never claims to be playing something that has been cut off.
        /// </summary>
        void StopAudio()
        {
            _audio.Stop();
            if (_statusText.Text.StartsWith("Playing ", StringComparison.Ordinal)) SetStatus("");
        }

        /// <summary>
        /// Plays whatever the selection implies - a sample directly, or the sample the
        /// selected keygroup points at. <paramref name="force"/> is set for an explicit
        /// Play, and clear when this is a side effect of moving the selection.
        /// </summary>
        void PlayCurrent(bool force)
        {
            if (!force && !_autoPlay.Checked) return;

            var f = SelectedFile;
            if (f == null) return;

            AkaiEntry sample = null;
            if (f.Entry.Type == 'S') sample = f.Entry;
            else if (f.Entry.Type == 'P') sample = KeygroupSample(f);

            if (sample == null)
            {
                if (force) SetStatus("Nothing to play - select a sample, or a keygroup whose sample is on this disk.");
                return;
            }
            PlaySample(f.Disk, sample);
        }

        /// <summary>The selected keygroup's zone 1 sample, falling back to zone 2.</summary>
        AkaiEntry KeygroupSample(FileRef f)
        {
            if (_keygroups.SelectedIndices.Count == 0) return null;
            AkaiDisk.Zone zone;
            return SampleOf(f, _keygroups.SelectedIndices[0], out zone);
        }

        /// <summary>The sample a keygroup sounds, and the zone it came from.</summary>
        AkaiEntry SampleOf(FileRef f, int index, out AkaiDisk.Zone zone)
        {
            zone = null;
            var groups = f.Disk.Keygroups(f.Entry);
            if (index < 0 || index >= groups.Count) return null;

            var kg = groups[index];
            zone = kg.Zone1;

            var s = FindSample(f.Disk, kg.Sample1);
            if (s == null && kg.HasSecondZone)
            {
                zone = kg.Zone2;
                s = FindSample(f.Disk, kg.Zone2.Name);
            }
            return s;
        }

        /// <summary>Letting go of a key stops the note it started.</summary>
        void OnPianoKeyUp(int note)
        {
            if (_instrumentOk) _instrument.NoteOff(note);
        }

        /// <summary>
        /// Clicking a key picks its keygroup and sounds the sample at that key's pitch.
        /// </summary>
        void OnPianoKey(int group, int note)
        {
            _selectingFromKey = true;
            try { SelectKeygroupRow(group); }
            finally { _selectingFromKey = false; }

            var f = SelectedFile;
            if (f == null || f.Entry.Type != 'P') return;

            var groups = f.Disk.Keygroups(f.Entry);
            if (group < 0 || group >= groups.Count) return;

            AkaiDisk.Zone zone;
            var sample = SampleOf(f, group, out zone);
            if (sample == null)
            {
                SetStatus("Keygroup " + (group + 1) + "'s sample is not on this disk.");
                return;
            }

            var kg = groups[group];
            double shift = PitchShift(sample, kg, zone, note);

            try
            {
                /*
                 * Through the engine if there is one: the whole voice, which is the
                 * filter, both envelopes and the vibrato, and which keeps sounding while
                 * the next key is struck rather than cutting the last one off.
                 *
                 * The old path is still here for a machine whose output would not open,
                 * and it is worth saying which is which - the two do not sound alike,
                 * and being unable to tell would make every judgement about the
                 * emulation worthless.
                 */
                if (EnsureInstrument())
                {
                    _instrument.SetProgram(f.Disk, f.Entry);
                    _instrument.NoteOn(note, _velocity.Value);

                    // What it is SOUNDING, not what the keygroup under the pointer says.
                    // Overlapping keygroups mean a key can sound more than one sample, and
                    // the two answers differing is worth being able to see.
                    SetStatus("Playing " + _instrument.LastPlayed() + " at " + NoteName(note) +
                              "  -  keygroup " + (group + 1) + " names " + sample.Name.Trim() +
                              ", " + shift.ToString("+0.00;-0.00;0", CultureInfo.InvariantCulture) +
                              " semitones" + (kg.ConstantPitch ? ", constant pitch" : "") +
                              "  -  " + _instrument.LatencyMs.ToString("0") + " ms");
                }
                else
                {
                    _audio.PlayShifted(Sounded(f.Disk, sample), sample.SampleRate, shift);
                    SetStatus("Playing " + sample.Name + " at " + NoteName(note) +
                              "  -  " + shift.ToString("+0.00;-0.00;0", CultureInfo.InvariantCulture) +
                              " semitones, " +
                              (sample.SampleRate * Math.Pow(2, shift / 12.0)).ToString("N0") + " Hz" +
                              "  -  plain playback: " + _instrument.Error);
                }
            }
            catch (Exception ex)
            {
                SetStatus("Could not play " + sample.Name + ": " + ex.Message);
            }
        }

        /// <summary>
        /// Semitones to shift a sample by to sound at a given key. The sample's own
        /// nominal pitch is the key at which it plays back at its recorded rate; the
        /// zone's transpose and fine tune shift that, and constant pitch detaches the
        /// result from the keyboard altogether.
        /// </summary>
        static double PitchShift(AkaiEntry sample, AkaiDisk.Keygroup kg, AkaiDisk.Zone zone, int note)
        {
            double offset = zone != null ? zone.PitchOffset : 0;
            if (kg.ConstantPitch) return offset;

            double nominal = sample.NominalPitch + sample.FinePitch / 16.0;
            return (note - nominal) + offset;
        }

        static readonly string[] NoteNames =
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        static string NoteName(int note)
        {
            return NoteNames[((note % 12) + 12) % 12] + (note / 12 - 2) + " (" + note + ")";
        }

        static AkaiEntry FindSample(AkaiDisk d, string name)
        {
            if (string.IsNullOrEmpty(name)) return null;
            string want = name.Trim();
            return d.Entries.FirstOrDefault(x => x.Type == 'S' &&
                string.Equals(x.Name.Trim(), want, StringComparison.OrdinalIgnoreCase));
        }

        /// <summary>
        /// The audio a sample sounds rather than the audio it stores: trimmed to its start
        /// and end markers, and with the loop held for a few seconds if it has one. The
        /// sampler sustains a looping sample as long as the key is down; this is the nearest
        /// a fire-and-forget SoundPlayer gets to it.
        /// </summary>
        static short[] Sounded(AkaiDisk d, AkaiEntry e)
        {
            return SamplePlayer.ApplyMarkers(d.SamplePcm(e), e.LoopStart, e.LoopEnd,
                                             e.LoopLength, e.LoopMode, e.SampleRate,
                                             SamplePlayer.SustainSeconds);
        }

        void PlaySample(AkaiDisk d, AkaiEntry e)
        {
            try
            {
                var pcm = d.SamplePcm(e);
                if (pcm.Length == 0) { SetStatus(e.Name + ": no sample data to play."); return; }

                bool loops = e.LoopMode == 'L' || e.LoopMode == 'A';

                // The same engine, with nothing shaping it - a sample on its own has no
                // keygroup, so there is no filter or envelope to apply. Going through it
                // anyway means one playback path to keep working rather than two.
                if (EnsureInstrument())
                {
                    _instrument.PlaySample(d, e, e.NominalPitch);
                    SetStatus("Playing " + e.Name + "  -  " +
                              e.Seconds.ToString("0.00", CultureInfo.InvariantCulture) + " s at " +
                              e.SampleRate.ToString("N0") + " Hz" +
                              (loops ? "  (looping" + (e.LoopMode == 'A' ? ", alternating" : "") + ")" : ""));
                    return;
                }

                _audio.Play(Sounded(d, e), e.SampleRate);
                SetStatus("Playing " + e.Name + "  -  " +
                          e.Seconds.ToString("0.00", CultureInfo.InvariantCulture) + " s at " +
                          e.SampleRate.ToString("N0") + " Hz" +
                          (loops ? "  (looping" + (e.LoopMode == 'A' ? ", alternating" : "") + ")" : ""));
            }
            catch (Exception ex)
            {
                SetStatus("Could not play " + e.Name + ": " + ex.Message);
            }
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            RememberSession();
            _audio.Dispose();
            base.OnFormClosed(e);
        }

        // ------------------------------------------------------------------ undo

        /// <summary>A disk's bytes as they were before one edit.</summary>
        sealed class UndoStep
        {
            public AkaiDisk Disk;
            public byte[] Image;
            public bool WasModified;
            public string What;
        }

        const int UndoDepth = 24;                  // 800 KB a step, so this is ~19 MB

        readonly List<UndoStep> _undo = new List<UndoStep>();
        readonly List<UndoStep> _redo = new List<UndoStep>();

        /// <summary>
        /// The edited disk's bytes as of the last point an edit could have begun. The
        /// property grid and the envelope handles only tell us a value changed after the
        /// fact, so the "before" has to be captured when the control is first touched.
        /// </summary>
        byte[] _baseline;
        AkaiDisk _baselineDisk;
        bool _baselineModified;

        void CaptureBaseline()
        {
            _baselineDisk = _editDisk;
            _baseline = _editDisk != null ? (byte[])_editDisk.Image.Clone() : null;
            _baselineModified = _editDisk != null && _editDisk.Modified;
        }

        /// <summary>
        /// Records a disk's state before an edit. Repeated edits of the same thing -
        /// dragging an envelope corner, nudging a slider - collapse into one step.
        /// </summary>
        void PushUndo(AkaiDisk d, string what, byte[] before, bool wasModified)
        {
            if (d == null || before == null || before.Length != d.Image.Length) return;

            if (_undo.Count > 0)
            {
                var top = _undo[_undo.Count - 1];
                if (top.Disk == d && top.What == what) return;      // same gesture
            }

            _undo.Add(new UndoStep { Disk = d, Image = before, WasModified = wasModified, What = what });
            if (_undo.Count > UndoDepth) _undo.RemoveAt(0);
            _redo.Clear();
        }

        /// <summary>Records the state before an edit we are about to make ourselves.</summary>
        void PushUndo(AkaiDisk d, string what)
        {
            if (d != null) PushUndo(d, what, (byte[])d.Image.Clone(), d.Modified);
        }

        void OnUndo(object sender, EventArgs e) { Step(_undo, _redo, "Undid"); }
        void OnRedo(object sender, EventArgs e) { Step(_redo, _undo, "Redid"); }

        void Step(List<UndoStep> from, List<UndoStep> to, string verb)
        {
            if (from.Count == 0) return;

            var step = from[from.Count - 1];
            from.RemoveAt(from.Count - 1);

            to.Add(new UndoStep
            {
                Disk = step.Disk,
                Image = (byte[])step.Disk.Image.Clone(),
                WasModified = step.Disk.Modified,
                What = step.What
            });

            Array.Copy(step.Image, step.Disk.Image, step.Image.Length);
            step.Disk.ParseDirectory();
            step.Disk.Modified = step.WasModified;

            _audio.Stop();
            _editDisk = step.Disk;
            RebuildTree();
            CaptureBaseline();
            UpdateCommands();
            SetStatus(verb + " " + step.What + ".");
        }

        void SetStatus(string text) { _statusText.Text = text; }
    }
}





