using BlobsetIO;
using PackageIO;
using System.ComponentModel;
using System.Diagnostics;
using System.Reflection;
using static Blobset_Tools.Enums;

namespace Blobset_Tools
{
    public partial class MainForm : Form
    {
        private BackgroundWorker? Extract_bgw = null;
        private BackgroundWorker? Modify_bgw = null;
        private BackgroundWorker? Create_bgw = null;
        private BackgroundWorker? FileMapping_bgw = null;
        private System.Media.SoundPlayer? player = null;
        private bool isFlipped = false;
        private readonly byte[]? oggData = null;
        private readonly byte[]? wavData = null;
        public MainForm()
        {
            InitializeComponent();

            Global.version = Assembly.GetEntryAssembly().GetName().Version.ToString();
            Text = "Blobset Tools - v" + Global.version;
            string gameName = "* Reading and Writing " + Global.gameInfo.GameName + " Blobset file *";
            gameName_toolStripTextBox.Text = gameName;

            dds_pictureBox.SizeMode = PictureBoxSizeMode.Zoom;
            dds_pictureBox.Image = Properties.Resources.Blobset_Tools;
            UI.BlobsetHeaderData();
        }

        private void MainForm_Load(object sender, EventArgs e)
        {
            string gameVersion = Utilities.GetGameVersion();
            BlobsetVersion blobsetVersion = (BlobsetVersion)Global.gameInfo.BlobsetVersion;
            string fileMappingVersion = Global.gameInfo.Version;

            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** " + "Blobset Tools - v" + Global.version + " ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText("Description: Extract and Modify BigAnt games blobset files." + Environment.NewLine + Environment.NewLine);

            if (gameVersion == fileMappingVersion)
                fileInfo_richTextBox.SelectionColor = Color.Green;
            else
                fileInfo_richTextBox.SelectionColor = Color.Red;

            fileInfo_richTextBox.AppendText("Game Version: " + gameVersion + Environment.NewLine);

            if (gameVersion == fileMappingVersion)
                fileInfo_richTextBox.SelectionColor = Color.Green;
            else
                fileInfo_richTextBox.SelectionColor = Color.Red;

            fileInfo_richTextBox.AppendText("File Mapping Version: " + fileMappingVersion + Environment.NewLine);

            if (gameVersion == fileMappingVersion)
                fileInfo_richTextBox.SelectionColor = Color.Green;
            else
                fileInfo_richTextBox.SelectionColor = Color.Red;

            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            string platform = platformDetails["Platform"];
            fileInfo_richTextBox.AppendText("Platform: " + platform + Environment.NewLine + Environment.NewLine);

            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
            fileInfo_richTextBox.AppendText("Author: Wouldubeinta" + Environment.NewLine);
            fileInfo_richTextBox.AppendText("Discord ID: Wouldubeinta" + Environment.NewLine + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.White;
            fileInfo_richTextBox.AppendText("*** Special Thanks To ***" + Environment.NewLine);
            fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;

            foreach (string line in UI.LoadingText)
                fileInfo_richTextBox.AppendText(line);

            if (gameVersion == fileMappingVersion)
            {
                folder_treeView.Nodes.Clear();
                UI.FilesList(folder_treeView);
            }
            else
            {
                DialogResult result = MessageBox.Show("You need to update the file mapping data for " + Global.gameInfo.GameName + ", click Yes to continue", "Update File Mapping Data", MessageBoxButtons.YesNo, MessageBoxIcon.Question);

                switch (result)
                {
                    case DialogResult.Yes:
                        fileInfo_richTextBox.Clear();
                        fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                        fileInfo_richTextBox.AppendText("Mapping files for " + Global.gameInfo.GameName + ", please wait..........");
                        FileMappingMain();
                        break;
                }
            }

            if (Global.blobsetHeaderData == null)
            {
                MessageBox.Show("There was a problem reading the blobset file, restart Blobset Tools.", "Blobset Reading Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                folder_treeView.Nodes.Clear();
                return;
            }

            IniFile settingsIni = new(Path.Combine(Global.currentPath, "Settings.ini"));

            bool loadGameCheck = "true" == settingsIni.Read("LoadGame", "Settings") ? true : false;
            bool skipUnknownFilesCheck = "true" == settingsIni.Read("SkipUnknown", "Settings") ? true : false;

            loadGameToolStripMenuItem.Checked = loadGameCheck;
            skipUnknownFilesToolStripMenuItem.Checked = skipUnknownFilesCheck;

            if (blobsetVersion < BlobsetVersion.v3)
            {
                loadGameToolStripMenuItem.Visible = false;
                validateSteamGameFilesToolStripMenuItem.Visible = false;
                restoreBackupFilesToolStripMenuItem.Visible = false;
                resetBlobsetToolStripMenuItem.Visible = true;
            }
            else
            {
                openToolStripMenuItem.Visible = false;
                createToolStripMenuItem.Visible = false;
                resetBlobsetToolStripMenuItem.Visible = false;
            }
        }

        private void openToolStripMenuItem_Click(object sender, EventArgs e)
        {
            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            string platformExt = platformDetails["PlatformExt"];

            string iniPath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "GameInfo.ini");
            IniFile gameInfo = new(Path.Combine(iniPath, "Settings.ini"));

            blobset_ofd.Filter = "Blobset File | *." + platformExt;
            blobset_ofd.DefaultExt = platformExt;

            if (blobset_ofd.ShowDialog() == DialogResult.OK)
            {
                Global.gameInfo.GameLocation = blobset_ofd.FileName;
                IniFile gameInfoIni = new(iniPath);
                gameInfoIni.Write("GameLocation", blobset_ofd.FileName, "GameInfo");

                DialogResult result = MessageBox.Show("Do you want to update the file mapping data for " + Global.gameInfo.GameName + ", click Yes to continue", "Update File Mapping Data", MessageBoxButtons.YesNo, MessageBoxIcon.Question);

                switch (result)
                {
                    case DialogResult.Yes:
                        fileInfo_richTextBox.Clear();
                        fileInfo_richTextBox.SelectionColor = Color.DodgerBlue;
                        fileInfo_richTextBox.AppendText("Mapping files for " + Global.gameInfo.GameName + ", please wait..........");
                        FileMappingMain();
                        break;
                    default:
                        folder_treeView.Nodes.Clear();
                        UI.FilesList(folder_treeView);
                        break;
                }

                UI.BlobsetHeaderData();
            }
        }

        private void folder_treeView_AfterSelect(object sender, TreeViewEventArgs e)
        {
            status_Label.ForeColor = Color.Black;

            files_listView.Refresh();
            files_listView.Clear();
            status_Label.Text = string.Empty;

            if (folder_treeView.SelectedNode == null)
                return;

            ImageList myImageList = new();
            myImageList.Images.Add(Properties.Resources.file_32);
            myImageList.Images.Add(Properties.Resources.dds_32);
            myImageList.Images.Add(Properties.Resources.txpk_32);
            myImageList.Images.Add(Properties.Resources.m3mp_32);
            myImageList.Images.Add(Properties.Resources.wem_32);
            myImageList.Images.Add(Properties.Resources.bnk_32);
            myImageList.Images.Add(Properties.Resources.bank_32);
            myImageList.Images.Add(Properties.Resources.fsb_32);
            myImageList.Images.Add(Properties.Resources.fev1_32);
            myImageList.Images.Add(Properties.Resources.wav_32);
            myImageList.Images.Add(Properties.Resources.bmf_32);
            myImageList.Images.Add(Properties.Resources.bsb_32);
            myImageList.Images.Add(Properties.Resources.dat_32);

            files_listView.SmallImageList = myImageList;

            Global.filelist = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            if (Global.filelist == null)
                return;

            ListViewItem[] lvi = new ListViewItem[Global.filelist.Count];

            for (int i = 0; i < Global.filelist.Count; i++)
            {
                int icon = 0;
                string ext = Path.GetExtension(Global.filelist[i].FileName);

                switch (ext)
                {
                    case ".dds":
                        icon = 1;
                        break;
                    case ".txpk":
                        icon = 2;
                        break;
                    case ".m3mp":
                        icon = 3;
                        break;
                    case ".wem":
                        icon = 4;
                        break;
                    case ".bnk":
                        icon = 5;
                        break;
                    case ".bank":
                        icon = 6;
                        break;
                    case ".fsb":
                        icon = 7;
                        break;
                    case ".fev1":
                        icon = 8;
                        break;
                    case ".wav":
                        icon = 9;
                        break;
                    case ".bmf":
                        icon = 10;
                        break;
                    case ".bsb":
                        icon = 11;
                        break;
                    case ".dat":
                        icon = 12;
                        break;
                    default:
                        icon = 0;
                        break;
                }

                lvi[i] = new ListViewItem { ImageIndex = icon, Text = Global.filelist[i].FileName };
            }

            AddItems(lvi);
            status_Label.Text = Global.filelist.Count + " items in " + folder_treeView.SelectedNode.Text + " folder";
        }

        private void AddItems(ListViewItem[] lvi)
        {
            if (InvokeRequired)
            {
                Invoke((System.Windows.Forms.MethodInvoker)delegate { AddItems(lvi); });
                return;
            }
            files_listView.Items.AddRange(lvi);
        }

        private void files_listView_MouseClick(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
            {
                if (Global.filelist == null)
                    return;

                if (Global.fileIndex == -1)
                    return;

                string ext = Path.GetExtension(Global.filelist[Global.fileIndex].FilePath);

                if (ext == ".dds")
                    extractImage_contextMenuStrip.Show(Cursor.Position);
                else if (ext == ".wem")
                    extractSoundFile_contextMenuStrip.Show(Cursor.Position);
                else if (ext == ".dat")
                    extractDatFilecontextMenuStrip.Show(Cursor.Position);
            }
        }

        private void files_listView_SelectedIndexChanged(object sender, EventArgs e)
        {
            try
            {
                dds_pictureBox.Image = Properties.Resources.Blobset_Tools;

                Global.fileIndex = UI.getLVSelectedIndex(files_listView);
                fileInfo_richTextBox.Clear();

                if (Global.filelist == null)
                    return;

                if (Global.fileIndex == -1)
                    return;

                string filePath = Global.gameInfo.GameLocation.Replace("data-0.blobset.pc", string.Empty) + Global.filelist[Global.fileIndex].FolderHash + @"\" + Global.filelist[Global.fileIndex].FileHash;
                int blobsetVersion = Global.gameInfo.BlobsetVersion;

                if (Global.gameInfo.BlobsetVersion != (int)BlobsetVersion.v4)
                    filePath = Global.gameInfo.GameLocation;

                if (!File.Exists(filePath))
                {
                    MessageBox.Show("Can't find the file - " + Global.filelist[Global.fileIndex].FilePath + ". You might need to run the File Mapping Data in the Options.", "File Not Found !!!", MessageBoxButtons.OK, MessageBoxIcon.Exclamation);
                    return;
                }

                string type = Path.GetExtension(Global.filelist[Global.fileIndex].FilePath);

                switch (type)
                {
                    case ".dds":
                        UiFileTypes.DDS(fileInfo_richTextBox, filePath, dds_pictureBox, blobsetVersion, alphaToolStripMenuItem.Checked, isFlipped);
                        break;
                    case ".txpk":
                        UiFileTypes.TXPK(fileInfo_richTextBox, filePath, dds_pictureBox, blobsetVersion);
                        break;
                    case ".m3mp":
                        UiFileTypes.M3MP(fileInfo_richTextBox, filePath, dds_pictureBox, blobsetVersion);
                        break;
                    case ".wem":
                        UiFileTypes.Wise_WEM(fileInfo_richTextBox, filePath, dds_pictureBox, blobsetVersion, player, oggData, wavData);
                        break;
                    case ".bnk":
                        UiFileTypes.Wise_BNK(fileInfo_richTextBox, filePath, dds_pictureBox, blobsetVersion);
                        break;
                    case ".bank":
                        break;
                    case ".fsb":
                        break;
                    case ".fev1":
                        break;
                    case ".wav":
                        UiFileTypes.WAV(fileInfo_richTextBox, filePath, dds_pictureBox, blobsetVersion, player, wavData);
                        break;
                    case ".bsb":
                        break;
                    case ".bmf":
                        break;
                    case ".dat":
                        UiFileTypes.DAT(fileInfo_richTextBox, filePath, dds_pictureBox, blobsetVersion);
                        break;
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
        }

        private void files_listView_MouseDoubleClick(object sender, MouseEventArgs e)
        {
            if (Global.fileIndex != -1)
            {
                if (Global.filelist == null)
                    return;

                string filePath = Global.gameInfo.GameLocation.Replace("data-0.blobset.pc", string.Empty) + Global.filelist[Global.fileIndex].FolderHash + @"\" + Global.filelist[Global.fileIndex].FileHash;
                string ext = Path.GetExtension(Global.filelist[Global.fileIndex].FilePath);
                int blobsetVersion = Global.gameInfo.BlobsetVersion;

                if (Global.gameInfo.BlobsetVersion != (int)BlobsetVersion.v4)
                    filePath = Global.gameInfo.GameLocation;

                if (!File.Exists(filePath))
                {
                    MessageBox.Show("Can't find the file - " + Global.filelist[Global.fileIndex].FilePath + ". You might need to run the File Mapping Data in the Options.", "File Not Found !!!", MessageBoxButtons.OK, MessageBoxIcon.Exclamation);
                    return;
                }

                var blobsetHeaderData = Global.blobsetHeaderData.Entries[Global.filelist[Global.fileIndex].BlobsetIndex];

                switch (ext)
                {
                    case ".dds":
                        DDS_Viewer dds_form = new(Global.filelist[Global.fileIndex].FilePath, Global.filelist);
                        bool IsDDSFormOpen = false;

                        foreach (Form f in Application.OpenForms)
                        {
                            if (f.Text == "DDS Viewer - " + Global.filelist[Global.fileIndex].FilePath)
                            {
                                IsDDSFormOpen = true;
                                f.Focus();
                                break;
                            }
                        }

                        if (!IsDDSFormOpen)
                            dds_form.Show();
                        break;
                    case ".txpk":
                        TXPK txpk = blobsetVersion > 2 ? ZSTD_IO.ReadTXPKInfo(filePath) : LZMA_IO.ReadTXPKInfo(Global.filelist);

                        TXPK_Viewer txpk_form = new(Global.filelist[Global.fileIndex].FilePath, txpk, Global.filelist);
                        bool IsTxpkFormOpen = false;

                        foreach (Form f in Application.OpenForms)
                        {
                            if (f.Text == "TXPK Viewer - " + Global.filelist[Global.fileIndex].FilePath)
                            {
                                IsTxpkFormOpen = true;
                                f.Focus();
                                break;
                            }
                        }

                        if (!IsTxpkFormOpen)
                            txpk_form.Show();
                        break;
                    case ".m3mp":
                        bool isCompressed = false;

                        if (blobsetHeaderData.MainCompressedSize != blobsetHeaderData.MainUnCompressedSize)
                            isCompressed = true;

                        M3MP? m3mp = blobsetVersion > 2 ? ZSTD_IO.ReadM3MPInfo(filePath, isCompressed) : LZMA_IO.ReadM3MPInfo(Global.filelist);

                        M3MP_Viewer m3mp_form = new(Global.filelist[Global.fileIndex].FilePath, m3mp, Global.filelist);
                        bool IsM3mpFormOpen = false;

                        foreach (Form f in Application.OpenForms)
                        {
                            if (f.Text == "M3MP Viewer - " + Global.filelist[Global.fileIndex].FilePath)
                            {
                                IsM3mpFormOpen = true;
                                f.Focus();
                                break;
                            }
                        }

                        if (!IsM3mpFormOpen)
                            m3mp_form.Show();
                        break;
                    case ".dat":
                        Hex_Viewer dat_form = new(filePath, Global.filelist[Global.fileIndex].FilePath, (int)blobsetHeaderData.MainUnCompressedSize, blobsetHeaderData.MainFinalOffSet);
                        bool IsDatFormOpen = false;

                        foreach (Form f in Application.OpenForms)
                        {
                            if (f.Text == "Hex Viewer - " + Global.filelist[Global.fileIndex].FilePath)
                            {
                                IsDatFormOpen = true;
                                f.Focus();
                                break;
                            }
                        }

                        if (!IsDatFormOpen)
                            dat_form.Show();
                        break;
                }
            }
        }

        private void extractBlobset_ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            fileInfo_richTextBox.Clear();
            fileInfo_richTextBox.AppendText("Extracting " + Global.gameInfo.GameName + " Blobset, please wait..........");
            fileInfo_richTextBox.AppendText(Environment.NewLine);
            fileInfo_richTextBox.AppendText("This may take a while, just depends on the size of the game.");
            ExtractMain();
        }

        private void ExtractMain()
        {
            Extract_bgw = new BackgroundWorker();
            Extract_bgw.DoWork += new DoWorkEventHandler(Extract_bgw_DoWork);
            Extract_bgw.ProgressChanged += new ProgressChangedEventHandler(Extract_bgw_ProgressChanged);
            Extract_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(Extract_bgw_RunWorkerCompleted);
            Extract_bgw.WorkerReportsProgress = true;
            Extract_bgw.WorkerSupportsCancellation = true;
            Extract_bgw.RunWorkerAsync();

            blobsetToolStripMenuItem.Enabled = false;
            validateSteamGameFilesToolStripMenuItem.Enabled = false;
            updateFileMappingDataToolStripMenuItem.Enabled = false;
            files_listView.Enabled = false;
            status_Label.ForeColor = Color.Black;
        }

        private void Extract_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            string blobsetFile = Global.gameInfo.GameLocation;
            BlobsetVersion blobsetVersion = (BlobsetVersion)Global.gameInfo.BlobsetVersion;

            bool errorCheck = true;

            switch (blobsetVersion)
            {
                case BlobsetVersion.v1:
                    errorCheck = Extract.BlobsetV1(blobsetFile, Extract_bgw);
                    break;
                case BlobsetVersion.v2:
                    errorCheck = Extract.BlobsetV2(blobsetFile, Extract_bgw);
                    break;
                case BlobsetVersion.v3:
                    errorCheck = Extract.BlobsetV3(blobsetFile, Extract_bgw);
                    break;
                case BlobsetVersion.v4:
                    errorCheck = Extract.BlobsetV4(blobsetFile, Extract_bgw);
                    break;
            }

            if (errorCheck)
                e.Cancel = true;
        }

        private void Extract_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            int progressPercentage = Math.Max(0, Math.Min(100, e.ProgressPercentage));
            progressStripStatusLabel.Text = $"{progressPercentage} %";
            string fileName = e.UserState?.ToString() ?? "Unknown file";
            status_Label.Text = $"Extracting File: {fileName}";
            toolStripProgressBar.Value = progressPercentage;
        }

        private void Extract_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                fileInfo_richTextBox.Clear();
                status_Label.Text = Global.gameInfo.GameName + " Blobset file has finished extracting....";
                fileInfo_richTextBox.AppendText("Blobset file has finished extracting....");
                MessageBox.Show(Global.gameInfo.GameName + " Blobset file has finished extracting", "Blobset Extraction", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            else
                fileInfo_richTextBox.Clear();

            toolStripProgressBar.Value = 0;
            progressStripStatusLabel.Text = string.Empty;

            blobsetToolStripMenuItem.Enabled = true;
            validateSteamGameFilesToolStripMenuItem.Enabled = true;
            updateFileMappingDataToolStripMenuItem.Enabled = true;
            files_listView.Enabled = true;

            if (Extract_bgw != null) { Extract_bgw.Dispose(); Extract_bgw = null; }
        }

        private void updateFileMappingDataToolStripMenuItem_Click(object sender, EventArgs e)
        {
            fileInfo_richTextBox.Clear();
            fileInfo_richTextBox.AppendText("Mapping files, please wait..........");
            FileMappingMain();
        }

        private void FileMappingMain()
        {
            FileMapping_bgw = new BackgroundWorker();
            FileMapping_bgw.DoWork += new DoWorkEventHandler(FileMapping_bgw_DoWork);
            FileMapping_bgw.ProgressChanged += new ProgressChangedEventHandler(FileMapping_bgw_ProgressChanged);
            FileMapping_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(FileMapping_bgw_RunWorkerCompleted);
            FileMapping_bgw.WorkerReportsProgress = true;
            FileMapping_bgw.WorkerSupportsCancellation = true;
            FileMapping_bgw.RunWorkerAsync();

            blobsetToolStripMenuItem.Enabled = false;
            validateSteamGameFilesToolStripMenuItem.Enabled = false;
            updateFileMappingDataToolStripMenuItem.Enabled = false;
            files_listView.Enabled = false;
            status_Label.ForeColor = Color.Black;
        }

        private void FileMapping_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            string blobsetFile = Global.gameInfo.GameLocation;
            BlobsetVersion blobsetVersion = (BlobsetVersion)Global.gameInfo.BlobsetVersion;
            bool errorCheck = true;

            switch (blobsetVersion)
            {
                case BlobsetVersion.v1:
                    errorCheck = FileMapping.WriteV1(blobsetFile, FileMapping_bgw);
                    break;
                case BlobsetVersion.v2:
                    errorCheck = FileMapping.WriteV2(blobsetFile, FileMapping_bgw);
                    break;
                case BlobsetVersion.v3:
                    errorCheck = FileMapping.WriteV3(blobsetFile, FileMapping_bgw);
                    break;
                case BlobsetVersion.v4:
                    errorCheck = FileMapping.WriteV4(blobsetFile, FileMapping_bgw);
                    break;
            }

            if (errorCheck)
                e.Cancel = true;
        }

        private void FileMapping_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            int progressPercentage = Math.Max(0, Math.Min(100, e.ProgressPercentage));
            progressStripStatusLabel.Text = $"{progressPercentage} %";
            string fileName = e.UserState?.ToString() ?? "Unknown file";
            status_Label.Text = $"Creating New File Mapping Data: {fileName}";
            toolStripProgressBar.Value = progressPercentage;
        }

        private void FileMapping_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];

                status_Label.ForeColor = Color.DarkGreen;
                fileInfo_richTextBox.Clear();
                status_Label.Text = "Creating New File Mapping Data for " + Global.gameInfo.GameName + " has finished....";
                fileInfo_richTextBox.AppendText("Creating New File Mapping Data for " + Global.gameInfo.GameName + " has finished....");

                string gameVersion = Utilities.GetGameVersion();
                int gameID = Global.gameInfo.GameId;

                if (gameID < 7)
                {
                    Global.gameInfo.Version = "1.00";
                    IniFile iniFile = new(Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "GameInfo.ini"));
                    iniFile.Write("Version", "1.00", "GameInfo");
                }
                else
                {
                    Global.gameInfo.Version = gameVersion;
                    IniFile iniFile = new(Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "GameInfo.ini"));
                    iniFile.Write("Version", gameVersion, "GameInfo");
                }

                MessageBox.Show("Creating New File Mapping Data has finished", "Creating New File Mapping", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            else
                fileInfo_richTextBox.Clear();

            toolStripProgressBar.Value = 0;
            progressStripStatusLabel.Text = string.Empty;

            blobsetToolStripMenuItem.Enabled = true;
            validateSteamGameFilesToolStripMenuItem.Enabled = true;
            updateFileMappingDataToolStripMenuItem.Enabled = true;
            files_listView.Enabled = true;

            if (FileMapping_bgw != null) { FileMapping_bgw.Dispose(); FileMapping_bgw = null; }

            folder_treeView.Nodes.Clear();
            UI.FilesList(folder_treeView);
        }

        private void createBlobset_ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            fileInfo_richTextBox.Clear();
            fileInfo_richTextBox.AppendText("Creating new update blobset with mods, please wait..........");

            if (blobset_sfd.ShowDialog() == DialogResult.OK)
            {
                CreateMain();
            }
            blobset_sfd.Dispose();
        }

        private void CreateMain()
        {
            Create_bgw = new BackgroundWorker();
            Create_bgw.DoWork += new DoWorkEventHandler(Create_bgw_DoWork);
            Create_bgw.ProgressChanged += new ProgressChangedEventHandler(Create_bgw_ProgressChanged);
            Create_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(Create_bgw_RunWorkerCompleted);
            Create_bgw.WorkerReportsProgress = true;
            Create_bgw.WorkerSupportsCancellation = true;
            Create_bgw.RunWorkerAsync();

            blobsetToolStripMenuItem.Enabled = false;
            validateSteamGameFilesToolStripMenuItem.Enabled = false;
            updateFileMappingDataToolStripMenuItem.Enabled = false;
            files_listView.Enabled = false;
            status_Label.ForeColor = Color.Black;
        }

        private void Create_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            string blobsetFile = blobset_sfd.FileName;

            if (string.IsNullOrEmpty(blobsetFile))
            {
                e.Cancel = true;
                return;
            }

            BlobsetVersion blobsetVersion = (BlobsetVersion)Global.gameInfo.BlobsetVersion;

            bool errorCheck = true;

            switch (blobsetVersion)
            {
                case BlobsetVersion.v1:
                    errorCheck = Create.BlobsetV1(blobsetFile, Create_bgw);
                    break;
            }

            if (errorCheck)
                e.Cancel = true;
        }

        private void Create_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            int progressPercentage = Math.Max(0, Math.Min(100, e.ProgressPercentage));
            progressStripStatusLabel.Text = $"{progressPercentage} %";
            string fileName = e.UserState?.ToString() ?? "Unknown file";
            status_Label.Text = $"Creating Update Blobset: {fileName}";
            toolStripProgressBar.Value = progressPercentage;
        }

        private void Create_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                fileInfo_richTextBox.Clear();
                status_Label.Text = "Mods have been added to the " + Global.gameInfo.GameName + " blobset....";
                fileInfo_richTextBox.AppendText("Mods have been added to the " + Global.gameInfo.GameName + " blobset....");
                MessageBox.Show("Mods have been added to the blobset....", "Blobset Create", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            else
                fileInfo_richTextBox.Clear();

            toolStripProgressBar.Value = 0;
            progressStripStatusLabel.Text = string.Empty;

            blobsetToolStripMenuItem.Enabled = true;
            validateSteamGameFilesToolStripMenuItem.Enabled = true;
            updateFileMappingDataToolStripMenuItem.Enabled = true;
            files_listView.Enabled = true;

            if (Create_bgw != null) { Create_bgw.Dispose(); Create_bgw = null; }
        }

        private void modifyBlobset_ToolStripMenuItem_Click(object sender, EventArgs e)
        {
            fileInfo_richTextBox.Clear();
            fileInfo_richTextBox.AppendText("Modifing Blobset with mods, please wait..........");
            ModifyMain();
        }

        private void ModifyMain()
        {
            Modify_bgw = new BackgroundWorker();
            Modify_bgw.DoWork += new DoWorkEventHandler(Modify_bgw_DoWork);
            Modify_bgw.ProgressChanged += new ProgressChangedEventHandler(Modify_bgw_ProgressChanged);
            Modify_bgw.RunWorkerCompleted += new RunWorkerCompletedEventHandler(Modify_bgw_RunWorkerCompleted);
            Modify_bgw.WorkerReportsProgress = true;
            Modify_bgw.WorkerSupportsCancellation = true;
            Modify_bgw.RunWorkerAsync();

            blobsetToolStripMenuItem.Enabled = false;
            validateSteamGameFilesToolStripMenuItem.Enabled = false;
            updateFileMappingDataToolStripMenuItem.Enabled = false;
            files_listView.Enabled = false;
            status_Label.ForeColor = Color.Black;
        }

        private void Modify_bgw_DoWork(object sender, DoWorkEventArgs e)
        {
            string blobsetFile = Global.gameInfo.GameLocation;
            BlobsetVersion blobsetVersion = (BlobsetVersion)Global.gameInfo.BlobsetVersion;

            bool errorCheck = true;

            switch (blobsetVersion)
            {
                case BlobsetVersion.v1:
                    errorCheck = Modify.BlobsetV1(blobsetFile, Modify_bgw);
                    break;
                case BlobsetVersion.v2:
                    errorCheck = Modify.BlobsetV2(blobsetFile, Modify_bgw);
                    break;
                case BlobsetVersion.v3:
                    errorCheck = Modify.BlobsetV3(blobsetFile, Modify_bgw);
                    break;
                case BlobsetVersion.v4:
                    errorCheck = Modify.BlobsetV4(blobsetFile, Modify_bgw);
                    break;
            }

            if (errorCheck)
                e.Cancel = true;
        }

        private void Modify_bgw_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            int progressPercentage = Math.Max(0, Math.Min(100, e.ProgressPercentage));
            progressStripStatusLabel.Text = $"{progressPercentage} %";
            string fileName = e.UserState?.ToString() ?? "Unknown file";
            status_Label.Text = $"Modifing Blobset: {fileName}";
            toolStripProgressBar.Value = progressPercentage;
        }

        private void Modify_bgw_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if (!e.Cancelled)
            {
                status_Label.ForeColor = Color.DarkGreen;
                fileInfo_richTextBox.Clear();
                status_Label.Text = "Mods have been added to the " + Global.gameInfo.GameName + " blobset....";
                fileInfo_richTextBox.AppendText("Mods have been added to the " + Global.gameInfo.GameName + " blobset....");

                bool loadGameCheck = loadGameToolStripMenuItem.Checked;

                if (loadGameCheck)
                {
                    Process? ps = null;
                    string steamLocation = UI.getSteamLocation();

                    if (!string.IsNullOrEmpty(steamLocation))
                    {
                        try
                        {
                            ps = new Process();
                            ps.StartInfo.FileName = steamLocation + @"\Steam.exe";
                            ps.StartInfo.Arguments = "-applaunch " + Global.gameInfo.SteamGameId + " -StraightIntoFreemode";
                            ps.Start();
                        }
                        catch (Exception arg)
                        {
                            MessageBox.Show("Error occurred, report it to Wouldy : " + arg, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                        }
                        finally
                        {
                            if (ps != null)
                                ps.Close();
                            Application.Exit();
                        }
                    }
                }

                MessageBox.Show("Mods have been added to the blobset....", "Blobset Modify", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            else
                fileInfo_richTextBox.Clear();

            toolStripProgressBar.Value = 0;
            progressStripStatusLabel.Text = string.Empty;

            blobsetToolStripMenuItem.Enabled = true;
            validateSteamGameFilesToolStripMenuItem.Enabled = true;
            updateFileMappingDataToolStripMenuItem.Enabled = true;
            files_listView.Enabled = true;

            if (Modify_bgw != null) { Modify_bgw.Dispose(); Modify_bgw = null; }
        }

        private void loadGameToolStripMenuItem_Click(object sender, EventArgs e)
        {
            string settingsFile = Path.Combine(Global.currentPath, "Settings.ini");

            if (File.Exists(settingsFile))
            {
                IniFile settingsIni = new(settingsFile);

                if (loadGameToolStripMenuItem.Checked)
                    settingsIni.Write("LoadGame", "true", "Settings");
                else
                    settingsIni.Write("LoadGame", "false", "Settings");
            }
            else
                MessageBox.Show("Can't find - " + settingsFile + " file.", "Settings Ini File Missing", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }

        private void validateSteamGameFilesToolStripMenuItem_Click(object sender, EventArgs e)
        {
            status_Label.Text = "Validating " + Global.gameInfo.GameName + " files";
            UI.ValidateSteamGame();
        }

        private void skipUnknownFilesToolStripMenuItem_Click(object sender, EventArgs e)
        {
            string settingsFile = Path.Combine(Global.currentPath, "Settings.ini");

            if (File.Exists(settingsFile))
            {
                IniFile settingsIni = new(settingsFile);

                if (skipUnknownFilesToolStripMenuItem.Checked)
                    settingsIni.Write("SkipUnknown", "true", "Settings");
                else
                    settingsIni.Write("SkipUnknown", "false", "Settings");
            }
            else
                MessageBox.Show("Can't find - " + settingsFile + " file.", "Settings Ini File Missing", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }

        private void txpkCreatorToolStripMenuItem_Click(object sender, EventArgs e)
        {
            TXPK_Creator form = new();
            bool IsOpen = false;

            foreach (Form f in Application.OpenForms)
            {
                if (f.Text == "TXPK Creator")
                {
                    IsOpen = true;
                    f.Focus();
                    break;
                }
            }

            if (!IsOpen)
                form.Show();
        }

        private void m3mpCreatorToolStripMenuItem_Click(object sender, EventArgs e)
        {
            M3MP_Creator form = new();
            bool IsOpen = false;

            foreach (Form f in Application.OpenForms)
            {
                if (f.Text == "M3MP Creator")
                {
                    IsOpen = true;
                    f.Focus();
                    break;
                }
            }

            if (!IsOpen)
                form.Show();
        }

        private void fileMappingEditorToolStripMenuItem_Click(object sender, EventArgs e)
        {

        }

        private void ddsFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;
            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save DDS File";
            saveFileDialog.Filter = "DDS" + " File|*.dds";
            saveFileDialog.DefaultExt = "dds";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                int blobsetVersion = Global.gameInfo.BlobsetVersion;
                byte[] ddsData = blobsetVersion >= 2 ? UI.GetDDSData_V3_V4(Global.filelist) : UI.GetDDSData_V1_V2(Global.filelist);

                File.WriteAllBytes(saveFileDialog.FileName, ddsData);

                fileInfo_richTextBox.Clear();
                fileInfo_richTextBox.AppendText("DDS File has been saved to - " + saveFileDialog.FileName);
                MessageBox.Show("DDS File has been saved to - " + saveFileDialog.FileName, "Save DDS File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }

        private void pngFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;
            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save PNG File";
            saveFileDialog.Filter = "PNG" + " File|*.png";
            saveFileDialog.DefaultExt = "png";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                dds_pictureBox.Image.Save(saveFileDialog.FileName);

                fileInfo_richTextBox.Clear();
                fileInfo_richTextBox.AppendText("PNG File has been saved to - " + saveFileDialog.FileName);
                MessageBox.Show("PNG File has been saved to - " + saveFileDialog.FileName, "Save PNG File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }

        private void wemFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (player != null) { player.Stop(); player.Stream.Dispose(); player.Dispose(); player = null; }

            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save WEM File";
            saveFileDialog.Filter = "WEM File|*" + ".wem";
            saveFileDialog.DefaultExt = "wem";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                string blobsetFilePath = Path.Combine(Global.gameInfo.GameLocation.Replace("data-0.blobset.pc", string.Empty), list[Global.fileIndex].FolderHash, list[Global.fileIndex].FileHash);
                IO.ReadWriteData(blobsetFilePath, saveFileDialog.FileName);
                fileInfo_richTextBox.Clear();
                fileInfo_richTextBox.AppendText("WEM File has been saved to - " + saveFileDialog.FileName);
                MessageBox.Show("WEM File has been saved to - " + saveFileDialog.FileName, "Save WEM File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }

        private void oggFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (player != null) { player.Stop(); player.Stream.Dispose(); player.Dispose(); player = null; }

            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save OGG File";
            saveFileDialog.Filter = "OGG File|*" + ".ogg";
            saveFileDialog.DefaultExt = "ogg";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                if (oggData != null)
                {
                    File.WriteAllBytes(saveFileDialog.FileName, oggData);
                    fileInfo_richTextBox.Clear();
                    fileInfo_richTextBox.AppendText("OGG File has been saved to - " + saveFileDialog.FileName);
                    MessageBox.Show("OGG File has been saved to - " + saveFileDialog.FileName, "Save OGG File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
                }
            }
            saveFileDialog.Dispose();
        }

        private void wavFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (player != null) { player.Stop(); player.Stream.Dispose(); player.Dispose(); player = null; }

            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save WAV File";
            saveFileDialog.Filter = "WAV File|*" + ".wav";
            saveFileDialog.DefaultExt = "wav";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                if (oggData != null)
                {
                    File.WriteAllBytes(saveFileDialog.FileName, wavData);
                    fileInfo_richTextBox.Clear();
                    fileInfo_richTextBox.AppendText("WAV File has been saved to - " + saveFileDialog.FileName);
                    MessageBox.Show("WAV File has been saved to - " + saveFileDialog.FileName, "Save WAV File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
                }
            }
            saveFileDialog.Dispose();
        }

        private void datFileToolStripMenuItem_Click(object sender, EventArgs e)
        {
            List<Structs.FileIndexInfo> list = (List<Structs.FileIndexInfo>)folder_treeView.SelectedNode.Tag;

            string path = list[Global.fileIndex].FilePath;

            saveFileDialog.Title = "Save DAT File";
            saveFileDialog.Filter = "DAT File|*" + ".dat";
            saveFileDialog.DefaultExt = "dat";
            saveFileDialog.FileName = Path.GetFileNameWithoutExtension(path);

            if (saveFileDialog.ShowDialog() == DialogResult.OK)
            {
                if (Path.GetDirectoryName(path) == @"unknown\mainuncompressed")
                {
                    int blobsetVersion = Global.gameInfo.BlobsetVersion;

                    if (blobsetVersion == (int)BlobsetVersion.v4)
                    {
                        string blobsetFilePath = Path.Combine(Global.gameInfo.GameLocation.Replace("data-0.blobset.pc", string.Empty), list[Global.fileIndex].FolderHash, list[Global.fileIndex].FileHash);
                        IO.ReadWriteData(blobsetFilePath, saveFileDialog.FileName);
                    }
                    else
                    {

                    }
                }

                fileInfo_richTextBox.Clear();
                fileInfo_richTextBox.AppendText("DAT File has been saved to - " + saveFileDialog.FileName);
                MessageBox.Show("DAT File has been saved to - " + saveFileDialog.FileName, "Save DAT File", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            saveFileDialog.Dispose();
        }

        private void dds_pictureBox_MouseClick(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
                flipImage_contextMenuStrip.Show(Cursor.Position);
        }

        private void flipImageToolStripMenuItem_Click(object sender, EventArgs e)
        {
            isFlipped = !isFlipped;
            dds_pictureBox.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
            dds_pictureBox.Refresh();
        }

        private void alphaToolStripMenuItem_Click(object sender, EventArgs e)
        {
            int blobsetVersion = Global.gameInfo.BlobsetVersion;
            Structs.DDSInfo ddsInfo = new();
            byte[] ddsData = blobsetVersion > 1 ? UI.GetDDSData_V3_V4(Global.filelist) : UI.GetDDSData_V1_V2(Global.filelist);
            if (ddsData == null) return;

            Bitmap bitmap = UI.DDStoBitmap(ddsData, alphaToolStripMenuItem.Checked, ref ddsInfo);

            if (bitmap != null)
            {
                dds_pictureBox.Image = bitmap;

                if (isFlipped)
                {
                    dds_pictureBox.Image.RotateFlip(RotateFlipType.Rotate180FlipX);
                    dds_pictureBox.Refresh();
                }
            }
        }

        private void fileInfo_richTextBox_SizeChanged(object sender, EventArgs e)
        {
            gameName_toolStripTextBox.Size = new Size(fileInfo_richTextBox.Size.Width, 21);
        }

        private void fileInfo_richTextBox_MouseClick(object sender, MouseEventArgs e)
        {
            SaveLog_contextMenuStrip.Show(Cursor.Position);
        }

        private void saveLogTotxtToolStripMenuItem_Click(object sender, EventArgs e)
        {
            File.WriteAllText(Global.currentPath + @"\FileLog.txt", fileInfo_richTextBox.Text);
            fileInfo_richTextBox.Clear();
            fileInfo_richTextBox.AppendText("File log saved to - " + Global.currentPath + @"\FileLog.txt");
            status_Label.Text = "File log saved to - " + Global.currentPath + @"\FileLog.txt";
        }

        private void Search()
        {
            bool valueResult = true;

            try
            {
                int filesCount = files_listView.Items.Count;
                int fileIndex = UI.getLVSelectedIndex(files_listView);
                int index = fileIndex + 1;

                if (fileIndex == 0)
                    index = 0;

                if (index == filesCount)
                    index = 0;

                for (int i = index; i < filesCount; i++)
                {
                    if (files_listView.Items[i].Text.Contains(searchToolStripTextBox.Text))
                    {
                        files_listView.Focus();
                        files_listView.Items[i].Focused = true;
                        files_listView.Items[i].Selected = true;
                        files_listView.EnsureVisible(i);
                        files_listView.Refresh();
                        valueResult = false;
                        break;
                    }
                }
                if (valueResult)
                {
                    files_listView.Focus();
                    files_listView.Items[0].Focused = true;
                    files_listView.Items[0].Selected = true;
                    files_listView.Refresh();
                    MessageBox.Show("Could not find the item", "No Results Found", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return;
                }
            }
            catch (Exception error)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + error, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
            }
        }

        private void searchToolStripTextBox_KeyDown(object sender, KeyEventArgs e)
        {
            if (files_listView.Items.Count > 0)
            {
                if (e.KeyValue == (char)Keys.Enter)
                    Search();
            }
        }

        private void searchToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (files_listView.Items.Count > 0)
                Search();
        }

        private void restoreBackupFilesToolStripMenuItem_Click(object sender, EventArgs e)
        {
            var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
            string platformExt = platformDetails["PlatformExt"];

            string backupFilePath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt, "backup");
            string gameLocation = Global.gameInfo.GameLocation.Replace("data-0.blobset.pc", "");

            string[] files = Utilities.DirectoryInfo(backupFilePath, "*");

            if (files.Length != 0)
            {
                foreach (string file in files)
                {
                    string? folder = Path.GetDirectoryName(file.Replace(backupFilePath, string.Empty));
                    string filePath = gameLocation + folder + @"\" + Path.GetFileName(file);

                    if (folder == string.Empty)
                        filePath = gameLocation + folder + Path.GetFileName(file);

                    if (File.Exists(filePath))
                    {
                        File.Delete(filePath);
                        File.Move(file, filePath);
                    }
                }

                UI.BlobsetHeaderData();
                MessageBox.Show("Backup files have been restored.", "Restore Backup Files", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
            }
            else
                MessageBox.Show("No files to restore.", "Restore Backup Files", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        private void resetBlobsetToolStripMenuItem_Click(object sender, EventArgs e)
        {
            Writer? bw = null;

            try
            {
                // Retrieve platform details
                var platformDetails = Utilities.GetPlatformInfo(Global.platforms);
                string platformExt = platformDetails["PlatformExt"];

                // Define the base path for game-related files
                string basePath = Path.Combine(Global.currentPath, "games", Global.gameInfo.GameName, platformExt);

                string filePath = Global.gameInfo.GameLocation.Replace("-0", "-1");

                string blobsetName = Path.GetFileName(Global.gameInfo.GameLocation);
                byte[] blobsetHeader = Utilities.ReadBlobsetHeader(blobsetName);

                if (blobsetHeader == null)
                {
                    string headerFilePath = Path.Combine(basePath, "backup", $"{blobsetName}.header");
                    string message = $"Can't find blobset header - {headerFilePath}";
                    string caption = "Blobset Header File Not Found";

                    MessageBox.Show(message, caption, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                // Restoring original blobset header 
                bw = new(Global.gameInfo.GameLocation, Endian.Little);
                bw.Write(blobsetHeader);
                if (bw != null) { bw.Close(); bw = null; }

                if (File.Exists(filePath))
                    File.Delete(filePath);

                MessageBox.Show("Blobset file has been reset to it's original state", "Resetting Blobset has finished", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Error occurred, report it to Wouldy : " + ex, "Hmm, something stuffed up :(", MessageBoxButtons.OK, MessageBoxIcon.Stop);
                return;
            }
            finally
            {
                if (bw != null) { bw.Close(); }
                UI.BlobsetHeaderData();
            }
        }

        private void MainForm_FormClosing(object sender, FormClosingEventArgs e)
        {
            if (Extract_bgw != null)
            {
                Extract_bgw.Dispose();

                foreach (string f in Directory.EnumerateFiles(Global.currentPath + @"\temp\", "*.*"))
                    File.Delete(f);

                Environment.Exit(Environment.ExitCode);
            }


            if (FileMapping_bgw != null)
            {
                FileMapping_bgw.Dispose();

                foreach (string f in Directory.EnumerateFiles(Global.currentPath + @"\temp\", "*.*"))
                    File.Delete(f);

                Environment.Exit(Environment.ExitCode);
            }

            if (Modify_bgw != null)
            {
                Modify_bgw.Dispose();
                Environment.Exit(Environment.ExitCode);
            }
        }

        private void aboutToolStripMenuItem_Click(object sender, EventArgs e)
        {
            About about = new();
            about.ShowDialog();
        }

        private void gameSelectionToolStripMenuItem_Click(object sender, EventArgs e)
        {
            Hide();
            GameSelection gameSelection = new();
            gameSelection.ShowDialog();
            Close();
        }

        private void exitToolStripMenuItem_Click(object sender, EventArgs e)
        {
            Environment.Exit(Environment.ExitCode);
        }
    }
}
